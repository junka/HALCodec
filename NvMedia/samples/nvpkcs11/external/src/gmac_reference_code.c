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

CK_RV gmac_reference_code(CK_SESSION_HANDLE hSession, CK_ULONG data_len)
{
	CK_RV rv;
	CK_RV destroyRv;

	printf("\n====================================================\n");
	printf("GMAC:\n");

	/* Allocate data_len bytes of memory */
	CK_BYTE_PTR pRndData = NULL;
	pRndData = (CK_BYTE *)malloc(data_len * sizeof(CK_BYTE));
	if (pRndData == NULL)
	{
		rv = CKR_FUNCTION_FAILED;
		GO_TO_LABEL_ON_ERROR(rv, "malloc", gmac_reference_code_exit);
	}

	/**
	 * Generate random data for data_len bytes.
	 * C_GenerateRandom can generate NVPKCS11_RANDOM_DATA_MAXLENGTH bytes at a time.
	 * Calculate how many blocks of NVPKCS11_RANDOM_DATA_MAXLENGTH bytes will be needed,
	 * and loop through allocated memory until all bytes are populated with a random value
	 */
	CK_ULONG rngBlocks = data_len / NVPKCS11_RANDOM_DATA_MAXLENGTH;
	CK_ULONG remain = data_len % NVPKCS11_RANDOM_DATA_MAXLENGTH;
	CK_BYTE_PTR pInputLoc = pRndData;

	for (CK_ULONG i = 0; i < rngBlocks; i++)
	{
		rv = C_GenerateRandom(hSession, pInputLoc, NVPKCS11_RANDOM_DATA_MAXLENGTH);
		GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom - NVPKCS11_RANDOM_DATA_MAXLENGTH", free_memory);
		pInputLoc += NVPKCS11_RANDOM_DATA_MAXLENGTH;
	}

	if (remain > 0)
	{
		rv = C_GenerateRandom(hSession, pInputLoc, remain);
		GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom - remain", free_memory);
	}

	/* Generate a GMAC sign/verify key */
	CK_BYTE gmacObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Generated key gmac sign verify  ";
	CK_OBJECT_CLASS objClass = CKO_SECRET_KEY;
	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;
	CK_MECHANISM mechanismGenKey = {CKM_AES_KEY_GEN, NULL, 0};
	CK_ULONG valueLen = NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES;
	CK_OBJECT_HANDLE hGmacKey = CK_INVALID_HANDLE;
	/* Set the derivation mechanism */
	CK_MECHANISM_TYPE gmacAllowedMechanismType = CKM_AES_GMAC;

	CK_ATTRIBUTE generateObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_ID, gmacObjectId, sizeof(gmacObjectId)},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE_LEN, &valueLen, sizeof(CK_ULONG)},
		{CKA_ALLOWED_MECHANISMS, &gmacAllowedMechanismType, sizeof(gmacAllowedMechanismType)},
	};

	rv = C_GenerateKey(hSession, &mechanismGenKey, generateObjTemplate, sizeof(generateObjTemplate)/sizeof(CK_ATTRIBUTE), &hGmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateKey", free_memory);

	/* Allocate buffers and establish parameters */
	CK_BYTE signature[GCM_PARAMS_TAG_BYTES] = {0};
	CK_ULONG ulSignatureLen = GCM_PARAMS_TAG_BYTES;
	CK_BYTE iv[GCM_PARAMS_IV_BYTES] = {0};

	CK_GCM_MESSAGE_PARAMS gcmMessageParams = {
		.pIv = iv,
		.ulIvLen = GCM_PARAMS_IV_BYTES,
		.ulIvFixedBits = 0,
		.ivGenerator = CKG_NO_GENERATE,
		.ulTagBits = GCM_PARAMS_TAG_BITS,
		.pTag = NULL
	};
	CK_ULONG ulGcmMessageParamsLen = sizeof(CK_GCM_MESSAGE_PARAMS);
	CK_MECHANISM mechanismSignVerify = {CKM_AES_GMAC, NULL, 0};

	/* Perform message GMAC sign */
	rv = C_MessageSignInit(hSession, &mechanismSignVerify, hGmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_MessageSignInit", hGmacKey_destroy);

	rv = C_SignMessage(hSession, &gcmMessageParams, ulGcmMessageParamsLen, pRndData, data_len, signature, &ulSignatureLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_SignMessage", hGmacKey_destroy);

	rv = C_MessageSignFinal(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "C_MessageSignFinal", hGmacKey_destroy);

	printf("\nIV value returned from GMAC sign:\n");
	print_data(iv, GCM_PARAMS_IV_BYTES);

	printf("\nComputed signature using GMAC:\n");
	print_data(signature, ulSignatureLen);

	/* Perform message GMAC verify */
	rv = C_MessageVerifyInit(hSession, &mechanismSignVerify, hGmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_MessageVerifyInit", hGmacKey_destroy);

	rv = C_VerifyMessage(hSession, &gcmMessageParams, ulGcmMessageParamsLen, pRndData, data_len, signature, ulSignatureLen);
	GO_TO_LABEL_ON_ERROR(rv, "C_VerifyMessage", hGmacKey_destroy);

	rv = C_MessageVerifyFinal(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "C_MessageVerifyFinal", hGmacKey_destroy);

hGmacKey_destroy:
	destroyRv = C_DestroyObject(hSession, hGmacKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_DestroyObject - hGmacKey", free_memory);
free_memory:
	free(pRndData);
gmac_reference_code_exit:
	if ((rv != CKR_OK) || (destroyRv != CKR_OK))
	{
		rv = CKR_FUNCTION_FAILED;
	}
	return rv;
}