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
 * <b> NVIDIA Trusted Little Kernel Interface: Cryptography</b>
 *
 * @b Description: Declares the cryptography APIs in the TLK.
 */

/**
 * @defgroup tlk_services_crypto Crypto Service
 *
 * Defines APIs for Trusted Little Kernel (TLK) crypto services.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_CRYPTO_H
#define __OTE_CRYPTO_H

#include <service/ote_attrs.h>

/* Crypto object/operation forward definition.  */
struct __te_crypto_object;
typedef struct __te_crypto_object *te_crypto_object_t;
struct __te_crypto_operation_t;
typedef struct __te_crypto_operation_t *te_crypto_operation_t;

/*! Holds a crypto operation info object. */
typedef struct {
	uint32_t algorithm;
	uint32_t operation_class;
	uint32_t mode;
	uint32_t digest_length;
	uint32_t key_size;
	uint32_t required_key_usage;
	uint32_t handle_state;
} te_crypto_operation_info_t;

/*! Internal data structure for \c te_crypto_operation_t.  */
struct __te_crypto_operation_t {
	te_crypto_operation_info_t info;
	void *key;
	void *iv;
	size_t iv_len;
	void *imp_obj;
	te_error_t (*init)(te_crypto_operation_t operation);
	te_error_t (*update)(te_crypto_operation_t operation,
			const void *src_data,
			size_t src_size,
			void *dst_dat,
			size_t *dst_size);
	te_error_t (*do_final)(te_crypto_operation_t operation,
			const void *srd_data,
			size_t src_size,
			void *dst_data,
			size_t *dst_size);
	te_error_t (*handle_req)(te_crypto_operation_t operation,
			const void *src_data,
			size_t src_size,
			void *dst_data,
			size_t *dst_size);
	void (*free)(te_crypto_operation_t operation);
};

/*! Holds internal data for RSA keys. */
typedef struct {
	/*! Holds public modulus. */
	uint8_t *public_mod;
	/*! Holds public modulus length in bytes. */
	int public_mod_len;
	/*! Holds public exponent. */
	uint8_t *public_expo;
	/*! Holds public exponent length in bytes. */
	int public_expo_len;
	/*! Holds private exponent. */
	uint8_t *private_expo;
	/*! Holds private exponent length in bytes. */
	int private_expo_len;
	/*! Holds secret prime factor. */
	uint8_t *prime1;
	/*! Holds \a prime1 length in bytes. */
	int prime1_len;
	/*! Holds secret prime factor. */
	uint8_t *prime2;
	/*! Holds \a prime2 length in bytes. */
	int prime2_len;
	/*! Holds d mod (p-1). */
	uint8_t *expo1;
	/*! Holds \a expo1 length in bytes. */
	int expo1_len;
	/*! Holds d mod (q-1). */
	uint8_t *expo2;
	/*! Holds \a expo2 length in bytes. */
	int expo2_len;
	/*! Holds q^-1 mod p. */
	uint8_t *coeff;
	/*! Holds the coefficient length in bytes. */
	int coeff_len;
} te_crypto_rsa_key_t;

/*!
 *  Allocates memory for a ::te_crypto_object_t.
 *
 * \param [in,out] obj A pointer to new crypto object.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_OUT_OF_MEMORY Indicates the system ran out of resources.
 */
te_error_t te_allocate_object(te_crypto_object_t *obj);

/*! Populates crypto object from a list of attributes.
 *  Allocates \a obj internal memory and copies attributes to \a obj.
 *
 * \param [in,out] obj Crypto object to store attributes.
 * \param [in] attrs Array of attributes.
 * \param [in] attr_count Array length.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_populate_object(te_crypto_object_t obj, te_attribute_t *attrs,
		uint32_t attr_count);

/*! Frees allocated memory within crypto object.
 *
 * \param [in] obj Crypto object.
 */

void te_free_object(te_crypto_object_t obj);

/*! Defines algorithm types. */
typedef enum {
	OTE_ALG_AES_ECB_NOPAD	= 0x10000010,
	OTE_ALG_AES_CBC_NOPAD	= 0x10000110,
	OTE_ALG_AES_CTR		= 0x10000210,
	OTE_ALG_AES_CTS		= 0x10000310,
	OTE_ALG_AES_ECB		= 0x10000510,
	OTE_ALG_AES_CBC		= 0x10000610,
	OTE_ALG_AES_CBC_256	= 0x10000710,
	OTE_ALG_AES_CBC_256_NOPAD	= 0x10000810,
	OTE_ALG_AES_CMAC_128	= 0x20000110, /* AES-CBC w/ 128 bit key */
	OTE_ALG_AES_CMAC_192	= 0x20000120, /* AES-CBC w/ 192 bit key */
	OTE_ALG_AES_CMAC_256	= 0x20000130, /* AES-CBC w/ 256 bit key */
	OTE_ALG_SHA_HMAC_224	= 0x20000210, /* HMAC w/ SHA224 */
	OTE_ALG_SHA_HMAC_256	= 0x20000220, /* HMAC w/ SHA256 */
	OTE_ALG_SHA_HMAC_384	= 0x20000230, /* HMAC w/ SHA384 */
	OTE_ALG_SHA_HMAC_512	= 0x20000240, /* HMAC w/ SHA512 */
	OTE_ALG_SHA_HMAC_1	= 0x20000250, /* HMAC w/ SHA1 */
	OTE_ALG_RSA_PKCS_OAEP	= 0x30000100,
	OTE_ALG_RSA_PSS		= 0x30000200,
	OTE_ALG_PKCS1_Block1	= 0x30000300,
	OTE_ALG_DRNG		= 0x50000000, /* Deterministic RNG */
} te_oper_crypto_algo_t;


#define AES_BLOCK_SIZE			16
#define AES_ENCRYPTION_PADDED_SIZE(clearlen) \
        ((((clearlen) + AES_BLOCK_SIZE) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE)

#define OTE_AES_MODE_NEEDS_PADDING(algo) \
	(((algo) == OTE_ALG_AES_ECB) || ((algo) == OTE_ALG_AES_CBC) || \
	((algo) == OTE_ALG_AES_CBC_256))

/*! Defines algrorithm modes. */
typedef enum {
	OTE_ALG_MODE_ENCRYPT,
	OTE_ALG_MODE_DECRYPT,
	OTE_ALG_MODE_SIGN,
	OTE_ALG_MODE_VERIFY,
	OTE_ALG_MODE_DIGEST,
	OTE_ALG_MODE_DERIVE
} te_oper_crypto_algo_mode_t;

/*! Allocates memory for crypto operation.
 *  Allocates crypto operation internal memory.
 *  Initializes operation based on algo/mode.
 *
 * \param [in,out] oper      A pointer to a crypto operation object.
 * \param [in]     algorithm Crypto algorithm.
 * \param [in]     mode      Crypto algorithm mode.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_allocate_operation(te_crypto_operation_t *oper,
		te_oper_crypto_algo_t algorithm,
		te_oper_crypto_algo_mode_t mode);

/*!
 *  Allocates memory in the crypto operation and copies the key from
 *  the crypto object to the operation object.
 *
 * \param [in,out] oper Crypto operation object.
 * \param [in]     obj Key crypto object.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_set_operation_key(te_crypto_operation_t oper,
		te_crypto_object_t obj);

/*! Initializes the operation cipher.
 *  Sets the initialization vector and calls the \c init function.
 *
 * \param [in,out] oper    Crypto operation object.
 * \param [in]     iv      Initialization vector.
 * \param [in]     iv_size Initialization vector size.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_cipher_init(te_crypto_operation_t oper, void *iv,
		size_t iv_size);

/*!
 * Updates the cipher by calling the operation \c update function
 * with the supplied parameters.
 *
 * \param [in]     oper     Crypto operation object
 * \param [in]     src_data A pointer to the source data buffer supplied
 *                 to \c init.
 * \param [in]     src_size Source buffer size supplied to \c init.
 * \param [in,out] dst_data A pointer to the destination data buffer supplied
 *                 to \c init.
 * \param [in,out] dst_size A pointer to the destination buffer size supplied
 *                 to \c init.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_cipher_update(te_crypto_operation_t oper, const void *src_data,
		size_t src_size, void *dst_data, size_t *dst_size);

/*!
 *  Calls operation \c do_final with supplied parameters.
 *
 * \param [in]     oper     Crypto operation object.
 * \param [in]     src_data NULL expected.
 * \param [in]     src_len  Zero expected.
 * \param [in,out] dst_data A pointer to the destination data buffer supplied
 *                 to \c do_final.
 * \param [in,out] dst_len  A pointer to the destination buffer size supplied
 *                 to \c do_final.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_PARAMETERS Indicates src_len was not 0
 *				or dst_data/dst_len was NULL.
 */
te_error_t te_cipher_do_final(te_crypto_operation_t oper, const void *src_data,
		size_t src_len, void *dst_data, size_t *dst_len);

/*! Initializes the RSA operation.
 *
 * \param [in,out] oper    Crypto operation object.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_rsa_init(te_crypto_operation_t oper);

/*! Returns the RSA modulus size in bytes.
 *
 * \param [in] oper                   Crypto operation object.
 * \param [out] modulus_size_bytes    Size of the modulus in Bytes.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_rsa_get_modulus_size(te_crypto_operation_t oper, size_t *modulus_size_bytes);

/*!
 *  Executes the specified RSA operations.
 *
 * \param [in]     oper     Crypto operation object.
 * \param [in]     src_data A pointer to the source data buffer supplied to
 *                 \c init.
 * \param [in]     src_size Source buffer size supplied to \c init.
 * \param [in,out] dst_data A pointer to the destination data buffer supplied
 *                 to \c init.
 * \param [in,out] dst_size A pointer to the destination buffer size supplied
 *                 to \c init.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_rsa_handle_request(te_crypto_operation_t oper,
		const void *src_data, size_t src_size,
		void *dst_data, size_t *dst_size);

/*! Frees operation internal memory. */
void te_free_operation(te_crypto_operation_t oper);

#define HWRNG_CACHE_SIZE_BYTES 1024
#define HWRNG_CACHE_ENTROPY_SIZE_BYTES 256

/* The hwrng cache is structured as follows:
 * [  Randomness available to TAs  |  Entropy for Crypto Libraries  ]
 *                                 <-HWRNG_CACHE_ENTROPY_SIZE_BYTES->
 * <-----------------HWRNG_CACHE_SIZE_BYTES------------------------->
 */

/*
 * @brief Cache to store random numbers generated by the HW.
 * @cache           Buffer to hold random data
 * @size            Overall Size of the Buffer
 * @entropy_size    Reserved Size to seed Entropy in a s/w crypto library
 * @bytes_available Number of Bytes available in the cache
 */
struct hwrng_cache {
	uint8_t *cache;
	size_t size;
	size_t entropy_size;
	size_t bytes_available;
};

/*! Generates random data.
 *
 * \param [in, out]  buffer A pointer to the buffer to be filled.
 * \param [in]       size   Length of the buffer.
 */
void te_generate_random(void *buffer, size_t size);

/*! Finds the first attribute in the crypto object that matches ID.
 *
 * \param [in]  object Crypto object.
 * \param [in]  id Attribute ID to match.
 * \param [out] ret A double pointer to the attribute found.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_ITEM_NOT_FOUND Indicates the requested data item was not
 *         found.
 */
te_error_t te_get_attribute_by_id(te_crypto_object_t object,
		te_attribute_id_t id, te_attribute_t **ret);

/*! Crypto wrapper around software crypto library.
 *  This calls te_cipher_init, te_cipher_update and te_cipher_do_final
 *  on the supplied input buffer and returns the resulting output.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *
 *  \param [in]        mode       Mode to use, e.g. decrypt/encrypt.
 *  \param [in]        algo       Algorithm to be used.
 *  \param [in]        key        Key to be used.
 *  \param [in]        keysize    The length of the key in bytes.
 *  \param [in]        iv         Initialization Vector.
 *  \param [in]        ivsize     The length of initialization vector in bytes.
 *  \param [in]        in         Input buffer.
 *  \param [in]        insize     The length of the input buffer in bytes.
 *  \param [out]       out        Output Buffer.
 *  \param [in, out]   outsize    The length of the output buffer in bytes.
 */
te_error_t te_crypt_data(te_oper_crypto_algo_mode_t mode,
			te_oper_crypto_algo_t algo, unsigned char *key,
			unsigned int keysize,
			unsigned char *iv, unsigned int ivsize,
			unsigned char *in, unsigned int insize,
			unsigned char *out, unsigned int *outsize);

/** @} */

#endif
