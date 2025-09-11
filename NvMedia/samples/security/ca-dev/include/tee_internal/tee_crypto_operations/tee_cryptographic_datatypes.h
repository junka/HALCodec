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
 * @brief <b>GlobalPlatform: TEE Cryptographic Datatypes</b>
 *
 * @b Description: Describes TEE cryptographic datatypes.
 */

/**
 * @defgroup global_platform_crytographic TEE Cryptographic Datatypes
 *
 * Describes TEE cryptographic datatypes.
 * @ingroup global_platform_iapi
 * @{
 */
#ifndef TEE_INTERNAL_CRYPTOGRAPHIC_DATATYPES_H
#define TEE_INTERNAL_CRYPTOGRAPHIC_DATATYPES_H

/** TEE operation modes */
typedef enum {
	TEE_MODE_ENCRYPT,
	TEE_MODE_DECRYPT,
	TEE_MODE_SIGN,
	TEE_MODE_VERIFY,
	TEE_MODE_MAC,
	TEE_MODE_DIGEST,
	TEE_MODE_DERIVE,
	TOS_MISRA_CPP_PD_3_4_1_TID_582
} TEE_OperationMode;

/**
 * Holds information about a cryptographic operation handle.
 */
typedef struct {
	uint32_t algorithm;
	uint32_t operationClass;
	uint32_t mode;
	uint32_t digestLength;
	uint32_t maxKeySize;
	uint32_t keySize;
	uint32_t requiredKeyUsage;
	uint32_t handleState;
} TEE_OperationInfo;

/** @} */
#endif /* TEE_INTERNAL_CRYPTOGRAPHIC_DATATYPES_H */
