/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef _RTE_MEMBER_LOONGARCH_H_
#define _RTE_MEMBER_LOONGARCH_H_

#include <rte_vect.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__loongarch_asx)

static inline int
update_entry_search_lasx(uint32_t bucket_id, member_sig_t tmp_sig,
		struct member_ht_bucket *buckets,
		member_set_t set_id)
{
	uint32_t hitmask = lasx_xvmskltz2gr_b(__lasx_xvseq_h(
		__lasx_xvld((__m256i const *)buckets[bucket_id].sigs, 0),
		__lasx_xvreplgr2vr_h(tmp_sig)));
	if (hitmask) {
		uint32_t hit_idx = rte_ctz32(hitmask) >> 1;
		buckets[bucket_id].sets[hit_idx] = set_id;
		return 1;
	}
	return 0;
}

static inline int
search_bucket_single_lasx(uint32_t bucket_id, member_sig_t tmp_sig,
		struct member_ht_bucket *buckets,
		member_set_t *set_id)
{
	uint32_t hitmask = lasx_xvmskltz2gr_b(__lasx_xvseq_h(
		__lasx_xvld((__m256i const *)buckets[bucket_id].sigs, 0),
		__lasx_xvreplgr2vr_h(tmp_sig)));
	while (hitmask) {
		uint32_t hit_idx = rte_ctz32(hitmask) >> 1;
		if (buckets[bucket_id].sets[hit_idx] != RTE_MEMBER_NO_MATCH) {
			*set_id = buckets[bucket_id].sets[hit_idx];
			return 1;
		}
		hitmask &= ~(3U << ((hit_idx) << 1));
	}
	return 0;
}

static inline void
search_bucket_multi_lasx(uint32_t bucket_id, member_sig_t tmp_sig,
				struct member_ht_bucket *buckets,
				uint32_t *counter,
				uint32_t match_per_key,
				member_set_t *set_id)
{
	uint32_t hitmask = lasx_xvmskltz2gr_b(__lasx_xvseq_h(
		__lasx_xvld((__m256i const *)buckets[bucket_id].sigs, 0),
		__lasx_xvreplgr2vr_h(tmp_sig)));
	while (hitmask) {
		uint32_t hit_idx = rte_ctz32(hitmask) >> 1;
		if (buckets[bucket_id].sets[hit_idx] != RTE_MEMBER_NO_MATCH) {
			set_id[*counter] = buckets[bucket_id].sets[hit_idx];
			(*counter)++;
			if (*counter >= match_per_key)
				return;
		}
		hitmask &= ~(3U << ((hit_idx) << 1));
	}
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* _RTE_MEMBER_LOONGARCH_H_ */
