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

#include "pkcs11_reference_apps_code.h"
#include "pkcs11_test_data.h"

static CK_RV pkcs11_open_session_with_channel(CK_SESSION_HANDLE_PTR phSession, CK_SLOT_ID ccplex_slot_id, CK_NVIDIA_CHANNEL_HANDLE_PTR phShaChannel, CK_NVIDIA_CHANNEL_HANDLE_PTR phAesChannel)
{
	CK_RV rv;
	CK_NVIDIA_FLAGS nvFlagsSha;

	printf("\n====================================================\n");
	printf("NVIDIA CHANNEL:\n");

	/**
	 * C_NVIDIA_InitializeChannel:
	 * Opens a channel to a hardware engine.
	 * In this case it will initialise a channel to the sha1 engine,
	 * this is determined by the variable ulChannelIdSha containing the GID
	 * for the sha1 engine.
	 *
	 * A pointer to store the channel handle is passed to the function.
	 * Upon successfully opening the channel the handle can then be used to open a
	 * PKCS11 session.
	 *
	 * The flag pointer can return information associated with the channel.
	 * No information will be returned for the sha1 engine channel
	 */
#ifdef __QNX__ /* TZ-SE and CKM_SHA256_HMAC not supported on Linux */
	const CK_ULONG ulChannelIdSha = 7109U;
	rv = C_NVIDIA_InitializeChannel(ulChannelIdSha, phShaChannel, &nvFlagsSha);
	GO_TO_LABEL_ON_ERROR(rv, "C_NVIDIA_InitializeChannel", pkcs11_open_session_with_channel_exit);
	printf("Channel opened on ID: %luU\n", ulChannelIdSha);
#endif

	/* Initialize a channel that allows up to 16MB-1 AES GCM operations */
	const CK_ULONG ulChannelIdAes16MB = 7143U;
	rv = C_NVIDIA_InitializeChannel(ulChannelIdAes16MB, phAesChannel, &nvFlagsSha);
	GO_TO_LABEL_ON_ERROR(rv, "C_NVIDIA_InitializeChannel", pkcs11_open_session_with_channel_exit);
	printf("Channel opened on ID: %luU\n", ulChannelIdAes16MB);

	CK_NVIDIA_CHANNEL_ATTRIBUTE channel_template[] =
	{
#ifdef __QNX__ /* TZ-SE and CKM_SHA256_HMAC not supported on Linux */
		{(CKF_DIGEST | CKF_NVIDIA_HMAC_SIGN | CKF_NVIDIA_HMAC_VERIFY), *phShaChannel},
#endif
		{(CKF_MESSAGE_SIGN | CKF_MESSAGE_VERIFY), *phAesChannel},
	};

	/* Read only session */
	CK_STATE session_flags = CKF_SERIAL_SESSION;

	/**
	 * C_NVIDIA_OpenSession:
	 * This function extends the functionality of the standard C_OpenSession API
	 * call to allow channels to be configured for a session.
	 *
	 * In this example we have initialised a channel to the sha1 engine.
	 * C_NVIDIA_OpenSession allows us to open a session that will use the sha engine
	 * for specific operations.
	 *
	 * The operations and engine channel have been specified within the sha_channel_template
	 * channel attribute template.
	 */
	rv = C_NVIDIA_OpenSession(
			ccplex_slot_id,
			session_flags,
			NULL,
			0U,
			phSession,
			channel_template,
			sizeof(channel_template)/sizeof(channel_template[0]),
			0U);
	GO_TO_LABEL_ON_ERROR(rv, "C_NVIDIA_OpenSession", pkcs11_open_session_with_channel_exit);
	printf("Custom session opened successfully\n");

pkcs11_open_session_with_channel_exit:
	return rv;
}

static CK_RV log_in(CK_SESSION_HANDLE hSession)
{
	CK_USER_TYPE user_type = CKU_USER; /* Set to normal user */
	CK_UTF8CHAR_PTR pin_ptr = NULL; /* No PIN */
	CK_ULONG pin_len = 0UL; /* No PIN used so zero length */

	return C_Login(hSession, user_type, pin_ptr, pin_len);
}

/**
 * A reference code which invokes a list
 * of APIs to perform the initialization
 * and slot related info.
 * C_Initialize
 * C_GetSlotList
 * C_OpenSession
 * C_NVIDIA_InitializeChannel
 * C_NVIDIA_OpenSession
 *
 * @param [in,out] phSession pointer to the handle for the new session
 */
static CK_RV pkcs11_init(CK_SESSION_HANDLE_PTR phSession, CK_SESSION_HANDLE_PTR phChannelSession, CK_NVIDIA_CHANNEL_HANDLE_PTR phShaChannel, CK_NVIDIA_CHANNEL_HANDLE_PTR phAesChannel)
{
	CK_RV rv;

	CK_BBOOL token_present = (CK_BBOOL)CK_TRUE;
	CK_BBOOL token_found = (CK_BBOOL)CK_FALSE;
	CK_SLOT_ID slot_ids[NVPKCS11_TOKEN_COUNT];
	CK_ULONG slot_count = NVPKCS11_TOKEN_COUNT;
	CK_SLOT_ID ccplex_dynamic_slot_id = 0U;

	/**
	 * a CK_C_INITIALIZE_ARGS structure containing information on how the library should
	 * deal with multi-threaded access as per section: 5.4 General purpose functions
	 *
	 * For Multi-threading support, the CK library only supports the following option:
	 * If the flag is set CKF_OS_LOCKING_OK, and the function pointer fields aren't supplied
	 * (i.e., they all have the value NULL), that means that the application will be performing
	 * multi-threaded Cryptoki access, and the library needs to use the native operating system
	 * primitives to ensure safe multi-threaded access.
	 * pReserved Should be NULL for this version of Cryptoki
	 */
	CK_C_INITIALIZE_ARGS init_args =
	{
		.CreateMutex = NULL,
		.DestroyMutex = NULL,
		.LockMutex = NULL,
		.flags = (CK_FLAGS)CKF_OS_LOCKING_OK,
		.pReserved = NULL
	};

	/** Initialise Cryptoki library */
	rv = C_Initialize(&init_args);
	GO_TO_LABEL_ON_ERROR(rv, "C_Initialize", reference_init_code_exit);

	/** GetSlotList: obtain a list of slots in the system.
	 * token_present is set to CK_TRUE to indicate the list
	 * obtained includes only those slots with a token
	 * present.
	 */
	rv = C_GetSlotList(token_present, slot_ids, &slot_count);
	GO_TO_LABEL_ON_ERROR(rv, "C_GetSlotList", finalize);

	/** GetTokenInfo: Can be used to identify and open a token that matches a
	 * given model name and establish the status of a token's secure storage
	 * as well as the status of a token itself.
	 */
	for (CK_ULONG i = 0; i < slot_count; i++)
	{
		CK_TOKEN_INFO token_info;
		rv = C_GetTokenInfo(slot_ids[i], &token_info);
		if (rv == CKR_OK)
		{
			/* Check NVPKCS11_CCPLEX_DYNAMIC_2_MODEL_NAME token status. This can be changed to match any other available token model */
			if (strncmp((char *)token_info.model, NVPKCS11_CCPLEX_DYNAMIC_2_MODEL_NAME, sizeof(token_info.model)) == 0)
			{
				ccplex_dynamic_slot_id = slot_ids[i];
				token_found = (CK_BBOOL)CK_TRUE;

				printf("\n====================================================\n");
				printf("TOKEN STATUS:\n");

				if ((CKF_NVIDIA_TOKEN_OK & token_info.flags) == CKF_NVIDIA_TOKEN_OK)
				{
					printf("CKF_NVIDIA_TOKEN_OK\n");
					if (token_info.ulMaxRwSessionCount != CK_UNAVAILABLE_INFORMATION)
					{
						printf("Secure Storage is available on this token with R/W capabilities\n");
					}
				}
				if ((CKF_NVIDIA_SECURE_STORAGE_FAILED & token_info.flags) == CKF_NVIDIA_SECURE_STORAGE_FAILED)
				{
					printf("CKF_NVIDIA_SECURE_STORAGE_FAILED\n");
				}
				if ((CKF_NVIDIA_SECURE_STORAGE_TAMPERED & token_info.flags) == CKF_NVIDIA_SECURE_STORAGE_TAMPERED)
				{
					printf("CKF_NVIDIA_SECURE_STORAGE_TAMPERED\n");
				}
				if ((CKF_NVIDIA_KEYLOAD_TIMEOUT & token_info.flags) == CKF_NVIDIA_KEYLOAD_TIMEOUT)
				{
					printf("CKF_NVIDIA_KEYLOAD_TIMEOUT\n");
				}
				if ((CKF_NVIDIA_KEYLOAD_FAILED & token_info.flags) == CKF_NVIDIA_KEYLOAD_FAILED)
				{
					printf("CKF_NVIDIA_KEYLOAD_FAILED\n");
				}
				if ((CKF_NVIDIA_TOKEN_ERROR & token_info.flags) == CKF_NVIDIA_TOKEN_ERROR)
				{
					printf("CKF_NVIDIA_TOKEN_ERROR\n");
				}
				if ((CKF_NVIDIA_SECURE_STORAGE_NOT_PROVISIONED & token_info.flags) == CKF_NVIDIA_SECURE_STORAGE_NOT_PROVISIONED)
				{
					printf("CKF_NVIDIA_SECURE_STORAGE_NOT_PROVISIONED\n");
				}
				if ((CKF_NVIDIA_SECURE_STORAGE_NOT_PRESENT & token_info.flags) == CKF_NVIDIA_SECURE_STORAGE_NOT_PRESENT)
				{
					printf("CKF_NVIDIA_SECURE_STORAGE_NOT_PRESENT\n");
				}
				break;
			}
		}
	}

	if (token_found == (CK_BBOOL)CK_FALSE)
	{
		printf("Could not find requested token\n");
		rv = CKR_TOKEN_NOT_PRESENT;
		goto finalize;
	}

	/**
	 * C_OpenSession:
	 * pApplication and Notify are set to NULL
	 * since callback functionality is not supported.
	 * This API could be called multiple times (once per thread),
	 * however, C_Initialize and C_GetSlotList need only be called
	 * once, given slot_id and slot_count are not NULL.
	 * Open the session as read only public session and log
	 * in as CKU_USER should you require access to the objects
	 * which are CKA_PRIVATE.
	 *
	 * To open a read write session, the flag CKF_RW_SESSION must be set.
	 * The board must have secure storage capabilities and a dynamic
	 * token must be selected.
	 */
	CK_STATE session_flags = CKF_SERIAL_SESSION; /* Read only session */
#ifdef TOKEN_OBJECT_EXAMPLE
	session_flags = (session_flags | CKF_RW_SESSION); /* Read write session */
#endif
	rv = C_OpenSession(
			ccplex_dynamic_slot_id,
			session_flags,
			NULL,
			NULL,
			phSession);
	GO_TO_LABEL_ON_ERROR(rv, "C_OpenSession", finalize);

	/**
	 * pkcs11_open_session_with_channel:
	 * This function provides an example of how to open a session to target the use of specific
	 * hardware using extensions C_NVIDIA_InitializeChannel() and C_NVIDIA_OpenSession().
	 * Note that this API is optional; C_OpenSession will bind to a default channel.
	 * The custom extensions allow binding to specific channels for performance and to fine-tune
	 * the engine workloads.
	 *
	 * A channel to the hardware must first be initialised. A GID to the target hardware must
	 * be provided. This example uses two GIDs to open up two channels. The channel handles
	 * will be returned with phShaChannel and phAesChannel.
	 *
	 * A session will then be created with the provided channel handles as well as flags to specify the
	 * channel specific cryptographic operations. The session handle will be returned with phChannelSession.
	 * By providing PKCS11 API with this session handle, the allowed operations can be performed on
	 * the targeted hardware.
	 *
	 * A channel can be closed at the de-init stage, if there are no sessions configured to use that channel.
	 */
	rv = pkcs11_open_session_with_channel(phChannelSession, ccplex_dynamic_slot_id, phShaChannel, phAesChannel);
	GO_TO_LABEL_ON_ERROR(rv, "pkcs11_open_session_with_channel", finalize);

finalize:
	if (rv != CKR_OK)
	{
		CK_RV finalizeRv = C_Finalize(NULL);
		GO_TO_LABEL_ON_ERROR(finalizeRv, "C_Finalize", reference_init_code_exit);
	}

reference_init_code_exit:
	return rv;
}

int32_t main(int argc, char ** argv)
{
	CK_RV rv;
	CK_RV clean_up_rv;
	CK_BBOOL failedFlag = (CK_BBOOL)CK_FALSE;
	CK_SESSION_HANDLE hSession = CK_INVALID_HANDLE;
	CK_SESSION_HANDLE hChannelSession = CK_INVALID_HANDLE;
	CK_NVIDIA_CHANNEL_HANDLE hShaChannel = CK_INVALID_HANDLE;
	CK_NVIDIA_CHANNEL_HANDLE hAesChannel = CK_INVALID_HANDLE;
	CK_ULONG data_len = 0U;
	CK_ULONG digest_max_data_len = 0U;
	/* Final rv to be returned by main() */
	int32_t ret = EXIT_SUCCESS;

	rv = pkcs11_init(&hSession, &hChannelSession, &hShaChannel, &hAesChannel);
	GO_TO_LABEL_ON_ERROR(rv, "pkcs11_init", end);

	/** Perform digest operations using the standard session handle */
	data_len = sizeof(in_data);
	/** Maximum input data size limits for any operations using GP-SE or TZ-SE (QNX only)
	 *  cryptographic hardware are determined by the max_buf_size and gcmdec_buf_size configuration
	 *  parameters described in the "VSE IVC Queue Configuration" topic in the NVIDIA DRIVE OS QNX
	 *  SDK Developer Guide.
	 *  The max digest buffer size for this channel is 2MB
	 */
	digest_max_data_len = 0x200000U;
	rv = digest_reference_code(hSession, in_data, data_len, digest_max_data_len);
	GO_TO_LABEL_ON_ERROR(rv, "digest_reference_code", clean_up);

	/**
	 * Log the user into a token
	 */
	rv = log_in(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "log_in", clean_up);

#ifdef __QNX__ /* TZ-SE and CKM_SHA256_HMAC not supported on Linux */

	/** Perform digest operations using the session configured to use a different channel for SHA operations */
	/**  The max digest buffer size for this channel is 8MB */
	digest_max_data_len = 0x800000U;
	rv = digest_reference_code(hChannelSession, in_data, data_len, digest_max_data_len);
	GO_TO_LABEL_ON_ERROR(rv, "digest_reference_code - SHA channel", clean_up);

	/* Derive a SHA256 HMAC key and use it to sign and verify the provided data */
	rv = hmac_reference_code(hSession, in_data, data_len);
	GO_TO_LABEL_ON_ERROR(rv, "hmac_reference_code", clean_up);

	/* Perform HMAC operations using the session configured to use a SHA engine for HMAC operations */
	rv = hmac_reference_code(hChannelSession, in_data, data_len);
	GO_TO_LABEL_ON_ERROR(rv, "hmac_reference_code", clean_up);
#endif

	/* Generate 1MB of random data, then sign and verify it using GMAC */
	rv = gmac_reference_code(hSession, (1 * MB));
	GO_TO_LABEL_ON_ERROR(rv, "gmac_reference_code - 1MB", clean_up);

	/* Generate 16MB-1 of random data, then sign and verify it using GMAC */
	rv = gmac_reference_code(hChannelSession, ((16 * MB) - 1));
	GO_TO_LABEL_ON_ERROR(rv, "gmac_reference_code - 16MB-1", clean_up);

	/* Encrypt using GCM, then use cipher text, IV and TAG to decrypt */
	rv = gcm_encrypt_decrypt(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "gcm_encrypt_decrypt", clean_up);

	/* TLS and GCM enc/dec example usage of CKA_NVIDIA_CALLER_NONCE */
	rv = tls_gcm_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "tls_gcm_reference_code", clean_up);

	/* Create an EDDSA public key and use it to verify a signed message */
	rv = eddsa_verify_reference_code(hSession, ed25519_pre_hash, sizeof(ed25519_pre_hash), ed25519ph_signature, sizeof(ed25519ph_signature));
	GO_TO_LABEL_ON_ERROR(rv, "eddsa_verify_reference_code", clean_up);

	/* Generate ECDSA key pair and sign/verify */
	rv = ecdsa_ephemeral_sign_verify_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "ecdsa_ephemeral_sign_verify_reference_code", clean_up);

	/* Create a public key from plain text (pre-generated by OpenSSL), and verify a signature of a simple string's digest. */
	rv = verify_ecdsa_openssl_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "verify_ecdsa_openssl_reference_code", clean_up);

	/* Wrap and unwrap a key using CBC */
	rv = cbc_iv_wrap_unwrap_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "cbc_iv_wrap_unwrap_reference_code", clean_up);

	/* Commit a key to secure storage */
	rv = token_object_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "token_object_reference_code", clean_up);

	/* Wrap two keys and additional data using CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP  */
	rv = nvidia_aes_cbc_key_data_wrap_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "nvidia_aes_cbc_key_data_wrap_reference_code", clean_up);

	/* Unwrap a key using GCM */
	rv = gcm_unwrap_reference_code(hSession);
	GO_TO_LABEL_ON_ERROR(rv, "gcm_unwrap_reference_code", clean_up);

clean_up:

	/** Log the user out from a token */
	clean_up_rv = C_Logout(hSession);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(clean_up_rv, "C_Logout", failedFlag);

	/** close existing opened session and issue C_Finalize */
	clean_up_rv = C_CloseSession(hSession);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(clean_up_rv, "C_CloseSession", failedFlag);

	clean_up_rv = C_CloseSession(hChannelSession);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(clean_up_rv, "C_CloseSession", failedFlag);

	clean_up_rv = C_NVIDIA_FinalizeChannel(hAesChannel);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(clean_up_rv, "C_NVIDIA_FinalizeChannel", failedFlag);

#ifdef __QNX__ /* TZ-SE and CKM_SHA256_HMAC not supported on Linux */
	/* Close the open channel on the inactive session */
	clean_up_rv = C_NVIDIA_FinalizeChannel(hShaChannel);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(clean_up_rv, "C_NVIDIA_FinalizeChannel", failedFlag);
#endif

	clean_up_rv = C_Finalize(NULL);
	PRINT_MESSAGE_ON_ERROR_CONTINUE(clean_up_rv, "C_Finalize", failedFlag);
end:
	if ((rv != CKR_OK) || (failedFlag == (CK_BBOOL)CK_TRUE))
	{
		printf("\n********** Overall run completed with failure **********\n");
		ret = EXIT_FAILURE;
	}
	else
	{
		printf("\n********** Overall run completed with success **********\n");
	}
	return ret;
}
