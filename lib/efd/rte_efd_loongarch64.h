/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016-2017 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

/* rte_efd_loongarch64.h
 * This file holds all LoongArch64 specific EFD functions
 */
#include <rte_vect.h>

static inline efd_value_t
efd_lookup_internal_lasx(const efd_hashfunc_t *group_hash_idx,
		const efd_lookuptbl_t *group_lookup_table,
		const uint32_t hash_val_a, const uint32_t hash_val_b)
{
#ifdef __loongarch_asx
	efd_value_t value = 0;
	uint32_t i = 0;
	__m256i vhash_val_a = __lasx_xvreplgr2vr_w(hash_val_a);
	__m256i vhash_val_b = __lasx_xvreplgr2vr_w(hash_val_b);

	for (; i < RTE_EFD_VALUE_NUM_BITS; i += 8) {
		__m256i vhash_idx = lasx_xvsllwil_wu_hu_128(__lsx_vld(
				&group_hash_idx[i], 0), 0);
		__m256i vlookup_table = lasx_xvsllwil_wu_hu_128(__lsx_vld(
				&group_lookup_table[i], 0), 0);
		__m256i vhash = __lasx_xvadd_w(vhash_val_a,
				__lasx_xvmul_w(vhash_idx, vhash_val_b));
		__m256i vbucket_idx = __lasx_xvsrli_w(vhash,
				EFD_LOOKUPTBL_SHIFT);
		__m256i vresult = __lasx_xvand_v(__lasx_xvsrl_w(
				vlookup_table, vbucket_idx),
				__lasx_xvslei_wu(vbucket_idx, 31));

		value |= (lasx_xvmskltz2gr_w(__lasx_xvslli_w(vresult, 31))
			& ((1 << (RTE_EFD_VALUE_NUM_BITS - i)) - 1)) << i;
	}

	return value;
#else
	RTE_SET_USED(group_hash_idx);
	RTE_SET_USED(group_lookup_table);
	RTE_SET_USED(hash_val_a);
	RTE_SET_USED(hash_val_b);
	/* Return dummy value, only to avoid compilation breakage */
	return 0;
#endif
}
