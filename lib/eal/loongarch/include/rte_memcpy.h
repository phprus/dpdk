/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2022 Loongson Technology Corporation Limited
 * Copyright(c) 2010-2014 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef RTE_MEMCPY_LOONGARCH_H
#define RTE_MEMCPY_LOONGARCH_H

/**
 * @file
 *
 * Functions for LSX/LASX implementation of memcpy().
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <rte_vect.h>
#include <rte_common.h>
#include <rte_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Copy bytes from one location to another. The locations must not overlap.
 *
 * @param dst
 *   Pointer to the destination of the data.
 * @param src
 *   Pointer to the source data.
 * @param n
 *   Number of bytes to copy.
 * @return
 *   Pointer to the destination data.
 */
static __rte_always_inline void *
rte_memcpy(void *__rte_restrict dst, const void *__rte_restrict src, size_t n);

/**
 * Copy bytes from one location to another,
 * locations must not overlap.
 * Use with n <= 15.
 */
static __rte_always_inline void *
rte_mov15_or_less(void *__rte_restrict dst, const void *__rte_restrict src, size_t n)
{
	/**
	 * Use the following structs to avoid violating C standard
	 * alignment requirements and to avoid strict aliasing bugs
	 */
	struct __rte_packed_begin rte_uint64_alias {
		uint64_t val;
	} __rte_packed_end __rte_may_alias;
	struct __rte_packed_begin rte_uint32_alias {
		uint32_t val;
	} __rte_packed_end __rte_may_alias;
	struct __rte_packed_begin rte_uint16_alias {
		uint16_t val;
	} __rte_packed_end __rte_may_alias;

	void *ret = dst;
	if (n & 8) {
		((struct rte_uint64_alias *)dst)->val =
			((const struct rte_uint64_alias *)src)->val;
		src = (const uint64_t *)src + 1;
		dst = (uint64_t *)dst + 1;
	}
	if (n & 4) {
		((struct rte_uint32_alias *)dst)->val =
			((const struct rte_uint32_alias *)src)->val;
		src = (const uint32_t *)src + 1;
		dst = (uint32_t *)dst + 1;
	}
	if (n & 2) {
		((struct rte_uint16_alias *)dst)->val =
			((const struct rte_uint16_alias *)src)->val;
		src = (const uint16_t *)src + 1;
		dst = (uint16_t *)dst + 1;
	}
	if (n & 1)
		*(uint8_t *)dst = *(const uint8_t *)src;
	return ret;
}

/**
 * Copy 16 bytes from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov16(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src)
{
	__m128i xmm0;

	xmm0 = __lsx_vld(src, 0);
	__lsx_vst(xmm0, dst, 0);
}

/**
 * Copy 32 bytes from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov32(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src)
{
#if defined __loongarch_asx
	__m256i ymm0;

	ymm0 = __lasx_xvld(src, 0);
	__lasx_xvst(ymm0, dst, 0);
#else /* LSX implementation */
	rte_mov16((uint8_t *)dst + 0 * 16, (const uint8_t *)src + 0 * 16);
	rte_mov16((uint8_t *)dst + 1 * 16, (const uint8_t *)src + 1 * 16);
#endif
}

/**
 * Copy 48 bytes from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov48(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src)
{
#if defined __loongarch_asx
	rte_mov32((uint8_t *)dst, (const uint8_t *)src);
	rte_mov16((uint8_t *)dst + 32, (const uint8_t *)src + 32);
#else /* LSX implementation */
	rte_mov16((uint8_t *)dst + 0 * 16, (const uint8_t *)src + 0 * 16);
	rte_mov16((uint8_t *)dst + 1 * 16, (const uint8_t *)src + 1 * 16);
	rte_mov16((uint8_t *)dst + 2 * 16, (const uint8_t *)src + 2 * 16);
#endif
}

/**
 * Copy 64 bytes from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov64(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src)
{
	rte_mov32((uint8_t *)dst + 0 * 32, (const uint8_t *)src + 0 * 32);
	rte_mov32((uint8_t *)dst + 1 * 32, (const uint8_t *)src + 1 * 32);
}

/**
 * Copy 128 bytes from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov128(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src)
{
	rte_mov64(dst + 0 * 64, src + 0 * 64);
	rte_mov64(dst + 1 * 64, src + 1 * 64);
}

/**
 * Copy 256 bytes from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov256(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src)
{
	rte_mov128(dst + 0 * 128, src + 0 * 128);
	rte_mov128(dst + 1 * 128, src + 1 * 128);
}

#if defined __loongarch_asx

/**
 * LASX implementation below
 */

#define ALIGNMENT_MASK 0x1F

/**
 * Copy 128-byte blocks from one location to another,
 * locations must not overlap.
 */
static __rte_always_inline void
rte_mov128blocks(uint8_t *__rte_restrict dst, const uint8_t *__rte_restrict src, size_t n)
{
	__m256i ymm0, ymm1, ymm2, ymm3;

	while (n >= 128) {
		ymm0 = __lasx_xvld(src, 0 * 32);
		n -= 128;
		ymm1 = __lasx_xvld(src, 1 * 32);
		ymm2 = __lasx_xvld(src, 2 * 32);
		ymm3 = __lasx_xvld(src, 3 * 32);
		src = (const uint8_t *)src + 128;
		__lasx_xvst(ymm0, dst, 0 * 32);
		__lasx_xvst(ymm1, dst, 1 * 32);
		__lasx_xvst(ymm2, dst, 2 * 32);
		__lasx_xvst(ymm3, dst, 3 * 32);
		dst = (uint8_t *)dst + 128;
	}
}

/**
 * Copy bytes from one location to another,
 * locations must not overlap.
 * Use with n > 64.
 */
static __rte_always_inline void *
rte_memcpy_generic_more_than_64(void *__rte_restrict dst, const void *__rte_restrict src,
		size_t n)
{
	void *ret = dst;
	size_t dstofss;
	size_t bits;

	/**
	 * Fast way when copy size doesn't exceed 256 bytes
	 */
	if (n <= 256) {
		if (n >= 128) {
			n -= 128;
			rte_mov128((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 128;
			dst = (uint8_t *)dst + 128;
		}
COPY_BLOCK_128_BACK31:
		if (n >= 64) {
			n -= 64;
			rte_mov64((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 64;
			dst = (uint8_t *)dst + 64;
		}
		if (n > 32) {
			rte_mov32((uint8_t *)dst, (const uint8_t *)src);
			rte_mov32((uint8_t *)dst - 32 + n,
					(const uint8_t *)src - 32 + n);
			return ret;
		}
		if (n > 0) {
			rte_mov32((uint8_t *)dst - 32 + n,
					(const uint8_t *)src - 32 + n);
		}
		return ret;
	}

	/**
	 * Make store aligned when copy size exceeds 256 bytes
	 */
	dstofss = (uintptr_t)dst & 0x1F;
	if (dstofss > 0) {
		dstofss = 32 - dstofss;
		n -= dstofss;
		rte_mov32((uint8_t *)dst, (const uint8_t *)src);
		src = (const uint8_t *)src + dstofss;
		dst = (uint8_t *)dst + dstofss;
	}

	/**
	 * Copy 128-byte blocks
	 */
	rte_mov128blocks((uint8_t *)dst, (const uint8_t *)src, n);
	bits = n;
	n = n & 127;
	bits -= n;
	src = (const uint8_t *)src + bits;
	dst = (uint8_t *)dst + bits;

	/**
	 * Copy whatever left
	 */
	goto COPY_BLOCK_128_BACK31;
}

#else /* __loongarch_asx */

/**
 * LSX implementation below
 */

#define ALIGNMENT_MASK 0x0F

/**
 * Macro for copying unaligned block from one location to another with constant load offset,
 * 47 bytes leftover maximum,
 * locations must not overlap.
 * Requirements:
 * - Store is aligned
 * - Load offset is <offset>, which must be immediate value within [1, 15]
 * - For <src>, make sure <offset> bit backwards & <16 - offset> bit forwards are available for loading
 * - <dst>, <src>, <len> must be variables
 * - __m128i <xmm0> ~ <xmm8> must be pre-defined
 */
#define MOVEUNALIGNED_LEFT47_IMM(dst, src, len, offset)                 \
{                                                                       \
    size_t tmp;                                                         \
    while (len >= 128 + 16 - offset) {                                  \
        xmm0 = __lsx_vld(src, 0 * 16 - offset);                         \
        len -= 128;                                                     \
        xmm1 = __lsx_vld(src, 1 * 16 - offset);                         \
        xmm2 = __lsx_vld(src, 2 * 16 - offset);                         \
        xmm3 = __lsx_vld(src, 3 * 16 - offset);                         \
        xmm4 = __lsx_vld(src, 4 * 16 - offset);                         \
        xmm5 = __lsx_vld(src, 5 * 16 - offset);                         \
        xmm6 = __lsx_vld(src, 6 * 16 - offset);                         \
        xmm7 = __lsx_vld(src, 7 * 16 - offset);                         \
        xmm8 = __lsx_vld(src, 8 * 16 - offset);                         \
        src = (const uint8_t *)src + 128;                               \
        __lsx_vst(lsx_valignr_b(xmm1, xmm0, offset), dst, 0 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm2, xmm1, offset), dst, 1 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm3, xmm2, offset), dst, 2 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm4, xmm3, offset), dst, 3 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm5, xmm4, offset), dst, 4 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm6, xmm5, offset), dst, 5 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm7, xmm6, offset), dst, 6 * 16);      \
        __lsx_vst(lsx_valignr_b(xmm8, xmm7, offset), dst, 7 * 16);      \
        dst = (uint8_t *)dst + 128;                                     \
    }                                                                   \
    tmp = len;                                                          \
    len = ((len - 16 + offset) & 127) + 16 - offset;                    \
    tmp -= len;                                                         \
    src = (const uint8_t *)src + tmp;                                   \
    dst = (uint8_t *)dst + tmp;                                         \
    if (len >= 32 + 16 - offset) {                                      \
        while (len >= 32 + 16 - offset) {                               \
            xmm0 = __lsx_vld(src, 0 * 16 - offset);                     \
            len -= 32;                                                  \
            xmm1 = __lsx_vld(src, 1 * 16 - offset);                     \
            xmm2 = __lsx_vld(src, 2 * 16 - offset);                     \
            src = (const uint8_t *)src + 32;                            \
            __lsx_vst(lsx_valignr_b(xmm1, xmm0, offset), dst, 0 * 16);  \
            __lsx_vst(lsx_valignr_b(xmm2, xmm1, offset), dst, 1 * 16);  \
            dst = (uint8_t *)dst + 32;                                  \
        }                                                               \
        tmp = len;                                                      \
        len = ((len - 16 + offset) & 31) + 16 - offset;                 \
        tmp -= len;                                                     \
        src = (const uint8_t *)src + tmp;                               \
        dst = (uint8_t *)dst + tmp;                                     \
    }                                                                   \
}

/**
 * Macro for copying unaligned block from one location to another,
 * 47 bytes leftover maximum,
 * locations must not overlap.
 * Use switch here because the aligning instruction requires immediate value for shift count.
 * Requirements:
 * - Store is aligned
 * - Load offset is <offset>, which must be within [1, 15]
 * - For <src>, make sure <offset> bit backwards & <16 - offset> bit forwards are available for loading
 * - <dst>, <src>, <len> must be variables
 * - __m128i <xmm0> ~ <xmm8> used in MOVEUNALIGNED_LEFT47_IMM must be pre-defined
 */
#define MOVEUNALIGNED_LEFT47(dst, src, len, offset)                   \
{                                                      \
    switch (offset) {                                                 \
    case 0x01: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x01); break;    \
    case 0x02: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x02); break;    \
    case 0x03: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x03); break;    \
    case 0x04: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x04); break;    \
    case 0x05: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x05); break;    \
    case 0x06: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x06); break;    \
    case 0x07: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x07); break;    \
    case 0x08: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x08); break;    \
    case 0x09: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x09); break;    \
    case 0x0A: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x0A); break;    \
    case 0x0B: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x0B); break;    \
    case 0x0C: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x0C); break;    \
    case 0x0D: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x0D); break;    \
    case 0x0E: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x0E); break;    \
    case 0x0F: MOVEUNALIGNED_LEFT47_IMM(dst, src, n, 0x0F); break;    \
    default:;                                                         \
    }                                                                 \
}

/**
 * Copy bytes from one location to another,
 * locations must not overlap.
 * Use with n > 64.
 */
static __rte_always_inline void *
rte_memcpy_generic_more_than_64(void *__rte_restrict dst, const void *__rte_restrict src,
		size_t n)
{
	__m128i xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8;
	void *ret = dst;
	size_t dstofss;
	size_t srcofs;

	/**
	 * Fast way when copy size doesn't exceed 512 bytes
	 */
	if (n <= 128) {
		goto COPY_BLOCK_128_BACK15;
	}
	if (n <= 512) {
		if (n >= 256) {
			n -= 256;
			rte_mov128((uint8_t *)dst, (const uint8_t *)src);
			rte_mov128((uint8_t *)dst + 128, (const uint8_t *)src + 128);
			src = (const uint8_t *)src + 256;
			dst = (uint8_t *)dst + 256;
		}
COPY_BLOCK_255_BACK15:
		if (n >= 128) {
			n -= 128;
			rte_mov128((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 128;
			dst = (uint8_t *)dst + 128;
		}
COPY_BLOCK_128_BACK15:
		if (n >= 64) {
			n -= 64;
			rte_mov64((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 64;
			dst = (uint8_t *)dst + 64;
		}
COPY_BLOCK_64_BACK15:
		if (n >= 32) {
			n -= 32;
			rte_mov32((uint8_t *)dst, (const uint8_t *)src);
			src = (const uint8_t *)src + 32;
			dst = (uint8_t *)dst + 32;
		}
		if (n > 16) {
			rte_mov16((uint8_t *)dst, (const uint8_t *)src);
			rte_mov16((uint8_t *)dst - 16 + n, (const uint8_t *)src - 16 + n);
			return ret;
		}
		if (n > 0) {
			rte_mov16((uint8_t *)dst - 16 + n, (const uint8_t *)src - 16 + n);
		}
		return ret;
	}

	/**
	 * Make store aligned when copy size exceeds 512 bytes,
	 * and make sure the first 15 bytes are copied, because
	 * unaligned copy functions require up to 15 bytes
	 * backwards access.
	 */
	dstofss = (uintptr_t)dst & 0x0F;
	if (dstofss > 0) {
		dstofss = 16 - dstofss + 16;
		n -= dstofss;
		rte_mov32((uint8_t *)dst, (const uint8_t *)src);
		src = (const uint8_t *)src + dstofss;
		dst = (uint8_t *)dst + dstofss;
	}
	srcofs = ((uintptr_t)src & 0x0F);

	/**
	 * For aligned copy
	 */
	if (srcofs == 0) {
		/**
		 * Copy 256-byte blocks
		 */
		for (; n >= 256; n -= 256) {
			rte_mov256((uint8_t *)dst, (const uint8_t *)src);
			dst = (uint8_t *)dst + 256;
			src = (const uint8_t *)src + 256;
		}

		/**
		 * Copy whatever left
		 */
		goto COPY_BLOCK_255_BACK15;
	}

	/**
	 * For copy with unaligned load
	 */
	MOVEUNALIGNED_LEFT47(dst, src, n, srcofs);

	/**
	 * Copy whatever left
	 */
	goto COPY_BLOCK_64_BACK15;
}

#endif /* __loongarch_asx */

/**
 * Copy bytes from one vector register size aligned location to another,
 * locations must not overlap.
 * Use with n > 64.
 */
static __rte_always_inline void *
rte_memcpy_aligned_more_than_64(void *__rte_restrict dst, const void *__rte_restrict src,
		size_t n)
{
	void *ret = dst;

	/* Copy 64 bytes blocks */
	for (; n > 64; n -= 64) {
		rte_mov64((uint8_t *)dst, (const uint8_t *)src);
		dst = (uint8_t *)dst + 64;
		src = (const uint8_t *)src + 64;
	}

	/* Copy whatever left */
	rte_mov64((uint8_t *)dst - 64 + n,
			(const uint8_t *)src - 64 + n);

	return ret;
}

static __rte_always_inline void *
rte_memcpy(void *__rte_restrict dst, const void *__rte_restrict src, size_t n)
{
	/* Fast way when copy size doesn't exceed 64 bytes. */
	if (n < 16)
		return rte_mov15_or_less(dst, src, n);
	if (n <= 32) {
		if (__rte_constant(n) && n == 32) {
			rte_mov32((uint8_t *)dst, (const uint8_t *)src);
			return dst;
		}
		rte_mov16((uint8_t *)dst, (const uint8_t *)src);
		if (__rte_constant(n) && n == 16)
			return dst; /* avoid (harmless) duplicate copy */
		rte_mov16((uint8_t *)dst - 16 + n, (const uint8_t *)src - 16 + n);
		return dst;
	}
	if (n <= 64) {
		if (__rte_constant(n) && n == 64) {
			rte_mov64((uint8_t *)dst, (const uint8_t *)src);
			return dst;
		}
#if defined __loongarch_asx
		rte_mov32((uint8_t *)dst, (const uint8_t *)src);
		rte_mov32((uint8_t *)dst - 32 + n, (const uint8_t *)src - 32 + n);
#else /* LSX implementation */
		rte_mov16((uint8_t *)dst + 0 * 16, (const uint8_t *)src + 0 * 16);
		rte_mov16((uint8_t *)dst + 1 * 16, (const uint8_t *)src + 1 * 16);
		if (n > 48)
			rte_mov16((uint8_t *)dst + 2 * 16, (const uint8_t *)src + 2 * 16);
		rte_mov16((uint8_t *)dst - 16 + n, (const uint8_t *)src - 16 + n);
#endif
		return dst;
	}

	/* Implementation for size > 64 bytes depends on alignment with vector register size. */
	if (!(((uintptr_t)dst | (uintptr_t)src) & ALIGNMENT_MASK))
		return rte_memcpy_aligned_more_than_64(dst, src, n);
	else
		return rte_memcpy_generic_more_than_64(dst, src, n);
}

#undef ALIGNMENT_MASK

#ifdef __cplusplus
}
#endif

#endif /* RTE_MEMCPY_LOONGARCH_H */
