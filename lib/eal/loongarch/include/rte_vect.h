/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Loongson Technology Corporation Limited
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef RTE_VECT_LOONGARCH_H
#define RTE_VECT_LOONGARCH_H

/**
 * @file
 *
 * RTE LSX/LASX related header.
 */

#include <assert.h>
#include <stdint.h>
#include <rte_config.h>
#include <rte_common.h>
#include "generic/rte_vect.h"

#include <lsxintrin.h>
#ifdef __loongarch_asx
#include <lasxintrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RTE_VECT_DEFAULT_SIMD_BITWIDTH RTE_VECT_SIMD_128

typedef __m128i xmm_t;

#define	XMM_SIZE	16
#define	XMM_MASK	(XMM_SIZE - 1)

static_assert(sizeof(xmm_t) == XMM_SIZE, "");

typedef union rte_xmm {
	xmm_t    x;
	uint8_t  u8[XMM_SIZE / sizeof(uint8_t)];
	uint16_t u16[XMM_SIZE / sizeof(uint16_t)];
	uint32_t u32[XMM_SIZE / sizeof(uint32_t)];
	uint64_t u64[XMM_SIZE / sizeof(uint64_t)];
	double   pd[XMM_SIZE / sizeof(double)];
} rte_xmm_t;

#ifdef __loongarch_asx

typedef __m256i ymm_t;

#define	YMM_SIZE	(sizeof(ymm_t))
#define	YMM_MASK	(YMM_SIZE - 1)

typedef union rte_ymm {
	ymm_t    y;
	xmm_t    x[YMM_SIZE / sizeof(xmm_t)];
	uint8_t  u8[YMM_SIZE / sizeof(uint8_t)];
	uint16_t u16[YMM_SIZE / sizeof(uint16_t)];
	uint32_t u32[YMM_SIZE / sizeof(uint32_t)];
	uint64_t u64[YMM_SIZE / sizeof(uint64_t)];
	double   pd[YMM_SIZE / sizeof(double)];
} rte_ymm_t;

#endif /* __loongarch_asx */

#ifdef __cplusplus
}
#endif

#endif /* RTE_VECT_LOONGARCH_H */
