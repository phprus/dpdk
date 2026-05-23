/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 * Copyright(c) 2018-2024 Arm Limited
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef COMPARE_SIGNATURES_LOONGARCH64_H
#define COMPARE_SIGNATURES_LOONGARCH64_H

#include <inttypes.h>

#include <rte_common.h>
#include <rte_vect.h>

#include "rte_cuckoo_hash.h"

/* LoongArch64 version uses a sparsely packed hitmask buffer: every other bit is padding. */
#define DENSE_HASH_BULK_LOOKUP 0

static inline void
compare_signatures_sparse(uint32_t *prim_hash_matches, uint32_t *sec_hash_matches,
			const struct rte_hash_bucket *prim_bkt,
			const struct rte_hash_bucket *sec_bkt,
			uint16_t sig,
			enum rte_hash_sig_compare_function sig_cmp_fn)
{
	unsigned int i;

	/* For match mask the first bit of every two bits indicates the match */
	switch (sig_cmp_fn) {
#if defined(__loongarch_sx) && RTE_HASH_BUCKET_ENTRIES <= 8
	case RTE_HASH_COMPARE_LSX:
		/* Compare all signatures in the bucket */
		*prim_hash_matches = __lsx_vpickve2gr_w(__lsx_vmskltz_b(__lsx_vseq_h(
			__lsx_vld(prim_bkt->sig_current, 0), __lsx_vreplgr2vr_h(sig))), 0);
		/* Extract the even-index bits only */
		*prim_hash_matches &= 0x5555;
		/* Compare all signatures in the bucket */
		*sec_hash_matches = __lsx_vpickve2gr_w(__lsx_vmskltz_b(__lsx_vseq_h(
			__lsx_vld(sec_bkt->sig_current, 0), __lsx_vreplgr2vr_h(sig))), 0);
		/* Extract the even-index bits only */
		*sec_hash_matches &= 0x5555;
		break;
#endif
	default:
		for (i = 0; i < RTE_HASH_BUCKET_ENTRIES; i++) {
			*prim_hash_matches |= (sig == prim_bkt->sig_current[i]) << (i << 1);
			*sec_hash_matches |= (sig == sec_bkt->sig_current[i]) << (i << 1);
		}
	}
}
#endif /* COMPARE_SIGNATURES_LOONGARCH64_H */
