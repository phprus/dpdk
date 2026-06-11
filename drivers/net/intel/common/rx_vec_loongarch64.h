/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2025 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef _COMMON_INTEL_RX_VEC_LOONGARCH64_H_
#define _COMMON_INTEL_RX_VEC_LOONGARCH64_H_

#include <stdint.h>

#include <ethdev_driver.h>
#include <rte_cpuflags.h>
#include <rte_io.h>

#include "rx.h"

enum ci_rx_vec_level {
	CI_RX_VEC_LEVEL_LSX = 0,
	CI_RX_VEC_LEVEL_LASX,
};

static inline int
_ci_rxq_rearm_get_bufs(struct ci_rx_queue *rxq)
{
	struct rte_mbuf **rxp = &rxq->sw_ring[rxq->rxrearm_start].mbuf;
	const uint16_t rearm_thresh = CI_VPMD_RX_REARM_THRESH;
	volatile union ci_rx_desc *rxdp;
	int i;

	rxdp = &rxq->rx_ring[rxq->rxrearm_start];

	if (rte_mbuf_raw_alloc_bulk(rxq->mp, rxp, rearm_thresh) < 0) {
		if (rxq->rxrearm_nb + rearm_thresh >= rxq->nb_rx_desc) {
			const __m128i zero = __lsx_vldi(0);

			for (i = 0; i < CI_VPMD_DESCS_PER_LOOP; i++) {
				rxp[i] = &rxq->fake_mbuf;
				__lsx_vst(zero, RTE_CAST_PTR(__m128i *, &rxdp[i]), 0);
			}
		}
		rte_eth_devices[rxq->port_id].data->rx_mbuf_alloc_failed += rearm_thresh;
		return -1;
	}
	return 0;
}

/**
 * Reformat data from mbuf to descriptor for one RX descriptor, using LSX instruction set.
 *
 * @param mhdr pointer to first 16 bytes of mbuf header
 * @return 16-byte register in descriptor format.
 */
static __rte_always_inline __m128i
_ci_rxq_rearm_desc_lsx(const __m128i *mhdr)
{
	const __m128i hdroom = __lsx_vreplgr2vr_d(RTE_PKTMBUF_HEADROOM);
	const __m128i zero = __lsx_vldi(0);

	/* add headroom to address values */
	__m128i reg = __lsx_vadd_d(*mhdr, hdroom);

#if RTE_IOVA_IN_MBUF
	/* expect buf_addr (low 64 bit) and buf_iova (high 64bit) */
	RTE_BUILD_BUG_ON(offsetof(struct rte_mbuf, buf_iova) !=
			 offsetof(struct rte_mbuf, buf_addr) + 8);
	/* move IOVA to Packet Buffer Address, erase Header Buffer Address */
	reg = __lsx_vilvh_d(zero, reg);
#else
	RTE_BUILD_BUG_ON(offsetof(struct rte_mbuf, buf_addr) != 0);
	/* erase Header Buffer Address */
	reg = __lsx_vilvl_d(zero, reg);
#endif
	return reg;
}

static __rte_always_inline void
_ci_rxq_rearm_lsx(struct ci_rx_queue *rxq)
{
	const uint16_t rearm_thresh = CI_VPMD_RX_REARM_THRESH;
	struct ci_rx_entry *rxp = &rxq->sw_ring[rxq->rxrearm_start];
	/* LSX writes 16-bytes regardless of descriptor size */
	const uint8_t desc_per_reg = 1;
	const uint8_t desc_per_iter = desc_per_reg * 2;
	volatile union ci_rx_desc *rxdp;
	int i;

	rxdp = &rxq->rx_ring[rxq->rxrearm_start];

	/* Initialize the mbufs in vector, process 2 mbufs in one loop */
	for (i = 0; i < rearm_thresh;
			i += desc_per_iter,
			rxp += desc_per_iter,
			rxdp += desc_per_iter) {
		const __m128i reg0 = _ci_rxq_rearm_desc_lsx(
				RTE_CAST_PTR(const __m128i *, rxp[0].mbuf));
		const __m128i reg1 = _ci_rxq_rearm_desc_lsx(
				RTE_CAST_PTR(const __m128i *, rxp[1].mbuf));

		/* flush descriptors */
		__lsx_vst(reg0, RTE_CAST_PTR(__m128i *, &rxdp[0]), 0);
		__lsx_vst(reg1, RTE_CAST_PTR(__m128i *, &rxdp[desc_per_reg]), 0);
	}
}

#ifdef __loongarch_asx
/**
 * Reformat data from mbuf to descriptor for one RX descriptor, using LSX instruction set.
 *
 * Note that for 32-byte descriptors, the second parameter must be zeroed out.
 *
 * @param mhdr0 pointer to first 16-bytes of 1st mbuf header.
 * @param mhdr1 pointer to first 16-bytes of 2nd mbuf header.
 *
 * @return 32-byte register with two 16-byte descriptors in it.
 */
static __rte_always_inline __m256i
_ci_rxq_rearm_desc_lasx(const __m128i *mhdr0, const __m128i *mhdr1)
{
	const __m256i hdr_room = __lasx_xvreplgr2vr_d(RTE_PKTMBUF_HEADROOM);
	const __m256i zero = __lasx_xvldi(0);

	/* merge by casting 0 to 256-bit and inserting 1 into the high lanes */
	__m256i reg = __lasx_concat_128(*mhdr0, *mhdr1);

	/* add headroom to address values */
	reg = __lasx_xvadd_d(reg, hdr_room);

#if RTE_IOVA_IN_MBUF
	/* load buf_addr(lo 64bit) and buf_iova(hi 64bit) */
	RTE_BUILD_BUG_ON(offsetof(struct rte_mbuf, buf_iova) !=
			offsetof(struct rte_mbuf, buf_addr) + 8);
	/* extract IOVA addr into Packet Buffer Address, erase Header Buffer Address */
	reg = __lasx_xvilvh_d(zero, reg);
#else
	RTE_BUILD_BUG_ON(offsetof(struct rte_mbuf, buf_addr) != 0);
	/* erase Header Buffer Address */
	reg = __lasx_xvilvl_d(zero, reg);
#endif
	return reg;
}

static __rte_always_inline void
_ci_rxq_rearm_lasx(struct ci_rx_queue *rxq)
{
	struct ci_rx_entry *rxp = &rxq->sw_ring[rxq->rxrearm_start];
	const uint16_t rearm_thresh = CI_VPMD_RX_REARM_THRESH;
	/* how many descriptors can fit into a register */
	const uint8_t desc_per_reg = sizeof(__m256i) / sizeof(union ci_rx_desc);
	/* how many descriptors can fit into one loop iteration */
	const uint8_t desc_per_iter = desc_per_reg * 2;
	volatile union ci_rx_desc *rxdp;
	int i;

	rxdp = &rxq->rx_ring[rxq->rxrearm_start];

	/* Initialize the mbufs in vector, process 2 or 4 mbufs in one loop */
	for (i = 0; i < rearm_thresh;
			i += desc_per_iter,
			rxp += desc_per_iter,
			rxdp += desc_per_iter) {
		__m256i reg0, reg1;

		if (desc_per_iter == 2) {
			/* no need to call LSX version as we only need two descriptors */
			reg0 = __lasx_cast_128(
				_ci_rxq_rearm_desc_lsx(
					RTE_CAST_PTR(const __m128i *, rxp[0].mbuf)));
			reg1 = __lasx_cast_128(
				_ci_rxq_rearm_desc_lsx(
					RTE_CAST_PTR(const __m128i *, rxp[1].mbuf)));
		} else {
			/* 16 byte descriptor times four */
			reg0 = _ci_rxq_rearm_desc_lasx(
					RTE_CAST_PTR(const __m128i *, rxp[0].mbuf),
					RTE_CAST_PTR(const __m128i *, rxp[1].mbuf));
			reg1 = _ci_rxq_rearm_desc_lasx(
					RTE_CAST_PTR(const __m128i *, rxp[2].mbuf),
					RTE_CAST_PTR(const __m128i *, rxp[3].mbuf));
		}

		/* flush descriptors */
		__lasx_xvst(reg0, RTE_CAST_PTR(__m256i *, &rxdp[0]), 0);
		__lasx_xvst(reg1, RTE_CAST_PTR(__m256i *, &rxdp[desc_per_reg]), 0);
	}
}
#endif /* __loongarch_asx */

/**
 * Rearm the RX queue with new buffers.
 *
 * This function is inlined, so the last parameter will be constant-propagated
 * if specified at compile time, and thus all unnecessary branching will be
 * eliminated.
 *
 * @param rxq
 *   Pointer to the RX queue structure.
 * @param vec_level
 *   The vectorization level to use for rearming.
 */
static __rte_always_inline void
ci_rxq_rearm(struct ci_rx_queue *rxq, const enum ci_rx_vec_level vec_level)
{
	const uint16_t rearm_thresh = CI_VPMD_RX_REARM_THRESH;
	uint16_t rx_id;

	/* Pull 'n' more MBUFs into the software ring */
	if (_ci_rxq_rearm_get_bufs(rxq) < 0)
		return;

	switch (vec_level) {
	case CI_RX_VEC_LEVEL_LASX:
#ifdef __loongarch_asx
		_ci_rxq_rearm_lasx(rxq);
		break;
#else
		/* fall back to LSX */
		/* fall through */
#endif
	case CI_RX_VEC_LEVEL_LSX:
		_ci_rxq_rearm_lsx(rxq);
		break;
	}

	rxq->rxrearm_start += rearm_thresh;
	if (rxq->rxrearm_start >= rxq->nb_rx_desc)
		rxq->rxrearm_start = 0;

	rxq->rxrearm_nb -= rearm_thresh;

	rx_id = (uint16_t)((rxq->rxrearm_start == 0) ?
			     (rxq->nb_rx_desc - 1) : (rxq->rxrearm_start - 1));

	/* Update the tail pointer on the NIC */
	rte_write32_wc(rte_cpu_to_le_32(rx_id), rxq->qrx_tail);
}

static inline enum rte_vect_max_simd
ci_get_loongarch64_max_simd_bitwidth(void)
{
	int ret = RTE_VECT_SIMD_DISABLED;
	int simd = rte_vect_get_max_simd_bitwidth();

	if (simd >= 256 && (rte_cpu_get_flag_enabled(RTE_CPUFLAG_LASX) == 1))
		ret = RTE_VECT_SIMD_256;
	else if (simd >= 128)
		ret = RTE_VECT_SIMD_128;
	return ret;
}

#endif /* _COMMON_INTEL_RX_VEC_LOONGARCH64_H_ */
