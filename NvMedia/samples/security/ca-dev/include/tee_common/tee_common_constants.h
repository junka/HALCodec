/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: Tee Common Constants</b>
 *
 * @b Description: Describes Tee Common constants.
 */

/**
 * @defgroup global_platform_common_constants TEE Common Constants
 *
 * Describes TEE Common constants.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_COMMON_CONSTANTS_H
#define TEE_COMMON_CONSTANTS_H

/* -- Parameter Types --
 * Because of OTE implementation limitation, we treat OUTPUT the same as INOUT.
 *
 * These constants are defined in tee_common folder to allow
 * tee_client header to reuse the definition as TEEC_PARAM_*.
 */
#define TEE_PARAM_TYPE_NONE            0U
#define TEE_PARAM_TYPE_VALUE_INPUT     1U
#define TEE_PARAM_TYPE_VALUE_OUTPUT    3U
#define TEE_PARAM_TYPE_VALUE_INOUT     3U
#define TEE_PARAM_TYPE_MEMREF_INPUT    5U
#define TEE_PARAM_TYPE_MEMREF_OUTPUT   7U
#define TEE_PARAM_TYPE_MEMREF_INOUT    7U


/** @} */

#endif /* TEE_COMMON_CONSTANTS_H */
