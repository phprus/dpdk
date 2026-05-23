/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef _RTE_CRC_LOONGARCH64_H_
#define _RTE_CRC_LOONGARCH64_H_

#include <larchintrin.h>

static inline uint32_t
crc32c_loongarch64_u8(uint8_t data, uint32_t init_val)
{
	return (uint32_t)__crcc_w_b_w((char)data, (int)init_val);
}

static inline uint32_t
crc32c_loongarch64_u16(uint16_t data, uint32_t init_val)
{
	return (uint32_t)__crcc_w_h_w((short)data, (int)init_val);
}

static inline uint32_t
crc32c_loongarch64_u32(uint32_t data, uint32_t init_val)
{
	return (uint32_t)__crcc_w_w_w((int)data, (int)init_val);
}

static inline uint32_t
crc32c_loongarch64_u64(uint64_t data, uint32_t init_val)
{
	return (uint32_t)__crcc_w_d_w((long int)data, (int)init_val);
}

/*
 * Use single crc32 instruction to perform a hash on a byte value.
 * Fall back to software crc32 implementation in case LoongArch CRC is
 * not supported.
 */
static inline uint32_t
rte_hash_crc_1byte(uint8_t data, uint32_t init_val)
{
	if (likely(rte_hash_crc32_alg & CRC32_LOONGARCH64))
		return crc32c_loongarch64_u8(data, init_val);

	return crc32c_1byte(data, init_val);
}

/*
 * Use single crc32 instruction to perform a hash on a 2 bytes value.
 * Fall back to software crc32 implementation in case LoongArch CRC is
 * not supported.
 */
static inline uint32_t
rte_hash_crc_2byte(uint16_t data, uint32_t init_val)
{
	if (likely(rte_hash_crc32_alg & CRC32_LOONGARCH64))
		return crc32c_loongarch64_u16(data, init_val);

	return crc32c_2bytes(data, init_val);
}

/*
 * Use single crc32 instruction to perform a hash on a 4 byte value.
 * Fall back to software crc32 implementation in case LoongArch CRC is
 * not supported.
 */
static inline uint32_t
rte_hash_crc_4byte(uint32_t data, uint32_t init_val)
{
	if (likely(rte_hash_crc32_alg & CRC32_LOONGARCH64))
		return crc32c_loongarch64_u32(data, init_val);

	return crc32c_1word(data, init_val);
}

/*
 * Use single crc32 instruction to perform a hash on a 8 byte value.
 * Fall back to software crc32 implementation in case LoongArch CRC is
 * not supported.
 */
static inline uint32_t
rte_hash_crc_8byte(uint64_t data, uint32_t init_val)
{
	if (likely(rte_hash_crc32_alg & CRC32_LOONGARCH64))
		return crc32c_loongarch64_u64(data, init_val);

	return crc32c_2words(data, init_val);
}

#endif /* _RTE_CRC_LOONGARCH64_H_ */
