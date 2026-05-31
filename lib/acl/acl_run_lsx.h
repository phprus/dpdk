/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#include "acl_run.h"
#include "acl_vect.h"

enum {
	SHUFFLE32_SWAP64 = 0x4e,
};

static const rte_xmm_t xmm_shuffle_input = {
	.u32 = {0x00000000, 0x04040404, 0x08080808, 0x0c0c0c0c},
};

static const rte_xmm_t xmm_ones_16 = {
	.u16 = {1, 1, 1, 1, 1, 1, 1, 1},
};

static const rte_xmm_t xmm_match_mask = {
	.u32 = {
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
	},
};

static const rte_xmm_t xmm_index_mask = {
	.u32 = {
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
	},
};

static const rte_xmm_t xmm_range_base = {
	.u32 = {
		0xffffff00, 0xffffff04, 0xffffff08, 0xffffff0c,
	},
};

/*
 * Resolve priority for multiple results (lsx version).
 * This consists comparing the priority of the current traversal with the
 * running set of results for the packet.
 * For each result, keep a running array of the result (rule number) and
 * its priority for each category.
 */
static inline void
resolve_priority_lsx(uint64_t transition, int n, const struct rte_acl_ctx *ctx,
	struct parms *parms, const struct rte_acl_match_results *p,
	uint32_t categories)
{
	uint32_t x;
	xmm_t results, priority, results1, priority1, selector;
	xmm_t *saved_results, *saved_priority;

	for (x = 0; x < categories; x += RTE_ACL_RESULTS_MULTIPLIER) {

		saved_results = (xmm_t *)(&parms[n].cmplt->results[x]);
		saved_priority =
			(xmm_t *)(&parms[n].cmplt->priority[x]);

		/* get results and priorities for completed trie */
		results = __lsx_vld(&p[transition].results[x], 0);
		priority = __lsx_vld(&p[transition].priority[x], 0);

		/* if this is not the first completed trie */
		if (parms[n].cmplt->count != ctx->num_tries) {

			/* get running best results and their priorities */
			results1 = __lsx_vld(saved_results, 0);
			priority1 = __lsx_vld(saved_priority, 0);

			/* select results that are highest priority */
			selector = __lsx_vslt_w(priority, priority1);
			results = __lsx_vbitsel_v(results, results1, selector);
			priority = __lsx_vbitsel_v(priority, priority1,
				selector);
		}

		/* save running best results and their priorities */
		__lsx_vst(results, saved_results, 0);
		__lsx_vst(priority, saved_priority, 0);
	}
}

/*
 * Extract transitions from an XMM register and check for any matches
 */
static void
acl_process_matches(xmm_t *indices, int slot, const struct rte_acl_ctx *ctx,
	struct parms *parms, struct acl_flow_data *flows)
{
	uint64_t transition1, transition2;

	/* extract transition from low 64 bits. */
	transition1 = __lsx_vpickve2gr_d(*indices, 0);

	/* extract transition from high 64 bits. */
	*indices = __lsx_vshuf4i_w(*indices, SHUFFLE32_SWAP64);
	transition2 = __lsx_vpickve2gr_d(*indices, 0);

	transition1 = acl_match_check(transition1, slot, ctx,
		parms, flows, resolve_priority_lsx);
	transition2 = acl_match_check(transition2, slot + 1, ctx,
		parms, flows, resolve_priority_lsx);

	/* update indices with new transitions. */
	*indices = lsx_set_d(transition2, transition1);
}

/*
 * Check for any match in 4 transitions (contained in 2 LSX registers)
 */
static __rte_always_inline void
acl_match_check_x4(int slot, const struct rte_acl_ctx *ctx, struct parms *parms,
	struct acl_flow_data *flows, xmm_t *indices1, xmm_t *indices2,
	xmm_t match_mask)
{
	xmm_t temp;

	/* put low 32 bits of each transition into one register */
	temp = __lsx_vpermi_w(*indices2, *indices1, 0x88);
	/* test for match node */
	temp = __lsx_vand_v(match_mask, temp);

	while (__lsx_bnz_v(temp)) {
		acl_process_matches(indices1, slot, ctx, parms, flows);
		acl_process_matches(indices2, slot + 2, ctx, parms, flows);

		temp = __lsx_vpermi_w(*indices2, *indices1, 0x88);
		temp = __lsx_vand_v(match_mask, temp);
	}
}

/*
 * Process 4 transitions (in 2 XMM registers) in parallel
 */
static __rte_always_inline xmm_t
transition4(xmm_t next_input, const uint64_t *trans,
	xmm_t *indices1, xmm_t *indices2)
{
	xmm_t addr, tr_lo, tr_hi;
	uint64_t trans0, trans2;

	/* Shuffle low 32 into tr_lo and high 32 into tr_hi */
	ACL_TR_HILO(lsx_v, *indices1, *indices2, tr_lo, tr_hi);

	 /* Calculate the address (array index) for all 4 transitions. */
	ACL_TR_CALC_ADDR(lsx_v, 128, addr, xmm_index_mask.x, next_input,
		xmm_shuffle_input.x, xmm_ones_16.x, xmm_range_base.x,
		tr_lo, tr_hi);

	 /* Gather 64 bit transitions and pack back into 2 registers. */

	trans0 = trans[__lsx_vpickve2gr_w(addr, 0)];

	/* get slot 2 */
	trans2 = trans[__lsx_vpickve2gr_w(addr, 2)];

	/* get slot 1 */
	*indices1 = lsx_set_d(trans[__lsx_vpickve2gr_w(addr, 1)], trans0);

	/* get slot 3 */
	*indices2 = lsx_set_d(trans[__lsx_vpickve2gr_w(addr, 3)], trans2);

	return __lsx_vsrli_w(next_input, CHAR_BIT);
}

/*
 * Execute trie traversal with 8 traversals in parallel
 */
static inline int
search_lsx_8(const struct rte_acl_ctx *ctx, const uint8_t **data,
	uint32_t *results, uint32_t total_packets, uint32_t categories)
{
	int n;
	struct acl_flow_data flows;
	uint64_t index_array[MAX_SEARCHES_LSX8];
	struct completion cmplt[MAX_SEARCHES_LSX8];
	struct parms parms[MAX_SEARCHES_LSX8];
	xmm_t input0, input1;
	xmm_t indices1, indices2, indices3, indices4;

	acl_set_flow(&flows, cmplt, RTE_DIM(cmplt), data, results,
		total_packets, categories, ctx->trans_table);

	for (n = 0; n < MAX_SEARCHES_LSX8; n++)
		index_array[n] = acl_start_next_trie(&flows, parms, n, ctx);

	/*
	 * indices1 contains index_array[0,1]
	 * indices2 contains index_array[2,3]
	 * indices3 contains index_array[4,5]
	 * indices4 contains index_array[6,7]
	 */

	indices1 = __lsx_vld(&index_array[0], 0);
	indices2 = __lsx_vld(&index_array[2], 0);

	indices3 = __lsx_vld(&index_array[4], 0);
	indices4 = __lsx_vld(&index_array[6], 0);

	 /* Check for any matches. */
	acl_match_check_x4(0, ctx, parms, &flows,
		&indices1, &indices2, xmm_match_mask.x);
	acl_match_check_x4(4, ctx, parms, &flows,
		&indices3, &indices4, xmm_match_mask.x);

	while (flows.started > 0) {

		/* Gather 4 bytes of input data for each stream. */
		input0 = __lsx_vreplgr2vr_w(GET_NEXT_4BYTES(parms, 0));
		input1 = __lsx_vreplgr2vr_w(GET_NEXT_4BYTES(parms, 4));

		input0 = __lsx_vinsgr2vr_w(input0, GET_NEXT_4BYTES(parms, 1), 1);
		input1 = __lsx_vinsgr2vr_w(input1, GET_NEXT_4BYTES(parms, 5), 1);

		input0 = __lsx_vinsgr2vr_w(input0, GET_NEXT_4BYTES(parms, 2), 2);
		input1 = __lsx_vinsgr2vr_w(input1, GET_NEXT_4BYTES(parms, 6), 2);

		input0 = __lsx_vinsgr2vr_w(input0, GET_NEXT_4BYTES(parms, 3), 3);
		input1 = __lsx_vinsgr2vr_w(input1, GET_NEXT_4BYTES(parms, 7), 3);

		/* Process the 4 bytes of input on each stream. */

		input0 = transition4(input0, flows.trans,
			&indices1, &indices2);
		input1 = transition4(input1, flows.trans,
			&indices3, &indices4);

		input0 = transition4(input0, flows.trans,
			&indices1, &indices2);
		input1 = transition4(input1, flows.trans,
			&indices3, &indices4);

		input0 = transition4(input0, flows.trans,
			&indices1, &indices2);
		input1 = transition4(input1, flows.trans,
			&indices3, &indices4);

		input0 = transition4(input0, flows.trans,
			&indices1, &indices2);
		input1 = transition4(input1, flows.trans,
			&indices3, &indices4);

		 /* Check for any matches. */
		acl_match_check_x4(0, ctx, parms, &flows,
			&indices1, &indices2, xmm_match_mask.x);
		acl_match_check_x4(4, ctx, parms, &flows,
			&indices3, &indices4, xmm_match_mask.x);
	}

	return 0;
}

/*
 * Execute trie traversal with 4 traversals in parallel
 */
static inline int
search_lsx_4(const struct rte_acl_ctx *ctx, const uint8_t **data,
	 uint32_t *results, int total_packets, uint32_t categories)
{
	int n;
	struct acl_flow_data flows;
	uint64_t index_array[MAX_SEARCHES_LSX4];
	struct completion cmplt[MAX_SEARCHES_LSX4];
	struct parms parms[MAX_SEARCHES_LSX4];
	xmm_t input, indices1, indices2;

	acl_set_flow(&flows, cmplt, RTE_DIM(cmplt), data, results,
		total_packets, categories, ctx->trans_table);

	for (n = 0; n < MAX_SEARCHES_LSX4; n++)
		index_array[n] = acl_start_next_trie(&flows, parms, n, ctx);

	indices1 = __lsx_vld(&index_array[0], 0);
	indices2 = __lsx_vld(&index_array[2], 0);

	/* Check for any matches. */
	acl_match_check_x4(0, ctx, parms, &flows,
		&indices1, &indices2, xmm_match_mask.x);

	while (flows.started > 0) {

		/* Gather 4 bytes of input data for each stream. */
		input = __lsx_vreplgr2vr_w(GET_NEXT_4BYTES(parms, 0));
		input = __lsx_vinsgr2vr_w(input, GET_NEXT_4BYTES(parms, 1), 1);
		input = __lsx_vinsgr2vr_w(input, GET_NEXT_4BYTES(parms, 2), 2);
		input = __lsx_vinsgr2vr_w(input, GET_NEXT_4BYTES(parms, 3), 3);

		/* Process the 4 bytes of input on each stream. */
		input = transition4(input, flows.trans, &indices1, &indices2);
		input = transition4(input, flows.trans, &indices1, &indices2);
		input = transition4(input, flows.trans, &indices1, &indices2);
		input = transition4(input, flows.trans, &indices1, &indices2);

		/* Check for any matches. */
		acl_match_check_x4(0, ctx, parms, &flows,
			&indices1, &indices2, xmm_match_mask.x);
	}

	return 0;
}
