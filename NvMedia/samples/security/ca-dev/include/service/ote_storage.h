/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: Storage Services</b>
 *
 * @b Description: Declares data types and functions for
 *                 TLK storage services.
 *
 */

/**
 * @defgroup tlk_services_storage Storage Service
 *
 * Defines Trusted Little Kernel (TLK) storage services
 * declarations and functions.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_STORAGE_H
#define __OTE_STORAGE_H

#include <common/ote_common.h>

/*! Defines the maximum file name length in bytes. */
#define TE_STORAGE_OBJID_MAX_LEN	64

/*! Holds a persistent storage object handle. */
struct __te_storage_object;
typedef struct __te_storage_object *te_storage_object_t;

/*! Defines file access flags. */
typedef enum {
	/*! Specifies read access. */
	OTE_STORAGE_FLAG_ACCESS_READ		= 0x1,
	/*! Specifies write access. */
	OTE_STORAGE_FLAG_ACCESS_WRITE		= 0x2,
	/*! Specifies delete access. */
	OTE_STORAGE_FLAG_ACCESS_WRITE_META	= 0x4,
} te_storage_flags_t;

/*! Creates a persistent storage object handle.
 *
 * @pre This function must be called before accessing a file.
 *
 * \param [in]  name	Name of the persistent storage object (file).
 * \param [in]  flags	File access flags.
 * \param [out] obj	A pointer to persistent object handle.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_create_storage_object(char *name, te_storage_flags_t flags,
				te_storage_object_t *obj);

/*! Opens a persistent storage object.
 *
 * @pre This function must be called before accessing a file.
 *
 * \param [in]  name	Name of the persistent storage object (file).
 * \param [in]  flags	File access flags.
 * \param [out] obj	A pointer to a persistent object handle.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_open_storage_object(char *name, te_storage_flags_t flags,
				te_storage_object_t *obj);

/*! Reads data from a persistent object.
 *
 * The actual number of bytes read can be less than the
 * requested value and does not necessarily mean a read failure.
 *
 * \param [in]  obj	Handle returned by te_open_storage_object().
 * \param [in]  buffer	Data buffer.
 * \param [in]  size	Size of the data buffer in bytes.
 * \param [out] count	Actual number of bytes read.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful and
 *         \a count contains a non-zero value.
 */
te_error_t te_read_storage_object(te_storage_object_t obj, void *buffer,
		uint32_t size, uint32_t *count);

/*! Writes data to the persistent object.
 *
 * \param [in]  obj	Handle returned by te_open_storage_object().
 * \param [in]  buffer	Data buffer.
 * \param [in]  size	Size of the data buffer in bytes.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_write_storage_object(te_storage_object_t obj, const void *buffer,
		uint32_t size);

/*! Gets the size of the data stored in the persistent object.
 *
 * \param [in]  obj	Handle returned by te_open_storage_object().
 * \param [in]  size    A pointer to hold the data size.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_get_storage_object_size(te_storage_object_t obj, uint32_t *size);

/*! Defines seek whence options. */
typedef enum {
	/*! Apply specified offset to start of file. */
	OTE_STORAGE_SEEK_WHENCE_SET	= 0x1,
	/*! Apply specified offset to current position in file. */
	OTE_STORAGE_SEEK_WHENCE_CUR	= 0x2,
	/*! Apply specified offset to current end of file. */
	OTE_STORAGE_SEEK_WHENCE_END	= 0x3,
} te_storage_whence_t;

/*! Seeks to the specified offset in the persistent object.
 *
 * \param [in]  obj	Handle returned by te_open_storage_object().
 * \param [in]  offset  Number of bytes by which to adjust the data position.
 *                      A positive value specifies to adjust the data position
 *                      forward while a negative value specifies to adjust it
 *                      backward.
 * \param [in]  whence  The position in the data from which to calculate
 *                      the new position.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_seek_storage_object(te_storage_object_t obj, int32_t offset,
				te_storage_whence_t whence);

/*! Truncates the data stored in the persistent object to the specified
 * size.  If the specified size is less than the current size
 * then any residual data are lost.
 *
 * \param [in]  obj	Handle to writable persistent storage object returned
 *                      by te_open_storage_object().
 * \param [in]  size    The new size of the object's data in bytes.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_trunc_storage_object(te_storage_object_t obj, uint32_t size);

/*! Deletes a persistent object.
 *
 * \param [in]  obj	Handle returned by te_open_storage_object().
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_delete_storage_object(te_storage_object_t obj);

/*! Deletes a persistent object by name.
 *
 * \param [in]  name	Name of the persistent storage object (file).
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_delete_named_storage_object(const char *name);

/*! Closes the persistent object handle.
 *
 * This must be called when a client is done using the handle. On completion
 * the handle will be invalid and te_open_storage_object() must be used
 * again to obtain a new handle.
 *
 * \param [in]  obj	Handle returned by \c %te_open_storage_object().
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_close_storage_object(te_storage_object_t obj);

/** @} */
#endif
