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

 /**
 * @file
 * @brief <b>GlobalPlatform: Tee Internal Panic</b>
 *
 * @b Description: Describes Tee internal panic.
 */

/**
 * @defgroup global_platform_panic TEE Internal Panic
 *
 * Describes TEE internal panic.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_PANIC_H
#define TEE_INTERNAL_PANIC_H

#include <tee_internal/tee_internal_datatypes.h>

/* TODO */
void TEE_Panic(TEE_Result panicCode);
/** @} */
#endif /* TEE_INTERNAL_PANIC_H */
