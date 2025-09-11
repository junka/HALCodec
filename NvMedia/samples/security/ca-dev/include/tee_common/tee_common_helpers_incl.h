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
#ifndef TEE_COMMON_HELPERS_INCL_H
#define TEE_COMMON_HELPERS_INCL_H

 /**
 * @file tee_common_helpers_incl.h
 * @b These functions formats the parameter type for TEE functions.
 */

/**
 * @brief Helper macro to construct param type
 */
static inline uint32_t TEE_PARAM_TYPES_8(const uint32_t t0,
    const uint32_t t1, const uint32_t t2, const uint32_t t3, const uint32_t t4,
    const uint32_t t5, const uint32_t t6, const uint32_t t7) {
    return  (t0 & 0xFU) |
            ((t1 & 0xFU) << 4U) |
            ((t2 & 0xFU) << 8U) |
            ((t3 & 0xFU) << 12U) |
            ((t4 & 0xFU) << 16U) |
            ((t5 & 0xFU) << 20U) |
            ((t6 & 0xFU) << 24U) |
            ((t7 & 0xFU) << 28U);
}

/* Format the param from four desired types */
static inline uint32_t TEE_PARAM_TYPES(const uint32_t t0,
    const uint32_t t1, const uint32_t t2, const uint32_t t3) {
    return TEE_PARAM_TYPES_8(t0, t1, t2, t3, TEE_PARAM_TYPE_NONE,
        TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE, TEE_PARAM_TYPE_NONE);
}

/* Get the ith type from the param */
static inline uint32_t TEE_PARAM_TYPE_GET(const uint32_t t, const uint32_t i) {
    return (((t) >> (i << 2U)) & 0xFU);
}

/* Check if the param object is a memory buffer i.e. type number equals 5, 6 or 7 */
static inline bool TEE_PARAM_IS_MEM(const uint32_t t) {
    return ((t & 4U) != 0U);
}

/* Check if the param is an input or in/out parameter */
static inline bool TEE_PARAM_IS_OUTPUT(const uint32_t t) {
    return ((t & 2U) != 0U);
}

/* Check if the param is an input or in/out parameter */
static inline bool TEE_PARAM_IS_INPUT(const uint32_t t) {
    return ((t & 1U) != 0U);
}

/* Check if the param is valid */
static inline bool TEE_PARAM_IS_VALID(const uint32_t t) {
    static const uint32_t validParamTypes[] = {
        TEE_PARAM_TYPE_NONE,
        TEE_PARAM_TYPE_VALUE_INPUT,
        TEE_PARAM_TYPE_VALUE_OUTPUT,
        TEE_PARAM_TYPE_VALUE_INOUT,
        TEE_PARAM_TYPE_MEMREF_INPUT,
        TEE_PARAM_TYPE_MEMREF_OUTPUT,
        TEE_PARAM_TYPE_MEMREF_INOUT
    };
    uint32_t i;
    bool retval = false;
    (void) retval;
    for (i = 0U; i < sizeof(validParamTypes) / sizeof(uint32_t); ++i) {
        if (t == validParamTypes[i]) {
            retval = true;
            break;
        }
    }
    return retval;
}

/* Check if the formatted param types are valid */
static inline bool TEE_PARAM_TYPES_IS_VALID(const uint32_t t) {
    const uint32_t t0 = TEE_PARAM_TYPE_GET(t, 0U);
    const uint32_t t1 = TEE_PARAM_TYPE_GET(t, 1U);
    const uint32_t t2 = TEE_PARAM_TYPE_GET(t, 2U);
    const uint32_t t3 = TEE_PARAM_TYPE_GET(t, 3U);
    const uint32_t t4 = TEE_PARAM_TYPE_GET(t, 4U);
    const uint32_t t5 = TEE_PARAM_TYPE_GET(t, 5U);
    const uint32_t t6 = TEE_PARAM_TYPE_GET(t, 6U);
    const uint32_t t7 = TEE_PARAM_TYPE_GET(t, 7U);
    (void) t0;
    (void) t1;
    (void) t2;
    (void) t3;
    (void) t4;
    (void) t5;
    (void) t6;
    (void) t7;

    return TEE_PARAM_IS_VALID(t0) && // verify t0 - t3
        TEE_PARAM_IS_VALID(t1) &&
        TEE_PARAM_IS_VALID(t2) &&
        TEE_PARAM_IS_VALID(t3) &&
        TEE_PARAM_IS_VALID(t4) &&
        TEE_PARAM_IS_VALID(t5) &&
        TEE_PARAM_IS_VALID(t6) &&
        TEE_PARAM_IS_VALID(t7);
}

#endif // TEE_COMMON_HELPERS_INCL_H