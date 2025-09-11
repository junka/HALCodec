/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2021, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Internal API</b>
 *
 * Describes TEE internal API. This section defines a
 * set of C APIs for the development of trusted applications (TAs)
 * running inside a trusted execution environment (TEE). The APIs
 * defined in this section provide TA developers the following
 * functionality:
 *  - Basic OS-like functions such as memory management and
 *    session management
 *  - Communication operations, such as handling incoming requests from
 *    the rich execution environment (REE)
 *  - Trusted storage operations
 *  - Cryptographic key operations
 *  - Other miscellaneous operations that the TAs might require, such
 *    as system time.
 *
 * The NVIDIA TEE solution only provides a comprehensive sub-set of
 * the GlobalPlatform API standard and not the full specification.
 * Describes TEE internal API.
 *
 * @note All TEE internal APIs provided by the NVIDIA TEE solution are
 * fully synchronous and are intended to be utilized in single-threaded
 * TA environments.
 */

/**
 * @defgroup global_platform_iapi TEE Internal API
 *
 * Describes TEE internal API. This section defines a
 * set of C APIs for the development of trusted applications (TAs)
 * running inside a trusted execution environment (TEE). The APIs
 * defined in this section provide TA developers the following
 * functionality:
 *  - Basic OS-like functions such as memory management and
 *    session management
 *  - Communication operations, such as handling incoming requests from
 *    the rich execution environment (REE)
 *  - Trusted storage operations
 *  - Cryptographic key operations
 *  - Other miscellaneous operations that the TAs might require, such
 *    as system time.
 *
 * The NVIDIA TEE solution only provides a comprehensive sub-set of
 * the GlobalPlatform API standard and not the full specification.
 * Describes TEE internal API.
 *
 * @note All TEE internal APIs provided by the NVIDIA TEE solution are
 * fully synchronous and are intended to be utilized in single-threaded
 * TA environments.
 *
 * @ingroup grp_tee_lib
 * @{
 */

#ifndef TEE_INTERNAL_API_H
#define TEE_INTERNAL_API_H

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

#include <tee_common/tee_common_helpers.h>

#include <tee_internal/tee_internal_cancellation.h>
#include <tee_internal/tee_internal_client_api.h>
#include <tee_internal/tee_internal_constants.h>
#include <tee_internal/tee_internal_datatypes.h>
#include <tee_internal/tee_internal_helpers.h>
#include <tee_internal/tee_internal_memory_management.h>
#include <tee_internal/tee_internal_ta_interface.h>
#include <tee_internal/tee_internal_time.h>
#include <tee_internal/tee_internal_panic.h>
#include <tee_internal/tee_internal_property_access.h>

#include <tee_internal/tee_crypto_operations/tee_cryptographic_constants.h>
#include <tee_internal/tee_crypto_operations/tee_cryptographic_datatypes.h>
#include <tee_internal/tee_crypto_operations/tee_cryptographic_operations.h>

#include <tee_internal/tee_trusted_storage/tee_trusted_storage.h>
#include <tee_internal/tee_trusted_storage/tee_trusted_storage_constants.h>
#include <tee_internal/tee_trusted_storage/tee_trusted_storage_datatypes.h>

/** @} */
#endif /* TEE_INTERNAL_API_H */
