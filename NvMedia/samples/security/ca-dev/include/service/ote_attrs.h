/*
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: Service Attributes</b>
 *
 * @b Description: Declares the service attributes in the TLK.
 */

/**
 * @defgroup tlk_services_attribs Common TA Service Attributes
 *
 * Defines Trusted Little Kernel (TLK) common
 * Trusted Application (TA) service attributes.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_ATTRS_H
#define __OTE_ATTRS_H

#include <sys/types.h>

#include <common/ote_error.h>

/*! Defines attribute ID types. */
typedef enum {
	OTE_ATTR_SECRET_VALUE		= 0xC0000000U,
	OTE_ATTR_RSA_MODULES		= 0xD0000130U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_RSA_PUBLIC_EXPONENT	= 0xD0000230U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_RSA_PRIVATE_EXPONENT	= 0xC0000330U,
	OTE_ATTR_RSA_PRIME1		= 0xC0000430U,
	OTE_ATTR_RSA_PRIME2		= 0xC0000530U,
	OTE_ATTR_RSA_EXPONENT1		= 0xC0000630U,
	OTE_ATTR_RSA_EXPONENT2		= 0xC0000730U,
	OTE_ATTR_RSA_COEFFICIENT	= 0xC0000830U,
	OTE_ATTR_DSA_PRIME		= 0xD0001031U,
	OTE_ATTR_DSA_SUBPRIME		= 0xD0001131U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DSA_BASE		= 0xD0001231U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DSA_PUBLIC_VALUE	= 0xD0000131U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DSA_PRIVATE_VALUE	= 0xD0000231U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DH_PRIME		= 0xD0001032U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DH_SUBPRIME		= 0xD0001132U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DH_BASE		= 0xD0001232U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DH_X_BITS		= 0xF0001332U, /**< OTE_ATTR_VAL | OTE_ATTR_PUB. */
	OTE_ATTR_DH_PUBLIC_VALUE	= 0xD0000132U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_DH_PRIVATE_VALUE	= 0xC0000232U,
	OTE_ATTR_RSA_OAEP_LABEL		= 0xD0000930U, /**< OTE_ATTR_PUB. */
	OTE_ATTR_RSA_PSS_SALT_LENGTH	= 0xF0000A30U, /**< OTE_ATTR_VAL | OTE_ATTR_PUB. */
} te_attribute_id_t;

/*! Defines attribute ID type flag bitmasks. */
#define OTE_ATTR_VAL		1 << 29
#define OTE_ATTR_PUB		1 << 28

/*! Defines attribute object internals.
 *  Attributes can be integer or reference type.
 */
typedef struct {
	te_attribute_id_t id;
	union {
		struct {
			void *buffer;
			size_t size;
		} ref;

		struct {
			uint32_t a, b;
		} value;
	} content;
} te_attribute_t;

/*! Sets up a memory attribute struct for use in other functions.
 * This function stores the \a id, \a buffer, and \a size parameters
 * in the memory attribute.
 * The \a id parameter must be compatible with the memory attributes.
 *
 * @sa te_get_mem_attribute_buffer() and te_get_mem_attribute_size()
 *
 * \param [in,out] attr   Attribute object.
 * \param [in]     id     Attribute ID of the buffer.
 * \param [in]     buffer Memory location of the buffer.
 * \param [in]     size   Size of the buffer.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_set_mem_attribute(te_attribute_t *attr, te_attribute_id_t id,
		void *buffer, uint32_t size);

/*! Gets a memory attribute buffer.
 *  Stores in \a ret the address of the buffer in the memory attribute.
 *
 * \param [in]  attr Attribute object.
 * \param [out] ret  Return buffer pointer.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_get_mem_attribute_buffer(te_attribute_t *attr, void **ret);

/*! Gets memory attribute size.
 *  Stores in \a ret the size of the buffer in the mem attribute.
 *
 * \param [in]  attr Attribute object.
 * \param [out] ret  Return size.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 */
te_error_t te_get_mem_attribute_size(te_attribute_t *attr, size_t *ret);

/*! Copies the memory attribute.
 *  Copies the memory attribute buffer to the destination buffer,
 *  which must previously be allocated.
 *
 * \param [in,out] buffer Destination buffer.
 * \param [in]     key    Source attribute.
 */
void te_copy_mem_attribute(void *buffer, te_attribute_t *key);

/*! Sets the integer attribute.
 *  Sets the attribute with integer values and \a id.
 *  The \a id parameter must match the integer type.
 *
 * \retval OTE_SUCCESS Indicates the operation was successful.
 *
 * \param [in,out] attr Attribute object.
 * \param [in]     id   Attribute ID.
 * \param [in]     a    Integer value.
 * \param [in]     b    Integer value.
 */
te_error_t te_set_int_attribute(te_attribute_t *attr, te_attribute_id_t id,
		uint32_t a, uint32_t b);

/*! Frees internal attribute memory.
 *  Frees any memory refereces by attribute.
 *
 * \param [in] attr Attribute object.
 */
void te_free_internal_attribute(te_attribute_t *attr);

/*! Copies attribute internals.
 *  After allocating space in destination attribute, this function
 *  copies integer memory attributes.
 *
 * \param [in,out] dst Destination attribute.
 * \param [in]     src Source attribute.
 */
te_error_t te_copy_attribute(te_attribute_t *dst, te_attribute_t *src);

/** @} */

#endif
