/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2022, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef TEE_CLIENT_DATATYPES_H
#define TEE_CLIENT_DATATYPES_H

#include <tee_client/tee_client_os_abstraction_layer.h>
#include <tee_common/tee_common_os_abstraction_layer.h>
#include <tee_common/tee_common_datatypes.h>

/* This type denotes a Shared Memory block which has either
 * been registered with the Implementation or allocated by it.
 * The fields of this structure have the following meaning:
 * - buffer is a pointer to the memory buffer shared with the TEE
 * - size is the size of the memory buffer, in bytes
 * - flags is a bit-vector which can contain the following flags:
 *   > TEEC_MEM_INPUT: the memory can be used to transfer data
 *     from the Client Application to the TEE
 *   > TEEC_MEM_OUTPUT: the memory can be used to transfer data
 *     from the TEE to the Client Application
 *   > All other bits in this field SHOULD be set to zero, and are
 *     reserved for future use
 * - imp contains any additional implementation-defined data
 *   attached to the Shared Memory structure
 */
typedef struct
{
    void*                 buffer;
    size_t                size;
    uint32_t              flags;
    /* Implementation-Specific Fields */
    uint32_t              allocated;
} TEEC_SharedMemory;


/* This type defines a Temporary Memory Reference. It is used as a
 * TEEC_Operation parameter when the corresponding parameter type
 * is one of TEEC_MEMREF_TEMP_INPUT, TEEC_MEMREF_TEMP_OUTPUT, or
 * TEEC_MEMREF_TEMP_INOUT.
 *
 * The fields of this structure have the following meaning:
 * - buffer is a pointer to the first byte of a region of memory
 * which needs to be temporarily registered for the duration of
 * the Operation. This field can be NULL to specify a null Memory
 * Reference.
 * size is the size of the referenced memory region, in bytes.
 * When the operation completes, and unless the parameter type is
 * TEEC_MEMREF_TEMP_INPUT, the Implementation must update this
 * field to reflect the actual or required size of the output:
 * o If the Trusted Application has actually written some data in
 *   the output buffer, then the Implementation MUST update the
 *   size field with the actual number of bytes written.
 * o If the output buffer was not large enough to contain the
 *   whole output, or if it is null, the Implementation MUST
 *   update the size field with the size of the output buffer
 *   requested by the Trusted Application. In this case, no data
 *   has been written into the output buffer.
 */
typedef TEE_MemoryReference TEEC_TempMemoryReference;

/* This type defines a parameter that is not referencing shared memory,
 * but carries instead small raw data passed by value. It is used as a
 * TEEC_Operation parameter when the corresponding parameter type is
 * one of TEEC_VALUE_INPUT, TEEC_VALUE_OUTPUT, or TEEC_VALUE_INOUT.
 *
 * The two fields of this structure do not have a particular meaning.
 * It is up to the protocol between the Client Application and the
 * Trusted Application to assign a semantic to those two integers.
 */
typedef TEE_Value TEEC_Value;

/* -- TEEC_Parameter --
 * This type defines a Parameter of a TEEC_Operation. It can be a
 * Temporary Memory Reference, a Registered Memory Reference, or a
 * Value Parameter.
 * The field to select in this struct depends on the type of the
 * parameter specified in the paramTypes field of the TEEC_Operation
 * structure:
 *
 * Parameter Type             | Field to use
 * ==================         | ============
 * TEEC_VALUE_INPUT           | value
 * TEEC_VALUE_OUTPUT          |
 * TEEC_VALUE_INOUT           |
 * ---------------------------------------
 * TEEC_MEMREF_TEMP_INPUT     | tmpref
 * TEEC_MEMREF_TEMP_OUTPUT    |
 * TEEC_MEMREF_TEMP_INOUT     |
 *
 * The TEEC_MEMREF_WHOLE and TEEC_MEMREF_PARTIAL types are not supported.
 */
typedef struct
{
    union {
        TEEC_TempMemoryReference        tmpref;
        /* -- Alias of tmpref. --
         * This alias allows implementation to treat TEEC_Parameter as TEE_Param.
         */
        TEEC_TempMemoryReference        memref;
    };
    TEEC_Value                      value;
} TEEC_Parameter;

typedef TEEC_Parameter TEE_Param;

/* -- TEEC_Operation --
 * This type defines the payload of either an open Session operation
 * or an invoke Command operation. It is also used for cancellation
 * of operations, which may be desirable even if no payload is passed.
 *
 * The fields of this structure have the following meaning:
 * - 'started' is a field which MUST be initialized to zero by the
 *    Client Application before each use in an operation if the Client
 *    Application may need to cancel the operation about to be
 *    performed.
 * - 'paramTypes' encodes the type of each of the Parameters in the
 *    operation. The layout of these types within a 32-bit integer is
 *    implementation-defined and the Client Application MUST use the
 *    macro TEEC_PARAMS_TYPE to construct a constant value for this
 *    field. As a special case, if the Client Application sets
 *    paramTypes to 0, then the Implementation MUST interpret it as
 *    meaning that the type for each Parameter is set to TEEC_NONE.
 *    The type of each Parameter can take one of the following values:
 *      o TEEC_NONE
 *      o TEEC_VALUE_INPUT
 *      o TEEC_VALUE_OUTPUT
 *      o TEEC_VALUE_INOUT
 *      o TEEC_MEMREF_TEMP_INPUT
 *      o TEEC_MEMREF_TEMP_OUTPUT
 *      o TEEC_MEMREF_TEMP_INOUT
 *      o TEEC_MEMREF_WHOLE
 *      o TEEC_MEMREF_PARTIAL_INPUT
 *      o TEEC_MEMREF_PARTIAL_OUTPUT
 *      o TEEC_MEMREF_PARTIAL_INOUT
 * - 'params' is an array of four Parameters. For each parameter,
 *    one of the memref, tmpref, or value fields must be used depending
 *    on the corresponding parameter type passed in paramTypes as
 *    described in the specification of TEEC_Parameter.
 * - 'imp' contains any additional implementation-defined data attached
 *   to the operation structure.
 */
typedef struct
{
    uint32_t        started;
    uint32_t        paramTypes;
    TEEC_Parameter  params[8];
    /* Implementation-Specific Fields */
} TEEC_Operation;

#endif /* TEE_CLIENT_DATATYPES_H */
