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
 * @brief <b>GlobalPlatform: TEE Client API</b>
 *
 * @b Description: Defines a communications API for connecting client
 * applications running in a rich operating environment with
 * security-related trusted applications running inside a trusted
 * execution environment (TEE).
 */

/**
 * @defgroup global_platform_codes TEE Client API
 *
 * Defines a communications API for connecting client applications
 * running in a rich operating environment with security-related
 * trusted applications running inside a trusted execution environment
 * (TEE).
 *
 * @ingroup grp_tee_lib
 * @{
 */
#ifndef TEE_CLIENT_API_H
#define TEE_CLIENT_API_H

/*
 * Standard library headers must be included before subordinate
 * TEE headers so that the subordinate headers do not need
 * to include the STD headers again.
 *
 * For legacy issues, Trusty still uses the C version of headers
 * even for C++.
 */
#if !defined(__cplusplus) || defined(CONFIG_TRUSTY)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#else
#include <cstdbool>
#include <cstddef>
#include <cstdint>
#endif

#ifndef BUILD_CONFIG_SAFETY
#include <tee_client/tee_client_constants.h>
#include <tee_client/tee_client_datatypes.h>
#include <tee_client/tee_client_functions.h>

#include <tee_common/tee_common_helpers.h>
#else
#include <tee_client_datatypes.h>
#include <tee_client_constants.h>
#include <tee_client_functions.h>
#endif
/** @} */
#endif /* TEE_CLIENT_API_H */
