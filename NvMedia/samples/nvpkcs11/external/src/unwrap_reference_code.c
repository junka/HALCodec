/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */



#include "pkcs11_reference_apps_code.h"
#include <unistd.h>

#define EXAMPLE_KEY_NAME "Example_dev_only_key.bin"
#define AAD_FILE_NAME "Test_Sign_key.aad"
#define IV_FILE_NAME "Test_Sign_key.iv"
#define KEYTAG_FILE_NAME "Test_Sign_key.keytag"
#define LABEL_FILE_NAME "Test_Sign_key.label"
#define CONTEXT_FILE_NAME "Test_Sign_key.ctxt"

static CK_BBOOL is_file_available(CK_CHAR_PTR fileName)
{
	CK_BBOOL isFound = (CK_BBOOL)CK_FALSE;
	if (access((const char*)fileName, F_OK) == 0)
	{
		isFound = (CK_BBOOL)CK_TRUE;
	}
	return isFound;
}

static CK_RV read_data(CK_BYTE_PTR data, CK_ULONG_PTR size, CK_CHAR_PTR fileName)
{
	FILE *fp = fopen((const char*)fileName, "rb");
	CK_RV rv = CKR_ARGUMENTS_BAD;

	if (fp != NULL)
	{
		CK_LONG readData = fread(data, sizeof(CK_BYTE), *size, fp);
		if (readData > 0)
		{
			*size = (CK_ULONG)readData;
			rv = CKR_OK;
		}
		else
		{
			printf("Failed to read all data from file: %s\n", fileName);
		}
		fclose(fp);
	}
	else
	{
		printf("Failed to open file: %s\n", fileName);
	}

	return (rv);
}

void print_data(CK_BYTE_PTR data, CK_ULONG size)
{
	for (CK_ULONG i = 0U; i < size; i++)
	{
		if ((i != 0U) && ((i % 8U) == 0U))
		{
			printf("\n");
		}
		printf("0x%02X ", data[i]);
	}
	printf("\n");
}

/**
 * Function to read all required data from wrapped key script output
 * and return to the caller
 **/

CK_RV pkcs11_read_unwrap_data(CK_BYTE_PTR aad_ptr, CK_ULONG_PTR aadSize_ptr,
			CK_BYTE_PTR iv_ptr, CK_ULONG_PTR ivSize_ptr,
			CK_BYTE_PTR wrappedKeyAndMac_ptr, CK_ULONG_PTR wrappedKeyAndMacSize_ptr,
			CK_BYTE_PTR derivationLabel_ptr, CK_ULONG_PTR labelSize_ptr,
			CK_BYTE_PTR derivationContext_ptr, CK_ULONG_PTR contextSize_ptr,
			CK_BYTE_PTR exampleKey_ptr, CK_ULONG_PTR exampleKeySize_ptr, CK_BBOOL *useExampleKey_ptr)
{
	CK_RV rv = CKR_OK;
	CK_BBOOL errorDetected = (CK_BBOOL)CK_FALSE;

	/* Check if example key is available on the file system or not */
	*useExampleKey_ptr = is_file_available((CK_CHAR_PTR)EXAMPLE_KEY_NAME);

	if (*useExampleKey_ptr == (CK_BBOOL)CK_TRUE)
	{
		rv = read_data(exampleKey_ptr, exampleKeySize_ptr, (CK_CHAR_PTR)EXAMPLE_KEY_NAME);
		if (rv != CKR_OK)
		{
			errorDetected = (CK_BBOOL)CK_TRUE;
		}
		else
		{
			printf("\n**************************************************************\n");
			printf("USING EXAMPLE KEY INSTEAD OF NV_OEM_KEY1 KEY FOR UNWRAP TEST\n");
			printf("**************************************************************\n\n");
		}
	}
	else
	{
		printf("\n***************************************\n");
		printf("USING NV_OEM_KEY1 KEY FOR UNWRAP TEST\n");
		printf("***************************************\n\n");
	}

	rv = read_data(aad_ptr, aadSize_ptr, (CK_CHAR_PTR)AAD_FILE_NAME);
	if (rv != CKR_OK)
	{
		errorDetected = (CK_BBOOL)CK_TRUE;
	}

	rv = read_data(iv_ptr, ivSize_ptr, (CK_CHAR_PTR)IV_FILE_NAME);
	if (rv != CKR_OK)
	{
		errorDetected = (CK_BBOOL)CK_TRUE;
	}

	rv = read_data(wrappedKeyAndMac_ptr, wrappedKeyAndMacSize_ptr, (CK_CHAR_PTR)KEYTAG_FILE_NAME);
	if (rv != CKR_OK)
	{
		errorDetected = (CK_BBOOL)CK_TRUE;
	}

	rv = read_data(derivationLabel_ptr, labelSize_ptr, (CK_CHAR_PTR)LABEL_FILE_NAME);
	if (rv != CKR_OK)
	{
		errorDetected = (CK_BBOOL)CK_TRUE;
	}

	rv = read_data(derivationContext_ptr, contextSize_ptr, (CK_CHAR_PTR)CONTEXT_FILE_NAME);
	if (rv != CKR_OK)
	{
		errorDetected = (CK_BBOOL)CK_TRUE;
	}

	if (errorDetected == (CK_BBOOL)CK_TRUE)
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}

/**
 * Unwrap a wrapped key blob using the CKM_NVIDIA_AES_GCM_KEY_UNWRAP mechanism.
 *
 * To unwrap a key the following information is required:
 * - The unwrapping key
 *   (locally derived using the same derivation string as the offline wrapping key)
 * - The wrapped key blob (including the GCM-generated TAG/MAC)
 * - The wrapped key metadata (supplied as AAD: Additional Authenticated Data)
 * - Nonce value
 *
 * The C_UnwrapKey API is called with the CKM_NVIDIA_AES_GCM_KEY_UNWRAP mechanism and
 * with the wrapped key blob as input.
 * No template is provided since the wrapped blob contains both the key value and the metadata.
 * The pTemplate and ulAttributeCount arguments should hence be set to NULL and 0 respectively.
 *
 * The aad buffer should be large enough to hold the maximum allowed
 * AAD size. For 4K RSA the AAD maximum size is 656 Bytes, so set
 * REFERENCE_UNWRAP_AAD_SIZE to 656U, this buffer size will accommodate
 * for AES, EC, and up to 4K RSA key unwrap.
 *
 * In the mechanism data for unwrap, gcmParamsUnwrap, the values for ulIvBits and ulTagBits
 * are fixed values represented in bits, (i.e ivSize * 8 and 16 * 8 respectively).
 */
CK_RV gcm_unwrap_reference_code(CK_SESSION_HANDLE hSession)
{
	CK_RV rv;
	CK_RV destroyRv = CKR_OK;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;

	/** Fused base key string identifier. Base key is used for deriving a session key. */
	char input_base_string[NVPKCS11_MAX_KEY_ID_SIZE] = "NV_OEM_KEY1";

	/** Key ID used as an identifier for the derived session key */
	char key_id_input_string[NVPKCS11_MAX_KEY_ID_SIZE] = "DERIVED_KEY";

	CK_BYTE aad[REFERENCE_UNWRAP_AAD_SIZE] = {0};
	CK_ULONG aadSize = sizeof(aad);

	CK_BYTE iv[REFERENCE_UNWRAP_IV_SIZE] = {0};
	CK_ULONG ivSize = sizeof(iv);

	CK_BYTE wrappedKeyAndMac[REFERENCE_UNWRAP_KEYTAG_SIZE] = {0};
	CK_ULONG wrappedKeyAndMacSize = sizeof(wrappedKeyAndMac);

	CK_BYTE derivationLabel[CK_SP800_MAX_LABEL_SIZE] = {0};
	CK_ULONG labelSize = sizeof(derivationLabel);

	CK_BYTE derivationContext[CK_SP800_MAX_CONTEXT_SIZE] = {0};
	CK_ULONG contextSize = sizeof(derivationContext);

	CK_BYTE exampleDevOnlyKey[NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES] = {0};
	CK_ULONG exampleDevOnlyKeySize = NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES;
	CK_BBOOL useExampleDevOnlyKey = (CK_BBOOL)CK_FALSE;

	/* Object handle and description in case example keys are used instead of NV_OEM_KEY1 */
	CK_OBJECT_HANDLE created_key_handle = CK_INVALID_HANDLE;
	CK_BYTE createdObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Created base key for derivation ";

	/* Load the wrapped key data from customer generated files copied to the system */
	rv = pkcs11_read_unwrap_data(aad, &aadSize,
				iv, &ivSize,
				wrappedKeyAndMac, &wrappedKeyAndMacSize,
				derivationLabel, &labelSize,
				derivationContext, &contextSize,
				exampleDevOnlyKey, &exampleDevOnlyKeySize, &useExampleDevOnlyKey);
	if (rv != CKR_OK)
	{
		printf("\n************************************************************\n");
		printf("MISSING WRAPPED FILES ON FILE SYSTEM, SKIPPING UNWRAP TEST\n");
		printf("************************************************************\n\n");
		rv = CKR_OK;
		goto gcm_unwrap_reference_code_end;
	}

	if (useExampleDevOnlyKey == (CK_BBOOL)CK_TRUE)
	{
		printf("\n*************************************************\n");
		printf("-- WARNING: USING NON-PRODUCTION EXAMPLE KEY --\n");
		printf("*************************************************\n\n");

		/* Create new base key to use to derive unwrapping key */
		CK_OBJECT_CLASS class = CKO_SECRET_KEY;
		CK_KEY_TYPE keyType = CKK_GENERIC_SECRET;
		CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;
		CK_MECHANISM_TYPE createAllowedMechanismType = CKM_SP800_108_COUNTER_KDF;

		CK_ATTRIBUTE createObjTemplate[] = {
			{CKA_CLASS, &class, sizeof(CK_OBJECT_CLASS)},
			{CKA_KEY_TYPE, &keyType, sizeof(CK_KEY_TYPE)},
			{CKA_ID, createdObjectId, sizeof(createdObjectId)},
			{CKA_DERIVE, &enabled, sizeof(CK_BBOOL)},
			{CKA_VALUE, exampleDevOnlyKey, exampleDevOnlyKeySize},
			{CKA_ALLOWED_MECHANISMS, &createAllowedMechanismType, sizeof(createAllowedMechanismType)},
		};

		rv = C_CreateObject(hSession, createObjTemplate, sizeof(createObjTemplate)/sizeof(CK_ATTRIBUTE), &created_key_handle);
		GO_TO_LABEL_ON_ERROR(rv, "C_CreateObject", gcm_unwrap_reference_code_end);
	}

	/* Derive a key to use as the unwrapping key, using the same parameters as the offline wrapping key. */
	CK_OBJECT_HANDLE unwrapping_key_handle = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE unwrapped_key_handle = CK_INVALID_HANDLE;
	CK_ULONG key_id_input_string_len = strlen(key_id_input_string);

	/* Select which key to use for derive purpose (NV_OEM_KEY1 or Example_key) */
	char *input_string_ptr = (char*)input_base_string;
	CK_ULONG input_base_string_len = strlen(input_string_ptr);
	if (useExampleDevOnlyKey == (CK_BBOOL)CK_TRUE)
	{
		input_string_ptr = (char*)createdObjectId;
		input_base_string_len = sizeof(createdObjectId);
	}

	rv = find_object_derive_key(
			hSession,
			&unwrapping_key_handle,
			input_string_ptr,
			input_base_string_len,
			key_id_input_string,
			key_id_input_string_len,
			(char *)derivationLabel,
			labelSize,
			(char *)derivationContext,
			contextSize,
			(CK_BBOOL)CK_TRUE);
	GO_TO_LABEL_ON_ERROR(rv, "find_object_derive_key", clean_up);

	/* Populate the mechanism data with required information to perform the unwrap */
	CK_GCM_PARAMS gcmParamsUnwrap = {
			.pIv = iv,
			.ulIvLen = ivSize,
			.ulIvBits = ivSize * 8U,
			.pAAD = aad, /* Additional Authentication Data */
			.ulAADLen = aadSize,
			.ulTagBits = 16U * 8U};

	CK_MECHANISM mechanismUnwrap = {CKM_NVIDIA_AES_GCM_KEY_UNWRAP, &gcmParamsUnwrap, sizeof(CK_GCM_PARAMS)};

	/* Unwrap the wrapped key, note that the pTemplate pointer needs to be NULL */
	rv = C_UnwrapKey(hSession,
			&mechanismUnwrap,
			unwrapping_key_handle,
			wrappedKeyAndMac,
			wrappedKeyAndMacSize,
			NULL,
			0,
			&unwrapped_key_handle);
	GO_TO_LABEL_ON_ERROR(rv, "C_UnwrapKey", unwrapping_clean_up);

	printf("\n********** Key unwrapped successfully **********\n");

	CK_MECHANISM mechanism_sign = {CKM_AES_CMAC, NULL, 0};
	char test_sign_data_buffer[] = "sample text to be signed";
	CK_BYTE macSignature[NVPKCS11_AES_CMAC_SIGNATURE_SIZE];
	CK_ULONG buffer_size = (CK_ULONG)NVPKCS11_AES_CMAC_SIGNATURE_SIZE;

	rv = C_SignInit(hSession, &mechanism_sign, unwrapped_key_handle);
	GO_TO_LABEL_ON_ERROR(rv, "C_SignInit", unwrap_clean_up);

	rv = C_Sign(hSession,
			(CK_BYTE_PTR) test_sign_data_buffer,
			strlen(test_sign_data_buffer),
			macSignature, &buffer_size);
	GO_TO_LABEL_ON_ERROR(rv, "C_Sign", unwrap_clean_up);

	printf("\n** Sign command with unwrapped key successful **\n");
	print_data(macSignature, buffer_size);

unwrap_clean_up:
	destroyRv = C_DestroyObject(hSession, unwrapped_key_handle);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject wrapped key", failedFlag);

unwrapping_clean_up:
	destroyRv = C_DestroyObject(hSession, unwrapping_key_handle);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject wrapping key", failedFlag);

clean_up:
	if (created_key_handle != CK_INVALID_HANDLE) /* Only applicable if NV_OEM_KEY1 not used */
	{
		destroyRv = C_DestroyObject(hSession, created_key_handle);
		PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject created key", failedFlag);
	}
gcm_unwrap_reference_code_end:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}
