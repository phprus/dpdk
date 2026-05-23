/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2015 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#include <rte_vect.h>

/* Function to compare multiple of 16 byte keys */
static inline int
rte_hash_k16_cmp_eq(const void *key1, const void *key2, size_t key_len __rte_unused)
{
	const __m128i k1 = __lsx_vld(key1, 0);
	const __m128i k2 = __lsx_vld(key2, 0);
	const __m128i x = __lsx_vxor_v(k1, k2);

	return __lsx_bnz_v(x);
}

static inline int
rte_hash_k32_cmp_eq(const void *key1, const void *key2, size_t key_len __rte_unused)
{
	return rte_hash_k16_cmp_eq(key1, key2, 16) |
		rte_hash_k16_cmp_eq((const uint8_t *) key1 + 16,
				    (const uint8_t *) key2 + 16, 16);
}
