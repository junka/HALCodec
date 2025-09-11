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
 * @brief <b>GlobalPlatform: TEE Internal Cancellation</b>
 *
 * @b Description: Describes TEE internal cancellation.
 */

/**
 * @defgroup global_platform_cancellation TEE Internal Cancellation
 *
 * Describes TEE internal cancellation.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_CANCELLATION_H
#define TEE_INTERNAL_CANCELLATION_H

/* TODO */
bool TEE_GetCancellationFlag( void );

/* TODO */
bool TEE_UnmaskCancellation( void );

/* TODO */
bool TEE_MaskCancellation( void );
/** @} */
#endif /* TEE_INTERNAL_CANCELLATION_H */
