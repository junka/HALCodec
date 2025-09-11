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
 * To alter the objects in the safety view:
 *
 * - Make the changes in the dynamic view of the same storage ID
 *
 * - Call C_NVIDIA_CommitTokenObjects() with no PKCS#11 sessions open on any safety token
 *   (this is to prevent safety-critical operations stalling as the commit happens)
 *
 * - Either reboot, or go through an SC7 cycle (calling C_Finalize() before suspending and
 *   C_Initialize() after resuming)
 */
CK_RV token_object_reference_code(CK_SESSION_HANDLE hSession)
{
	CK_RV rv = CKR_OK;
	CK_RV destroyRv = CKR_OK;

	/* Create a token key to be used for cbc encryption and decryption */
	CK_OBJECT_HANDLE hObject = CK_INVALID_HANDLE;
	CK_BYTE createdObjectId[NVPKCS11_MAX_KEY_ID_SIZE] = "refAppTokenObjectExampleId      ";
	CK_OBJECT_CLASS objClass = CKO_SECRET_KEY;
	CK_KEY_TYPE keyType = CKK_AES;
	CK_BBOOL enabled = (CK_BBOOL)CK_TRUE;
	CK_MECHANISM_TYPE createAllowedMechanismType = CKM_AES_CBC;
	CK_BYTE keyValue[NVPKCS11_LONG_SECRET_KEY_LENGTH_IN_BYTES] = {0};

	printf("\n====================================================\n");
	printf("TOKEN OBJECT:\n");

#ifndef TOKEN_OBJECT_EXAMPLE
	printf("Skipping example.\n");
	goto token_object_reference_code_exit;
#endif

	/**
	 * Check if the example token key already exists.
	 * If it does exist then delete it.
	 */
	CK_OBJECT_HANDLE hFindObject = CK_INVALID_HANDLE;
	CK_ULONG ulFoundObjectCount = 0;
	CK_OBJECT_CLASS searchObjectClass = CKO_SECRET_KEY;
	CK_ATTRIBUTE findKeyTemplate[] = {
			{CKA_ID, createdObjectId, NVPKCS11_MAX_KEY_ID_SIZE},
			{CKA_CLASS, &searchObjectClass, sizeof(CK_OBJECT_CLASS)}};

	rv = C_FindObjectsInit(hSession, findKeyTemplate, sizeof(findKeyTemplate)/sizeof(CK_ATTRIBUTE));
	GO_TO_LABEL_ON_ERROR(rv, "C_FindObjectsInit", token_object_reference_code_exit);

	rv = C_FindObjects(hSession, &hFindObject, 1, &ulFoundObjectCount);
	GO_TO_LABEL_ON_ERROR(rv, "C_FindObjects", token_object_reference_code_exit);

	rv = C_FindObjectsFinal(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "C_FindObjectsFinal", token_object_reference_code_exit);

	if (ulFoundObjectCount != 0)
	{
		/**
		 * Note: This will delete the existing key from the dynamic token.
		 * If C_NVIDIA_CommitTokenObjects is then called, the key will be permanently deleted after a reboot / sc7 cycle.
		 * After this, the key will be removed from the safety token.
		 * If C_NVIDIA_CommitTokenObjects is NOT called, the key will reappear after a reboot / sc7 cycle.
		 */
		printf("Deleting previous example token key from the dynamic token\n");
		rv = C_DestroyObject(hSession, hFindObject);
		GO_TO_LABEL_ON_ERROR(rv, "C_DestroyObject", token_object_reference_code_exit);
	}

	/* Generate a random value to use as the key value */
	rv = C_GenerateRandom(hSession, keyValue, sizeof(keyValue));
	GO_TO_LABEL_ON_ERROR(rv, "C_GenerateRandom", token_object_reference_code_exit);

	CK_ATTRIBUTE createObjTemplate[] = {
		{CKA_CLASS, &objClass, sizeof(CK_OBJECT_CLASS)},
		{CKA_KEY_TYPE, &keyType, sizeof(CK_KEY_TYPE)},
		{CKA_ID, createdObjectId, sizeof(createdObjectId)},
		{CKA_ENCRYPT, &enabled, sizeof(CK_BBOOL)},
		{CKA_DECRYPT, &enabled, sizeof(CK_BBOOL)},
		{CKA_VALUE, keyValue, sizeof(keyValue)},
		{CKA_ALLOWED_MECHANISMS, &createAllowedMechanismType, sizeof(createAllowedMechanismType)},
		/**
		 * Create a token object by enabling the CKA_TOKEN attribute.
		 * This attribute can only be set on a dynamic token in a Read/write (R/W) session.
		 * If this attribute is set in a read only (RO) session C_CreateObject will fail.
		 */
		{CKA_TOKEN, &enabled, sizeof(CK_BBOOL)},
	};

	rv = C_CreateObject(hSession, createObjTemplate, sizeof(createObjTemplate)/sizeof(CK_ATTRIBUTE), &hObject);
	GO_TO_LABEL_ON_ERROR(rv, "C_CreateObject", token_object_reference_code_exit);

	/**
	 * C_NVIDIA_CommitTokenObjects writes the current state of all token objects on a dynamic token to secure storage.
	 * By committing the tokens to secure storage they will become available to view on the safety token of the same
	 * storage ID after a reboot or SC7 cycle.
	 *
	 * If changes have been made, this function may take up to several minutes to complete.
	 * This would apply to all tokens in the system, not just the token referenced in the call.
	 * To protect safety-critical operations from being blocked, this function must only be called during
	 * the deinit phase, as it could have an impact on live operations and boot time.
	 *
	 * To enforce safe operation, this function will fail with CKR_OPERATION_ACTIVE if any application
	 * has a safety token session open on this device (not just the token referenced in this call).
	 */
	rv = C_NVIDIA_CommitTokenObjects(hSession, 0U);
	GO_TO_LABEL_ON_ERROR(rv, "C_NVIDIA_CommitTokenObjects", hObject_destroy);
	printf("Token object created and committed to secure storage\n");

hObject_destroy:
	/**
	 * A real application would now reboot or perform an sc7 cycle and then use the key.
	 * For this example, the key is now deleted, otherwise it would remain on the device permanently.
	 */
	destroyRv = C_DestroyObject(hSession, hObject);
	GO_TO_LABEL_ON_ERROR(destroyRv, "C_DestroyObject", token_object_reference_code_exit);

	/* Commit the deletion of the key from secure storage */
	rv = C_NVIDIA_CommitTokenObjects(hSession, 0U);
	GO_TO_LABEL_ON_ERROR(rv, "C_NVIDIA_CommitTokenObjects", token_object_reference_code_exit);
	printf("Token object deleted and committed to secure storage\n");

token_object_reference_code_exit:
	if ((rv != CKR_OK) || (destroyRv != CKR_OK))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}