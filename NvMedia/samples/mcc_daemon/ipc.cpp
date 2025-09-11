/*
 * Copyright (c) 2023 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>

#include "ipc.h"

#ifdef __QNX__
#include <sys/neutrino.h>
#endif

MccDaemonStatus_t wait_event(ipc_context_t *ipc_ctx, int idx, int32_t value, int64_t timeout_us)
{
    NvSciError err = NvSciError_Success;
    uint32_t event = 0;

    while(true) {
        event = 0;
        err = NvSciIpcGetEventSafe(ipc_ctx->ep[idx].h, &event);
        if (err != NvSciError_Success) {
            printf("MCC_Daemon: %s: Error in NvSciIpcGetEventSafe. ret: 0x%x\n",
                   __func__, err);
            return MccDaemonStatusError;
        }

        if (event & value) {
            return MccDaemonStatusOK;
        }

        err = ipc_ctx->eventLoopService->WaitForEvent(
                                          ipc_ctx->ep[idx].eventNotifier,
                                          timeout_us);
        if (err == NvSciError_Success) {
            continue;
        } else if (err == NvSciError_Timeout) {
            return MccDaemonStatusTimeout;
        } else {
            printf("MCC_Daemon: [chname=%s] WaitForEvent err: 0x%x\n",
                    ipc_ctx->ep[idx].chname, err);
            return MccDaemonStatusError;
        }
    }
}

MccDaemonStatus_t wait_event_multi_ep(ipc_context_t *ipc_ctx, int32_t value, bool gotEventArr[], int64_t timeout_us)
{
    NvSciError err = NvSciError_Success;
    uint32_t event = 0;
    bool gotEvent = false;

    while (true) {
        for (int i=0; i < ipc_ctx->num_eps; i++) {
            event = 0;
            err = NvSciIpcGetEventSafe(ipc_ctx->ep[i].h, &event);
            if (err != NvSciError_Success) {
                printf("MCC_Daemon: %s: Error in NvSciIpcGetEventSafe() err: 0x%x\n",
                       __func__, err);
                return MccDaemonStatusError;
            }

            if (event & value) {
                gotEventArr[i] = true;
                gotEvent = true;
            } else {
                gotEventArr[i] = false;
            }
        }
        if (gotEvent) {
            return MccDaemonStatusOK;
        }

        err = ipc_ctx->eventLoopService->WaitForMultipleEventsExt(
                                    &ipc_ctx->eventLoopService->EventService,
                                    ipc_ctx->evtNotifierArr, ipc_ctx->num_eps,
                                    timeout_us, gotEventArr);
        if (err == NvSciError_Success) {
            continue;
        } else if (err == NvSciError_Timeout) {
            return MccDaemonStatusTimeout;
        } else {
            printf("MCC_Daemon: WaitForMultipleEventsExt err: 0x%x\n", err);
            return MccDaemonStatusError;
        }
    }
    return MccDaemonStatusError;
}

void release_ipc_resources(ipc_context_t *ipc_ctx)
{
    NvSciError err = NvSciError_Success;

    for (int i=0; i < ipc_ctx->num_eps; i++) {
        ipc_ctx->ep[i].eventNotifier->Delete(ipc_ctx->ep[i].eventNotifier);

        err = NvSciIpcCloseEndpointSafe(ipc_ctx->ep[i].h, false);
        if (err != NvSciError_Success) {
            printf("MCC_Daemon: %s: Failed to close endpoint. err is: 0x%x\n",
                     __func__, err);
        }
    }

    NvSciIpcDeinit();

    ipc_ctx->eventLoopService->EventService.Delete(
                                 &ipc_ctx->eventLoopService->EventService);
}

MccDaemonStatus_t init_ipc_resources(ipc_context_t *ipc_ctx)
{
    MccDaemonStatus_t status = MccDaemonStatusOK;
    NvSciError err = NvSciError_Success;
    void *os_config = NULL; // NULL for Linux and Valid struct for QNX

#ifdef __QNX__
    struct nto_channel_config config = {0};
    config.num_pulses = 12U; /* Set this value as 4 times num of notifiers */
    config.rearm_threshold = 0;
    os_config = &config;
#endif

    err = NvSciEventLoopServiceCreateSafe(1, os_config, &ipc_ctx->eventLoopService);
    if (err != NvSciError_Success) {
        status = MccDaemonStatusError;
        printf("MCC_Daemon: %s: Failed to create event loop service. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    err = NvSciIpcInit();
    if (err != NvSciError_Success) {
        status = MccDaemonStatusError;
        printf("MCC_Daemon: %s: Failed to initialize NvSciIpc. err is: 0x%x\n",
                 __func__, err);
        goto fail;
    }

    /* Opening NvSciIpc endpoint */
    for (int i=0; i<ipc_ctx->num_eps; i++) {
        err = NvSciIpcOpenEndpointWithEventService(ipc_ctx->ep[i].chname,
                                       &ipc_ctx->ep[i].h,
                                       &ipc_ctx->eventLoopService->EventService);
        if (err != NvSciError_Success) {
            status = MccDaemonStatusError;
            printf("MCC_Daemon: %s: Failed to open NvSciIpc endpoint. err is: 0x%x\n",
                     __func__, err);
            goto fail;
        }

        /* for NvSciEventService */
        err = NvSciIpcGetEventNotifier(ipc_ctx->ep[i].h, &ipc_ctx->ep[i].eventNotifier);
        if (err != NvSciError_Success) {
            status = MccDaemonStatusError;
            printf("MCC_Daemon: %s: Failed to get event notifier. err is: 0x%x\n",
                     __func__, err);
            goto fail;
        }
        ipc_ctx->evtNotifierArr[i] = ipc_ctx->ep[i].eventNotifier;

        err = NvSciIpcGetEndpointInfo(ipc_ctx->ep[i].h, &ipc_ctx->ep[i].info);
        if (err != NvSciError_Success) {
            status = MccDaemonStatusError;
            printf("MCC_Daemon: %s: Failed to get endpoint info. err is: 0x%x\n",
                     __func__, err);
            goto fail;
        }
        debug_printf("MCC_Daemon: NvSciIpc Endpoint Info: \n");
        debug_printf("MCC_Daemon: nframes: %u \n", ipc_ctx->ep[i].info.nframes);
        debug_printf("MCC_Daemon: frame_size: %u \n", ipc_ctx->ep[i].info.frame_size);

        err = NvSciIpcResetEndpointSafe(ipc_ctx->ep[i].h);
        if (err != NvSciError_Success) {
            status = MccDaemonStatusError;
            printf("MCC_Daemon: %s: Failed to reset endpoint. err is: 0x%x\n",
                     __func__, err);
            goto fail;
        }
    }

fail:
    return status;
}

MccDaemonStatus_t write_ipc_rawbuf(ipc_context_t *ipc_ctx, int idx, uint8_t *buf, uint32_t buf_size)
{
    MccDaemonStatus_t status = MccDaemonStatusOK;
    uint32_t bytes;
    NvSciError err = NvSciError_NotInitialized;

    if (buf == NULL) {
        status = MccDaemonStatusBadParameter;
        printf("MCC_Daemon: %s: [chname=%s] Buffer to Write is Empty!\n",
                __func__, ipc_ctx->ep[idx].chname);
        goto fail;
    }

    err = NvSciIpcWriteSafe(ipc_ctx->ep[idx].h, buf, buf_size, &bytes);
    if(err != NvSciError_Success) {
        status = MccDaemonStatusError;
        printf("MCC_Daemon: %s: [chname=%s] Failed to NvSciIpcWriteSafe. err is: 0x%x\n",
                 __func__, ipc_ctx->ep[idx].chname, err);
    }

fail:
    return status;
}

MccDaemonStatus_t read_ipc_rawbuf(ipc_context_t *ipc_ctx, int idx, uint8_t *buf, uint32_t buf_size)
{
    MccDaemonStatus_t status = MccDaemonStatusOK;
    uint32_t bytes;
    NvSciError err = NvSciError_NotInitialized;

    if (buf == NULL) {
        status = MccDaemonStatusBadParameter;
        printf("MCC_Daemon: %s: [chname=%s] Buffer to Return IPC Read is Empty!\n",
                __func__, ipc_ctx->ep[idx].chname);
        goto fail;
    }

    err = NvSciIpcReadSafe(ipc_ctx->ep[idx].h, buf, buf_size, &bytes);
    if(err != NvSciError_Success) {
        status = MccDaemonStatusError;
        printf("MCC_Daemon: %s: [chname=%s] Failed to NvSciIpcReadSafe. err is: 0x%x\n",
                 __func__, ipc_ctx->ep[idx].chname, err);
    }

fail:
    return status;
}
