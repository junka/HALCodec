/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "pkcs11_reference_apps_code.h"

#define CUSTOM_DATA_SIZE 16U
#define WRAPPED_KEY_DATA_LENGTH_2_KEYS ((2 * NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES) + CUSTOM_DATA_SIZE)

/**
 * CKM_AES_CBC wrap and unwrap key example
 * First generate a key for wrapping and unwrapping.
 * Then create a key to be wrapped.
 *
 * To wrap a key the following information is required:
 * - Wrapping mechanism consisting of:
 *  	1) The wrapping mechanism type (CKM_AES_CBC)
 *  	2) A pointer to a buffer to receive the internally generated IV
 *  	3) A CK_ULONG that contains the length of the IV buffer (16-Bytes)
 * - A key to perform the wrapping
 * - A key to be wrapped
 * - A pointer to a buffer to hold the wrapped key
 * - A pointer to a CK_ULONG containing the length of the wrapped key buffer.
 * 	 The buffer must be the same length as the key being wrapped
 *
 * When the wrap operation is performed the IV buffer will be populated with a randomly generated IV.
 * The same IV value will be required to successfully unwrap the key.
 *
 * To unwrap a key the following information is required:
 * - Unwrapping mechanism consisting of:
 *  	1) The wrapping mechanism type (CKM_AES_CBC)
 *  	2) A pointer to a buffer containing the IV
 *  	3) A CK_ULONG that contains the length of the IV buffer (16-Bytes)
 * - A key to perform the unwrapping
 * - A wrapped key
 * - The size of the wrapped key
 * - The wrapped key attributes
 *   (supplied as a regular CK_ATTRIBUTE array)
 * - The size of the attribute array
 * - A pointer to a handle for the unwrapped key
 *
 */
CK_RV cbc_iv_wrap_unwrap_reference_code(CK_SESSION_HANDLE hSession)
{
	CK_RV rv;
	CK_RV destroyRv;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;

	CK_OBJECT_HANDLE hWrapUnwrapKey = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE hKeyToBeWrapped = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE hUnwrappedKey = CK_INVALID_HANDLE;

	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;

	/* Generate a key to wrap and unwrap another key */
	CK_OBJECT_CLASS secretKeyClass = CKO_SECRET_KEY;
	CK_MECHANISM_TYPE wrapUnwrapAllowedMechanismType = CKM_AES_CBC;
	CK_MECHANISM mechanismGenKey = {CKM_AES_KEY_GEN, NULL, 0};
	CK_ULONG ulValueLen = NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES;
	CK_BYTE wrapUnwrapKeyId[NVPKCS11_MAX_KEY_ID_SIZE] = "wrapUnwrapCbcIvExampleKeyId     ";

	CK_ATTRIBUTE wrapUnwrapKeyTemplate[] = {
		{CKA_CLASS, &secretKeyClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_VALUE_LEN, &ulValueLen, sizeof(CK_ULONG)},
		{CKA_ID, wrapUnwrapKeyId, sizeof(wrapUnwrapKeyId)},
		{CKA_WRAP, &enabled, sizeof(CK_BBOOL)},
		{CKA_UNWRAP, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &wrapUnwrapAllowedMechanismType, sizeof(wrapUnwrapAllowedMechanismType)},
	};

	rv = C_GenerateKey(hSession, &mechanismGenKey, wrapUnwrapKeyTemplate, sizeof(wrapUnwrapKeyTemplate)/sizeof(CK_ATTRIBUTE), &hWrapUnwrapKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateKey", end_cbc_iv_wrap_unwrap_reference_code);

	/* Generate a key to be wrapped */
	CK_BYTE keyToBeWrappedUnwrappedId[NVPKCS11_MAX_KEY_ID_SIZE] = "keyToBeWrapUnwrappedCbcIvId     ";
	CK_MECHANISM_TYPE keyToBeWrapUnwrappedAllowedMechType = CKM_AES_CMAC;

	CK_ATTRIBUTE keyToBeWrappedTemplate[] = {
		{CKA_CLASS, &secretKeyClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_VALUE_LEN, &ulValueLen, sizeof(CK_ULONG)},
		{CKA_ID, keyToBeWrappedUnwrappedId, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &keyToBeWrapUnwrappedAllowedMechType, sizeof(keyToBeWrapUnwrappedAllowedMechType)},
		/* Note: CKA_EXTRACTABLE attribute must be set to allow this key to be wrapped */
		{CKA_EXTRACTABLE, &enabled, sizeof(CK_BBOOL)},
	};

	rv = C_GenerateKey(hSession, &mechanismGenKey, keyToBeWrappedTemplate, sizeof(keyToBeWrappedTemplate)/sizeof(CK_ATTRIBUTE), &hKeyToBeWrapped);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateKey", destroy_hWrapUnwrapKey);

	/* Sign data with the key to be wrapped */
	CK_BYTE inputData[NVPKCS11_AES_CBC_BLOCK_SIZE] = "inputData";
	CK_BYTE signature[NVPKCS11_AES_CMAC_SIGNATURE_SIZE] = {0};
	CK_ULONG ulSignatureLen = NVPKCS11_AES_CMAC_SIGNATURE_SIZE;
	CK_MECHANISM signVerifyMechanism = {CKM_AES_CMAC, NULL, 0};

	rv = C_SignInit(hSession, &signVerifyMechanism, hKeyToBeWrapped);
	GO_TO_LABEL_ON_ERROR(rv, "C_SignInit", destroy_hKeyToBeWrapped);

	rv = C_Sign(hSession, inputData, sizeof(inputData), signature, &ulSignatureLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_Sign", destroy_hKeyToBeWrapped);

	/* Wrap the signing key - note the IV is part of the wrapping mechanism and will be overwritten by the C_WrapKey API */
	CK_BYTE iv[NVPKCS11_AES_CBC_IV_LEN] = {0};

	CK_MECHANISM wrapUnwrapMechanism = {CKM_AES_CBC, iv, NVPKCS11_AES_CBC_IV_LEN};
	CK_BYTE encryptedKey[NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES] = {0};
	CK_ULONG ulKeySize = sizeof(encryptedKey);

	printf("\n====================================================\n");
	printf("CBC IV WRAP/UNWRAP:\n");
	printf("IV before wrapping:\n");
	print_data(iv, NVPKCS11_AES_CBC_IV_LEN);

	rv = C_WrapKey(hSession, &wrapUnwrapMechanism, hWrapUnwrapKey, hKeyToBeWrapped, encryptedKey, &ulKeySize);
	GO_TO_LABEL_ON_ERROR(rv, "C_WrapKey", destroy_hKeyToBeWrapped);

	printf("\nIV returned after wrapping:\n");
	print_data(iv, NVPKCS11_AES_CBC_IV_LEN);

	/* Delete hKeyToBeWrapped so the CKA_ID can be reused */
	rv = C_DestroyObject(hSession, hKeyToBeWrapped);
	GO_TO_LABEL_ON_ERROR(rv, "C_DestroyObject - hKeyToBeWrapped", destroy_hWrapUnwrapKey);
	hKeyToBeWrapped = CK_INVALID_HANDLE;

	CK_KEY_TYPE aesKeyType = CKK_AES;
	CK_ATTRIBUTE unwrappedKeyTemplate[] = {
		{CKA_CLASS, &secretKeyClass, sizeof(CK_OBJECT_CLASS)},
		/* Note: CKA_KEY_TYPE is needed for the C_UnwrapKey template */
		{CKA_KEY_TYPE, &aesKeyType, sizeof(CK_KEY_TYPE)},
		{CKA_VALUE_LEN, &ulValueLen, sizeof(CK_ULONG)},
		{CKA_ID, keyToBeWrappedUnwrappedId, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &keyToBeWrapUnwrappedAllowedMechType, sizeof(keyToBeWrapUnwrappedAllowedMechType)},
	};

	/* Unwrap the key
	 * Note: Attributes to be applied to the unwrapped key must be supplied within an attribute template.
	 * The same template used to generate the key to be wrapped will be reused here for the unwrapped key
	 */
	rv = C_UnwrapKey(hSession, &wrapUnwrapMechanism, hWrapUnwrapKey, encryptedKey, ulKeySize, unwrappedKeyTemplate, sizeof(unwrappedKeyTemplate)/sizeof(CK_ATTRIBUTE), &hUnwrappedKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_UnwrapKey", destroy_hWrapUnwrapKey);

	/* Verify the signature with the unwrapped key */
	rv = C_VerifyInit(hSession, &signVerifyMechanism, hUnwrappedKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_VerifyInit", destroy_hUnwrappedKey);

	rv = C_Verify(hSession, inputData, sizeof(inputData), signature, ulSignatureLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_Verify", destroy_hUnwrappedKey);

destroy_hUnwrappedKey:
	destroyRv = C_DestroyObject(hSession, hUnwrappedKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hUnwrappedKey", failedFlag);
destroy_hKeyToBeWrapped:
	if (hKeyToBeWrapped != CK_INVALID_HANDLE)
	{
		destroyRv = C_DestroyObject(hSession, hKeyToBeWrapped);
		PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hKeyToBeWrapped", failedFlag);
	}
destroy_hWrapUnwrapKey:
	destroyRv = C_DestroyObject(hSession, hWrapUnwrapKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hWrapUnwrapKey", failedFlag);
end_cbc_iv_wrap_unwrap_reference_code:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}
	return rv;
}

/**
 * CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP example:
 * Generate a key for wrapping.
 * Create two keys to be wrapped. The leading key and the trailing key.
 * Set the wrapping parameters and perform the wrap operation.
 *
 * The CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP mechanism type must be used
 * with a CK_NVIDIA_AES_CBC_KEY_DATA_WRAP_PARAMS mechanism parameter to wrap either
 * one secret key, or a pair of secret keys with custom data interleaved between
 * the two.
 *
 * If hTrailingKey is CK_INVALID_HANDLE, the mechanism wraps a single key (data=[hKey])
 * using AES in CBC mode.
 *
 * If hTrailingKey is a valid handle, the mechanism wraps two keys with custom data
 * interleaved between them (data=[hKey|pData|hTrailingKey]) using AES in CBC mode.
 *
 * The mechanism generates a random IV that is returned to the caller
 * in the iv field of the mechanism parameter.
 *
 * The wrapping mechanism consists of:
 * - The wrapping mechanism type (CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP)
 * - The wrapping parameters (CK_NVIDIA_AES_CBC_KEY_DATA_WRAP_PARAMS)
 * - The size of the wrapping parameters
 *
 * CK_NVIDIA_AES_CBC_KEY_DATA_WRAP_PARAMS consists of:
 * - pData - Custom data pointer. Must be NULL if hTrailingKey is CK_INVALID_HANDLE.
 * - ulLen - Custom data length in bytes. Must be a multiple of 16. Must be 0 if hTrailingKey is CK_INVALID_HANDLE.
 * - hTrailingKey - Handle to the second key to be wrapped, or CK_INVALID_HANDLE
 * - iv - Buffer to be overwritten with the IV generated for CBC mode (16 bytes)
 */
CK_RV nvidia_aes_cbc_key_data_wrap_reference_code(CK_SESSION_HANDLE hSession)
{
	CK_RV rv;
	CK_RV destroyRv;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;

	CK_OBJECT_HANDLE hWrappingKey = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE hLeadingKey = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE hTrailingKey = CK_INVALID_HANDLE;

	/* Generate a wrapping key to wrap the keys with */
	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;
	CK_OBJECT_CLASS secretKeyClass = CKO_SECRET_KEY;
	CK_MECHANISM_TYPE wrapAllowedMechanismType = CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP;
	CK_BYTE wrappingKeyId[NVPKCS11_MAX_KEY_ID_SIZE] = "WrappingKeyCbcDataWrapId        ";
	CK_ULONG ulKeyValueLen = NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES;
	CK_MECHANISM mechanismGenKey = {CKM_AES_KEY_GEN, NULL, 0};

	printf("\n====================================================\n");
	printf("NVIDIA CBC DATA WRAP:\n");

	CK_ATTRIBUTE wrapKeyTemplate[] = {
		{CKA_CLASS, &secretKeyClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_VALUE_LEN, &ulKeyValueLen, sizeof(CK_ULONG)},
		{CKA_ID, wrappingKeyId, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_WRAP, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &wrapAllowedMechanismType, sizeof(wrapAllowedMechanismType)},
	};

	rv = C_GenerateKey(hSession, &mechanismGenKey, wrapKeyTemplate, sizeof(wrapKeyTemplate)/sizeof(CK_ATTRIBUTE), &hWrappingKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateKey - hWrappingKey", end_nvidia_aes_cbc_key_data_wrap_reference_code);

	/* Generate a leading key to be wrapped */
	CK_BYTE leadingKeyId[NVPKCS11_MAX_KEY_ID_SIZE] = "leadingKeyCbcDataWrapId         ";
	CK_MECHANISM_TYPE allowedMechType = CKM_AES_CMAC;

	CK_ATTRIBUTE leadingKeyTemplate[] = {
		{CKA_CLASS, &secretKeyClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_VALUE_LEN, &ulKeyValueLen, sizeof(CK_ULONG)},
		{CKA_ID, leadingKeyId, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &allowedMechType, sizeof(allowedMechType)},
		/* Note: the extractable attribute must be set to allow this key to be wrapped */
		{CKA_EXTRACTABLE, &enabled, sizeof(CK_BBOOL)},
	};

	rv = C_GenerateKey(hSession, &mechanismGenKey, leadingKeyTemplate, sizeof(leadingKeyTemplate)/sizeof(CK_ATTRIBUTE), &hLeadingKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateKey - hLeadingKey", destroy_hWrappingKey);

	/* Generate a trailing key to be wrapped */
	CK_BYTE trailingKeyId[NVPKCS11_MAX_KEY_ID_SIZE] = "trailingKeyCbcDataWrapId        ";

	CK_ATTRIBUTE trailingKeyTemplate[] = {
		{CKA_CLASS, &secretKeyClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_VALUE_LEN, &ulKeyValueLen, sizeof(CK_ULONG)},
		{CKA_ID, trailingKeyId, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &allowedMechType, sizeof(allowedMechType)},
		/* Note: the extractable attribute must be set to allow this key to be wrapped */
		{CKA_EXTRACTABLE, &enabled, sizeof(CK_BBOOL)},
	};
	rv = C_GenerateKey(hSession, &mechanismGenKey, trailingKeyTemplate, sizeof(trailingKeyTemplate)/sizeof(CK_ATTRIBUTE), &hTrailingKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateKey - hTrailingKey", destroy_hLeadingKey);

	/* Wrap leading and trailing key */
	CK_BYTE customData[CUSTOM_DATA_SIZE];
	/* Generate a random value for the customData */
	rv = C_GenerateRandom(hSession, customData, CUSTOM_DATA_SIZE);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", destroy_hTrailingKey);

	CK_NVIDIA_AES_CBC_KEY_DATA_WRAP_PARAMS wrapParams;
	wrapParams.pData = customData;
	wrapParams.ulLen = CUSTOM_DATA_SIZE;
	wrapParams.hTrailingKey = hTrailingKey;
	memset(wrapParams.iv, 0x00, NVPKCS11_AES_CBC_IV_LEN);

	CK_MECHANISM wrapMechanism = {CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP, &wrapParams, sizeof(CK_NVIDIA_AES_CBC_KEY_DATA_WRAP_PARAMS)};
	CK_ULONG wrappedKeysLen = WRAPPED_KEY_DATA_LENGTH_2_KEYS;
	CK_BYTE wrappedKeys[WRAPPED_KEY_DATA_LENGTH_2_KEYS];

	rv = C_WrapKey(hSession, &wrapMechanism, hWrappingKey, hLeadingKey, wrappedKeys, &wrappedKeysLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_WrapKey", destroy_hTrailingKey);

	printf("Wrapped data:\n");
	print_data(wrappedKeys, wrappedKeysLen);

destroy_hTrailingKey:
	destroyRv = C_DestroyObject(hSession, hTrailingKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hTrailingKey", failedFlag);
destroy_hLeadingKey:
	destroyRv = C_DestroyObject(hSession, hLeadingKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hLeadingKey", failedFlag);
destroy_hWrappingKey:
	destroyRv = C_DestroyObject(hSession, hWrappingKey);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hWrappingKey", failedFlag);

end_nvidia_aes_cbc_key_data_wrap_reference_code:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}
	return rv;
}