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

#define RTE_VECT_DEFAULT_SIMD_BITWIDTH RTE_VECT_SIMD_256

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


static __rte_always_inline __m128i
lsx_set_b(int8_t e15, int8_t e14, int8_t e13, int8_t e12,
	  int8_t e11, int8_t e10, int8_t  e9, int8_t  e8,
	  int8_t  e7, int8_t  e6, int8_t  e5, int8_t  e4,
	  int8_t  e3, int8_t  e2, int8_t  e1, int8_t  e0)
{
	return (__m128i)((v16i8){ e0,  e1,  e2,  e3,
				  e4,  e5,  e6,  e7,
				  e8,  e9,  e10, e11,
				  e12, e13, e14, e15 });
}


static __rte_always_inline __m128i
lsx_set_w(int32_t e3, int32_t e2, int32_t e1, int32_t e0)
{
	return (__m128i)((v4i32){ e0, e1, e2, e3 });
}


static __rte_always_inline __m128i
lsx_set_d(int64_t e1, int64_t e0)
{
	return (__m128i)((v2i64){ e0, e1 });
}


static __rte_always_inline __m128i
lsx_vshuffle_b(__m128i a, __m128i b)
{
	a = __lsx_vshuf_b(a, a, __lsx_vandi_b(b, 15));
	return __lsx_vandn_v(__lsx_vslti_b(b, 0), a);
}


#define lsx_valignr_b(a, b, count)						\
__extension__ ({								\
	__m128i dest;								\
	if ((count) > 31)							\
		dest = __lsx_vreplgr2vr_b(0);					\
	else if ((count) > 15)							\
		dest = __lsx_vbsrl_v((a), ((count)&15));			\
	else if ((count) == 0)							\
		dest = (b);							\
	else if ((count) == 8)							\
		dest = __lsx_vpermi_w((a), (b), 0x4E);				\
	else									\
		dest = __lsx_vor_v(__lsx_vbsll_v((a), (16-((count)&15))),	\
				__lsx_vbsrl_v((b), ((count)&15)));		\
	(__m128i)dest;								\
})


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


#ifndef __loongarch_asx_sx_conv

static __rte_always_inline __m256i
__lasx_cast_128(__m128i src)
{
	__m256i dest;
	asm ("" : "=f"(dest) : "0"(src));
	return dest;
}


static __rte_always_inline __m256i
__lasx_concat_128(__m128i lo, __m128i hi)
{
	__m256i dest;
	asm ("xvpermi.q %u0,%u2,0x02\n" : "=f"(dest) : "0"(lo), "f"(hi));
	return dest;
}


static __rte_always_inline __m128i
__lasx_extract_128_lo(__m256i src)
{
	__m128i dest;
	asm ("" : "=f"(dest) : "0"(src));
	return dest;
}


static __rte_always_inline __m128i
__lasx_extract_128_hi(__m256i src)
{
	__m128i dest;
	asm ("xvpermi.d %u0,%u1,0xe\n" : "=f"(dest) : "f"(src));
	return dest;
}


#endif /* __loongarch_asx_sx_conv */


static __rte_always_inline __m256i
lasx_set_b(int8_t e31, int8_t e30, int8_t e29, int8_t e28,
	  int8_t e27, int8_t e26, int8_t e25, int8_t e24,
	  int8_t e23, int8_t e22, int8_t e21, int8_t e20,
	  int8_t e19, int8_t e18, int8_t e17, int8_t e16,
	  int8_t e15, int8_t e14, int8_t e13, int8_t e12,
	  int8_t e11, int8_t e10, int8_t  e9, int8_t  e8,
	  int8_t  e7, int8_t  e6, int8_t  e5, int8_t  e4,
	  int8_t  e3, int8_t  e2, int8_t  e1, int8_t  e0)
{
	return (__m256i)((v32i8){ e0,  e1,  e2,  e3,  e4,  e5,  e6,  e7,
				  e8,  e9,  e10, e11, e12, e13, e14, e15,
				  e16, e17, e18, e19, e20, e21, e22, e23,
				  e24, e25, e26, e27, e28, e29, e30, e31 });
}


static __rte_always_inline __m256i
lasx_set_h(int16_t e15, int16_t e14, int16_t e13, int16_t e12,
	   int16_t e11, int16_t e10, int16_t  e9, int16_t  e8,
	   int16_t  e7, int16_t  e6, int16_t  e5, int16_t  e4,
	   int16_t  e3, int16_t  e2, int16_t  e1, int16_t  e0)
{
	return (__m256i)((v16i16){ e0, e1, e2,  e3,  e4,  e5,  e6,  e7,
				   e8, e9, e10, e11, e12, e13, e14, e15 });
}


static __rte_always_inline __m256i
lasx_set_w(int32_t e7, int32_t e6, int32_t e5, int32_t e4,
	   int32_t e3, int32_t e2, int32_t e1, int32_t e0)
{
	return (__m256i)((v8i32){ e0, e1, e2,  e3,  e4,  e5,  e6,  e7 });
}


static __rte_always_inline __m256i
lasx_set_d(int64_t  e3, int64_t  e2, int64_t  e1, int64_t  e0)
{
	return (__m256i)((v4i64){ e0, e1, e2,  e3 });
}


static __rte_always_inline __m256i
lasx_xvshuffle_b(__m256i a, __m256i b)
{
	a = __lasx_xvshuf_b(a, a, __lasx_xvandi_b(b, 15));
	return __lasx_xvandn_v(__lasx_xvslti_b(b, 0), a);
}


#define MB(mask, i) -((mask >> i) & 1)

static __rte_always_inline __m256i
lasx_xvbitselmaski_h(int mask)
{
	return lasx_set_h(
		MB(mask, 7), MB(mask, 6), MB(mask, 5), MB(mask, 4),
		MB(mask, 3), MB(mask, 2), MB(mask, 1), MB(mask, 0),
		MB(mask, 7), MB(mask, 6), MB(mask, 5), MB(mask, 4),
		MB(mask, 3), MB(mask, 2), MB(mask, 1), MB(mask, 0));
}

static __rte_always_inline __m256i
lasx_xvbitselmaski_w(int mask)
{
	return lasx_set_w(
		MB(mask, 7), MB(mask, 6), MB(mask, 5), MB(mask, 4),
		MB(mask, 3), MB(mask, 2), MB(mask, 1), MB(mask, 0));
}

#undef MB


#define lasx_xvpickve2gr_b(a, idx) \
	(int)((__lasx_xvpickve2gr_wu((a), (idx) / 4) >> ((idx) % 4) * 8) & 0xFF)

#define lasx_xvalignr_b(a, b, count)						\
__extension__ ({								\
	__m256i dest;								\
	if ((count) > 31)							\
		dest = __lasx_xvreplgr2vr_w(0);					\
	else if ((count) > 15)							\
		dest = __lasx_xvbsrl_v((a), ((count)&15));			\
	else if ((count) == 0)							\
		dest = (b);							\
	else if ((count) == 8)							\
		dest = __lasx_xvpermi_w((a), (b), 0x4E);			\
	else									\
		dest = __lasx_xvor_v(__lasx_xvbsll_v((a), (16-((count)&15))),	\
				__lasx_xvbsrl_v((b), ((count)&15)));		\
	(__m256i)dest;								\
})


#define lasx_xvsllwil_wu_hu_128(a, imm) \
	__lasx_xvsllwil_wu_hu(__lasx_xvpermi_d(__lasx_cast_128(a), 0xd8), (imm))


static __rte_always_inline int
lasx_xvmskltz2gr_b(__m256i a)
{
	a = __lasx_xvmskltz_b(a);
	return (__lasx_xvpickve2gr_w(a, 0) | (__lasx_xvpickve2gr_w(a, 4) << 16));
}


static __rte_always_inline int
lasx_xvmskltz2gr_w(__m256i a)
{
	a = __lasx_xvmskltz_w(a);
	return (__lasx_xvpickve2gr_w(a, 0) | (__lasx_xvpickve2gr_w(a, 4) << 4));
}

#endif /* __loongarch_asx */

#ifdef __cplusplus
}
#endif

#endif /* RTE_VECT_LOONGARCH_H */
