/* ***************************************************************************** *
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 ******************************************************************************* */

#ifndef EFS_KEY_DEBUG_H_
#define EFS_KEY_DEBUG_H_
#include <stdbool.h>
#include "nvpkcs11.h"

#define DEBUG_PRINT(...) \
	if (verbose) \
		(void)printf(__VA_ARGS__)

#define DEBUG_HEXDUMP(...) \
	if (verbose) \
		printBuffer(__VA_ARGS__)

#define DATA_CHUNK_SIZE (2 * NVPKCS11_AES_CBC_BLOCK_SIZE)

#define C_FUNCTION_RESULT(rv, function_name, label, expected_rv) \
	isReturnAllowed(#function_name, rv); \
	if ((expected_rv) != (rv)) \
	{ \
		DEBUG_PRINT(#function_name": FAIL\n"); \
		printErrorMsg((rv), __FILE__, __LINE__); \
		goto label; \
	} \
	else \
	{ \
		DEBUG_PRINT(#function_name": PASS\n"); \
		rv = CKR_OK; \
	}

#ifdef NVPKCS11_CHECK_RETURN_VALUE

#include <string.h>
#include "nvpkcs11.h"

#define CHECK_FUNC(function) \
	if ((strcmp(caller, #function) == 0))

void isReturnAllowed(const char *caller, CK_RV input_rv);

#define IS_RETURN_CODE_ALLOWED(rv) \
	(isReturnAllowed(__func__, rv))

#else
#define IS_RETURN_CODE_ALLOWED(...)
#define isReturnAllowed(...)
#endif

void RetCode2String( CK_RV rc );
void printErrorMsg(CK_RV rv, char * filename, int line_no);
void printBuffer(const char* name, CK_BYTE_PTR pData, CK_ULONG size);

#endif /* EFS_KEY_DEBUG_H_ */
