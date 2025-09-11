/*
 * Copyright (c) 2022-2023, NVIDIA Corporation. All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation. Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */

#include "ist_client_mcc_types.h"
#include "ist_client_mcc_ipc.h"

#include <ist_client_mcc.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__QNX__)
#include <sys/procmgr.h>
#endif

/**
 * @brief Referenced documents
 * [TKT:NVBUG]
 *  - https://nvbugswb.nvidia.com/NvBugs5/
 */

/* Maximum number of times to retry sending CLIENT_READY if MCU does not respond */
#define MAX_CLIENT_READY_RETRIES 5

typedef struct {
	void *ipc_handle;
} ISTClient_mcc_t;

/**
 * Allocate and initialize MCC lib instance
 *
 * @returns void * : On success, mcc_handle : On error, NULL
 */
static void *ISTClient_mcc_init(void);

/**
 * Deinitialize and cleanup MCC lib instance
 *
 * @param [in] mcc_handle: handle returned by ISTClient_mcc_init()
 * @returns 0 on success
 */
static int ISTClient_mcc_deinit(void *mcc_handle);

static void *ISTClient_mcc_init(void)
{
	ISTClient_mcc_t *mcc = NULL;
	void *mcc_handle = NULL;

	ISTClient_mcc_trace_entry(__func__);

	/* Allocate and init memory for mcc handle */
	mcc = (ISTClient_mcc_t *)malloc(sizeof(ISTClient_mcc_t));
	if (NULL == mcc) {
		ISTClient_mcc_log("[ERROR] ISTClient_mcc_init(): Failed to allocate mcc_handle\n");
		goto done;
	}
	memset(mcc, 0, sizeof(ISTClient_mcc_t));

	mcc->ipc_handle = ISTClient_mcc_IPC_init();
	if (mcc->ipc_handle == NULL) {
		ISTClient_mcc_log("[ERROR] ISTClient_mcc_init(): ISTClient_mcc_IPC_init() ==> [NULL]\n");
		goto done;
	}

	mcc_handle = mcc;
done:
	/* Cleanup allocated memory if init failed */
	if ((NULL == mcc_handle) && (mcc != NULL)) {
		ISTClient_mcc_deinit(mcc);
	}

	if (mcc_handle == NULL) {
		ISTClient_mcc_log("[<==] ISTClient_mcc_init(): [NULL]\n");
	} else {
		ISTClient_mcc_log("[<==] ISTClient_mcc_init(): [OK]\n");
	}

	return mcc_handle;
}

static int ISTClient_mcc_deinit(void *mcc_handle)
{
	ISTClient_mcc_t *mcc = NULL;
	int ret = -EINVAL;

	ISTClient_mcc_trace_entry(__func__);

	/* Sanity check input params */
	if (NULL == mcc_handle) {
		ISTClient_mcc_log("[ERROR] ISTClient_mcc_deinit(): Invalid mcc_handle\n");
		goto done;
	}
	mcc = (ISTClient_mcc_t *)mcc_handle;

	if (mcc->ipc_handle != NULL) {
		ISTClient_mcc_IPC_deinit(mcc->ipc_handle);
		mcc->ipc_handle = NULL;
	}

	free(mcc);
	ret = 0;
done:
	ISTClient_mcc_trace_exit(__func__, (int64_t)ret);
	return ret;
}

#if defined(__QNX__)
/**
 * @brief Drop and lock QNX abilities used by MCC for root and non-root users
 *
 * @returns EOK on success
 * [TKT:NVBUG:4126596] [D6.0] QNX BSP:Guidelines to drop privileges
 */
static int ISTClient_mcc_drop_privs(void)
{
	int ret = EOK;
	unsigned ability_id = 0;
	const unsigned ability_cfg = PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT |
				     PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK;

	ret = procmgr_ability(0, PROCMGR_AID_INTERRUPTEVENT | ability_cfg, PROCMGR_AID_EOL);
	if (EOK != ret) {
		ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_drop_privs(): Failed to drop 'interruptevent' ability", ret);
		return ret;
	}

	ret = procmgr_ability(0, PROCMGR_AID_PUBLIC_CHANNEL | ability_cfg, PROCMGR_AID_EOL);
	if (EOK != ret) {
		ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_drop_privs(): Failed to drop 'public_channel' ability", ret);
		return ret;
	}

	ret =  procmgr_ability_lookup("NvSciIpcEndpoint");
	if (ret < 0) {
		ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_drop_privs(): procmgr_ability_lookup(NvSciIpcEndpoint)", ret);
		return ret;
	}
	ability_id = (unsigned)ret;

	ret = procmgr_ability(0, ability_id | ability_cfg, PROCMGR_AID_EOL);
	if (EOK != ret) {
		ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_drop_privs(): Failed to drop 'NvSciIpcEndpoint' ability", ret);
		return ret;
	}

	ISTClient_mcc_log("[INFO] ISTClient_mcc_drop_privs(): Dropped 'interruptevent','public_channel','NvSciIpcEndpoint' abilities\n");
	return ret;
}
#endif /* defined(__QNX__) */

static int ISTClient_mcc_send_results_inner(ist_client_result_t result)
{
	ISTClient_mcc_t *mcc = NULL;
	int ret = -EINVAL;
	int err = 0;
	ist_client_cmd_t rx_msg = {0};
	ist_client_msg_t *rx_cmd = NULL;
	ist_client_rsp_t tx_msg = {0};
	int32_t client_ready_retries = MAX_CLIENT_READY_RETRIES;

	ISTClient_mcc_trace_entry(__func__);

	mcc = ISTClient_mcc_init();
	if (NULL == mcc) {
		ISTClient_mcc_log("[ERROR] ISTClient_mcc_send_results_inner() : ISTClient_mcc_init(...) ==> NULL\n");
		goto done;
	}

	ret = -ETIMEDOUT;
	while (-ETIMEDOUT == ret) {
		ret = ISTClient_mcc_IPC_connect(mcc->ipc_handle);
		if ((ret != -ETIMEDOUT) && (ret != 0)) {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_send_results_inner(): ISTClient_mcc_IPC_connect()", ret);
			goto done;
		}
		if (ret == -ETIMEDOUT) {
			ISTClient_mcc_log("[INFO] Timed out waiting to connect... retrying\n");
		}
	}
	ISTClient_mcc_log("[INFO] Connected to MCC daemon\n");

	/* Send CLIENT_READY message to MCU */
	memset(&tx_msg, 0, sizeof(tx_msg));
	tx_msg.header.major_version = IST_CLIENT_MSG_MAJOR_VERSION;
	tx_msg.header.minor_version = IST_CLIENT_MSG_MINOR_VERSION;
	tx_msg.header.command_id = IST_CLIENT_CMD_NOP;

	do {
		ISTClient_mcc_log("[INFO] Sending ClientReady message ...\n");

		ret = ISTClient_mcc_IPC_sendmsg(mcc->ipc_handle, &tx_msg);
		if (ret != 0) {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_send_results_inner(): ISTClient_mcc_IPC_sendmsg(CLIENT_READY)", ret);
			goto done;
		}

		/* Wait for command message from MCU */
		memset(&rx_msg, 0, sizeof(rx_msg));
		ret = ISTClient_mcc_IPC_recvmsg(mcc->ipc_handle, &rx_msg);
		if (ret == 0) {
			break;
		} else if (ret != -ETIMEDOUT) {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_send_results_inner(): ISTClient_mcc_IPC_recvmsg()", ret);
			goto done;
		}

		/* Try sending CLIENT_READY again if ISTClient_mcc_IPC_recvmsg() timed out */
		ISTClient_mcc_log_u32("[WARN] Timed out waiting for request after sending CLIENT_READY : retries remaining",
					client_ready_retries);
		client_ready_retries--;

	} while (client_ready_retries >= 0);

	if (client_ready_retries < 0) {
		ret = -ETIMEDOUT;
		goto done;
	}

	/* Sanity check input message */
	rx_cmd = &rx_msg.header;
	if (rx_cmd->major_version != IST_CLIENT_MSG_MAJOR_VERSION ||
		rx_cmd->minor_version != IST_CLIENT_MSG_MINOR_VERSION)
	{
		ISTClient_mcc_log("[ERROR] ISTClient_mcc_send_results_inner(): rx_msg version != IST_CLIENT_MSG_VERSION\n");
		ret = -EINVAL;
		goto done;
	}

	/* Initialize response message */
	memset(&tx_msg, 0, sizeof(tx_msg));
	tx_msg.header.major_version = IST_CLIENT_MSG_MAJOR_VERSION;
	tx_msg.header.minor_version = IST_CLIENT_MSG_MINOR_VERSION;
	tx_msg.header.command_id = 0x80 | rx_msg.header.command_id;

	memcpy(&(tx_msg.get_result.result), &result, sizeof(tx_msg.get_result.result));
	tx_msg.get_result.status = 0;

	/* Send Response to MCU */
	ret = ISTClient_mcc_IPC_sendmsg(mcc->ipc_handle, &tx_msg);
	if (ret != 0) {
		ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_send_results_inner(): ISTClient_mcc_IPC_sendmsg()", ret);
		goto done;
	}

done:
	if (mcc != NULL) {
		err = ISTClient_mcc_deinit(mcc);
		if (err != 0) {
			ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_send_results_inner(): ISTClient_mcc_deinit()", err);
			ret = err;
		}
		mcc = NULL;
	}

	ISTClient_mcc_trace_exit(__func__, (int64_t)ret);
	return ret;
}

int ISTClient_mcc_send_results(ist_client_result_t result)
{
	int32_t err;

	ISTClient_mcc_trace_entry(__func__);

	while( (err = ISTClient_mcc_send_results_inner(result)) != 0) {
		sleep(1);
	}

#if defined(__QNX__)
	err = ISTClient_mcc_drop_privs();
	if (EOK != err) {
		ISTClient_mcc_log_s32("[ERROR] ISTClient_mcc_send_results(): ISTClient_mcc_drop_privs()", err);
	}
#endif /* defined(__QNX__) */

	ISTClient_mcc_trace_exit(__func__, (int64_t)err);

	return err;
}