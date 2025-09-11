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

#ifndef PKCS11_ERROR_HANDLING_H
#define PKCS11_ERROR_HANDLING_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nvpkcs11.h"
#include "nvpkcs11_public_defs.h"

#define GO_TO_LABEL_ON_ERROR(rv, funcName, label) \
	if ((CKR_OK) != (rv)) \
	{ \
		printErrorMsg((funcName), (rv)); \
		goto label; \
	}

#define PRINT_MESSAGE_ON_ERROR_CONTINUE(rv, funcName, failedFlag) \
	if ((CKR_OK) != (rv)) \
	{ \
		failedFlag = (CK_BBOOL)CK_TRUE; \
		printErrorMsg((funcName), (rv)); \
	}

void printErrorMsg(const char *funcName, CK_RV rv);
char *retValToString(CK_RV rv);

#endif /* PKCS11_ERROR_HANDLING_H */
