/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef __L3FWD_EM_HLM_LSX_H__
#define __L3FWD_EM_HLM_LSX_H__

#include "l3fwd_lsx.h"

static __rte_always_inline void
get_ipv4_5tuple(struct rte_mbuf *m0, __m128i mask0,
		union ipv4_5tuple_host *key)
{
	 __m128i tmpdata0 = __lsx_vld(
			rte_pktmbuf_mtod_offset(m0, __m128i *,
				sizeof(struct rte_ether_hdr) +
				offsetof(struct rte_ipv4_hdr, time_to_live)), 0);

	key->xmm = __lsx_vand_v(tmpdata0, mask0);
}

static inline void
get_ipv6_5tuple(struct rte_mbuf *m0, __m128i mask0,
		__m128i mask1, union ipv6_5tuple_host *key)
{
	__m128i tmpdata0 = __lsx_vld(
			rte_pktmbuf_mtod_offset(m0, __m128i *,
				sizeof(struct rte_ether_hdr) +
				offsetof(struct rte_ipv6_hdr, payload_len)), 0);

	__m128i tmpdata1 = __lsx_vld(
			rte_pktmbuf_mtod_offset(m0, __m128i *,
				sizeof(struct rte_ether_hdr) +
				offsetof(struct rte_ipv6_hdr, payload_len) +
				sizeof(__m128i)), 0);

	__m128i tmpdata2 = __lsx_vld(
			rte_pktmbuf_mtod_offset(m0, __m128i *,
				sizeof(struct rte_ether_hdr) +
				offsetof(struct rte_ipv6_hdr, payload_len) +
				sizeof(__m128i) + sizeof(__m128i)), 0);

	key->xmm[0] = __lsx_vand_v(tmpdata0, mask0);
	key->xmm[1] = tmpdata1;
	key->xmm[2] = __lsx_vand_v(tmpdata2, mask1);
}
#endif /* __L3FWD_EM_HLM_LSX_H__ */
