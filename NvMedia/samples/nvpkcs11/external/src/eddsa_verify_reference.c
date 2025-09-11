/* ***************************************************************************** *
 * Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 ******************************************************************************* */

#include "pkcs11_reference_apps_code.h"

CK_RV eddsa_verify_operation(
		CK_SESSION_HANDLE hSession,
		CK_MECHANISM_PTR mechanism_verify_ptr,
		CK_OBJECT_HANDLE eddsa_key_handle,
		CK_BYTE_PTR data_ptr,
		CK_ULONG data_len,
		CK_BYTE_PTR signature_ptr,
		CK_ULONG signature_len)
{
	/** Initialize a verification operation */
	CK_RV rv = C_VerifyInit(hSession, mechanism_verify_ptr, eddsa_key_handle);
	GO_TO_LABEL_ON_ERROR(rv, "C_VerifyInit", eddsa_verify_operation_end);

	/** Verify the signature in a single-part operation */
	rv = C_Verify(
			hSession,
			data_ptr,
			data_len,
			signature_ptr,
			signature_len);
	if (rv != CKR_OK)
	{
		printf("C_Verify() returned rv = %lu: *** This verify operation failure is expected due to dummy input data\n", rv);
		printf("User must provide correct ed25519 compressed point data, signature, and message data\n");
	}
	GO_TO_LABEL_ON_ERROR(rv, "C_Verify", eddsa_verify_operation_end);

eddsa_verify_operation_end:
	return rv;
}

CK_RV eddsa_verify_reference_code(CK_SESSION_HANDLE hSession, CK_BYTE_PTR data_ptr, CK_ULONG data_len, CK_BYTE_PTR signature_ptr, CK_ULONG signature_len)
{
	CK_RV rv;
	CK_RV destroyRv = CKR_OK;

	/**
	 * CKM_EDDSA Verify with the ed25519ph signature scheme
	 * First create the key object, then if
	 * successful invoke the verify operation
	 */
	CK_OBJECT_HANDLE eddsa_key_handle = CK_INVALID_HANDLE;
	/** ed25519_KEY ID string */
	char cka_ecid_string[] = "ed25519";
	CK_ULONG cka_ecid_string_len = strlen(cka_ecid_string);
	rv = eddsa_key_object_create(hSession, &eddsa_key_handle, cka_ecid_string, cka_ecid_string_len);
	GO_TO_LABEL_ON_ERROR(rv, "eddsa_key_object_create", eddsa_verify_reference_code_end);

	CK_EDDSA_PARAMS eddsa_parameters = {(CK_BBOOL)CK_TRUE, (CK_ULONG)0, NULL};
	CK_MECHANISM eddsa_mechanism_verify = {CKM_EDDSA, &eddsa_parameters, sizeof(CK_EDDSA_PARAMS)};
	rv = eddsa_verify_operation(
			hSession,
			&eddsa_mechanism_verify,
			eddsa_key_handle,
			data_ptr,
			data_len,
			signature_ptr,
			signature_len);
	GO_TO_LABEL_ON_ERROR(rv, "eddsa_mechanism_verify", eddsa_key_destroy);

eddsa_key_destroy:
	destroyRv = C_DestroyObject(hSession, eddsa_key_handle);
	GO_TO_LABEL_ON_ERROR(destroyRv, "C_DestroyObject", eddsa_verify_reference_code_end);

eddsa_verify_reference_code_end:
	if ((rv != CKR_OK) || (destroyRv != CKR_OK))
	{
		rv = CKR_FUNCTION_FAILED;
	}
	return rv;
}
