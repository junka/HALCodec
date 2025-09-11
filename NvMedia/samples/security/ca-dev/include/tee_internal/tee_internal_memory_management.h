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
 * @brief <b>GlobalPlatform: TEE Internal Memory Management</b>
 *
 * @b Description: Describes TEE internal memory management.
 */

/**
 * @defgroup global_platform_imm TEE Internal Memory Management
 *
 * Describes TEE internal memory management.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_MEMORY_MANAGEMENT_H
#define TEE_INTERNAL_MEMORY_MANAGEMENT_H

#include <tee_internal/tee_internal_datatypes.h>

/// \brief This function allocates memory for use by the caller
///
/// (QNX PDK only) Error behavior:
///             (1) If input hint is not a valid memory type, halt the system
///             (2) All other errors return an nullptr.
///
/// (QNX PDK Only) Side effects:
///             one memory 256-Byte chunk will be marked as allocated if
///             the allocation succeeds.
///
/// \param[in] size Size of memory chunk required. Unit: Byte.
///            (QNX PDK Only) Valid range: positive number <= 256.
/// \param[in] hint Specifies the memory type.
///            Valid values: \n
///            (1) TEE_ALLOC_HEAP_MEM for normal memory;
///            (2) TEE_ALLOC_DEVICE_MEM for non-speculative memory;
///            (3) TEE_ALLOC_TZSYSRAM_MEM for TZSysRAM memory (QNX PDK only)
///
/// \return returns a pointer to the allocated space on Success,
///         nullptr, if the space cannot be allocated or parameters are
///         invalid.
///
void* TEE_Malloc(const size_t size, const uint32_t hint);

/// \brief The TEE_Free function deallocates memory that was previously
///        allocated with the TEE_Malloc() API.
///
/// (QNX PDK Only) Error behavior:
///        (1) If buffer is a NULL pointer, TEE_Free does nothing.
///        (2) If the provided address is not currently allocated with
///            TEE_Malloc() API, halt the system.
///
/// (QNX PDK Only) Side effects:
///        one memory chunk will be returned to the pool if free succeeds.
///
/// \param[in] buffer The pointer to the memory buffer to be freed.
///
void TEE_Free(void *const buffer);

/// \brief This function copies size bytes from a particular buffer denoted as
///        source buffer to another buffer denoted as destination. If the
///        source and destination buffer overlap, the API will ensure that the
///        the final destination buffer contents match the contents of the
///        original source buffer.
///
/// (QNX PDK Only) Error behavior:
///                 (1) if the source or destination buffers are null, halt the
///                     system
///                 (2) if input arguments (buffer + size) result in an integer
///                     overflow, halt the system.
///                 (3) if the source address has been allocated with
///                     TEE_ALLOC_DEVICE_MEM and the destination address has
///                     not been allocated with either TEE_ALLOC_DEVICE_MEM or
///                     TEE_ALLOC_TZSYSRAM_MEM, the system will halt.
///                 (4) if the source address has been allocated with
///                     TEE_ALLOC_TZSYSRAM_MEM and the destination address has
///                     not been allocated with either TEE_ALLOC_DEVICE_MEM or
///                     TEE_ALLOC_TZSYSRAM_MEM, the system will halt.
///
/// (QNX PDK Only) Side effects:
///                 N/A
///
/// \param[in] src A pointer to the source buffer.
/// \param[out] dest A pointer to the destination buffer,
/// \param[in] size The number of bytes to be copied.
///
void TEE_MemMove(void *const dest, const void *const src, const uint32_t size);

/// \brief This function compares the first size bytes of the buffer pointed
///        to by buffer1 to the first size bytes of the buffer pointed to by
///        buffer2.
///
/// (QNX PDK Only) Error behavior:
///        (1) if either buffer parameter is null, halt the system.
///
/// (QNX PDK Only) Side effects:
///        N/A
///
/// \param[in] buffer1 A pointer to the first buffer.
/// \param[in] buffer2 A pointer to the second buffer.
/// \param[in] size The number of bytes to be compared.
///
/// \return 1. If buffer1 differs from buffer2, then return 1, irrespective of
///         the greater buffer.
///         2. If the first size bytes of the two buffers are identical,
///         then return zero.
///         3. (Linux PDK Only) If either buffer parameter is null, then
///         return '-1'
///
/// \note   This API is not GP (Global Platform) compliant and returns only 0
///         in case of same buffer contents and +1 in case of different
///         contents. It does not return the relation of the buffers and the
///         greater/ lesser buffer content cannot be determined.
///
/// \note   This API is resistant to side channel attacks (SCA-resistant).
///
int32_t TEE_MemCompare(const void *const buffer1, const void *const buffer2,
        const uint32_t size);

/// \brief The TEE_MemFill function writes the byte x (converted to a uint8_t)
///        into the first size bytes of the object pointed to by buffer.
///
/// (QNX PDK Only) Error behavior:
///        (1) if input buffer is nullptr, halt the system.
///        (2) if input arguments (buffer + size) result in an integer
///            overflow, halt the system.
///        (3) if parameter x is outside of the supported range, halt the system.
///
/// (QNX PDK Only) Side effects:
///        N/A
///
/// \param[out] buffer A pointer to the destination buffer.
/// \param[in] x The value to be set.
///     Valid Range (QNX PDK Only): 0 through 255
/// \param[in] size The number of bytes to be set.
///
void TEE_MemFill(void *const buffer, const uint32_t x, const uint32_t size);

/** @} */
#endif /* TEE_INTERNAL_MEMORY_MANAGEMENT_H */
