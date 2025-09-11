/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Client Constants</b>
 *
 * @b Description: Describes TEE client constants.
 */

/**
 * @defgroup global_platform_constants TEE Client Constants
 *
 * Describes client constants.
 * @{
 * @ingroup global_platform_codes
 */

#ifndef TEE_CLIENT_CONSTANTS_H
#define TEE_CLIENT_CONSTANTS_H

#include <tee_common/tee_common_constants.h>
/*
 * This type is used to contain return codes which are
 * the results of invoking TEE Client API functions.
 *
 * Type: TEEC_Result;
 *
 * Implementation Defined
 * as an enum in tee_client/tee_client_constants.h
 */
typedef enum {
	TEEC_SUCCESS                    =       0x00000000,
	TEEC_ERROR_GENERIC              =       0xFFFF0000,
	TEEC_ERROR_ACCESS_DENIED        =       0xFFFF0001,
	TEEC_ERROR_CANCEL               =       0xFFFF0002,
	TEEC_ERROR_ACCESS_CONFLICT      =       0xFFFF0003,
	TEEC_ERROR_EXCESS_DATA          =       0xFFFF0004,
	TEEC_ERROR_BAD_FORMAT           =       0xFFFF0005,
	TEEC_ERROR_BAD_PARAMETERS       =       0xFFFF0006,
	TEEC_ERROR_BAD_STATE            =       0xFFFF0007,
	TEEC_ERROR_ITEM_NOT_FOUND       =       0xFFFF0008,
	TEEC_ERROR_NOT_IMPLEMENTED      =       0xFFFF0009,
	TEEC_ERROR_NOT_SUPPORTED        =       0xFFFF000A,
	TEEC_ERROR_NO_DATA              =       0xFFFF000B,
	TEEC_ERROR_OUT_OF_MEMORY        =       0xFFFF000C,
	TEEC_ERROR_BUSY                 =       0xFFFF000D,
	TEEC_ERROR_COMMUNICATION        =       0xFFFF000E,
	TEEC_ERROR_SECURITY             =       0xFFFF000F,
	TEEC_ERROR_SHORT_BUFFER         =       0xFFFF0010,
	TEEC_PENDING                    =       0xFFFF2000,
	TEEC_ERROR_TIMEOUT              =       0xFFFF3001,
	TEEC_ERROR_OVERFLOW             =       0xFFFF300F,
	TEEC_ERROR_TARGET_DEAD          =       0xFFFF3024,
	TEEC_ERROR_STORAGE_NO_SPACE     =       0xFFFF3041,
	TEEC_ERROR_MAC_INVALID          =       0xFFFF3071,
	TEEC_ERROR_SIGNATURE_INVALID    =       0xFFFF3072,
	TEEC_ERROR_TIME_NOT_SET         =       0xFFFF5000,
	TEEC_ERROR_TIME_NEEDS_RESET     =       0xFFFF5001,
	TEEC_ERROR_AGAIN                =       0xFFFF6001,
	TEEC_ERROR_NO_MESSAGE           =       0xFFFF6002,
	TEEC_ERROR_NO_RESOURCE          =       0xFFFF6003,
	TEEC_ERROR_BLOCKED              =       0x00000001,
	TEEC_ERROR_NO_ANSWER            =       0x00000002,
	/* IMPLEMENTATION-DEFINED 0x00000001 - 0xFFFEFFFF */
} TEEC_Result;

/** Return Code Origins */
static const uint32_t TEEC_ORIGIN_API          =  0x00000001;
static const uint32_t TEEC_ORIGIN_COMMS        =  0x00000002;
static const uint32_t TEEC_ORIGIN_TEE          =  0x00000003;
static const uint32_t TEEC_ORIGIN_TRUSTED_APP  =  0x00000004;

/** Shared Memory Control */
static const uint32_t TEEC_MEM_INPUT    =  0x00000001;
static const uint32_t TEEC_MEM_OUTPUT  =  0x00000002;

/** API Parameter Types
 * Because of OTE implementation limitation, we treat OUTPUT the same as INOUT.
 *
 * TEEC_MEMREF_WHOLE and TEEC_MEMREF_PARTIAL types are not supported.
 */
static const uint32_t TEEC_NONE                  = TEE_PARAM_TYPE_NONE;
static const uint32_t TEEC_VALUE_INPUT           = TEE_PARAM_TYPE_VALUE_INPUT;
static const uint32_t TEEC_VALUE_OUTPUT          = TEE_PARAM_TYPE_VALUE_OUTPUT;
static const uint32_t TEEC_VALUE_INOUT           = TEE_PARAM_TYPE_VALUE_INOUT;
static const uint32_t TEEC_MEMREF_TEMP_INPUT     = TEE_PARAM_TYPE_MEMREF_INPUT;
static const uint32_t TEEC_MEMREF_TEMP_OUTPUT    = TEE_PARAM_TYPE_MEMREF_OUTPUT;
static const uint32_t TEEC_MEMREF_TEMP_INOUT     = TEE_PARAM_TYPE_MEMREF_INOUT;

/** Session Login Methods */
static const uint32_t TEEC_LOGIN_PUBLIC             = 0x00000000;
static const uint32_t TEEC_LOGIN_USER               = 0x00000001;
static const uint32_t TEEC_LOGIN_GROUP              = 0x00000002;
static const uint32_t TEEC_LOGIN_APPLICATION        = 0x00000004;
static const uint32_t TEEC_LOGIN_USER_APPLICATION   = 0x00000005;
static const uint32_t TEEC_LOGIN_GROUP_APPLICATION  = 0x00000006;
/* IMPLEMENTATION-DEFINED 0x80000000 - 0xFFFFFFFF */
/** @} */
#endif /* TEE_CLIENT_CONSTANTS_H */
