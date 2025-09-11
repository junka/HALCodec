/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Internal Datatypes</b>
 *
 * @b Description: Describes TEE internal datatypes.
 */

/**
 * @defgroup global_platform_internal TEE Internal Datatypes
 *
 * Describes TEE internal datatypes.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_DATATYPES_H
#define TEE_INTERNAL_DATATYPES_H

#include <tee_internal/tee_internal_constants.h>
#include <tee_internal/tee_internal_os_abstraction_layer.h>
#include <tee_common/tee_common_os_abstraction_layer.h>
#include <tee_common/tee_common_datatypes.h>

///
/// \brief Describes one parameter passed by the trusted core framework to the
/// entry points TA_OpenSessionEntryPoint or TA_InvokeCommandEntryPoint
/// or by the TA to the functions TEE_OpenTASession or
/// TEE_InvokeTACommand. Which of the field value or memref to select
/// is determined by the parameter type specified in the argument
/// paramTypes passed to the entry point.
///
typedef struct
{
    /* \brief pointer and size representing a fixed-size memory buffer */
    TEE_MemoryReference memref;
    /* \brief a pair of integer values */
    TEE_Value value;
} TEE_Param;

///
/// \brief The TEE_Time structure holds a time value as defined in the GP spec.
/// Currently supported by Trusty, but not TZVault.
///
typedef struct
{
    uint32_t seconds;
    uint32_t millis;
} TEE_Time;

///
/// An opaque handle on a property set or enumerator. These handles
/// either are returned by the function TEE_AllocatePropertyEnumerator
/// or are one of the pseudo-handles defined in section 4.2.4 of the
/// specification.
/// Currently supported by Trusty but not TZVault.
///
typedef struct __TEE_PropSetHandle* TEE_PropSetHandle;

#ifdef __cplusplus
/// \brief TEE_Param in GP spec has default four parameters. In some cases, four parameters
/// are not enough in DriveOS 6.0. Thus, TEE_param extends to eight parameters instead.
/// These aliases are used to access the TEE_Param array.
using TEE_PARAMS_DEFAULT = TEE_Param (&)[4];
using TEE_PARAMS_EXT = TEE_Param (&)[8];
#endif
/** @} */
#endif /* TEE_INTERNAL_DATATYPES_H */

