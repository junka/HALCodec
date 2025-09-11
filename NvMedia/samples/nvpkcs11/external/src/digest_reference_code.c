/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
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
 * perform single or multi-part digest operations:
 * C_DigestInit: to initialize a message-digest
 * operation, this is for single and multi-part digest.
 * Note that multi-part will be slower as this will invoke
 * 1 or more calls to C_DigestUpdate() before calling C_DigestFinal
 * to obtain the digest.
 */
static CK_RV digest_operation(
		CK_SESSION_HANDLE hSession,
		CK_BYTE_PTR in_data_ptr,
		CK_ULONG data_len,
		CK_BYTE_PTR digest_ptr,
		CK_ULONG_PTR digest_len_ptr,
		CK_MECHANISM_PTR mechanism_digest_ptr,
		CK_ULONG digest_max_data_len)
{
	/** Initialize for digest operation */
	CK_RV rv = C_DigestInit(hSession, mechanism_digest_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "C_DigestInit", digest_operation_exit);

	/**
	 *  If data_len is <= digest_max_data_len
	 *  then one single-part digest operation, otherwise
	 *  multi-part digest operation which will invoke one
	 *  or more C_DigestUpdate() followed, if successful,
	 *  by C_DigestFinal to obtain the digest.
	 */
	if (data_len <= digest_max_data_len)
	{
		rv = C_Digest(hSession,
				in_data_ptr,
				data_len,
				digest_ptr,
				digest_len_ptr);
		GO_TO_LABEL_ON_ERROR(rv, "C_Digest", digest_operation_exit);
	}
	else
	{
		/**
		 * Multi-part digest:
		 * While data available is > total data sent
		 * send data in segments of digest_max_data_len length
		 * if the remaining data is > digest_max_data_len
		 * otherwise, it must be the last segment.
		 */
		CK_ULONG part_len = digest_max_data_len;
		CK_ULONG digested_data_len = (CK_ULONG)0;

		while (digested_data_len < data_len)
		{
			/** the last block might be less than digest_max_data_len */
			if ((data_len - digested_data_len) < digest_max_data_len)
			{
				/** Last data chunk */
				part_len = data_len - digested_data_len;
			}
			/** C_DigestUpdate for multiple-part message-digesting operation */
			rv = C_DigestUpdate(hSession, &in_data_ptr[digested_data_len], part_len);
			GO_TO_LABEL_ON_ERROR(rv, "C_DigestUpdate", digest_operation_exit);
			digested_data_len += part_len;
		}
		/** C_DigestFinal finishes a multiple-part message-digesting operation */
		rv = C_DigestFinal(hSession,
				digest_ptr,
				digest_len_ptr);
		GO_TO_LABEL_ON_ERROR(rv, "C_DigestFinal", digest_operation_exit);
	}

digest_operation_exit:
	return rv;
}

CK_RV digest_reference_code(CK_SESSION_HANDLE hSession, CK_BYTE_PTR in_data_ptr, CK_ULONG data_len, CK_ULONG digest_max_data_len)
{
	CK_RV rv;
	CK_ULONG digest_len = 0UL;
	/** Digest buffer for SHA 256 */
	CK_BYTE digest_SHA_256[NVPKCS11_SHA256_DIGEST_SIZE] = { 0x00 };
	/** Digest buffer for SHA 512 */
	CK_BYTE digest_SHA_512[NVPKCS11_SHA512_DIGEST_SIZE] = { 0x00 };

	CK_VOID_PTR parameter_ptr = NULL;
	CK_ULONG parameter_len = (CK_ULONG)0;
	/**
	 * The digest mechanisms for both SHA256 and SHA512.
	 */
	CK_MECHANISM mechanism_digest_sha_256 = {CKM_SHA256, parameter_ptr, parameter_len};
	CK_MECHANISM mechanism_digest_sha_512 = {CKM_SHA512, parameter_ptr, parameter_len};

	/**
	 * run the operation for CKM_SHA256 mechanism.
	 */
	digest_len = NVPKCS11_SHA256_DIGEST_SIZE;
	rv = digest_operation(
			hSession,
			in_data_ptr,
			data_len,
			digest_SHA_256,
			&digest_len,
			&mechanism_digest_sha_256,
			digest_max_data_len);
	GO_TO_LABEL_ON_ERROR(rv, "digest_operation", digest_reference_code_exit);

	printf("\n====================================================\n");
	printf("DIGEST:\n");

	printf("Computed digest using SHA256:\n");
	print_data(digest_SHA_256, digest_len);

	/**
	 * run the operation for CKM_SHA512 mechanism.
	 */
	digest_len = NVPKCS11_SHA512_DIGEST_SIZE;
	rv = digest_operation(
			hSession,
			in_data_ptr,
			data_len,
			digest_SHA_512,
			&digest_len,
			&mechanism_digest_sha_512,
			digest_max_data_len);
	GO_TO_LABEL_ON_ERROR(rv, "digest_operation", digest_reference_code_exit);

	printf("\nComputed digest using SHA512:\n");
	print_data(digest_SHA_512, digest_len);

digest_reference_code_exit:
	return rv;
}
