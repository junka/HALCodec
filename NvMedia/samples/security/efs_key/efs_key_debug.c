/* ***************************************************************************** *
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 ******************************************************************************* */

#include <stdio.h>
#include "efs_key_debug.h"

void RetCode2String(CK_RV rc)
{
	char *str = NULL;

	switch (rc)
	{
		case CKR_OK:	str = "CKR_OK"; break;
		case CKR_CANCEL:	str = "CKR_CANCEL"; break;
		case CKR_HOST_MEMORY:	str = "CKR_HOST_MEMORY"; break;
		case CKR_SLOT_ID_INVALID:	str = "CKR_SLOT_ID_INVALID"; break;
		case CKR_GENERAL_ERROR:	str = "CKR_GENERAL_ERROR"; break;
		case CKR_FUNCTION_FAILED:	str = "CKR_FUNCTION_FAILED"; break;
		case CKR_ARGUMENTS_BAD:	str = "CKR_ARGUMENTS_BAD"; break;
		case CKR_NO_EVENT:	str = "CKR_NO_EVENT"; break;
		case CKR_NEED_TO_CREATE_THREADS:	str = "CKR_NEED_TO_CREATE_THREADS"; break;
		case CKR_CANT_LOCK:	str = "CKR_CANT_LOCK"; break;
		case CKR_ATTRIBUTE_READ_ONLY:	str = "CKR_ATTRIBUTE_READ_ONLY"; break;
		case CKR_ATTRIBUTE_SENSITIVE:	str = "CKR_ATTRIBUTE_SENSITIVE"; break;
		case CKR_ATTRIBUTE_TYPE_INVALID:	str = "CKR_ATTRIBUTE_TYPE_INVALID"; break;
		case CKR_ATTRIBUTE_VALUE_INVALID:	str = "CKR_ATTRIBUTE_VALUE_INVALID"; break;
		case CKR_DATA_INVALID:	str = "CKR_DATA_INVALID"; break;
		case CKR_DATA_LEN_RANGE:	str = "CKR_DATA_LEN_RANGE"; break;
		case CKR_DEVICE_ERROR:	str = "CKR_DEVICE_ERROR"; break;
		case CKR_DEVICE_MEMORY:	str = "CKR_DEVICE_MEMORY"; break;
		case CKR_DEVICE_REMOVED:	str = "CKR_DEVICE_REMOVED"; break;
		case CKR_ENCRYPTED_DATA_INVALID:	str = "CKR_ENCRYPTED_DATA_INVALID"; break;
		case CKR_ENCRYPTED_DATA_LEN_RANGE:	str = "CKR_ENCRYPTED_DATA_LEN_RANGE"; break;
		case CKR_FUNCTION_CANCELED:	str = "CKR_FUNCTION_CANCELED"; break;
		case CKR_FUNCTION_NOT_PARALLEL:	str = "CKR_FUNCTION_NOT_PARALLEL"; break;
		case CKR_FUNCTION_NOT_SUPPORTED:	str = "CKR_FUNCTION_NOT_SUPPORTED"; break;
		case CKR_KEY_HANDLE_INVALID:	str = "CKR_KEY_HANDLE_INVALID"; break;
		case CKR_KEY_SIZE_RANGE:	str = "CKR_KEY_SIZE_RANGE"; break;
		case CKR_KEY_TYPE_INCONSISTENT:	str = "CKR_KEY_TYPE_INCONSISTENT"; break;
		case CKR_KEY_NOT_NEEDED:	str = "CKR_KEY_NOT_NEEDED"; break;
		case CKR_KEY_CHANGED:	str = "CKR_KEY_CHANGED"; break;
		case CKR_KEY_NEEDED:	str = "CKR_KEY_NEEDED"; break;
		case CKR_KEY_INDIGESTIBLE:	str = "CKR_KEY_INDIGESTIBLE"; break;
		case CKR_KEY_FUNCTION_NOT_PERMITTED:	str = "CKR_KEY_FUNCTION_NOT_PERMITTED"; break;
		case CKR_KEY_NOT_WRAPPABLE:	str = "CKR_KEY_NOT_WRAPPABLE"; break;
		case CKR_KEY_UNEXTRACTABLE:	str = "CKR_KEY_UNEXTRACTABLE"; break;
		case CKR_MECHANISM_INVALID:	str = "CKR_MECHANISM_INVALID"; break;
		case CKR_MECHANISM_PARAM_INVALID:	str = "CKR_MECHANISM_PARAM_INVALID"; break;
		case CKR_OBJECT_HANDLE_INVALID:	str = "CKR_OBJECT_HANDLE_INVALID"; break;
		case CKR_OPERATION_ACTIVE:	str = "CKR_OPERATION_ACTIVE"; break;
		case CKR_OPERATION_NOT_INITIALIZED:	str = "CKR_OPERATION_NOT_INITIALIZED"; break;
		case CKR_PIN_INCORRECT:	str = "CKR_PIN_INCORRECT"; break;
		case CKR_PIN_INVALID:	str = "CKR_PIN_INVALID"; break;
		case CKR_PIN_LEN_RANGE:	str = "CKR_PIN_LEN_RANGE"; break;
		case CKR_PIN_EXPIRED:	str = "CKR_PIN_EXPIRED"; break;
		case CKR_PIN_LOCKED:	str = "CKR_PIN_LOCKED"; break;
		case CKR_SESSION_CLOSED:	str = "CKR_SESSION_CLOSED"; break;
		case CKR_SESSION_COUNT:	str = "CKR_SESSION_COUNT"; break;
		case CKR_SESSION_HANDLE_INVALID:	str = "CKR_SESSION_HANDLE_INVALID"; break;
		case CKR_SESSION_PARALLEL_NOT_SUPPORTED:	str = "CKR_SESSION_PARALLEL_NOT_SUPPORTED"; break;
		case CKR_SESSION_READ_ONLY:	str = "CKR_SESSION_READ_ONLY"; break;
		case CKR_SESSION_EXISTS:	str = "CKR_SESSION_EXISTS"; break;
		case CKR_SESSION_READ_ONLY_EXISTS:	str = "CKR_SESSION_READ_ONLY_EXISTS"; break;
		case CKR_SESSION_READ_WRITE_SO_EXISTS:	str = "CKR_SESSION_READ_WRITE_SO_EXISTS"; break;
		case CKR_SIGNATURE_INVALID:	str = "CKR_SIGNATURE_INVALID"; break;
		case CKR_SIGNATURE_LEN_RANGE:	str = "CKR_SIGNATURE_LEN_RANGE"; break;
		case CKR_TEMPLATE_INCOMPLETE:	str = "CKR_TEMPLATE_INCOMPLETE"; break;
		case CKR_TEMPLATE_INCONSISTENT:	str = "CKR_TEMPLATE_INCONSISTENT"; break;
		case CKR_TOKEN_NOT_PRESENT:	str = "CKR_TOKEN_NOT_PRESENT - ICSF is not active or not configured for TKDS operations"; break;
		case CKR_TOKEN_NOT_RECOGNIZED:	str = "CKR_TOKEN_NOT_RECOGNIZED - You are not authorized to perform the token operation"; break;
		case CKR_TOKEN_WRITE_PROTECTED:	str = "CKR_TOKEN_WRITE_PROTECTED"; break;
		case CKR_UNWRAPPING_KEY_HANDLE_INVALID:	str = "CKR_UNWRAPPING_KEY_HANDLE_INVALID"; break;
		case CKR_UNWRAPPING_KEY_SIZE_RANGE:	str = "CKR_UNWRAPPING_KEY_SIZE_RANGE"; break;
		case CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT:	str = "CKR_UNWRAPPING_KEY_TYPE_INCONSISTENT"; break;
		case CKR_USER_ALREADY_LOGGED_IN:	str = "CKR_USER_ALREADY_LOGGED_IN"; break;
		case CKR_USER_NOT_LOGGED_IN:	str = "CKR_USER_NOT_LOGGED_IN"; break;
		case CKR_USER_PIN_NOT_INITIALIZED:	str = "CKR_USER_PIN_NOT_INITIALIZED"; break;
		case CKR_USER_TYPE_INVALID:	str = "CKR_USER_TYPE_INVALID"; break;
		case CKR_USER_ANOTHER_ALREADY_LOGGED_IN:	str = "CKR_USER_ANOTHER_ALREADY_LOGGED_IN"; break;
		case CKR_USER_TOO_MANY_TYPES:	str = "CKR_USER_TOO_MANY_TYPES"; break;
		case CKR_WRAPPED_KEY_INVALID:	str = "CKR_WRAPPED_KEY_INVALID"; break;
		case CKR_WRAPPED_KEY_LEN_RANGE:	str = "CKR_WRAPPED_KEY_LEN_RANGE"; break;
		case CKR_WRAPPING_KEY_HANDLE_INVALID:	str = "CKR_WRAPPING_KEY_HANDLE_INVALID"; break;
		case CKR_WRAPPING_KEY_SIZE_RANGE:	str = "CKR_WRAPPING_KEY_SIZE_RANGE"; break;
		case CKR_WRAPPING_KEY_TYPE_INCONSISTENT:	str = "CKR_WRAPPING_KEY_TYPE_INCONSISTENT"; break;
		case CKR_RANDOM_SEED_NOT_SUPPORTED:	str = "CKR_RANDOM_SEED_NOT_SUPPORTED"; break;
		case CKR_RANDOM_NO_RNG:	str = "CKR_RANDOM_NO_RNG"; break;
		case CKR_BUFFER_TOO_SMALL:	str = "CKR_BUFFER_TOO_SMALL"; break;
		case CKR_SAVED_STATE_INVALID:	str = "CKR_SAVED_STATE_INVALID"; break;
		case CKR_INFORMATION_SENSITIVE:	str = "CKR_INFORMATION_SENSITIVE"; break;
		case CKR_STATE_UNSAVEABLE:	str = "CKR_STATE_UNSAVEABLE"; break;
		case CKR_CRYPTOKI_NOT_INITIALIZED:	str = "CKR_CRYPTOKI_NOT_INITIALIZED"; break;
		case CKR_CRYPTOKI_ALREADY_INITIALIZED:	str = "CKR_CRYPTOKI_ALREADY_INITIALIZED"; break;
		case CKR_MUTEX_BAD:	str = "CKR_MUTEX_BAD"; break;
		case CKR_MUTEX_NOT_LOCKED:	str = "CKR_MUTEX_NOT_LOCKED"; break;
		case CKR_ACTION_PROHIBITED:	str = "CKR_ACTION_PROHIBITED"; break;
		case CKR_PUBLIC_KEY_INVALID:	str = "CKR_PUBLIC_KEY_INVALID"; break;
		case CKR_FUNCTION_REJECTED:	str = "CKR_FUNCTION_REJECTED"; break;
		case CKR_CURVE_NOT_SUPPORTED:	str = "CKR_CURVE_NOT_SUPPORTED"; break;
		/* Otherwise - Value does not match a known PKCS11 return value */
		default:
		{
			printf("Unknown error value 0x%08lX\n",rc);
			break;
		}
	}

	if (str != NULL)
		printf("%s", str);
}

void printErrorMsg(CK_RV rv, char * filename, int line_no)
{
	printf("%s : return value: ", "ERROR");
	RetCode2String(rv);
	printf("\n[%s:%d]\n", filename, line_no);
}

void printBuffer(const char* name, CK_BYTE_PTR pData, CK_ULONG size)
{
	printf("--------------%s--------------\n", name);
	printf(" [00] ");
	for(CK_ULONG i = 0; i < size; i++)
	{
		if ((i % 16 == 0) && (i != 0))
		{
			printf("\n [%02lu] ", i);
		}
		printf("%02x ", pData[i]);
	}
	printf("\n-----------------------------\n");
}
