/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: NV PKCS11 constants </b>
 *
 * @b Description: Describes NV PKCS11 KeyStore constants.
 */

#ifndef TEE_CLIENT_PKCS11KS_CONSTANTS_H
#define TEE_CLIENT_PKCS11KS_CONSTANTS_H

enum Pkcs11KsConstants {
    /// \brief Max count of sessions per token
    PKS_MAX_SESSION_COUNT_PER_TOKEN = 8U,
    /// \brief Max count of session
    PKS_MAX_SESSION_COUNT = 16U,
    /// \brief Max count of symmetric keys per session
    PKS_MAX_SYM_KEY_CNT_PER_SESSION = 64U,
    /// \brief Max count of asymmetric public keys per session
    PKS_MAX_RSA_PUB_KEY_CNT_PER_SESSION = 8U,
    /// \brief Max count of asymmetric private keys per session
    PKS_MAX_ECC_PUB_KEY_CNT_PER_SESSION = 8U,
    /// \brief Max count of asymmetric private keys per session
    PKS_MAX_ECC_PRIV_KEY_CNT_PER_SESSION = 8U,
    /// \brief Max count of Generic Data Objects per session
    PKS_MAX_GDO_CNT_PER_SESSION = 2U,
    /*
     * Adding or removing a key type from the enum should be followed
     * by updation of corresponding SECURE_STORAGE_MAX_OBJECT_COUNT.
     */
    /// \brief Max count of cached persistent sysmmetric keys
    PKS_MAX_PERSISTENT_SYM_KEY_CNT = 68U,
    /// \brief Max count of cached persistent RSA public keys
    PKS_MAX_PERSISTENT_RSA_PUB_KEY_CNT = 16U,
    /// \brief Max count of cached persistent ECC public keys
    PKS_MAX_PERSISTENT_ECC_PUB_KEY_CNT = 16U,
    /// \brief Max count of cached persistent ECC private keys
    PKS_MAX_PERSISTENT_ECC_PRIV_KEY_CNT = 16U,
    /// \brief Max count of cached Generic Data Objects
    PKS_MAX_PERSISTENT_GDO_CNT = 16U,
    /// \brief Max count of KEK
    PKS_MAX_KEK_KEY_CNT = 3U,
    /// \brief Dummy session ID for persistent key objects
    PKS_DUMMY_SESSION_ID = 0xFFFFFFFFU,
};

/// \brief Defines TOS PKCS#11 trusted application commandIDs
///        available to clients.

typedef enum {
    /// <b> Description </b>
    /// \brief Command to ping and check response from PKCS#11 trusted application.
    ///
    /// To check if call is going into PKCS#11 service successfully,
    /// ie, test the reach-ability of PKCS#11 service.
    ///
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \return TEE_SUCCESS If successful.
    /// \return TEE_ERROR_BAD_PARAMETERS If invalid parameter is passed.
    ///
    PKCS11_SERVICE_PING = 0x00000001,
} NV_TE_PKCS11Operation;

/// \defgroup oksm_enums Oem Keystore PKCS11 Extension Enumerators
///@{

enum PKSFusedKeySlots {
/// \brief Defines key handle constant for KEK0 fused key.
    PKS_FUSED_KEYSLOT_KEK0 = 0xFFFAU,
/// \brief Defines key handle constant for KEK2 fused key.
    PKS_FUSED_KEYSLOT_KEK2 = 0xFFFBU,
};

/// TODO: As some tests/pkcs11 applications are common for T19x and T23x,
///       change the T19x key names to be same as T23x's. The cleaner fix
///       is to have tests/applications use different names as per the
///       platform.
/// \brief Defines KEK0 Fused Keyslot object id to
///        lookup KEK0 fused key handle
#define NV_FUSED_KEY_KEK0_OBJ_ID         "NV_OEM_KEY1                     "
/// \brief Defines the KEK2 Fused Keyslot object id to
///        lookup KEK2 fused key handle
#define NV_FUSED_KEY_KEK2_OBJ_ID         "NV_OEM_KEY2                     "

/// \brief Defines object id to
///        lookup PSC_OEM_K1_KEY derived from ODM_KDK0
#define NV_OEM_KEY1_OBJ_ID         "NV_OEM_KEY1                     "
/// \brief Defines object id to
///        lookup PSC_OEM_K2_KEY derived from ODM_KDK0
#define NV_OEM_KEY2_OBJ_ID         "NV_OEM_KEY2                     "
/// \brief Defines object id to
///        lookup PSC_OEM_K3_KEY derived from ODM_KDK0
#define NV_OEM_KEY3_OBJ_ID         "NV_OEM_KEY3                     "

/// Security Engine (SE) user values
#define PKS_SE_USER_TZ  (2U) ///< TrustZone, secure world SE
#define PKS_SE_USER_GP  (3U) ///< General-purpose, non-secure world SE

/// \enum PKS Key Object Type
///
/// \brief Describes the supported PKCS11 key object types.
enum PKSKeyObjType {
    PKS_KEY_OBJTYPE_SEC_KEY    = 0x1U,
    PKS_KEY_OBJTYPE_RSA_PUBKEY = 0x2U,
    PKS_KEY_OBJTYPE_ECC_PUBKEY = 0x3U,
    PKS_KEY_OBJTYPE_RSA_PRIKEY = 0x4U,
    PKS_KEY_OBJTYPE_ECC_PRIKEY = 0x5U,
    PKS_KEY_OBJTYPE_GDO        = 0x7U,
    PKS_KEY_OBJTYPE_FUSED_KEY  = 0xFU,
    PKS_KEY_OBJTYPE_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKS Key versions
///
/// \brief Describes the supported PKCS11 key object versions.
enum PKSKeyVersion {
    /// \brief PKCS11 key object version
    PKS_KEY_VERSION_01    = 0x1U,
    PKS_KEY_VERSION_02    = 0x2U,
    PKS_KEY_INVALID_VERSION = 0xFU,
    PKS_KEY_VERSION_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKS Key
///
/// \brief Describes if a key can be destroyed with C_DestroyObject()
enum PKSKeyDST {
    /// \brief key cannot be destroyed
    PKS_DST_KEY_DESTROYABLE = 0x0U,
    /// \brief key cannot be destroyed
    PKS_DST_KEY_NOT_DESTROYABLE = 0x1U,
    PKS_DST_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyCPY
///
/// \brief Describes if a key can be copied with C_CopyObject()
enum PKSKeyCPY {
    /// \brief key may be copied
    PKS_CPY_KEY_COPYABLE = 0x0U,
    /// \brief key cannot be copied
    PKS_CPY_KEY_NOT_COPYABLE = 0x1U,
    PKS_CPY_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyMOD
///
/// \brief Describes if a key attributes are modifiable with
///        C_SetAttributeValue()
enum PKSKeyMOD {
    /// \brief key's attributes may be modified
    PKS_MOD_KEY_MODIFIABLE = 0x0U,
    /// \brief key's attributes cannot be modified
    PKS_MOD_KEY_NOT_MODIFIABLE = 0x1U,
    PKS_MOD_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyKCV
///
/// \brief Describes key's Key Check Value validity
enum PKSKeyKCV {
    /// \brief key's KEYCHECK field has a key check value
    PKS_KCV_VALID_KEYCHECK = 0x0U,
    /// \brief key's KEYCHECK field shall be ignored
    PKS_KCV_INVALID_KEYCHECK = 0x1U,
    PKS_KCV_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyLCL
///
/// \brief Describes if a key is generated locally
enum PKSKeyLCL {
    /// \brief key generated locally or from an object copy
    PKS_LCL_KEY_LOCAL = 0x0U,
    /// \brief key not generated locally or from an object copy
    PKS_LCL_KEY_NOT_LOCAL = 0x1U,
    PKS_LCL_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyTST
///
/// \brief Describes if a key is trusted wrapping key
enum PKSKeyTST {
    /// \brief key is a trusted wrapping key
    PKS_TST_KEY_TRUSTED = 0x0U,
    /// \brief key is not a trusted wrapping key
    PKS_TST_KEY_NOT_TRUSTED = 0x1U,
    PKS_TST_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyEXT
///
/// \brief Describes the Ex-tractability of a key.
enum PKSKeyEXT {
    /// \brief key may be extracted when wrapped
    PKS_EXT_KEY_EXTRACTABLE = 0x0U,
    /// \brief key may be extracted when wrapped with a trusted key
    PKS_EXT_KEY_EXTRACTABLE_WITH_TRUSTED = 0x4U,
    /// \brief key shall not be extracted which was previously extractable
    PKS_EXT_KEY_NOT_EXTRACTABLE = 0x1U,
    /// \brief key shall not be extracted which was never extractable
    PKS_EXT_KEY_NEVER_EXTRACTABLE = 0x3U,
    PKS_EXT_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeySEN
///
/// \brief Describes the Sensitivity of a key.
enum PKSKeySEN {
    /// \brief Key has the sensitive property and
    ///        does not have the always sensitive property
    PKS_SEN_KEY_SENSITIVE = 0x0U,
    /// \brief Key has the sensitive and always sensitive properties
    PKS_SEN_KEY_ALWAYS_SENSITIVE = 0x1U,
    /// \brief Invalid sensitivity encoding
    PKS_SEN_KEY_INVALID = 0x2U,
    /// \brief Key does not have sensitive and always sensitive properties
    PKS_SEN_KEY_NOT_SENSITIVE = 0x3U,
    PKS_SEN_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyPurpose
///
/// \brief Describes the Purpose of the key. It's a combination of
///        key fields ASY, SYM, WRP, MAC, DRV and KAG
enum PKSKeyPurpose {
    PKS_KEYPURPOSE_ENCRYPT_DECRYPT = 0x33fU,
    PKS_KEYPURPOSE_ENCRYPT = 0x37fU,
    PKS_KEYPURPOSE_DECRYPT = 0x3bfU,
    PKS_KEYPURPOSE_SIGN = 0x1ffU,
    PKS_KEYPURPOSE_VERIFY = 0x2ffU,
    PKS_KEYPURPOSE_WRAP_UNWRAP = 0x3cfU,
    PKS_KEYPURPOSE_WRAP = 0x3dfU,
    PKS_KEYPURPOSE_UNWRAP = 0x3efU,
    PKS_KEYPURPOSE_MAC_GEN_VERIFY = 0x3f3U,
    PKS_KEYPURPOSE_MAC_VERIFY = 0x3f7U,
    PKS_KEYPURPOSE_MAC_GEN = 0x3fbU,
    PKS_KEYPURPOSE_DERIVE = 0x3fdU,
    PKS_KEYPURPOSE_KAG = 0x3feU,
    PKS_KEYPURPOSE_NO_PURPOSE = 0x3ffU,
    PKS_KEYPURPOSE_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKCS11_CK_KEY_TYPE
///
/// \brief Describes the supported PKCS11 key types.
enum PKCS11_CK_KEY_TYPE {
    PKCS11_CKK_RSA = 0x00000000U,
    PKCS11_CKK_EC = 0x00000003U,
    PKCS11_CKK_AES = 0x0000001FU,
    PKCS11_CKK_GENERIC_SECRET = 0x00000010U,
    PKCS11_CKK_EC_EDWARDS = 0x00000040U,
    PKCS11_CKK_EC_MONTGOMERY = 0x00000041U,
    PKCS11_CKK_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKCS11_CK_MECHANISM_TYPE
///
/// \brief Describes the supported PKCS11 Mechanism types.
enum PKCS11_CK_MECHANISM_TYPE {
    PKCS11_CKM_RSA_PKCS_PSS = 0x0000000DU,
    PKCS11_CKM_ECDSA = 0x00001041U,
    PKCS11_CKM_EC_EDWARDS_KEY_PAIR_GEN = 0x00001055U,
    PKCS11_CKM_EC_MONTGOMERY_KEY_PAIR_GEN = 0x00001056U,
    PKCS11_CKM_EDDSA = 0x00001057U,
    PKCS11_CKM_AES_KEY_GEN = 0x00001080U,
    PKCS11_CKM_AES_CCM = 0x00001088U,
    PKCS11_CKM_AES_CMAC = 0x0000108AU,
    PKCS11_CKM_AES_GMAC = 0x0000108EU,
    PKCS11_CKM_SP800_108_COUNTER_KDF = 0x000003ACU,
    PKCS11_CKM_AES_CBC = 0x00001082U,
    PKCS11_CKM_AES_CBC_PAD = 0x00001085U,
    PKCS11_CKM_AES_CTR = 0x00001086U,
    PKCS11_CKM_AES_GCM = 0x00001087U,
    PKCS11_CKM_AES_WRAP_UNWRAP_MECH = 0x00001089U,
    PKCS11_CKM_SHA256 = 0x00000250U,
    PKCS11_CKM_SHA256_HMAC = 0x00000251U,
    PKCS11_CKM_SHA384_HMAC = 0x00000261U,
    PKCS11_CKM_SHA512 = 0x00000270U,
    PKCS11_CKM_ECDH1_DERIVE = 0x00001050U,
    PKCS11_CKM_NVIDIA_AES_CBC_KEY_DATA_WRAP = 0x80000001U, // (CKM_VENDOR_DEFINED | 0x00000001UL)
    PKCS11_CKM_SP800_56C_TWO_STEPS_KDF = 0x80000002U, // (CKM_VENDOR_DEFINED | 0x00000002UL)
    PKCS11_CKM_NVIDIA_MACSEC_AES_KEY_WRAP = 0x80000003UL, // (CKM_VENDOR_DEFINED | 0x00000003UL)
    PKCS11_CKM_NVIDIA_PSC_AES_CMAC = 0x80000004UL,     // (CKM_VENDOR_DEFINED | 0x00000004UL)
    PKCS11_CKM_NVIDIA_AES_GCM_KEY_UNWRAP = 0x80000005UL,  //(CKM_VENDOR_DEFINED | 0x00000005UL)

    PKCS11_CKM_INFO_UNAVAILABLE = 0xFFFFFFFFU,
    PKCS11_CKM_MAX_SIZE = 0xFFFFFFFFU,

    /* TLS Support */
    PKCS11_CKM_TLS12_MAC = 0x000003D8U,
    PKCS11_CKM_TLS12_KDF = 0x000003D9U,
    PKCS11_CKM_TLS12_KEY_AND_MAC_DERIVE = 0x000003E1U,
    PKCS11_CKM_TLS12_MASTER_KEY_DERIVE_DH = 0x000003E2U,
    PKCS11_CKM_TLS12_KEY_SAFE_DERIVE = 0x000003E3U,
};

/// \enum PKSKeyCryptoPeriod
///
/// \brief Describes the crypto period of key.
enum PKSKeyCRYPRD {
    PKS_CRYPRD_INVALID_CRYPTO_PERIODS = 0x0U,
    PKS_CRYPRD_VALID_INITIAL_TIME = 0x1U,
    PKS_CRYPRD_VALID_PROCESS_START = 0x2U,
    PKS_CRYPRD_VALID_PROTECT_STOP = 0x4U,
    PKS_CRYPRD_VALID_DEACTIVATION = 0x8U,
    PKS_CRYPRD_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum PKSKeyIVGEN
///
/// \brief Describes the symmetric key's nonce type
enum PKSKeyIVGEN {
   /// \brief key's IV is randomly generated
    PKS_IVGEN_RANDOM = 0x00U,
    /// \brief key's IV is provided by caller
    PKS_IVGEN_USER = 0x01U,
    /// \brief IVGEN fld is invalid
    PKS_IVGEN_INVALID = 0x10U,
    /// \brief key's IV is randomly generated
    PKS_IVGEN_DEFAULT = 0x11U,
    PKS_IVGEN_MAX_SIZE = 0xFFFFFFFFU,
};

/// \enum  KeyConsts
///
/// \brief Describes the enumerators used in PKCS11 KeyStore.
enum {
    /// \brief Maximum number of mechanisms supported.
    ///        This value is limited to 4.
    PKS_MAX_SUPPORTED_MECHANISMS = 4U,
    PKS_MAX_ENUM_SIZE = 0xFFFFFFFFU,
};

/// \enum Ecc Curve Types
///
/// Describes the Curve used for ECC
/// Keep aligned with enum TEE_EccCurveID
enum PKCS11_CURVE_TYPE {
    PKCS11_CURVE_NIST_P_256 = 0x00000003U,
    PKCS11_CURVE_ED25519 = 0x00000300U,
    PKCS11_CURVE_C25519 = 0x00000301U,
};

/// \enum Secure Storage State
///
/// \brief Describes the state of secure storage
enum PKSSecureStorageState {
    /* Secure Storage Device Not Connected,
     * NOR Less or removed NOR before provision */
    PKS_SECURE_STORAGE_NOT_PRESENT = 0x1U,
    /* Secure Storage device connected but not provisioned */
    PKS_SECURE_STORAGE_NOT_PROVISIONED = 0x2U,
    /* Secure Storage device tampered or data tampered */
    PKS_SECURE_STORAGE_TAMPERED = 0x4U,

    /* General error detected */
    PKS_SECURE_STORAGE_GENERIC_ERROR = 0x8U,

    PKS_SECURE_STORAGE_FUNCTIONAL = 0x10U,

    /* Below error codes are used for T194 only, not valid for T23x */
    PKS_SECURE_STORAGE_PRESENT = 0x10000000U,
    PKS_SECURE_STORAGE_PRESENT_MAXSIZE = 0xFFFFFFFFU,
};

/// \enum RSA public key size
///
/// \brief Describes the supported RSA & ECC public key sizes
enum {
    PKS_RSA_PUBLIC_KEY_SIZE_384 = 384U,
    PKS_RSA_PUBLIC_KEY_SIZE_512 = 512U,
    PKS_ECC_PUBLIC_KEY_SIZE = 32U,
    PKS_PUBLIC_KEY_MAX_SIZE = 0xFFFFFFFFU
};

/// \enum ECC Private Key Size
///
/// \brief Describes the supported ECC private key sizes
enum {
    //TODO: Just keep Ecc_Priv_Key_Size or specific to curves?
    PKS_ECC_PRIVATE_KEY_SIZE = 32U,
    PKS_ECC_PRIV_CURVE_ED25519_KEY_SIZE = 32U,
    PKS_ECC_PRIV_CURVE_NIST_P256_KEY_SIZE = 32U
};

enum PKCS11_TLS_MAC_OPS{
    PKCS11_TLS_MAC_GEN = 0x0AU,
    PKCS11_TLS_MAC_VERIFY = 0x0BU,
};

enum PKCS11_AES_CMAC_OPS {
    PKCS11_AES_CMAC_GEN = 0x1AU,
    PKCS11_AES_CMAC_VERIFY = 0x1BU,
};

/// \enum Reset types
///
/// \brief Describes different types of resets available
///  NOTE: KEYSLOTS AND KEY OBJECTS ARE NOT IN USE AND ONLY ONE SESSION IS
///        OPENED FOR RESET.
enum {
    /// \brief Secure Storage and PKCS11 Keystore reload persistent keyobjects
    ///        to mimic platform reboot.
    /// PKCS11KS clears persistent object cache, resets keystore state to
    /// uninitialized, resets authentication state to unauthenticted,
    /// Read objects from secure nor, authenticates the objects read.
    /// This needs INIT_KEYSTORE_COMMAND which does the authentication of
    /// objects loaded from secure storage.
        PKS_KEYSTORE_AND_SECURE_STORAGE_FS_RELOAD = 0U,
    /// \brief Secure Storage and PKCS11 Keystore permanently erase persistent
    ///        key objects if Secure storage and Keystore allow deletion.
    /// PKCS11KS clears the persistent key object cache.
        PKS_KEYSTORE_AND_SECURE_STORAGE_FS_ERASE = 1U,
    /// \brief Secure storage and PKCS11 Keystore permanently erase persistent
    ///        key objects ignoring secure storage and keystore states.
    /// PKCS11KS clears the persistent key object cache.
        PKS_KEYSTORE_AND_SECURE_STORAGE_FS_FORCE_ERASE = 2U,
        PKS_KEYSTORE_AND_SECURE_STORAGE_FS_MAXSIZE = 0xFFFFFFFFU,
};

typedef enum {
    PKS_FSI_DYNAMIC_1_TOKEN_ID     = 2U,

    PKS_CCPLEX_SAFE_2_TOKEN_ID     = 3U,
    PKS_CCPLEX_DYNAMIC_2_TOKEN_ID  = 4U,

    PKS_TSEC_SAFE_3_TOKEN_ID       = 5U,
    PKS_TSEC_DYNAMIC_3_TOKEN_ID    = 6U,

    PKS_CCPLEX_SAFE_4_TOKEN_ID     = 7U,
    PKS_CCPLEX_DYNAMIC_4_TOKEN_ID  = 8U,

    PKS_CCPLEX_SAFE_5_TOKEN_ID     = 9U,
    PKS_CCPLEX_DYNAMIC_5_TOKEN_ID  = 10U,

    PKS_CCPLEX_SAFE_6_TOKEN_ID     = 11U,
    PKS_CCPLEX_DYNAMIC_6_TOKEN_ID  = 12U,

    PKS_CCPLEX_SAFE_7_TOKEN_ID     = 13U,
    PKS_CCPLEX_DYNAMIC_7_TOKEN_ID  = 14U,

    PKS_CCPLEX_SAFE_8_TOKEN_ID     = 15U,
    PKS_CCPLEX_DYNAMIC_8_TOKEN_ID  = 16U,

    PKS_CCPLEX_SAFE_9_TOKEN_ID     = 17U,
    PKS_CCPLEX_DYNAMIC_9_TOKEN_ID  = 18U,

    PKS_CCPLEX_SAFE_10_TOKEN_ID    = 19U,
    PKS_CCPLEX_DYNAMIC_10_TOKEN_ID = 20U,

    PKS_CCPLEX_SAFE_11_TOKEN_ID    = 21U,
    PKS_CCPLEX_DYNAMIC_11_TOKEN_ID = 22U,

    PKS_CCPLEX_SAFE_12_TOKEN_ID    = 23U,
    PKS_CCPLEX_DYNAMIC_12_TOKEN_ID = 24U,

    PKS_CCPLEX_SAFE_13_TOKEN_ID    = 25U,
    PKS_CCPLEX_DYNAMIC_13_TOKEN_ID = 26U,

    PKS_CCPLEX_SAFE_14_TOKEN_ID    = 27U,
    PKS_CCPLEX_DYNAMIC_14_TOKEN_ID = 28U,
    
    PKS_MAX_TOKEN_COUNT = PKS_CCPLEX_DYNAMIC_14_TOKEN_ID,

    PKS_TOKEN_ID_MAX_SIZE = 0xFFFFFFFFU,
} PKSTokenID;

typedef enum {
    PKS_FSI_SS_GROUPID       = 1U,
    PKS_CCPLEX_2_SS_GROUPID  = 2U,
    PKS_TSEC_3_SS_GROUPID    = 3U,
    PKS_CCPLEX_4_SS_GROUPID  = 4U,
    PKS_CCPLEX_5_SS_GROUPID  = 5U,
    PKS_CCPLEX_6_SS_GROUPID  = 6U,
    PKS_CCPLEX_7_SS_GROUPID  = 7U,
    PKS_CCPLEX_8_SS_GROUPID  = 8U,
    PKS_CCPLEX_9_SS_GROUPID  = 9U,
    PKS_CCPLEX_10_SS_GROUPID = 10U,
    PKS_CCPLEX_11_SS_GROUPID = 11U,
    PKS_CCPLEX_12_SS_GROUPID = 12U,
    PKS_CCPLEX_13_SS_GROUPID = 13U,
    PKS_CCPLEX_14_SS_GROUPID = 14U,
    PKS_MAX_SS_GROUP_COUNT = PKS_CCPLEX_14_SS_GROUPID,
} SSGroupIDs;

typedef enum {
    PKS_DEFAULT_CCPLEX_SS_GROUP_ID = PKS_CCPLEX_2_SS_GROUPID,
} DefaultPKSTokenID;

enum TLS_VERSION{
    /// \brief Supported versions of TLS
    TLS_VER_1_2 = 0x00000000,
    TLS_VER_1_3 = 0x00000001,
};

enum PKS_OPEN_SESSION_TYPE {
    PKS_DEFAULT_OPEN = 0x00000000U,
    PKS_KAT_OPEN     = 0x11111111U,
    PKS_KAT_PASS     = 0x22222222U,
    PKS_KAT_FAIL     = 0x44444444U,
};
///@} // pks_enums

static inline uint32_t PKS_GET_SS_GROUPID_FROM_PKS_TOKENID(const uint32_t pksTknId) {
    uint32_t retval = 0U;
  
    if (pksTknId > (UINT32_MAX - 1U)) {
        goto exit;
    }
    retval = (((pksTknId) + 1U) >> 1U);

    exit:
        return retval;
}

static inline uint32_t PKS_GET_SAFETY_TOKEN_ID_FROM_SS_GROUPID(const uint32_t groupId) {
    uint32_t retval = 0U;

    if (((groupId) << 1U) < 1U) {
        goto exit;
    } else {
        retval = (((groupId) << 1U) - 1U);
    }

    exit:
    return retval;
}

static inline uint32_t PKS_GET_DYNAMIC_TOKEN_ID_FROM_SS_GROUPID(const uint32_t groupId) {
    uint32_t retval = 0U;
    retval = (((groupId) << 1U));
    return retval;
}

#endif /* TEE_CLIENT_PKCS11KS_CONSTANTS_H */
