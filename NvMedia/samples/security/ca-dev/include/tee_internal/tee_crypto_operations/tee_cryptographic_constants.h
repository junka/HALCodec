/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2019-2021, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: TEE Cryptographic Constants</b>
 *
 * @b Description: Describes TEE cryptographic constants.
 */

/**
 * @defgroup global_platform_tee_constants TEE Cryptographic Constants
 *
 * Describes TEE cryptographic constants.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_INTERNAL_CRYPTOGRAPHIC_CONSTANTS_H
#define TEE_INTERNAL_CRYPTOGRAPHIC_CONSTANTS_H

/** Specifies operation class constants. */
typedef enum {
    TEE_OPERATION_CIPHER              = 1U,
    TEE_OPERATION_MAC                 = 3U,
    TEE_OPERATION_AE                  = 4U,
    TEE_OPERATION_DIGEST              = 5U,
    TEE_OPERATION_ASYMMETRIC_CIPHER   = 6U,
    TEE_OPERATION_ASYMMETRIC_SIGNATURE= 7U,
    TEE_OPERATION_KEY_DERIVATION      = 8U,
} TEE_OperationClass;

/** Specifies cryptographic algorithm identifiers. */
/* TODO: Underlying implementation only supports a limited subset */

typedef enum {
    TEE_ALG_AES_ECB_NOPAD             = 0x10000010,
    TEE_ALG_AES_CBC_NOPAD             = 0x10000110,
    TEE_ALG_AES_CTR                   = 0x10000210,
    TEE_ALG_AES_CMAC                  = 0x30000610, /* CBC-MAC w/ AES-128 bit key */
    TEE_ALG_HMAC_SHA256               = 0x30000004, /* HMAC w/ SHA256 */

    TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA256 = 0x70414930,
    TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA384 = 0x70515930,
    TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA512 = 0x70616930,
    TEE_ALG_RSASSA_PKCS1_V1_5_SHA256     = 0x70004830,
    TEE_ALG_SHA256 = 0x50000004,
    TEE_ALG_SHA384 = 0x50000005,
    TEE_ALG_SHA512 = 0x50000006,
    TEE_ALG_ECDSA_SHA256 = 0x70003042,
    TEE_ALG_ED25519      = 0x70006043,
    TEE_ALG_ECDH_DERIVE_SHARED_SECRET    = 0x80000042,

#ifdef CONFIG_TRUSTY
    TEE_ALG_AES_CTS     = 0x10000310,
    TEE_ALG_HMAC_SHA1   = 0x30000002, /* HMAC w/ SHA1 */
    TEE_ALG_HMAC_SHA224 = 0x30000003, /* HMAC w/ SHA224 */
    TEE_ALG_HMAC_SHA384 = 0x30000005, /* HMAC w/ SHA384 */
    TEE_ALG_HMAC_SHA512 = 0x30000006, /* HMAC w/ SHA512 */
#endif
} TEE_OperationAlgorithm;

/** List of Supported Cryptographic Elements */
typedef enum {
    TEE_CRYPTO_ELEMENT_NONE     = 0x00000000,
    TEE_ECC_CURVE_NIST_P256     = 0x00000003,

    /* Edwards curve for backward compatibility */
    TEE_ECC_CURVE_25519         = 0x00000300,
    /* Twisted edwards curve ED25519 */
    TEE_ECC_CURVE_ED25519         = TEE_ECC_CURVE_25519,
    /* Montgomery curve Curve25519 */
    TEE_ECC_CURVE_C25519        = 0x00000301,
} TEE_EccCurveID;
/** @} */
#endif /* TEE_INTERNAL_CRYPTOGRAPHIC_CONSTANTS_H */
