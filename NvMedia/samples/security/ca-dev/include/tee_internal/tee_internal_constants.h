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
 * @brief <b>GlobalPlatform: Tee Internal Constants</b>
 *
 * @b Description: Describes Tee internal constants.
 */

/**
 * @defgroup global_platform_iconstants TEE Internal Constants
 *
 * Describes TEE internal constants.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_CONSTANTS_H
#define TEE_INTERNAL_CONSTANTS_H

#include <tee_common/tee_common_constants.h>

/// \brief Error codes as defined in the GP spec
typedef enum {

    TEE_SUCCESS                      =  0x00000000U,
    TEE_ERROR_STORAGE_NOT_AVAILABLE  =  0xF0100003U,
    TEE_ERROR_GENERIC                =  0xFFFF0000U,
    TEE_ERROR_ACCESS_DENIED          =  0xFFFF0001U,
    TEE_ERROR_CANCEL                 =  0xFFFF0002U,
    TEE_ERROR_ACCESS_CONFLICT        =  0xFFFF0003U,
    TEE_ERROR_EXCESS_DATA            =  0xFFFF0004U,
    TEE_ERROR_BAD_FORMAT             =  0xFFFF0005U,
    TEE_ERROR_BAD_PARAMETERS         =  0xFFFF0006U,
    TEE_ERROR_BAD_STATE              =  0xFFFF0007U,
    TEE_ERROR_ITEM_NOT_FOUND         =  0xFFFF0008U,
    TEE_ERROR_NOT_IMPLEMENTED        =  0xFFFF0009U,
    TEE_ERROR_NOT_SUPPORTED          =  0xFFFF000AU,
    TEE_ERROR_NO_DATA                =  0xFFFF000BU,
    TEE_ERROR_OUT_OF_MEMORY          =  0xFFFF000CU,
    TEE_ERROR_BUSY                   =  0xFFFF000DU,
    TEE_ERROR_COMMUNICATION          =  0xFFFF000EU,
    TEE_ERROR_SECURITY               =  0xFFFF000FU,
    TEE_ERROR_SHORT_BUFFER           =  0xFFFF0010U,
    TEE_PENDING                      =  0xFFFF2000U,
    TEE_ERROR_TIMEOUT                =  0xFFFF3001U,
    TEE_ERROR_OVERFLOW               =  0xFFFF300FU,
    TEE_ERROR_TARGET_DEAD            =  0xFFFF3024U,
    TEE_ERROR_STORAGE_NO_SPACE       =  0xFFFF3041U,
    TEE_ERROR_MAC_INVALID            =  0xFFFF3071U,
    TEE_ERROR_SIGNATURE_INVALID      =  0xFFFF3072U,
    TEE_ERROR_TIME_NOT_SET           =  0xFFFF5000U,
    TEE_ERROR_TIME_NEEDS_RESET       =  0xFFFF5001U,

} TEE_Result;

/// \brief Login type constants as defined in the GP spec
typedef enum {

    TEE_LOGIN_PUBLIC             =  0x00000000U,
    TEE_LOGIN_USER               =  0x00000001U,
    TEE_LOGIN_GROUP              =  0x00000002U,
    TEE_LOGIN_APPLICATION        =  0x00000004U,
    TEE_LOGIN_APPLICATION_USER   =  0x00000005U,
    TEE_LOGIN_APPLICATION_GROUP  =  0x00000006U,
    TEE_LOGIN_TRUSTED_APP        =  0xF0000000U,

} TEE_LoginType;

/// \brief Origin code constants as defined in the GP spec
typedef enum {

    TEE_ORIGIN_API               =  0x00000001U,
    TEE_ORIGIN_COMMS             =  0x00000002U,
    TEE_ORIGIN_TEE               =  0x00000003U,
    TEE_ORIGIN_TRUSTED_APP       =  0x00000004U,

} TEE_OriginCode;

/// \brief TEE_Malloc memory type, NVIDIA proprietary
typedef enum {

    TEE_ALLOC_HEAP_MEM           =  0xAAAA0000U,
    TEE_ALLOC_TZSYSRAM_MEM       =  0x5555AAAAU,

} TEE_MallocType;

/// \brief Time related constants as defined in the GP spec
typedef enum {

    TEE_TIMEOUT_INFINITE         =  0xFFFFFFFFU,

} TEE_TimeConstant;

/// \brief Special value denoting the absence of a handle, value is 0.
#define TEE_HANDLE_NULL                NULL

/** @} */

/* -- Property Pseudo-Handles, currently only supported by Trusty -- */
#define TEE_PROPSET_CURRENT_TA         (TEE_PropSetHandle)0xFFFFFFFFU
#define TEE_PROPSET_CURRENT_CLIENT     (TEE_PropSetHandle)0xFFFFFFFEU
#define TEE_PROPSET_TEE_IMPLEMENTATION (TEE_PropSetHandle)0xFFFFFFFDU

#endif /* TEE_INTERNAL_CONSTANTS_H */
