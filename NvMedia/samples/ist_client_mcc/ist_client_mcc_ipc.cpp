/*
 * Copyright (c) 2023, NVIDIA Corporation. All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation. Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include "ist_client_mcc_types.h"
#include "ist_client_mcc_ipc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __QNX__
#include <sys/neutrino.h>
#endif

#define TIMEOUT_WAIT_FOR_EVENT_US	(1000000U) //1sec
#define MAX_RETRY_COUNT				(9)
#define IPC_ENDPOINT				"nvmcc_ist_ipc_1"
#define VERBOSE						(0)

typedef struct ISTClient_MCC_IPC_t_ {
	NvSciEventNotifier *eventNotifier;
	NvSciEventLoopService *eventLoopService;
	NvSciIpcEndpoint hIpc;
} ISTClient_MCC_IPC_t;

#ifdef __QNX__
	static struct nto_channel_config config = {
		.num_pulses = 3,
		.rearm_threshold = 0,
	};
#endif /* __QNX__ */

void *ISTClient_mcc_IPC_init()
{
	NvSciError err = NvSciError_Success;
	void *os_config = NULL;
	ISTClient_MCC_IPC_t *mcc_ipc = NULL;
	char ipc_chname[NVSCIIPC_MAX_ENDPOINT_NAME];	/* endpoint name */

	ISTClient_mcc_trace_entry(__func__);
	int n = snprintf(ipc_chname, NVSCIIPC_MAX_ENDPOINT_NAME-1, "%s", IPC_ENDPOINT);
	if (n < 0) {
		ISTClient_mcc_log("[ERROR-IPC] ISTClient_mcc_IPC_init(): Failed to write IST ipc chname to local string\n");
		goto fail;
	}

	mcc_ipc = (ISTClient_MCC_IPC_t *)malloc(sizeof(ISTClient_MCC_IPC_t));
	if (NULL == mcc_ipc) {
		ISTClient_mcc_log("[ERROR-IPC] ISTClient_mcc_IPC_init(): Failed to allocate mcc_ipc handle\n");
		goto fail;
	}

	memset(mcc_ipc, 0, sizeof(ISTClient_MCC_IPC_t));

#ifdef __QNX__
	os_config = &config;
#endif

	err = NvSciEventLoopServiceCreateSafe(1, os_config, &mcc_ipc->eventLoopService);
	if (err != NvSciError_Success) {
		ISTClient_mcc_log_x64("[ERROR-IPC] ISTClient_mcc_IPC_init(): NvSciEventLoopServiceCreateSafe() error",
					(uint64_t)err);
		goto fail;
	}

	err = NvSciIpcInit();
	if (err != NvSciError_Success) {
		ISTClient_mcc_log_x64("[ERROR-IPC] ISTClient_mcc_IPC_init(): NvSciIpcInit() error", (uint64_t)err);
		goto fail;
	}

	err = NvSciIpcOpenEndpointWithEventService(ipc_chname, &(mcc_ipc->hIpc),
							&((mcc_ipc->eventLoopService)->EventService));
	if (err != NvSciError_Success) {
		ISTClient_mcc_log_x64("[ERROR-IPC] ISTClient_mcc_IPC_init(): NvSciIpcOpenEndpointWithEventService() error",
					(uint64_t)err);
		goto fail;
	}

	err = NvSciIpcGetEventNotifier(mcc_ipc->hIpc, &(mcc_ipc->eventNotifier));
	if (err != NvSciError_Success) {
		ISTClient_mcc_log_x64("[ERROR-IPC] ISTClient_mcc_IPC_init(): NvSciIpcGetEventNotifier() error",
					(uint64_t)err);
		goto fail;
	}

	struct NvSciIpcEndpointInfo info;
	err = NvSciIpcGetEndpointInfo(mcc_ipc->hIpc, &info);
	if (err != NvSciError_Success) {
		ISTClient_mcc_log_x64("[ERROR-IPC] ISTClient_mcc_IPC_init(): NvSciIpcGetEndpointInfo() error",
					(uint64_t)err);
		goto fail;
	}
	ISTClient_mcc_log_u32("[INFO-IPC] NvSciIpc Endpoint Info: [nframes]", (uint32_t)info.nframes);
	ISTClient_mcc_log_u32("[INFO-IPC] NvSciIpc Endpoint Info: [frame_size]", (uint32_t)info.frame_size);

	err = NvSciIpcResetEndpointSafe(mcc_ipc->hIpc);
	if (err != NvSciError_Success) {
		ISTClient_mcc_log_x64("[ERROR-IPC] ISTClient_mcc_IPC_init(): NvSciIpcResetEndpointSafe() error",
					(uint64_t)err);
		goto fail;
	}

	ISTClient_mcc_log("[<==] ISTClient_mcc_IPC_init(): [OK]\n");
	return mcc_ipc;

fail:
	if (mcc_ipc != NULL) {
		ISTClient_mcc_IPC_deinit(mcc_ipc);
		mcc_ipc = NULL;
	}

	ISTClient_mcc_log("[<==] ISTClient_mcc_IPC_init(): [NULL]\n");
	return NULL;
}

int ISTClient_mcc_IPC_deinit(void *mcc_IPC_handle)
{
	ISTClient_MCC_IPC_t *pHandle = (ISTClient_MCC_IPC_t*)mcc_IPC_handle;

	if (pHandle->eventNotifier) {
		pHandle->eventNotifier->Delete(pHandle->eventNotifier);
		pHandle->eventNotifier = NULL;
	}

	(void)NvSciIpcCloseEndpointSafe(pHandle->hIpc, false);

	NvSciIpcDeinit();

	if (pHandle->eventLoopService) {
		pHandle->eventLoopService->EventService.Delete(&(pHandle->eventLoopService->EventService));
		pHandle->eventLoopService = NULL;
	}

	free(pHandle);

	return 0;
}

static int wait_event(NvSciEventLoopService *eventLoopService,
					NvSciEventNotifier *eventNotifier,
					NvSciIpcEndpoint hIpc,
					int32_t value,
					int32_t timeout,
					int32_t retry_count)
{
	NvSciError err = NvSciError_Success;
	uint32_t event = 0;

	while (retry_count >= 0) {
		event = 0;
		err = NvSciIpcGetEventSafe(hIpc, &event);
		if (err != NvSciError_Success) {
			ISTClient_mcc_log_x64("[ERROR-IPC] wait_event(): NvSciIpcGetEventSafe() error", (uint64_t)err);
			return -EIO;
		}

		if (event & value) {
			return 0;
		}

		err = eventLoopService->WaitForEvent(eventNotifier, timeout);
		if (err == NvSciError_Timeout) {
#if VERBOSE
			ISTClient_mcc_log_s32("[INFO-IPC] WaitForEvent() timeout: retries remaining", retry_count);
#endif
			retry_count--;
		}
		else if (err != NvSciError_Success) {
			ISTClient_mcc_log_x64("[ERROR-IPC] wait_event(): WaitForEvent() error", (uint64_t)err);
			break;
		}
	}

	return -ETIMEDOUT;
}

static int write_msg_ipc(NvSciIpcEndpoint hIpc, const uint8_t* buf, uint32_t buf_size)
{
	uint32_t bytes;
	NvSciError err = NvSciError_NotInitialized;
	int ret = 0;

	err = NvSciIpcWriteSafe(hIpc, buf, buf_size, &bytes);

	if (!(err == NvSciError_Success)) {
		/* Write failed */
		ret = -EINVAL;
		ISTClient_mcc_log_x64("[ERROR-IPC] read_msg_ipc(): NvSciIpcWriteSafe() error", (uint64_t)err);
	}

	return ret;
}

static int read_msg_ipc(NvSciIpcEndpoint hIpc, uint8_t* buf, uint32_t buf_size)
{
	uint32_t bytes;
	NvSciError err = NvSciError_NotInitialized;
	int ret = 0;

	err = NvSciIpcReadSafe(hIpc, buf, buf_size, &bytes);
	if (!(err == NvSciError_Success)) {
		ret = -EINVAL;
		ISTClient_mcc_log_x64("[ERROR-IPC] read_msg_ipc(): NvSciIpcReadSafe() error", (uint64_t)err);
	}

	return ret;
}

int ISTClient_mcc_IPC_connect(void *mcc_IPC_handle)
{
	int ret = 0;
	ISTClient_MCC_IPC_t *pHandle = (ISTClient_MCC_IPC_t*)mcc_IPC_handle;

	ret = wait_event(pHandle->eventLoopService,
					pHandle->eventNotifier,
					pHandle->hIpc,
					NV_SCI_IPC_EVENT_CONN_EST,
					TIMEOUT_WAIT_FOR_EVENT_US,
					MAX_RETRY_COUNT);
	if (ret != 0) {
		if (ret == -ETIMEDOUT) {
			ISTClient_mcc_log("[WARN] ISTClient_mcc_IPC_connect(): wait_event() timed out\n");
		} else {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_IPC_connect(): wait_event() error\n", ret);
		}
	}

	return ret;
}

int ISTClient_mcc_IPC_sendmsg(void *mcc_IPC_handle, const ist_client_rsp_t *tx_msg)
{
	int ret = 0;
	ISTClient_MCC_IPC_t *pHandle = (ISTClient_MCC_IPC_t*)mcc_IPC_handle;

	ret = wait_event(pHandle->eventLoopService,
					pHandle->eventNotifier,
					pHandle->hIpc,
					NV_SCI_IPC_EVENT_WRITE,
					TIMEOUT_WAIT_FOR_EVENT_US,
					MAX_RETRY_COUNT);
	if (ret != 0) {
		if (ret == -ETIMEDOUT) {
			ISTClient_mcc_log("[WARN] ISTClient_mcc_IPC_sendmsg(): wait_event() timed out\n");
		} else {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_IPC_sendmsg(): wait_event() error", ret);
		}
		return ret;
	}

	uint8_t* send_msg = (uint8_t*)tx_msg;
	uint32_t Pkt_size = sizeof(*tx_msg);

#if VERBOSE
	fprintf(stderr, "[INFO-IPC] Attempting Write : ");
	for (uint32_t idx = 0; idx < Pkt_size; idx++) {
		fprintf(stderr, "0x%x ", send_msg[idx]);
	}
	fprintf(stderr, "\n");
#endif

	/* Write to IPC Endpoint */
	ret = write_msg_ipc(pHandle->hIpc, send_msg, Pkt_size);
	if (ret != 0) {
		ISTClient_mcc_log_s32("[ERROR-IPC] ISTClient_mcc_IPC_sendmsg(): write_msg_ipc() error", ret);
		goto fail;
	}

#if VERBOSE
	fprintf(stderr, "[INFO-IPC] Write complete");
#endif

fail:
	return ret;
}

int ISTClient_mcc_IPC_recvmsg(void *mcc_IPC_handle, ist_client_cmd_t *rx_msg)
{
	int ret = 0;
	ISTClient_MCC_IPC_t *pHandle = (ISTClient_MCC_IPC_t*)mcc_IPC_handle;

	/* Wait for message from MCC daemon */
	ret = wait_event(pHandle->eventLoopService,
					pHandle->eventNotifier,
					pHandle->hIpc,
					NV_SCI_IPC_EVENT_READ,
					TIMEOUT_WAIT_FOR_EVENT_US,
					1);
	if (ret != 0) {
		if (ret == -ETIMEDOUT) {
			ISTClient_mcc_log("[WARN] ISTClient_mcc_IPC_recvmsg(): wait_event() timed out\n");
		} else {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_IPC_recvmsg(): wait_event() error", ret);
		}
		return ret;
	}

	/* Read from IPC Endpoint */
	ret = read_msg_ipc(pHandle->hIpc, (uint8_t*)rx_msg, sizeof(*rx_msg));
	if (ret != 0) {
		return -EINVAL;
	}

#if VERBOSE
	fprintf(stderr, "[INFO-IPC] Read packet : ", rx_msg);
	uint8_t *ptr = rx_msg->bytes;
	for (uint32_t idx = 0; idx < sizeof(*rx_msg); idx++) {
		fprintf(stderr, "0x%x ", ptr[idx]);
	}
	fprintf(stderr, "\n");
#endif

	return ret;
}
