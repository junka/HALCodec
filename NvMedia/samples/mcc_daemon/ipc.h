/*
 * Copyright (c) 2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef MCC_DAEMON_IPC_H__
#define MCC_DAEMON_IPC_H__

/* Header File for SciIPC and EventService related Params and Objects */

#include <nvsciipc.h>
#include "common.h"

#define IPC_CH_NAME_IST "nvmcc_ist_ipc_0"
#define IPC_CH_NAME_DU  "nvmcc_du_ipc_0"
#define IPC_CH_NAME_CIF "nvmcc_cif_ipc_0"

/* Data structure representing the NvSciIpcEndpoint related parameters. */
struct endpoint
{
    char chname[NVSCIIPC_MAX_ENDPOINT_NAME];    /* endpoint name */
    NvSciIpcEndpoint h;  /* NvSciIpc handle */
    struct NvSciIpcEndpointInfo info; /* endpoint info */
    NvSciEventNotifier *eventNotifier;
};

/* Data structure representing the IPC related Context Parameters. */
typedef struct ipc_context {
    NvSciEventLoopService *eventLoopService;
    int num_eps; /* Number of Endpoints */
    struct endpoint ep[MAX_NUM_CHANNELS];
    NvSciEventNotifier *evtNotifierArr[MAX_NUM_CHANNELS];
} ipc_context_t;

/* One-Time init/deinit Functions for IPC */
MccDaemonStatus_t init_ipc_resources(ipc_context_t *ipc_ctx);
void release_ipc_resources(ipc_context_t *ipc_ctx);

MccDaemonStatus_t wait_event(ipc_context_t *ipc_ctx, int idx, int32_t value, int64_t timeout_us);
MccDaemonStatus_t wait_event_multi_ep(ipc_context_t *ipc_ctx, int32_t value, bool gotEventArr[], int64_t timeout_us);

MccDaemonStatus_t write_ipc_rawbuf(ipc_context_t *ipc_ctx, int idx, uint8_t *buf, uint32_t buf_size);
MccDaemonStatus_t read_ipc_rawbuf(ipc_context_t *ipc_ctx, int idx, uint8_t *buf, uint32_t buf_size);

#endif /*MCC_DAEMON_IPC_H__*/
