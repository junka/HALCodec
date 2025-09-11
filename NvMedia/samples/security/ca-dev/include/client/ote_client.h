/*
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: Client Communications</b>
 *
 * @b Description: Declares the TLK client interface.
 */

/**
 *
 * @defgroup tlk_client Client Application Interface
 *
 * Defines the client application APIs.
 *
 * @ingroup ote_modules
 * @{
 */

#ifndef __OTE_CLIENT_H
#define __OTE_CLIENT_H

#include <common/ote_command.h>

#define TLK_DEVICE_BASE_NAME "tlk_device"
#define TE_IOCTL_MAGIC_NUMBER  ('t')

#define TE_IOCTL_OPEN_CLIENT_SESSION \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x10, union te_cmd)
#define TE_IOCTL_CLOSE_CLIENT_SESSION  \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x11, union te_cmd)
#define TE_IOCTL_LAUNCH_OP \
	_IOWR(TE_IOCTL_MAGIC_NUMBER, 0x14, union te_cmd)

/* secure storage ioctl */
#define TE_IOCTL_SS_CMD \
	_IOR(TE_IOCTL_MAGIC_NUMBER, 0x30, int)

#define TE_IOCTL_SS_CMD_GET_NEW_REQ	1
#define TE_IOCTL_SS_CMD_REQ_COMPLETE	2

/* macros to help manage debug output */
#define LIBCA_ERR(args...)		te_fprintf(TE_ERR, args)
#define LIBCA_INFO(args...)		te_fprintf(TE_INFO, args)
#define LIBCA_SECURE(args...)		te_fprintf(TE_SECURE, args)

/** Defines secure monitor calls (SMC) that clients use to communicate
    with trusted applications (TAs) in the secure world. */
enum {
	TLK_SMC_REQUEST = 0xFFFF1000,    /**< Requests OTE to launch a TA operation. */
	TLK_SMC_GET_MORE = 0xFFFF1001,   /**< Gets a pending answer without making new operation. */
	TLK_SMC_ANSWER = 0xFFFF1002,     /**< Answers from secure side. */
	TLK_SMC_NO_ANSWER = 0xFFFF1003,  /**< No answers for now (secure side idle). */
	TLK_SMC_OPEN_SESSION = 0xFFFF1004,
	TLK_SMC_CLOSE_SESSION = 0xFFFF1005,
};

/**
 * @defgroup tlk_client_app_com User Application Communication
 * @ingroup tlk_client
 * @{
 */

struct te_answer {
	uint32_t result;
	uint32_t session_id;
	uint32_t result_origin;
};


/**
 * Opens an open trusted environment (OTE) session.
 */
struct ote_opensession {
	te_service_id_t dest_service_id;
	te_operation_t operation;
	cmnptr_t answer;
};

/**
 * Closes an OTE session.
 */

struct ote_closesession {
	uint32_t session_id;
	cmnptr_t answer;
};

/**
 * Launches an operation request.
 */
struct ote_launchop {
	uint32_t session_id;
	te_operation_t operation;
	cmnptr_t answer;
};

union te_cmd {
	struct ote_opensession opensession;
	struct ote_closesession closesession;
	struct ote_launchop launchop;
};

/**
 * Currently supported Trusted OS variants.
 */
typedef enum {
	TOS_NONE = 0x0,
	TOS_TRUSTY = 0x1,
	TOS_TLK = 0x2
} tos_type_t;

/**
 * Returns the current Trusted OS type
 */
tos_type_t te_tos_type(void);

/** @} <!-- tlk_client_app_com --> */

/** @} */

#endif
