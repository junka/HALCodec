/*
 * Copyright (c) 2023, NVIDIA Corporation. All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation. Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#ifndef __IST_CLIENT_MCC_IPC_H__
#define __IST_CLIENT_MCC_IPC_H__

#include <nvsciipc.h>
#include <nvos_s3_tegra_log.h>

#if !defined(__QNX__)
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ISTClient_mcc IPC interface for communication with MCU
 *
 * @returns void * : On success, mcc_ipc_handle : On error, NULL
 */
void *ISTClient_mcc_IPC_init(void);

/**
 * @brief Cleanup ISTClient_mcc IPC interface
 *
 * @param [in] mcc_IPC_handle: handle returned by ISTClient_mcc_IPC_init()
 * @returns 0 on success
 */
int ISTClient_mcc_IPC_deinit(void *mcc_IPC_handle);

/**
 * @brief Wait for connection to be established with MCC
 *
 * @param [in] mcc_IPC_handle: handle returned by ISTClient_mcc_IPC_init()
 * @returns 0 on success
 */
int ISTClient_mcc_IPC_connect(void *mcc_IPC_handle);

/**
 * @brief Send message from IST client to MCU
 *
 * @param [in] mcc_IPC_handle: handle returned by ISTClient_mcc_IPC_init()
 * @param [in] tx_msg: Message data to send to MCU
 * @returns 0 on success
 */
int ISTClient_mcc_IPC_sendmsg(void *mcc_IPC_handle, const ist_client_rsp_t *tx_msg);

/**
 * @brief Receive a message sent from MCU to IST client
 *
 * @param [in] mcc_IPC_handle: handle returned by ISTClient_mcc_IPC_init()
 * @param [out] rx_msg: Message data sent from MCU
 * @returns 0 on success
 */
int ISTClient_mcc_IPC_recvmsg(void *mcc_IPC_handle, ist_client_cmd_t *rx_msg);

/**
 * @brief Log function entry
 *
 * @param [in] func: function name
 * @returns void
 */
static inline void ISTClient_mcc_trace_entry(const char *func)
{
	NvOsDebugPrintStrStr(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, "[==>]", func);
}

/**
 * @brief Log function exit and return value
 *
 * @param [in] func: function name
 * @param [in] retval: function return value
 * @returns void
 */
static inline void ISTClient_mcc_trace_exit(const char *func, int64_t retval)
{
	char line[128] = "";

	(void)snprintf(line, sizeof(line), "[<==] %s ", func);
	NvOsDebugPrintStrSLong(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, line, retval);
}

/**
 * @brief Write message to system log
 *
 * @param [in] msg: log message
 * @returns void
 */
static inline void ISTClient_mcc_log(const char *msg)
{
	NvOsDebugPrintStr(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg);
}

/**
 * @brief Write message and string value to system log
 *
 * @param [in] msg: log message
 * @param [in] str: string to be logged
 * @returns void
 */
static inline void ISTClient_mcc_log_str(const char *msg, const char *str)
{
	NvOsDebugPrintStrStr(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg, str);
}

/**
 * @brief Write message and hexadecimal uint32_t value to system log
 *
 * @param [in] msg: log message
 * @param [in] val: uint32_t value to be logged
 * @returns void
 */
static inline void ISTClient_mcc_log_x32(const char *msg, uint32_t val)
{
	NvOsDebugPrintStrHexUInt(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg, val);
}

/**
 * @brief Write message and uint32_t value to system log
 *
 * @param [in] msg: log message
 * @param [in] val: uint32_t value to be logged
 * @returns void
 */
static inline void ISTClient_mcc_log_u32(const char *msg, uint32_t val)
{
	NvOsDebugPrintStrUInt(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg, val);
}

/**
 * @brief Write message and int32_t value to system log
 *
 * @param [in] msg: log message
 * @param [in] val: int32_t value to be logged
 * @returns void
 */
static inline void ISTClient_mcc_log_s32(const char *msg, int32_t val)
{
	NvOsDebugPrintStrInt(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg, val);
}

/**
 * @brief Write message and hexadecimal uint64_t value to system log
 *
 * @param [in] msg: log message
 * @param [in] val: uint64_t value to be logged
 * @returns void
 */
static inline void ISTClient_mcc_log_x64(const char *msg, int64_t val)
{
	NvOsDebugPrintStrHexULong(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg, val);
}

/**
 * @brief Write message and int64_t value to system log
 *
 * @param [in] msg: log message
 * @param [in] val: int64_t value to be logged
 * @returns void
 */
static inline void ISTClient_mcc_log_s64(const char *msg, int64_t val)
{
	NvOsDebugPrintStrSLong(NVOS_SLOG_CODE_ISTCLIENT, NVOS_LOG_SEVERITY_INFO, msg, val);
}

#ifdef __cplusplus
}
#endif

#endif /* __IST_CLIENT_MCC_IPC_H__ */
