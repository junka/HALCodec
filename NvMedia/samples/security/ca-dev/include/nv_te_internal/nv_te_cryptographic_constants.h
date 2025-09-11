/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: NV Cryptographic Constants </b>
 *
 * @b Description: Describes NV cryptographic constants.
 */

#ifndef NV_TE_CRYPTOGRAPHIC_CONSTANTS_H
#define NV_TE_CRYPTOGRAPHIC_CONSTANTS_H

/*
 * There is another set of crypto algorithm defined in tee_cryptographic_constants.h
 * that are supported in TZVault. This set of operation constants are used by the
 * openssl library, and will be removed and replaced by those in tee_cryptographic_constants.h
 * after CCC is available
 */
/// \brief This set of operation constants are used by NV-maintained TAs
typedef enum {
    AES_CBC = 1,           ///< AES-128-CBC with PKCS#7 padding
    AES_CBC_NOPAD = 2,     ///< AES-128-CBC no padding
    AES_ECB = 3,           ///< AES-128-ECB with PKCS#7 padding
    AES_ECB_NOPAD = 4,     ///< AES-128-ECB no padding
    AES_CTS = 5,           ///< AES-128-CTS
    AES_CTR_128 = 6,       ///< AES-128-CTR
    AES_CTR_256 = 7,       ///< AES-256-CTR
    AES_CBC_256 = 8,       ///< AES-256-CBC with PKCS#7 padding
    AES_CBC_256_NOPAD = 9, ///< AES-256-CBC no padding
} NV_TE_CryptoAlgorithm;

/// \brief This type is used to contain operation algorithms
typedef enum {
    NV_TE_ALG_AES_ECB           = 0x10000510, ///< AES-ECB  w/ 128 bit key
    NV_TE_ALG_AES_CBC           = 0x10000610, ///< AES-CBC  w/ 128 bit key
    NV_TE_ALG_AES_CBC_256       = 0x10000710, ///< AES-CBC  w/ 256 bit key
    NV_TE_ALG_AES_CBC_256_NOPAD = 0x10000810, ///< AES-CBC  w/ 256 bit key without padding
    NV_TE_ALG_AES_CMAC_128      = 0x20000110, ///< AES-CMAC w/ 128 bit key
    NV_TE_ALG_AES_CMAC_256      = 0x20000130, ///< AES-CMAC w/ 256 bit key
    NV_TE_ALG_AES_GMAC          = 0x20000230, ///< AES-GMAC


    NV_TE_ALG_ED25519PH         = 0x70006050, ///< ED25519 pre-hash
} NV_TE_OperationAlgorithm;

/// @}

/// \defgroup se_diag_function_api Commands
/// @ingroup global_platform_tee_constants
/// @{

/// \brief This type is used to contain SeDiag-Service commands.
///
/// Below is the description of each command's TEE_Param
/// NV_TE_SeDiagServiceOperation
/// Implementation defined as an enum in nv_te_internal/nv_te_cryptographic_constants.h
typedef enum {
    /// <b> Description </b>
    /// \brief To service SE SW diagnostic request.
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if wrong parameter types
    /// \retval TEE_ERROR_BAD_STATE if Config register verify failed
    ///
    SE_DIAG_SERVICE_SE_VERIFY_CONFIG     = 0x00000201,

    /// <b> Description </b>
    /// \brief To service SE Error Injection request.
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    /// \retval TEE_SUCCESS if success
    /// \retval TEE_ERROR_BAD_PARAMETERS if wrong parameter types
    /// \retval TEE_ERROR_BAD_STATE if writting registers failed
    ///
    SE_DIAG_SERVICE_SE_ERROR_INJECT = 0x00000202,

} NV_TE_SeDiagServiceOperation;

/// @}

/// \defgroup okss_eds_consts Oem Keystore Service Constants
/// Describes the Oem Keystore Service Constants used by clients.
/// @ingroup global_platform_tee_constants
/// @{

/// \brief Defines TOS OEM Keystore Service trusted application commandIDs
///        available to clients.
typedef enum {

    /// <b> Description </b>
    /// \brief Command to ping and check response from OEM Keystore Service.
    ///
    /// To check if call is going into OEM Keystore service successfully,
    /// ie, test the reach-ability of OEM Keystore service.
    ///
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \retval TEE_SUCCESS If successful.
    /// \retval TEE_ERROR_BAD_PARAMETERS If invalid parameter is passed.
    /// \retval TEE_ERROR_BAD_STATE If OEM Keystore service is not initialized.
    ///
    KEYSTORE_SERVICE_PING = 0x00000000,

    /// <b> Description </b>
    /// \brief Command to return a key in plaintext format.
    ///
    /// Get key material in plain text format in the caller provided buffer,
    /// corresponding to the requested key entry index and lookup type,
    /// after authenticating the access based on UUID of requesting TA and
    /// guest virtual machine number.
    ///
    /// \param[in] params[0].value.a Index of the requested key entry
    /// \param[in] params[0].value.b Lookup type to use while searching
    ///            for key entry. \n
    ///            KEYSTORE_LOOKUP_TYPE_ABSOLUTE means treat key entry Index
    ///            as absolute index from the start of EKS payload. \n
    ///            KEYSTORE_LOOKUP_TYPE_RELATIVE means treat key entry Index
    ///            as index within the set of key entries having same
    ///            matching caller's TA UUID.
    ///
    /// \param[in] params[1].value.a Guest Virtual Machine ID.
    ///
    /// \param[in] params[2].memref.buffer On success, shall contain
    ///            requested key material.
    /// \param[inout] params[2].memref.size Shall contain size of input
    ///                buffer and on successfull return shall contain
    ///                size of copied key material.
    ///
    /// \retval TEE_SUCCESS Success
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key entry access denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_SHORT_BUFFER Caller supplied keyBufferSize is less.
    ///
    KEYSTORE_SERVICE_GET_KEY = 0x00000001,

    /// <b> Description </b>
    /// \brief Command to return attribute of a key.
    ///
    /// Gets key material attribute corresponding to the key entry index and
    /// lookup type, after authenticating the access based on UUID of
    /// requesting TA and guest virtual machine number.
    ///
    /// \param[in] params[0].value.a Index of the requested key entry.
    /// \param[in] params[0].value.b Lookup type to use while searching
    ///            for key entry. \n
    ///            KEYSTORE_LOOKUP_TYPE_ABSOLUTE means treat key entry Index
    ///            as absolute index from the start of EKS payload. \n
    ///            KEYSTORE_LOOKUP_TYPE_RELATIVE means treat key entry Index
    ///            as index within the set of key entries having same
    ///            matching caller's TA UUID.
    ///
    /// \param[in] params[1].value.a Guest Virtual Machine ID.
    ///
    /// \param[in] params[2].value.a Attribute type requested. \n
    ///            KEYSTORE_KEY_ATTRIBUTE_SIZE means that request is
    ///            for retrieving encrypted key material size.
    ///
    /// \param[out] params[3].value.a On success, shall contain requested
    ///             attribute value.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key entry access denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    ///
    KEYSTORE_SERVICE_GET_KEY_ATTRIBUTE = 0x00000002,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 symmetric object into AES Keyslot.
    ///
    /// Gets PKCS11 Symmetric object corresponding to the key entry index
    /// based on absolute lookup type, after authenticating the access
    /// based on guest virtual machine number. Validates the key
    /// material of the symmetric object and loads into AES Keyslot.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the AES key to be loaded.
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[in] params[2].value.a(SE_USER_TYPE): The SE in which key is to loaded.
    ///                               Valid values are \a PKS_SE_USER_TZ &
    ///                               \a PKS_SE_USER_GP. Default SE is GP-SE.
    ///
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_LOAD_AES_KEY = 0x00000003,

    /// <b> Description </b>
    /// \brief Command to Load Persistent PKCS11 symmetric object
    ///        from the secure storage into AES Keyslot.
    ///
    /// Gets PKCS11 Symmetric object corresponding to the key object
    /// handle and validates the key material of the symmetric object
    /// and loads into AES Keyslot.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the AES key to be loaded.
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[in] params[2].value.a(SE_USER_TYPE): The SE in which key is to loaded.
    ///                               Valid values are \a PKS_SE_USER_TZ &
    ///                               \a PKS_SE_USER_GP. Default SE is GP-SE.
    ///
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size: size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_AES_KEY = 0x00000103,

    /// <b> Description </b>
    /// \brief Command to release AES Keyslot when a Persistent PKCS11
    ///        Symmetric object is loaded into Keyslot using
    ///        KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_AES_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_RELEASE_AES_KEYSLOT = 0x00000104,

    /// <b> Description </b>
    /// \brief Command to release AES Keyslot when a PKCS11 Symmetric
    ///        object is loaded into Keyslot using
    ///        KEYSTORE_SERVICE_PKCS11_LOAD_AES_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_RELEASE_AES_KEYSLOT = 0x00000004,

    /// <b> Description </b>
    /// \brief Get PKCS11 object metadata.
    ///
    /// Gets PKCS11 object metadata. The PKCS11 object type from the metadata
    /// indicates the type of object like a secret key object or a public key
    /// object and it must be used to interpret the returned metadata.
    ///
    /// \param[in] params[0].value.a(KeyHandle): KeyHandle of the key object returned by
    ///            KEYSTORE_SERVICE_PKCS11_* APIs.
    /// \param[out] params[1].memref.buffer(PkcsObject_t): Pointer to the metadata struct.
    /// \param[in] params[1].memref.size(uint32_t): Size of metadata buffer where
    ///                                          the metadata will be copied
    /// \param[out] params[2].value.a(uint32_t): Optional param utilized only if param
    ///             type passed as VALUE_OUTPUT. If utilized, this will return 0 for
    ///             all ephemeral objects.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    ///
    KEYSTORE_SERVICE_PKCS11_GET_KEY_OBJ_METADATA = 0x00000006,

    /// <b> Description </b>
    /// \brief Command to generate new PKCS11 Symmetric Key Objects
    ///
    /// This command helps to generate new PKCS11 Symmetric key objects
    /// in TZRAM. These keys are ephemeral in nature because they reside in
    /// secure memory and not persistent across system boots. This command
    /// generates new key based on PKCS11 Symmetric Key template from non secure
    /// client which is provided as input parameter. The following metadata
    /// fields of template are validated before generating new secret key:
    /// 1. Key Metdata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    ///
    /// \param[in] params[0].memref.buffer(pkcsSymmetricKey_t): Pointer to Symmetric Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of Symmetric Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key generaion is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if TZRAM Symmetric key entries are full
    ///
    KEYSTORE_SERVICE_PKCS11_GENERATE_AES_KEY = 0x00000007,

    /// <b> Description </b>
    /// \brief Command to delete ephemeral PKCS11 key objects created
    /// by key generate commands.
    ///
    /// Performs a look up in available PKSC11 Objects in TZRAM based
    /// on input key handle and deletes the Key Entry.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Valid Key handle to delete. Key Handle
    ///            must be from the prior successfull Key generation command
    ///
    /// \retval TEE_SUCCESS if Key deletion is successfull.
    /// \retval TEE_ERROR_BAD_PARAMETERS if Key handle is invalid.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if object is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_DELETE_KEY = 0x00000008,

    /// <b> Description </b>
    /// \brief Command to derive AES Key based on exisiting deriving AES Key.
    ///
    /// Finds the deriving key provided based on key handle and loads it into
    /// AES Keyslot after verifying if the key can be used for key derivation.
    /// Derives new key material by invoking NIST SP800-108 compliant derivation
    /// on the supplied label and context data.
    /// A new key is then derived using the provided Symmetric Key template as
    /// input along with derived key material.
    /// The following metadata fields of template are validated before
    /// generating new secret key:
    /// 1. Key Metdata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    ///
    /// \param[in]     params[0].value.a(KeyHandle): Handle for the deriving Key
    /// \param[in]     params[0].value.b(PKCS11_CK_MECHANISM_TYPE): PRF mechanism type for KDF
    /// \param[out]    params[0].value.b(KeyHandle): On success, contains derived keyhandle
    /// \param[in]     params[1].memref.buffer(pkcsSymmetricKey_t): Pointer to PKCS symmetric key
    ///                metadata template structure
    /// \param[in]     params[1].memref.size(uint32_t): Size of symmteric obj metadata
    /// \param[in]     params[2].memref.buffer(uint8_t*): Pointer to buffer containing label
    /// \param[in]     params[2].memref.size(uint32_t): Size of the label string
    /// \param[in]     params[3].memref.buffer(uint8_t*): Pointer to buffer containing context
    /// \param[in]     params[3].memref.size(uint32_t): Size of the context string
    /// \param[in]     params[4].value.a(uint32_t): Size of the SP800-108 CTR data in bits
    /// \param[in]     params[4].value.a(uint32_t): Size of the SP800-108 key length data in bits
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_DERIVE_AES_KEY = 0x00000009,

    /// <b> Description </b>
    /// \brief Command to unwrap an AES key using AES-GCM/AES_KWUW from
    ///        exisiting AES key.
    ///
    /// Unwraps an AES key from another AES key using AES-GCM. Input parameters
    /// needed for unwrapping a key are its tag length, nonce, wrapped key
    /// material and AAD(Additional Authentication Data). Below are the details:
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input via 'encKeyData'
    ///             field of unwrapped key template.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding IV, Key and MAC Fields is
    ///          considered as AAD(Additional Authentication Data) and is used for
    ///          authenticating the key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsSymmetricKey_t): Pointer to PKCS symmetric key
    ///             metadata template structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of symmteric obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    /// \retval  unwrapping fails.
    ///
    KEYSTORE_SERVICE_PKCS11_UNWRAP_AES_KEY = 0x0000000C,

    /// <b> Description </b>
    /// \brief Command to make a copy of an existing AES key object.
    ///
    /// Finds a source key based on the provided key handle and creates a copy
    /// of it with the provided unique object ID and label (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    ///
    KEYSTORE_SERVICE_PKCS11_COPY_AES_KEY = 0x00000018,

    /// \brief Set PKCS11 object identifier.
    ///
    /// Sets a new object identifier(ID) for a key object specified by
    /// the key handle.
    ///
    /// \param[in] params[0].value.a(KeyHandle): KeyHandle of the key object returned by
    ///            KEYSTORE_SERVICE_PKCS11_* APIs.
    /// \param[in] params[1].memref.buffer(uint8_t*): Pointer to object Id buffer.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing object id.
    ///
    /// \retval TEE_SUCCESS Success in updating object identifier.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    ///
    KEYSTORE_SERVICE_PKCS11_SET_KEY_OBJ_ID = 0x0000000E,

    /// <b> Description </b>
    /// \brief Set PKCS11 object label.
    ///
    /// Sets a new object label for a key object specified by
    /// the key handle.
    ///
    /// \param[in] params[0].value.a(KeyHandle): KeyHandle of the key object returned by
    ///            KEYSTORE_SERVICE_PKCS11_* APIs.
    /// \param[in] params[1].memref.buffer(uint8_t*): Pointer to object label buffer.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing object label.
    ///
    /// \retval TEE_SUCCESS Success in updating object label.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    ///
    KEYSTORE_SERVICE_PKCS11_SET_KEY_OBJ_LABEL = 0x0000000F,

    /// <b> Description </b>
    /// \brief Get the state of secure storage.
    ///
    /// Get the state of secure storage.If secure storage is present read object
    /// with invalid object Id to confirm if secure storage is functional as well.
    ///
    /// \param[out] params[0].value.a(PKSSecureStorageState):
    ///             PKS_SECURE_STORAGE_FUNCTIONAL secure storage is functional.
    ///             PKS_SECURE_STORAGE_PRESENT secure storage is present.
    ///             PKS_SECURE_STORAGE_NOT_PRESENT secure storage is not.
    ///             present.
    ///
    /// \retval TEE_SUCCESS Success in getting the state of secure storage.
    /// \retval TEE_ERROR_BAD_PARAMETERS bad parameters to the function.
    ///
    KEYSTORE_SERVICE_PKCS11_SECURE_STORAGE_GET_STATE = 0x00000010,

    /// <b> Description </b>
    /// \brief Resets Secure Storage FileSystem.
    ///
    /// Resets the Secure Storage FileSystem. This command is available only on
    /// debug overlay and works on unfused devices. If secure NOR is not present
    /// this command isn't supported.
    ///
    /// \param[in] params[0].value.a(uint32_t): Whether to do an erase of the NOR flash.
    ///                    zero (false) The cache is cleared in filesystem
    ///                    and reloads the objects from flash mimicking
    ///                    power reset.
    ///                    non-zero (true) The flash will reset to a state
    ///                    as if it was never used by erasing all the metadata
    ///                    on the flash.
    ///
    /// \retval TEE_SUCCESS Success in resetting the secure storage filesystem.
    /// \retval TEE_ERROR_BAD_PARAMETERS bad parameters to the function
    /// \retval TEE_ERROR_ACCESS_DENIED access is denied because of
    ///         permission check failure
    /// \retval TEE_ERROR_GENERIC The file system cannot be reset
    ///         because of error returned from flash
    /// \retval TEE_ERROR_BAD_STATE if securestorage session is not
    ///         established
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if secure nor is not available
    /// \retval TEE_ERROR_NOT_SUPPORTED if reset fs stubbed out
    ///
    KEYSTORE_SERVICE_PKCS11_SECURE_STORAGE_RESET_FILESYSTEM = 0x00000011,

    /// \brief Command to unwrap an AES key using AES-GCM from exisiting
    ///        persistent AES key and write to secure storage.
    ///
    /// Unwraps an AES key from another AES key using AES-GCM. Input parameters
    /// needed for unwrapping a key are its tag length, nonce, wrapped key
    /// material and AAD(Additional Authentication Data). Below are the details
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input via 'encKeyData'
    ///             field of unwrapped key template.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding IV, Key and MAC Fields is
    ///          considered as AAD(Additional Authentication Data) and is used for
    ///          authenticating the key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping
    ///                                key(ephemeral/persistent).
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(uint8_t*): Pointer to PKCS symmetric key
    ///             metadata template structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of symmteric obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    /// \retval TEE_ERROR_BAD_FORMAT secure storage session is not established.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check
    /// \retval TEE_ERROR_COMMUNICATION if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in
    ///         secure storage is reached.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_UNWRAP_AES_KEY = 0x00000012,

    /// \brief Command to create a new PKCS11 RSA public Key Object
    ///
    /// This command helps to generate new PKCS11 RSA public key object
    /// in TZRAM. These keys are ephemeral in nature because it resides in
    /// secure memory and is not persistent across system boots. This command
    /// generates new key based on PKCS11 RSA public key template from non secure
    /// client which is provided as input parameter. The following metadata
    /// fields of template are validated before generating new secret key,
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS and KEYSIZE
    ///
    /// \param[in] params[0].memref.buffer(pkcsRsaPubKey_t): Pointer to RSA public Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of RSA public Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key creation is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key handle generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if TZRAM RSA public key entries
    ///         are full in the current session.
    ///
    KEYSTORE_SERVICE_PKCS11_CREATE_RSA_PUB_KEY = 0x00000020,

    /// <b> Description </b>
    /// \brief Command to Look up persistent objects based on object id.
    ///
    /// Performs a look up in available persistent objects based on
    /// input object id and returns key handle and key object type.
    ///
    /// \param[in] params[0].memref.buffer(uint8_t*): Pointer to object Id buffer.
    /// \param[in] params[0].memref.size(uint32_t): Size of buffer containing object id.
    /// \param[in] params[1].value.a(PKSKeyObjType): Object type
    /// \param[out] params[2].value.a(uint32_t): For TSEC safety token, shall return
    ///             the 1-based index of object found in the pkcs-ks ta cache.
    ///             In case of successful lookup with any other token, or if
    ///             the object is not found or any other error, shall return 0.
    /// \param[out] params[3].value.a(KeyHandle): If object is found, shall contain
    ///             requested handle to the object.
    /// \param[out] params[3].value.b(uint32_t): If object is found, shall return 1.
    ///             If the object is not found or in case of any other
    ///             error, shall return 0.
    ///
    /// \retval TEE_SUCCESS if lookup completes without any errors.
    ///         In this case, params[3].value.b will signify whether an
    ///         object was found when lookup completed without any errors.
    ///         If the object is found, params[3].value.b will be 1.
    ///         If the object is not found, params[3].value.b will be 0.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE persistent objects were not loaded/
    ///         authenticated so keystore supports only ephemeral keys.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOOKUP_KEY = 0x000000013,

    /// <b> Description </b>
    /// \brief Get PKCS11 persistent key object metadata.
    ///
    /// Gets PKCS11 object metadata. The PKCS11 object type from the metadata
    /// indicates the type of object like a secret key object or a public key
    /// object and it must be used to interpret the returned metadata.
    /// \param[in] params[0].value.a(KeyHandle): KeyHandle of the persistent key object
    ///            returned by KEYSTORE_SERVICE_PKCS11_PERSISTENT* APIs.
    /// \param[out] params[1].memref.buffer(uint8_t*): Pointer to the metadata struct.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer where the metadata
    ///             would be copied.
    /// \param[out] params[2].value.a(uint32_t): Optional param utilized only if param
    ///             type passed as VALUE_OUTPUT. If utilized, this will return TSEC
    ///             cache index for a persistent object on TSEC safety token and will
    ///             return 0 for other tokens.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if the object associated with
    ///         the handle is not found.
    /// \retval TEE_ERROR_BAD_STATE if keystore is in a bad state and doesn't
    ///         support persistent key objects.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_GET_KEY_OBJ_METADATA = 0x00000014,

    /// <b> Description </b>
    /// \brief Command to Look up PKSC11 key Object based on object id
    ///
    /// Performs a look up in available PKSC11 key Objects based on
    /// input object id and returns handle and objType to object.
    ///
    /// \param[in] params[0].memref.buffer(uint8_t*): Pointer to object Id buffer.
    /// \param[in] params[0].memref.size(uint32_t): Size of buffer containing object id.
    /// \param[in] params[1].value.a(PKSKeyObjType): Object type
    /// \param[out] params[2].value.a(uint32_t): For TSEC safety token, shall return
    ///             the 1-based index of object found in the pkcs-ks ta cache.
    ///             In case of successful lookup with any other token, or if
    ///             the object is not found or any other error, shall return 0.
    /// \param[out] params[3].value.a(uint32_t): If object is found, shall contain
    ///             requested handle to the object.
    /// \param[out] params[3].value.b(uint32_t): If object is found, shall return 1.
    ///             If the object is not found or in case of any other
    ///             error, shall return 0.
    ///
    /// \retval TEE_SUCCESS if lookup completes without any errors.
    ///         In this case, params[3].value.b will signify whether an
    ///         object was found when lookup completed without any errors.
    ///         If the object is found, params[3].value.b will be 1.
    ///         If the object is not found, params[3].value.b will be 0.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    ///
    KEYSTORE_SERVICE_PKCS11_LOOKUP_KEY = 0x00000021,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 RSA public key object into PKA1 Keyslot.
    ///
    /// Gets PKCS11 RSA public key object corresponding to the key object handle,
    /// Validates the key object metadata, and loads into PKA1 Keyslot.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the RSA public key to be loaded.
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_LOAD_RSA_PUB_KEY = 0x00000022,

    /// <b> Description </b>
    /// \brief Command to release RSA Keyslot which was loaded with
    ///        a PKCS11 RSA key object using
    ///        KEYSTORE_SERVICE_PKCS11_LOAD_RSA_PUB_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_RELEASE_RSA_KEYSLOT = 0x00000023,

    /// <b> Description </b>
    /// \brief Command to make a copy of an existing RSA public key object.
    ///
    /// Finds a source key based on the provided key handle and creates a copy
    /// of it with the provided unique object ID and label (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a: On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if CryptoSession is not established.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    ///
    KEYSTORE_SERVICE_PKCS11_COPY_RSA_PUB_KEY = 0x000000024,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 Persistent RSA public key object
    ///        into PKA1 Keyslot.
    ///
    /// Gets the persistent RSA public key object corresponding to the
    /// key object handle, validates the key object metadata, and loads
    /// into PKA1 Keyslot.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the Persistent RSA public key
    ///                               to be loaded.
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_RSA_PUB_KEY = 0x00000122,

    /// <b> Description </b>
    /// \brief Command to release a RSA Keyslot which was loaded with
    ///        a PKCS11 Persistent RSA key object using
    ///        KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_RSA_PUB_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_RELEASE_RSA_KEYSLOT = 0x00000123,

    /// \brief Command to make a copy of an existing Persistent RSA public key
    /// object.
    ///
    /// Finds a source key based on the provided key handle and creates a
    /// persistent copy of it with the provided unique object ID and label
    /// (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_COPY_RSA_PUB_KEY = 0x000000124,

    /// \brief Command to create a new Persistent PKCS11 RSA public Key Object
    ///
    /// This command helps to generate new PKCS11 RSA public key object
    /// in Secure Storage. These keys are persistent in nature because it resides in
    /// persistent secure storage. This command generates new key based on
    /// PKCS11 RSA public key template from non secure
    /// client which is provided as input parameter. The following metadata
    /// fields of template are validated before generating new secret key,
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS and KEYSIZE
    ///
    /// \param[in] params[0].memref.buffer(pkcsRsaPubKey_t): Pointer to RSA public Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of RSA public Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key creation is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key handle generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if TZRAM RSA public key entries
    ///         are full in the current session.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_CREATE_RSA_PUB_KEY = 0x00000120,

    /// \brief Command to create a new PKCS11 ECC public Key Object
    ///
    /// This command helps to generate new PKCS11 ECC public key object
    /// in TZRAM. These keys are ephemeral in nature because it resides in
    /// secure memory and is not persistent across system boots. This command
    /// generates new key based on PKCS11 ECC public key template from non secure
    /// client which is provided as input parameter. The following metadata
    /// fields of template are validated before generating new secret key,
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS, GENMECHANISM and KEYSIZE
    ///
    /// \param[in] params[0].memref.buffer(pkcsEccPubKey_t): Pointer to ECC public Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of ECC public Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key creation is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key handle generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if TZRAM ECC public key entries
    ///         are full in the current session.
    ///
    KEYSTORE_SERVICE_PKCS11_CREATE_ECC_PUB_KEY = 0x00000030,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 ECC public key object into PKA1 Keyslot.
    ///
    /// Gets PKCS11 ECC public key object corresponding to the key handle.
    /// Validates the key object metadata, and loads into PKA1 Keyslot.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the ECC public key to be loaded.
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_LOAD_ECC_PUB_KEY = 0x00000031,

    /// <b> Description </b>
    /// \brief Command to release ECC Keyslot which was loaded with
    ///        a PKCS11 ECC key object using
    ///        KEYSTORE_SERVICE_PKCS11_LOAD_ECC_PUB_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_RELEASE_ECC_KEYSLOT = 0x00000032,

    /// \brief Command to make a copy of an existing ECC Public key object.
    ///
    /// Finds a source key based on the provided key handle and creates a copy
    /// of it with the provided unique object ID and label (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if CryptoSession is not established.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    ///
    KEYSTORE_SERVICE_PKCS11_COPY_ECC_PUB_KEY = 0x000000033,

    /// \brief Command to create a new Persistent PKCS11 ECC public Key Object
    ///
    /// This command helps to generate new PKCS11 ECC public key object
    /// in secure storage. These keys are persistent in nature because it resides in
    /// persistent secure storage. This command generates new key based on
    /// PKCS11 ECC public key template from non secure
    /// client which is provided as input parameter. The following metadata
    /// fields of template are validated before generating new secret key,
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS, GENMECHANISM and KEYSIZE
    ///
    /// \param[in] params[0].memref.buffer(pkcsEccPubKey_t): Pointer to ECC public Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of ECC public Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key creation is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key handle generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if max key count has reached.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_CREATE_ECC_PUB_KEY = 0x00000130,

    /// \brief Command to make a copy of an existing Persistent ECC Public key
    /// object.
    ///
    /// Finds a source key based on the provided key handle and creates a
    /// persistent copy of it with the provided unique object ID and label
    /// (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_COPY_ECC_PUB_KEY = 0x000000133,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 Persistent ECC public key object
    ///        into PKA1 Keyslot.
    ///
    /// Gets the persistent ECC public key object corresponding to the
    /// key object handle, validates the key object metadata, and loads
    /// into PKA1 Keyslot.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the Persistent ECC public key
    ///                               to be loaded.
    ///
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_ECC_PUB_KEY = 0x00000131,

    /// <b> Description </b>
    /// \brief Command to release a ECC Keyslot which was loaded with
    ///        a PKCS11 Persistent ECC key object using
    ///        KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_ECC_PUB_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_RELEASE_ECC_KEYSLOT = 0x00000132,

    /// <b> Description </b>
    /// \brief Command to generate new PKCS11 Symmetric Key Objects and
    ///        store them in secure storage.
    ///
    /// This command helps to generate new PKCS11 Symmetric key objects
    /// in the secure storage. These keys are persistent across system reboots.
    /// This command generates a new key based on PKCS11 Symmetric Key metadata
    /// provided by a non secure client as the input to the command. The
    /// following fields of metadata are validated before
    /// generating a new symmetric key,
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique in the current session of ephemeral
    ///    and persistent key objects.
    /// Upon successfull generation of symmetric key, the key object is stored
    /// in secure storage.
    ///
    /// \param[in] params[0].memref.buffer(pkcsSymmetricKey_t): Pointer to Symmetric Key metadata
    ///                                     buffer.
    /// \param[in] params[0].memref.size(uint32_t): Size of Symmetric Key metadata.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain handle
    ///                                to new key object.
    ///
    /// \retval TEE_SUCCESS if Key generaion is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                  or if key object metadata validation
    ///                                  fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state is in a bad state so
    ///                             only persistent commands are not supported.
    /// \retval TEE_ERROR_GENERIC if crypto operation fails.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if max key count has reached.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_GENERATE_AES_KEY = 0x000000015,

    /// \brief Set PKCS11 persistent object identifier.
    ///
    /// Sets a new object identifier(ID) for a persistent key object specified
    /// by the key handle.
    ///
    /// \param[in] params[0].value.a(KeyHandle): KeyHandle of the key object returned by
    ///            KEYSTORE_SERVICE_PKCS11_PERSISTENT* APIs.
    /// \param[in] params[1].memref.buffer(uint8_t*): Pointer to object Id buffer.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing object id.
    ///
    /// \retval TEE_SUCCESS Success in updating object identifier.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if key based on key handle isn't found.
    /// \retval TEE_ERROR_GENERIC if MAC computation failed.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check
    /// \retval TEE_ERROR_COMMUNICATION if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_NO_DATA if there is no secure storage object id
    ///         associated with key handle.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_SET_KEY_OBJ_ID = 0x00000016,

    /// \brief Set PKCS11 persistent object label.
    ///
    /// Sets a new object label for a persistent key object specified
    /// by the key handle.
    ///
    /// \param[in] params[0].value.a(KeyHandle): KeyHandle of the key object returned by
    ///            KEYSTORE_SERVICE_PKCS11_PERSISTENT* APIs.
    /// \param[in] params[1].memref.buffer(uint8_t*): Pointer to object label buffer.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing
    ///                                   object label.
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \retval TEE_SUCCESS Success in updating object label.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if key based on key handle isn't found.
    /// \retval TEE_ERROR_GENERIC if MAC computation failed.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check
    /// \retval TEE_ERROR_COMMUNICATION if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_NO_DATA if there is no secure storage object id
    ///         associated with key handle.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_SET_KEY_OBJ_LABEL = 0x00000017,

    /// \brief Command to initialize the keystore. This command must be invoked
    ///        before invoking any other PKCS11 command.
    ///
    /// This command triggers authentication of all the persistent key objects
    /// and reports a success or failure code based on the result.
    ///
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \retval TEE_SUCCESS Indicates that the keystore is authenticated and
    ///         ready to be used.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function. The
    ///         command must be invoked again with correct parameters.
    /// \retval TEE_ERROR_BAD_STATE Indicates that there is a problem with the
    ///         keystore and none of the persistent key object functionality
    ///         will be available to the application. Epheremal objects shall
    ///         continue to work as expected.
    /// \retval TEE_ERROR_NOT_IMPLEMENETED Indicates the secure storage isn't
    ///                                    functional on the platform.
    ///
    KEYSTORE_SERVICE_PKCS11_INIT_KEYSTORE = 0x00000034,

    /// <b> Description </b>
    /// \brief Command to make a copy of an existing persistent AES key object.
    ///
    /// Finds a source key based on the provided key handle and creates a copy
    /// of it with the provided unique object ID and label (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function,
    ///         source key not copyable/modifiable, object Id is not unique.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if crypto operation failed.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_COPY_AES_KEY = 0x00000019,

    /// <b> Description </b>
    /// \brief Command to delete persistent PKCS11 key objects created
    /// by key generate/unwrap commands.
    ///
    /// Performs a look up in the available persistent PKSC11 Objects based
    /// on the input key handle and deletes the key object from the PKCS11
    /// keystore and the the secure store.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Valid Key handle to delete. Key Handle
    ///            must be from the prior successful Key generation/unwrap
    ///            command
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    /// \retval TEE_ERROR_BAD_FORMAT secure storage session is not established.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check
    /// \retval TEE_ERROR_COMMUNICATION if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in
    ///         secure storage is reached.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_DELETE_KEY = 0x0000001A,

    /// <b> Description </b>
    /// \brief Command to request persistent key permission
    ///        PersistentKeyPerm::PKCS_PST_KEY_PERM_READ_WRITE
    ///
    /// \param: None
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ACCESS_DENIED if requesting is not from the
    ///         privileged VM or other session requests already any
    ///         persistent key permission.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_REQUEST_RW_ACCESS = 0x0000001B,

    /// <b> Description </b>
    /// \brief Command to relinquish persistent key permission
    ///        PersistentKeyPerm::PKCS_PST_KEY_PERM_READ_WRITE
    ///
    /// \param: None
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_RELINQUISH_RW_ACCESS = 0x0000001C,

    /// <b> Description </b>
    /// \brief Command to request persistent key permission
    ///        PersistentKeyPerm::PKCS_PST_KEY_PERM_READ_ONLY
    ///
    /// \param: None
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ACCESS_DENIED if a session requests already
    ///         permission PersistentKeyPerm::PKCS_PST_KEY_PERM_READ_WRITE.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_REQUEST_RO_ACCESS = 0x0000001D,

    /// <b> Description </b>
    /// \brief Command to relinquish persistent key permission
    ///        PersistentKeyPerm::PKCS_PST_KEY_PERM_READ_ONLY
    ///
    /// \param: None
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_RELINQUISH_RO_ACCESS = 0x0000001E,

    ///
    /// \brief Command to unwrap an persistent RSA key and
    ///        write to secure storage.
    ///
    /// Unwraps and verifies RSA key object using AES-GCM. Input parameters
    /// needed are tag length, wrapped RSA key object metadata and keytag
    /// containing key information such as keysize, exponent, modulus and MAC
    /// Below are the details,
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding keysize, exponent,
    ///          modulus and MAC fields is considered as AAD (Additional
    ///          Authentication Data) and is used for authenticating the
    ///          key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsRsaPubKey_t): Pointer to PKCS RSA key
    ///             metadata structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of RSA key obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    /// \retval TEE_ERROR_BAD_FORMAT secure storage session is not established.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check
    /// \retval TEE_ERROR_COMMUNICATION if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in
    ///         secure storage is reached.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_UNWRAP_RSA_PUB_KEY = 0x00000127,

    /// \brief Command to unwrap an ephemeral RSA key and write to TZDRAM.
    ///
    /// Unwraps and verifies RSA key object using AES-GCM. Input parameters
    /// needed are tag length, wrapped RSA key object metadata and keytag
    /// containing key information such as keysize, exponent, modulus and MAC
    /// Below are the details,
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding keysize, exponent,
    ///          modulus and MAC fields is considered as AAD (Additional
    ///          Authentication Data) and is used for authenticating the
    ///          key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsRsaPubKey_t): Pointer to PKCS RSA key
    ///             metadata structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of RSA key obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    ///
    KEYSTORE_SERVICE_PKCS11_UNWRAP_RSA_PUB_KEY = 0x00000027,

    /// \brief Command to unwrap a persistent ECC key
    ///        and write to secure storage.
    ///
    /// Unwraps and verifies ECC key using AES-GCM. Input parameters
    /// needed are tag length, wrapped ECC key object metadata and keytag
    /// containing key information such as x-coordinate, y-coordinate and MAC
    /// Below are the details,
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding x-coordinate,
    ///          y-coordinate and MAC fields is considered as AAD (Additional
    ///          Authentication Data) and is used for authenticating the
    ///          key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsEccPubKey_t): Pointer to PKCS ECC Pub key
    ///             metadata structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of ECC Pub key obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in
    ///         secure storage is reached.
    /// \retval TEE_ERROR_NO_DATA Write to secure storage failed, the buffer
    ///         comparison at force-read failed after a write.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_UNWRAP_ECC_PUB_KEY = 0x00000128,

    /// \brief Command to unwrap an ephemeral ECC key and write to TZDRAM.
    ///
    /// Unwrap and verify ECC key using AES-GCM. Input parameters
    /// needed are tag length, wrapped RSA key object metadata and tag
    /// containing key information such as x-coordinate, y-coordinate and MAC
    /// Below are the details,
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding x-coordinate,
    ///          y-coordinate and MAC fields is considered as AAD (Additional
    ///          Authentication Data) and is used for authenticating the
    ///          key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsEccPubKey_t): Pointer to PKCS ECC key
    ///             metadata template structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of ECC Pub key obj metadata structure
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    ///
    KEYSTORE_SERVICE_PKCS11_UNWRAP_ECC_PUB_KEY = 0x00000028,


    /// \brief Command to unwrap a persistent ECC private key,
    ///        rewrap and write it to SECURE STORAGE.
    ///
    /// Unwrap ECC key using AES-GCM. Input parameters
    /// needed are tag length, wrapped ECC key object metadata and tag
    /// containing key information such as x-coordinate, y-coordinate and MAC.
    /// Below are the details,
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding NONCE (IV),
    ///          WRAPPED KEY & TAG is considered as AAD (Additional
    ///          Authentication Data) and is used for authenticating the
    ///          key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsEccPrivKey_t): Pointer to PKCS ECC Private key
    ///             metadata template structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of ECC key obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in
    ///         secure storage is reached.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_UNWRAP_ECC_PRIV_KEY = 0x00000160,

    /// \brief Command to unwrap an ephemeral ECC private key,
    ///        Rewrap and write it to TZRAM.
    ///
    /// Unwrap ECC key using AES-GCM. Input parameters
    /// needed are tag length, wrapped ECC key object metadata and tag
    /// containing key information such as x-coordinate, y-coordinate and MAC
    /// Below are the details:
    ///  1. TAG Length: This is fixed as 16 and is not provided as input.
    ///  2. NONCE: Nonce and nonce size are provided as one of the input params.
    ///            Size of nonce is fixed as 12 bytes.
    ///  3. WRAPPED KEY: Wrapped data is provided as input.
    ///  4. TAG: This is also know as MAC and is length 16 bytes.
    ///          Passed via 'macData' field of unwrapped key template.
    ///  5. AAD: The entire metadata template excluding NONCE (IV),
    ///          WRAPPED KEY & TAG is considered as AAD (Additional
    ///          Authentication Data) and is used for authenticating the
    ///          key during unwrapping by the Keystore service.
    ///
    /// Following metadata fields of unwrapped key are validated before
    /// key unwrapping:
    /// 1. Key Metadata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    /// NOTE: Fields of Unwrapped key Template should contain Wrapped key metadata,
    ///       wrapped key material and TAG.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the unwrapping key.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to nonce buffer.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the nonce buffer.
    /// \param[in]  params[2].memref.buffer(pkcsEccPrivKey_t): Pointer to PKCS ECC Private key
    ///             metadata template structure
    /// \param[in]  params[2].memref.size(uint32_t): Size of ECC Private key obj metadata
    /// \param[out] params[3].value.a(KeyHandle): On success, contains unwrapped keyhandle
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if unwrapping key is not found
    /// \retval TEE_ERROR_GENERIC if any AAD authentication fails or
    ///         unwrapping fails
    ///
    KEYSTORE_SERVICE_PKCS11_UNWRAP_ECC_PRIV_KEY = 0x00000060,

    /// <b> Description </b>
    /// \brief Command to generate new PKCS11 EC Keypair
    ///
    /// This command helps to generate new PKCS11 ECC Private key and Public key
    /// objects in TZRAM. These keys are ephemeral in nature because they reside in
    /// secure memory and not persistent across system boots. This command
    /// generates new key based on PKCS11 ECC Private-key template from non secure
    /// client which is provided as input parameter.
    /// A PKA keyslot is requested in behave of client, make sure at least one PKA slot
    /// configured for the client VM.
    /// Following metadata fields of template are validated:
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS, GENMECHANISM and KEYSIZE
    /// TODO: Above validation list may not be excludive, need to modify it based on
    ///       latest ec private-key obj metadata
    ///
    /// \param[in] params[0].memref.buffer(pkcsEccPrivKey_t): Pointer to ECC Private Key template.
    ///                                i.e. Private-key object metadata
    ///                                as per pkcs11_object_metadata_60.pdf.
    /// \param[in] params[0].memref.size(uint32_t): Size of ECC Private Key template.
    /// \param[in] params[1].memref.buffer(pkcsEccPubKey_t): Pointer to ECC Public Key template.
    ///                                i.e. Public-key object metadata
    ///                                as per pkcs11_object_metadata_60.pdf.
    /// \param[in] params[1].memref.size(uint32_t): Size of ECC Public Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new Private-key object.
    /// \param[out] params[3].value.b(KeyHandle): On success, shall contain Key handle
    ///                                to new Public-key object.
    ///
    /// \retval TEE_SUCCESS if Key generaion is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if TZRAM Symmetric key entries are full
    ///
    KEYSTORE_SERVICE_PKCS11_GENERATE_EC_KEYPAIR = 0x00000061,

    /// <b> Description </b>
    /// \brief Command to generate new PKCS11 EC Keypair and
    ///        store ECC Private-key and Public-key  objects in secure storage.
    ///
    /// This command helps to generate new PKCS11 ECC Private key and Public key
    /// objects in the secure storage. These keys are persistent across system reboots.
    /// This command generates a new key based on PKCS11 ECC Private Key metadata
    /// provided by a non secure client as the input to the command.
    /// A PKA keyslot is requested in behave of client, make sure at least one PKA slot
    /// configured for the client VM.
    /// Following fields of metadata are validated:
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS, GENMECHANISM and KEYSIZE
    /// TODO: Above validation list may not be excludive, need to modify it based on
    ///       latest ec private-key obj metadata
    /// Upon successfull generation of key-pair, the private-key object is stored
    /// in secure storage.
    ///
    /// \param[in] params[0].memref.buffer(pkcsEccPrivKey_t): Pointer to ECC Private Key template.
    ///                                i.e. Private-key object metadata
    ///                                as per pkcs11_object_metadata_60.pdf.
    /// \param[in] params[0].memref.size(uint32_t): Size of ECC Private Key template.
    /// \param[in] params[1].memref.buffer(pkcsEccPubKey_t): Pointer to ECC Public Key template.
    ///                                i.e. Public-key object metadata
    ///                                as per pkcs11_object_metadata_60.pdf.
    /// \param[in] params[1].memref.size(uint32_t): Size of ECC Public Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain handle
    ///                                to new Private-key object.
    /// \param[out] params[3].value.b(KeyHandle): On success, shall contain handle
    ///                                to new Pubic-key object.
    ///
    /// \retval TEE_SUCCESS if Key generaion is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                  or if key object metadata validation
    ///                                  fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state is in a bad state so
    ///                             only persistent commands are not supported.
    /// \retval TEE_ERROR_GENERIC if crypto operation fails.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if max key count has reached.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_GENERATE_EC_KEYPAIR = 0x000000161,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 ECC Private-Key object
    /// into PKA1 Keyslot.
    ///
    /// Gets PKCS11 ECC Private-Key object corresponding to the key entry index
    /// based on absolute lookup type, after authenticating the access
    /// based on guest virtual machine number. Validates the key
    /// material of the private-key object and loads into PKA1 Keyslot.
    ///
    /// \param[in] params[0].value.a(pkcsEccPrivKey_t): Handle of the ECC Private key to be loaded.
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    ///
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_LOAD_ECC_PRIV_KEY = 0x000000062,

    /// <b> Description </b>
    /// \brief Command to Load PKCS11 Persistent ECC private key
    ///        into PKA1 Keyslot.
    ///
    /// Gets the persistent ECC private key object corresponding to the
    /// key object handle, validates the key object metadata, and loads
    /// into PKA1 Keyslot.
    ///
    /// \param[in] params[0].value.a(pkcsEccPrivKey_t): Handle of the Persistent ECC private key
    ///                               to be loaded.
    ///
    /// \param[in] params[1].value.a(PKCS11_CK_MECHANISM_TYPE): Mechanism to be used by this keyslot.
    /// \param[in] params[1].value.b(KeyPurpose): Purpose for loading the key into keyslot.
    /// \param[out] params[3].memref.buffer(KeySlotHandle*): A KeySlotHandle pointer.
    ///      On success contains a KeySlotHandle for the requested key.
    /// \param[out] params[3].memref.size(uint32_t): size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_NOT_SUPPORTED if provided mechanism or purpose
    ///         is not supported by the key
    /// \retval TEE_ERROR_OVERFLOW if no keyslots allocated to guest are free.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_ECC_PRIV_KEY = 0x0000162,

    /// <b> Description </b>
    /// \brief Command to release ECC Keyslot which was loaded with
    ///        a PKCS11 ECC private key object using
    ///        KEYSTORE_SERVICE_PKCS11_LOAD_ECC_PRIV_KEY command.
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_RELEASE_ECC_PRIV_KEYSLOT = 0x00000063,

    /// <b> Description </b>
    /// \brief Command to release a ECC Keyslot which was loaded with
    ///        a PKCS11 Persistent ECC private key object using
    ///        KEYSTORE_SERVICE_PKCS11_PERSISTENT_LOAD_ECC_PRIV_KEY command
    ///
    /// Performs a check if the input Keyslot handle is valid and already
    /// loaded. Releases the keyslot, if true and return error if false.
    ///
    /// \param[in] params[0].memref.buffer(KeySlotHandle*): Pointer for a KeySlotHandle to be released.
    /// \param[in] params[0].memref.size(uint32_t): Size of KeySlotHandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_STATE if the PKCS11KS state cannot support the command.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if Keyslot handle is not found.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_RELEASE_ECC_PRIV_KEYSLOT = 0x0000163,

    /// \brief Command to make a copy of an existing ECC Private key object.
    ///
    /// Finds a source key based on the provided key handle and creates a copy
    /// of it with the provided unique object ID and label (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if CryptoSession is not established.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    ///
    KEYSTORE_SERVICE_PKCS11_COPY_ECC_PRIV_KEY = 0x000000064,

    /// <b> Description </b>
    /// \brief Command to make a copy of an existing persistent ECC
    ///        Private key object.
    ///
    /// Finds a source key based on the provided key handle and creates a copy
    /// of it with the provided unique object ID and label (optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source key to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique object ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having object ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[out] params[3].value.a(KeyHandle): On success, contains copied keyhandle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function,
    ///         source key not copyable/modifiable, object Id is not unique.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source key is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source key is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if crypto operation failed.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_COPY_ECC_PRIV_KEY = 0x00000164,

    /// \brief Command to create a new AES Key Object
    ///
    /// This command helps to generate new PKCS11 AES symmetric key object
    /// in TZRAM. These keys are ephemeral in nature because it resides in
    /// secure memory and is not persistent across system boots. This command
    /// copies plain text PKCS11 AES symmetric key from the template provided
    /// by non secure client as input parameter. The following metadata fields
    /// of template are validated before creating new AES Key Object:
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS and KEYSIZE
    ///
    /// \param[in] params[0].memref.buffer(pkcsSymmetricKey_t): Pointer to Symmetric Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of Symmetric Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key creation is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key handle generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if TZRAM AES symmetric key entries
    ///         are full in the current session.
    ///
    KEYSTORE_SERVICE_PKCS11_CREATE_AES_KEY = 0x00002000,

    /// \brief Command to create a new Persistent AES Key Object
    ///
    /// This command helps to generate new PKCS11 AES symmetric key object
    /// in secure storage. These keys are persistent in nature across system
    /// boots because it resides in secure storage. This command copies plain
    /// text PKCS11 AES symmetric key from the template provided by non secure
    /// client as input parameter. The following metadata fields
    /// of template are validated before creating new AES Key Object:
    /// PURPOSE, SEN, EXT, TST, LCL, KCV, DST, VERSION, OBJTYPE, NMECH, ID,
    /// MECHANISMS and KEYSIZE
    ///
    /// \param[in] params[0].memref.buffer(pkcsSymmetricKey_t): Pointer to Symmetric Key template.
    /// \param[in] params[0].memref.size(uint32_t): Size of Symmetric Key template.
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_SUCCESS if Key creation is Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function
    ///                                 or if template validation fails.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_GENERIC if Key handle generation fails
    /// \retval TEE_ERROR_OUT_OF_MEMORY if max key count has reached.
    /// \retval TEE_ERROR_EXCESS_DATA if max number of objects in secure
    ///                               storage is reached.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE all metadata slot or unallocated
    ///         object slot has reached MAX_ERASE_COUNT. In former situation,
    ///         the secure NOR becomes only readable but not writable.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check, if bad block is found during write
    ///         to flash.
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_CREATE_AES_KEY = 0x00002001,

    /// <b> Description </b>
    /// \brief Command to derive Persistent AES Key based on exisiting
    ///        deriving Persistent AES Key.
    ///
    /// Finds the deriving key from the Secure Storage provided based on
    /// key handle and verifies if the key can be used for key derivation.
    /// Derives new key material by invoking NIST SP800-108 compliant derivation
    /// on the supplied label and context data.
    /// A new key is then derived using the provided Symmetric Key template as
    /// input along with derived key material. The new key is then stored in the
    /// Secure Storage.
    /// The following metadata fields of template are validated before
    /// generating new secret key:
    /// 1. Key Metdata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    ///
    /// \param[in]      params[0].value.a(KeyHandle): Handle for the deriving Key
    /// \param[in]  params[0].value.b(PKCS11_CK_MECHANISM_TYPE): PRF mechanism type for KDF
    /// \param[out] params[0].value.b(KeyHandle): On success, contains derived keyhandle
    /// \param[in]      params[1].memref.buffer(pkcsSymmetricKey_t): Pointer to PKCS symmetric key
    ///                 metadata template structure
    /// \param[in]      params[1].memref.size(uint32_t): Size of symmteric obj metadata
    /// \param[in]      params[2].memref.buffer(uint8_t*): Pointer to buffer containing label
    /// \param[in]      params[2].memref.size(uint8_t*): Size of the label string
    /// \param[in]      params[3].memref.buffer(uint8_t*): Pointer to buffer containing context
    /// \param[in]      params[3].memref.size(uint32_t): Size of the context string
    /// \param[in]      params[4].value.a(uint32_t): Size of the SP800-108 CTR data in bits
    /// \param[in]      params[4].value.a(uint32_t): Size of the SP800-108 key length data in bits
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_DERIVE_AES_KEY = 0x00002002,

    /// \brief Command for ECDH Key derivation based on exisiting deriving ECC Private Key.
    ///
    /// Finds the deriving key provided based on key handle and loads it into
    /// PKA Keyslot after verifying if the key can be used for key derivation.
    /// Derives new key material by invoking ECDH1 derivation on the supplied
    /// ECC private key and x-coordinate and y-coordinate of ECC public key and
    /// the curve specified in the ECC private key.
    /// A new key is then derived using the provided Symmetric Key template as
    /// input along with derived key material.
    /// The following metadata fields of template are validated before
    /// generating new secret key:
    /// 1. Key Metdata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the deriving Key
    /// \param[out] params[0].value.b(KeyHandle): On success, contains derived keyhandle
    /// \param[in]  params[1].memref.buffer(pkcsSymmetricKey_t): Pointer to PKCS symmetric key
    ///             metadata template structure
    /// \param[in]  params[1].memref.size(uint32_t): Size of symmteric obj metadata
    /// \param[in]  params[2].memref.buffer(uint8_t*): Pointer to buffer containing
    ///             x-coordinate of ECC public key
    /// \param[in]  params[2].memref.size(uint32_t): Size of the x-coordinate
    /// \param[in]  params[3].memref.buffer(uint8_t*): Pointer to buffer containing
    ///             y-coordinate of ECC public key
    /// \param[in]  params[3].memref.size(uint32_t): Size of the y-coordinate
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_ECDH_DERIVE_KEY = 0x00004000,

    /// <b> Description </b>
    /// \brief Command to derive AES Key based on existing deriving AES Key
    ///        with NIST SP800-56C compliant key derivation.
    ///
    /// Finds the deriving key provided based on key handle and loads it into
    /// AES Keyslot after verifying if the key can be used for key derivation.
    /// Derives new key material by invoking NIST SP800-56C compliant derivation
    /// on the supplied salt data, fixedInfo data and counter value.
    /// A new key is then derived using the provided Symmetric Key template as
    /// input along with derived key material.
    /// The following metadata fields of template are validated before
    /// generating new secret key:
    /// 1. Key Metdata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    ///
    /// \param[in]     params[0].value.a(KeyHandle): Handle for the deriving Key
    /// \param[in]     params[0].value.b(PKCS11_CK_MECHANISM_TYPE): [in] KDF PRF type
    /// \param[out]    params[0].value.b(KeyHandle): On success, contains derived keyhandle
    /// \param[in]     params[1].memref.buffer(pkcsSymmetricKey_t): Pointer to PKCS symmetric key
    ///                metadata template structure
    /// \param[in]     params[1].memref.size(uint32_t): Size of symmteric obj metadata
    /// \param[in]     params[2].memref.buffer(uint8_t*): Pointer to buffer containing salt
    /// \param[in]     params[2].memref.size(uint32_t): Size of the salt string
    /// \param[in]     params[3].memref.buffer(uint8_t*): Pointer to buffer containing fixedInfo
    /// \param[in]     params[3].memref.size(uint32_t): Size of the fixedInfo string
    /// \param[in]     params[4].value.a(uint32_t): Counter value used in Key expansion step
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_DERIVE_AES_KEY_SP800_56C = 0x00004001,

#ifdef TOS_DEVTEST_ENABLE_KPI
    /// Calculate init time of pkcs11-Keystore TA
    KEYSTORE_SERVICE_PKCS11_CALCULATE_INIT_TIME = 0x00004002,
#endif

    /// <b> Description </b>
    /// \brief Command to generate wrapped object from given key object
    ///        with given KEK object with AES-KW algorithm.
    ///
    /// Finds the wrapping key provided based on key handle and loads into
    /// AES Keyslot after verifying if the key can be used for key wrapping.
    /// Finds the target key provided based on key handle and loads into
    /// AES Keyslot after verifying if the key can be handled by SE.
    /// Wraps the target key with the wrapping key
    /// Wraps one secret key object or two secret key objects having custom
    /// data between them with the wrapping key using AES-CBC algorithm.
    /// The following metadata fields of template are validated before
    /// generating wrapped objects:
    /// 1. Key Metdata which includes key type, key purpose and mechanisms, key
    ///    sensitivity, key extractability.
    /// 2. Object Type, Structure Version.
    /// 3. Checks if ObjectId is unique.
    ///
    /// Params for One Key Wrap:
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the wrapping Key
    /// \param[in]  params[0].value.b(KeyHandle): Handle for the leading key
    /// \param[out] params[1].memref.buffer(uint8_t*): Pointer to buffer containing IV
    /// \param[in]  params[1].memref.size(uint32_t): Size of the IV string
    /// \param[out] params[2].memref.buffer(uint8_t*): Pointer to buffer for wrapped blob
    /// \param[in]  params[2].memref.size(uint32_t): Size of the wrapped blob
    ///
    /// Additional params for Two Keys with Custom Data Wrap:
    /// \param[in]  params[3].value.a(KeyHandle): Handle for the trailing key
    /// \param[in]  params[4].memref.buffer(uint8_t*): Pointer to buffer containing custom data
    /// \param[in]  params[4].memref.size(uint32_t): Size of the custom data string
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_AES_CBC_KEY_DATA_WRAP = 0x00004003,

    /// <b> Description </b>
    /// \brief Command to generate wrapped objects from given key handle
    ///        with given wrapping key handle using RFC-3394 AES KW primitive.
    ///
    /// Finds the wrapping key provided based on key handle and loads it into
    /// AES Keyslot after verifying if the key can be used for key wrapping.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the wrapping key
    /// \param[in]  params[0].value.b(KeyHandle): Handle for the key to be wrapped
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to buffer containing IV
    ///             If IV is not provided, default IV (A6A6A6A6A6A6A6A6,
    ///             as described in PKCS11 spec and NIST 800-38F) will be used.
    /// \param[in]  params[1].memref.size(uint32_t): Size of the IV string
    /// \param[out] params[2].memref.buffer(uint8_t*): Pointer to buffer for wrapped key blob
    /// \param[in]  params[2].memref.size(uint32_t): Size of the wrapped key blob
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_AES_KEY_WRAP = 0x00004004,

    /// <b> Description </b>
    /// \brief Command to unwrap SAK with given KEK handle using RFC-3394 AES KW
    ///        primitive then invoke MACsec TA with plain test SAK key and
    ///        hash of SAK to install the key.
    ///
    /// Finds the unwrapping key provided based on key handle and loads it into
    /// AES Keyslot after verifying if the key can be used for key wrapping.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle for the wrapping key
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to buffer containing SAK
    /// \param[in]  params[1].memref.size(uint32_t): Size of wrapped SAK
    /// \param[out] params[2].memref.buffer(uint8_t*): Pointer to buffer containing
    ///                                      MACsec metadata
    /// \param[in]  params[2].memref.size(uint32_t): Size of the MACsec metadata
    /// \param[in]  params[3].memref.buffer(uint8_t*): Pointer to buffer containing IV
    /// \param[in]  params[3].memref.size(uint32_t): Size of the IV buffer
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_MACSEC_AES_KEY_UNWRAP = 0x00004005,

    /// <b> Description </b>
    /// \brief Command to generate wrapped object from given symmetric key
    ///        object and wrapping key object with AES-CBC algorithm. This
    ///        interface only supports wrapping of ephemeral keys.
    ///
    /// \param[in]  params[0].value.a: Handle for the wrapping Key
    /// \param[in]  params[0].value.b: Handle for the key to be wrapped
    /// \param[out] params[1].memref.buffer: Pointer to buffer containing IV
    /// \param[in]  params[1].memref.size: Size of the IV buffer
    /// \param[out] params[2].memref.buffer: Pointer to buffer for wrapped blob
    /// \param[in]  params[2].memref.size: Size of the wrapped blob
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_AES_CBC_WRAP = 0x00004006,

    /// <b> Description </b>
    /// \brief Command to obtain unwrapped symmetric key object from
    ///        a given wrapped blob and metadata template,using AES-CBC
    ///        algorithm. This interface only supports unwrapping of
    ///        ephemeral keys.
    ///
    /// \param[in]  params[0].value.a: Handle for the unwrapping Key
    /// \param[in]  params[1].memref.buffer: Pointer to buffer containing IV
    /// \param[in]  params[1].memref.size: Size of the IV buffer
    /// \param[in]  params[2].memref.buffer: Pointer to buffer for wrapped blob
    /// \param[in]  params[2].memref.size: Size of the wrapped blob
    /// \param[in]  params[3].memref.buffer: Pointer to metadata template buffer
    /// \param[in]  params[3].memref.size: Size of the metadata template
    /// \param[out] params[4].value.a : Handle for the unwrapped Key
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_AES_CBC_UNWRAP = 0x00004007,

    /// <b> Description </b>
    /// \brief Command to generate wrapped object from given ecc private key
    ///        object and wrapping key object with AES-CBC algorithm. This
    ///        interface only supports wrapping of ephemeral keys.
    ///
    /// \param[in]  params[0].value.a: Handle for the wrapping Key
    /// \param[in]  params[0].value.b: Handle for the key to be wrapped
    /// \param[out] params[1].memref.buffer: Pointer to buffer containing IV
    /// \param[in]  params[1].memref.size: Size of the IV buffer
    /// \param[out] params[2].memref.buffer: Pointer to buffer for wrapped blob
    /// \param[in]  params[2].memref.size: Size of the wrapped blob
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_ECC_PRIV_CBC_WRAP = 0x00004008,

    /// <b> Description </b>
    /// \brief Command to obtain unwrapped ecc private key object from
    ///        a given wrapped blob and metadata template,using AES-CBC
    ///        algorithm. This interface only supports unwrapping of
    ///        ephemeral keys.
    ///
    /// \param[in]  params[0].value.a: Handle for the unwrapping Key
    /// \param[in]  params[1].memref.buffer: Pointer to buffer containing IV
    /// \param[in]  params[1].memref.size: Size of the IV buffer
    /// \param[in]  params[2].memref.buffer: Pointer to buffer for wrapped blob
    /// \param[in]  params[2].memref.size: Size of the wrapped blob
    /// \param[in]  params[3].memref.buffer: Pointer to metadata template buffer
    /// \param[in]  params[3].memref.size: Size of the metadata template
    /// \param[out] params[4].value.a : Handle for the unwrapped Key
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if wrapping key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_ECC_PRIV_CBC_UNWRAP = 0x00004009,

    /// <b> Description </b>
    /// \brief Command to generate the required CMAC on the supplied data
    ///        using the supplied Object handle.
    ///
    /// \param[in]  params[0].value.a: Handle to the key to be used for
    ///                                generating MAC
    /// \param[in]  params[0].value.b: Value to indicate whether it is
    ///                                Sign or Verify operation
    /// \param[in]  params[1].memref.buffer: Pointer to the buffer containing
    ///                                      the data on which the MAC has to be
    ///                                      generated for signing/verification
    /// \param[in]  params[1].memref.size: Size of the buffer containing the data
    /// \param[in]  params[2].memref.buffer: Pointer to the buffer containing
    ///                                      the data that would be used for
    ///                                      comparison for Verification operation
    ///                                         Or would contain the generated MAC
    ///                                         data for Signing operation
    /// \param[in]  params[2].memref.size: Size of the buffer containing MAC data
    ///             \Valid Range: size >=16B and size <=32B
    /// \param[out] params[3].memref.buffer: Pointer to the buffer containing
    ///                                      the data that would be used for
    ///                                      Signing operation
    /// \param[out] params[3].memref.size: Size of the buffer containing MAC data
    ///             \Valid Range: size >=16B and size <=32B
    /// \Note: For VERIFY Operation, params[2] is mandatory parameter and AES CMAC
    ///                              used for Verification is always 16B
    ///        For SIGN   Operation, Params[3] is mandatory parameter and AES CMAC
    ///                              generated is of size 16B
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if the key handle is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_AES_CMAC = 0x0000400A,

    /// \brief Command to create a new Generic Data Object
    ///
    /// This command helps to create new PKCS11 Generic Data object
    /// This command copies plain text Generic Data Object Value from the
    /// template provided by non secure client as input parameter.
    /// The following metadata fields of template are validated before
    /// creating new AES Key Object:
    /// DST, VERSION, OBJTYPE, ID and LENGTH
    ///
    /// \param[in] params[0].memref.buffer(pkcsGenericDataObject_t): Pointer to the template with the ID,
    ///                                     APPLICATION, OBJECT-ID, Label,
    ///                                     Metadata and VALUE filled
    /// \param[in] params[0].memref.size(uint32_t): Size of the Generic Data Object template
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_ERROR_OUT_OF_MEMORY if Generic data object entries are full
    ///                                 and no available slots
    /// \retval TEE_ERROR_BAD_PARAMETERS if the input parameters are not valid
    /// \retval TEE_ERROR_NOT_SUPPORTED if PKCS11KS cannot support this command
    /// \retval TEE_SUCCESS if the Generic data object generation succeeds
    ///
    KEYSTORE_SERVICE_PKCS11_CREATE_DATA_OBJ = 0x00005001,

    /// \brief Command to create a new Peristent Generic Data Object
    ///
    /// This command helps to create new Persistent PKCS11 Generic Data object
    /// in secure storage. The created Generic Data Object is preserved across
    /// system reboots since it is stored in secure storage.
    /// This command copies plain text Generic Data Object Value from the
    /// template provided by non secure client as input parameter.
    /// The following metadata fields of template are validated before
    /// creating new AES Key Object:
    /// DST, VERSION, OBJTYPE, ID and LENGTH
    ///
    /// \param[in] params[0].memref.buffer(pkcsGenericDataObject_t): Pointer to the template with the ID,
    ///                                     APPLICATION, OBJECT-ID, Label,
    ///                                     Metadata and VALUE filled
    /// \param[in] params[0].memref.size(uint32_t): Size of the Generic Data Object template
    /// \param[out] params[3].value.a(KeyHandle): On success, shall contain Key handle
    ///                                to new object.
    ///
    /// \retval TEE_ERROR_OUT_OF_MEMORY if Generic data object entries are full
    ///                                 and no available slots
    /// \retval TEE_ERROR_ACCESS_DENIED if session does not proper permissions
    ///                                 to create persistent objects
    /// \retval TEE_ERROR_BAD_PARAMETERS if the input parameters are not valid
    /// \retval TEE_ERROR_NOT_SUPPORTED if PKCS11KS cannot support this command
    /// \retval TEE_SUCCESS if the Generic data object generation succeeds
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_CREATE_DATA_OBJ = 0x00005002,

    /// \brief Command to make a copy of an existing Generic Data object.
    ///
    /// Finds a source Generic Data Object based on the provided handle and
    /// creates a copy of it with the provided unique ID and label(optional),
    /// value(optional), object-id(optional), application(optional).
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source object to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[in]  params[3].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having value for new copy of the key.
    /// \param[in]  params[3].memref.size(uint32_t): Size of buffer having value
    /// \param[in]  params[4].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having object-id for new copy of the key.
    /// \param[in]  params[4].memref.size(uint32_t): Size of buffer having object-id
    /// \param[in]  params[5].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having application for new copy of the key.
    /// \param[in]  params[5].memref.size(uint32_t): Size of buffer having application
    /// \param[out] params[6].value.a(KeyHandle): On success, contains copied handle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function, source
    ///         object not copyable, object Id is not unique
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source object is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source object is found but a
    ///         a free key entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    ///
    KEYSTORE_SERVICE_PKCS11_COPY_DATA_OBJ = 0x00005003,

    /// \brief Command to make a copy of an existing peristent Generic Data object.
    ///
    /// Finds a source Generic Data Object based on the provided handle and
    /// creates a copy of it with the provided unique ID and label(optional),
    /// value(optional), object-id(optional), application(optional) in the
    /// secure storage.
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Handle of the source object to be copied.
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to unique ID buffer
    ///                               for new copy of the key.
    /// \param[in]  params[1].memref.size(uint32_t): Size of buffer having ID
    /// \param[in]  params[2].memref.buffer(uint8_t*): Optional Parameter.Pointer to
    ///                         buffer having label for new copy of the key.
    /// \param[in]  params[2].memref.size(uint32_t): Size of buffer having label
    /// \param[in]  params[3].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having value for new copy of the key.
    /// \param[in]  params[3].memref.size(uint32_t): Size of buffer having value
    /// \param[in]  params[4].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having object-id for new copy of the key.
    /// \param[in]  params[4].memref.size(uint32_t): Size of buffer having object-id
    /// \param[in]  params[5].memref.buffer(uint8_t*): Optional pointer to
    ///                         buffer having application for new copy of the key.
    /// \param[in]  params[5].memref.size(uint32_t): Size of buffer having application
    /// \param[out] params[6].value.a(KeyHandle): On success, contains copied handle.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function, source
    ///         object not copyable, object Id is not unique
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal retrieval.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source data object is not found.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if source data object is found but a
    ///         a free entry is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    /// \retval TEE_ERROR_NOT_SUPPORTED if object cannot be copied.
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_COPY_DATA_OBJ = 0x00005004,

    /// \brief Set PKCS11 attribute CKA_VALUE for a Generic Data Object
    ///
    /// Sets a new value (CKA_VALUE) for a generic
    /// data object specified by the handle.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the Generic Data Object
    ///            returned by the
    ///            KEYSTORE_SERVICE_PKCS11_CREATE_DATA_OBJ API.
    /// \param[in] params[1].memref.buffer(uint8_t*): Pointer to value buffer.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing value.
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \retval TEE_SUCCESS Success in updating object identifier.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source data object is not found.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Object access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_NOT_SUPPORTED If PKCS11KS cannot support this command
    /// \retval TEE_ERROR_GENERIC for any other failures
    ///
    KEYSTORE_SERVICE_PKCS11_SET_OBJECT_VALUE = 0x00005009,

    /// \brief Set PKCS11 attribute CKA_VALUE of a persistent Generic
    ///        Data Object
    ///
    /// Sets a new value(CKA_VALUE) for a generic
    /// data object in the secure storage specified by the handle.
    ///
    /// \param[in] params[0].value.a(KeyHandle): Handle of the Generic Data Object
    ///            returned by the
    ///            KEYSTORE_SERVICE_PKCS11_PERSISTENT_CREATE_DATA_OBJ API.
    /// \param[in] params[1].memref.buffer(uint8_t*): Pointer to value buffer.
    /// \param[in] params[1].memref.size(uint32_t): Size of buffer containing value.
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \retval TEE_SUCCESS Success in updating object identifier.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if source data object is not found.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Object access is denied because of
    ///         permission check failure or internal key retrieval.
    /// \retval TEE_ERROR_NOT_SUPPORTED If PKCS11KS cannot support this command
    /// \retval TEE_ERROR_GENERIC for any other failures
    ///
    KEYSTORE_SERVICE_PKCS11_PERSISTENT_SET_OBJECT_VALUE = 0x0000500A,

    /// \brief  Function to derive new TLS Symmetric key using
    ///         a master secret key
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Key handle to the master secret key object
    /// \param[out] params[0].value.b(KeyHandle): Key handle to the master derived key object
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                        info containing client random data
    /// \param[in]  params[1].memref.size(uint32_t): Size of the buffer containing the
    ///                                         client random data
    /// \param[in]  params[2].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                        info containing server random data
    /// \param[in]  params[2].memref.size(uint32_t): Size of the buffer containing the
    ///                                         server random data
    /// \param[in]  params[3].memref.buffer(uint8_t*):  Pointer to the buffer containing
    ///                                         info containing label
    /// \param[in]  params[3].memref.size(uint32_t): Size of the buffer containing the
    ///                                         label
    /// \param[in]  params[4].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                         info containing context
    /// \param[in]  params[4].memref.size(uint32_t): Size of buffer containing context
    /// \param[in]  params[5].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                         the template of the derived key
    /// \param[in]  params[5].memref.size(uint32_t): Size of the above buffer, should be
    ///                                         equal to the size of Symmetric
    ///                                         key object
    /// \param[in] params[6]: NONE
    /// \param[in]  params[7].value.a(TLS_VERSION):  TLS version for which the master key
    ///                                         needs to be derived (TLSv1.2
    ///                                         or TLSv1.3)
    /// \param[in]  params[7].value.b(PKCS11_CK_MECHANISM_TYPE): HMAC_SHA type to be used for computation
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_TLS_KDF = 0x00006001,

    /// \brief Function to generate MAC data for a given version
    ///         of TLS
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Key handle to the master secret key object
    /// \param[in]  params[1].memref.buffer(uint8_t*):  Pointer to the buffer containing
    ///                                         info containing label
    /// \param[in]  params[1].memref.size(uint32_t): Size of the buffer containing the
    ///                                         label
    /// \param[in]  params[2].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                         data for which the MAC needs
    ///                                         to be generated
    /// \param[in]  params[2].memref.size(uint32_t): Size of the above data buffer
    /// \param[in]  params[3].memref.buffer(uint8_t*): Pointer to the buffer where the
    ///                                         generated MAC would be stored for
    ///                                         signing OR the signature that
    ///                                         needs to be verified
    /// \param[in]  params[3].memref.size(uint32_t): Size of the above buffer containing MAC
    ///                                       or Signature
    /// \param[in] params[4].value.a(PKCS11_TLS_MAC_OPS): Enum specifying the MAC operation i.e Signing
    ///                                or Verification
    /// \param[in]  params[7].value.a(TLS_VERSION):  TLS version for which the master key
    ///                                         needs to be derived (TLSv1.2
    ///                                         or TLSv1.3)
    /// \param[in]  params[7].value.b(PKCS11_CK_MECHANISM_TYPE): HMAC_SHA type to be used for computation
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_SIGNATURE_INVALID if the verification of signature
    ///         passed fails
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_TLS_MAC = 0x00006002,

    /// \brief  Function to derive new TLS Master secret key using
    ///         a pre master secret key
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Key handle to the pre-master secret
    ///                                 key object
    /// \param[out] params[0].value.b(KeyHandle): Key handle to the master secret key object
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                        info required for master key
    ///                                        derivation i.e the concatenation
    ///                                        of "master secret",
    ///                                        client-hello-random info,
    ///                                        server-hello-random info for TLS v1.2
    /// \param[in]  params[1].memref.size(uint32_t): Size of the buffer containing the
    ///                                      master key derivation info
    /// \param[in]  params[2].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                        the master secret key object
    /// \param[in]  params[2].memref.size(uint32_t): Size of the buffer containing the
    ///                                         master secret key object
    /// \param[in]  params[7].value.a(TLS_VERSION):  TLS version for which the master key
    ///                                         needs to be derived (TLSv1.2
    ///                                         or TLSv1.3)
    /// \param[in]  params[7].value.b(PKCS11_CK_MECHANISM_TYPE): HMAC_SHA type to be used for computation
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_TLS_MASTER_KEY_DERIVE_DH = 0x00006003,

    /// \brief  Function to derive new TLS Symmetric key and Mac
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Key handle to the master secret key object
    /// \param[out] params[0].value.a(KeyHandle): Key handle to the client write key
    /// \param[out] params[0].value.b(KeyHandle): Key handle to the server write key
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                        random info i.e the concatenation
    ///                                        of client and server random info
    /// \param[in]  params[1].memref.size(uint32_t): Size of the buffer containing the
    ///                                      random info
    /// \param[in]  params[2].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        client mac key template
    ///                                        (Optional Param)
    /// \param[in]  params[2].memref.size(uint32_t): Size of the buffer containing the
    ///                                      client mac key template
    ///                                      (Optional Param)
    /// \param[in]  params[3].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        server mac key template
    ///                                        (Optional Param)
    /// \param[in]  params[3].memref.size(uint32_t): Size of the buffer containing the
    ///                                      server mac key template
    ///                                      (Optional Param)
    /// \param[in]  params[4].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        client write key template
    /// \param[in]  params[4].memref.size(uint32_t): Size of the buffer containing the
    ///                                      client write key template
    /// \param[in]  params[5].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        server write key template
    /// \param[in]  params[5].memref.size(uint32_t): Size of the buffer containing the
    ///                                      server write key template
    /// \param[out] params[6].memref.buffer(uint8_t*): Pointer to the buffer to store the
    ///                                        concatenated client and serve
    ///                                        random IV (Optional Param)
    /// \param[out] params[6].memref.size(uint32_t): Size of the above IV buffer, should
    ///                                      be equal to the sum of required
    ///                                      client IV size and server IV size
    ///                                      (Optional Param)
    /// \param[in]  params[7].value.a(TLS_VERSION):  TLS version for which the master key
    ///                                         needs to be derived (TLSv1.2
    ///                                         or TLSv1.3)
    /// \param[in]  params[7].value.b(PKCS11_CK_MECHANISM_TYPE): HMAC_SHA type to be used for computation
    /// \param[out]  params[7].value.a(KeyHandle): Key handle to client mac key
    /// \param[out]  params[7].value.b(KeyHandle): Key handle to server mac key
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_TLS_KEY_AND_MAC_DERIVE = 0x00006004,

    /// \brief  Function for safe derivation of new TLS Key
    ///
    /// \param[in]  params[0].value.a(KeyHandle): Key handle to the master secret key object
    /// \param[out] params[0].value.a(KeyHandle): Key handle to the client write key
    /// \param[out] params[0].value.b(KeyHandle): Key handle to the server write key
    /// \param[in]  params[1].memref.buffer(uint8_t*): Pointer to the buffer containing
    ///                                        random info i.e the concatenation
    ///                                        of client and server random info
    /// \param[in]  params[1].memref.size(uint32_t): Size of the buffer containing the
    ///                                      random info
    /// \param[in]  params[2].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        client mac key template
    ///                                        (Optional Param)
    /// \param[in]  params[2].memref.size(uint32_t): Size of the buffer containing the
    ///                                      client mac key template
    ///                                      (Optional Param)
    /// \param[in]  params[3].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        server mac key template
    ///                                        (Optional Param)
    /// \param[in]  params[3].memref.size(uint32_t): Size of the buffer containing the
    ///                                      server mac key template
    ///                                      (Optional Param)
    /// \param[in]  params[4].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        client write key template
    /// \param[in]  params[4].memref.size(uint32_t): Size of the buffer containing the
    ///                                      client write key template
    /// \param[in]  params[5].memref.buffer(pkcsSymmetricKey_t): Pointer to the buffer containing
    ///                                        server write key template
    /// \param[in]  params[5].memref.size(uint32_t): Size of the buffer containing the
    ///                                      server write key template
    /// \param[in]  params[7].value.a(TLS_VERSION):  TLS version for which the master key
    ///                                         needs to be derived (TLSv1.2
    ///                                         or TLSv1.3)
    /// \param[in]  params[7].value.b(PKCS11_CK_MECHANISM_TYPE): HMAC_SHA type to be used for computation
    /// \param[out]  params[7].value.a(KeyHandle): Key handle to client mac key
    /// \param[out]  params[7].value.b(KeyHandle): Key handle to server mac key
    ///
    /// \retval TEE_SUCCESS Success._
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_ACCESS_DENIED Key access is denied because of
    ///         permission check failure or internal key retrieval,
    ///         validation error.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_ITEM_NOT_FOUND if deriving key is not found.
    /// \retval TEE_ERROR_GENERIC if any generic error.
    ///
    KEYSTORE_SERVICE_PKCS11_TLS_KEY_SAFE_DERIVE = 0x00006005,

    /// <b> Description </b>
    /// \brief Command is used to mark a session is actively accessing the
    ///        persistent objects.
    ///
    /// This command would be used to keep track of the active RO sessions that
    /// are accessing the persistent objects.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    ///
    KEYSTORE_SERVICE_PKCS11_MARK_SESSION_AS_ACTIVE = 0x00007001,

    /// <b> Description </b>
    /// \brief Command is used to mark an active session accessing the persistent
    ///        objects as inactive i.e., post this the session is not expected
    ///        to access any secure storage objects.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    ///
    KEYSTORE_SERVICE_PKCS11_MARK_SESSION_AS_INACTIVE = 0x00007002,

    /// <b> Description </b>
    /// \brief Command is used to flush the update state of the persistent
    ///        objects belonging to a particular secure storage group to
    ///        the Secure Storage.
    ///
    /// \retval TEE_SUCCESS Success.
    /// \retval TEE_ERROR_BAD_PARAMETERS Bad parameters to the function.
    /// \retval TEE_ERROR_BAD_STATE if PKCS11KS state cannot support
    ///         the command.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error in the
    ///         NOR Flash Interface unit, including timeouts, SPI error,
    ///         MX packet CRC check
    /// \retval TEE_ERROR_SECURITY if a security-related error in the NOR
    ///         Flash Interface unit, including MxArmor reported error during
    ///         security field command or failed CCM authentication.
    KEYSTORE_SERVICE_PKCS11_COMMIT_TOKEN_OBJECTS = 0x00007003,

    /// <b> Description </b>
    /// \brief Command is used to retrieve multiple persistent objects
    ///        of a particular type for a token by providing an offset
    ///        in the persistent cache and the number of objects required.
    ///
    /// \param[in] params[0].value.a(PKSKeyObjType): Type of persistent objects requested.
    /// \param[in] params[0].value.b(uint32_t): Persistent cache offset starting from which
    ///                                         objects are requested.
    /// \param[out] params[0].value.b(uint32_t): Persistent cache offset starting from which
    ///                                          objects can be requested in the next retrieval call.
    /// \param[in] params[1].value.a(uint32_t): Number of objects library would like to retrieve.
    /// \param[in] params[1].value.b(uint32_t): Sequence value to indicate whether some persistent
    ///                                         object has been updated/deleted during the retrieval
    ///                                         for the object type. Value 0U indicates that this is
    ///                                         first call as part of the retrieval.
    /// \param[out] params[2].memref.buffer(uint32_t*): Pointer to the buffer that will contain the
    ///                                                 key handles of retrieved objects.
    /// \param[out] params[2].memref.size(uint32_t): Size of the above buffer.
    /// \param[out] params[3].value.a(uint32_t): Number of objects retrieved. When set to zero,
    ///                                          would indicate the completion of retrieval for
    ///                                          the object type.
    /// \param[out] params[3].value.b(uint32_t): Current sequence number for the persistent objects
    ///                                          cache for the object type.
    ///
    /// \retval TEE_SUCCESS if able to retrieve objects even if there are
    ///         not enough objects as requested.
    /// \retval TEE_ERROR_BAD_STATE if Secure Storage in not functional.
    /// \retval TEE_ERROR_BAD_PARAMETERS if invalid params like token Id,
    ///         object type, first offset, number of objects or buffer
    ///         pointer are passed or if param types are invalid.
    /// \retval TEE_ERROR_ACCESS_CONFLICT if invalid sequence Id.
    KEYSTORE_SERVICE_PKCS11_RETRIEVE_PERSISTENT_OBJECTS = 0x00007004,

} NV_TE_KeystoreServiceOperation;

/// \brief Defines various key attribute types supported by TOS OEM
///        Keystore Service.
///
/// For each attribute types, the key index has a different meaning.
typedef enum {
    /// \brief Size of encrypted key material present in the key entry.
    ///        For example: If AES-128-CBC is used as encryption algorithm for
    ///        key material then, \n
    ///        encrypted size is = key_material_size_in_bytes +
    ///        (16 - (key_material_size_in_bytes % 16)) bytes.
    KEYSTORE_KEY_ATTRIBUTE_SIZE = 0x00000000,
    /// \brief Decryption Algorithm of encrypted key material present
    ///        in the key entry.
    /// \note Not supported by TOS OEM Keystore Service.
    ///       Option exists for backward compatiability.
    KEYSTORE_KEY_ATTRIBUTE_ALGO = 0x00000001,
    /// \brief IV to use alongwith decryption algorithm to decrypt
    ///        encrypted key material present in the key entry.
    /// \note Not supported by TOS OEM Keystore Service.
    ///       Option exists for backward compatiability.
    KEYSTORE_KEY_ATTRIBUTE_IV = 0x00000002,
} NV_TE_KeystoreKeyAttribute;

/// \brief Defines various key lookup schemes supported by TOS OEM
///        Keystore Service. For each lookup scheme, the key index
///        has a different meaning.
///
typedef enum {
    /// \brief Absolute lookup. Use this option when a client knows
    ///        the exact key index of the key slot.
    ///
    /// \note  KEYSTORE_LOOKUP_TYPE_ABSOLUTE will eventually be depreciated.
    ///        It is therefore recommended to abstain from its usage.
    KEYSTORE_LOOKUP_TYPE_ABSOLUTE = 0x00000001,

    /// \brief Lookup relative to UUID.If there are more than one keys
    ///        present for a given UUID, clients can use this lookup to
    ///        get Nth key from the keyslots that they are allowed
    ///        to access (UUID is an access control field).
    KEYSTORE_LOOKUP_TYPE_RELATIVE = 0x00000002,

    /// \brief Lookup by entry ID. Each Key is uniquely identified
    ///        by (UUID, Entry ID) tuple. Client can use this
    ///        lookup if it knows the exact tuple to access.
    /// \note Not supported by TOS OEM Keystore Service
    KEYSTORE_LOOKUP_TYPE_BY_ID = 0x00000003,
} NV_TE_KeystoreLookupType;

/// @} // okss_eds_consts

/// \defgroup crypto_comms_unit_function_api Cryptographic Commands
/// @ingroup global_platform_tee_constants
/// @{

/// \brief This type enumerates key usage metadata types
typedef enum {
    KEYSLOT_USAGE_SIGN = 1U,
    KEYSLOT_USAGE_VERIFY,
    KEYSLOT_USAGE_ENCRYPT,
    KEYSLOT_USAGE_DECRYPT,
    KEYSLOT_USAGE_MAC,
    KEYSLOT_USAGE_DERIVE,
    KEYSLOT_USAGE_DUAL_CRYPTO,
    /// \brief KEYSLOT_USAGE_ENCRYPT, KEYSLOT_USAGE_DECRYPT
    ///        and KEYSLOT_USAGE_DERIVE are allowed in compound
    ///        operation usage using this type.
    KEYSLOT_USAGE_WRAPPING,
    /// \brief Labels for TOS to know the differnce b/w AES-GCM
    ///        and other AES algotithms.
    KEYSLOT_USAGE_GCM_ENCRYPT,
    KEYSLOT_USAGE_GCM_DECRYPT,
    KEYSLOT_USAGE_GMAC_SIGN,
    KEYSLOT_USAGE_GMAC_VERIFY,
    KEYSLOT_USAGE_CMAC_SIGN,
    KEYSLOT_USAGE_CMAC_VERIFY,
    KEYSLOT_USAGE_HMAC_SIGN,
    KEYSLOT_USAGE_HMAC_VERIFY,
} KeySlotUsageInfo;

/// \brief This type is used to define key slot handle to hold the key slot.

/// short keyhandles for t19x
typedef uint32_t  KeySlotHandleShort;
/// \brief Long keyslot handles are for t23x

typedef struct{
    uint8_t h[16];
} KeySlotHandleLong;

/// \brief This type is used to define key slot type.
/// It is used as an input in keyslot management commands
typedef enum {
    KEYSLOT_TYPE_AES = 1U,
    KEYSLOT_TYPE_RSA,
    KEYSLOT_TYPE_PKA1,
} KeySlotType;

/// \brief This type is used to define the intent of invoking
///        the AES-CMAC operation.
typedef enum {
    CMAC_VERIFY = 0x0000FFFFU,
    CMAC_GENERATE = 0xFFFF0000U,
} CmacIntent;

/// \brief This type is used to define the fuse key slots
///        which Crypto Service uses.
typedef enum {
    CRYPTO_KEK0_KEYSLOT = 0U,
    CRYPTO_KEK2_KEYSLOT = 2U,
} FuseKeySlot;

/// \brief This type defines the KAC user
typedef enum {
    KAC_USER_TZ = 0x1U, ///< TZ
    KAC_USER_NS, ///< NS
} KAC_USER;

/// \brief This type defines the bit-wise flags for KAC flags
typedef enum {
    KAC_FLAGS_NONE = 0x1U, ///< Default flags
    KAC_FLAGS_EXPORTABLE = 0x2U, ///< Allows key to be exported (wrapped) if set
    KAC_FLAGS_LOCK = 0x4U, ///< Key will be locked
    KAC_FLAGS_PURPOSE_SECONDARY_KEY = 0x8U, ///< The second key for KDF-2KEY
} KAC_FLAGS;

/// \brief This type defines the different Purpose param pertaining to KAC (key access control)
typedef enum {
    KAC_PURPOSE_ALGO_ENC = 0x1U, ///< AES ENC/DEC
    KAC_PURPOSE_ALGO_CMAC, ///< AES-CMAC
    KAC_PURPOSE_ALGO_HMAC, ///< HMAC-SHA2
    KAC_PURPOSE_ALGO_KW, ///< Key Wrap
    KAC_PURPOSE_ALGO_KUW, ///< Key Unwrap
    KAC_PURPOSE_ALGO_KWUW, ///< Key Wrap/Unwrap
    KAC_PURPOSE_ALGO_KDF_1KEY, ///< KDF using one key
    KAC_PURPOSE_ALGO_KDF_2KEY, ///< KDF using two keys
    KAC_PURPOSE_ALGO_XTS, ///< AES-XTS
    KAC_PURPOSE_ALGO_GCM, ///< AES-GCM
} KAC_PURPOSE_ALGO;

/// @}

#endif /* NV_TE_CRYPTOGRAPHIC_CONSTANTS_H */
