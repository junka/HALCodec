/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: Tee Internal Property Access</b>
 *
 * @b Description: Describes Tee internal property access.
 */

/**
 * @defgroup global_platform_property TEE Internal Property Access
 *
 * Describes TEE internal property access.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_PROPERTY_ACCESS_H
#define TEE_INTERNAL_PROPERTY_ACCESS_H

#include <tee_internal/tee_internal_constants.h>
#include <tee_internal/tee_internal_datatypes.h>
#include <tee_internal/tee_internal_os_abstraction_layer.h>
#include <tee_common/tee_common_os_abstraction_layer.h>
/* Currently Supported Global platform Property Names
 * Only supported by Trusty, not TZVault.
 *
 * Constant Definitions of property names
 * as per the GlobalPlatformAPI spec
 */
#define GPD_CLIENT_IDENTITY "gpd.client.identity"
#define GPD_CURRENT_TA_UUID "gpd.ta.appID"
/* ------------------------------------------- */

/* TODO Currently Unsupported */
TEE_Result TEE_AllocatePropertyEnumerator(
	TEE_PropSetHandle* 	enumerator );

/* TODO Currently Unsupported */
TEE_Result TEE_GetPropertyAsString(
	TEE_PropSetHandle 	propsetOrEnumerator,
	char*				name,
	char*				valueBuffer,
	size_t* 			valueBufferLen );

/* TODO Currently Unsupported */
TEE_Result TEE_GetPropertyAsBool(
	TEE_PropSetHandle 	propsetOrEnumerator,
	char*				name,
	bool*				value );

/* TODO Currently Unsupported */
TEE_Result TEE_GetPropertyAsU32(
	TEE_PropSetHandle 	propsetOrEnumerator,
	char*				name,
	uint32_t*			value );

/* TODO Currently Unsupported */
TEE_Result TEE_GetPropertyAsBinaryBlock(
	TEE_PropSetHandle 	propsetOrEnumerator,
	char*				name,
	void*				valueBuffer,
	size_t* 			valueBufferLen );

/* TODO Currently Unsupported */
TEE_Result TEE_GetPropertyAsUUID(
	TEE_PropSetHandle 	propsetOrEnumerator,
	char*				name,
	TEE_UUID*			value );

/* TODO Currently Unsupported */
void TEE_FreePropertyEnumerator(
	TEE_PropSetHandle 	enumerator );

/* TODO Currently Unsupported */
void TEE_StartPropertyEnumerator(
	TEE_PropSetHandle 	enumerator,
	TEE_PropSetHandle 	propSet );

/* TODO Currently Unsupported */
void TEE_ResetPropertyEnumerator(
	TEE_PropSetHandle 	enumerator );

/* TODO Currently Unsupported */
TEE_Result TEE_GetPropertyName(
	TEE_PropSetHandle 	enumerator,
	void*				nameBuffer,
	size_t* 			nameBufferLen );

/* TODO Currently Unsupported */
TEE_Result TEE_GetNextProperty(
	TEE_PropSetHandle 	enumerator);
/** @} */
#endif /* TEE_INTERNAL_PROPERTY_ACCESS_H */
