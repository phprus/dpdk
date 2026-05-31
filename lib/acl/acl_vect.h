/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2014 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */

#ifndef _RTE_ACL_VECT_H_
#define _RTE_ACL_VECT_H_

/**
 * @file
 *
 * RTE ACL SSE/AVX/LSX/LASX related header.
 */

#if defined(RTE_ARCH_X86)

/*
 * Takes 2 SIMD registers containing N transitions each (tr0, tr1).
 * Shuffles it into different representation:
 * lo - contains low 32 bits of given N transitions.
 * hi - contains high 32 bits of given N transitions.
 */
#define	ACL_TR_HILO(P, TC, tr0, tr1, lo, hi)                        do { \
	lo = (typeof(lo))_##P##_shuffle_ps((TC)(tr0), (TC)(tr1), 0x88);  \
	hi = (typeof(hi))_##P##_shuffle_ps((TC)(tr0), (TC)(tr1), 0xdd);  \
} while (0)


/*
 * Calculate the address of the next transition for
 * all types of nodes. Note that only DFA nodes and range
 * nodes actually transition to another node. Match
 * nodes not supposed to be encountered here.
 * For quad range nodes:
 * Calculate number of range boundaries that are less than the
 * input value. Range boundaries for each node are in signed 8 bit,
 * ordered from -128 to 127.
 * This is effectively a popcnt of bytes that are greater than the
 * input byte.
 * Single nodes are processed in the same ways as quad range nodes.
 */
#define ACL_TR_CALC_ADDR(P, S,					\
	addr, index_mask, next_input, shuffle_input,		\
	ones_16, range_base, tr_lo, tr_hi)               do {	\
								\
	typeof(addr) in, node_type, r, t;			\
	typeof(addr) dfa_msk, dfa_ofs, quad_ofs;		\
								\
	t = _##P##_xor_si##S(index_mask, index_mask);		\
	in = _##P##_shuffle_epi8(next_input, shuffle_input);	\
								\
	/* Calc node type and node addr */			\
	node_type = _##P##_andnot_si##S(index_mask, tr_lo);	\
	addr = _##P##_and_si##S(index_mask, tr_lo);		\
								\
	/* mask for DFA type(0) nodes */			\
	dfa_msk = _##P##_cmpeq_epi32(node_type, t);		\
								\
	/* DFA calculations. */					\
	r = _##P##_srli_epi32(in, 30);				\
	r = _##P##_add_epi8(r, range_base);			\
	t = _##P##_srli_epi32(in, 24);				\
	r = _##P##_shuffle_epi8(tr_hi, r);			\
								\
	dfa_ofs = _##P##_sub_epi32(t, r);			\
								\
	/* QUAD/SINGLE calculations. */				\
	t = _##P##_cmpgt_epi8(in, tr_hi);			\
	t = _##P##_sign_epi8(t, t);				\
	t = _##P##_maddubs_epi16(t, t);				\
	quad_ofs = _##P##_madd_epi16(t, ones_16);		\
								\
	/* blend DFA and QUAD/SINGLE. */			\
	t = _##P##_blendv_epi8(quad_ofs, dfa_ofs, dfa_msk);	\
								\
	/* calculate address for next transitions. */		\
	addr = _##P##_add_epi32(addr, t);			\
} while (0)

#elif defined(RTE_ARCH_LOONGARCH)

/*
 * Takes 2 SIMD registers containing N transitions each (tr0, tr1).
 * Shuffles it into different representation:
 * lo - contains low 32 bits of given N transitions.
 * hi - contains high 32 bits of given N transitions.
 */
#define	ACL_TR_HILO(P, tr0, tr1, lo, hi)     do { \
	lo = __##P##permi_w((tr1), (tr0), 0x88);  \
	hi = __##P##permi_w((tr1), (tr0), 0xdd);  \
} while (0)


/*
 * Calculate the address of the next transition for
 * all types of nodes. Note that only DFA nodes and range
 * nodes actually transition to another node. Match
 * nodes not supposed to be encountered here.
 * For quad range nodes:
 * Calculate number of range boundaries that are less than the
 * input value. Range boundaries for each node are in signed 8 bit,
 * ordered from -128 to 127.
 * This is effectively a popcnt of bytes that are greater than the
 * input byte.
 * Single nodes are processed in the same ways as quad range nodes.
 */
#define ACL_TR_CALC_ADDR(P, S,					\
	addr, index_mask, next_input, shuffle_input,		\
	ones_16, range_base, tr_lo, tr_hi)               do {	\
								\
	typeof(addr) in, node_type, r, t;			\
	typeof(addr) dfa_msk, dfa_ofs, quad_ofs;		\
								\
	t = __##P##xor_v(index_mask, index_mask);		\
	in = P##shuffle_b(next_input, shuffle_input);		\
								\
	/* Calc node type and node addr */			\
	node_type = __##P##andn_v(index_mask, tr_lo);		\
	addr = __##P##and_v(index_mask, tr_lo);			\
								\
	/* mask for DFA type(0) nodes */			\
	dfa_msk = __##P##seq_w(node_type, t);			\
								\
	/* DFA calculations. */					\
	r = __##P##srli_w(in, 30);				\
	r = __##P##add_b(r, range_base);			\
	t = __##P##srli_w(in, 24);				\
	r = P##shuffle_b(tr_hi, r);				\
								\
	dfa_ofs = __##P##sub_w(t, r);				\
								\
	/* QUAD/SINGLE calculations. */				\
	t = __##P##slt_b(tr_hi, in);				\
	t = __##P##signcov_b(t, t);				\
	t = __##P##sadd_h(__##P##mulwev_h_bu_b(t, t),		\
				__##P##mulwod_h_bu_b(t, t));	\
	quad_ofs = __##P##maddwod_w_h(__##P##mulwev_w_h(t,	\
				ones_16), t, ones_16);		\
								\
	/* blend DFA and QUAD/SINGLE. */			\
	t = __##P##bitsel_v(quad_ofs, dfa_ofs, dfa_msk);	\
								\
	/* calculate address for next transitions. */		\
	addr = __##P##add_w(addr, t);				\
} while (0)

#endif

#endif /* _RTE_ACL_VECT_H_ */
