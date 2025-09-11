/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2019-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
#ifndef TEE_INTERNAL_HELPER_INCL_H
#define TEE_INTERNAL_HELPER_INCL_H

 /**
 * @file tee_internal_helpers_incl.h
 * @b These functions formats the parameter type for TEE functions.
 */

/* Check if the received param types match the expected */

static inline bool CHECK_PARAM_TYPES(const uint32_t paramTypes,
        const uint32_t t1, const uint32_t t2, const uint32_t t3,
        const uint32_t t4) {
    return (paramTypes == TEE_PARAM_TYPES(t1, t2, t3, t4));
}

static inline bool CHECK_PARAM_TYPES_8(const uint32_t paramTypes,
        const uint32_t t1, const uint32_t t2, const uint32_t t3,
        const uint32_t t4, const uint32_t t5, const uint32_t t6,
        const uint32_t t7, const uint32_t t8) {
    return (paramTypes == TEE_PARAM_TYPES_8(t1, t2, t3, t4, t5, t6, t7, t8));
}

static inline bool NULL_CHECK_MEMREF_BUFFER_SIZE(const TEE_Param params) {
    return ((params.memref.buffer == NULL) || (params.memref.size == 0U));
}

/* If the param object is a memory buffer, check if the buffer is null */
static inline bool CHECK_MEMREF_IS_NULL(const uint32_t paramTypes,
#ifdef __cplusplus
        const TEE_Param (&params)[4]
#else
        /* Important: 'params' type is *DIFFERENT* from the C++ version */
        const TEE_Param params[4]
#endif
        ) {
    return !((TEE_PARAM_IS_MEM(TEE_PARAM_TYPE_GET(paramTypes, 0U)) &&
        NULL_CHECK_MEMREF_BUFFER_SIZE(params[0])) ||
        (TEE_PARAM_IS_MEM(TEE_PARAM_TYPE_GET(paramTypes, 1U)) &&
        NULL_CHECK_MEMREF_BUFFER_SIZE(params[1])) ||
        (TEE_PARAM_IS_MEM(TEE_PARAM_TYPE_GET(paramTypes, 2U)) &&
        NULL_CHECK_MEMREF_BUFFER_SIZE(params[2])) ||
        (TEE_PARAM_IS_MEM(TEE_PARAM_TYPE_GET(paramTypes, 3U)) &&
        NULL_CHECK_MEMREF_BUFFER_SIZE(params[3])));
}

static inline bool CHECK_MEMREF_IS_NULL_8(const uint32_t paramTypes,
#ifdef __cplusplus
        const TEE_Param (&params)[8]
#else
        /* Important: 'params' type is *DIFFERENT* from the C++ version */
        const TEE_Param params[8]
#endif
        ) {
    uint32_t i;
    for (i = 0U; i < 8U; ++i) {
        if (TEE_PARAM_IS_MEM(TEE_PARAM_TYPE_GET(paramTypes, i)) &&
            NULL_CHECK_MEMREF_BUFFER_SIZE(params[i])) {
            break;
        }
    }
    return (i == 8U);
}

#endif  //TEE_INTERNAL_HELPER_INCL_H
