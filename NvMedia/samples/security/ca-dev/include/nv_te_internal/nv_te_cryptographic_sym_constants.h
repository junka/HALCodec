/*
 * Copyright (c) 2021-2023, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

/**
 * @file
 * @brief <b>GlobalPlatform: NV Cryptographic Symmetric Constants </b>
 *
 * @b Description: Describes NV cryptographic symmetric constants.
 */

#ifndef NV_TE_CRYPTOGRAPHIC_SYM_CONSTANTS_H
#define NV_TE_CRYPTOGRAPHIC_SYM_CONSTANTS_H

/// \brief This type is used to contain Crypto-Sym-Service commands.
///
/// Below is the description of each command's TEE_Param
/// NV_TE_CryptoSymServiceOperation
/// Implementation defined as an enum in nv_te_internal/nv_te_cryptographic_sym_constants.h
typedef enum {
    /// <b> Description </b>
    /// \brief Derive keys from KEK0 with AES-CMAC directly into a key slot.
    ///
    /// Perform AES-CMAC base key derivation operation using KEK0 (fuse) key
    ///
    /// This API is identical to CRYPTO_SYM_SERVICE_DERIVE_KEK2 except that
    /// CRYPTO_SYM_SERVICE_DERIVE_KEK utilizes KEK0 and CRYPTO_SYM_SERVICE_DERIVE_KEK2 utilizes KEK2.
    ///
    /// Refer to CRYPTO_SYM_SERVICE_DERIVE_KEK2 for information about the parameters, return
    /// values, and API behavior of CRYPTO_SYM_SERVICE_DERIVE_KEK.
    CRYPTO_SYM_SERVICE_DERIVE_KEK = 0x00000003,
    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for AES symmetric-key cryptography operations. The INIT command
    /// is typically followed by corresponding UPDATE and DOFINAL commands
    /// to successfully complete an AES cryptography operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating memory to instance an object of the AES Class, within the context
    ///    of the currently open session
    ///    3. Initializing the data memebers of the newly instanced object with
    ///    client provided values, sent as part of operation params
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a AES algorithm identifier (uint32_t)
    ///     \n Valid values:
    ///     \n TEE_ALG_AES_ECB_NOPAD
    ///     \n TEE_ALG_AES_CBC_NOPAD
    ///     \n TEE_ALG_AES_CTR
    ///     \n NV_TE_ALG_AES_ECB
    ///     \n NV_TE_ALG_AES_CBC
    ///     \n NV_TE_ALG_AES_CBC_256
    ///     \n NV_TE_ALG_AES_CBC_256_NOPAD
    ///     \n NV_TE_ALG_AES_CMAC_128
    ///     \n NV_TE_ALG_AES_CMAC_256
    /// \param[in] params[0].value.b AES operation mode identifier (uint32_t);
    ///     \n Valid values:
    ///     \n TEE_MODE_ENCRYPT
    ///     \n TEE_MODE_DECRYPT
    ///
    /// <b> In case of TEE_ALG_AES_CTR algorithm </b>
    /// \param[in,out] params[1].memref.buffer Initialization vector (const uint8_t*)
    /// \param[in,out] params[1].memref.size Size of initialization vector (uint32_t)
    ///     \n Valid values:
    ///     \n 16 bytes
    ///
    /// <b> In case of other supported algorithms </b>
    /// \param[in] params[1].memref.buffer Initialization vector (const uint8_t*)
    /// \param[in] params[1].memref.size Size of initialization vector (uint32_t)
    ///     \n Valid values:
    ///     \n 16 bytes
    ///     \n Note: For all modes that don't require IV value and this paramType must be set to TYPE_NONE
    ///
    /// <b> If not using a keyslot to supply key </b>
    ///
    /// \param[in] params[2].memref.buffer Optional key material in plaintext (const uint8_t*)
    /// \param[in] params[2].memref.size Size of buffer containing key material (uint32_t)
    ///     \n Valid values:
    ///     \n 16 bytes
    ///     \n 32 bytes
    /// \param[in] params[3].value.a Set to zero
    /// \param[in] params[3].value.b Maximum number of data blocks will be sent in update and do_final
    ///
    /// <b> OR, if using a keyslot to supply key </b>
    ///
    /// \param[in] params[2].value.a Key Size in bytes (uint32_t)
    /// \param[in] params[3].value.a Opaque handle to keyslot returned with the appropriate AES key
    ///            installed (uint32_t)
    /// \param[in] params[3].value.b Maximum number of data blocks will be sent in update and do_final
    ///
    /// <b> Return Values </b>
    /// \return TEE_ERROR_BUSY if AES context already exists
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid IV size, algorithm, mode, keyslot and keyslot usage.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_GENERIC if key size is other than 16 or 32.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is greater than 32.
    /// \return TEE_ERROR_GENERIC Unable in instantiate object of class
    /// \return TEE_SUCCESS Command successful, no errors
    ///
    CRYPTO_SYM_SERVICE_AES_INIT             = 0x00000008,

    /// <b> Description </b>
    /// \brief This command provides functionality to update the stage of
    /// AES symmetric-key cryptography operations. The UPDATE command
    /// typically follows an INIT command and is followed by the DOFINAL command
    /// to succesfully complete an AES cryptography operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating DMA memory to store the source and destination buffers, within the context
    ///    of the currently open session
    ///    3. Updating the data memebers of the object
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1].memref.buffer Source buffer for AES operation
    ///            (ciphertext/plaintext depending on operation mode)
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 256 bytes
    /// \param[out]    params[2].memref.buffer Destination buffer for AES operation
    ///                (ciphertext/plaintext depending on operation mode)
    /// \param[in,out] params[2].memref.size Size of destination buffer (uint32_t);
    ///                this value is updated by TOS Cryptography Service
    ///     \n Valid range:
    ///     \n 256 bytes
    /// \param[in] params[3] NONE
    ///
    /// <b> Return Values </b>
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_GENERIC if source or destination length is more than supported size.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination length is greater than 1024 bytes.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed
    /// \return TEE_ERROR_GENERIC in case of other error.
    ///
    CRYPTO_SYM_SERVICE_AES_UPDATE           = 0x00000009,

    /// <b> Description </b>
    /// \brief This command provides functionality to update the stage of
    /// AES symmetric-key cryptography operations. The DO_FINAL command
    /// typically follows an INIT command or and UPDATE command
    /// to succesfully complete an AES cryptography operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating DMA memory to store the source and destination buffers, within the context
    ///    of the currently open session
    ///    3. Updating the data memebers of the object
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1].memref.buffer Source buffer for AES operation
    ///            (ciphertext/plaintext depending on operation mode)
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 256 bytes
    /// \param[out]    params[2].memref.buffer Destination buffer for AES operation
    ///                (ciphertext/plaintext depending on operation mode)
    /// \param[in,out] params[2].memref.size Size of destination buffer (uint32_t);
    ///                this value is updated by TOS Cryptography Service
    ///     \n Valid range:
    ///     \n 256 bytes
    /// \param[in] params[3] NONE
    ///
    /// <b> Return Values </b>
    /// \return TEE_SUCCESS Command successful, no errors
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_GENERIC if source or destination length is greater than supported size.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination length is greater than 1024 bytes.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    ///
    CRYPTO_SYM_SERVICE_AES_DO_FINAL         = 0x0000000A,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// params[0]: none
    /// params[1]: none
    /// params[2]: none
    /// params[3]: none
    /// \return TEE_ERROR_BAD_PARAMETERS Failed input parameter validation
    /// \return TEE_ERROR_GENERIC No exist AES operation to free
    /// \return TEE_SUCCESS Command successful, no errors
    CRYPTO_SYM_SERVICE_AES_FREE             = 0x0000000B,

    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for Message Authentication Code(MAC) cryptographic operations. The INIT command
    /// is typically followed by corresponding UPDATE and DOFINAL commands
    /// to successfully complete the MAC cryptographic operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating memory to instance an object of MAC Class, within the context
    ///    of the currently open session
    ///    3. Initializing the data memebers of the newly instanced object with
    ///    client provided values, sent as part of operation params
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a MAC algorithm identifier (uint32_t);
    ///     \n Valid values:
    ///     \n NV_TE_ALG_AES_CMAC_128
    ///     \n NV_TE_ALG_AES_CMAC_256
    ///     \n TEE_ALG_HMAC_SHA256
    ///     \n NV_TE_ALG_AES_GMAC
    ///
    /// If algo is NV_TE_ALG_AES_GMAC then client must provide the nonce
    ///
    /// \param[in] params[1].memref.buffer Buffer containing nonce
    /// \param[in] params[1].memref.size Size of Nonce
    ///
    /// For any other algo nonce is not needed
    ///
    /// \param[in] params[1] None
    ///
    /// <b> If not using a keyslot to supply key </b>
    ///
    /// \param[in] params[2].memref.buffer Buffer containing public key
    /// \param[in] params[2].memref.size Size of public key (uint32_t);
    ///     \n Valid values:
    ///     \n 16 bytes
    ///     \n 32 bytes
    /// \param[in] params[3].value.a Set this to zero
    ///
    /// <b> OR, if using a keyslot to supply key </b>
    ///     \n Note: SE HW supports using HW keyslots for AES based MAC algos only
    ///
    /// \param[in] params[2].value.a key_size of the key in key slot
    /// \param[in] params[3].value.a key slot handle of the keyslot holding the key.
    ///
    /// \param[in] params[3].value.b Specifies whether the MAC operation incase of
    ///                              CMAC is for generation or verification purpose.
    ///     \n Valid values:
    ///     \n CMAC_VERIFY
    ///     \n CMAC_GENERATE
    ///
    /// \return TEE_ERROR_BUSY MAC context already exists
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid IV size, algorithm, mode, keyslot and keyslot usage.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_GENERIC if key size is other than 16 or 32.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is greater than 32.
    /// \return TEE_ERROR_GENERIC Unable in instantiate object of class or any other error.
    /// \return TEE_SUCCESS if command is successful, no errors
    ///
    CRYPTO_SYM_SERVICE_MAC_INIT             = 0x0000000C,

    /// <b> Description </b>
    /// \brief This command provides functionality to update the stage of
    /// MAC cryptographic operation. The UPDATE command
    /// always follows an INIT command and is followed by the DOFINAL command
    /// to succesfully complete the MAC cryptographic operation.
    /// This delivers a block of data.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///       of valid input values
    ///    2. Allocating DMA memory to store the source and destination buffers, within
    ///       the context of the currently open session
    ///    3. Updating the data memebers of the object
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1].memref.buffer Source plaintext buffer for MAC operation
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t)
    ///     \n Valid range:
    ///     \n 1024 bytes
    /// \param[out] params[2].memref.buffer Destination buffer for MAC operation
    /// \param[in,out] params[2].memref.size Size of destination buffer (uint32_t)
    ///            this value is updated by TOS Cryptography Service
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination length is greater than 1024 bytes.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    ///
    CRYPTO_SYM_SERVICE_MAC_UPDATE           = 0x0000000D,

    /// <b> Description </b>
    /// \brief This command provides functionality to update the stage of
    /// MAC cryptographic operations. The DO_FINAL command
    /// typically follows an INIT command or/and UPDATE command
    /// to succesfully complete the MAC cryptographic operation.
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
    /// \param[in] params[1].memref.buffer Source buffer for MAC operation
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    /// \param[in,out] params[2].memref.buffer Destination buffer for MAC operation
    /// \param[in,out] params[2].memref.size Size of destination buffer (uint32_t);
    ///                this value is updated by TOS Cryptography Service
    ///     \n Valid range:
    ///     \n 1024 bytes
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination length is greater than 1024 bytes.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    ///
    CRYPTO_SYM_SERVICE_MAC_DO_FINAL         = 0x0000000E,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    /// \return TEE_ERROR_BAD_PARAMETERS Failed input parameter validation
    /// \return TEE_ERROR_GENERIC No exist MAC operation to free
    /// \return TEE_SUCCESS Command successful, no errors
    CRYPTO_SYM_SERVICE_MAC_FREE             = 0x0000000F,

    /// <b> Description </b>
    /// \brief This command provides functionality to generate DRBG random number
    /// This command is responsible for the following operations:
    ///    1) Input validation of incoming parameters based on the various permutations
    ///       of valid input values
    ///    2) Allocating memory for HW context and command arguments.
    ///    3) Initializing the data memebers to command argument with
    ///       client provided values, sent as part of operation params
    ///    4) Perform Crypto operation to generate random number.
    ///
    /// <b> Parameters: </b>
    /// \param[out] params[0].memref.buffer(uint8_t*): Destination buffer to store random number
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[0].memref.size(uint32_t): Size of destination buffer
    /// valid_range:
    ///               1 to 1024
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer,
    ///         or if  input buffer is more than max supported size.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \retval TEE_ERROR_GENERIC if crypto operation failed.
    ///
    CRYPTO_SYM_SERVICE_DRBG_GENERATE        = 0x00000013,

    /// <b> Description </b>
    /// \brief This command provides functionality to request a AES key slot.
    /// It is responsible for the following operations:
    ///    1) Input validation of incoming parameters based on the various permutations
    ///       of valid input values
    ///    2) Set requested parameters to keyslot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type.
    /// valid_range:
    ///               KEYSLOT_TYPE_AES
    /// \param[in] params[0].value.b(KeySlotUsageInfo): Valid Keyslot usage info.
    /// valid_range:
    ///              KEYSLOT_USAGE_ENCRYPT, KEYSLOT_USAGE_DECRYPT,
    ///              KEYSLOT_USAGE_MAC, KEYSLOT_USAGE_DERIVE
    /// \param[out] params[1].memref.buffer(KeySlotHandle*): Pointer to the buffer
    ///                                                     containg keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[out] params[1].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types,
    ///         or if unknown keyslot usage.
    /// \retval TEE_ERROR_BAD_STATE if KeySlotMgr is not initialized.
    /// \retval TEE_ERROR_ACCESS_DENIED if caller is not a valid keyslot owner.
    /// \retval TEE_ERROR_BUSY if keyslot is in use.
    ///
    CRYPTO_SYM_SERVICE_REQUEST_SE_KEYSLOT   = 0x00000014,

    /// <b> Description </b>
    /// \brief This command provides functionality to set a key slot with key material.
    /// It is responsible for the following operations:
    ///    1) Input validation of incoming parameters based on the various permutations
    ///       of valid input values
    ///    2) Set requested parameters to keyslot.
    ///
    /// <b> Parameters </b>
    ///
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type.
    /// valid_range:
    ///               KEYSLOT_TYPE_AES
    /// \param[in] params[1].memref.buffer(KeySlotHandle*): Pointer to KeySlotHandle(handle
    ///                                                     of key slot to write.)
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    /// \param[in] params[2].memref.buffer(uint8_t*): AES key material
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[2].memref.size(uint32_t): AES key size
    /// valid_range:
    ///               16, 32
    /// \param[in] params[3].value.a(bool): userNonce if nonce is provided by user.
    /// valid_range:
    ///               0, 1
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer,
    ///         or if keyslot owner VMID check failed, or if failed to get keyslot,
    ///         or if exponentOrKeySize is out of range(UINT32_MAX bits).
    /// \retval TEE_ERROR_GENERIC if failed to set key, or if failed to generate random number,
    ///         or if key size is invalid.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if dynamic memory allocation failed.
    ///
    CRYPTO_SYM_SERVICE_UPDATE_SE_KEYSLOT    = 0x00000015,

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
    ///               KEYSLOT_TYPE_AES
    /// \param[in] params[1].memref.buffer(KeySlotHandle*): Buffer containing 16 byte keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[1].memref.size(uint32_t): Size of the keyslot handle
    /// valid_range:
    ///               16
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer,
    ///         or if keyslot owner VMID check failed. Or if failed to get keyslot.
    /// \retval TEE_ERROR_GENERIC if failed to release keyslot
    ///
    CRYPTO_SYM_SERVICE_RELEASE_SE_KEYSLOT   = 0x00000016,

    /// <b> Description </b>
    /// \brief This command provides functionality to unwrap key to AES keyslot
    /// It is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Unwrap the key.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Supported algorithm.
    ///     \n Valid values:
    ///     \n TEE_ALG_AES_ECB_NOPAD
    ///     \n TEE_ALG_AES_CBC_NOPAD
    ///     \n TEE_ALG_AES_CTR
    ///     \n NV_TE_ALG_AES_ECB
    ///     \n NV_TE_ALG_AES_CBC
    ///     \n NV_TE_ALG_AES_CBC_256
    ///     \n NV_TE_ALG_AES_CBC_256_NOPAD
    /// \param[in] params[0].value.b target keyslot handle
    /// \param[in] params[1].memref.buffer Initialization vector (IV)
    /// \param[in] params[1].memref.size Size of initialization vector (uint32_t)
    ///  Valid values:
    ///    0 byte for AES_ECB and AES_ECB_NOPAD algorithm, 16 bytes otherwise
    ///
    /// <b> If using plain key text for unwrapping key </b>
    ///
    /// \param[in] params[2].memref.buffer Unwrapping key.
    /// \param[in] params[2].memref.size Key Size in bytes (uint32_t).
    ///  Valid range:
    ///    16 bytes
    ///    32 bytes
    ///
    /// <b> OR, if using keyslot for unwrapping key </b>
    ///
    /// \param[in] params[2].value.a keyslot handle for unwrapping key.
    /// \param[in] params[2].value.b Key Size in bytes (uint32_t)
    ///  Valid range:
    ///    16 bytes
    ///    32 bytes
    ///
    /// \param[in] params[3].memref.buffer wrapped Key
    /// \param[in] params[3].memref.size wrapped key size
    ///     \n Valid range:
    ///     \n 16-bytes for _CBC and
    ///     \n 32-bytes for _CBC_256
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer.
    /// \return TEE_ERROR_BAD_PARAMETERS if unwrapping key, wrapped key is NULL.
    /// \return TEE_ERROR_BAD_PARAMETERS if IV is NULL in case of non ECB & ECB_NOPAD algorithm
    /// \return TEE_ERROR_BAD_PARAMETERS if keyslot is invalid. Or if unsupported AES algorithm
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation for the key failed
    /// \return TEE_ERROR_GENERIC if keysize is invalid
    /// \return TEE_ERROR_GENERIC if SE operation failed
    ///
    CRYPTO_SYM_SERVICE_UNWRAP_KEY           = 0x00000017,

    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for AES-CCM cryptographic operations. The INIT command
    /// is always followed by corresponding HANDLE_REQUEST and FREE commands
    /// to successfully complete the AES-CCM cryptographic operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating memory to instance an object of CCM Class, within the context
    ///    of the currently open session
    ///    3. Initializing the data memebers of the newly instanced object with
    ///    client provided values, sent as part of operation params
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Tag length
    /// \param[in] params[0].value.b Valid operation mode
    ///     \n Valid values:
    ///     \n TEE_MODE_ENCRYPT
    ///     \n TEE_MODE_DECRYPT
    /// \param[in] params[1].memref.buffer nonce
    /// \param[in] params[1].memref.size nonce size
    ///
    /// <b> If not using a keyslot to supply key </b>
    ///
    /// \param[in] params[2].memref.buffer Buffer containing public key
    /// \param[in] params[2].memref.size Size of public key (uint32_t);
    ///     \n Valid values:
    ///     \n 16 or 32 bytes
    /// \param[in] params[3].value.a Set this to zero
    ///
    /// <b> If using keyslot to supply key </b>
    ///
    /// \param[in] params[2].value.a contains key size of the key in keyslot
    /// \param[in] params[3].value.a key slot handle of the keyslot holding the key.
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer.
    /// \return TEE_ERROR_BUSY CCM context already exists.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid keyslot. Or if unsupported CCM operation mode.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is more than 32 bytes.
    /// \return TEE_ERROR_GENERIC is key size is other than supported size of 16 or 32 bytes
    ///
    CRYPTO_SYM_SERVICE_AES_CCM_INIT         = 0x00000019,

    /// <b> Description </b>
    /// \brief This command provides functionality to perfor CCM DoFinal operation.
    /// This command is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Allocating memory for source and destination buffer.
    ///    3. Perform CCM operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1].memref.buffer Source data buffer
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    /// \param[out] params[2].memref.buffer Destination data buffer
    /// \param[out] params[2].memref.size Size of destination data buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    /// \param[in] params[3].memref.buffer AAD buffer
    /// \param[in] params[3].memref.size Size of AAD buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is more than max supported size.
    /// \return TEE_ERROR_GENERIC if source, destination or AAD buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory for src and dest buffer
    /// \return TEE_ERROR_GENERIC if SE crypto operation failed
    ///
    CRYPTO_SYM_SERVICE_CCM_HANDLE_REQUEST   = 0x0000001A,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if parameters are invalid
    /// \return TEE_ERROR_GENERIC if error occured while freeing CCM context
    ///
    CRYPTO_SYM_SERVICE_CCM_FREE             = 0x0000001B,

    /// <b> Description </b>
    /// \brief Derive key using AES-CMAC into a key slot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Contains target keyslot handle
    /// \param[in] params[1].memref.buffer Containing source buffer
    /// \param[in] params[1].memref.size Size of source data (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    ///
    /// <b> if using key </b>
    /// \param[in] params[2].memref.buffer Containing key
    /// \param[in] params[2].memref.size Size of key (uint32_t);
    ///     \n Valid values:
    ///     \n 16 or 32 bytes
    ///
    /// <b> OR, if using a keyslot to supply key </b>
    /// \note The KeySlot should be requested for KEYSLOT_USAGE_DERIVE
    ///       usage.
    ///
    /// \param[in] params[2].value.a key_size, in case of using key slot
    /// \param[in] params[3].value.a Source keySlot handle
    /// \note if input key materials are in key slot already, set this,
    ///       and also key size in params[2].value.a.
    ///       otherwise, set this to zero!
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if wrong parameter types
    /// \return TEE_ERROR_BAD_PARAMETERS if fialed to check keyslot owner
    /// \return TEE_ERROR_BAD_PARAMETERS if fialed to get keyslot index
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory
    /// \return TEE_ERROR_GENERIC if key size is invalid
    /// \return TEE_ERROR_GENERIC if failed to generate random number
    /// \return TEE_ERROR_GENERIC if failed to set IV
    /// \return TEE_ERROR_GENERIC if SE DO_FINAL operation failed
    ///
    CRYPTO_SYM_SERVICE_DERIVE_KEY           = 0x00000024,

    /// <b> Description </b>
    /// \brief Derive keys from KEK2 with AES-CMAC directly into a key slot.
    ///
    /// Perform AES-CMAC base key derivation operation using KEK2 (fuse) key
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a target key slot handle
    /// \param[in] params[0].value.b 1: if KCV check is enabled; 0: otherwise
    /// \param[in] params[1].memref.buffer Containing source buffer
    /// \param[in] params[1].memref.size Size of source data (16 or 32 bytes)
    /// \param[in] params[2].value.a KEK key size, 16 or 32 bytes, respectively.
    /// \param[in] params[3].value.a Valid KCV value if KCV check is enabled
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if wrong parameter types
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to check keyslot owner
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to get keyslot index
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory
    /// \return TEE_ERROR_GENERIC if failed to generate random number
    /// \return TEE_ERROR_GENERIC if failed to set IV
    /// \return TEE_ERROR_GENERIC if SE DO_FINAL operation failed
    ///
    CRYPTO_SYM_SERVICE_DERIVE_KEK2          = 0x00000027,

    /// <b> Description </b>
    /// \brief This command provides functionality to generate a AES key
    ///     Supported Key Size: 128 bits
    ///     Pre-allocated AES key in a keyslot is required for this operation
    ///     API returns a wrapped key
    /// It is responsible for the following operations:
    ///    1. Input validation of incoming parameters based on the various permutations
    ///    of valid input values
    ///    2. Generate a key using DRBG
    ///    3. Encrypt with AES-CBC and return the wrapped key
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a keysize of key in keyslot
    /// \param[in] params[0].value.b keyslot handle
    /// \param[in, out] params[1].memref.buffer
    ///                 As input param, it stores the caller supplied
    ///                 initialization vector.
    ///                 As output param, it stores the calulated KCV of the
    ///                 randomly generated key.
    /// \param[in, out] params[1].memref.size
    ///                 As input parameter it stores initialization vector size.
    ///                 As output parameter it stores the size of KCV buffer.
    /// \param[out] params[2].memref.buffer Destination buffer for encrypted key
    /// \param[out] params[2].memref.size Destination buffer size
    /// \param[in] params[3].value.a Key size to generate
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination length is greater than 1024 bytes.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    ///
    CRYPTO_SYM_SERVICE_GENERATE_AES_KEY = 0x00000028,

    /// <b> Description </b>
    /// \brief Derive key using NIST SP800-108 compliant KDF (AES-CMAC based)
    ///        into a key slot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Contains target keyslot handle.
    /// \param[in] params[0].value.b Contains derived key size.
    /// \param[in] params[1].memref.buffer Containing label buffer.
    /// \param[in] params[1].memref.size Size of label data (uint32_t).
    /// \param[in] params[2].memref.buffer Containing context buffer.
    /// \param[in] params[2].memref.size Size of context data (uint32_t).
    ///
    /// <b> if using key </b>
    /// \param[in] params[3].memref.buffer Containing key.
    /// \param[in] params[3].memref.size Size of key (uint32_t).
    ///     \n Valid values:
    ///     \n 16 or 32 bytes
    ///
    /// <b> OR, if using a keyslot to supply key </b>
    /// \note The KeySlot should be requested for KEYSLOT_USAGE_DERIVE
    ///       usage.
    ///
    /// \param[in] params[3].value.a Source KeySlot handle.
    /// \param[in] params[3].value.b size of key.
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if wrong parameter types.
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to check keyslot owner.
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to get keyslot index.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid keyslot usage info.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid target keyslot.
    /// \return TEE_ERROR_BAD_PARAMETERS if unsupported key length.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_GENERIC if failed to clean keyslot.
    /// \return TEE_ERROR_GENERIC if failed to generate random number.
    /// \return TEE_ERROR_GENERIC if operation context setup failed.
    /// \return TEE_ERROR_GENERIC if SE DO_FINAL operation failed.
    /// \return TEE_ERROR_BAD_STATE if failed to derive key in target keyslot.
    ///
    CRYPTO_SYM_SERVICE_DERIVE_KEY_NIST_SP800_108 = 0x00000029,

    /// <b> Description </b>
    /// \brief Derive keys from 128 bits KEK0 with AES-CMAC, and then
    ///     encrypt/wrap the derived key with AES-CBC and return a memory
    ///     buffer constained the wrapped key.
    ///
    /// <b> Parameters: </b>
    /// \param[out] params[0].memref.buffer Contains the wrapped result
    /// \param[out] params[0].memref.size Size of the wrapped reslut
    /// \param[in] params[1].memref.buffer Contains source buffer
    /// \param[in] params[1].memref.size Size of source data (<=1024 bytes)
    /// \param[in] params[2].memref.buffer Contains IV.
    /// \param[in] params[2].memref.size: Size of IV
    /// \param[in] params[3].value.a Contains size of the wrapping root key.
    /// \param[in] params[3].vaule.b Contains keyslot handle of the wrapping root key.
    ///
    /// <b> returns: </b>
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if wrong parameters
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory
    /// \return TEE_ERROR_GENERIC if any other errors.
    ///
    CRYPTO_SYM_SERVICE_WRAP_KEK0_DERIVATION = 0x00000030,


    /// <b> Description </b>
    /// \brief Same as CRYPTO_SYM_SERVICE_WRAP_KEK0_DERIVATION except using
    ///        KEK2 to derive.
    ///
    CRYPTO_SYM_SERVICE_WRAP_KEK2_DERIVATION = 0x00000031,

    /// <b> Description </b>
    /// \brief Derive key materials with 128 bits KEK0 using NIST SP800-108
    ///        complaint KDF(AES-CMAC based) into a key slot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Contains target keyslot handle.
    /// \param[in] params[0].value.b Contains derived key size in bit.
    ///     \n Valid values:
    ///     \n 128 or 256 bits
    /// \param[in] params[1].memref.buffer Containing label buffer.
    /// \param[in] params[1].memref.size Size of label data (uint32_t).
    /// \param[in] params[2].memref.buffer Containing context buffer.
    /// \param[in] params[2].memref.size Size of context data (uint32_t).
    /// \param[in] params[3] NONE.
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if wrong parameter types.
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to check keyslot owner.
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to get keyslot index.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid keyslot usage info.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid target keyslot.
    /// \return TEE_ERROR_BAD_PARAMETERS if unsupported key length.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_GENERIC if failed to clean keyslot.
    /// \return TEE_ERROR_GENERIC if failed to generate random number.
    /// \return TEE_ERROR_GENERIC if operation context setup failed.
    /// \return TEE_ERROR_GENERIC if SE DO_FINAL operation failed.
    /// \return TEE_ERROR_BAD_STATE if failed to derive key in target keyslot.
    ///
    CRYPTO_SYM_SERVICE_NIST_SP800_108_DERIVE_FROM_KEK0 = 0x00000032,


    /// <b> Description </b>
    /// \brief Same as CRYPTO_SYM_SERVICE_NIST_SP800_108_DERIVE_FROM_KEK0
    ///        except using KEK2 to derive.
    ///
    CRYPTO_SYM_SERVICE_NIST_SP800_108_DERIVE_FROM_KEK2 = 0x00000033,

    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for AES CCM symmetric-key decryption operation
    /// in the compound operation of rewrapping the result of
    /// a CCM decryption operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a Tag Length
    ///
    /// \param[in] params[1].memref.buffer nonce
    /// \param[in] params[1].memref.size nonce size
    ///
    /// \param[in] params[2].value.a contains key size of the key in keyslot.
    ///    Valid value: 16 bytes
    /// \param[in] params[3].value.a key slot handle of the keyslot holding
    ///            the key.
    ///
    /// \return TEE_SUCCESS if success.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param
    ///         types or null buffer.
    /// \return TEE_ERROR_BUSY CCM context already exists.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid keyslot Or,
    ///         if unsupported CCM operation mode.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is different from 16 bytes.
    CRYPTO_SYM_SERVICE_CCM_UNWRAP_CBC_WRAP_INIT_CCM = 0x00000034,

    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for AES CBC symmetric-key encryption operation
    /// in the compound operation of rewrapping the result of
    /// a CCM decryption operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[2].memref.buffer Initialization vector (IV).
    /// \param[in] params[2].memref.size Size of initialization vector (uint32_t).
    ///            Valid values: 16 bytes
    ///
    /// \param[in] params[3].value.a Keyslot handle to the key used for
    ///            re-wrapping.
    /// \param[in] params[3].value.b Size of key used for re-wrapping.
    ///    Valid Values: 16 bytes
    ///
    /// <b> Return Values </b>
    /// \return TEE_ERROR_BUSY if AES context already exists.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported
    ///         param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid IV size, algorithm,
    ///         mode, keyslot and keyslot usage.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is different from 16 bytes.
    /// \return TEE_ERROR_GENERIC Unable in instantiate object of class
    /// \return TEE_SUCCESS Command successful, no errors.
    CRYPTO_SYM_SERVICE_CCM_UNWRAP_CBC_WRAP_INIT_CBC = 0x00000035,

    /// <b> Description </b>
    /// \brief This command provides functionality to perform
    ///     a. CCM DoFinal operation to decrypt a buffer by using
    ///        a client supplied key.
    ///     b. Store the unwrapped key in device mapped TZSYSRAM.
    ///     c. AES DoFinal to rewrap the unwrapped key with
    ///        AES-128-CBC-NOPAD using a key that is already loaded in a keyslot.
    ///     d. The wrapped output is returned to the caller.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[1].memref.buffer Source data buffer.
    /// \param[in] params[1].memref.size Size of source buffer (uint32_t);
    ///    \n Valid range:
    ///    \n 1024 bytes.
    ///
    /// \param[in] params[2].memref.buffer AAD buffer.
    /// \param[in] params[2].memref.size Size of AAD buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    ///
    /// \param[out] params[3].memref.buffer Destination data buffer.
    /// \param[out] params[3].memref.size Size of destination data buffer (uint32_t);
    ///     \n Valid range:
    ///     \n 1024 bytes
    ///
    /// \return TEE_SUCCESS Command successful, no errors
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_GENERIC if source or destination length is greater than supported size.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination length is greater than 1024 bytes.
    /// \return TEE_ERROR_BAD_PARAMETERS if source or destination buffer is null.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    CRYPTO_SYM_SERVICE_CCM_UNWRAP_CBC_WRAP_HANDLE_OPERATION = 0x00000036,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if parameters are invalid.
    /// \return TEE_ERROR_GENERIC if error occured while freeing CBC &
    ///     CCM context.
    CRYPTO_SYM_SERVICE_CCM_UNWRAP_CBC_WRAP_FREE = 0x00000037,

    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for AES-CBC symmetric-key encryption operation
    /// in the compound operation of wrapping user key derivation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[2].memref.buffer Initialization vector (IV).
    /// \param[in] params[2].memref.size Size of initialization vector (uint32_t).
    ///     \n Valid values: 16 bytes
    ///
    /// \param[in] params[3].value.a Keyslot handle to the key used for
    ///            re-wrapping of derived key.
    /// \param[in] params[3].value.b Size of key used for re-wrapping.
    ///     \n Valid Values: 16 bytes
    ///
    /// <b> Return Values </b>
    /// \return TEE_ERROR_BUSY if AES context already exists.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported
    ///         param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid IV size, algorithm,
    ///         mode, keyslot and keyslot usage.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is different from 16 bytes.
    /// \return TEE_ERROR_GENERIC Unable in instantiate object of class
    /// \return TEE_SUCCESS Command successful, no errors.
    CRYPTO_SYM_SERVICE_REWRAP_DERIVED_KEY_INIT = 0x00000038,

    /// <b> Description </b>
    /// \brief This command provides the functionality to:
    ///      a. Perform key derivation using NIST-SP800-108 compliant (AES-CMAC)
    ///         based KDF from a caller supplied root key, label and context.
    ///      b. The derived key is stored in buffer allocated from TZSysram.
    ///      c. Wrapping of derived key is performed using caller supplied key
    ///         using AES-CBC algorithm.
    ///      d. The wrapped Key along with the KCV is returned back to the caller.
    ///
    /// <b> Parameters: </b>
    /// \param[in,out] params[0].memref.buffer
    ///                As input param it stores the caller supplied label string.
    ///                As output param it stores the wrapped Key.
    /// \param[in,out] params[0].memref.size
    ///                As input param it stores the size of buffer which should be
    ///                sufficient to hold the derived key and the label string.
    ///                As output param, it stores the actual size of wrapped key.
    ///     \n Valid Range:
    ///     \n 16 Bytes to 502 bytes
    ///
    /// \param[in,out] params[1].memref.buffer
    ///                As input param it stores the caller supplied context.
    ///                As output param it stores the calulated KCV of the derived key.
    /// \param[in,out] params[1].memref.size
    ///                As input param, it stores the size of buffer which should
    ///                be sufficient to hold the derived key KCV as well as the
    ///                context string.
    ///                As output param, it stores the actual size of KCV
    ///     \n Valid Range:
    ///     \n 16 Bytes to 502 bytes
    ///
    /// \param[in] params[2].value.a It stores the keyslot handle of the
    ///            caller supplied root Key.
    /// \param[in] params[2].value.b It stores the derived key length in bits.
    ///     \n Valid Values:
    ///     \n 128 bits and 256 bits
    ///
    /// \param[in] params[3].value.a It stores the length of caller supplied
    ///            label string.
    /// \param[in] params[3].value.b It stores the length of caller supplied
    ///            context string.
    ///
    /// \return TEE_SUCCESS Command successful, no errors
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported
    ///         param types or null pointer.
    /// \return TEE_ERROR_GENERIC if source or destination length is greater
    ///         than supported size.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    CRYPTO_SYM_SERVICE_REWRAP_DERIVE_KEY_HANDLE_OPERATION = 0x00000039,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if parameters are invalid.
    /// \return TEE_ERROR_GENERIC if error occured while freeing CBC context.
    CRYPTO_SYM_SERVICE_REWRAP_DERIVE_KEY_FREE = 0x00000040,

    /// <b> Description </b>
    /// \brief This command provides functionality to initialize
    /// parameters for AES-CBC symmetric-key encryption operation
    /// in the compound operation of wrapping the KEK Derivation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[2].memref.buffer Initialization vector (IV).
    /// \param[in] params[2].memref.size Size of initialization vector (uint32_t).
    ///     valid values: 16 bytes
    ///
    /// \param[in] params[3].value.a Keyslot handle to the key used for
    ///            wrapping the derived key.
    /// \param[in] params[3].value.b Size of key used for re-wrapping.
    ///    valid values: 16 bytes
    ///
    /// <b> Return Values </b>
    /// \return TEE_ERROR_BUSY if AES context already exists.
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported
    ///         param types or null pointer.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid IV size, algorithm,
    ///         mode, keyslot and keyslot usage.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_BAD_PARAMETERS if key size is different from 16 bytes.
    /// \return TEE_ERROR_GENERIC Unable in instantiate object of class
    /// \return TEE_SUCCESS Command successful, no errors.
    CRYPTO_SYM_SERVICE_WRAP_KEK_NISTKDF_DERIVATION_INIT = 0X00000041,

    /// <b> Description </b>
    /// This command provides functionality to perform:
    /// a. Derive key from KEK0/KEK2 keyslot as specified by the caller
    ///    using NIST SP800-108 compliant (AES-CMAC based) KDF.
    /// b. Calculate KCV for the derived key.
    /// c. Perform AES DoFinal to wrap the derived key with
    ///    AES-128-CBC-NOPAD using a key that is already loaded in a keyslot.
    /// d. Return the wrapped output to the caller along with the KCV.
    ///
    /// \param[in] params[0].memref.buffer Containing label buffer.
    /// \param[in] params[0].memref.size Size of label data (uint32_t).
    ///
    /// \param[in, out] params[1].memref.buffer
    ///                 As input param it stores the context string.
    ///                 As output param it stores  the KCV of the derived key.
    ///       [in, out] params[1].memref.size
    ///                 As input param it stores the size of buffer supplied.
    ///                 As output param it stores the size of KCV buffer returned.
    ///       valid values: 3 bytes to 502 bytes
    /// \n Note: Since this is an inout param, the size of buffer should be
    ///          at least 3 bytes to store the KCV value since context string
    ///          can be 1 byte as well.
    /// \param[out] params[2].memref.buffer It stores the wrapped key
    /// \param[out] params[2].memref.size wrapped key length.
    ///     valid values: 16 Bytes and 32 Bytes
    ///
    /// \param[in] params[3].value.a It stores the context length.
    /// \param[in] params[3].value.b It stores the value to determine whether
    ///                KEK0/KEK2 is to be used for key derivation.
    ///        valid values: CRYPTO_SYM_KEK0_KEYSLOT for KEK0
    ///                      CRYPTO_SYM_KEK2_KEYSLOT for KEK2
    ///
    /// <b> Return Values </b>
    /// \return TEE_SUCCESS Command successful, no errors
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    CRYPTO_SYM_SERVICE_WRAP_KEK_NISTKDF_DERIVATION_HANDLE_OPERATION = 0x00000042,

    /// <b> Description </b>
    /// \brief To release under layer context, and reset operation.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if parameters are invalid.
    /// \return TEE_ERROR_GENERIC if error occured while freeing CBC context.
    CRYPTO_SYM_SERVICE_WRAP_KEK_NISTKDF_DERIVATION_FREE = 0X00000043,

    /// <b> Description </b>
    /// This command provides functionality to perform KCV verification
    /// for the key which is provided in keyslot handle.
    /// Fcuntionality as below:
    /// a. Validate param types
    /// b. Calculate KCV for the key in keyslot
    /// c. Compare the calculated KCV with the KCV recevieved in input
    ///    and return the result
    ///
    /// \param[in] params[0].value.a key slot handle containing the key
    /// \param[in] params[0].value.b key_size of the key in key slot
    /// \param[in] params[1].memref.buffer Valid KCV for the key in kesylot
    /// \param[in] params[1].memref.size KCV size
    ///
    /// <b> Return Values </b>
    /// \return TEE_SUCCESS Command successful, no errors
    /// \return TEE_ERROR_BAD_STATE on KCV mis-match
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null pointer.
    /// \return TEE_ERROR_OUT_OF_MEMORY if memory allocation failed.
    /// \return TEE_ERROR_GENERIC in case of other error.
    CRYPTO_SYM_SERVICE_KEY_VALIDATE_KCV = 0x00000045,

    /// <b> Description </b>
    /// \brief This command sets the KAC (key access control) metadata
    /// associated with a key. It is responsible for the following:
    ///     1) Input validation of incoming parameters based on the
    ///        various permutations of valid input values
    ///     2) Set requested KAC parameters to keyslot.
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type.
    /// valid_range:
    ///               KEYSLOT_TYPE_AES
    /// \param[in] params[1].value.a(KAC_USER): User param pertaining to
    ///         KAC (key access control)
    /// valid_range:
    ///               KAC_USER_TZ, KAC_USER_NS
    /// \param[in] params[1].value.b(KAC_FLAGS): Flags param pertaining to
    ///         KAC (key access control)
    /// valid_range:
    ///               KAC_FLAGS_NONE, KAC_FLAGS_EXPORTABLE,
    ///               KAC_FLAGS_LOCK, KAC_FLAGS_PURPOSE_SECONDARY_KEY
    /// \param[in] params[2].value.a(uint32_t): SW param pertaining to
    ///         KAC (key access control)
    /// valid_range:
    ///               0 to 2^16-2
    /// \param[in] params[2].value.b(KAC_PURPOSE_ALGO): Purpose param pertaining
    ///         to KAC (key access control)
    /// valid_range:
    ///               KAC_PURPOSE_ALGO_ENC, KAC_PURPOSE_ALGO_CMAC,
    ///               KAC_PURPOSE_ALGO_HMAC, KAC_PURPOSE_ALGO_KW,
    ///               KAC_PURPOSE_ALGO_KUW, KAC_PURPOSE_ALGO_KWUW,
    ///               KAC_PURPOSE_ALGO_KDF_1KEY, KAC_PURPOSE_ALGO_KDF_2KEY,
    ///               KAC_PURPOSE_ALGO_XTS, KAC_PURPOSE_ALGO_GCM
    /// \param[in] params[3].memref.buffer(KeySlotHandle*): Keyslot Handle for which KAC is to be updated.
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[3].memref.size(uint32_t): Size of KeySlot handle
    /// valid_range:
    ///               16
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param
    ///         types or null buffer, or if KAC MetaData is not within range.
    /// \retval TEE_ERROR_GENERIC if failed to set KAC paramteres.
    /// \retval TEE_ERROR_OUT_OF_MEMORY if dynamic memory allocation failed.
    ///
    CRYPTO_SYM_SERVICE_SET_KAC_METADATA_SE_KEYSLOT = 0x00000047,

    /// <b> Description </b>
    ///
    /// \brief INIT for key derivation using NIST SP800-108 compliant KDF
    ///     (SHA-HMAC based) into a key slot.
    ///
    /// <b> Parameters: </b>
    ///
    /// \param[in] params[0].value.a User param pertaining to
    ///         KAC (key access control)
    ///         \n Valid values: Any value from enum KAC_USER
    /// \param[in] params[0].value.b SW param pertaining to
    ///         KAC (key access control)
    /// \param[in] params[1].value.a Purpose param pertaining
    ///         to KAC (key access control)
    ///         \n Valid values: Any value from enum KAC_PURPOSE_ALGO
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param
    ///         types or null buffer.
    /// \return TEE_ERROR_BAD_PARAMETERS if KAC MetaData is not within range
    /// \return TEE_ERROR_GENERIC if failed to set KAC paramteres
    /// \return TEE_ERROR_OUT_OF_MEMORY if dynamic memory allocation failed
    ///
    CRYPTO_SYM_SERVICE_DERIVE_KEY_HMAC_NISTSP800_INIT = 0x00000076,

    /// <b> Description </b>
    ///
    /// \brief HANDLE_OPERATION for key derivation using NIST SP800-108
    ///     compliant KDF (SHA-HMAC based) into a key slot.
    ///
    /// <b> Parameters: </b>
    ///
    /// <b> if KDK (KeyDerivationKey) provided in a buffer </b>
    ///
    /// \param[in] params[0].memref.buffer Containing KDK key.
    /// \param[in] params[0].memref.size Size of KDK key (uint32_t).
    ///     \n Valid values:
    ///     \n 16 or 32 bytes
    ///
    /// <b> OR, if KDK (KeyDerivationKey) provided in a keySlot </b>
    /// \note The keySlot should be requested for KEYSLOT_USAGE_DERIVE
    ///
    /// \param[in] params[0].value.a Handle to keySlot containing KDK key
    /// \param[in] params[0].value.b Size of KDK key.
    ///     \n Valid values:
    ///     \n 16 or 32 bytes
    ///
    /// \param[in] params[1].memref.buffer Containing context buffer.
    /// \param[in] params[1].memref.size Size of context data (uint32_t).
    ///
    /// \param[in] params[2].memref.buffer Containing label buffer.
    /// \param[in] params[2].memref.size Size of label data (uint32_t).
    ///
    /// \param[in] params[3].value.a keyslot handle to place the derived key
    /// \param[in] params[3].value.b size of derived key
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if wrong parameter types.
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to check keyslot owner.
    /// \return TEE_ERROR_BAD_PARAMETERS if failed to get keyslot index.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid keyslot usage info.
    /// \return TEE_ERROR_BAD_PARAMETERS if invalid target keyslot.
    /// \return TEE_ERROR_BAD_PARAMETERS if unsupported key length.
    /// \return TEE_ERROR_OUT_OF_MEMORY if failed to allocate memory.
    /// \return TEE_ERROR_GENERIC if failed to clean keyslot.
    /// \return TEE_ERROR_GENERIC if crypto operation failed.
    /// \return TEE_ERROR_BAD_STATE if failed to derive key in target keyslot.
    ///
    CRYPTO_SYM_SERVICE_DERIVE_KEY_HMAC_NISTSP800_HANDLE_OPERATION = 0x00000077,

    /// <b> Description </b>
    ///
    /// \brief Reset and Free the DERIVE_KEY_HMAC_NISTSP800 operation
    ///
    /// <b> Parameters: </b>
    ///
    /// \param[in] params[0] NONE
    /// \param[in] params[1] NONE
    /// \param[in] params[2] NONE
    /// \param[in] params[3] NONE
    ///
    /// \return TEE_SUCCESS if success
    /// \return TEE_ERROR_BAD_PARAMETERS if parameters are invalid
    /// \return TEE_ERROR_GENERIC if error occured while freeing context
    ///
    CRYPTO_SYM_SERVICE_DERIVE_KEY_HMAC_NISTSP800_FREE = 0x00000078,

    /// <b> Description </b>
    /// \brief This command provides functionality to
    ///     sync the state of a key slot with SE server.
    /// This command is called in the key loading flow:
    ///     1) Request SE keyslot.
    ///     2) Move key to the keyslot.
    ///     3) Call this command to sync with SE server.
    /// It is responsible for the following operations:
    ///     1) Input validation of incoming parameters based on
    ///        the various permutations of valid input values.
    ///     2) Sync the state of the given keyslot with SE server.
    ///     3) Disable keymover operations.
    ///
    /// <b> Parameters: </b>
    ///
    /// \param[in] params[0].value.a(KeySlotType): type of key slot to sync.
    /// valid_range:
    ///               KEYSLOT_TYPE_AES
    /// \param[in] params[1].value.a(uint32_t): size of the key in bytes
    /// valid_range:
    ///               16, 32
    /// \param[in] params[1].value.b(bool): userNonce
    /// valid_range:
    ///               0, 1
    /// \param[in] params[2].memref.buffer(KeySlotHandle*): Pointer to the keyslot handle.
    /// valid_range:
    ///               Valid Pointer
    /// \param[in] params[2].memref.size(uint32_t): Size of the keyslot handle.
    /// valid_range:
    ///               16
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types or null buffer.
    /// \retval TEE_ERROR_GENERIC if keyslot owner check failed.
    CRYPTO_SYM_SERVICE_SYNC_SE_KEYSLOT_STATE = 0x00000105,

    /// <b> Description </b>
    /// \brief This command provides functionality to request an AES key slot
    ///     and also return the corresponding key slot index.
    /// It is responsible for the following operations:
    ///    1) Input validation of incoming parameters based on the various
    ///       permutations of valid input values.
    ///    2) Set requested parameters to AES keyslot and return key slot
    ///       index.
    /// Since keyslot index is sensitive information, this command should only
    /// be used by TAs and not by VMs. Currently, this is achieved by not
    /// adding this command to isValidCommand().
    ///
    /// <b> Parameters: </b>
    /// \param[in] params[0].value.a(KeySlotType): Valid Keyslot type.
    /// valid_range:
    ///               KEYSLOT_TYPE_AES
    /// \param[in] params[0].value.b(KeySlotUsageInfo): Valid Keyslot usage info.
    /// valid_range:
    ///               KEYSLOT_USAGE_ENCRYPT,
    ///               KEYSLOT_USAGE_DECRYPT,
    ///               KEYSLOT_USAGE_MAC,
    ///               KEYSLOT_USAGE_DERIVE
    ///
    /// \param[out] params[1].memref.buffer(KeySlotHandle*): Pointer to the buffer containing the
    ///     returned keyslot handle
    /// valid_range:
    ///               Valid Pointer
    /// \param[out] params[1].memref.size(uint32_t): Size of the keySlot handle
    /// valid_range:
    ///               16
    /// \param[out] params[2].value.a(uint32_t): keyslot index
    /// valid_range:
    ///               0 to 15
    ///
    /// \retval TEE_SUCCESS if success.
    /// \retval TEE_ERROR_BAD_PARAMETERS if TEE_Param have unsupported param types,
    ///         or if unknown keyslot usage or unsupported keyslot type.
    /// \retval TEE_ERROR_BAD_STATE if KeySlotMgr is not initialized.
    /// \retval TEE_ERROR_ACCESS_DENIED if caller is not a valid keyslot owner.
    /// \retval TEE_ERROR_BUSY if keyslot is in use.
    /// \retval TEE_ERROR_GENERIC if getting keyslot index info failed.
    ///
    CRYPTO_SYM_SERVICE_REQUEST_AES_SE_KEYSLOT_WITH_INDEX = 0x00000106,

#ifdef TOS_DEVTEST_ENABLE_KPI
    /// Calculate init time of Crypto Sym TA
    CRYPTO_SYM_GET_INIT_TIME = 0x00000107,

    /// Calculate init time of GP SE TA
    CRYPTO_SYM_SERVICE_GP_SE_INIT_TIME = 0x00000108,
#endif

} NV_TE_CryptoSymServiceOperation;

#endif /* NV_TE_CRYPTOGRAPHIC_SYM_CONSTANTS_H */
