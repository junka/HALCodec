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
 * @brief <b>GlobalPlatform: TEE Trusted Storage Datatypes</b>
 *
 * @b Description: Describes TEE trusted storage datatypes.
 */

/**
 * @defgroup global_platform_tsd TEE Trusted Storage Datatypes
 *
 * Describes TEE trusted storage datatypes.
 * These datatypes are only supported on Linux PDK.
 * @ingroup global_platform_iapi
 * @{
 */

#ifndef TEE_TRUSTED_STORAGE_DATATYPES_H
#define TEE_TRUSTED_STORAGE_DATATYPES_H

#include <tee_common/tee_common_datatypes.h>

/**
 * A TEE_Attribute can either define a buffer or a value. This is
 * determined by bit [29] of the attribute identifier.
 *
 * bit [29] = 0 : attribute is a buffer and the field ref is used.
 * bit [29] = 1 : attribute is a value and the field value is used.
 *
 * An array of attributes is passed to a function, either to populate
 * an an object or to specify operation parameters, and if an
 * attribute identifier is present twice in the array, then only the
 * first occurence is used.
 */
typedef struct
{
	uint32_t attribute_ID;
	TEE_MemoryReference ref;
	TEE_Value value;
} TEE_Attribute;

/**
 * A TEE_ObjectInfo structure contains information about a storage object:
 */
typedef struct
{
        /// The parameter objectType passed during object creation,
        /// i.e persistent or transient.
        uint32_t objectType;
        /// Contains the size of storage object. Set to 0 for an
        /// uninitialized object.
        uint32_t objectSize;
        /// Equal to objecSize for a persistent object. Size is passed
        /// to TEE_AllocateTransientObject for transient objects.
	uint32_t maxObjectSize;
        /// Defines the scope of usage for the storage object. A bit
        /// vector of the TEE_USAGE_XXX bits defined in
        /// TEE_TRUSTED_STORAGE_CONSTANTS_H. It is initialized to
        /// 0xFFFFFFFF and the scope can be narrowed by calling
        /// TEE_SetRestrictUsage
        uint32_t objectUsage;
        /// Set to the current size of the data associated with the
        /// object for persistent objects. Always set to 0 for
        /// transient objects.
        uint32_t dataSize;
        /// Set to the current position in the data for the current
        /// handle. Data positions for different handles on the same
        /// object may differ. Set to 0 for transient objects.
	uint32_t dataPosition;
        /// A bit vector containing one or more of the following
        /// flags:
        ///  - TEE_HANDLE_FLAG_PERSISTENT: Set for a persistent
        ///    object.
        ///  - TEE_HANDLE_FLAG_INITIALIZED: Always set for persistent
        ///    objects. For transient objects, initially cleared, then
        ///    set when the object is initialized.
        ///  - TEE_DATA_FLAG_XXX: Only for persistent objects, these
        ///    flags are used to open or create the object.
       uint32_t handleFlags;
} TEE_ObjectInfo;

/**
 * Enumerates the possible start offset when moving a data position
 * in the data stream associated with a persisitent object
 */
typedef enum
{
	TEE_DATA_SEEK_SET = 0,
	TEE_DATA_SEEK_CUR,
	TEE_DATA_SEEK_END
} TEE_Whence;

/**
 * An opaque handle on a object enumerator These handles are returned
 * by the TEE_AllocatePersistentObjectEnumerator function.
 */
typedef struct __TEE_ObjectEnumHandle* TEE_ObjectEnumHandle;
/** @} */
#endif /* TEE_TRUSTED_STORAGE_DATATYPES_H */
