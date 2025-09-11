/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: NV PKCS11 consts </b>
 *
 * @b Description: Describes NV PKCS11 KeyStore constants.
 */

#ifndef TEE_COMMON_PKCS11KS_CONSTANTS_H
#define TEE_COMMON_PKCS11KS_CONSTANTS_H

#define PKS_KEY_BYTE_SIZE_ID            (32U)
#define PKS_KEY_BYTE_SIZE_LABEL         (32U)
#define PKS_KEY_BYTE_SIZE_UNIQUE_ID     (8U)
#define PKS_KEY_BYTE_SIZE_MECHANISMS    (16U)
#define PKS_SEC_KEY_BYTE_SIZE_IV        (16U)
#define PKS_SEC_KEY_BYTE_SIZE_KEY       (48U)
#define PKS_KEY_BYTE_SIZE_MAC           (16U)
#define PKS_SEC_KEY_AES_CCM_TAG_LEN     (PKS_KEY_BYTE_SIZE_MAC)
#define PKS_SEC_KEY_AES_CCM_NONCE_SIZE  (12U)
#define PKS_SEC_KEY_AES_GCM_TAG_LEN     (16U)   // Tag length: 128 bits
#define PKS_SEC_KEY_AES_GCM_NONCE_SIZE  (12U)   // IV length:   96 bits
#define PKS_RSA_KEY_BYTE_SIZE_KEY       (512U)
#define PKS_RSA_KEY_BYTE_SIZE_IV        (16U)
#define PKS_RSA_KEY_PUB_EXP_SIZE_KEY    (4U)
#define PKS_RSA_KEY_PUB_RSV_SIZE_KEY    (12U)
#define PKS_RSA_KEY_PRIV_RSV_SIZE_KEY    (12U)
#define PKS_ECC_KEY_BYTE_SIZE_KEY       (68U)
#define PKS_ECC_KEY_BYTE_SIZE_IV        (16U)
#define PKS_ECC_KEY_RSV_SIZE_KEY        (8U)
#define PKS_SEC_KEY_KEYCHECK_BYTE_SIZE  (3U)
#define PKS_GDO_BYTE_SIZE_APPLICATION   (32U)
#define PKS_GDO_BYTE_SIZE_OBJECT_ID     (64U)
#define PKS_GDO_RSV_BYTE_SIZE           (12U)
#define PKS_GDO_VALUE_BYTE_SIZE         (3616U)
#define PKS_GDO_BYTE_SIZE_IV            (16U)
#define PKS_GDO_BYTE_SIZE_MAC           (16U)
#define PKS_SEC_KEY_AES_WRAP_IV_SIZE    (8U)    // RFC-3394 AES WRAP
#define PKS_AES_CBC_KEY_IV_SIZE         (16U)

// Metadata Word0
#define PKS_MD_FLD_OBJTYPE_LSB        (0U)
#define PKS_MD_FLD_OBJTYPE_MSB        (3U)
#define PKS_MD_FLD_VERSION_LSB        (4U)
#define PKS_MD_FLD_VERSION_MSB        (7U)
#define PKS_MD_FLD_RESERVED_LSB       (8U)
#define PKS_MD_FLD_RESERVED_MSB       (10U)
#define PKS_MD_FLD_RESERVED1_LSB      (8U)
#define PKS_MD_FLD_RESERVED1_MSB      (10U)
#define PKS_GDO_MD_FLD_RESERVED1_LSB  (8U)
#define PKS_GDO_MD_FLD_RESERVED1_MSB  (10U)
#define PKS_MD_FLD_DST_LSB            (11U)
#define PKS_MD_FLD_DST_MSB            (11U)
#define PKS_MD_FLD_CPY_LSB            (12U)
#define PKS_MD_FLD_CPY_MSB            (12U)
#define PKS_MD_FLD_MOD_LSB            (13U)
#define PKS_MD_FLD_MOD_MSB            (13U)
#define PKS_MD_FLD_KCV_LSB            (14U)
#define PKS_MD_FLD_KCV_MSB            (14U)
#define PKS_MD_FLD_RESERVED2_LSB      (14U)
#define PKS_MD_FLD_RESERVED2_MSB      (14U)
#define PKS_MD_FLD_LCL_LSB            (15U)
#define PKS_MD_FLD_LCL_MSB            (15U)
#define PKS_MD_FLD_TST_LSB            (16U)
#define PKS_MD_FLD_TST_MSB            (16U)
#define PKS_MD_FLD_EXT_LSB            (17U)
#define PKS_MD_FLD_EXT_MSB            (19U)
#define PKS_MD_FLD_SEN_LSB            (20U)
#define PKS_MD_FLD_SEN_MSB            (21U)
#define PKS_MD_FLD_KAG_LSB            (22U)
#define PKS_MD_FLD_KAG_MSB            (22U)
#define PKS_MD_FLD_DRV_LSB            (23U)
#define PKS_MD_FLD_DRV_MSB            (23U)
#define PKS_MD_FLD_MAC_LSB            (24U)
#define PKS_MD_FLD_MAC_MSB            (25U)
#define PKS_MD_FLD_WRP_LSB            (26U)
#define PKS_MD_FLD_WRP_MSB            (27U)
#define PKS_MD_FLD_SYM_LSB            (28U)
#define PKS_MD_FLD_SYM_MSB            (29U)
#define PKS_MD_FLD_ASY_LSB            (30U)
#define PKS_MD_FLD_ASY_MSB            (31U)

#define PKS_MD_FLD_KEYPURPOSE_LSB     (22U)
#define PKS_MD_FLD_KEYPURPOSE_MSB     (31U)
#define PKS_GDO_MD_FLD_RESERVED2_LSB  (14U)
#define PKS_GDO_MD_FLD_RESERVED2_MSB  (31U)

// Metadata Word2
#define PKS_MD_FLD_NMECH_LSB         (0U)
#define PKS_MD_FLD_NMECH_MSB         (3U)
#define PKS_MD_FLD_CRYPRD_LSB        (4U)
#define PKS_MD_FLD_CRYPRD_MSB        (7U)
#define PKS_MD_FLD_KEYCHECK_LSB      (8U)
#define PKS_MD_FLD_KEYCHECK_MSB      (31U)
#define PKS_MD_FLD_IVGEN_LSB         (8U)
#define PKS_MD_FLD_IVGEN_MSB         (9U)
#define PKS_MD_FLD_AES_RESERVED3_LSB (10U)
#define PKS_MD_FLD_AES_RESERVED3_MSB (31U)
#define PKS_MD_FLD_RESERVED3_LSB     (8U)
#define PKS_MD_FLD_RESERVED3_MSB     (31U)

// Constants for metadata reserved fields
#define PKS_MD_RESERVED1_SET_3BITS       (0x7U)
#define PKS_MD_RESERVED2_SET_1BIT        (0x1U)
#define PKS_MD_RESERVED3_SET_22BITS      (0x3FFFFFU)
#define PKS_MD_RESERVED3_SET_24BITS      (0xFFFFFFU)
#define PKS_GDO_MD_RESERVED1_SET_3BITS   (0x7U)
#define PKS_GDO_MD_RESERVED2_SET_18BITS  (0x3FFFFU)
#define PKS_MD_RESERVED_FLD_BYTE         (0xFF)

#endif
