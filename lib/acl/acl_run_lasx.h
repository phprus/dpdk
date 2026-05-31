/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#include "acl_run_lsx.h"

static const rte_ymm_t ymm_match_mask = {
	.u32 = {
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
		RTE_ACL_NODE_MATCH,
	},
};

static const rte_ymm_t ymm_index_mask = {
	.u32 = {
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
		RTE_ACL_NODE_INDEX,
	},
};

static const rte_ymm_t ymm_shuffle_input = {
	.u32 = {
		0x00000000, 0x04040404, 0x08080808, 0x0c0c0c0c,
		0x00000000, 0x04040404, 0x08080808, 0x0c0c0c0c,
	},
};

static const rte_ymm_t ymm_ones_16 = {
	.u16 = {
		1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1,
	},
};

static const rte_ymm_t ymm_range_base = {
	.u32 = {
		0xffffff00, 0xffffff04, 0xffffff08, 0xffffff0c,
		0xffffff00, 0xffffff04, 0xffffff08, 0xffffff0c,
	},
};

/*
 * Process 8 transitions in parallel.
 * tr_lo contains low 32 bits for 8 transition.
 * tr_hi contains high 32 bits for 8 transition.
 * next_input contains up to 4 input bytes for 8 flows.
 */
static __rte_always_inline ymm_t
i32gather_epi32(const int32_t *ptr, ymm_t vindex, size_t scale)
{
	uint32_t alignas(16) index[8];
	ymm_t ret;
	__lasx_xvst(vindex, index, 0);
	scale = scale / sizeof(int32_t);

	ret = __lasx_xvreplgr2vr_w(ptr[index[0] * scale]);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[1] * scale], 1);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[2] * scale], 2);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[3] * scale], 3);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[4] * scale], 4);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[5] * scale], 5);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[6] * scale], 6);
	ret = __lasx_xvinsgr2vr_w(ret, ptr[index[7] * scale], 7);

	return ret;
}

static __rte_always_inline ymm_t
transition8(ymm_t next_input, const uint64_t *trans, ymm_t *tr_lo, ymm_t *tr_hi)
{
	const int32_t *tr;
	ymm_t addr;

	tr = (const int32_t *)(uintptr_t)trans;

	/* Calculate the address (array index) for all 8 transitions. */
	ACL_TR_CALC_ADDR(lasx_xv, 256, addr, ymm_index_mask.y, next_input,
		ymm_shuffle_input.y, ymm_ones_16.y, ymm_range_base.y,
		*tr_lo, *tr_hi);

	/* load lower 32 bits of 8 transactions at once. */
	*tr_lo = i32gather_epi32(tr, addr, sizeof(trans[0]));

	next_input = __lasx_xvsrli_w(next_input, CHAR_BIT);

	/* load high 32 bits of 8 transactions at once. */
	*tr_hi = i32gather_epi32(tr + 1, addr, sizeof(trans[0]));

	return next_input;
}

/*
 * Process matches for  8 flows.
 * tr_lo contains low 32 bits for 8 transition.
 * tr_hi contains high 32 bits for 8 transition.
 */
static inline void
acl_process_matches_lasxx8(const struct rte_acl_ctx *ctx,
	struct parms *parms, struct acl_flow_data *flows, uint32_t slot,
	ymm_t matches, ymm_t *tr_lo, ymm_t *tr_hi)
{
	ymm_t t0, t1;
	ymm_t lo, hi;
	xmm_t l0, l1;
	uint32_t i;
	uint64_t tr[MAX_SEARCHES_LSX8];

	l1 = __lasx_extract_128_hi(*tr_lo);
	l0 = __lasx_extract_128_lo(*tr_lo);

	for (i = 0; i != RTE_DIM(tr) / 2; i++) {

		/*
		 * Extract low 32bits of each transition.
		 * That's enough to process the match.
		 */
		tr[i] = (uint32_t)__lsx_vpickve2gr_w(l0, 0);
		tr[i + 4] = (uint32_t)__lsx_vpickve2gr_w(l1, 0);

		l0 = __lsx_vbsrl_v(l0, sizeof(uint32_t));
		l1 = __lsx_vbsrl_v(l1, sizeof(uint32_t));

		tr[i] = acl_match_check(tr[i], slot + i,
			ctx, parms, flows, resolve_priority_lsx);
		tr[i + 4] = acl_match_check(tr[i + 4], slot + i + 4,
			ctx, parms, flows, resolve_priority_lsx);
	}

	/* Collect new transitions into 2 YMM registers. */
	t0 = lasx_set_d(tr[5], tr[4], tr[1], tr[0]);
	t1 = lasx_set_d(tr[7], tr[6], tr[3], tr[2]);

	/* For each transition: put low 32 into tr_lo and high 32 into tr_hi */
	ACL_TR_HILO(lasx_xv, t0, t1, lo, hi);

	/* Keep transitions with NOMATCH intact. */
	*tr_lo = __lasx_xvbitsel_v(*tr_lo, lo, matches);
	*tr_hi = __lasx_xvbitsel_v(*tr_hi, hi, matches);
}

static inline void
acl_match_check_lasxx8(const struct rte_acl_ctx *ctx, struct parms *parms,
	struct acl_flow_data *flows, uint32_t slot,
	ymm_t *tr_lo, ymm_t *tr_hi, ymm_t match_mask)
{
	ymm_t matches, temp;

	/* test for match node */
	temp = __lasx_xvand_v(match_mask, *tr_lo);
	matches = __lasx_xvseq_w(temp, match_mask);

	while (__lasx_xbnz_v(matches)) {
		acl_process_matches_lasxx8(ctx, parms, flows, slot,
			matches, tr_lo, tr_hi);
		temp = __lasx_xvand_v(match_mask, *tr_lo);
		matches = __lasx_xvseq_w(temp, match_mask);
	}
}

/*
 * Execute trie traversal for up to 16 flows in parallel.
 */
static inline int
search_lasxx16(const struct rte_acl_ctx *ctx, const uint8_t **data,
	uint32_t *results, uint32_t total_packets, uint32_t categories)
{
	uint32_t n;
	struct acl_flow_data flows;
	uint64_t index_array[MAX_SEARCHES_LASX16];
	struct completion cmplt[MAX_SEARCHES_LASX16];
	struct parms parms[MAX_SEARCHES_LASX16];
	ymm_t input[2], tr_lo[2], tr_hi[2];
	ymm_t t0, t1;

	acl_set_flow(&flows, cmplt, RTE_DIM(cmplt), data, results,
		total_packets, categories, ctx->trans_table);

	for (n = 0; n < RTE_DIM(cmplt); n++)
		index_array[n] = acl_start_next_trie(&flows, parms, n, ctx);

	t0 = lasx_set_d(index_array[5], index_array[4],
		index_array[1], index_array[0]);
	t1 = lasx_set_d(index_array[7], index_array[6],
		index_array[3], index_array[2]);

	ACL_TR_HILO(lasx_xv, t0, t1, tr_lo[0], tr_hi[0]);

	t0 = lasx_set_d(index_array[13], index_array[12],
		index_array[9], index_array[8]);
	t1 = lasx_set_d(index_array[15], index_array[14],
		index_array[11], index_array[10]);

	ACL_TR_HILO(lasx_xv, t0, t1, tr_lo[1], tr_hi[1]);

	 /* Check for any matches. */
	acl_match_check_lasxx8(ctx, parms, &flows, 0, &tr_lo[0], &tr_hi[0],
		ymm_match_mask.y);
	acl_match_check_lasxx8(ctx, parms, &flows, 8, &tr_lo[1], &tr_hi[1],
		ymm_match_mask.y);

	while (flows.started > 0) {

		uint32_t in[MAX_SEARCHES_LSX8];

		/* Gather 4 bytes of input data for first 8 flows. */
		in[0] = GET_NEXT_4BYTES(parms, 0);
		in[4] = GET_NEXT_4BYTES(parms, 4);
		in[1] = GET_NEXT_4BYTES(parms, 1);
		in[5] = GET_NEXT_4BYTES(parms, 5);
		in[2] = GET_NEXT_4BYTES(parms, 2);
		in[6] = GET_NEXT_4BYTES(parms, 6);
		in[3] = GET_NEXT_4BYTES(parms, 3);
		in[7] = GET_NEXT_4BYTES(parms, 7);
		input[0] = lasx_set_w(in[7], in[6], in[5], in[4],
			in[3], in[2], in[1], in[0]);

		/* Gather 4 bytes of input data for last 8 flows. */
		in[0] = GET_NEXT_4BYTES(parms, 8);
		in[4] = GET_NEXT_4BYTES(parms, 12);
		in[1] = GET_NEXT_4BYTES(parms, 9);
		in[5] = GET_NEXT_4BYTES(parms, 13);
		in[2] = GET_NEXT_4BYTES(parms, 10);
		in[6] = GET_NEXT_4BYTES(parms, 14);
		in[3] = GET_NEXT_4BYTES(parms, 11);
		in[7] = GET_NEXT_4BYTES(parms, 15);
		input[1] = lasx_set_w(in[7], in[6], in[5], in[4],
			in[3], in[2], in[1], in[0]);

		input[0] = transition8(input[0], flows.trans,
			&tr_lo[0], &tr_hi[0]);
		input[1] = transition8(input[1], flows.trans,
			&tr_lo[1], &tr_hi[1]);

		input[0] = transition8(input[0], flows.trans,
			&tr_lo[0], &tr_hi[0]);
		input[1] = transition8(input[1], flows.trans,
			&tr_lo[1], &tr_hi[1]);

		input[0] = transition8(input[0], flows.trans,
			&tr_lo[0], &tr_hi[0]);
		input[1] = transition8(input[1], flows.trans,
			&tr_lo[1], &tr_hi[1]);

		input[0] = transition8(input[0], flows.trans,
			&tr_lo[0], &tr_hi[0]);
		input[1] = transition8(input[1], flows.trans,
			&tr_lo[1], &tr_hi[1]);

		 /* Check for any matches. */
		acl_match_check_lasxx8(ctx, parms, &flows, 0,
			&tr_lo[0], &tr_hi[0], ymm_match_mask.y);
		acl_match_check_lasxx8(ctx, parms, &flows, 8,
			&tr_lo[1], &tr_hi[1], ymm_match_mask.y);
	}

	return 0;
}
