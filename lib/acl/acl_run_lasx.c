/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */


#include "acl_run_lasx.h"

/*
 * Note, that to be able to use LASX classify method,
 * both compiler and target cpu have to support LASX instructions.
 */
int
rte_acl_classify_lasx(const struct rte_acl_ctx *ctx, const uint8_t **data,
	uint32_t *results, uint32_t num, uint32_t categories)
{
	if (likely(num >= MAX_SEARCHES_LASX16))
		return search_lasxx16(ctx, data, results, num, categories);
	else if (num >= MAX_SEARCHES_LSX8)
		return search_lsx_8(ctx, data, results, num, categories);
	else if (num >= MAX_SEARCHES_LSX4)
		return search_lsx_4(ctx, data, results, num, categories);
	else
		return rte_acl_classify_scalar(ctx, data, results, num,
			categories);
}
