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
 * <b> NVIDIA Trusted Little Kernel Interface: NVIDIA Cryptography</b>
 *
 * @b Description: Declares the cryptography APIs in the TLK.
 */

/**
 * @defgroup tlk_services_nvcrypto Crypto Service Manager
 *
 * Defines APIs for managing Trusted Little Kernel (TLK)
 * crypto services.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_NVCRYPTO_H
#define __OTE_NVCRYPTO_H

#include <common/ote_error.h>

/**
 * @defgroup crypto_services Crypto Services
 * @{
 */

// mobile and common cmd id (start from 0x00)
#define CRYPTO_SERVICE_SANITY_CHECK		0
#define CRYPTO_SERVICE_GET_KEYBOX		1
#define CRYPTO_SERVICE_GET_STORAGE_KEY		2
#define CRYPTO_SERVICE_GET_ROLLBACK_KEY		3
#define CRYPTO_SERVICE_GET_RO_TRUST_KEY		4
#define CRYPTO_SERVICE_GET_WV_SIG_RSA_KEY	5
#define CRYPTO_SERVICE_GET_GS_SIG_KEY		6
#define CRYPTO_SERVICE_GET_VUDU_PLATFORM_KEY	7	/* Deprecated */
#define CRYPTO_SERVICE_GET_HWRANDOM		8
#define CRYPTO_SERVICE_GET_EKS2_MAC_KEY		9U
#define CRYPTO_SERVICE_INSTALL_EKS2_KEYS	10U
#define CRYPTO_SERVICE_GET_WIDEVINE_KEY		11U
#define CRYPTO_SERVICE_GET_KEYBOX_ATTRIBUTE	12U
#define CRYPTO_SERVICE_GET_IPP_CLIENT_DEC_KEY   13U
#define CRYPTO_SERVICE_GET_WIDEVINE_RENEWAL_KEY		14U

// auto cmd id (start from 0x80)
#define CRYPTO_SERVICE_GET_STORAGE_MASTER_KEY 	CRYPTO_SERVICE_GET_STORAGE_KEY	// for compatibility
#define CRYPTO_SERVICE_GET_RANDOM_NUMBER 	CRYPTO_SERVICE_GET_HWRANDOM
#define CRYPTO_SERVICE_DERIVE_KEY 		0x80
#define CRYPTO_SERVICE_UPDATE_SE_KEYSLOT	0x81
#define CRYPTO_SERVICE_RSA_RAW_PRIVATE_ENCRYPT	0x82
#define CRYPTO_SERVICE_DO_CRYPT_FUNCTIONS 	0x83
#define CRYPTO_SERVICE_GET_EFS_MASTER_KEY	0x84
/* Adding SE Keyslot access interface through crypto service */
#define CRYPTO_SERVICE_REQUEST_SE_KEYSLOT	0x85
#define CRYPTO_SERVICE_RELEASE_SE_KEYSLOT	0x86
/** @} */

// IP Protection client type
#define IPP_CLIENT_DOLBY			0
#define IPP_CLIENT_SUPERRES			1

// AES, RSA keyslot types registered at kernel using apps manifest.c
#define KEYSLOT_TYPE_AES			0
#define KEYSLOT_TYPE_RSA			1

// HW keyslot accessible setting
enum {
	KEYSLOT_RICH_OS_NO_READ_NO_WRITE_KEY_ACCESSIBLE = 0,
	KEYSLOT_CONFIG_ALL,
};

/*
 * @brief defines various key lookup schemes supported by NVCrypto
 *        for each lookup scheme, the key index has a different meaning
 * KEY_LOOKUP_ABSOLUTE Absolute lookup. Use this option when a client knows the
 *                     exact key index of the key slot. This option is for
 *                     backward compatibility as old key lookup implementation
 *                     uses absolute lookups. Ideally, this should be
 *                     deprecated
 * KEY_LOOKUP_RELATIVE Lookup relative to UUID. If there are more than one keys
 *                     present for a given UUID, clients can use this lookup
 *                     to get Nth key from the keyslots that they are allowed
 *                     to access (UUID is an access control field)
 * KEY_LOOKUP_BY_ID    Lookup by entry ID. Each Key is uniquely identified by
 *                     (UUID, Entry ID) tuple. Client can use this lookup if it
 *                     knows the exact tuple to access.
 */
typedef enum {
	KEYSTORE_LOOKUP_ABSOLUTE = 1,
	KEYSTORE_LOOKUP_RELATIVE,
	KEYSTORE_LOOKUP_BY_ID
} keystore_lookup_type;

/*
 * @brief defines various attributes that can be queried for a keybox entry
 *
 * KEYBOX_ATTR_SIZE	The length of a particular keybox entry
 */
typedef enum {
	KEYBOX_ATTR_SIZE = 1,
} keybox_attr_type;

/*! Initializes and opens an nvcrypto service session.
 *  This function keeps track of the number of open sessions.
 *
 * The \c ote_nvcrypto* functions provide an interface to the crypto_service.
  *
 * To use the crypto_service through this interface:
 *
 * 1. Initialize the service with ote_nvcrypto_init()
 * 2. Call the necessary operation such as ote_nvcrypto_get_keybox() or ote_nvcrypto_get_key().
 * 3. Un-initalize the service with ote_nvcrypto_deinit().
 *
 * \retval OTE_SUCCESS       The operation was successful.
 */
te_error_t ote_nvcrypto_init(void);

/*! Closes an nvcrypto service session.
 *
 *
* \return OTE_SUCCESS to indicate that the operation was successful.
 *
 */
te_error_t ote_nvcrypto_deinit(void);

/*! Gets the key box provisioned in the EKS partition.
 *  A TA calls this function to import its keybox from the EKS partition.
 *
 *  \param [in]     keybox_lookup_index   The index of the keybox.
 *  \param [in]     lookup_type           The type of lookup requested.
 *  \param [in,out] buf                   A pointer to the key.
 *  \param [in,out] len                   A pointer to the length of the buffer
 *                                         in bytes.
 *
 * \retval OTE_SUCCESS           The operation was successful.
 * \retval OTE_BAD_STATE         The nvcrypto session was not open.
 * \retval OTE_ERROR_BAD_PARAMS  \a buf was empty.
 */
te_error_t ote_nvcrypto_get_keybox(uint32_t keybox_lookup_index,
keystore_lookup_type lookup_type, void *buf, uint32_t *len);

/*! Gets the size of the key box provisioned in the EKS partition.
 *
 *  \param [in]     keybox_lookup_index   The index of the keybox.
 *  \param [in]     lookup_type           The type of lookup requested.
 *  \param [out]    len                   A pointer to the size of the keybox
 *                                         in bytes.
 *
 * \retval OTE_SUCCESS           The operation was successful.
 * \retval OTE_BAD_STATE         The nvcrypto session was not open.
 * \retval OTE_ERROR_BAD_PARAMS  len was empty.
 */
te_error_t ote_nvcrypto_get_keybox_size(uint32_t keybox_lookup_index,
keystore_lookup_type lookup_type, uint32_t *len);

/*! \brief Gets the storage key.
 *
 * \retval OTE_SUCCESS  The operation was successful.
 *
 *  \param [in,out]    key         A pointer to the key.
 *  \param [in]        key_size    The length of the key in bytes.
 */
te_error_t ote_nvcrypto_get_storage_key(uint8_t *key, uint32_t key_size);

/*! \brief Gets the rollback key.
 *
 * \retval OTE_SUCCESS  The operation was successful.
 *
 *  \param [out]       key               A pointer to the key.
 *  \param [in]        key_size          The length of the key in bytes.
 */
te_error_t ote_nvcrypto_get_rollback_key(uint8_t *key, uint32_t key_size);

/*! \brief Gets the key derived from the root of trust.
 *
 *  \param [out]   key          A pointer to the key.
 *  \param [in]    key_size     The length of the key in bytes.
 * \retval OTE_SUCCESS          The operation was successful.
 */
te_error_t ote_nvcrypto_get_ro_trust_key(uint8_t *key, uint32_t key_size);

/* Size limit for HW RNG request */
#define MAX_HWRANDOM_SIZE	4096

/*! \brief Gets SE HW random number generated data.
 *
 *  \param [in,out] buf     A pointer to the result buffer.
 *  \param [in]     buf_len Number of bytes requested from nvcrypto, up to
 *                          the maximum size supported.
 */
te_error_t ote_nvcrypto_get_random(uint8_t *buf, uint32_t buf_len);

/*! \brief Gets the wv rsa sig key.
 *
 * \retval OTE_SUCCESS  The operation was successful.
 *
 *  \param [in,out] key         A pointer to the key.
 *  \param [in,out] key_size    A pointer to the length of the key in bytes.
 */
te_error_t ote_nvcrypto_get_wv_rsa_sig_key(uint8_t *key, uint32_t *key_size);
/*! \brief Gets the vrr auth key.
 * \retval OTE_SUCCESS  The operation was successful.
 *
 *  \param [in,out] key         A pointer to the key.
 *  \param [in,out] key_size    A pointer to the length of the key in bytes.
 */
te_error_t ote_nvcrypto_get_gs_key(uint8_t *key, uint32_t *key_size);

/*! \brief Gets the storage/rollback key. It helps in getting 2 key types
 * STORAGE_MASTER_KEY - used as a key to sign log, meta data files
 * ROLLBACK_KEY - used as a key to sign data written to RPMB device.
 *
 * \retval OTE_SUCCESS  The operation was successful.
 *
 *  \param [in,out]    key        A pointer to the key.
 *  \param [in]        key_size   The length of the key in bytes.
 *  \param [in]        key_type   The storage key operation. Supported values are:
                                  @li CRYPTO_SERVICE_GET_STORAGE_MASTER_KEY
                                  @li CRYPTO_SERVICE_GET_ROLLBACK_KEY
 * \retval OTE_SUCCESS Indicates the operation was successful.
 * \retval OTE_ERROR_BAD_STATE   The nvcrypto session was not open.
 * \retval OTE_ERROR_BAD_PARAMS  \a key was empty.
 */
te_error_t ote_nvcrypto_get_key(uint8_t *key, uint32_t key_size, uint32_t key_type);

/*! Generates a unique encryption key by performing crypto operations on the src_buffer
 * a predefined initial vector IV and a secure storage key.
 *
 *  \param [in]     src_buffer    A pointer to the input data buffer.
 *  \param [in]     src_buf_len   Length of input buffer, it must be a multiple of 16.
 *  \param [out]    dest          A  pointer to an output buffer.
 *                                The length of the \a dest buffer must match
 *                                the length of the \a src_buffer.
 *  \retval OTE_SUCCESS          The operation was successful.
 *  \retval OTE_ERROR_BAD_STATE  The nvcrypto session was not open.
 *  \retval OTE_ERROR_BAD_PARAMS  A \a dest or \a src_buffer was empty,
 *                                or \a src_buf_len was not a multiple of 16.
 */
te_error_t ote_nvcrypto_derive_key(const uint8_t *src_buffer,
					const uint32_t src_buf_len, uint8_t *dest);

/*! \brief Gets the Widevine Device Unique key.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *
 *  \param [out]       key               A pointer to the key.
 *  \param [in]        key_size          The length of the key in bytes.
 */
te_error_t ote_nvcrypto_get_widevine_key(uint8_t *key, uint32_t key_size);

/*! \brief Gets the Widevine Renewal key.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *
 *  \param [out]       key               A pointer to the key.
 *  \param [in]        key_size          The length of the key in bytes.
 *  \param [in]        x0                partition 0 of the obfuscated vector
 *  \param [in]        x0_size           The length of the x0
 *  \param [in]        x2                partition 2 of the obfuscated vector
 *  \param [in]        x2_size           The length of the x2 partition
 */
te_error_t ote_nvcrypto_get_widevine_renewal_key(uint8_t *key, uint32_t key_size,
		uint8_t *x0, uint32_t x0_size, uint8_t *x2, uint32_t x2_size);

/*! Updates the HW security engine AES/RSA keyslots with an input Key.
 *
 *  \param [in]      KeySlotType    Keyslot type, AES or RSA.
 *  \param [in]      KeySlotIdx     Index of the key to be updated.
 *  \param [in]      access_control Permissions to be set for this keyslot.
 *  \param [in]      pData          A pointer to the key data bytes.
 *  \param [in]      pData_len      Length of the key data.
 *  \retval OTE_SUCCESS The operation was successful.
 */
te_error_t ote_nvcrypto_update_se_keyslot(uint32_t KeySlotType, uint32_t KeySlotIdx,
					uint32_t access_control, const uint32_t *pData, uint32_t pData_len);

/*! Interface sign a data blob with a RSA key with no data padding. Calls the following
 * openssl API with padding = RSA_NO_PADDING.
 * All clients need to ensure their data is padded securely such that the datasize matches
 * the RSA exponent size.
 * https://www.openssl.org/docs/manmaster/crypto/RSA_private_encrypt.html
 *
 *  \param [in]     pri_key     Private RSA key in DER format with which to
 *                              sign the data.
 *  \param [in]     pri_key_len Length of the RSA key in bytes.
 *  \param [in]     data_in     Input buffer.
 *  \param [in]     data_in_len Length of the input buffer in bytes.
 *  \param [out]    signedData  Output buffer.
 *  \param [out]    signed_data_len Length of output buffer in bytes.
 *
 *  \retval OTE_SUCCESS The operation was successful
*/
te_error_t ote_nvcrypto_rsa_raw_private_encrypt(
			uint8_t *pri_key,uint32_t pri_key_len, uint32_t *data_in,
			uint32_t data_in_len, uint8_t* signedData, uint32_t signed_data_len);


/*! Interface for crypto operations such as AES, SHA, and RSA using openssl library.
 *
 *  \param [in]     algo        Algorithm to use (AES, SHA, or RSA).
 *  \param [in]     mode        Mode in which the algorithm is called (SHA1, SHA256, etc...).
 *  \param [in]     inbuf       Input buffer.
 *  \param [in]     inbuf_len   Length of input buffer in bytes.
 *  \param [in]     iv          Initial buffer (IV).
 *  \param [in]     iv_len      Length of initial buffer in bytes.
 *  \param [in]     key         Key buffer.
 *  \param [in]     key_len     Length of key buffer in bytes.
 *  \param [out]    outbuf      Output buffer.
 *  \param [in,out] outbuf_len  Length of output buffer in bytes.
 *
 *  \retval OTE_SUCCESS The operation was successful
*/
te_error_t ote_nvcrypto_do_crypt_functions(
			uint32_t algo, uint32_t mode,
			uint8_t *inbuf,uint32_t inbuf_len,
			uint8_t *iv,uint32_t iv_len,
			uint8_t *key,uint32_t key_len,
			uint8_t *outbuf,uint32_t *outbuf_len);

/*! \brief Gets the EKS2 mac key, which is used to validate integrity of the
 *  EKS2 blob coming from the non-secure world.
 *
 * \retval OTE_SUCCESS The operation was successful.
 *
 *  \param [out]       key               A pointer to the key.
 *  \param [out]       key_size          A pointer to the length of the key in bytes.
 */
te_error_t ote_nvcrypto_get_eks2_mac_key(uint8_t *key, uint32_t *key_size);

/*! \brief Installs EKS2 keys in NVCrypto key slots
 *
 *  \param [in]       buf               A pointer to the buffer with EKS2 keys.
 *  \param [in]       buf_len           Length of the buffer.
 *  \param [in]       num_keys          Number of keys in the buffer.
 *
 * \retval OTE_SUCCESS The operation was successful.
 */
te_error_t ote_nvcrypto_install_eks2_keys(const uint8_t *buf, uint32_t buf_len, uint32_t num_keys);

/*! \brief Gets the IP protection decryment key for client type: Dolby and Superres
 *
 *  \param [out]      buffer            A pointer to the buffer to store the key.
 *  \param [out]      buffer_size       A pointer to store the size of the buffer.
 *  \param [in]       client_type       client_type is either dolby or superres.
 *
 * \retval OTE_SUCCESS The operation was successful.
 */
te_error_t ote_nvcrypto_get_ipp_dec_key(uint8_t *buffer, uint32_t *buffer_size, const uint8_t client_type);

#endif
/** @} */
