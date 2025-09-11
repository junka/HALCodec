/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: NV PKCS11 Datatypes </b>
 *
 * @b Description: Describes NV PKCS11 datatypes.
 */

#ifndef TEE_CLIENT_PKCS11KS_DATATYPES_H
#define TEE_CLIENT_PKCS11KS_DATATYPES_H

#include <tee_client/tee_client_pkcs11ks_constants.h>
#include <tee_common/tee_common_pkcs11ks_datatypes.h>

#define PKS_ENUM_TO_VAL(e) ((uint32_t)e)

/// \defgroup pks_ids_structs PKCS11 Keystore Structs
///@{

#define PKS_METADATA_TRUE   (0U)
#define PKS_METADATA_FALSE  (1U)

// Secret key object metadata fields

// Metadata Word0
#define PKS_SEC_KEY_OBJTYPE_SHIFT       (PKS_MD_FLD_OBJTYPE_LSB)
#define PKS_SEC_KEY_OBJTYPE_MASK        (0xFFFFFFFFU >> (31U - PKS_MD_FLD_OBJTYPE_MSB + PKS_MD_FLD_OBJTYPE_LSB))
#define PKS_SEC_KEY_VERSION_SHIFT       (PKS_MD_FLD_VERSION_LSB)
#define PKS_SEC_KEY_VERSION_MASK        (0xFFFFFFFFU >> (31U - PKS_MD_FLD_VERSION_MSB + PKS_MD_FLD_VERSION_LSB))
#define PKS_SEC_KEY_RESERVED_SHIFT      (PKS_MD_FLD_RESERVED_LSB)
#define PKS_SEC_KEY_RESERVED_MASK       (0xFFFFFFFFU >> (31U - PKS_MD_FLD_RESERVED_MSB + PKS_MD_FLD_RESERVED_LSB))
#define PKS_SEC_KEY_RESERVED1_SHIFT     (PKS_MD_FLD_RESERVED1_LSB)
#define PKS_SEC_KEY_RESERVED1_MASK      (0xFFFFFFFFU >> (31U - PKS_MD_FLD_RESERVED1_MSB + PKS_MD_FLD_RESERVED1_LSB))
#define PKS_GDO_RESERVED1_SHIFT         (PKS_GDO_MD_FLD_RESERVED1_LSB)
#define PKS_GDO_RESERVED1_MASK          (0xFFFFFFFFU >> (31U - PKS_GDO_MD_FLD_RESERVED1_MSB + PKS_GDO_MD_FLD_RESERVED1_LSB))
#define PKS_SEC_KEY_DST_SHIFT           (PKS_MD_FLD_DST_LSB)
#define PKS_SEC_KEY_DST_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_DST_MSB + PKS_MD_FLD_DST_LSB))
#define PKS_SEC_KEY_CPY_SHIFT           (PKS_MD_FLD_CPY_LSB)
#define PKS_SEC_KEY_CPY_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_CPY_MSB + PKS_MD_FLD_CPY_LSB))
#define PKS_SEC_KEY_MOD_SHIFT           (PKS_MD_FLD_MOD_LSB)
#define PKS_SEC_KEY_MOD_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_MOD_MSB + PKS_MD_FLD_MOD_LSB))
#define PKS_SEC_KEY_KCV_SHIFT           (PKS_MD_FLD_KCV_LSB)
#define PKS_SEC_KEY_KCV_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_KCV_MSB + PKS_MD_FLD_KCV_LSB))
#define PKS_SEC_KEY_RESERVED2_SHIFT     (PKS_MD_FLD_RESERVED2_LSB)
#define PKS_SEC_KEY_RESERVED2_MASK      (0xFFFFFFFFU >> (31U - PKS_MD_FLD_RESERVED2_MSB + PKS_MD_FLD_RESERVED2_LSB))
#define PKS_SEC_KEY_LCL_SHIFT           (PKS_MD_FLD_LCL_LSB)
#define PKS_SEC_KEY_LCL_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_LCL_MSB + PKS_MD_FLD_LCL_LSB))
#define PKS_SEC_KEY_TST_SHIFT           (PKS_MD_FLD_TST_LSB)
#define PKS_SEC_KEY_TST_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_TST_MSB + PKS_MD_FLD_TST_LSB))
#define PKS_SEC_KEY_EXT_SHIFT           (PKS_MD_FLD_EXT_LSB)
#define PKS_SEC_KEY_EXT_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_EXT_MSB + PKS_MD_FLD_EXT_LSB))
#define PKS_SEC_KEY_SEN_SHIFT           (PKS_MD_FLD_SEN_LSB)
#define PKS_SEC_KEY_SEN_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_SEN_MSB + PKS_MD_FLD_SEN_LSB))
#define PKS_SEC_KEY_KAG_SHIFT           (PKS_MD_FLD_KAG_LSB)
#define PKS_SEC_KEY_KAG_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_KAG_MSB + PKS_MD_FLD_KAG_LSB))
#define PKS_SEC_KEY_DRV_SHIFT           (PKS_MD_FLD_DRV_LSB)
#define PKS_SEC_KEY_DRV_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_DRV_MSB + PKS_MD_FLD_DRV_LSB))
#define PKS_SEC_KEY_MAC_SHIFT           (PKS_MD_FLD_MAC_LSB)
#define PKS_SEC_KEY_MAC_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_MAC_MSB + PKS_MD_FLD_MAC_LSB))
#define PKS_SEC_KEY_WRP_SHIFT           (PKS_MD_FLD_WRP_LSB)
#define PKS_SEC_KEY_WRP_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_WRP_MSB + PKS_MD_FLD_WRP_LSB))
#define PKS_SEC_KEY_SYM_SHIFT           (PKS_MD_FLD_SYM_LSB)
#define PKS_SEC_KEY_SYM_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_SYM_MSB + PKS_MD_FLD_SYM_LSB))
#define PKS_SEC_KEY_ASY_SHIFT           (PKS_MD_FLD_ASY_LSB)
#define PKS_SEC_KEY_ASY_MASK            (0xFFFFFFFFU >> (31U - PKS_MD_FLD_ASY_MSB + PKS_MD_FLD_ASY_LSB))

#define PKS_SEC_KEY_KEYPURPOSE_SHIFT    (PKS_MD_FLD_KEYPURPOSE_LSB)
#define PKS_SEC_KEY_KEYPURPOSE_MASK     (0xFFFFFFFFU >> (31U - PKS_MD_FLD_KEYPURPOSE_MSB + PKS_MD_FLD_KEYPURPOSE_LSB))
#define PKS_GDO_RESERVED2_SHIFT         (PKS_GDO_MD_FLD_RESERVED2_LSB)
#define PKS_GDO_RESERVED2_MASK          (0xFFFFFFFFU >> (31U - PKS_GDO_MD_FLD_RESERVED2_MSB + PKS_GDO_MD_FLD_RESERVED2_LSB))

// Metadata Word1
#define PKS_SEC_KEY_KEYTYPE_SHIFT       0U
#define PKS_SEC_KEY_KEYTYPE_MASK        0xFFFFFFFFU

// Metadata Word2
#define PKS_SEC_KEY_TOKEN_SHIFT         0U
#define PKS_SEC_KEY_TOKEN_MASK          0xFFFFFFFFU

// Metadata Word3
#define PKS_SEC_KEY_NMECH_SHIFT        (PKS_MD_FLD_NMECH_LSB)
#define PKS_SEC_KEY_NMECH_MASK         (0xFFFFFFFFU >> (31U - PKS_MD_FLD_NMECH_MSB + PKS_MD_FLD_NMECH_LSB))
#define PKS_SEC_KEY_CRYPRD_SHIFT       (PKS_MD_FLD_CRYPRD_LSB)
#define PKS_SEC_KEY_CRYPRD_MASK        (0xFFFFFFFFU >> (31U - PKS_MD_FLD_CRYPRD_MSB + PKS_MD_FLD_CRYPRD_LSB))
#define PKS_SEC_KEY_KEYCHECK_SHIFT     (PKS_MD_FLD_KEYCHECK_LSB)
#define PKS_SEC_KEY_KEYCHECK_MASK      (0xFFFFFFFFU >> (31U - PKS_MD_FLD_KEYCHECK_MSB + PKS_MD_FLD_KEYCHECK_LSB))
#define PKS_SEC_KEY_RESERVED3_SHIFT     (PKS_MD_FLD_RESERVED3_LSB)
#define PKS_SEC_KEY_RESERVED3_MASK      (0xFFFFFFFFU >> (31U - PKS_MD_FLD_RESERVED3_MSB + PKS_MD_FLD_RESERVED3_LSB))
#define PKS_SEC_KEY_IVGEN_SHIFT        (PKS_MD_FLD_IVGEN_LSB)
#define PKS_SEC_KEY_IVGEN_MASK         (0xFFFFFFFFU >> (31U - PKS_MD_FLD_IVGEN_MSB + PKS_MD_FLD_IVGEN_LSB))
#define PKS_SEC_KEY_AES_RESERVED3_SHIFT (PKS_MD_FLD_AES_RESERVED3_LSB)
#define PKS_SEC_KEY_AES_RESERVED3_MASK  (0xFFFFFFFFU >> (31U - PKS_MD_FLD_AES_RESERVED3_MSB + PKS_MD_FLD_AES_RESERVED3_LSB))

// Metadata Word4
#define PKS_SEC_KEY_INITIALTIME_SHIFT       0U
#define PKS_SEC_KEY_INITIALTIME_MASK        0xFFFFFFFFU
// Metadata Word5
#define PKS_SEC_KEY_PROCESSSTART_SHIFT       0U
#define PKS_SEC_KEY_PROCESSSTART_MASK        0xFFFFFFFFU
// Metadata Word6
#define PKS_SEC_KEY_PROTECTSTOP_SHIFT       0U
#define PKS_SEC_KEY_PROTECTSTOP_MASK        0xFFFFFFFFU
// Metadata Word7
#define PKS_SEC_KEY_DEACTIVATION_SHIFT       0U
#define PKS_SEC_KEY_DEACTIVATION_MASK        0xFFFFFFFFU
// Metadata Word8
#define PKS_SEC_KEY_GENMECHANISM_SHIFT       0U
#define PKS_SEC_KEY_GENMECHANISM_MASK        0xFFFFFFFFU

// RSA public key object metadata fields
#define PKS_RSA_PUBKEY_OBJTYPE_SHIFT        0U
#define PKS_RSA_PUBKEY_OBJTYPE_MASK         0xFU

// RSA private key object metadata fields
#define PKS_RSA_PRIKEY_OBJTYPE_SHIFT        0U
#define PKS_RSA_PRIKEY_OBJTYPE_MASK         0xFU

// ECC public key object metadata fields
#define PKS_ECC_PUBKEY_OBJTYPE_SHIFT        0U
#define PKS_ECC_PUBKEY_OBJTYPE_MASK         0xFU

// ECC private key object metadata fields
#define PKS_ECC_PRIKEY_OBJTYPE_SHIFT        0U
#define PKS_ECC_PRIKEY_OBJTYPE_MASK         0xFU

// Check the type of a key object
#define PKS_IS_SEC_KEY(key) \
    (((*((unsigned char*)key)) & PKS_SEC_KEY_OBJTYPE_MASK) == 0x1U)
#define PKS_IS_RSA_PUBKEY(key) \
    (((*((unsigned char*)key)) & PKS_RSA_PUBKEY_OBJTYPE_MASK) == 0x2U)
#define PKS_IS_RSA_PRIKEY(key) \
    (((*((unsigned char*)key)) & PKS_RSA_PRIKEY_OBJTYPE_MASK) == 0x4U)
#define PKS_IS_ECC_PUBKEY(key) \
    (((*((unsigned char*)key)) & PKS_ECC_PUBKEY_OBJTYPE_MASK) == 0x3U)
#define PKS_IS_ECC_PRIKEY(key) \
    (((*((unsigned char*)key)) & PKS_ECC_PRIKEY_OBJTYPE_MASK) == 0x5U)

// Wrappers to get/set common field values (word or smaller) of a key object
#define PKS_KEY_GET(key, field)                 \
    PKS_SEC_KEY_GET(key, field)
#define PKS_KEY_SET(key, field, val)            \
    PKS_SEC_KEY_SET(key, field, val)

// Wrappers to get/set field values (word or smaller) of a secret key object
#define PKS_SEC_KEY_GET(key, field)             \
    PKS_SEC_KEY_GET_##field(key)
#define PKS_SEC_KEY_SET(key, field, val)        \
    PKS_SEC_KEY_SET_##field(key, val)

// Wrappers to translate a field to a metadata word
#define PKS_SEC_KEY_GET_OBJTYPE(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, OBJTYPE)
#define PKS_SEC_KEY_SET_OBJTYPE(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, OBJTYPE, val)
#define PKS_SEC_KEY_GET_VERSION(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, VERSION)
#define PKS_SEC_KEY_SET_VERSION(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, VERSION, val)
#define PKS_SEC_KEY_GET_DST(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, DST)
#define PKS_SEC_KEY_SET_DST(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, DST, val)
#define PKS_SEC_KEY_GET_CPY(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, CPY)
#define PKS_SEC_KEY_SET_CPY(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, CPY, val)
#define PKS_SEC_KEY_GET_MOD(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, MOD)
#define PKS_SEC_KEY_SET_MOD(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, MOD, val)
#define PKS_SEC_KEY_GET_KCV(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, KCV)
#define PKS_SEC_KEY_SET_KCV(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, KCV, val)
#define PKS_SEC_KEY_GET_LCL(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, LCL)
#define PKS_SEC_KEY_SET_LCL(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, LCL, val)
#define PKS_SEC_KEY_GET_TST(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, TST)
#define PKS_SEC_KEY_SET_TST(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, TST, val)
#define PKS_SEC_KEY_GET_EXT(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, EXT)
#define PKS_SEC_KEY_SET_EXT(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, EXT, val)
#define PKS_SEC_KEY_GET_SEN(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, SEN)
#define PKS_SEC_KEY_SET_SEN(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, SEN, val)
#define PKS_SEC_KEY_GET_WRP(key)                    \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, WRP)
#define PKS_SEC_KEY_SET_WRP(key, val)                   \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, WRP, val)
#define PKS_SEC_KEY_GET_KEYPURPOSE(key)                     \
    PKS_SEC_KEY_GET_DEF(key, keyAttrMetadata, KEYPURPOSE)
#define PKS_SEC_KEY_SET_KEYPURPOSE(key, val)                    \
    PKS_SEC_KEY_SET_DEF(key, keyAttrMetadata, KEYPURPOSE, val)

#define PKS_SEC_KEY_GET_KEYTYPE(key)            \
    PKS_SEC_KEY_GET_DEF(key, keyType, KEYTYPE)
#define PKS_SEC_KEY_SET_KEYTYPE(key, val)           \
    PKS_SEC_KEY_SET_DEF(key, keyType, KEYTYPE, val)

#define PKS_SEC_KEY_GET_TOKEN(key)                   \
    PKS_SEC_KEY_GET_DEF(key, token, TOKEN)
#define PKS_SEC_KEY_SET_TOKEN(key, val)                  \
    PKS_SEC_KEY_SET_DEF(key, token, TOKEN, val)

#define PKS_SEC_KEY_GET_NMECH(key)                      \
    PKS_SEC_KEY_GET_DEF(key, keyCryptoMetadata, NMECH)
#define PKS_SEC_KEY_SET_NMECH(key, val)                     \
    PKS_SEC_KEY_SET_DEF(key, keyCryptoMetadata, NMECH, val)
#define PKS_SEC_KEY_GET_CRYPRD(key)                     \
    PKS_SEC_KEY_GET_DEF(key, keyCryptoMetadata, CRYPRD)
#define PKS_SEC_KEY_SET_CRYPRD(key, val)                        \
    PKS_SEC_KEY_SET_DEF(key, keyCryptoMetadata, CRYPRD, val)
#define PKS_SEC_KEY_GET_KEYCHECK(key)                       \
    PKS_SEC_KEY_GET_DEF(key, keyCryptoMetadata, KEYCHECK)
#define PKS_SEC_KEY_SET_KEYCHECK(key, val)                      \
    PKS_SEC_KEY_SET_DEF(key, keyCryptoMetadata, KEYCHECK, val)
#define PKS_SEC_KEY_GET_IVGEN(key)                       \
    PKS_SEC_KEY_GET_DEF(key, keyCryptoMetadata, IVGEN)
#define PKS_SEC_KEY_SET_IVGEN(key, val)                      \
    PKS_SEC_KEY_SET_DEF(key, keyCryptoMetadata, IVGEN, val)

#define PKS_SEC_KEY_GET_INITIALTIME(key)                \
    PKS_SEC_KEY_GET_DEF(key, initialTime, INITIALTIME)
#define PKS_SEC_KEY_SET_INITIALTIME(key, val)               \
    PKS_SEC_KEY_SET_DEF(key, initialTime, INITIALTIME, val)

#define PKS_SEC_KEY_GET_PROCESSSTART(key)                   \
    PKS_SEC_KEY_GET_DEF(key, processStart, PROCESSSTART)
#define PKS_SEC_KEY_SET_PROCESSSTART(key, val)                  \
    PKS_SEC_KEY_SET_DEF(key, processStart, PROCESSSTART, val)

#define PKS_SEC_KEY_GET_PROTECTSTOP(key)                \
    PKS_SEC_KEY_GET_DEF(key, protectStop, PROTECTSTOP)
#define PKS_SEC_KEY_SET_PROTECTSTOP(key, val)               \
    PKS_SEC_KEY_SET_DEF(key, protectStop, PROTECTSTOP, val)

#define PKS_SEC_KEY_GET_DEACTIVATION(key)                   \
    PKS_SEC_KEY_GET_DEF(key, deactivation, DEACTIVATION)
#define PKS_SEC_KEY_SET_DEACTIVATION(key, val)                  \
    PKS_SEC_KEY_SET_DEF(key, deactivation, DEACTIVATION, val)

#define PKS_SEC_KEY_GET_GENMECHANISM(key)                   \
    PKS_SEC_KEY_GET_DEF(key, genMechanism, GENMECHANISM)
#define PKS_SEC_KEY_SET_GENMECHANISM(key, val)                  \
    PKS_SEC_KEY_SET_DEF(key, genMechanism, GENMECHANISM, val)

// The field get/set bits ops
#define PKS_SEC_KEY_GET_DEF(key, member, field)         \
    (((key)->member >> (uint32_t)(PKS_SEC_KEY_##field##_SHIFT)) &   \
        (uint32_t)(PKS_SEC_KEY_##field##_MASK))

#define PKS_SEC_KEY_SET_DEF(key, member, field, val)                    \
    do {                                                                \
        (key)->member &=                                                \
            (~((uint32_t)(PKS_SEC_KEY_##field##_MASK) <<                \
                (PKS_SEC_KEY_##field##_SHIFT)));                        \
        (key)->member |=                                                \
            (((uint32_t)(val) & (uint32_t)(PKS_SEC_KEY_##field##_MASK)) << \
                (PKS_SEC_KEY_##field##_SHIFT));                         \
    } while ((bool)0)

// The field get/set bits ops for GDO
#define PKS_GDO_GET_DEF(key, member, field)                             \
    (((key)->member >> (uint32_t)(PKS_GDO_##field##_SHIFT)) &           \
        (uint32_t)(PKS_GDO_##field##_MASK))

#define PKS_GDO_SET_DEF(key, member, field, val)                        \
    do {                                                                \
        (key)->member &=                                                \
            (~((uint32_t)(PKS_GDO_##field##_MASK) <<                    \
                (PKS_GDO_##field##_SHIFT)));                            \
        (key)->member |=                                                \
            (((uint32_t)(val) & (uint32_t)(PKS_GDO_##field##_MASK)) <<  \
                (PKS_GDO_##field##_SHIFT));                             \
    } while ((bool)0)

#define PKS_SEC_KEY_BYTE_SIZE_ID         (PKS_KEY_BYTE_SIZE_ID)
#define PKS_SEC_KEY_BYTE_SIZE_LABEL      (PKS_KEY_BYTE_SIZE_LABEL)
#define PKS_SEC_KEY_BYTE_SIZE_MECHANISMS (PKS_KEY_BYTE_SIZE_MECHANISMS)
#define PKS_SEC_KEY_BYTE_SIZE_MAC        (PKS_KEY_BYTE_SIZE_MAC)

#endif /* TEE_CLIENT_PKCS11KS_DATATYPES_H */
