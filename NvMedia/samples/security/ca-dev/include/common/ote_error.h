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
 * <b> NVIDIA Trusted Little Kernel Interface: Error Handling</b>
 *
 * @b Description: Declares the error handling codes for the TLK interface.
 */

/**
 * @defgroup tlk_common_errors Errors
 *
 * Defines common error codes for Trusted Little Kernel (TLK).
 *
 * @ingroup ote_common
 *
 * @{
 */

#ifndef __OTE_ERROR_H
#define __OTE_ERROR_H

/** Defines Open Trusted Environment (OTE) error codes. */
typedef enum {
        /// Indicates the operation was successful.
	OTE_SUCCESS		  = 0x00000000U,
        /// Indicates the operation was successful.
	OTE_ERROR_NO_ERROR	  = 0x00000000U,
        /// Indicates an unspecified error occurred.
	OTE_ERROR_GENERIC	  = 0xFFFF0000U,
        /// Indicates access privileges are insufficient.
	OTE_ERROR_ACCESS_DENIED	  = 0xFFFF0001U,
        /// Indicates the operation was cancelled.
	OTE_ERROR_CANCEL	  = 0xFFFF0002U,
        /// Indicates a concurrent accesses conflict.
	OTE_ERROR_ACCESS_CONFLICT = 0xFFFF0003U,
        /// Indicates data passed exceeds request.
	OTE_ERROR_EXCESS_DATA	  = 0xFFFF0004U,
        /// Indicates input data is in an invalid format.
	OTE_ERROR_BAD_FORMAT	  = 0xFFFF0005U,
        /// Indicates input parameters are invalid.
	OTE_ERROR_BAD_PARAMETERS  = 0xFFFF0006U,
        /// Indicates the operation is invalid in its current state.
	OTE_ERROR_BAD_STATE	  = 0xFFFF0007U,
        /// Indicates the requested data item was not found.
	OTE_ERROR_ITEM_NOT_FOUND  = 0xFFFF0008U,
        /// Indicates the requested operation was not implemented.
	OTE_ERROR_NOT_IMPLEMENTED = 0xFFFF0009U,
        /// Indicates the requested operation is not supported.
	OTE_ERROR_NOT_SUPPORTED	  = 0xFFFF000AU,
        /// Indicates the data expected is missing.
	OTE_ERROR_NO_DATA	  = 0xFFFF000BU,
        /// Indicates the system ran out of resources.
	OTE_ERROR_OUT_OF_MEMORY	  = 0xFFFF000CU,
        /// Indicates the system is busy.
	OTE_ERROR_BUSY		  = 0xFFFF000DU,
        /// Indicates that communication failed.
	OTE_ERROR_COMMUNICATION	  = 0xFFFF000EU,
        /// Indicates a security fault was detected.
	OTE_ERROR_SECURITY	  = 0xFFFF000FU,
        /// Indicates the supplied buffer is too short.
	OTE_ERROR_SHORT_BUFFER	  = 0xFFFF0010U,
        /// Task administratively blocked, does not accept new sessions.
	OTE_ERROR_BLOCKED	  = 0xFFFF0011U,
        /// Indicates no answer was received from the command target.
	OTE_ERROR_NO_ANSWER = 0xFFFF1003U,
} te_error_t;

/** Defines the origin of an error. */
typedef enum {
        /// Indicates the error originated from the OTE Client API.
        OTE_RESULT_ORIGIN_API	     = 1U,
        /// Indicates the error originated from the underlying
        /// communications stack.
        OTE_RESULT_ORIGIN_COMMS	     = 2U,
        /// Indicates the error originated from the common OTE kernel code.
        OTE_RESULT_ORIGIN_KERNEL	     = 3U,
        /// Indicates the error originated from the Trusted Application code.
        OTE_RESULT_ORIGIN_TRUSTED_APP = 4U,
} te_result_origin_t;

/** @} */

#endif
