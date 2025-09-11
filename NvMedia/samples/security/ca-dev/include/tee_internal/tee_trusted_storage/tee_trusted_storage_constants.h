/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Trusted Storage Constants</b>
 *
 * @b Description: Describes TEE trusted storage constants.
 */

/**
 * @defgroup global_platform_storange TEE Trusted Storage Constants
 *
 * Describes TEE trusted storage constants.
 * These constants are only supported on Linux PDK.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_TRUSTED_STORAGE_CONSTANTS_H
#define TEE_TRUSTED_STORAGE_CONSTANTS_H

/* -- Object Storage Constants -- */
#define TEE_OBJECT_STORAGE_PRIVATE  0x00000001
#define TEE_STORAGE_PRIVATE         0U

/** Data Flag Constants */
typedef enum {
	TEE_DATA_FLAG_ACCESS_READ       = 0x00000001,
	TEE_DATA_FLAG_ACCESS_WRITE      = 0x00000002,
	TEE_DATA_FLAG_ACCESS_WRITE_META = 0x00000004,
	TEE_DATA_FLAG_SHARE_READ        = 0x00000010,
	TEE_DATA_FLAG_SHARE_WRITE       = 0x00000020,
	TEE_DATA_FLAG_CREATE            = 0x00000200,
	TEE_DATA_FLAG_EXCLUSIVE         = 0x00000400,

} TEE_TrustedStorageDataFlag;

/** Usage Constants */
typedef enum {
	TEE_USAGE_EXTRACTABLE           = 0x00000001,
	TEE_USAGE_ENCRYPT               = 0x00000002,
	TEE_USAGE_DECRYPT               = 0x00000004,
	TEE_USAGE_MAC                   = 0x00000008,
	TEE_USAGE_SIGN                  = 0x00000010,
	TEE_USAGE_VERIFY                = 0x00000020,
	TEE_USAGE_DERIVE                = 0x00000040,

} TEE_TrustedStorageUsageFlag;

/** Handle Flag Constants */
typedef enum {
	TEE_HANDLE_FLAG_PERSISTENT      = 0x00001000,
	TEE_HANDLE_FLAG_INITIALIZED     = 0x00002000,
	TEE_HANLDE_FLAG_KEY_SET         = 0x00004000,
	TEE_HANDLE_FLAG_EXPECT_TWO_KEYS = 0x00008000,

} TEE_TrustedStorageHandleFlag;

/* Object Types */
typedef enum {
	TEE_TYPE_AES                    = 0xA00000010U,
	TEE_TYPE_DES                    = 0xA00000011U,
	TEE_TYPE_DES3                   = 0xA00000013U,
	TEE_TYPE_HMAC_MD5               = 0xA00000001U,
	TEE_TYPE_HMAC_SHA1              = 0xA00000002U,
	TEE_TYPE_HMAC_SHA224            = 0xA00000003U,
	TEE_TYPE_HMAC_SHA256            = 0xA00000004U,
	TEE_TYPE_HMAC_SHA384            = 0xA00000005U,
	TEE_TYPE_HMAC_SHA512            = 0xA00000006U,
	TEE_TYPE_RSA_PUBLIC_KEY         = 0xA00000030U,
	TEE_TYPE_RSA_KEYPAIR            = 0xA10000030U,
	TEE_TYPE_DSA_PUBLIC_KEY         = 0xA00000031U,
	TEE_TYPE_DSA_KEYPAIR            = 0xA10000031U,
	TEE_TYPE_DH_KEYPAIR             = 0xA10000032U,
	TEE_TYPE_GENERIC_SECRET         = 0xA00000000U,
} TEE_TrustedStorageObjectType;

/** Attribute IDs */
typedef enum {
	TEE_ATTR_SECRET_VALUE           = 0xC0000000U,
	TEE_ATTR_RSA_MODULES            = 0xD0000130U,
	TEE_ATTR_RSA_PUBLIC_EXPONENT    = 0xD0000230U,
	TEE_ATTR_RSA_PRIVATE_EXPONENT   = 0xC0000330U,
	TEE_ATTR_RSA_PRIME1             = 0xC0000430U,
	TEE_ATTR_RSA_PRIME2             = 0xC0000530U,
	TEE_ATTR_RSA_EXPONENT1          = 0xC0000630U,
	TEE_ATTR_RSA_EXPONENT2          = 0xC0000730U,
	TEE_ATTR_RSA_COEFFICIENT        = 0xC0000830U,
	TEE_ATTR_DSA_PRIME              = 0xD0001031U,
	TEE_ATTR_DSA_SUBPRIME           = 0xD0001131U,
	TEE_ATTR_DSA_BASE               = 0xD0001231U,
	TEE_ATTR_DSA_PUBLIC_VALUE       = 0xD0000131U,
	TEE_ATTR_DSA_PRIVATE_VALUE      = 0xD0000231U,
	TEE_ATTR_DH_PRIME               = 0xD0001032U,
	TEE_ATTR_DH_SUBPRIME            = 0xD0001132U,
	TEE_ATTR_DH_BASE                = 0xD0001232U,
	TEE_ATTR_DH_X_BITS              = 0xF0001332U,
	TEE_ATTR_DH_PUBLIC_VALUE        = 0xD0000132U,
	TEE_ATTR_DH_PRIVATE_VALUE       = 0xC0000232U,
	TEE_ATTR_RSA_OAEP_LABEL	        = 0xD0000930U,
	TEE_ATTR_RSA_PSS_SALT_LENGTH    = 0xF0000A30U,
} TEE_AttributeID;

typedef enum {
    TEE_ATTR_FLAG_VALUE         = 0x20000000,
    TEE_ATTR_FLAG_PUBLIC        = 0x10000000,
} TEE_AttributeFlag;

/* -- Miscellaneous Constants -- */
#define TEE_DATA_MAX_POSITION            0xFFFFFFFFU
#define TEE_OBJECT_ID_MAX_LEN            64U
/** @} */
#endif /* TEE_TRUSTED_STORAGE_CONSTANTS_H */
