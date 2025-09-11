/*
 * Copyright (c) 2010 GlobalPlatform Inc. All Rights Reserved.
 * The technology provided or described herein is subject to updates, revisions,
 * and extensions by GlobalPlatform. Use of this information is governed by the
 * GlobalPlatform license agreement and any use inconsistent with that agreement
 * is strictly prohibited
 *
 * Copyright (c) 2018-2020, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

 /**
 * @file
 * @brief <b>GlobalPlatform: TEE Trusted Storage</b>
 *
 * @b Description: Describes TEE trusted storage.
 */

/**
 * @defgroup global_platform_tee_storage TEE Trusted Storage
 *
 * Describes TEE trusted storage.
 * These APIs are stubbed on QNX PDK.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_TRUSTED_STORAGE_H
#define TEE_TRUSTED_STORAGE_H

#include <tee_internal/tee_internal_constants.h>
#include <tee_internal/tee_trusted_storage/tee_trusted_storage_constants.h>
#include <tee_internal/tee_trusted_storage/tee_trusted_storage_datatypes.h>

/* -- -- -- -- Generic Object Functions -- -- -- -- */

/**
 * This API is stubbed on QNX PDK
 *
 * Returns the characteristcs of a storage object.
 *
 * It fills in the TEE_ObjectInfo structure.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object An opaque handle for storage object.
 * @param objectInfo a structure to store store object information.
 */
void TEE_GetObjectInfo1(TEE_ObjectHandle object, TEE_ObjectInfo* objectInfo);

/**
 * This API is stubbed on QNX PDK
 *
 * Modifies the objectUsage flags of an object handle to restrict its
 * usage scope.
 *
 * (QNX PDK Only) Error behavior:
 *     prints "unsupported operation" if debug-overlay is enabled
 *     and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object An opaque handle for storage object.
 * @param objectUsage New object usage OR combination of one or
 *        more TEE_USAGE_XXX constants.
 */
void TEE_RestrictObjectUsage1(TEE_ObjectHandle object, uint32_t objectUsage);

/**
 * This API is stubbed on QNX PDK
 *
 * Closes an open Object handle.
 *
 * The object can be persistent or transient.
 *
 * (QNX PDK Only) Error behavior:
 *     prints "unsupported operation" if debug-overlay is enabled
 *     and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object An Opaque handle for storage object.
 */
void TEE_CloseObject(TEE_ObjectHandle object);

/* -- -- -- -- Transient Object Functions -- -- -- -- */

/**
 * This API is stubbed on QNX PDK
 *
 * Resets a transient object to its initial state after allocation
 * i.e. makes it an uninitialized container.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object A handle to the transient object that needs to
 *        be reset.
 */
void TEE_ResetTransientObject(TEE_ObjectHandle object);

/**
 * This API is stubbed on QNX PDK
 *
 * Populates an uninitialized object handle with the attributes of
 * another pre-populated object.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param destObject An uninitialized transient object handle.
 * @param srcObject A pre-populated transient object handle.
 */
void TEE_CopyObjectAttributes(TEE_ObjectHandle destObject,
				TEE_ObjectHandle srcObject);

/**
 * This API is stubbed on QNX PDK
 *
 * Generates a random key or a key-pair and populates a transient key
 * object with the generated key material.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object A handle to an uninitialized transient object.
 * @param keySize The requested key size.
 * @param params Parameters for key generation.
 * @param paramCount The number of parameters.
 */
TEE_Result TEE_GenerateKey(TEE_ObjectHandle object, uint32_t keySize,
				TEE_Attribute* params, uint32_t paramCount);

/* -- -- -- -- Persistent Object Functions -- -- -- -- */

/**
 * This API is stubbed on QNX PDK
 *
 * Opens a handle on an existing persistent object.
 *
 * It returns a handle that can be used to access the object's
 * attributes and data stream.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param storageID The TEE_STORAGE_PRIVATE storage to use.
 * @param objectID The object identifier. This buffer cannot reside in
 *        shared memory.
 * @param objectIDLen The length of the buffer.
 * @param flags The flags which determine the settings under which the
 *        object is opened.
 * @param object A pointer to the handle, whihc contains the
 *        opened handle upon successful completion. If this function
 *        fails the value pointed to by the handle is set to
 *        TEE_HANDLE_NULL.
 *
 * @return TEE_SUCCESS if successful, TEE_ERROR_ITEM_NOT_FOUND if
 *         storageID doesn't exist or if the object identifier cannot
 *         be found in the storage, TEE_ERROR_ACCESS_CONFLICT If an
 *         access right conflict was detected while opening the
 *         persistent storage object, or TEE_ERROR_OUT_OF_MEMORY if
 *         there is not enough memory to complete the operation.
 */
TEE_Result TEE_OpenPersistentObject(uint32_t storageID,
				void* objectID, size_t objectIDLen,
				uint32_t flags,
				TEE_ObjectHandle* object);

/**
 * This API is stubbed on QNX PDK
 *
 * Creates a persistent objecect with intial attributes and initial
 * data steam content.  Optionally, it returns a handle on the created
 * object.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param storageID The TEE_STORAGE_PRIVATE storage to use.
 * @param objectID The object identifier. This buffer cannot reside in
 *        shared memory.
 * @param objectIDLen The length of the buffer.
 * @param flags The flags which determine the settings under which the
 *        object is opened.
 * @param attributes A handle on a transient object from which to take
 *        the persistent object attributes. Can be TEE_HANDLE_NULL if
 *        the persistent object contains no attributes.
 * @param initialData Initial content of the persistent object.
 * @param initialDataLen Length of initial content of the persistent
 *        object.
 * @param object A pointer to the handle, whihc contains the
 *        opened handle upon successful completion. If this function
 *        fails the value pointed to by the handle is set to
 *        TEE_HANDLE_NULL.
 *
 * @return TEE_SUCCESS if successful, TEE_ERROR_ITEM_NOT_FOUND if
 *         storageID doesn't exist or if the object identifier cannot
 *         be found in the storage, TEE_ERROR_ACCESS_CONFLICT If an
 *         access right conflict was detected while opening the
 *         persistent storage object, or TEE_ERROR_STORAGE_NO_SPACE if
 *         insufficient space is available to create persistent object.
 */
TEE_Result TEE_CreatePersistentObject(uint32_t storageID,
				void* objectID, size_t objectIDLen,
				uint32_t flags,
				TEE_ObjectHandle attributes,
				void* initialData, size_t initialDataLen,
				TEE_ObjectHandle* object);

/**
 * This API is stubbed on QNX PDK
 *
 * Marks an object for deletion and closes the object handle.
 *
 * Deleting an object is atomic. Once this function returns the object
 * is definitetly deleted and no more open handles for that object
 * exist.
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object An opaque handle for storage object.
 */
void TEE_CloseAndDeletePersistentObject1(TEE_ObjectHandle object);

/**
 * This API is stubbed on QNX PDK
 *
 * Changes the identifier of an object. The object handle must have
 * been opened with the write-meta access right Renaming is an atomic
 * operation, either the object is renamed or nothing happens
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * @param object An opaque handle for storage object.
 * @param newObjectID The new object identifier. This buffer cannot
 *        reside in shared memory.
 * @param newObjectLen The length of the buffer.
 *
 * @return TEE_SUCCESS if successful, or TEE_ERROR_ACCESS_CONFLICT If
 *         an object with the same identifier exists.
 */
TEE_Result TEE_RenamePersistentObject(TEE_ObjectHandle object,
				void* newObjectID, size_t newObjectLen);

/* -- -- -- -- Data Stream Access Functions -- -- -- -- */

/**
 * \brief This API is stubbed on QNX PDK
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * The TEE_ReadObjectData function attempts to read size bytes from the data
 * stream associated with the object object into the buffer pointed to by buffer
 *
 * @param object An opaque handle for storage object.
 * @param buffer A pointer to the memory which, upon successful completion,
 *        contains the bytes read
 * @param size The number of bytes to read
 * @param count A pointer to the variable which upon successful completion
 *        contains the number of bytes read
 *
 * @return TEE_SUCCESS if successful, or TEE_ERROR_ACCESS_CONFLICT If
 *         an object with the same identifier exists.
 */
TEE_Result TEE_ReadObjectData(TEE_ObjectHandle object,
				void* buffer,
				size_t size,
				uint32_t* count);

/**
 * \brief This API is stubbed on QNX PDK
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * The TEE_WriteObjectData function writes size bytes from the buffer pointed to
 * by buffer to the data stream associated with the open object handle object
 *
 * @param object An opaque handle for storage object.
 * @param buffer A buffer containing the data to be written
 * @param size The number of bytes to write
 *
 * @return TEE_SUCCESS if successful, or TEE_ERROR_ACCESS_CONFLICT If
 *         an object with the same identifier exists.
 */
TEE_Result TEE_WriteObjectData(TEE_ObjectHandle object,
				void* buffer,size_t size);

/**
 * \brief This API is stubbed on QNX PDK
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * The function TEE_TruncateObjectData changes the size of a data stream. If
 * size is less than the current 3138 size of the data stream then all bytes
 * beyond size are removed. If size is greater than the current size of 3139 the
 * data stream then the data stream is extended by adding zero bytes at the end
 * of the stream.
 *
 * @param object An opaque handle for storage object.
 * @param size The new size of the data stream.
 *
 * @return TEE_SUCCESS if successful, or TEE_ERROR_BAD_PARAMETERS if
 *         the provided parameters are invalid.
 */
TEE_Result TEE_TruncateObjectData(TEE_ObjectHandle object,
				size_t size);

/**
 * \brief This API is stubbed on QNX PDK
 *
 * (QNX PDK Only) Error behavior:
 *    prints "unsupported operation" if debug-overlay is enabled
 *    and returns without error.
 *
 * (QNX PDK Only) Side effects:
 *     N/A
 *
 * The TEE_SeekObjectData function sets the data position indicator associated
 * with the object handle.
 *
 * @param object An opaque handle for storage object.
 * @param offset The number of bytes to move the data position. A positive value
 *        moves the data position forward; a negative value moves the data
 *        position backward
 * @param whence The position in the data stream from which to calculate the new
 *        position
 *
 * @return TEE_SUCCESS if successful, or TEE_ERROR_BAD_PARAMETERS if
 *         the provided parameters are invalid.
 */
TEE_Result TEE_SeekObjectData(TEE_ObjectHandle object,
				int32_t offset,
				TEE_Whence whence);
/** @} */
#endif /* TEE_TRUSTED_STORAGE_H */
