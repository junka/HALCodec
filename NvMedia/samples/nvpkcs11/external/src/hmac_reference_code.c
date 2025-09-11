/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 *
 */

#include "pkcs11_reference_apps_code.h"

/**
 * Perform a single part SHA256 HMAC sign and verify operation.
 * Requirements to perform SHA256 HMAC sign and/or verify:
 * - A 256-bit key with the sign and/or verify attribute(s) set and
 *   an allowed mechanism of CKM_SHA256_HMAC
 * - A mechanism structure specifying the use of CKM_SHA256_HMAC for the
 *   sign or verify operation
 * - Input data greater than a minimum of one, maximum data varies per channel
 *   refer to the PCT configuration
 *
 * This reference code includes an example of using CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF to derive
 * a CKM_SHA256_HMAC key. CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF uses two step key derivation as
 * described in NISTSP800-56CREV.1: first extract randomness from the base key and the salt, then
 * expand it in counter mode with an info string.
 * Note a CKM_SHA256_HMAC key can be obtained through all normal methods, not just through
 * CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF.
 *
*/
CK_RV hmac_reference_code(CK_SESSION_HANDLE hSession, CK_BYTE_PTR in_data_ptr, CK_ULONG data_len)
{
	CK_RV rv;
	CK_RV destroyRv = CKR_OK;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;

	/* Create a base key to derive from */
	CK_OBJECT_HANDLE hBaseKey = CK_INVALID_HANDLE;
	CK_BYTE baseObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Created base key for derivation ";
	CK_OBJECT_CLASS objClass = CKO_SECRET_KEY;
	CK_KEY_TYPE keyType = CKK_GENERIC_SECRET;
	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;
	CK_BYTE baseObjectValue[NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES] = {0};
	/* Set the derivation mechanism */
	CK_MECHANISM_TYPE createAllowedMechanismType = CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF;

	printf("\n====================================================\n");
	printf("SHA256 HMAC:\n");

	/* Generate a random value to use as the base key value */
	rv = C_GenerateRandom(hSession, baseObjectValue, sizeof(baseObjectValue));
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", hmac_reference_code_exit);

	CK_ATTRIBUTE createObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &keyType, sizeof(CK_KEY_TYPE)},
		{CKA_ID, baseObjectId, sizeof(baseObjectId)},
		{CKA_DERIVE, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE, baseObjectValue, sizeof(baseObjectValue)},
		{CKA_ALLOWED_MECHANISMS, &createAllowedMechanismType, sizeof(createAllowedMechanismType)},
	};

	rv = C_CreateObject(hSession, createObjTemplate, sizeof(createObjTemplate)/sizeof(CK_ATTRIBUTE), &hBaseKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_CreateObject", hmac_reference_code_exit);

	/* Derive a CKM_SHA256_HMAC key using CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF */
	/* Set up CK_NVIDIA_SP800_56C_TWO_STEPS_KDF_PARAMS */
	CK_BYTE salt[NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES] = {0x00U};
	CK_BYTE info[] = "info value";
	CK_NVIDIA_SP800_56C_TWO_STEPS_KDF_PARAMS sp800_56cKdfParams = {
		.prfType = CKM_AES_CMAC,
		.pSalt = salt,
		.ulSaltLen = NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES,
		.pInfo = info,
		.ulInfoLen = strlen((char *)info),
		.ctr = 1,
	};

	/* The mechanism must contain the derivation mechanism, the parameters and the size of the parameters */
	CK_MECHANISM mechanism;
	mechanism.mechanism = CKM_NVIDIA_SP800_56C_TWO_STEPS_KDF;
	mechanism.pParameter = &sp800_56cKdfParams;
	mechanism.ulParameterLen = sizeof(sp800_56cKdfParams);

	/* Create a template for the derived CKM_SHA256_HMAC key */
	CK_BYTE hmacObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Derived key for HMAC sign/verify";
	CK_OBJECT_HANDLE hHmacKey = CK_INVALID_HANDLE;
	CK_MECHANISM_TYPE hmacAllowedMechanismType = CKM_SHA256_HMAC;
 	/* SHA256 HMAC key must be 256 bits */
	CK_ULONG bytesLen = NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES;

	CK_ATTRIBUTE hmacObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &keyType, sizeof(CK_KEY_TYPE)},
		{CKA_ID, hmacObjectId, sizeof(hmacObjectId)},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE_LEN, &bytesLen, sizeof(bytesLen)},
		{CKA_ALLOWED_MECHANISMS, &hmacAllowedMechanismType, sizeof(hmacAllowedMechanismType)},
	};

	rv = C_DeriveKey(hSession, &mechanism, hBaseKey, hmacObjTemplate, sizeof(hmacObjTemplate)/sizeof(CK_ATTRIBUTE), &hHmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_DeriveKey", hBaseKey_destroy);

	/* Perform SHA256 HMAC sign/verify */
	CK_MECHANISM mechanismSignVerify = { CKM_SHA256_HMAC, NULL, 0 };
	CK_BYTE signature[NVPKCS11_SHA256_DIGEST_SIZE] = {0};
	CK_ULONG ulSignatureLen = NVPKCS11_SHA256_DIGEST_SIZE;

	rv = C_SignInit(hSession, &mechanismSignVerify, hHmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_SignInit", hHmacKey_destroy);

	rv = C_Sign(hSession, in_data_ptr, data_len, signature, &ulSignatureLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_Sign", hHmacKey_destroy);

	rv = C_VerifyInit(hSession, &mechanismSignVerify, hHmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_VerifyInit", hHmacKey_destroy);

	rv = C_Verify(hSession, in_data_ptr, data_len, signature, ulSignatureLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_Verify", hHmacKey_destroy);

	printf("\nComputed signature using SHA256 HMAC:\n");
	print_data(signature, ulSignatureLen);

hHmacKey_destroy:
	destroyRv = C_DestroyObject(hSession, hHmacKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject hHmacKey", failedFlag);
hBaseKey_destroy:
	destroyRv = C_DestroyObject(hSession, hBaseKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject hBaseKey", failedFlag);

hmac_reference_code_exit:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}