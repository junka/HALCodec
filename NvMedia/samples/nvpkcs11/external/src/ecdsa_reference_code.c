/*
 * Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 *
 */

#include "pkcs11_reference_apps_code.h"

#define TEMPLATE_NUM_ENTRIES(template) ((CK_ULONG)(sizeof(template) / sizeof(CK_ATTRIBUTE)))
#define ECDSA_MSG_SIZE 32 /* supported size is 32 bytes, i.e. digest output 256 bits */
#define ECDSA_X962_ID_SIZE 1U /* X962 id field size */

/*
 * Generating new ECDSA key pair (two key objects).
 *
 * hSession    [IN] - PKCS11 session handle
 * key_id      [IN] - private and public key object IDs (must be common for both)
 * priv_key_handle_ptr  [OUT] - pointer to private key handle
 * pub_key_handle_ptr   [OUT] - pointer to public key handle
 *
 * Returns:
 *   CKR_OK if successful and error message otherwise.
 */
static CK_RV generate_ECDSA_key_pair(
		CK_SESSION_HANDLE hSession,
		const CK_BYTE_PTR key_id,
		CK_OBJECT_HANDLE_PTR priv_key_handle_ptr,
		CK_OBJECT_HANDLE_PTR pub_key_handle_ptr)
{
	CK_RV rv;
	CK_BBOOL enabled = CK_TRUE;
	CK_MECHANISM_TYPE ecdsa_mech = CKM_ECDSA;

	ecParameters_t ec_params = {
		NVPKCS11_DER_PRINTABLE_IDENTIFIER,
		(CK_BYTE)strlen(NVPKCS11_ECDSA_SECP256R1_STRING),
		NVPKCS11_ECDSA_SECP256R1_STRING};
	CK_MECHANISM gen_mech = {CKM_EC_KEY_PAIR_GEN, NULL, 0};

	/* Public key */
	CK_ATTRIBUTE pub_key_template[] = {
		{CKA_ID, key_id, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &ecdsa_mech, sizeof(ecdsa_mech)},
		{CKA_EC_PARAMS, &ec_params, sizeof(ec_params)},
	};

	/* Private key */
	CK_ATTRIBUTE priv_key_template[] = {
		{CKA_ID, key_id, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_SIGN, &enabled, sizeof(CK_BBOOL)},
		{CKA_ALLOWED_MECHANISMS, &ecdsa_mech, sizeof(ecdsa_mech)},
	};

	rv = C_GenerateKeyPair(hSession, &gen_mech, pub_key_template, TEMPLATE_NUM_ENTRIES(pub_key_template),
			priv_key_template, TEMPLATE_NUM_ENTRIES(priv_key_template), pub_key_handle_ptr, priv_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "generate_ECDSA_key_pair", generate_ECDSA_key_pair_exit);

	printf("Generated ECDSA secp265r1 ephemeral key pair.\n");

generate_ECDSA_key_pair_exit:
	return rv;
}

CK_RV ecdsa_ephemeral_sign_verify_reference_code(CK_SESSION_HANDLE hSession)
{
	CK_RV rv = CKR_FUNCTION_FAILED;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;
	CK_OBJECT_HANDLE pub_key_handle_ptr = CK_INVALID_HANDLE;
	CK_OBJECT_HANDLE priv_key_handle_ptr = CK_INVALID_HANDLE;
	CK_MECHANISM ecdsa_mech = {CKM_ECDSA, NULL, 0};
	/* N.B.: ECDSA signature is in ASN.1 DER format, so it can be any size of 3..72 bytes */
	CK_BYTE signature[NVPKCS11_MAX_ECDSA_SECP256R1_SIGNATURE_SIZE]; /* 72U */
	CK_ULONG signature_len = NVPKCS11_MAX_ECDSA_SECP256R1_SIGNATURE_SIZE;
	CK_BYTE key0[NVPKCS11_MAX_KEY_ID_SIZE] = "ecdsa_key0                      ";
	CK_BYTE msg_buf[ECDSA_MSG_SIZE];

	printf("\n====================================================\n");
	printf("ECDSA:\n");

	/* Fill msg_buf with some testing data */
	for (unsigned int i = 0; i < ECDSA_MSG_SIZE; i++)
	{
		msg_buf[i] = i & 0xff;
	}

	rv = generate_ECDSA_key_pair(hSession, key0, &priv_key_handle_ptr, &pub_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "generate_ECDSA_key_pair", ecdsa_ephemeral_reference_code_exit);

	/* Sign */
	rv = C_SignInit(hSession, &ecdsa_mech, priv_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "C_SignInit", ecdsa_key_destroy);

	/* N.B.: ECDSA signature is in ASN.1 DER format */
	rv = C_Sign(hSession, msg_buf, ECDSA_MSG_SIZE, signature, &signature_len);
	GO_TO_LABEL_ON_ERROR(rv, "C_Sign", ecdsa_key_destroy);
	printf("Signature:\n");
	print_data(signature, signature_len);

	/* Verify */
	rv = C_VerifyInit(hSession, &ecdsa_mech, pub_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "C_VerifyInit", ecdsa_key_destroy);

	rv = C_Verify(hSession, msg_buf, ECDSA_MSG_SIZE, signature, signature_len);
	GO_TO_LABEL_ON_ERROR(rv, "C_Verify", ecdsa_key_destroy);

ecdsa_key_destroy:
	rv = C_DestroyObject(hSession, priv_key_handle_ptr);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(rv, "C_DestroyObject - priv_key_handle_ptr", failedFlag);

	rv = C_DestroyObject(hSession, pub_key_handle_ptr);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(rv, "C_DestroyObject - pub_key_handle_ptr", failedFlag);

ecdsa_ephemeral_reference_code_exit:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}

static CK_RV create_ecdsa_pub_key(
		CK_SESSION_HANDLE hSession,
		CK_BYTE_PTR key_val,
		const CK_BYTE_PTR key_id,
		const CK_BYTE_PTR key_label,
		CK_OBJECT_HANDLE_PTR key_h)
{
	CK_RV rv = CKR_FUNCTION_FAILED;
	CK_BBOOL enabled = CK_TRUE;
	CK_OBJECT_CLASS key_class = CKO_PUBLIC_KEY;
	CK_KEY_TYPE key_type = CKK_EC;
	CK_MECHANISM_TYPE mech_list = CKM_ECDSA;
	ecParameters_t ec_params;
	CK_UTF8CHAR const secp256r1_name_str[] = NVPKCS11_ECDSA_SECP256R1_STRING;
	uint8_t const secp_256r1_str_len = (uint8_t)(sizeof(secp256r1_name_str) - 1U);
	ec_params.identifier = NVPKCS11_DER_PRINTABLE_IDENTIFIER;
	ec_params.size = secp_256r1_str_len;
	memcpy(ec_params.printableString, secp256r1_name_str, secp_256r1_str_len);

	uncompressed32BytePoint_t ec_point = {
		NVPKCS11_DER_OCTET_IDENTIFIER,								/* identifier */
		(CK_BYTE)(ECDSA_X962_ID_SIZE + NVPKCS11_EC_256_POINT_SIZE + NVPKCS11_EC_256_POINT_SIZE),	/* size : size of x962id and keys */
		NVPKCS11_ECDSA_X962_UNCOMP_ID,								/* x962 type identifier */
		/* 0 values for qX and qY are invalid, will be updated later. */
		{0U},											/* qX - 32 bytes X coordinate */
		{0U},											/* qY - 32 bytes Y coordinate */
	};

	if (key_val == NULL)
	{
		fprintf(stderr, "NULL pointer provided to key_val");
		rv = CKR_FUNCTION_FAILED;
		goto create_ecdsa_pub_key_exit;
	}

	memcpy(ec_point.qX, key_val, NVPKCS11_ECDSA_256_KEY_SIZE); /* Qx 32 bytes */
	memcpy(ec_point.qY, &key_val[NVPKCS11_ECDSA_256_KEY_SIZE], NVPKCS11_ECDSA_256_KEY_SIZE); /* Qy 32 bytes */

	CK_ATTRIBUTE key_template[] = {
		{CKA_CLASS, &key_class, sizeof(key_class)},
		{CKA_KEY_TYPE, &key_type, sizeof(key_type)},
		{CKA_ID, key_id, NVPKCS11_MAX_KEY_ID_SIZE},
		{CKA_ALLOWED_MECHANISMS, &mech_list, sizeof(mech_list)},
		{CKA_VERIFY, &enabled, sizeof(CK_BBOOL)},
		{CKA_EC_PARAMS, &ec_params, sizeof(ec_params)},
		{CKA_EC_POINT, &ec_point, sizeof(ec_point)},
		{CKA_LABEL, key_label, NVPKCS11_MAX_CKA_LABEL_SIZE}};

	rv = C_CreateObject(hSession, key_template, sizeof(key_template) / sizeof(CK_ATTRIBUTE), key_h);
	GO_TO_LABEL_ON_ERROR(rv, "C_CreateObject", create_ecdsa_pub_key_exit);
	printf("ECDSA public key created with ID: %.32s\n", key_id);

create_ecdsa_pub_key_exit:
	return rv;
}

/**
 * Test creating a public key from OpenSSL's pre-generated public key, and verify a signature of a simple string's digest.
 *
 * Please note that NV PKCS#11 API accepts ECDSA sepc265r1 keys (Qx and Qy),
 * but signature must be ASN.1 encoded.
 *
 * The private and public key were generated with OpenSSL using the commands:
 *   openssl ecparam -name secp256r1 -genkey -noout -out priv_ecdsa_secp256r1.pem
 *   openssl pkey -in priv_ecdsa_secp256r1.pem -pubout -out pub_ecdsa_secp256r1.pem
 *
 * In the following test, these keys were used:
 *
 * -----BEGIN EC PRIVATE KEY-----
 * MHcCAQEEIOIt+0QafhywUvZrqtBV9O0Wi6hhCGZPT/+xVWE0J7w6oAoGCCqGSM49
 * AwEHoUQDQgAEWmlXFBO1rTP52nIrjWFPlJw8behG+LPnUKzvW7KPjubk5faVTuF0
 * rXDmuXvEtRiaQZJetOk95W8MzP3leJlO2g==
 * -----END EC PRIVATE KEY-----
 *
 * -----BEGIN PUBLIC KEY-----
 * MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEWmlXFBO1rTP52nIrjWFPlJw8behG
 * +LPnUKzvW7KPjubk5faVTuF0rXDmuXvEtRiaQZJetOk95W8MzP3leJlO2g==
 * -----END PUBLIC KEY-----
 *
 * Generate some data and its 32 byte sha-256 digest:
 *   echo "Hello, NVIDIA" > plain.txt
 *   openssl dgst -binary -sha256 -out sha256.bin plain.txt
 *   xxd -i sha256.bin
 *
 * Sign digest (sha256) of this data:
 *   openssl dgst -sha256 -sign priv_ecdsa_secp256r1.pem -out signature.bin plain.txt
 *   xxd -i signature.bin
 *
 * Generate public key in hex format:
 *   openssl pkey -pubin -in pub_ecdsa_secp256r1.pem -outform DER -out pub_ecdsa_secp256r1.der
 *   xxd -s 27 -i pub_ecdsa_secp256r1.der
 *
 * signature.bin: {
 *   0x30, 0x46, 0x02, 0x21, 0x00, 0x81, 0x56, 0xb1, 0x11, 0xee, 0xd0, 0xf1,
 *   0xca, 0xcc, 0xe4, 0xf3, 0x98, 0xef, 0xcc, 0xb6, 0xdb, 0x49, 0x9d, 0x4a,
 *   0x08, 0xd4, 0xd3, 0x35, 0xc0, 0x9e, 0x76, 0x90, 0xad, 0x84, 0x55, 0x65,
 *   0xbf, 0x02, 0x21, 0x00, 0xa3, 0xd4, 0x59, 0x4c, 0x36, 0x47, 0xdd, 0x30,
 *   0xc6, 0x2b, 0x6d, 0x49, 0xa2, 0xc2, 0xfb, 0x2b, 0x2f, 0x28, 0x73, 0x6a,
 *   0xd6, 0x12, 0x02, 0x6e, 0xa9, 0x2f, 0x32, 0x1d, 0xb2, 0x4b, 0xf5, 0x17
 * }
 *
 * sha256.bin: {
 *   0xff, 0xd9, 0x95, 0x0a, 0xc1, 0xb2, 0x6f, 0xc6, 0xdb, 0xf1, 0xcd, 0x78,
 *   0x3d, 0x06, 0xda, 0xde, 0x83, 0xe2, 0xe8, 0xe4, 0xf7, 0xad, 0xaf, 0x4a,
 *   0x44, 0xa0, 0xbf, 0x60, 0xdd, 0x40, 0x32, 0xe7
 * }
 *
 * pub_ecdsa_secp256r1.der: {
 *   0x5a, 0x69, 0x57, 0x14, 0x13, 0xb5, 0xad, 0x33, 0xf9, 0xda, 0x72, 0x2b,
 *   0x8d, 0x61, 0x4f, 0x94, 0x9c, 0x3c, 0x6d, 0xe8, 0x46, 0xf8, 0xb3, 0xe7,
 *   0x50, 0xac, 0xef, 0x5b, 0xb2, 0x8f, 0x8e, 0xe6, 0xe4, 0xe5, 0xf6, 0x95,
 *   0x4e, 0xe1, 0x74, 0xad, 0x70, 0xe6, 0xb9, 0x7b, 0xc4, 0xb5, 0x18, 0x9a,
 *   0x41, 0x92, 0x5e, 0xb4, 0xe9, 0x3d, 0xe5, 0x6f, 0x0c, 0xcc, 0xfd, 0xe5,
 *   0x78, 0x99, 0x4e, 0xda
 * }
 *
 */
CK_RV verify_ecdsa_openssl_reference_code(CK_SESSION_HANDLE hSession)
{
	/* Pub key in raw Qx:Qy format: */
	unsigned char pub_ecdsa_secp256r1_der[] = {
		0x5a, 0x69, 0x57, 0x14, 0x13, 0xb5, 0xad, 0x33, 0xf9, 0xda, 0x72, 0x2b, 0x8d,
		0x61, 0x4f, 0x94, 0x9c, 0x3c, 0x6d, 0xe8, 0x46, 0xf8, 0xb3, 0xe7, 0x50, 0xac,
		0xef, 0x5b, 0xb2, 0x8f, 0x8e, 0xe6, 0xe4, 0xe5, 0xf6, 0x95, 0x4e, 0xe1, 0x74,
		0xad, 0x70, 0xe6, 0xb9, 0x7b, 0xc4, 0xb5, 0x18, 0x9a, 0x41, 0x92, 0x5e, 0xb4,
		0xe9, 0x3d, 0xe5, 0x6f, 0x0c, 0xcc, 0xfd, 0xe5, 0x78, 0x99, 0x4e, 0xda};

	/* 32 bytes of data to be signed (digest): */
	unsigned char sha256_data[] = {
		0xff, 0xd9, 0x95, 0x0a, 0xc1, 0xb2, 0x6f, 0xc6, 0xdb, 0xf1, 0xcd,
		0x78, 0x3d, 0x06, 0xda, 0xde, 0x83, 0xe2, 0xe8, 0xe4, 0xf7, 0xad,
		0xaf, 0x4a, 0x44, 0xa0, 0xbf, 0x60, 0xdd, 0x40, 0x32, 0xe7};
	unsigned int sha256_data_len = 32;

	/* Signature in ASN.1 format: */
	unsigned char expected_signature[] = {
		0x30, 0x46, 0x02, 0x21, 0x00, 0x81, 0x56, 0xb1, 0x11, 0xee, 0xd0, 0xf1, 0xca, 0xcc, 0xe4,
		0xf3, 0x98, 0xef, 0xcc, 0xb6, 0xdb, 0x49, 0x9d, 0x4a, 0x08, 0xd4, 0xd3, 0x35, 0xc0, 0x9e,
		0x76, 0x90, 0xad, 0x84, 0x55, 0x65, 0xbf, 0x02, 0x21, 0x00, 0xa3, 0xd4, 0x59, 0x4c, 0x36,
		0x47, 0xdd, 0x30, 0xc6, 0x2b, 0x6d, 0x49, 0xa2, 0xc2, 0xfb, 0x2b, 0x2f, 0x28, 0x73, 0x6a,
		0xd6, 0x12, 0x02, 0x6e, 0xa9, 0x2f, 0x32, 0x1d, 0xb2, 0x4b, 0xf5, 0x17};
	unsigned int expected_signature_len = 72;

	CK_RV rv = CKR_FUNCTION_FAILED;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;
	CK_OBJECT_HANDLE pub_key_handle_ptr = CK_INVALID_HANDLE;
	CK_MECHANISM ecdsa_mech = {CKM_ECDSA, NULL, 0};
	CK_BYTE key_id[NVPKCS11_MAX_KEY_ID_SIZE] = "ecdsa_ephem2                    ";
	CK_BYTE key_label[NVPKCS11_MAX_CKA_LABEL_SIZE] = "key_label2                      ";

	printf("\n====================================================\n");
	printf("Verify ECDSA OpenSSL:\n");

	/* Create ephemeral key object */
	rv = create_ecdsa_pub_key(hSession, pub_ecdsa_secp256r1_der, key_id, key_label, &pub_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "create_ecdsa_pub_key", verify_ecdsa_openssl_reference_code_exit);

	/* Verify */
	rv = C_VerifyInit(hSession, &ecdsa_mech, pub_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "C_VerifyInit", verify_ecdsa_openssl_destroy_key);

	rv = C_Verify(hSession, sha256_data, sha256_data_len, expected_signature, expected_signature_len);
	GO_TO_LABEL_ON_ERROR(rv, "C_Verify", verify_ecdsa_openssl_destroy_key);

	printf("ECDSA sepc265r1 Verify OK.\n");

verify_ecdsa_openssl_destroy_key:
	rv = C_DestroyObject(hSession, pub_key_handle_ptr);
	GO_TO_LABEL_ON_ERROR(rv, "C_DestroyObject", verify_ecdsa_openssl_reference_code_exit);

verify_ecdsa_openssl_reference_code_exit:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		rv = CKR_FUNCTION_FAILED;
	}

	return rv;
}
