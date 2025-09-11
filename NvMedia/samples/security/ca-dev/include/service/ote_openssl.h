/*
 * Copyright (c) 2012-2014, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: SSL Methods</b>
 *
 * @b Description: Declares the SSL functions in the TLK.
 */

/**
 * @defgroup tlk_services_ssl_layer Secure Sockets Layer
 *
 * TLK services SSL functions.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_OPENSSL_H
#define __OTE_OPENSSL_H

#include <service/ote_crypto.h>
#include <common/ote_error.h>

/*! Initializes crypto operation for AES.
 *
 *  \param [in,out] operation Crypto operation object
 *
 *  \retval OTE_SUCCESS on success.
 */
te_error_t openssl_aes_init(te_crypto_operation_t operation);

/*!
 *  AES update with crypto object and supplied parameters.
 *
 *  Called by operation->update(operation, ...)
 *
 *  \param [in]  operation Crypto operation object
 *  \param [in]  src       Source buffer
 *  \param [in]  src_len   Source buffer length
 *  \param [out] dst       Destination buffer
 *  \param [out] dst_len   Output length
 *
 *  @return OTE_SUCCESS on success.
 */
te_error_t openssl_aes_update(te_crypto_operation_t operation, const void *src,
		size_t src_len, void *dst, size_t *dst_len);

/*!
 *  AES \c do_final with crypto object and supplied parameters.
 *
 *  Called by operation->do_final(operation, ...)
 *
 *  \param [in]  operation Crypto operation object
 *  \param [in]  src       NULL expected
 *  \param [in]  src_len   Zero expected
 *  \param [out] dst       Destination buffer
 *  \param [out] dst_len   Output length
 *
 *  @return OTE_SUCCESS on success.
 */
te_error_t openssl_aes_do_final(te_crypto_operation_t  operation,
		const void *src, size_t src_len,
		void *dst, size_t *dst_len);

/*!
 *  Free AES internal allocated memory.
 *
 *  Called by operation->free(operation, ...)
 *
 *  \param [in] operation Crypto operation object.
 */
void openssl_aes_free(te_crypto_operation_t operation);

/*! \brief Initialize MAC operation.
 *
 *  Return OTE_SUCCESS on success.
 *
 *  \param [in] op Crypto operation object
 */
te_error_t openssl_mac_init(te_crypto_operation_t op);

/*! \brief Perform the actual MAC operation
 *
 *  Called by operation->update(operation, ...)
 *
 *  Return OTE_SUCCESS on success.
 *
 *  \param [in]  operation Crypto operation object
 *  \param [in]  src       Source buffer
 *  \param [in]  src_size  Source buffer length
 *  \param [out] dst       Destination buffer
 *  \param [out] dst_size  Output length
 */
te_error_t openssl_mac_update(te_crypto_operation_t operation, const void *src,
		size_t src_size, void *dst, size_t *dst_size);

/*! \brief Free internal allocated memory.
 *
 *  Called by operation->free(operation, ...)
 *
 *  \param [in] operation Crypto operation object.
 */
void openssl_mac_free(te_crypto_operation_t operation);

/*! Initializes a crypto operation for RSA.
 *
 *  \param [in,out] op  The object of the crypto operation.
 *
 *  \retval OTE_SUCCESS on success.
 */
te_error_t openssl_rsa_init(te_crypto_operation_t op);

/*!
 *  RSA-OAEP encrypt-decrypt function
 *
 *  Called by operation->handle_req(operation, ...)
 *
 *  \param [in]  operation Object of the crypto operation.
 *  \param [in]  src       A pointer to the source buffer.
 *  \param [in]  src_size  Size of the source buffer.
 *  \param [out] dst       A pointer to the destination buffer.
 *  \param [out] dst_size  A pointer to the length of data in the destination
 *                         buffer.
 *  @return OTE_SUCCESS on success.
 */
te_error_t openssl_rsa_handle_request(te_crypto_operation_t operation,
		const void *src, size_t src_size,
		void *dst, size_t *dst_size);

/*!
 *  Free RSA internal allocated memory.
 *
 *  Called by operation->free(operation, ...)
 *
 *  \param [in] operation Crypto operation object.
 */
void openssl_rsa_free(te_crypto_operation_t operation);

/*!
 *  Get random bytes
 *
 *  \param [in] buffer input buffer.
 *  \param [in] size input buffer size.
 */
te_error_t openssl_rand_bytes(void *buffer, size_t size);

/** @} */

#endif /* __OTE_OPENSSL_H  */
