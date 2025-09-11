/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef NV_TE_INTERNAL_API_H
#define NV_TE_INTERNAL_API_H

/*
 * Standard library headers must be included before subordinate
 * NV_TE headers so that the subordinate headers do not need
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

/*
 * This is a shared interface between OTE and TZVault based implementation.
 * Any OTE or TZVault specific items should be defined in
 * nv_te_internal/nv_te_internal_os_abstraction_layer.h under OTE or TZVault
 * path.
 */
#include <nv_te_internal/nv_te_internal_os_abstraction_layer.h>

#include <nv_te_uuid.h>
#include <nv_te_internal/nv_te_cryptographic_constants.h>
#include <nv_te_internal/nv_te_cryptographic_asym_constants.h>
#include <nv_te_internal/nv_te_cryptographic_sym_constants.h>
#include <nv_te_internal/nv_te_memory_management.h>
#include <tee_client/tee_client_pkcs11ks_constants.h>

#endif /* NV_TE_INTERNAL_API_H */
