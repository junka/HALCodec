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
 * <b> NVIDIA Trusted Little Kernel Interface: Serialized Context</b>
 *
 * @b Description: Declares the serialized context object and supporting
 *     functions in the Tristed Little Kernel (TLK) services.
 */

/**
 * @defgroup tlk_common_serialization Serialized Context Object
 *
 * Defines a struct for the TLK's serialized context object, and several
 * supporting functions.
 *
 * @ingroup ote_common
 * @{
 */

#pragma once

#include <common/ote_error.h>

/*
 * @brief serialized context object. used to hold context over an receive/send
 *        operation
 * @recv_flatten_buf buffer pointer to hold serialized buffer reference for
 *                   receive operation
 * @send_flatten_buf buffer pointer to hold serialized buffer reference for
 *                   send operation
 */
typedef struct {
	uint8_t *recv_flatten_buf;
	size_t recv_buf_size;
	uint8_t *send_flatten_buf;
	size_t send_buf_size;
} serialized_context_t;

/*
 * @brief initializes serialized context. resets the buffer pointers so that
 *        a new context may begin
 * @sc    serialized_context_t object
 */
te_error_t serialized_context_init(serialized_context_t *sc);

/*
 * @brief destroys the serialized context resources. frees the buffers after
 *        their use is done
 * @sc    serialized_context_t object
 */
void serialized_context_destroy(serialized_context_t *sc);

/** @} */
