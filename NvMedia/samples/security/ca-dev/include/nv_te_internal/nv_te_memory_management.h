/*
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 *
 */

#ifndef NV_TE_MEMORY_MANAGEMENT_H
#define NV_TE_MEMORY_MANAGEMENT_H

/// \brief This function copies size bytes from a particular buffer denoted as
///        source buffer to another buffer denoted as destination. If the
///        source and destination buffer overlap, the API will ensure that the
///        the final destination buffer contents match the contents of the
///        original source buffer. It uses SecureMemcpy to minimalize the
///        intermidiate data leakage.
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
void TEE_SecureMemMove(void *const dest, const void *const src, const uint32_t size);

/// \brief The TEE_SecureMemFill function writes the byte x (converted to a uint8_t)
///        into the first size bytes of the object pointed to by buffer. It guarantees
///        aligned access and uses SecureMemcpy to minimalize the intermidiate data leakage.
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
void TEE_SecureMemFill(void *const buffer, const uint32_t x, const uint32_t size);

#endif /* NV_TE_MEMORY_MANAGEMENT_H */
