/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef TEE_INTERNAL_TIME_H
#define TEE_INTERNAL_TIME_H

/*
 * TEE_GetSystemTime
 *
 * This call returns the current system
 * time since boot tracked by the kernel
 * ONLY supports millisecond precision
 *
 * Params:
 * TEE_Time *time: Pointer to TEE_Time struct
 *
 * Return:
 * void
 */
void TEE_GetSystemTime(TEE_Time* time);

/*
 * TODO
 * Currently Unsupported
 */
TEE_Result TEE_Wait(uint32_t timeout);

/*
 * TODO
 * Currently Unsupported
 */
TEE_Result TEE_GetTAPersistentTime(void);

/*
 * TODO
 * Currently Unsupported
 */
TEE_Result TEE_SetTAPersistentTime(void);

/*
 * TODO
 * Currently Unsupported
 */
void TEE_GetREETime(TEE_Time* time);

#endif /* TEE_INTERNAL_TIME_H */
