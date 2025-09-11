/*
 * Copyright (c) 2020-2021, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef NV_TE_SECURE_STORAGE_CONSTANTS_H
#define NV_TE_SECURE_STORAGE_CONSTANTS_H

/// \brief Secure Storage public constants
typedef enum {
    /// \brief The maximum number of objects that may be stored
    /// in the filesystem simultaneously.
    SECURE_STORAGE_MAX_OBJECT_COUNT = 116U,

    /// \brief Maximum allowed size of an object
    SECURE_STORAGE_MAX_OBJECT_SIZE = 1024U,
} NV_TE_SecureStorageConstants;

/// \brief Secure Storage commands.
typedef enum {

    /// <b> Description </b>
    /// \brief Write one object from user provided buffer permanently
    /// into Secure NOR flash. If the ObjectID does not yet exist,
    /// a new object is created; otherwise the existing object is updated.
    ///
    /// \param[in] params[0].memref.buffer the ObjectID
    ///                    Valid value: any 16-byte array except all 0xFF
    ///            params[0].memref.size Length of ObjectID buffer in bytes
    ///                    Valid range: 16
    /// \param[in] params[1].memref.buffer user buffer holding the object
    ///            params[1].memref.size the size of the object in bytes
    ///                    Valid range: (0, SECURE_STORAGE_MAX_OBJECT_SIZE]
    /// \param params[2] NONE
    /// \param params[3] NONE
    ///
    /// \retval TEE_SUCCESS the filesystem has been reset and reinitialized
    /// \retval TEE_ERROR_ACCESS_DENIED client TA is not allowed.
    ///         On ODM production fused device, only PKCS11 Keystore TA
    ///         is allowed. Otherwise, PKCS11 Keystore TA and
    ///         Testing Service TA are allowed.
    /// \retval TEE_ERROR_BAD_STATE Secure NOR flash is not in the
    ///         NormalOperation state.
    /// \retval TEE_ERROR_BAD_PARAMETERS input paramType is not correct
    /// \retval TEE_ERROR_BAD_PARAMETERS object ID is invalid (all 0xFF)
    /// \retval TEE_ERROR_BAD_PARAMETERS object size not in valid range.
    /// \retval TEE_ERROR_EXCESS_DATA when creating an object, the filesystem
    ///         already has SECURE_STORAGE_MAX_OBJECT_COUNT objects.
    /// \retval TEE_ERROR_STORAGE_NO_SPACE the secure NOR becomes only readable
    ///         but not writable because all metadata slots have reached
    ///         MAX_ERASE_COUNT.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error
    ///         in the drivers.
    /// \retval TEE_ERROR_SECURITY if security-related error.
    ///         Secure Storage transitions into DegradedOperation state.
    ///
    SECURE_STORAGE_WRITE_OBJECT               = 0x00002001,

    /// <b> Description </b>
    /// \brief Read one object into user provided buffer.
    ///
    /// \param[in] params[0].memref.buffer the ObjectID
    ///                    Valid value: any 16-byte array except all 0xFF
    ///            params[0].memref.size Length of ObjectID buffer in bytes
    ///                    Valid range: 16
    /// \param[in/out] params[1].memref.buffer buffer to store the object
    ///                params[1].memref.size [in] the size of user buffer;
    ///                                      [out] object size if read succeeds
    /// \param params[2] NONE
    /// \param params[3] NONE
    ///
    /// \retval TEE_SUCCESS the filesystem has been reset and reinitialized
    /// \retval TEE_ERROR_ACCESS_DENIED client TA is not allowed.
    ///         On ODM production fused device, only PKCS11 Keystore TA
    ///         is allowed. Otherwise, PKCS11 Keystore TA and
    ///         Testing Service TA are allowed.
    /// \retval TEE_ERROR_BAD_STATE Secure NOR flash is not in the
    ///         NormalOperation state.
    /// \retval TEE_ERROR_BAD_PARAMETERS input paramType is not correct
    /// \retval TEE_ERROR_BAD_PARAMETERS object ID is invalid (all 0xFF).
    /// \retval TEE_ERROR_ITEM_NOT_FOUND there is no object with the given ID
    /// \retval TEE_ERROR_SHORT_BUFFER the user provided buffer is too short.
    SECURE_STORAGE_READ_OBJECT                = 0x00002002,

    /// <b> Description </b>
    /// \brief Erase one object permanently from Secure NOR flash.
    ///
    /// \param[in] params[0].memref.buffer the ObjectID
    ///                    Valid value: any 16-byte array except all 0xFF
    ///            params[0].memref.size Length of ObjectID buffer in bytes
    ///                    Valid range: 16
    /// \param params[1] NONE
    /// \param params[2] NONE
    /// \param params[3] NONE
    ///
    /// \retval TEE_SUCCESS the filesystem has been reset and reinitialized
    /// \retval TEE_ERROR_ACCESS_DENIED client TA is not allowed.
    ///         On ODM production fused device, only PKCS11 Keystore TA
    ///         is allowed. Otherwise, PKCS11 Keystore TA and
    ///         Testing Service TA are allowed.
    /// \retval TEE_ERROR_BAD_STATE Secure NOR flash is not in the
    ///         NormalOperation state.
    /// \retval TEE_ERROR_BAD_PARAMETERS input paramType is not correct
    /// \retval TEE_ERROR_BAD_PARAMETERS object ID is invalid (all 0xFF).
    /// \retval TEE_ERROR_ITEM_NOT_FOUND there is no object
    ///         with the given ID in the filesystem
    /// \retval TEE_ERROR_STORAGE_NO_SPACE the secure NOR becomes only readable
    ///         but not writable because all metadata slots have reached
    ///         MAX_ERASE_COUNT.
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error
    ///         in the drivers.
    /// \retval TEE_ERROR_SECURITY if security-related error.
    ///         Secure Storage transitions into DegradedOperation state.
    SECURE_STORAGE_ERASE_OBJECT               = 0x00002003,

    /// <b> Description </b>
    /// \brief Reset Secure Storage filesystem.
    ///
    /// This interface is exposed only on Safety Debug Extended Packages
    /// and is stubbed out on Safety Packages.
    ///
    /// If reset with persistent erase, the NOR flash will first be
    /// reset to a state as if it has never been used before by erasing
    /// all metadata on the NOR flash. Then the filesystem is initialized.
    ///
    /// If reset without persistent erase, the filesystem simply cleans
    /// the cache and reloads the objects from NOR flash again, mimicking
    /// a power reset. All previous objects still persist.
    ///
    /// Upon any error, Secure Storage enters into DegradedOperation state;
    /// otherwise, enters into NormalOperation state.
    ///
    /// \param[in] params[0].value.a Whether to do an erase of the NOR flash.
    ///                    Valid range: all unsigned int.
    ///                    zero (false) indicates no persistent erase;
    ///                    non-zero (true) indicates persistent erase.
    /// \param params[1] NONE
    /// \param params[2] NONE
    /// \param params[3] NONE
    ///
    /// \retval TEE_SUCCESS the filesystem has been reset and reinitialized
    /// \retval TEE_ERROR_ACCESS_DENIED if this command is not invoked
    ///         by Testing Service TA or PKCS11 TA
    /// \retval TEE_ERROR_ACCESS_DENIED this command is invoked on a
    ///         ODM production fused device.
    /// \retval TEE_ERROR_BAD_PARAMETERS input paramType is not correct.
    /// \retval TEE_ERROR_NOT_SUPPORTED if ResetFs is stubbed out
    /// \retval TEE_ERROR_COMMUNICATION if non-security related error.
    /// \retval TEE_ERROR_SECURITY if security-related error.
    SECURE_STORAGE_RESET_FS                   = 0x00002004,

    /// <b> Description </b>
    /// \brief Run a secure storage internal test.
    ///
    /// This interface is exposed only on Safety Debug Extended Packages
    /// and is stubbed out on Safety Packages.
    ///
    /// After the test is run, Secure Storage will transition into
    /// DegradedOperation state.
    ///
    /// \param[in] params[0].value.a Test ID of the internal test.
    ///         Valid range: internal test enum value defined in
    ///                      Testing Service's test-header.h.
    /// \param[out] params[1].value.a Result of internal tests
    ///         in TEE_Result type, if the test could be executed.
    /// \param params[2] NONE
    /// \param params[3] NONE
    ///
    /// \retval TEE_SUCCESS test was run, test result stored
    ///         in params[1].value.a
    /// \retval TEE_ERROR_BAD_PARAMETERS input GP paramTypes are incorrect
    /// \retval TEE_ERROR_NOT_IMPLEMENTED if test ID does not refer to an
    ///         implemented test.
    /// \retval TEE_ERROR_NOT_SUPPORTED if testlib is stubbed out
    /// \retval TEE_ERROR_ACCESS_DENIED if this command is not invoked
    ///         by Testing Service TA or PKCS11 TA
    /// \retval TEE_ERROR_ACCESS_DENIED this command is invoked on a
    ///         ODM production fused device.
    ///
    SECURE_STORAGE_RUN_INTERNAL_TEST          = 0x00002005,
} NV_TE_SecureStorageOperation;

#endif /* NV_TE_SECURE_STORAGE_CONSTANTS_H */

