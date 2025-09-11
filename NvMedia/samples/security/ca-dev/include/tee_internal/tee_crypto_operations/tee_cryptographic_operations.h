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
 * @brief <b>GlobalPlatform: TEE Cryptographic Operations</b>
 *
 * @b Description: Describes TEE cryptographic operations.
 */

/**
 * @defgroup global_platform_cry_op TEE Cryptographic Operations
 *
 * Describes TEE cryptographic operations.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_CRYPTOGRAPHIC_OPERATIONS_H
#define TEE_CRYPTOGRAPHIC_OPERATIONS_H

#include <tee_internal/tee_internal_constants.h>
#include <tee_internal/tee_crypto_operations/tee_cryptographic_constants.h>
#include <tee_internal/tee_crypto_operations/tee_cryptographic_datatypes.h>
#include <tee_internal/tee_internal_os_abstraction_layer.h>

/* -- -- -- -- Symmetric Cipher Functions -- -- -- -- */

/**
 * Generates random data into the supplied buffer.
 *
 * (QNX PDK Only) Error behavior:
 *       if there is any error, halts the system.
 *
 * (QNX PDK Only) Side effects:
 *       session will be opened towards TOS Crypto Service TA.  If the function
 *       returns normally, the opened session will be closed.  Otherwise, the
 *       opened session will not be closed and become an orphan.
 *
 * \param[out] randomBuffer The buffer which the API will populate with
 *             random data.
 *             (QNX PDK Only) Valid Range:
 *                 The address must be an address allocated with TEE_Malloc()
 * \param[in] randomBufferLen The number of RNG bytes to generate. Unit: Byte.
 */
void TEE_GenerateRandom(void* randomBuffer, size_t randomBufferLen);

/** @} */
#endif /* TEE_CRYPTOGRAPHIC_OPERATIONS_H */
