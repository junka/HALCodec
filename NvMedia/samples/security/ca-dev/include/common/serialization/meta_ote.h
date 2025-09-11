/*
 * Copyright (c) 2017-2018 NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

/* *
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: Serialization Functions</b>
 *
 * @b Description: Declares meta data headers for payload and stream.
 */

/* *
 * @defgroup tlk_common_metadataheaders Meta data header definitions
 *
 * Declares data types for the payload meta data header and stream meta data
 * header.
 *
 * @ingroup ote_common
 * @{
 */

#pragma once

#include <common/ote_common.h>

/*
 * Serialized buffer format
 * +-------------------------------------------------------------------+
 * |   Stream header  |  Payload 1 header | Payload 1| [Next payload   |
 * | (stream_header_t)| (payload_header_t)|  (data)  |  header + data] |
 * +-------------------------------------------------------------------+
 */

#define STREAM_HEADER_MAGIC	0xfeedbeefU
#define STREAM_HEADER_CUR_VERSION	0x1U

#define PAYLOAD_HEADER_MAGIC		0xcafebabeU

#define STREAM_HEADER_LEN	(sizeof(stream_header_t))
#define PAYLOAD_HEADER_LEN	(sizeof(payload_header_t))

/*
 * If serialization buffers are allocated in device memory, make sure
 * they are aligned by telling the compiler explicitly about it.
 *
 * @brief payload meta data header
 * @magic payload header magic
 * @type payload_type_t object
 * @index te_operation_t object linked list index
 * @length length of the payload followed by the header
 */
#if defined(WITH_WORD_ALIGNED_FLAT_BUFFERS)
typedef struct {
	uint32_t magic __attribute__((aligned(4)));
	te_oper_param_type_t type __attribute__((aligned(4)));
	uint32_t index __attribute__((aligned(4)));
	uint32_t length __attribute__((aligned(4)));
} payload_header_t;
#else
typedef struct {
	uint32_t magic;
	te_oper_param_type_t type;
	uint32_t index;
	uint32_t length;
} payload_header_t;
#endif

/*
 * If serialization buffers are allocated in device memory, make sure
 * they are aligned by telling the compiler explicitly about it.
 *
 * @brief stream meta data header
 * @magic 4 byte id
 * @version version of the serialized buffer format
 * @command te_operation_t object command
 * @status te_operation_t object status code
 * @interface_side te_operation_t object interface side
 * @num_entries number of payload entries in the serialized buffer
 * @total_length length of the serialized buffer
 */
#if defined(WITH_WORD_ALIGNED_FLAT_BUFFERS)
typedef struct {
	uint32_t magic __attribute__((aligned(4)));
	uint32_t version __attribute__((aligned(4)));
	uint32_t command __attribute__((aligned(4)));
	te_error_t status __attribute__((aligned(4)));
	uint32_t interface_side __attribute__((aligned(4)));
	uint32_t num_entries __attribute__((aligned(4)));
	uint32_t total_length __attribute__((aligned(4)));
} stream_header_t;
#else
typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t command;
	te_error_t status;
	uint32_t interface_side;
	uint32_t num_entries;
	uint32_t total_length;
} stream_header_t;
#endif

/** @} */
