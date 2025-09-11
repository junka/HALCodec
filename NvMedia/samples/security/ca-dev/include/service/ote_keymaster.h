/*
 * Copyright (c) 2013-2016, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: NVIDIA Cryptography</b>
 *
 * @b Description: Declares the cryptography APIs in the TLK.
 */

/**
 * @defgroup tlk_services_keymaster Keymaster Service Manager
 *
 * Defines APIs for managing Trusted Little Kernel (TLK)
 * keymaster services.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_KEYMASTER_H
#define __OTE_KEYMASTER_H

#include <common/ote_error.h>

/*! Initializes and opens a keymaster service session.
 *  This function keeps track of the number of open sessions.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_keymaster_init(void);

/*! Closes a keymaster service session.
 *
 *
* \retval OTE_SUCCESS Indicates the operation was successful.
 *
 */
te_error_t ote_keymaster_deinit(void);

/*! Gets the AuthTokenKey.
 *
 *  \param [in,out]     key_buf          A pointer to the key, allocated by the
 *                                       callee and freed by the caller.
 *  \param [in,out]     size             The length of the buffer in bytes.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t ote_keymaster_get_auth_token_key(const uint8_t** key_buf, size_t* size);

#endif // __OTE_KEYMASTER_H
