/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if.h>		/* for struct ifreq */
#include <linux/sockios.h>	/* for IOCTL no */
#include <linux/ethtool.h>
#include "ether_export.h"

#define ETHER_MAX_INT_FRAME_SIZE (1024 * 16)
#define ETHER_DEVTETST_VER 7U

static unsigned int connected_speed = SPEED_100;

static char *exe_name;
static char *if_name;
static int print_all;

static void usage_common(char *cmd)
{
	fprintf(stderr, "NOTE: This is reference sample code and shall not be used in production\n");
	if (print_all) {
		fprintf(stderr, "  %s",cmd);
	} else {
		fprintf(stderr, "\n  %s %s %s <parameters>\n\n", exe_name,
				(if_name ? if_name : "<interface_name>"),
				(cmd ? cmd : "<command>"));
		fprintf(stderr, "  supported parameters ::\n");
	}
}

static void usage_avb(void)
{
	usage_common("avb");
	fprintf(stderr,
			"  <qinx> <algorithm_value> <bw> <credit_control> <tcinx>\n"
			"             [algorithm value\n"
			"      		  0 - default (strict priority for EQOS and EST for MGBE)\n"
			"      		  1 - cbs (credit based shaper)\n"
			"      	      bw - value in terms of percentage of bandwidth to be allocated (1-100)\n"
			"      	      credit_control\n"
			"                 0 - disabled credit_control\n"
			"                 1 - enabled credit_control]\n"
			"      	      tcinx - TC mapping for <qinx>\n"
			"             note:\n"
			"		\"credit_control\" is applicable for cbs only\n"
			"\n");
}

static void usage_gavb(void)
{
	usage_common("gavb");
	fprintf(stderr,
			"  <qinx> \n"
			"\n");
}

static void usage_fpe(void)
{
	usage_common("fpe");
	fprintf(stderr,
			" <TC preemption mask><RQ>\n"
			"             [preemption- bit 1 corresponding to TC else 0]\n"
			"             [RQ - queue for unfiltered preemptable packets\n"
			"		 - RQ can't be Queue 0]\n"
			" example to set preemption for tx queues, 1 and 2 (bit representation 0x6), and rq 2"
			" ../eqos_ioctl_app eth2 fpe 0x6 2"
			"\n");
}

static void usage_est(void)
{
	usage_common("est");
	fprintf(stderr,
			" <enable/disable> <base-time> <base-time offset> <cycle time> <number of GCL entry> <GCL-entries>\n"
			"             [enable - 1, disable - 0]\n"
			"	      [base time <nsec> <sec> put 0 0 if not sure in decimal\n"
			"	      [base time offset <nsec> <sec> in decimal\n"
			"	      [cycle time <nsec> <sec> in HEX]\n"
		        "	      [number of GCL entry max index 255 in decimal]\n"
			"	      [0x<8 bit gate mask> <24 bit interval in ns>]\n"
			"	      [0x<8 bit gate mask> <24 bit interval in ns>]\n"
			"	      : "
			"	      [0x<8 bit gate mask> <interval in ns>]\n"
			"\n");
}

static void usage_frp_add(void)
{
	usage_common("frp_add");
	fprintf(stderr,
			" <ID> <Match> <Type> <Filter Mode> <Offset> <OKI> <DMASel>\n"
			"             [Id - FRP table ID to add (0 to 255)\n"
			"              Match - Match data is used for comparing\n"
			"                     MAX 12 bytes data\n"
			"              Type - Match data type\n"
			"                     0 - Normal data\n"
			"                     1 - L2 DA MAC\n"
			"                     2 - L2 SA MAC\n"
			"                     3 - L3 Source IP\n"
			"                     4 - L3 Destination IP\n"
			"                     5 - L4 UDP Source Port\n"
			"                     6 - L4 UDP Destination Port\n"
			"                     7 - L4 TCP Source Port\n"
			"                     8 - L4 TCP Destination Port\n"
			"                     9 - VLAN Tag\n"
			"              Mode - Filter mode for the entry\n"
			"                     0 - Accept and route\n"
			"                     1 - Reject and Drop\n"
			"                     2 - Accept and Bypass FRP Route\n"
			"                     3 - Link to OKI Index\n"
			"                     4 - Inverse the Match, Accept and route\n"
			"                     5 - Inverse the Match, Reject and Drop\n"
			"                     6 - Inverse the Match, Accept and Bypass FRP Route\n"
			"                     7 - Inverse the Match, Link to OKI Index\n"
			"              Offset -  Frame offset of Match data\n"
			"              OKI -  When NIC set give the value for Next Instruction\n"
			"              DMASel - Bit selection of DMA channels to route the frame\n"
			"                          Bit[0] - DMA channel 1\n"
			"                          ...\n"
			"                          Bit [N] - DMA channel N]\n"
			"                       Note: The Multiple DMA channel selection only works\n"
			"                             for MC/BC packets and Highest RXQ need to be enabled on the DT\n"
			"\n");
}

static void usage_frp_update(void)
{
	usage_common("frp_update");
	fprintf(stderr,
			" <ID> <Match> <Type> <Filter Mode> <Offset> <OKI> <DMASel>\n"
			"             [Id - FRP table ID to add (0 to 255)\n"
			"              Match - Match data is used for comparing\n"
			"                     MAX 12 bytes data\n"
			"              Type - Match data type\n"
			"                     0 - Normal data\n"
			"                     1 - L2 DA MAC\n"
			"                     2 - L2 SA MAC\n"
			"                     3 - L3 Source IP\n"
			"                     4 - L3 Destination IP\n"
			"                     5 - L4 UDP Source Port\n"
			"                     6 - L4 UDP Destination Port\n"
			"                     7 - L4 TCP Source Port\n"
			"                     8 - L4 TCP Destination Port\n"
			"                     9 - VLAN Tag\n"
			"              Mode - Filter mode for the entry\n"
			"                     0 - Accept and route\n"
			"                     1 - Reject and Drop\n"
			"                     2 - Accept and Bypass FRP Route\n"
			"                     3 - Link to OKI Index\n"
			"                     4 - Inverse the Match, Accept and route\n"
			"                     5 - Inverse the Match, Reject and Drop\n"
			"                     6 - Inverse the Match, Accept and Bypass FRP Route\n"
			"                     7 - Inverse the Match, Link to OKI Index\n"
			"              Offset -  Frame offset of Match data\n"
			"              OKI -  When NIC set give the value for Next Instruction\n"
			"              DMASel - Bit selection of DMA channels to route the frame\n"
			"                          Bit[0] - DMA channel 1\n"
			"                          ...\n"
			"                          Bit [N] - DMA channel N]\n"
			"                       Note: The Multiple DMA channel selection only works\n"
			"                             for MC/BC packets and Highest RXQ need to be enabled on the DT\n"
			"\n");
}

static void usage_frp_del(void)
{
	usage_common("frp_del");
	fprintf(stderr,
			" <ID> \n"
			"             [Id - FRP table ID to update (0 to 255)]\n"
			"\n");
}
static void usage_l2_filter(void)
{
	usage_common("l2_filter");
	fprintf(stderr,
			"  <filter_no> <enable/disable> <mac addr>\n"
			"             [filter_no - 0 to 31\n"
			"              enable/disable\n"
			"                0 - to disable filter, 1 - to enable filter\n"
			"              mac addr - MAC address(eq-94:18:82:71:ae:1d)]\n"
			" example - ./eqos_ioctl_app mgbe1_0 l2_filter 9 1 00:10:18:2b:8f:8a"
			"\n");
}

static void ether_version(void)
{
    fprintf(stderr, "ether_devtest version is %u\n", ETHER_DEVTETST_VER);
}

static void print_all_usage(void)
{
	fprintf(stderr,
			"\nUsage:\n"
			"  %s <interface_name> <command> <parameters>\n\n",exe_name);
	print_all = 1;
	usage_avb();
	usage_gavb();
	usage_fpe();
	usage_est();
	usage_frp_add();
	usage_frp_del();
	usage_frp_update();
	ether_version();
	usage_l2_filter();
	print_all = 0;
}

static int ether_get_connected_speed(int sockfd, char *ifname)
{
	struct ifreq ifr;
	struct ethtool_cmd ethtool_cmd;
	int ret = 0;

	ethtool_cmd.cmd = ETHTOOL_GSET;
	ifr.ifr_data = (caddr_t)&ethtool_cmd;
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);

	ret = ioctl(sockfd, SIOCETHTOOL, &ifr);
	if (ret < 0)
		printf("IOCTL Error\n");
	else
		printf("Speed is %dMBps\n", ethtool_cmd.speed);

	return ethtool_cmd.speed;
}

static unsigned int get_idle_slope(unsigned char bw)
{
	unsigned int multiplier = 4;
	unsigned int idle_slope = 0;

	/**
	 * multiplier values are derived based on Link Speed
	 * which is related to PHY
	 *
	 * multiplier is 4 [for MII], 8 [for GMII] * or 32 [forXGMII]
	 * */
	if (connected_speed == SPEED_1000) {
		multiplier = 8;
	} else if (connected_speed == SPEED_2500) {
		multiplier = 32;
	} else if (connected_speed == SPEED_10000) {
		multiplier = 32;
	} else {
		multiplier = 4;
	}

	 /**
	  * idleslope = ((bandwidth/100) * multiplier * 1024
	  * 1024 is multiplied for normalizing the calculated value
	  * */
	idle_slope = (((multiplier * bw) * 1024)/100);

	return idle_slope;
}

static unsigned int get_send_slope(unsigned char bw)
{
	unsigned int multiplier = 4;
	unsigned int idle_slope = 0;
	unsigned int send_slope = 0;

	/**
	 * multiplier values are derived based on Link Speed
	 * which is related to PHY
	 *
	 * multiplier is 4 [for MII], 8 [for GMII] * or 32 [forXGMII]
	 */
	if (connected_speed == SPEED_1000) {
		multiplier = 8;
	} else if (connected_speed == SPEED_2500) {
		multiplier = 32;
	} else if (connected_speed == SPEED_10000) {
		multiplier = 32;
	} else {
		multiplier = 4;
	}


	/**
	 * sendslope = (((100 - bandwidth)/100) * multiplier * 1024
	 * 1024 is multiplied for normalizing the calculated value
	 * OR
	 * sendslope = (1024 * multiplier) - idle_slope)
	 */
	idle_slope = (((multiplier * bw) * 1024)/100);
	send_slope = ((multiplier * 1024) - idle_slope);

	return send_slope;
}

static unsigned int get_hi_credit(unsigned char bw)
{
	unsigned int hi_credit = 0;
	typedef unsigned long u64;

	hi_credit = (u64)(((u64)ETHER_MAX_INT_FRAME_SIZE * (u64)bw * 1024))/100;
	return hi_credit;
}

static unsigned int get_low_credit(unsigned char bw)
{
	int low_credit = 0;
	typedef unsigned long u64;

	low_credit =  -((u64)(((u64)ETHER_MAX_INT_FRAME_SIZE * (100 - bw)) * 1024)/100);

	return low_credit;
}

static int program_fpe(int sockfd, char *ifname, char *argv1,
		char *argv2)
{
	struct ifreq ifr;
	struct ether_exported_ifr_data data;
	struct osi_fpe_config cfg;
	int tx_preemption_flag = strtol(argv1, NULL, 16);
	int rq = atoi(argv2);
	int ret = -1;

	data.ifcmd = ETHER_CONFIG_FPE;
	if (tx_preemption_flag < 0 ||
	    tx_preemption_flag > 255) {
		printf("Invalid argument\n");
		usage_fpe();
		return ret;
	}

	if (tx_preemption_flag != 0x0 && (rq <= 0  || rq >= 10)) {
		printf("Invalid RQ argument\n");
		usage_fpe();
		return ret;
	}
	cfg.tx_queue_preemption_enable = tx_preemption_flag;
	cfg.rq = rq;
	data.ptr = &cfg;
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);
	ifr.ifr_ifru.ifru_data = &data;

	ret = ioctl(sockfd, SIOCDEVPRIVATE, &ifr);
	if (ret < 0)
		printf("IOCTL Error in %s()\n",__func__);
	else
		printf("Configured fpe successfully\n");

	return ret;
}

static int program_est(int sockfd, char *ifname, char *argv1,
		char *argv2, char *argv3, char *argv4,
		char *argv5, char *argv6, char *argv7,
		char *argv8, int argc, char *argv[])
{
	struct ifreq ifr;
	struct ether_exported_ifr_data data;
	struct osi_est_config cfg;
	int ret = -1;
	int i = 0;

	cfg.en_dis = atoi(argv1);
	cfg.btr[0] = atoi(argv2);
	cfg.btr[1] = atoi(argv3);
	cfg.btr_offset[0] = atoi(argv4);
	cfg.btr_offset[1] = atoi(argv5);
	cfg.ctr[0] = strtol(argv6, NULL, 16);
	cfg.ctr[1] = strtol(argv7, NULL, 16);
	printf("est en_dis = 0x%x \n",cfg.en_dis);
	printf("btr(nsec) = 0x%x \n",cfg.btr[0]);
	printf("btr(sec) = 0x%x \n",cfg.btr[1]);
	printf("btr_offset(nsec) = 0x%x \n",cfg.btr_offset[0]);
	printf("btr_offset(sec) = 0x%x \n",cfg.btr_offset[1]);
	printf("ctr(nsec) = 0x%x \n",cfg.ctr[0]);
	printf("ctr(sec) = 0x%x \n",cfg.ctr[1]);

	if (cfg.ctr[1] > 0xFF) {
		printf("Invalid argument CTR\n");
		usage_est();
		return ret;
	}

	cfg.llr = atoi(argv8);
	cfg.ter = 0xFF;
	if(cfg.llr > 256 || cfg.llr <= 0) {
		printf("Invalid argument llr\n");
		usage_est();
		return ret;
	}
	printf("llr = 0x%x \n",cfg.llr);

	for (i = 0 ; i < (int)cfg.llr; i++) {
		if ((11 + i) > argc) {
			printf("unsufficient argument\n");
			break;
		}
		if(argv[11 + i] != NULL) {
			cfg.gcl[i] =  strtol(argv[11 + i],  NULL, 16);
			printf("GCL[%d] = 0x%x \n",i, cfg.gcl[i]);
		}
	}

	data.ifcmd = ETHER_CONFIG_EST;
	data.ptr = &cfg;
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);
	ifr.ifr_ifru.ifru_data = &data;

	ret = ioctl(sockfd, SIOCDEVPRIVATE, &ifr);
	if (ret < 0)
		printf("IOCTL Error in %s()\n",__func__);
	else
		printf("Configured est successfully\n");

	return ret;
}

static int program_avb_algorithm(int sockfd, char *ifname, char *argv1,
		char *argv2, char *argv3, char *argv4, char *argv5)
{
	struct ifreq ifr;
	struct ether_exported_ifr_data data;
	struct osi_core_avb_algorithm avb_struct= {0};
	int qInx = atoi(argv1);
	int algo = atoi(argv2);
	int bw = atoi(argv3);
	int cc = atoi(argv4);
	int tc = atoi(argv5);
	int ret = 0;

	if ((bw <= 0) || (bw > 100)) {
		printf("Invalid BW argment\n");
		return -1;
        }

	data.ifcmd = ETHER_AVB_ALGORITHM;
	data.qinx = qInx;
	avb_struct.qindex = qInx;
	avb_struct.algo = algo;
	avb_struct.credit_control = cc;
	avb_struct.idle_slope = get_idle_slope(bw);
	avb_struct.send_slope = get_send_slope(bw);
	avb_struct.hi_credit = get_hi_credit(bw);
	avb_struct.low_credit = get_low_credit(bw);
	if (algo == OSI_MTL_TXQ_AVALG_CBS) {
		avb_struct.oper_mode = OSI_MTL_QUEUE_AVB;
	} else {
		avb_struct.oper_mode = OSI_MTL_QUEUE_ENABLE;
	}
	avb_struct.tcindex = tc;
	data.ptr = &avb_struct;
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);
	ifr.ifr_ifru.ifru_data = &data;

	ret = ioctl(sockfd, SIOCDEVPRIVATE, &ifr);
	if (ret < 0)
		printf("IOCTL Error\n");
	else
		printf("Configured AVB Algorithm parameters successfully\n");

	return ret;
}

static int get_avb_algorithm(int sockfd, char *ifname, char *argv1)
{
	struct ifreq ifr;
	struct ether_exported_ifr_data data;
	struct osi_core_avb_algorithm avb_struct= {0};
	int qInx = atoi(argv1);
	int ret = 0;

	data.ifcmd = ETHER_GET_AVB_ALGORITHM;
	data.qinx = qInx;
	avb_struct.qindex = qInx;
	avb_struct.algo = 0;
	avb_struct.credit_control = 0;
	avb_struct.idle_slope = 0;
	avb_struct.send_slope = 0;
	avb_struct.hi_credit = 0;
	avb_struct.low_credit = 0;
	avb_struct.oper_mode = -1;
	avb_struct.tcindex = 0;
	data.ptr = &avb_struct;
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);
	ifr.ifr_ifru.ifru_data = &data;

	ret = ioctl(sockfd, SIOCDEVPRIVATE, &ifr);
	if (ret < 0)
		printf("IOCTL Error\n");
	else {
		printf("getting AVB Algorithm parameters successfully\n");
		printf("values returned are index=%d, algo=%d, cc=%d, is=%d, ss=%d, hc=%d, lc=%d, opm=%d tcindex=%d\n",
			avb_struct.qindex, avb_struct.algo, avb_struct.credit_control, avb_struct.idle_slope,
			avb_struct.send_slope, avb_struct.hi_credit, avb_struct.low_credit,
			avb_struct.oper_mode, avb_struct.tcindex);
	}

	return ret;
}

static int config_frp_cmd(int sockfd, char *ifname, char *cmd, char *entry,
			  char *md, char *mt, char *fmode,
			  char *fo, char *ok_indx, char *dma_sel)
{
	int ret = 0, i = 0;
	struct ifreq ifr;
	struct ether_exported_ifr_data data;
	struct osi_core_frp_cmd frp_cmd;
	int cmd_t = atoi(cmd);
	int entry_t = atoi(entry);
	int length = 0;
	int mtype_t = atoi(mt);
	int fmode_t = atoi(fmode);
	int fo_t = atoi(fo);
	int ok_indx_t = strtol(ok_indx, NULL, 16);
	int dma_sel_t = strtol(dma_sel, NULL, 16);
	unsigned char *dst = frp_cmd.match, *end = frp_cmd.match + sizeof(frp_cmd.match);

	/* Prepare FRP command */
	/* Match Data in frp_cmd */
	memset(frp_cmd.match, 0U, OSI_FRP_MATCH_DATA_MAX);
	while (dst < end && sscanf(md, "%2x", &i) == 1)
	{
		*dst++ = i;
		md += 2;
		length++;
	}
	frp_cmd.cmd = cmd_t;
	frp_cmd.frp_id = entry_t;
	frp_cmd.match_type = mtype_t;
	frp_cmd.match_length = length;
	frp_cmd.offset = fo_t;
	frp_cmd.filter_mode = fmode_t;
	frp_cmd.next_frp_id = ok_indx_t;
	frp_cmd.dma_sel = dma_sel_t;

	/* Copy FRP Entry into IOCTL command */
	data.ifcmd = ETHER_CONFIG_FRP_CMD;
	data.ptr = &frp_cmd;
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);
	ifr.ifr_ifru.ifru_data = &data;

	ret = ioctl(sockfd, SIOCDEVPRIVATE, &ifr);
	if (ret < 0)
		printf("%s IOCTL Error\n", __func__);
	else
		printf("FRP command executed successfully\n");

	return ret;
}
static int config_l2_filters(int sockfd, char *ifname,
				char *filter_no,
				char *enable_disable,
				char *mac_addr)
{
	struct ifreq ifr;
	struct ether_exported_ifr_data data;
	struct ether_l2_filter l2_filter;
	int filter_en_dis = atoi(enable_disable);
	int ret = 0;

	memset(&l2_filter, 0x0, sizeof(struct ether_l2_filter));
	data.if_flags = atoi(enable_disable);
	data.ifcmd = ETHER_L2_ADDR;
	data.qinx = 0; /* Not used */
	strcpy(ifr.ifr_ifrn.ifrn_name, ifname);
	ifr.ifr_ifru.ifru_data = &data;

	l2_filter.index = atoi(filter_no);
	if (l2_filter.index < 0 || l2_filter.index >= 32) {
		printf("Filter number should be between 0 to 31 for this feature\n");
		return -1;
	}

	l2_filter.mac_addr[0] = strtol(strtok(mac_addr, ":"), NULL, 16);
	l2_filter.mac_addr[1] = strtol(strtok(NULL, ":"), NULL, 16);
	l2_filter.mac_addr[2] = strtol(strtok(NULL, ":"), NULL, 16);
	l2_filter.mac_addr[3] = strtol(strtok(NULL, ":"), NULL, 16);
	l2_filter.mac_addr[4] = strtol(strtok(NULL, ":"), NULL, 16);
	l2_filter.mac_addr[5] = strtol(strtok(NULL, ":"), NULL, 16);
	l2_filter.en_dis = filter_en_dis;

	data.ptr = &l2_filter;

	printf("%2x:%2x:%2x:%2x:%2x:%2x\n", l2_filter.mac_addr[0], l2_filter.mac_addr[1], l2_filter.mac_addr[2], l2_filter.mac_addr[3], l2_filter.mac_addr[4], l2_filter.mac_addr[5]);
	printf ("idx %d\n", l2_filter.index);
	ret = ioctl(sockfd, SIOCDEVPRIVATE, &ifr);
	if (ret < 0) {
		printf("IOCTL Error\n");
		printf("adding L2 filter is failed\n");
	} else
		printf("Successfully %s L2 DA filtering on %d filter index\n",
			(filter_en_dis ? "ENABLED" : "DISABLED"),
			l2_filter.index);

	return ret;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int sockfd;

	exe_name = strdup(argv[0]);

	if (argc >= 3) {
		sockfd = socket(PF_INET, SOCK_STREAM, 0);
		if_name = strdup(argv[1]);
		if (sockfd < 0) {
			printf("unable to open %s socket\n", argv[1]);
			return sockfd;
		}

		connected_speed = ether_get_connected_speed(sockfd, argv[1]);

		if (0 == strcmp(argv[2], "avb")) {
			if (argc < 8) {
				usage_avb();
				goto argc_failed;
			}
			ret = program_avb_algorithm(sockfd, argv[1], argv[3],
						    argv[4], argv[5], argv[6],
						    argv[7]);
		} else if (0 == strcmp(argv[2], "gavb")) {
			if (argc < 4) {
				usage_gavb();
				goto argc_failed;
			}
			ret = get_avb_algorithm(sockfd, argv[1], argv[3]);
		} else if (0 == strcmp(argv[2], "fpe")) {
			if (argc < 5) {
				usage_fpe();
				goto argc_failed;
			}
			ret = program_fpe(sockfd, argv[1], argv[3],
					  argv[4]);

		} else if (0 == strcmp(argv[2], "l2_filter")) {
			if (argc < 6) {
				usage_l2_filter();
				goto argc_failed;
			}
			ret = config_l2_filters(sockfd, argv[1],
					        argv[3], argv[4], argv[5]);

		} else if (0 == strcmp(argv[2], "est")) {
			if (argc < 12) {
				usage_est();
				goto argc_failed;
			}
			if (argc > 268) {
				printf("MAX 255 entry supported for experiment\n");
				usage_est();
				goto argc_failed;
			}
			ret = program_est(sockfd, argv[1], argv[3],
					  argv[4], argv[5], argv[6], argv[7],
					  argv[8], argv[9],argv[10],argc, argv);
		} else if (0 == strcmp(argv[2], "frp_add")) {
			if (argc != 10) {
				usage_frp_add();
				goto argc_failed;
			}
			ret = config_frp_cmd(sockfd, argv[1], "0", argv[3],
					     argv[4], argv[5], argv[6],
					     argv[7], argv[8], argv[9]);
		} else if (0 == strcmp(argv[2], "frp_update")) {
			if (argc != 10) {
				usage_frp_update();
				goto argc_failed;
			}
			ret = config_frp_cmd(sockfd, argv[1], "1", argv[3],
					     argv[4], argv[5], argv[6],
					     argv[7], argv[8], argv[9]);
		} else if (0 == strcmp(argv[2], "frp_del")) {
			if (argc != 4) {
				usage_frp_del();
				goto argc_failed;
			}
			ret = config_frp_cmd(sockfd, argv[1], "2", argv[3],
					     "0", "0", "0",
					     "0", "0", "0");
		} else {
			print_all_usage();
		}

		close(sockfd);
	} else {
		print_all_usage();
	}

	return ret;

argc_failed:
	printf("PLEASE SPECIFY CORRECT NUMBER OF ARGUMENTS\n");
	close(sockfd);


	return 0;
}
