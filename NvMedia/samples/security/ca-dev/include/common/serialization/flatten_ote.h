/*
 * Copyright (c) 2017 NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/**
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: Serialization Functions</b>
 *
 * @b Description: Declares functions that serialize and deserialize TLK
 *     operation objects.
 */

/**
 * @defgroup tlk_common_serialization_ftns Serialization Functions
 *
 * Declares functions that serialize and deserialize TLK operation objects. The
 * serialized version of a TLK operation object is known as a serialized
 * context.
 *
 * @ingroup ote_common
 * @{
 */

#pragma once

#include <common/ote_common.h>
#include <common/ote_error.h>
#include <common/serialization/context.h>

/*
 * @brief flattens given operation object by applying appropriate meta data
 *        headers
 * @op              [in] te_operation_t object. used as a source to
 *                       serialization library
 * @sc_p            [out] serialized_context_t object populated with send buffer
 *                        and its size
 */
te_error_t serialize_object(te_operation_t *op, serialized_context_t *sc_p);

/*
 * @brief extracts and reconstructs te_operation_t object from a serialized
 *        buffer
 * @sc           [in]  serialized_context_t object. Must be populated with
 *                     non-NULL recv_flatten_buf reference
 * @op           [out] te_operation_t object that is constructed from the
 *                     serialized buffer
 */
te_error_t deserialize_object(serialized_context_t *sc, te_operation_t *op_p);

/** @} */
