/*
 * Copyright (c) 2021-2024, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: NV Cryptographic Asymmetric Constants </b>
 *
 * @b Description: Describes NV cryptographic asymmetric constants.
 */

#ifndef NV_TE_CRYPTOGRAPHIC_ASYM_CONSTANTS_H
#define NV_TE_CRYPTOGRAPHIC_ASYM_CONSTANTS_H

/// \brief This type is used to contain Crypto-Asym-Service commands.
///
/// Below is the description of each command's TEE_Param
/// NV_TE_CryptoAsymServiceOperation
/// Implementation defined as an enum in nv_te_internal/nv_te_cryptographic_asym_constants.h
typedef enum {
    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for hash cryptographic operations. The INIT command
    /// is typically followed by corresponding UPDATE and DOFINAL commands
    /// to successfully complete the Digest(hash) cryptographic operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating memory to instance an object of the Digest Class, within the context
    ///    of the currently open session
    ///    3. Initializing the data memebers of the newly instanced object with
    ///    client provided values, sent as part of operation params
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Digest algorithm identifier (uint32_t);
    ///     \n Valid values:
    ///     \n TEE_ALG_SHA256
    ///     \n TEE_ALG_SHA384
    ///     \n TEE_ALG_SHA512
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BUSY if Digest context already exists.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types.
    /// \return TEE_ERROR_BAD_PARAMETERS if unsupported digest algorithm.
    ///
    CRYPTO_ASYM_SERVICE_DIGEST_INIT          = 0x00000004,

    /// <b> Description </b>
    /// \brief This command provides functionality to update the stage of
    /// hash cryptographic operations. The UPDATE command
    /// always follows an INIT command and is followed by the DOFINAL command
    /// to succesfully complete the Digest(hash) cryptographic operation.
    /// This delivers a block of data.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating DMA memory to store the source and destination buffers, within the context
    ///    of the currently open session
    ///    3. Updating the data memebers of the object
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1].memref.buffer Source plaintext buffer for Digest operation
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t)
    ///     \n Max range:
    ///     \n 1024 bytes
    /// \param[out]    params[2].memref.buffer Destination ciphertext buffer for Digest operation
    /// \param[in,out] params[2].memref.size Size of destination buffer (uint32_t)
    ///                this value is updated by TOS Cryptography Service
    ///     \n Max range:
    ///     \n 1024 bytes
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if source length is greater than max support size. Or if source buffer is null.
    /// \return TEE_ERROR_BAD_PARAMETERS if destination buffer is NULL
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed
    /// \return TEE_ERROR_GENERIC in case of error.
    ///
    CRYPTO_ASYM_SERVICE_DIGEST_UPDATE        = 0x00000005,

    /// <b> Description </b>
    /// \brief This command provides functionality to update the stage of
    /// hash cryptographic operations. The DO_FINAL command
    /// typically follows an INIT command or/and UPDATE command
    /// to succesfully complete the Digest(hash) cryptographic operation.
    /// This delivers a block of data and finishes the operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating DMA memory to store the source and destination buffers, within the context
    ///    of the currently open session
    ///    3. Updating the data memebers of the object
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1].memref.buffer Source plaintext buffer for Digest operation
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t);
    ///     \n Max range:
    ///     \n 1024 bytes
    /// \param[out]    params[2].memref.buffer Destination ciphertext buffer for Digest operation
    /// \param[in,out] params[2].memref.size Size of destination buffer (uint32_t);
    ///                this value is updated by TOS Cryptography Service
    ///     \n Max range:
    ///     \n 1024 bytes
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if source length is greater than max support size.
    /// \return TEE_ERROR_BAD_PARAMETERS if destination buffer is NULL
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed
    /// \return TEE_ERROR_GENERIC in case of error.
    ///
    CRYPTO_ASYM_SERVICE_DIGEST_DO_FINAL      = 0x00000006,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    CRYPTO_ASYM_SERVICE_DIGEST_FREE          = 0x00000007,

    /// <b> Description </b>
    /// \brief This command provides functionality for RSA verify operation.
    /// This command is responsible for the following operations:
    ///   1) Input validation of incoming parameters based on the various permutations of
    ///      valid input values.
    ///   2) Allocating memory to instance an object of the Digest Class, within the context
    ///      of the currently open session.
    ///   3) Initializing the data members of the newly instanced object with client provided
    ///      values, sent as part of operation params.
    ///   4) Perform verify operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(TEE_OperationAlgorithm): RSA algorithm identifier
    /// valid_range:
    ///               TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA256, TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA384,
    ///               TEE_ALG_RSASSA_PKCS1_PSS_MGF1_SHA512, TEE_ALG_RSASSA_PKCS1_V1_5_SHA256
    /// \param[in] params[0].value.b(TEE_OperationMode): RSA operation mode
    /// valid_range:
    ///               TEE_MODE_VERIFY
    /// \param[in] params[1].memref.buffer(uint8_t*): Digest or plaintext source data buffer
    ///                                               for RSA operation
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of source buffer
    /// valid_range:
    ///               20 to 64:  Digest,
    ///               1 to 1024:  Raw message
    /// \param[in] params[2].memref.buffer(uint8_t*): Source buffer with signature data
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[2].memref.size(uint32_t): Size of signature buffer
    /// valid_range:
    ///               384, 512
    /// \param[out] params[3].value.a(uint32_t): Result of RSA operation. Meaningful only
    ///                                          if operation succeeds
    /// valid_range:
    ///               0 Valid signature, 1 Invalid signature
    /// \param[in]  params[3].value.b(uint32_t): RSA operation flag
    /// valid_range:
    ///               1 source is digest, 0 source is raw data
    /// \param[in] params[4].memref.buffer(KeySlotHandle*): Pointer to the buffer
    ///                                                     containg keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[4].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer,
    ///         or if digest size or signature size is more than max supported size,
    ///         or if digest or signature buffer is null,
    ///         or if invalid keyslot type or keyslot handle is 0.
    /// \retval TEE_ERROR_NOT_SUPPORTED if unsupported algorithm is given as input.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \retval TEE_ERROR_GENERIC if failed to get key slot index,
    ///         or failed to setup crypto context, or RSA operation failed.
    ///
    CRYPTO_ASYM_SERVICE_RSA_HANDLE_REQUEST   = 0x00000011,

    /// <b> Description </b>
    /// \brief This command provides functionality to request a PKA key slot
    /// It is responsible for the following operations:
    ///   1) Input validation of incoming parameters based on the various permutations
    ///      of valid input values.
    ///   2) Set requested parameters to keyslot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type.
    /// valid_range:
    ///               KEYSLOT_TYPE_PKA1
    /// \param[in] params[0].value.b(KeySlotUsageInfo): Valid Keyslot usage info.
    /// valid_range:
    ///               KEYSLOT_USAGE_SIGN, KEYSLOT_USAGE_VERIFY
    /// \param[out] params[1].memref.buffer(KeySlotHandle*): buffer containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[out] params[1].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types,
    ///         or if unknown keyslot usage.
    /// \retval TEE_ERROR_BAD_STATE if KeySlotMgr is not initialized
    /// \retval TEE_ERROR_ACCESS_DENIED if caller is not a valid keyslot owner
    /// \retval TEE_ERROR_BUSY if keyslot is in use
    ///
    CRYPTO_ASYM_SERVICE_REQUEST_SE_KEYSLOT   = 0x00000014,

    /// <b> Description </b>
    /// \brief This command provides functionality to set a key slot with key material.
    /// It is responsible for the following operations:
    ///   1) Input validation of incoming parameters based on the various permutations
    ///      of valid input values.
    ///   2) Set requested parameters to keyslot.
    ///
    /// <b> PKA1 Keyslot Parameters </b>
    ///
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type.
    /// valid_range:
    ///               KEYSLOT_TYPE_PKA1
    /// \param[in] params[0].value.b(uint32_t): PKA1 key flag
    /// valid_range:
    ///               0: RSA Key in little endianness,
    ///               1: RSA Key in big endianness,
    ///               2: ECDSA Nist curve point X and Y,
    ///               3: EDDSA Nist curve compressed point
    /// \param[in] params[1].value.b(uint32_t): RSA key Montgomery flag, not meaningful for EC key
    /// valid_range:
    ///               0: RSA key without Montgomery values,
    ///               1: RSA key with Montgomery values
    /// \param[in] params[2].memref.buffer(uint8_t*): RSA key exponent, or EC pointX
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[2].memref.size(uint32_t): RSA key exponent size, EC pointX size
    /// valid_range:
    ///               4 bytes: RSA key exponent,
    ///               32 bytes: ECC key pointX
    /// \param[in] params[3].memref.buffer(uint8_t*): RSA key modulus, or ECDSA pointY
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[3].memref.size(uint32_t): RSA modulus size, or ECDSA pointY size
    /// valid_range:
    ///               384, 512 bytes: RSA modulus,
    ///               32 bytes: ECC key pointY
    /// \param[in] params[4].memref.buffer(KeySlotHandle*): buffer containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[4].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    ///
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer,
    ///         or if keyslot owner VMID check failed, Or if failed to get keyslot,
    ///         or if modulusOrIvSize or exponentOrKeySize is out of range(UINT32_MAX bits)
    /// \retval TEE_ERROR_GENERIC if failed to set key, or if key size is invalid,
    ///         or if failed to write keyslot
    CRYPTO_ASYM_SERVICE_UPDATE_SE_KEYSLOT    = 0x00000015,

    /// <b> Description </b>
    /// \brief This command provides functionality to release a key slot.
    /// It is responsible for the following operations:
    ///    1) Input validation of incoming parameters based on the various permutations
    ///       of valid input values
    ///    2) Release the given keyslot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type of the key slot to release.
    /// valid_range:
    ///               KEYSLOT_TYPE_PKA1
    /// \param[in] params[1].memref.buffer(KeySlotHandle*): buffer containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer,
    ///         or if keyslot owner VMID check failed, or if failed to get keyslot.
    /// \retval TEE_ERROR_GENERIC if failed to release keyslot.
    ///
    CRYPTO_ASYM_SERVICE_RELEASE_SE_KEYSLOT   = 0x00000016,

    /// <b> Description </b>
    /// \brief ECDSA initialize parameters, supports verification only.
    /// Requirments for ECDSA:
    ///     1) ASN.1 DER format signature in big endian, which is Openssl compatible.
    ///     2) Big endian point coordinates.
    ///     3) Digested source data.
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(TEE_OperationAlgorithm): Supported algorithm identifier
    /// valid_range:
    ///               TEE_ALG_ECDSA_SHA256
    /// \param[in] params[0].value.b(TEE_EccCurveID): supported Curve ID
    /// valid_range:
    ///               TEE_ECC_CURVE_NIST_P256
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameter types.
    /// \retval TEE_ERROR_BUSY if EC object already exist.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \retval TEE_ERROR_GENERIC if SE INIT operation failed
    ///
    CRYPTO_ASYM_SERVICE_ECDSA_INIT           = 0x0000001C,

    /// <b> Description </b>
    /// \brief ECDSA set_key parameters.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].memref.buffer(uint8_t*): Buffer containing raw source data.
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[0].memref.size(uint32_t): Size of buffer containing point X
    /// valid_range:
    ///               32
    /// \param[in] params[1].memref.buffer(uint8_t*): Buffer containing point Y.
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing point Y
    /// valid_range:
    ///               32
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameter types or null buffer,
    ///         or if x_size or y_size is > TE_MAX_ECC_BYTES
    /// \retval TEE_ERROR_GENERIC if SE SET_KEY operation failed
    ///
    CRYPTO_ASYM_SERVICE_ECDSA_SET_KEY        = 0x0000001D,

    /// <b> Description </b>
    /// \brief Deliver a block of data and finishes the operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].memref.buffer(uint8_t*): Source buffer containing digest
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[0].memref.size(uint32_t): Size of buffer containing digest
    /// valid_range:
    ///               32
    /// \param[in] params[1].memref.buffer(uint8_t*): Buffer containing signature
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of signature
    /// valid_range:
    ///               1 to 1792 bytes
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameter types or null buffer,
    ///         or if digest or signature buffer length is more than max supported size.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory
    /// \retval TEE_ERROR_GENERIC if SE DO_FINAL operation failed
    ///
    CRYPTO_ASYM_SERVICE_ECDSA_DO_FINAL       = 0x0000001E,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported parameter types.
    ///
    CRYPTO_ASYM_SERVICE_ECDSA_FREE           = 0x0000001F,

    /// <b> Description </b>
    /// \brief ED25519 initialize parameters, supports verification only.
    /// Requirments for ED25519:
    ///     1) Little endian signature, 64 bytes in all.
    ///     2) Little endian commpressed point.
    ///     3) Raw message source data in little endian or pre-hash data.
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(TEE_OperationAlgorithm): Supported algorithm identifier
    /// valid_range:
    ///               TEE_ALG_ED25519
    /// \param[in] params[0].value.b(TEE_EccCurveID): supported Curve ID
    /// valid_range:
    ///               TEE_ECC_CURVE_25519
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported parameter types.
    /// \retval TEE_ERROR_BUSY if EC object already exist.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \retval TEE_ERROR_GENERIC if SE INIT operation failed.
    ///
    CRYPTO_ASYM_SERVICE_ED25519_INIT         = 0x00000020,

    /// <b> Description </b>
    /// \brief ED25519 set_key parameters.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].memref.buffer(uint8_t*): Buffer containing compressed_point
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[0].memref.size(uint32_t): Size of buffer containing compressed_point
    /// valid_range:
    ///               32
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameter types or null buffer,
    ///         or if compressed_point > CCC_ED25519_COMPRESSED_POINT_SIZE
    /// \retval TEE_ERROR_GENERIC if SE SET_KEY operation failed
    ///
    CRYPTO_ASYM_SERVICE_ED25519_SET_KEY      = 0x00000021,

    /// <b> Description </b>
    /// \brief Deliver a block of data and finishes the operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].memref.buffer(uint8_t*): Source buffer containing raw message or digest
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[0].memref.size(uint32_t): Size of buffer containing raw message or digest
    /// valid_range:
    ///               1 to 1792
    /// \param[in] params[1].memref.buffer(uint8_t*): Buffer containing signature
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of signature
    /// valid_range:
    ///               64
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameter types or null buffer,
    ///         or if digest or signature buffer length is more than max supported size
    /// \retval TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory
    /// \retval TEE_ERROR_GENERIC if SE DO_FINAL operation failed
    ///
    CRYPTO_ASYM_SERVICE_ED25519_DO_FINAL     = 0x00000022,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if wrong parameter types
    /// \retval TEE_ERROR_GENERIC if failed to free ED25519 context
    ///
    CRYPTO_ASYM_SERVICE_ED25519_FREE         = 0x00000023,

    /// <b> Description </b>
    /// \brief Set key slot handle for ED25519 or ECDSA operation.
    /// This is exclusive with CRYPTO_ASYM_SERVICE_ED25519_SET_KEY
    /// and CRYPTO_ASYM_SERVICE_ECDSA_SET_KEY.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[0].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameter types or null buffer
    /// \retval TEE_ERROR_GENERIC if SE SET_KEY operation failed
    ///
    CRYPTO_ASYM_SERVICE_ECC_SET_KEYSLOT = 0x00000044,

    /// <b> Description </b>
    /// \brief This command provides functionality to set a key slot with key material.
    /// It is responsible for the following operations:
    ///    1) Input validation of incoming parameters based on the various permutations
    ///       of valid input values
    ///    2) Set requested parameters to keyslot.
    ///
    /// <b> Parameters </b>
    /// \param[in] params[0].value.a(KeySlotType): Type of keyslot to write
    /// valid_range: 
    ///               KEYSLOT_TYPE_PKA1
    /// \param[in] params[1].memref.buffer(KeySlotHandle*): Buffer containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of the keyslot handle (uint32_t)
    /// valid_range:
    ///               16
    /// \param[in] params[3].memref.buffer(pkcsEccPrivKey_t*): Buffer containing ECC private key
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[3].memref.size(uint32_t): Buffer containing ECC private key
    /// valid_range:
    ///               372
    ///
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer,
    ///         or if keyslot owner VMID check failed. Or if failed to get keyslot
    /// \retval TEE_ERROR_OUT_OF_MEMORY if dynamic memory allocation failed
    /// \retval TEE_ERROR_GENERIC if key size is invalid,
    ///         or if failed to write keyslot in case of PKA1 keyslot type
    ///
    /// \note params[2] is not used when ED25519 signature is generated using
    ///       PSC-SE because PSC-SE will decrypt the encrypted key in key
    ///       object using EXPORT_KEY_KEYSLOT.
    ///
    CRYPTO_ASYM_SERVICE_UPDATE_PRIV_SE_KEYSLOT = 0x00000081,

    /// <b> Description </b>
    /// \brief Performs EDDSA or ECDSA private key signing.
    ///
    /// \note Crypto-Asym TA will route the request for signing to
    ///       PSC KeyManager App which perform signing and returns
    ///       back the digital signature.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(TEE_OperationAlgorithm): Supported algorithm identifier
    /// valid_range:
    ///               TEE_ALG_ED25519 for ED25519,
    ///               NV_TE_ALG_ED25519PH for ED25519,
    ///               TEE_ALG_ECDSA_SHA256 for ECDSA
    /// \param[in] params[0].value.b(TEE_EccCurveID): Supported Curve ID
    /// valid_range:
    ///              TEE_ECC_CURVE_25519 for EDDSA,
    ///              TEE_ECC_CURVE_NIST_P256 for ECDSA
    /// \param[in] params[1].memref.buffer(KeySlotHandle*): Pointer to Emulated PKA1 Keyslot
    ///            handle in which private key object has already been installed.
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size Emulated PKA1 Keyslot handle
    /// valid_range:
    ///               16
    /// \param[in] params[2].memref.buffer(uint8_t*): Source buffer containing
    ///            raw message or digest
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[2].memref.size(uint32_t): Size of buffer containing
    ///            raw message or digest
    /// valid_range:
    ///               1 to 1024: Raw message for TEE_ALG_ED25519,
    ///               32 bytes: For TEE_ALG_ECDSA_SHA256 which receives the digested input.
    ///
    /// \param[out] params[3].memref.buffer(uint8_t*): Buffer containing signature
    /// valid_range:
    ///               Valid Pointer
    /// \param[out] params[3].memref.size(uint32_t): Size of signature
    /// valid_range:
    ///               64: For EDDSA signatures TEE_ALG_ED25519 and NV_TE_ALG_ED25519PH,
    ///               72 to 1792: For ECDSA signatures TEE_ALG_ECDSA_SHA256
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported parameter types.
    /// \retval TEE_ERROR_GENERIC if the Signing  operation failed.
    CRYPTO_ASYM_SERVICE_HANDLE_PRIV_KEY_SIGNING = 0x00000088,

#ifdef TOS_DEVTEST_ENABLE_KPI
    /// Calculate init time of Crypto Asym TA
    CRYPTO_ASYM_GET_INIT_TIME = 0x00000089,
#endif

    /// <b> Description </b>
    /// \brief ECC signature verification.
    /// Requirments for ECDSA:
    ///     1) ASN.1 DER format signature in big endian, which is Openssl compatible.
    ///     2) Big endian point coordinates.
    ///     3) Digested source data.
    /// Requirments for ED25519:
    ///     1) Little endian signature, 64 bytes in all.
    ///     2) Little endian commpressed point.
    ///     3) Raw message source data in little endian or pre-hash data.
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(TEE_OperationAlgorithm): Supported algorithm identifier
    /// valid_range:
    ///               TEE_ALG_ECDSA_SHA256
    ///               TEE_ALG_ED25519
    ///               NV_TE_ALG_ED25519PH
    /// \param[in] params[0].value.b(TEE_EccCurveID): supported Curve ID
    /// valid_range:
    ///               TEE_ECC_CURVE_NIST_P256
    ///               TEE_ECC_CURVE_25519
    /// \param[in] params[1].memref.buffer(KeySlotHandle*): containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    /// \param[in] params[2].memref.buffer(uint8_t*): Source buffer containing digest or raw message
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[2].memref.size(uint32_t): Size of buffer containing digest or raw message
    /// valid_range:
    ///               ECDSA: 32
    ///               ED25519: 1 to 1792
    /// \param[in] params[3].memref.buffer(uint8_t*): Buffer containing signature
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[3].memref.size(uint32_t): Size of signature
    /// valid_range:
    ///               ECDSA: 64
    ///               ED25519: 1 to 1792 bytes
    ///
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have wrong parameters.
    /// \retval TEE_ERROR_BUSY if EC object already exist.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \retval TEE_ERROR_GENERIC if ECC operation failed
    CRYPTO_ASYM_SERVICE_ECC_VERIFY_REQUEST = 0x0000008A,

} NV_TE_CryptoAsymServiceOperation;

#endif /* NV_TE_CRYPTOGRAPHIC_ASYM_CONSTANTS_H */
