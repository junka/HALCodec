/*
 * Copyright (c) 2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef MCC_DAEMON_NVSOCKET_H__
#define MCC_DAEMON_NVSOCKET_H__

/* Header File for NvSocket related Params and Objects */

#include "common.h"
#include <memory>
#include "nvsocketwrapper_message.h"
#include "nvsocketwrapper_endpoint.h"

/* Data structure representing one UDP based NvSocket Endpoint related parameters. */
typedef struct udp_endpoint
{
    int portNum; /* UDP endpoint port number */
    std::shared_ptr<NvSocketWrapper::Endpoint> endPoint;
    std::shared_ptr<NvSocketWrapper::Message> txRxMsg;
} udp_endpoint_t;

/* Data structure representing the NvSocket related Context Parameters. */
typedef struct nvsocket_context {
    struct udp_endpoint udp_ep[MAX_NUM_PORTS];
} nvsocket_context_t;

/* One-Time init/deinit Functions for NvSocket */
MccDaemonStatus_t init_nvsocket_resources(char *aurix_ip_addr, char *client_ip_addr, nvsocket_context_t *nvsocket_ctx, int idx);
void release_nvsocket_resources(nvsocket_context_t *nvsocket_ctx, int idx);

MccDaemonStatus_t wait_n_recv_nvsocket_msg(udp_endpoint_t *udp_ep, int64_t timeout_us, uint8_t *buf, uint32_t buf_size);
MccDaemonStatus_t send_nvsocket_msg(udp_endpoint_t *udp_ep, uint8_t *buf, uint32_t buf_size);

#endif /*MCC_DAEMON_NVSOCKET_H__*/
