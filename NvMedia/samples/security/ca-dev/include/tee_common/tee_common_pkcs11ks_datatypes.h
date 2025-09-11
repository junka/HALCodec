//
// Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
//
// NVIDIA CORPORATION and its licensors retain all intellectual property
// and proprietary rights in and to this software, related documentation
// and any modifications thereto.  Any use, reproduction, disclosure or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA CORPORATION is strictly prohibited.
//

#ifndef TEE_COMMON_PKCS11KS_DATATYPES_H
#define TEE_COMMON_PKCS11KS_DATATYPES_H

#include <tee_common/tee_common_pkcs11ks_constants.h>

/// \brief Defines the handle for a key
typedef uint32_t KeyHandle;
/// \brief Attributes of a PKCS object metadata. IMP Note: Any new member
///        added to the pkcsSymmetricKeyObject struct needs to be reviewed
///        for sharing with PKCS11 clients. The struct members which are
///        not shared with clients are zeroed out in the
///        KEYSTORE_SERVICE_PKCS11_GET_KEY_OBJ_METADATA and 
///        KEYSTORE_SERVICE_PKCS11_PERSISTENT_GET_KEY_OBJ_METADATA commands
///
typedef struct __attribute__((__packed__)) pkcsSymmetricKeyObject {
    /// \brief Contains key attribute metadata
    uint32_t keyAttrMetadata;
    /// \brief Contains the type of key represented by this object.
    uint32_t keyType;
    /// \brief Contains bitstring that encodes the tokens this object is a
    //         part of.
    uint32_t token;
    /// \brief Contains crypto period and mechanism metadata
    uint32_t  keyCryptoMetadata;
    /// \brief CryptoPeriod when key was created.
    uint32_t initialTime;
    /// \brief CryptoPeriod for key used.
    uint32_t processStart;
    /// \brief CryptoPeriod after which key can not be used.
    uint32_t protectStop;
    /// \brief CryptoPeriod when key will be deactivated.
    uint32_t deactivation;
    /// \brief Mechanism that the cryptographic key in the KEY field
    ///        was created with.
    uint32_t genMechanism;
    /// \brief object ID encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  objId[PKS_KEY_BYTE_SIZE_ID];
    /// \brief object label encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  label[PKS_KEY_BYTE_SIZE_LABEL];
    /// \brief object unique ID encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  uniqueId[PKS_KEY_BYTE_SIZE_UNIQUE_ID];
    /// \brief List of supported Mechanisms
    uint32_t mechanisms[PKS_KEY_BYTE_SIZE_MECHANISMS / 4];
    /// \brief key size.
    uint32_t keySize;
    /// \brief initialization vector (IV) used to encrypt the cryptographic key
    ///        in the KEY field.
    uint8_t  iv[PKS_SEC_KEY_BYTE_SIZE_IV];
    /// \brief Encoded key data
    uint8_t  encKeyData[PKS_SEC_KEY_BYTE_SIZE_KEY];
    /// \brief MAC generated using AES-GCM, compliant with NIST38B,
    ///        over all fields except macData.
    uint8_t  macData[PKS_KEY_BYTE_SIZE_MAC];
} pkcsSymmetricKey_t; ///< pkcsSymmetricKey_t

/// \brief Attributes of a PKCS object metadata. IMP Note: Any new member
///        added to the pkcsRsaPubKeyObject struct needs to be reviewed
///        for sharing with PKCS11 clients.
///
typedef struct __attribute__((__packed__)) pkcsRsaPubKeyObject {
    /// \brief Contains key attribute metadata
    uint32_t keyAttrMetadata;
    /// \brief Contains the type of key represented by this object.
    uint32_t keyType;
    /// \brief Contains bitstring that encodes the tokens this object is a
    //         part of.
    uint32_t token;
    /// \brief Contains crypto period and mechanism metadata
    uint32_t  keyCryptoMetadata;
    /// \brief CryptoPeriod when key was created.
    uint32_t initialTime;
    /// \brief CryptoPeriod for key used.
    uint32_t processStart;
    /// \brief CryptoPeriod after which key can not be used.
    uint32_t protectStop;
    /// \brief CryptoPeriod when key will be deactivated.
    uint32_t deactivation;
    /// \brief Mechanism that the cryptographic key in the KEY field
    ///        was created with.
    uint32_t genMechanism;
    /// \brief object ID encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  objId[PKS_KEY_BYTE_SIZE_ID];
    /// \brief object label encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  label[PKS_KEY_BYTE_SIZE_LABEL];
    /// \brief object unique ID encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  uniqueId[PKS_KEY_BYTE_SIZE_UNIQUE_ID];
    /// \brief List of supported Mechanisms
    uint32_t mechanisms[PKS_KEY_BYTE_SIZE_MECHANISMS / 4];
    /// \brief key size.
    uint32_t keySize;
    /// \brief key exponent
    uint8_t  exponent[PKS_RSA_KEY_PUB_EXP_SIZE_KEY];
    /// \brief key modulus
    uint8_t  modulus[PKS_RSA_KEY_BYTE_SIZE_KEY];
    /// \brief reserved
    uint8_t  reserved[PKS_RSA_KEY_PUB_RSV_SIZE_KEY];
    /// \brief initialization vector (IV) used to encrypt the cryptographic key
    ///        in the KEY field.
    uint8_t  iv[PKS_RSA_KEY_BYTE_SIZE_IV];
    ///        over all fields except macData.
    uint8_t  macData[PKS_KEY_BYTE_SIZE_MAC];
} pkcsRsaPubKey_t;

/// \brief Attributes of a PKCS object metadata. IMP Note: Any new member
///        added to the pkcsEccKeyObject struct needs to be reviewed
///        for sharing with PKCS11 clients.
///
typedef struct __attribute__((__packed__)) pkcsEccKeyObject {
    /// \brief Contains key attribute metadata
    uint32_t keyAttrMetadata;
    /// \brief Contains the type of key represented by this object.
    uint32_t keyType;
    /// \brief Contains bitstring that encodes the tokens this object is a
    //         part of.
    uint32_t token;
    /// \brief Contains crypto period and mechanism metadata
    uint32_t  keyCryptoMetadata;
    /// \brief CryptoPeriod when key was created.
    uint32_t initialTime;
    /// \brief CryptoPeriod for key used.
    uint32_t processStart;
    /// \brief CryptoPeriod after which key can not be used.
    uint32_t protectStop;
    /// \brief CryptoPeriod when key will be deactivated.
    uint32_t deactivation;
    /// \brief Mechanism that the cryptographic key in the KEY field
    ///        was created with.
    uint32_t genMechanism;
    /// \brief object ID encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  objId[PKS_KEY_BYTE_SIZE_ID];
    /// \brief object label encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  label[PKS_KEY_BYTE_SIZE_LABEL];
    /// \brief object unique ID encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  uniqueId[PKS_KEY_BYTE_SIZE_UNIQUE_ID];
    /// \brief List of supported Mechanisms
    uint32_t mechanisms[PKS_KEY_BYTE_SIZE_MECHANISMS / 4];
    /// \brief Curve specifier.
    uint32_t curve;
    /// \brief point X
    uint8_t  pointX[PKS_ECC_KEY_BYTE_SIZE_KEY];
    /// \brief point Y
    uint8_t  pointY[PKS_ECC_KEY_BYTE_SIZE_KEY];
    /// \brief reserved
    uint8_t  reserved[PKS_ECC_KEY_RSV_SIZE_KEY];
    /// \brief initialization vector (IV) used to encrypt the cryptographic key
    ///        in the KEY field.
    uint8_t  iv[PKS_ECC_KEY_BYTE_SIZE_IV];
    ///        over all fields except macData.
    ///        over all fields except macData.
    uint8_t  macData[PKS_KEY_BYTE_SIZE_MAC];
} pkcsEccPubKey_t;

/// \brief Attributes of a PKCS object metadata. IMP Note: Any new member
///        added to the pkcsRsaPrivKeyObject struct needs to be reviewed
///        for sharing with PKCS11 clients.
///
typedef struct __attribute__((__packed__)) pkcsRsaPrivKeyObject {
    /// \brief Contains key attribute metadata
    uint32_t keyAttrMetadata;
    /// \brief Contains the type of key represented by this object.
    uint32_t keyType;
    /// \brief Contains bitstring that encodes the tokens this object is a
    //         part of.
    uint32_t token;
    /// \brief Contains crypto period and mechanism metadata
    uint32_t  keyCryptoMetadata;
    /// \brief CryptoPeriod when key was created.
    uint32_t initialTime;
    /// \brief CryptoPeriod for key used.
    uint32_t processStart;
    /// \brief CryptoPeriod after which key can not be used.
    uint32_t protectStop;
    /// \brief CryptoPeriod when key will be deactivated.
    uint32_t deactivation;
    /// \brief Mechanism that the cryptographic key in the KEY field
    ///        was created with.
    uint32_t genMechanism;
    /// \brief object ID encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  objId[PKS_KEY_BYTE_SIZE_ID];
    /// \brief object label encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  label[PKS_KEY_BYTE_SIZE_LABEL];
    /// \brief object unique ID encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  uniqueId[PKS_KEY_BYTE_SIZE_UNIQUE_ID];
    /// \brief List of supported Mechanisms
    uint32_t mechanisms[PKS_KEY_BYTE_SIZE_MECHANISMS / 4];
    /// \brief key size.
    uint32_t keySize;
    /// \brief key exponent
    uint8_t  exponent[PKS_RSA_KEY_PUB_EXP_SIZE_KEY];
    /// \brief key modulus
    uint8_t  modulus[PKS_RSA_KEY_BYTE_SIZE_KEY];
    /// \brief reserved
    uint8_t  reserved[PKS_RSA_KEY_PRIV_RSV_SIZE_KEY];
    /// \brief initialization vector (IV) used to encrypt the cryptographic key
    ///        in the KEY field.
    uint8_t  iv[PKS_RSA_KEY_BYTE_SIZE_IV];
    /// \brief Encoded key data
    uint8_t  encKeyData[PKS_RSA_KEY_BYTE_SIZE_KEY];
    ///        over all fields except macData.
    uint8_t  macData[PKS_KEY_BYTE_SIZE_MAC];
} pkcsRsaPrivKey_t;

/// \brief Attributes of a PKCS object metadata. IMP Note: Any new member
///        added to the pkcsEccPrivKeyObject struct needs to be reviewed
///        for sharing with PKCS11 clients.
///
typedef struct __attribute__((__packed__)) pkcsEccPrivKeyObject {
    /// \brief Contains key attribute metadata
    uint32_t keyAttrMetadata;
    /// \brief Contains the type of key represented by this object.
    uint32_t keyType;
    /// \brief Contains bitstring that encodes the tokens this object is a
    //         part of.
    uint32_t token;
    /// \brief Contains crypto period and mechanism metadata
    uint32_t  keyCryptoMetadata;
    /// \brief CryptoPeriod when key was created.
    uint32_t initialTime;
    /// \brief CryptoPeriod for key used.
    uint32_t processStart;
    /// \brief CryptoPeriod after which key can not be used.
    uint32_t protectStop;
    /// \brief CryptoPeriod when key will be deactivated.
    uint32_t deactivation;
    /// \brief Mechanism that the cryptographic key in the KEY field
    ///        was created with.
    uint32_t genMechanism;
    /// \brief object ID encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  objId[PKS_KEY_BYTE_SIZE_ID];
    /// \brief object label encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  label[PKS_KEY_BYTE_SIZE_LABEL];
    /// \brief object unique ID encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  uniqueId[PKS_KEY_BYTE_SIZE_UNIQUE_ID];
    /// \brief List of supported Mechanisms
    uint32_t mechanisms[PKS_KEY_BYTE_SIZE_MECHANISMS / 4];
    /// \brief Curve specifier.
    uint32_t curve;
    /// \brief point X
    uint8_t  pointX[PKS_ECC_KEY_BYTE_SIZE_KEY];
    /// \brief point Y
    uint8_t  pointY[PKS_ECC_KEY_BYTE_SIZE_KEY];
    ///        over all fields except macData.
    /// \brief reserved
    uint8_t  reserved[PKS_ECC_KEY_RSV_SIZE_KEY];
    /// \brief initialization vector (IV) used to encrypt the cryptographic key
    ///        in the KEY field.
    uint8_t  iv[PKS_ECC_KEY_BYTE_SIZE_IV];
    /// \brief Encoded key data
    uint8_t  encKeyData[PKS_ECC_KEY_BYTE_SIZE_KEY];
    ///        over all fields except macData.
    uint8_t  macData[PKS_KEY_BYTE_SIZE_MAC];
} pkcsEccPrivKey_t;
/// \brief Attributes of a PKCS Generic Object metadata. IMP Note: Any new
///        member added to the pkcsSymmetricKeyObject struct needs to
///        be reviewed for sharing with PKCS11 clients. The struct
///        members which are not shared with clients are zeroed out in the
///        KEYSTORE_SERVICE_PKCS11_GET_AES_KEY_METADATA command.
///
typedef struct __attribute__((__packed__)) pkcsGenericDataObject {
    /// \brief Contains key attribute metadata
    uint32_t keyAttrMetadata;
    /// \brief Contains bitstring that encodes the tokens this object is a
    //         part of.
    uint32_t token;
    /// \brief Metadata specification ID field encoded in UTF-8 from the BASIC LATIN
    ///        collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  objId[PKS_KEY_BYTE_SIZE_ID];
    /// \brief object LABEL encoded in UTF-8 from the BASIC LATIN collection
    ///        from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  label[PKS_KEY_BYTE_SIZE_LABEL];
    /// \brief object UNIQUEID encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  uniqueId[PKS_KEY_BYTE_SIZE_UNIQUE_ID];
    /// \brief object APPLICATION encoded in UTF-8 from the BASIC LATIN
    //         collection from ISO10646, that has no termination value
    ///        (i.e., not null terminated)
    uint8_t  application[PKS_GDO_BYTE_SIZE_APPLICATION];
    /// \brief Metadata specification OBJECTID field encoded in DER format
    ///        which is compliant with the CKA_OBJECT_ID field
    uint8_t  dataObjId[PKS_GDO_BYTE_SIZE_OBJECT_ID];
    /// \brief LENGTH of the value in bytes.
    uint32_t length;
    /// \brief reserved
    uint8_t  reserved[PKS_GDO_RSV_BYTE_SIZE];
    /// \breif object's VALUE containing the generic data object
    uint8_t  value[PKS_GDO_VALUE_BYTE_SIZE];
    /// \brief initialization vector (IV) used to authenticate to the
    ///        metadata
    uint8_t  iv[PKS_GDO_BYTE_SIZE_IV];
    /// \brief MAC generated using AES-GMAC, compliant with NIST38D,
    ///        using all other fields excluding iv and macData as
    ///        AAD.
    uint8_t  macData[PKS_GDO_BYTE_SIZE_MAC];
} pkcsGenericDataObject_t; ///< pkcsGenericDataObject_t

#endif
