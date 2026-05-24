/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#include <stdalign.h>
#include <string.h>
#include <larchintrin.h>

#include <rte_common.h>
#include <rte_net_crc.h>
#include <rte_vect.h>

#include "net_crc.h"

uint32_t
rte_crc32_eth_loongarch64_handler(const uint8_t *data, uint32_t data_len)
{
	unsigned i;
	uintptr_t pd = (uintptr_t) data;
	uint32_t crc = 0xffffffffUL;

	/* align input to 8 byte boundary if needed */
	if (pd & 0x7) {
		unsigned int unaligned_bytes = RTE_MIN(8 - (pd & 0x7), data_len);
		for (i = 0; i < unaligned_bytes; i++) {
			crc = __crc_w_b_w(*(const char *)pd, (int)crc);
			pd++;
			data_len--;
		}
	}

	for (i = 0; i < data_len / 8; i++) {
		crc = __crc_w_d_w(*(const long int *)pd, (int)crc);
		pd += 8;
	}

	if (data_len & 0x4) {
		crc = __crc_w_w_w(*(const int *)pd, (int)crc);
		pd += 4;
	}

	if (data_len & 0x2) {
		crc = __crc_w_h_w(*(const short *)pd, (int)crc);
		pd += 2;
	}

	if (data_len & 0x1)
		crc = __crc_w_b_w(*(const char *)pd, (int)crc);

	return ~crc;
}
