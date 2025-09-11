/* ***************************************************************************** *
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 ******************************************************************************* */

#include "pkcs11_reference_apps_code.h"

CK_RV gcm_encrypt_decrypt(CK_SESSION_HANDLE hSession)
{
	CK_RV rv;
	CK_RV destroyRv = CKR_OK;
	/* Create a key to be used for gcm encryption and decryption */
	CK_OBJECT_HANDLE hObject = CK_INVALID_HANDLE;
	CK_BYTE createdObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Created key for gcm enc dec     ";
	CK_OBJECT_CLASS objClass = CKO_SECRET_KEY;
	CK_KEY_TYPE keyType = CKK_AES;
	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;
	CK_MECHANISM_TYPE createAllowedMechanismType = CKM_AES_GCM;
	CK_BYTE keyValue[NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES] = {0};

	printf("\n====================================================\n");
	printf("AES GCM:\n");

	rv = C_GenerateRandom(hSession, keyValue, sizeof(keyValue));
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", gcm_encrypt_decrypt_exit);

	CK_ATTRIBUTE createObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &keyType, sizeof(CK_KEY_TYPE)},
		{CKA_ID, createdObjectId, sizeof(createdObjectId)},
		{CKA_ENCRYPT, &enabled, sizeof(CK_BBOOL)},
		{CKA_DECRYPT, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE, keyValue, sizeof(keyValue)},
		{CKA_ALLOWED_MECHANISMS, &createAllowedMechanismType, sizeof(createAllowedMechanismType)},
	};

	rv = C_CreateObject(hSession, createObjTemplate, sizeof(createObjTemplate)/sizeof(CK_ATTRIBUTE), &hObject);
	GO_TO_LABEL_ON_ERROR(rv, "C_CreateObject", gcm_encrypt_decrypt_exit);

	CK_BYTE aad[] = "\x00\x01\x02\x03\x04"; /*< Optional Additional Authenticated Data (AAD) */
	CK_ULONG ulAadSize = sizeof(aad);
	CK_BYTE plainText[] = "\x70\x6C\x61\x69\x6E\x74\x65\x78\x74\x20\x74\x6F\x20\x65\x6E\x63\x72\x79\x70\x74";
	CK_ULONG ulPlainTextSize = sizeof(plainText);
	CK_BYTE cipherText[sizeof(plainText) + GCM_PARAMS_TAG_BYTES] = {0};	/*< Data buffer where encrypted data will be returned together with the tag value (16 bytes) appended to the end */
	CK_ULONG ulCipherTextSize = sizeof(cipherText);
	CK_BYTE ivBuffer[GCM_PARAMS_IV_BYTES] = {0};
	CK_BYTE ivBufferSize = (CK_BYTE)sizeof(ivBuffer);

	/* Configure GCM parameters for encrypt operation */
	CK_GCM_PARAMS gcmEncParams = {
		.pIv = NULL,		/*< Mandatory, always NULL for encrypt operations as IV is not allowed as an input value */
		.ulIvLen = 0,		/*< Mandatory, always 0 */
		.ulIvBits = 0,		/*< Mandatory, always 0 */
		.pAAD = aad,		/*< Optional, point to AAD buffer if used, if not, set to NULL */
		.ulAADLen = ulAadSize,	/*< Optional, if AAD is provided, set to the length of data, otherwise set to 0 */
		.ulTagBits = GCM_PARAMS_TAG_BYTES * 8u	/*< Mandatory, must be set to 128 bits */
	};
	CK_MECHANISM encMechanism = {CKM_AES_GCM, &gcmEncParams, sizeof(gcmEncParams)};

	rv = C_EncryptInit(hSession, &encMechanism, hObject);
	GO_TO_LABEL_ON_ERROR(rv, "C_EncryptInit", hObject_destroy);

	rv = C_Encrypt(hSession, plainText, ulPlainTextSize, cipherText, &ulCipherTextSize);
	GO_TO_LABEL_ON_ERROR(rv, "C_Encrypt", hObject_destroy);

	/* The IV value is extracted after the encrypt operation is completed */
	rv = C_NVIDIA_EncryptGetIV(hSession, ivBuffer, &ivBufferSize);
	GO_TO_LABEL_ON_ERROR(rv, "C_NVIDIA_EncryptGetIV", hObject_destroy);

	printf("Plain text input data:\n");
	print_data(plainText, ulPlainTextSize);

	printf("\nCipher text:\n");
	print_data(cipherText, ulCipherTextSize - GCM_PARAMS_TAG_BYTES);

	printf("\nGCM TAG:\n");
	print_data(&cipherText[ulCipherTextSize - GCM_PARAMS_TAG_BYTES], GCM_PARAMS_TAG_BYTES);

	printf("\nIV data:\n");
	print_data(ivBuffer, ivBufferSize);

	/* Configure GCM parameters for decrypt operation */
	CK_GCM_PARAMS gcmDecParams = {
		.pIv = ivBuffer,		/*< Mandatory, pointer to IV buffer */
		.ulIvLen = ivBufferSize,	/*< Mandatory, must be set to 12 bytes */
		.ulIvBits = ivBufferSize * 8u,	/*< Mandatory, must be set to 96 bits */
		.pAAD = aad,			/*< Optional, point to AAD buffer if used, if not, set to NULL */
		.ulAADLen = ulAadSize,		/*< Optional, if AAD is provided, set to the length of data, otherwise set to 0 */
		.ulTagBits = GCM_PARAMS_TAG_BYTES * 8u		/*< Mandatory, must be set to 128 bits */
	};
	CK_MECHANISM decMechanism = {CKM_AES_GCM, &gcmDecParams, sizeof(gcmEncParams)};

	rv = C_DecryptInit(hSession, &decMechanism, hObject);
	GO_TO_LABEL_ON_ERROR(rv, "C_DecryptInit", hObject_destroy);

	CK_BYTE decBuffer[sizeof(plainText)] = {0};
	CK_ULONG ulDecBufferSize = sizeof(decBuffer);

	rv = C_Decrypt(hSession, cipherText, ulCipherTextSize, decBuffer, &ulDecBufferSize);
	GO_TO_LABEL_ON_ERROR(rv, "C_Decrypt", hObject_destroy);

	printf("\nDecrypted data:\n");
	print_data(decBuffer, ulDecBufferSize);

	if (ulDecBufferSize != ulPlainTextSize)
	{
		rv = CKR_FUNCTION_FAILED;
		printf("ERROR! Decrypted data size does not match original data size\n");
	}
	else
	{
		if (memcmp(plainText, decBuffer, ulDecBufferSize) != 0)
		{
			rv = CKR_FUNCTION_FAILED;
			printf("ERROR! Decrypt buffer does not match original data\n");
		}
	}

hObject_destroy:
	destroyRv = C_DestroyObject(hSession, hObject);
	GO_TO_LABEL_ON_ERROR(destroyRv, "C_DestroyObject", gcm_encrypt_decrypt_exit);

gcm_encrypt_decrypt_exit:
	if ((rv != CKR_OK) || (destroyRv != CKR_OK))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}

CK_RV tls_gcm_reference_code(CK_SESSION_HANDLE hSession)
{
	CK_RV rv;
	CK_RV destroyRv = CKR_OK;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;

	CK_OBJECT_HANDLE hClientKey = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE hPreMasterObject = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE hMasterObject = CK_INVALID_HANDLE;

	CK_BYTE preMasterObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "PreMaster key for derivation    ";
	CK_BYTE masterObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Master key for derivation       ";
	CK_BYTE encDecObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "Derived key for enc/dec         ";

	CK_OBJECT_CLASS objClass = CKO_SECRET_KEY;
	CK_KEY_TYPE masterKeyType = CKK_GENERIC_SECRET;
	CK_KEY_TYPE encDecKeyType = CKK_AES;
	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;

	CK_MECHANISM_TYPE encDecKeyAllowedMechanismType = CKM_AES_GCM;
	CK_MECHANISM_TYPE preMasterKeyAllowedMechanismType = CKM_TLS12_MASTER_KEY_DERIVE_DH;
	CK_MECHANISM_TYPE masterKeyAllowedMechanismType = CKM_TLS12_KEY_SAFE_DERIVE;

	printf("\n====================================================\n");
	printf("TLS AES GCM:\n");

	/* Create a preMaster key, generating a random value for the preMaster key value */
	CK_BYTE preMasterKeyValue[NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES] = {0};
	rv = C_GenerateRandom(hSession, preMasterKeyValue, sizeof(preMasterKeyValue));
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", tls_gcm_reference_code_exit);

	CK_ATTRIBUTE preMasterKeyObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &masterKeyType, sizeof(CK_KEY_TYPE)},
		{CKA_ID, preMasterObjectId, sizeof(preMasterObjectId)},
		{CKA_DERIVE, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE, preMasterKeyValue, sizeof(preMasterKeyValue)},
		{CKA_ALLOWED_MECHANISMS, &preMasterKeyAllowedMechanismType, sizeof(preMasterKeyAllowedMechanismType)},
	};

	rv = C_CreateObject(hSession, preMasterKeyObjTemplate, sizeof(preMasterKeyObjTemplate)/sizeof(CK_ATTRIBUTE), &hPreMasterObject);
	GO_TO_LABEL_ON_ERROR(rv, "C_CreateObject - hPreMasterObject", tls_gcm_reference_code_exit);

	/* Derive TLS master key */
	CK_VERSION tlsVersion = {
		.major = 1,
		.minor = 2,
	};

	/* Generate a random value for the clientRandom value */
	CK_BYTE clientRandom[NVPKCS11_TLS_HANDSHAKE_RANDOM_LENGTH_IN_BYTES] = {0};
	rv = C_GenerateRandom(hSession, clientRandom, sizeof(clientRandom));
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", hPreMasterObject_destroy);

	/* Generate a random value for the serverRandom value */
	CK_BYTE serverRandom[NVPKCS11_TLS_HANDSHAKE_RANDOM_LENGTH_IN_BYTES] = {0};
	rv = C_GenerateRandom(hSession, serverRandom, sizeof(serverRandom));
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", hPreMasterObject_destroy);

	CK_TLS12_MASTER_KEY_DERIVE_PARAMS tlsMasterSecretDeriveParams = {
		.pVersion = &tlsVersion,
		.prfHashMechanism = CKM_SHA256_HMAC,
		.RandomInfo.pClientRandom = clientRandom,
		.RandomInfo.ulClientRandomLen = NVPKCS11_TLS_HANDSHAKE_RANDOM_LENGTH_IN_BYTES,
		.RandomInfo.pServerRandom = serverRandom,
		.RandomInfo.ulServerRandomLen = NVPKCS11_TLS_HANDSHAKE_RANDOM_LENGTH_IN_BYTES,
	};

	CK_ULONG ulMasterSecretBytesLen = NVPKCS11_TLS_MASTER_SECRET_KEY_LENGTH_IN_BYTES;

	CK_ATTRIBUTE masterSecretTemplate[] = {
		{CKA_ID, masterObjectId, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &masterKeyType, sizeof(CK_KEY_TYPE)},
		{CKA_DERIVE, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE_LEN, &ulMasterSecretBytesLen, sizeof(ulMasterSecretBytesLen)},
		{CKA_ALLOWED_MECHANISMS, &masterKeyAllowedMechanismType, sizeof(masterKeyAllowedMechanismType)}
	};
	CK_ULONG ulMasterSecretAttributeCount = sizeof(masterSecretTemplate) / sizeof(CK_ATTRIBUTE);

	/* Derive the master secret */
	CK_MECHANISM tlsMasterSecretDeriveMechanism = {CKM_TLS12_MASTER_KEY_DERIVE_DH, &tlsMasterSecretDeriveParams, sizeof(CK_TLS12_MASTER_KEY_DERIVE_PARAMS) };
	rv = C_DeriveKey(hSession, &tlsMasterSecretDeriveMechanism, hPreMasterObject, masterSecretTemplate, ulMasterSecretAttributeCount, &hMasterObject);
	GO_TO_LABEL_ON_ERROR(rv, "C_DeriveKey - hMasterObject", hPreMasterObject_destroy);

	/* Derive a key to be used for gcm encryption and decryption */
	CK_SSL3_KEY_MAT_OUT keyMatOut = {
		.hClientMacSecret = CK_INVALID_HANDLE,
		.hServerMacSecret = CK_INVALID_HANDLE,
		.hClientKey = CK_INVALID_HANDLE,
		.hServerKey = CK_INVALID_HANDLE,
		.pIVClient = NULL,
		.pIVServer = NULL,
	};

	CK_TLS12_KEY_MAT_PARAMS tlsParams = {
		.prfHashMechanism = CKM_SHA256_HMAC,
		.ulMacSizeInBits = 0,
		.ulKeySizeInBits = NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES * 8U,
		.ulIVSizeInBits = 0,
		.bIsExport = CK_FALSE,
		.RandomInfo = {
			.pClientRandom = clientRandom,
			.ulClientRandomLen = NVPKCS11_TLS_HANDSHAKE_RANDOM_LENGTH_IN_BYTES,
			.pServerRandom = serverRandom,
			.ulServerRandomLen = NVPKCS11_TLS_HANDSHAKE_RANDOM_LENGTH_IN_BYTES,
		},
		.pReturnedKeyMaterial = &keyMatOut,
	};

	CK_ULONG encDecKeyValueLen = NVPKCS11_SECRET_KEY_LENGTH_IN_BYTES;

	CK_ATTRIBUTE encDecObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &encDecKeyType, sizeof(CK_KEY_TYPE)},
		{CKA_ID, encDecObjectId, sizeof(encDecObjectId)},
		{CKA_ENCRYPT, &enabled, sizeof(CK_BBOOL)},
		{CKA_DECRYPT, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE_LEN, &encDecKeyValueLen, sizeof(CK_ULONG)},
		{CKA_NVIDIA_CALLER_NONCE, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &encDecKeyAllowedMechanismType, sizeof(encDecKeyAllowedMechanismType)},
	};

	CK_MECHANISM tlsKeyDeriveMechanism = {CKM_TLS12_KEY_SAFE_DERIVE, &tlsParams, sizeof(CK_TLS12_KEY_MAT_PARAMS)};
	rv = C_DeriveKey(hSession, &tlsKeyDeriveMechanism, hMasterObject, encDecObjTemplate, sizeof(encDecObjTemplate)/sizeof(CK_ATTRIBUTE), NULL);
	GO_TO_LABEL_ON_ERROR(rv, "C_DeriveKey", hMasterObject_destroy);

	/* hClientKey supports encryption and decryption using GCM with caller nonces */
	hClientKey = keyMatOut.hClientKey;

	CK_BYTE aad[] = "\x00\x01\x02\x03\x04"; /*< Optional Additional Authenticated Data (AAD) */
	CK_ULONG ulAadSize = sizeof(aad);
	CK_BYTE plainText[] = "\x70\x6C\x61\x69\x6E\x74\x65\x78\x74\x20\x74\x6F\x20\x65\x6E\x63\x72\x79\x70\x74";
	CK_ULONG ulPlainTextSize = sizeof(plainText);
	CK_BYTE cipherText[sizeof(plainText) + GCM_PARAMS_TAG_BYTES] = {0};	/* Data buffer where encrypted data will be returned together with the tag value (16 bytes) appended to the end */
	CK_ULONG ulCipherTextSize = sizeof(cipherText);
	CK_BYTE ivBuffer[GCM_PARAMS_IV_BYTES] = "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B"; /*< Caller determined IV value */
	CK_BYTE ivBufferSize = (CK_BYTE)sizeof(ivBuffer);

	/* Configure GCM parameters for encrypt and decrypt operations */
	CK_GCM_PARAMS gcmEncDecParams = {
		.pIv = ivBuffer,                        /*< Mandatory, pointer to IV buffer */
		.ulIvLen = ivBufferSize,                /*< Mandatory, must be set to 12 bytes */
		.ulIvBits = ivBufferSize * 8,           /*< Mandatory, must be set to 96 bits */
		.pAAD = aad,                            /*< Optional, point to AAD buffer if used, if not, set to NULL */
		.ulAADLen = ulAadSize,                  /*< Optional, if AAD is provided, set to the length of data, otherwise set to 0 */
		.ulTagBits = GCM_PARAMS_TAG_BYTES * 8u       /*< Mandatory, must be set to 128 bits */
	};
	CK_MECHANISM encDecMechanism = {CKM_AES_GCM, &gcmEncDecParams, sizeof(gcmEncDecParams)};

	/* Perform encrypt operation */
	rv = C_EncryptInit(hSession, &encDecMechanism, hClientKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_EncryptInit", hClientKey_destroy);

	rv = C_Encrypt(hSession, plainText, ulPlainTextSize, cipherText, &ulCipherTextSize);
	GO_TO_LABEL_ON_ERROR(rv, "C_Encrypt", hClientKey_destroy);

	printf("Plain text input data:\n");
	print_data(plainText, ulPlainTextSize);

	printf("\nCipher text:\n");
	print_data(cipherText, ulCipherTextSize - GCM_PARAMS_TAG_BYTES);

	printf("\nGCM TAG:\n");
	print_data(&cipherText[ulCipherTextSize - GCM_PARAMS_TAG_BYTES], GCM_PARAMS_TAG_BYTES);

	printf("\nIV data:\n");
	print_data(ivBuffer, ivBufferSize);

	/**
	 * Perform decrypt operation.
	 * Note: The IV value in the mechanism has not been updated, it is the value provided by the caller.
	 */
	CK_BYTE decBuffer[sizeof(plainText)] = {0};
	CK_ULONG ulDecBufferSize = sizeof(decBuffer);

	rv = C_DecryptInit(hSession, &encDecMechanism, hClientKey);
	GO_TO_LABEL_ON_ERROR(rv, "C_DecryptInit", hClientKey_destroy);

	rv = C_Decrypt(hSession, cipherText, ulCipherTextSize, decBuffer, &ulDecBufferSize);
	GO_TO_LABEL_ON_ERROR(rv, "C_Decrypt", hClientKey_destroy);

	printf("\nDecrypted data:\n");
	print_data(decBuffer, ulDecBufferSize);

	if (ulDecBufferSize != ulPlainTextSize)
	{
		rv = CKR_FUNCTION_FAILED;
		printf("ERROR! Decrypted data size does not match original data size\n");
	}
	else
	{
		if (memcmp(plainText, decBuffer, ulDecBufferSize) != 0)
		{
			rv = CKR_FUNCTION_FAILED;
			printf("ERROR! Decrypt buffer does not match original data\n");
		}
	}

hClientKey_destroy:
	destroyRv = C_DestroyObject(hSession, keyMatOut.hServerKey);
        PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - keyMatOut.hServerKey", failedFlag);
	destroyRv = C_DestroyObject(hSession, hClientKey);
        PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hClientKey", failedFlag);
hMasterObject_destroy:
	destroyRv = C_DestroyObject(hSession, hMasterObject);
        PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hMasterObject", failedFlag);
hPreMasterObject_destroy:
	destroyRv = C_DestroyObject(hSession, hPreMasterObject);
        PRINT_MESSAGE_ON_ERROR_CONTINUE(destroyRv, "C_DestroyObject - hPreMasterObject", failedFlag);
tls_gcm_reference_code_exit:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}
