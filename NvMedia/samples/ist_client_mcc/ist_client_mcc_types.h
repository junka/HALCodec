/*
 * Copyright (c) 2022, NVIDIA Corporation. All Rights Reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software and related documentation. Any
 * use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation
 * is strictly prohibited.
 */
#ifndef __IST_CLIENT_MCC_TYPES_H__
#define __IST_CLIENT_MCC_TYPES_H__

#include <ist_client_mcc.h>

#include <stdint.h>

/* Pack all ist_client message structures ... */
#pragma pack(push, 1)

#define IST_CLIENT_MSG_MAJOR_VERSION (3U)
#define IST_CLIENT_MSG_MINOR_VERSION (0U)

#define IST_CLIENT_CMD_NOP        (0U)
#define IST_CLIENT_CMD_GET_RESULT (3U)

/**
 * @brief  Header common to all messages sent between IST manager and client
 *
 * @member major_version
 * @member minor_version
 * @member command_id
 */
typedef struct ist_client_msg {
	uint8_t major_version;
	uint8_t minor_version;
	uint8_t command_id;
} ist_client_msg_t;

/**
 * @brief Message packet for IST_CLIENT_CMD_GET_RESULT
 *
 * @member header
 */
typedef struct ist_client_get_result_msg {
	ist_client_msg_t header;
} ist_client_get_result_msg_t;

/**
 * @brief Response packet for IST_CLIENT_CMD_GET_RESULT
 *
 * @member header
 * @member status
 * @member result
 */
typedef struct ist_client_get_result_rsp {
	ist_client_msg_t header;

	uint8_t status;
	ist_client_result_t result;
} ist_client_get_result_rsp_t;

/**
 * @brief Generic ist_client cmd message
 *
 * @member header
 * @member get_result
 * @member bytes
 */
typedef union {
	ist_client_msg_t header;

	ist_client_get_result_msg_t get_result;

	uint8_t bytes[64];
} ist_client_cmd_t;

/**
 * @brief Generic ist_client cmd response
 *
 * @member header
 * @member get_result
 * @member bytes
 */
typedef union {
	ist_client_msg_t header;

	ist_client_get_result_rsp_t get_result;

	uint8_t bytes[64];
} ist_client_rsp_t;

/* Restore prior structure packing config */
#pragma pack(pop)

#endif /* __IST_CLIENT_MCC_TYPES_H__ */
