/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2019-2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: TEE Common Datatypes</b>
 *
 * @b Description: Describes TEE Common datatypes.
 */

#ifndef TEE_COMMON_DATATYPES_H
#define TEE_COMMON_DATATYPES_H

#include <tee_common/tee_common_os_abstraction_layer.h>

/**
 * @defgroup global_platform_internal TEE Internal Datatypes
 *
 * Describes TEE internal datatypes.
 * @ingroup global_platform_iapi
 * @{
 */

/*
 * This structure is defined in tee_common folder to allow
 * tee_client header to reuse the definition as TEEC_TempMemoryReference.
 */
///
/// \brief Describes a fixed-length buffer to be used as a GP parameter
/// when the GP parameter's type is TEE_PARAM_TYPE_MEMREF_INPUT,
/// TEE_PARAM_TYPE_MEMREF_OUTPUT or TEE_PARAM_TYPE_MEMREF_INOUT.
///
typedef struct
{
	/// \brief Address of the buffer
    void* buffer;
    /// \brief Size of the buffer
    size_t size;
} TEE_MemoryReference;

/*
 * This structure is defined in tee_common folder to allow
 * tee_client header to reuse the definition as TEEC_Value.
 */
///
/// \brief Describes a pair of unsigned integers to be used as a GP parameter
/// when the GP parameter's type is TEE_PARAM_TYPE_VALUE_INPUT,
/// TEE_PARAM_TYPE_VALUE_OUTPUT or TEE_PARAM_TYPE_VALUE_INOUT.
///
typedef struct
{
    /// \brief First unsigned integer of the pair
    uint32_t a;
    /// \brief Second unsigned integer of the pair
    uint32_t b;
} TEE_Value;

#ifdef __cplusplus
/// \brief Constants represent Default and Extended sizes of TEE_Param arrays.
static const uint8_t TEE_PARAMS_SIZE_DEFAULT = 4U;
static const uint8_t TEE_PARAMS_SIZE_EXT = 8U;
#else
#define TEE_PARAMS_SIZE_DEFAULT 4U
#define TEE_PARAMS_SIZE_EXT 8U
#endif
/** @} */

#endif /* TEE_CLIENT_DATATYPES_H */
