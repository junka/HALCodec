/*
 * Copyright (c) 2020-2024, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 *
 */

#ifndef PKCS11_REFERENCE_APPS_CODE_H
#define PKCS11_REFERENCE_APPS_CODE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "nvpkcs11.h"
#include "nvpkcs11_public_defs.h"
#include "pkcs11_error_handling.h"

#define KB 1024LU
#define MB (1024LU * KB)

#define CK_SP800_108_COUNTER_FORMAT_WIDTH_IN_BITS 32UL
#define CK_SP800_REQUIRED_LENGTH_FORMAT_WIDTH 32U
#define CK_SP800_MAX_LABEL_SIZE 32U
#define CK_SP800_MAX_CONTEXT_SIZE 32U

#define GCM_PARAMS_TAG_BITS 128U
#define GCM_PARAMS_TAG_BYTES 16U
#define GCM_PARAMS_IV_BYTES 12U

#define REFERENCE_UNWRAP_AAD_SIZE 128U
#define REFERENCE_UNWRAP_IV_SIZE 12U
#define REFERENCE_UNWRAP_KEYTAG_SIZE 64U


/**
 * Reference code for performing single and multi
 * part digest operations:
 *
 * @param [in] hSession Session handle
 * @param [in] in_data_ptr Pointer to the message data
 * @param [in] data_len Length of the message data
 * @param [in] digest_max_data_len Maximum input buffer size allowed for single part digest (channel dependent)
 */
CK_RV digest_reference_code(CK_SESSION_HANDLE hSession, CK_BYTE_PTR in_data_ptr, CK_ULONG data_len, CK_ULONG digest_max_data_len);

/**
 * Reference code for performing ed25519 verify operations
 *
 * @param [in] hSession Session handle
 * @param [in] mechanism_verify_ptr A structure that specifies a particular mechanism
 * @param [in] eddsa_key_handle Handle to the ed25519 key object
 * @param [in] data_ptr Input message data
 * @param [in] data_len Length of input data
 * @param [in] signature_ptr Pointer to the signature o be verified
 * @param [in] signature_len The length of the signature to be verified
 */
CK_RV eddsa_verify_operation(
		CK_SESSION_HANDLE hSession,
		CK_MECHANISM_PTR mechanism_verify_ptr,
		CK_OBJECT_HANDLE eddsa_key_handle,
		CK_BYTE_PTR data_ptr,
		CK_ULONG data_len,
		CK_BYTE_PTR signature_ptr,
		CK_ULONG signature_len);

/**
 * Reference code for creating a public ed25519 key and performing verify operations
 *
 * @param [in] hSession Session handle
 * @param [in] data_ptr Input message data
 * @param [in] data_len Length of input data
 * @param [in] signature_ptr Pointer to the signature to be verified
 * @param [in] signature_len The length of the signature to be verified
 */
CK_RV eddsa_verify_reference_code(
		CK_SESSION_HANDLE hSession,
		CK_BYTE_PTR data_ptr,
		CK_ULONG data_len,
		CK_BYTE_PTR signature_ptr,
		CK_ULONG signature_len);

/**
 * Reference code which invokes a list of APIs to
 * perform object find then key derivation.
 * @param [in] hSession Session handle.
 * @param [in, out] derived_key_handle_ptr Pointer to the derived key.
 * @param [in] input_base_string_ptr Pointer to the fused base key string
 * @param [in] input_base_string_len Length of the fused base key string
 * @param [in] key_id_input_string_ptr Pointer to the ID string
 * @param [in] key_id_input_string_len Length of the ID string
 * @param [in] key_derivation_label_string Pointer to the key derivation label string
 * @param [in] key_derivation_label_string_len Length of the key derivation label string
 * @param [in] key_derivation_context_string Pointer to the key derivation context string
 * @param [in] key_derivation_context_string_len Length of the key derivation context string
 * @param [in] key_purpose purpose of the derived key
 */
CK_RV find_object_derive_key(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE_PTR derived_key_handle_ptr,
		char * input_base_string_ptr,
		CK_ULONG input_base_string_len,
		char * key_id_input_string_ptr,
		CK_ULONG key_id_input_string_len,
		char *  key_derivation_label_string,
		CK_ULONG key_derivation_label_string_len,
		char *  key_derivation_context_string,
		CK_ULONG key_derivation_context_string_len,
		CK_BBOOL use_unwrap);

/**
 * Reference code for ed25519 public key object create.
 * @param [in] hSession Session handle
 * @param [in, out] eddsa_key_handle_ptr Pointer to the received public key object handle created.
 * @param [in] cka_ecid_string_ptr Pointer to the ID string.
 * @param [in] cka_ecid_string_len Length of the ID string.
 */
CK_RV eddsa_key_object_create(
		CK_SESSION_HANDLE hSession,
		CK_OBJECT_HANDLE_PTR eddsa_key_handle_ptr,
		char * cka_ecid_string_ptr,
		CK_ULONG cka_ecid_string_len);

void print_data(CK_BYTE_PTR data, CK_ULONG size);

/**
 * Reference code to read wrapped key data.
 * This data is read from the output files from the key wrapping script,
 * which needs to be copied to the same location of the calling application before this code is run.
 * @param [in] aad_ptr Pointer to Additional Authenticated Data
 * @param [in, out] aadSize_ptr Pointer to size of Additional Authenticated Data to read from file, and return how much was read.
 * @param [in] iv_ptr Pointer to iv byte array
 * @param [in, out] ivSize_ptr Pointer to size of iv to read from file, and return how much was read.
 * @param [in] wrappedKeyAndMac_ptr Pointer to key and MAC byte array
 * @param [in, out] wrappedKeyAndMacSize_ptr Pointer to size of key and MAC to read from file, and return how much was read.
 * @param [in] labelSize_ptr Pointer to key derivation label byte array
 * @param [in, out] labelSize_ptr Pointer to size of key derivation label to read from file, and return how much was read.
 * @param [in] derivationContext_ptr Pointer to key derivation context byte array
 * @param [in, out] contextSize_ptr Pointer to size of key context to read from file, and return how much was read.
 * @param [in] exampleKey_ptr Pointer to example key value byte array.
 * @param [in, out] exampleKeySize_ptr Pointer to size of example key, and return how much was read.
 * @param [out] useExampleKey_ptr Boolean value to indicate if the example key value should be used.
 */
CK_RV pkcs11_read_unwrap_data(CK_BYTE_PTR aad_ptr,
			CK_ULONG_PTR aadSize_ptr,
			CK_BYTE_PTR iv_ptr,
			CK_ULONG_PTR ivSize_ptr,
			CK_BYTE_PTR wrappedKeyAndMac_ptr,
			CK_ULONG_PTR wrappedKeyAndMacSize_ptr,
			CK_BYTE_PTR derivationLabel_ptr,
			CK_ULONG_PTR labelSize_ptr,
			CK_BYTE_PTR derivationContext_ptr,
			CK_ULONG_PTR contextSize_ptr,
			CK_BYTE_PTR exampleKey_ptr,
			CK_ULONG_PTR exampleKeySize_ptr,
			CK_BBOOL *useExampleKey_ptr);

/**
 * Reference code for performing key unwrap using CKM_AES_GCM.
 *
 * @param [in] hSession Session handle
 */
CK_RV gcm_unwrap_reference_code(CK_SESSION_HANDLE hSession);

/**
 * Reference code for performing key wrapping and unwrapping using CKM_AES_CBC.
 * This function demonstrates how the IV is returned during the key wrap
 * and is then used to perform a successful unwrap operation.
 *
 * @param [in] hSession Session handle
 */
CK_RV cbc_iv_wrap_unwrap_reference_code(CK_SESSION_HANDLE hSession);

/**
 * Reference code for committing token objects to secure storage using the extension API C_NVIDIA_CommitTokenObjects.
 *
 * Each token (representing a combination of secure storage allocation and access to cryptographic hardware) has a
 * dynamic token view and a safety token view. The dynamic token allows for token objects to be added, updated, and
 * deleted, and once added can be used immediately. The safety token has a static view of the content of the secure
 * storage as it was at boot time - token objects can be accessed, but not altered, added, or deleted in this view.
 *
 * This example demonstrates how to add and remove token objects in the safety token by:
 * - Making changes in a read/write session on the dynamic token
 * - Calling C_NVIDIA_CommitTokenObjects() with no PKCS#11 sessions open on any safety token (this is to prevent
 *   safety-critical operations stalling as the commit happens)
 * - Either reboot, or go through an SC7 cycle (calling C_Finalize() before suspending and C_Initialize() after resuming)
 *
 * Exercise caution if enabling this example code, as token objects may become lost or forgotten on the system if they
 * are not deleted when no longer required. The example code will check for any pre-existing keys with the same CKA_ID.
 * If the board does not have secure storage capabilities it will fail.
 *
 * To enable this test, set the TOKEN_OBJECT_EXAMPLE flag in Makefile.tmk
 *
 * @param [in] hSession Session handle
 */
CK_RV token_object_reference_code(CK_SESSION_HANDLE hSession);

/**
 * Reference code to encrypt and decrypt data using CKM_AES_GCM.
 * Test includes extracting IV data using the C_NVIDIA_EncryptGetIV function as well as detailing how TAG data is handled
 * @param [in] hSession Session handle
 */
CK_RV gcm_encrypt_decrypt(CK_SESSION_HANDLE hSession);

/**
 * Reference code to derive a CKM_AES_GCM encrypt and decrypt key for use with the TLS protocol.
 * Test includes CKA_NVIDIA_CALLER_NONCE example that allows the user to set their own IV
 * @param [in] hSession Session handle
 */
CK_RV tls_gcm_reference_code(CK_SESSION_HANDLE hSession);

/**
 * Reference code to sign and verify data using CKM_SHA256_HMAC.
 * Includes an example of deriving a key using CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF.
 * @param [in] hSession Session handle
 * @param [in] in_data_ptr Pointer to the message data
 * @param [in] data_len Length of the message data
 */
CK_RV hmac_reference_code(CK_SESSION_HANDLE hSession, CK_BYTE_PTR in_data_ptr, CK_ULONG data_len);

/**
 * Reference code to sign and verify data using CKM_AES_GMAC.
 * The data to be signed is generated randomly. The size of the data can be determined by the 'data_len' parameter.
 * @param [in] hSession Session handle
 * @param [in] data_len Length of the message data
 */
CK_RV gmac_reference_code(CK_SESSION_HANDLE hSession, CK_ULONG data_len);

/**
 * Reference code to wrap keys and data using CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP
 * @param [in] hSession Session handle
 */
CK_RV nvidia_aes_cbc_key_data_wrap_reference_code(CK_SESSION_HANDLE hSession);

/**
 * Reference code to generate an ECDSA ephemeral key pair using CKM_EC_KEY_PAIR_GEN then use them to sign and verify test data
 * @param [in] hSession Session handle
 */
CK_RV ecdsa_ephemeral_sign_verify_reference_code(CK_SESSION_HANDLE hSession);

/**
 * Reference code to create a public key from plain text (pre-generated by OpenSSL), and verify signature of a simple string's digest
 * @param [in] hSession Session handle
 */
CK_RV verify_ecdsa_openssl_reference_code(CK_SESSION_HANDLE hSession);

#endif /* PKCS11_REFERENCE_APPS_CODE_H */
