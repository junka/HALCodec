/*
 * Copyright (c) 2013-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: Common Declarations</b>
 *
 * @b Description: Declares the common declarations in the TLK interface.
 */

/**
 * @defgroup tlk_common_common Types
 *
 * Defines common data types and functions for Trusted Little Kernel (TLK).
 *
 * @ingroup ote_common
 *
 * @{
 */

#ifndef __OTE_COMMON_H
#define __OTE_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <compiler.h>
#include <common/ote_error.h>

#ifdef CONFIG_TRUSTY
#include <uapi/err.h>
#endif

/* Prefix for the UUID based port names, which are now fixed length
 * strings constructed from this prefix and the formatted TA UUID.
 *
 * Port name fixed size is strlen(SERVICE_PORT_NAME_PREFIX)+UUID_STR_SIZE_BYTES).
 * This size includes the terminating NULL.
 *
 * Sample port name (for the trusted_app): com.nvidia.tos.403A0B40-B50D-40a4-A87C-62D253FDA594.
 *
 */
#define SERVICE_PORT_NAME_PREFIX "com.nvidia.tos"

/* Length includes space for the uuid, dashes and NULL termination */
#define UUID_STR_SIZE_BYTES ((2U * sizeof(te_service_id_t)) + 4U)

/* Max Port Length is the sum of the Prefix + UUID Converted Post fix */
#define MAX_PORT_NAME_LENGTH sizeof(SERVICE_PORT_NAME_PREFIX) + UUID_STR_SIZE_BYTES

/*! Defines the maximum length of a zero-terminated informative task name.
 */
#define OTE_TASK_NAME_MAX_LENGTH 24

/*! Defines the length of private data for the Trusted Application (TA).
 * The definition goes in the manifest.
 * The semantics of this optional data is defined per each TA.
 *
 * To hold an SHA1 digest, this definition must be at least 20 bytes.
 * Such a digest enables building a chain of trust to a TA with the manifest
 * data.
 *
 * This definition can be used to perform tasks such as:
 * - Loading public keys
 * - Loading X509 certificates and digests
 */
#define OTE_TASK_PRIVATE_DATA_LENGTH 20

/*
 * Defines maximum chunk size allowed by the trusty kernel to pass in a single
 * SMC call. This value is referenced from Android Open Source implementation
 * of secure storage proxy daemon.
 */
#define TIPC_MAX_CHUNK_SIZE 4040

/*
 * Value of ACK that is agreed upon by NS clients and Trusty as
 * an application level protocol
 */
#define TIPC_CHUNK_MSG_ACK 0xcafefeedU

#define WORD_SIZE 	sizeof(uint32_t)
#define ROUNDUP(a, b) 	(((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))

/*! Defines a unique 16-byte ID for each TLK service. */
typedef struct {
	uint32_t time_low;
	uint16_t time_mid;
	uint16_t time_hi_and_version;
	uint8_t clock_seq_and_node[8];
} te_service_id_t;

#if !defined(CONFIG_TRUSTY)
/* Defines a struct used to communicate the status of a service
 *   uuid: ID of the TLK service
 *   valid: true if the service ID corresponds to a TA that is recognized by TLK
 *   active: true if service ID corresponds to an service that is currently running
 */
typedef struct {
	te_service_id_t uuid;
	bool valid;
	bool active;
} te_service_status_t;
#endif

/*! Specifies the operation object's parameter types. */
typedef enum {
	TE_PARAM_TYPE_NONE		= 0x0U,
	TE_PARAM_TYPE_INT_RO		= 0x1U,
	TE_PARAM_TYPE_INT_RW		= 0x2U,
	TE_PARAM_TYPE_MEM_RO		= 0x3U,
	TE_PARAM_TYPE_MEM_RW		= 0x4U,
	TE_PARAM_TYPE_PERSIST_MEM_RO	= 0x100U,
	TE_PARAM_TYPE_PERSIST_MEM_RW	= 0x101U,
	TE_PARAM_TYPE_FLAGS_PHYS_LIST   = 0x1000U,
	TE_PARAM_TYPE_ALL_FLAGS         = \
		(TE_PARAM_TYPE_FLAGS_PHYS_LIST)
} te_oper_param_type_t;

/*! Holds a pointer large enough to support 32- and 64-bit clients. */
typedef uint64_t	cmnptr_t;

/*! Holds session information. */
typedef union {
	struct {
		uint32_t session_id;
		uint32_t context_id;
		te_result_origin_t result_origin;
	} client;
	struct {
		uint32_t session_id;
		te_result_origin_t result_origin;
	} service;
} te_session_t;

/*! Holds the operation object parameters. */
typedef struct {
	uint32_t index;
	te_oper_param_type_t type;
	union {
		struct {
			uint32_t val;
		} Int;
		struct {
			cmnptr_t base;
			uint32_t len;
			uint32_t type;
		} Mem;
	} u;
	cmnptr_t next;
} te_oper_param_t;

enum {
	TE_MEM_TYPE_NS_USER = 0x0U,
	TE_MEM_TYPE_NS_KERNEL = 0x1U,
};

/*! Holds operation object information that is to be
 * delivered to the TLK Secure Service.
 * NOTE: This structure need to be in sync with \c te_operation_container_t
 */
typedef struct {
	uint32_t command;
	te_error_t status;

	/** Holds pointers to the head/tail of the list
         * of \a param_t nodes.
	 */
	cmnptr_t list_head;
	cmnptr_t list_tail;

	uint32_t list_count;
	uint32_t interface_side;
} te_operation_t;

/*! Returns the origin of a returned result.
*
* Because it is possible for the operation to fail anywhere
* in the pipeline, this function returns the general block
* where the returned result originated.
*
* \param[in] session A valid session pointer.
*
* \return A \c te_result_origin_t ID number.
*/
te_result_origin_t te_get_result_origin(te_session_t *session);

/** @} */

enum {
	TE_CRITICAL	= 0U,
	TE_ERR 		= 1U,
	TE_INFO		= 2U,
	TE_SPEW		= 3U,
	TE_SECURE	= 4U,
	TE_INTERFACE	= 5U,
	TE_RESULT	= 6U,
};

/*!
 * For secure tasks:
 * Redirects prints to Trusted Little Kernel (TLK) writes.
 * This is a printf function for TLK services.
 *
 * For clients:
 * Prints to stdout.
 */
int te_fprintf(int fd, const char *fmt, ...) __PRINTFLIKE(2,3);
int te_vfprintf(int lvl, const char* fmt, va_list ap);

/* Definitions for OTE TIPC wrapper */

/*! Holds Unique Task identifiers, including uuid and port name. */
typedef struct {
        te_service_id_t uuid;
        char port_name[MAX_PORT_NAME_LENGTH];
} task_identifier;

/*! Returns the port name of a secure task.
 *
 * \param[out] path     Port name of Secure Task.
 * \param[in]  max_len   Maximum length of the port name.
 * \param[in]  service  UUID of Secure Task.
 * \return OTE_SUCCESS if successful, or an error code otherwise.
 */
te_error_t get_port_name_by_uuid(char *path, uint32_t max_len, te_service_id_t *service);

/*! Translates TIPC Error Code to an OTE Error Code.
*
* \param[in] err_num TIPC Error code.
*
* \return OTE Error Code.
*/
te_error_t tipc_error_to_ote_error(int err_num);

/*!
 * Converts UUID to a string format.
 *
 * \param[out] ustr A pointer to a char * array in which to store the result.
 *
 * \param[in] ulen Length of the char * array.
 *
 * \param[in] uuid UUID to convert.
 *
 * \retval Error code.
 */
te_error_t te_uuid_to_string(char *ustr, uint32_t ulen,
			     const te_service_id_t *uuid);

/*! Converts an error code to appropriate string description.
*
* \param[in] errcode to be translated to string
*
* \return Error string corresponding to error code
*/
const char* te_strerror(te_error_t errcode);

/*!
 * Implements a range checker which is free from integer overflow.
 *
 * \param[in] range_base Base address of valid range to check against.
 *
 * \param[in] range_size Size of valid range to check against
 *
 * \param[in] base Base address to check against range_base
 *
 * \param[in] size Size to check against range_size
 *
 * \retval true if {base, to base+size} lies within
 *         {range_base to range_base+range_size}, false otherwise..
 */
bool te_validate_range(void* range_base, uint32_t range_size,
			void* base, uint32_t size);
#endif
