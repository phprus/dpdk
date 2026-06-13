/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016 Intel Corporation
 * Copyright(c) 2026 Vladislav Shchapov <vladislav@shchapov.ru>
 */


#ifndef _L3FWD_LSX_H_
#define _L3FWD_LSX_H_

#include "l3fwd.h"
#include "lsx/port_group.h"
#include "l3fwd_common.h"

#undef SENDM_PORT_OVERHEAD
#define SENDM_PORT_OVERHEAD(x) ((x) + 2 * FWDSTEP)

/*
 * Update source and destination MAC addresses in the ethernet header.
 * Perform RFC1812 checks and updates for IPV4 packets.
 */
static inline void
processx4_step3(struct rte_mbuf *pkt[FWDSTEP], uint16_t dst_port[FWDSTEP])
{
	__m128i te[FWDSTEP];
	__m128i ve[FWDSTEP];
	__m128i *p[FWDSTEP];

	p[0] = rte_pktmbuf_mtod(pkt[0], __m128i *);
	p[1] = rte_pktmbuf_mtod(pkt[1], __m128i *);
	p[2] = rte_pktmbuf_mtod(pkt[2], __m128i *);
	p[3] = rte_pktmbuf_mtod(pkt[3], __m128i *);

	ve[0] = val_eth[dst_port[0]];
	te[0] = __lsx_vld(p[0], 0);

	ve[1] = val_eth[dst_port[1]];
	te[1] = __lsx_vld(p[1], 0);

	ve[2] = val_eth[dst_port[2]];
	te[2] = __lsx_vld(p[2], 0);

	ve[3] = val_eth[dst_port[3]];
	te[3] = __lsx_vld(p[3], 0);

	/* Update first 12 bytes, keep rest bytes intact. */
	te[0] =  __lsx_vextrins_w(ve[0], te[0], 0x33);
	te[1] =  __lsx_vextrins_w(ve[1], te[1], 0x33);
	te[2] =  __lsx_vextrins_w(ve[2], te[2], 0x33);
	te[3] =  __lsx_vextrins_w(ve[3], te[3], 0x33);

	__lsx_vst(te[0], p[0], 0);
	__lsx_vst(te[1], p[1], 0);
	__lsx_vst(te[2], p[2], 0);
	__lsx_vst(te[3], p[3], 0);

	rfc1812_process((struct rte_ipv4_hdr *)
			((struct rte_ether_hdr *)p[0] + 1),
			&dst_port[0], pkt[0]->packet_type);
	rfc1812_process((struct rte_ipv4_hdr *)
			((struct rte_ether_hdr *)p[1] + 1),
			&dst_port[1], pkt[1]->packet_type);
	rfc1812_process((struct rte_ipv4_hdr *)
			((struct rte_ether_hdr *)p[2] + 1),
			&dst_port[2], pkt[2]->packet_type);
	rfc1812_process((struct rte_ipv4_hdr *)
			((struct rte_ether_hdr *)p[3] + 1),
			&dst_port[3], pkt[3]->packet_type);
}

/**
 * Process one packet:
 * Update source and destination MAC addresses in the ethernet header.
 * Perform RFC1812 checks and updates for IPV4 packets.
 */
static inline void
process_packet(struct rte_mbuf *pkt, uint16_t *dst_port)
{
	struct rte_ether_hdr *eth_hdr;
	__m128i te, ve;

	eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);

	te = __lsx_vld((__m128i *)eth_hdr, 0);
	ve = val_eth[dst_port[0]];

	rfc1812_process((struct rte_ipv4_hdr *)(eth_hdr + 1), dst_port,
			pkt->packet_type);

	te =  __lsx_vextrins_w(ve, te, 0x33);
	__lsx_vst(te, (__m128i *)eth_hdr, 0);
}

/**
 * Send packets burst from pkts_burst to the ports in dst_port array
 */
static __rte_always_inline void
send_packets_multi(struct lcore_conf *qconf, struct rte_mbuf **pkts_burst,
		uint16_t dst_port[SENDM_PORT_OVERHEAD(MAX_PKT_BURST)],
		int nb_rx)
{
	int32_t k;
	int j = 0;
	uint16_t dlp;
	uint16_t *lp;
	uint16_t pnum[MAX_PKT_BURST + 1];

	/*
	 * Finish packet processing and group consecutive
	 * packets with the same destination port.
	 */
	k = RTE_ALIGN_FLOOR(nb_rx, FWDSTEP);
	if (k != 0) {
		__m128i dp1, dp2;

		lp = pnum;
		lp[0] = 1;

		processx4_step3(pkts_burst, dst_port);

		/* dp1: <d[0], d[1], d[2], d[3], ... > */
		dp1 = __lsx_vld((__m128i *)dst_port, 0);

		for (j = FWDSTEP; j != k; j += FWDSTEP) {
			processx4_step3(&pkts_burst[j], &dst_port[j]);

			/*
			 * dp2:
			 * <d[j-3], d[j-2], d[j-1], d[j], ... >
			 */
			dp2 = __lsx_vld((__m128i *)
					&dst_port[j - FWDSTEP + 1], 0);
			lp  = port_groupx4(&pnum[j - FWDSTEP], lp, dp1, dp2);

			/*
			 * dp1:
			 * <d[j], d[j+1], d[j+2], d[j+3], ... >
			 */
			static_assert(((FWDSTEP - 1) *
					sizeof(dst_port[0])) < 16,
					"Shift must be less 16");
			dp1 = __lsx_vbsrl_v(dp2, (FWDSTEP - 1) *
						sizeof(dst_port[0]));
		}

		/*
		 * dp2: <d[j-3], d[j-2], d[j-1], d[j-1], ... >
		 */
		dp2 = __lsx_vextrins_d(__lsx_vshuf4i_h(dp1, 0xf9), dp1, 0x11);
		lp  = port_groupx4(&pnum[j - FWDSTEP], lp, dp1, dp2);

		/*
		 * remove values added by the last repeated
		 * dst port.
		 */
		lp[0]--;
		dlp = dst_port[j - 1];
	} else {
		/* set dlp and lp to the never used values. */
		dlp = BAD_PORT - 1;
		lp = pnum + MAX_PKT_BURST;
	}

	/* Process up to last 3 packets one by one. */
	switch (nb_rx % FWDSTEP) {
	case 3:
		process_packet(pkts_burst[j], dst_port + j);
		GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
		j++;
		/* fall-through */
	case 2:
		process_packet(pkts_burst[j], dst_port + j);
		GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
		j++;
		/* fall-through */
	case 1:
		process_packet(pkts_burst[j], dst_port + j);
		GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
		j++;
	}

	/*
	 * Send packets out, through destination port.
	 * Consecutive packets with the same destination port
	 * are already grouped together.
	 * If destination port for the packet equals BAD_PORT,
	 * then free the packet without sending it out.
	 */
	for (j = 0; j < nb_rx; j += k) {

		int32_t m;
		uint16_t pn;

		pn = dst_port[j];
		k = pnum[j];

		if (likely(pn != BAD_PORT))
			send_packetsx4(qconf, pn, pkts_burst + j, k);
		else
			for (m = j; m != j + k; m++)
				rte_pktmbuf_free(pkts_burst[m]);

	}
}

static __rte_always_inline uint16_t
process_dst_port(uint16_t *dst_ports, uint16_t nb_elem)
{
	uint16_t i = 0, res;

	while (nb_elem > 7) {
		__m128i dp = __lsx_vreplgr2vr_h(dst_ports[0]);
		__m128i dp1;

		dp1 = __lsx_vld((__m128i *)&dst_ports[i], 0);
		dp1 = __lsx_vseq_h(dp1, dp);
		res = __lsx_vpickve2gr_w(__lsx_vmskltz_b(dp1), 0);
		if (res != 0xFFFF)
			return BAD_PORT;

		nb_elem -= 8;
		i += 8;
	}

	while (nb_elem > 3) {
		__m128i dp = __lsx_vreplgr2vr_h(dst_ports[0]);
		__m128i dp1;

		dp1 = __lsx_vld((__m128i *)&dst_ports[i], 0);
		dp1 = __lsx_vseq_h(dp1, dp);
		dp1 = __lsx_vilvl_h(dp1, dp1);
		res = __lsx_vpickve2gr_wu(__lsx_vmskltz_w(dp1), 0);
		if (res != 0xF)
			return BAD_PORT;

		nb_elem -= 4;
		i += 4;
	}

	while (nb_elem) {
		if (dst_ports[i] != dst_ports[0])
			return BAD_PORT;
		nb_elem--;
		i++;
	}

	return dst_ports[0];
}

#endif /* _L3FWD_LSX_H_ */
