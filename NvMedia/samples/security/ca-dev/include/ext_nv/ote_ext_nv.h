/*
 * Copyright (c) 2013-2016, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: Memory/Cache Management</b>
 *
 * @b Description: Declares memory/cache management in the TLK.
 */

/**
 *
 * @defgroup tlk_mem_management Memory/Cache Management
 * @ingroup ote_modules
 * Defines TLK memory and cache management data types and functions.
 * @{
 */

#ifndef __OTE_EXT_NV_H
#define __OTE_EXT_NV_H

#include <common/ote_common.h>
#include <common/ote_ioctl.h>

/*! Defines cache maintenance operations. */
typedef enum {
	/*! Cache clean operation. */
	OTE_EXT_NV_CM_OP_CLEAN		= 1,
	/*! Cache invalidate operation. */
	OTE_EXT_NV_CM_OP_INVALIDATE	= 2,
	/*! Cache flush operation. */
	OTE_EXT_NV_CM_OP_FLUSH		= 3,
} te_ext_nv_cache_maint_op_t;

/** Holds a pointer to the map memory for a specific ::OTE_CONFIG_MAP_MEM ID
  * value. Used with the OTE_IOCTL_GET_MAP_MEM_ADDR operation. */
typedef struct {
	uint32_t id;		/**< Holds the ::OTE_CONFIG_MAP_MEM ID value. */
	void *addr;		/**< Holds a pointer to the corresponding
                                         address of mapping. */
} te_map_mem_addr_args_t;

/** Holds a pointer to the physical address for a specific virtual address.
    Used with the OTE_IOCTL_V_TO_P operation. */
typedef struct {
	void *vaddr;		/**< Holds a pointer to the virtual address. */
	uint64_t paddr;		/**< Holds a pointer to the corresponding
                                         physical address. */
} te_v_to_p_args_t;

/** Holds an op code and data used to for cache maintenance.
    Used with the OTE_IOCTL_CACHE_MAINT operation. */
typedef struct {
	void *vaddr;		/**< Holds a pointer to the virtual address. */
	uint32_t length;	/**< Holds the length of the address space. */
	uint32_t op;		/**< Holds the operation; see
                                         ::te_ext_nv_cache_maint_op_t. */
} te_cache_maint_args_t;

/*! Bit mask defines for secure storage configuration options */
typedef enum {
	/*! Bit to enable RPMB rollback protection support. */
	OTE_SS_CONFIG_RPMB_ENABLE		= 0x0000001,
	/*! Bit to enable CPC rollback protection support. */
	OTE_SS_CONFIG_CPC_ENABLE		= 0x0000002,
} te_ss_config_option_t;

/*!
 * Performs a cache maintenance operation.
 *
 * This API allows the caller to perform a range of platform-specific cache
 * management operations on the specified memory range.
 *
 * \param [in] op      Cache maintenance operation to perform.
 *                     See ::te_ext_nv_cache_maint_op_t for more information.
 * \param [in] addr    Virtual address of the buffer on which
 *                     the cache maintenance operation is to be performed.
 * \param [in] length  Length in bytes of the buffer specified
 *                     by the \a addr parameter.
 *
 * \retval OTE_SUCCESS
 *            Indicates the operation completed successfully.
 * \retval OTE_ERROR_BAD_PARAMETERS
 *            Indicates one or more of the input parameters is invalid:
 *              - The value specified in the ID parameter is not a valid
 *                \c %te_ext_nv_cache_maint_op_t value.
 *              - The virtual address specified in \a addr is not valid.
 * \retval OTE_ERROR_OUT_OF_MEMORY
 *            Indicates the system ran out of resources.
 * \retval OTE_ERROR_GENERIC
 *            Indicates an unspecified error occurred.
 */
te_error_t te_ext_nv_cache_maint(te_ext_nv_cache_maint_op_t op, void *addr,
				uint32_t length);

/*!
 * Performs virtual-to-physical address translation.
 *
 * This API returns the physical address that corresponds to the specified
 * virtual adress.
 *
 * \param [in]  addr   The virtual address to translate.
 * \param [out] paddr  A pointer with the physical address for \a addr.
 *
 * \retval OTE_SUCCESS
 *            Indicates the operation completed successfully.
 * \retval OTE_ERROR_BAD_PARAMETERS
 *            Indicates one or more of the input parameters is invalid:
 *              - The virtual address specified in \a addr is not valid.
 *              - The \a paddr parameter contains an invalid pointer.
 * \retval OTE_ERROR_OUT_OF_MEMORY
 *            Indicates the system ran out of resources.
 * \retval OTE_ERROR_GENERIC
 *            Indicates an unspecified error occurred.
 */
te_error_t te_ext_nv_virt_to_phys(void *addr, uint64_t *paddr);

#ifdef CONFIG_TRUSTY

/*!
 * Performs virtual-to-physical address translation and cache maintenance.
 *
 * This API returns the physical address that corresponds to the specified
 * virtual adress.
 *
 * \param [in]  addr   The virtual address to translate.
 * \param [in]  size   The size of the mapped address.
 * \param [in]  write  This indicates the DMA directions
 * 							i.e if true then use DMA_FLAG_TO_DEVICE
 * 								if false then use DMA_FLAG_FROM_DEVICE
 * \param [out] paddr  A pointer with the physical address for \a addr.
 *
 * \retval OTE_SUCCESS
 *            Indicates the operation completed successfully.
 * \retval OTE_ERROR_BAD_PARAMETERS
 *            Indicates one or more of the input parameters is invalid:
 *              - The virtual address specified in \a addr is not valid.
 *              - The \a paddr parameter contains an invalid pointer.
 * \retval OTE_ERROR_OUT_OF_MEMORY
 *            Indicates the system ran out of resources.
 * \retval OTE_ERROR_GENERIC
 *            Indicates an unspecified error occurred.
 */

te_error_t te_ext_trusty_prepare_dma(void *addr, uint64_t *paddr, uint32_t size, bool write);

/*!
 * Performs flushing of dcache.
 *
 * This API allows the caller to perform a range of platform-specific cache
 * management operations on the specified memory range.
 *
 * \param [in] regs    Virtual address of the buffer on which
 *                     the cache maintenance operation is to be performed.
 * \param [in] size    Length in bytes of the buffer specified
 *                     by the \a addr parameter.
 * \param [in] write   This indicates the DMA directions
 * 							i.e if true then use DMA_FLAG_TO_DEVICE
 * 								if false then use DMA_FLAG_FROM_DEVICE
 */

long te_ext_trusty_finish_dma(void *regs, uint32_t size, bool write);

#endif

/*!
 * Retrieves mapping of a specified memory range.
 *
 * This API returns the mapped address of the specified memory range
 * setup in the service's manifest via the ::OTE_CONFIG_MAP_MEM option.
 *
 * \param [in]  id     Memory region identifier specified in an
 *                     \c OTE_CONFIG_MAP_MEM configuration option in the
 *                     service's manifest.
 * \param [out] addr   A pointer with the mapped address.
 *
 * \retval OTE_SUCCESS
 *            Indicates the operation completed successfully.
 * \retval OTE_ERROR_BAD_PARAMETERS
 *            Indicates one or more of the input parameters is invalid:
 *              - The \c OTE_CONFIG_MAP_MEM ID parameter is unknown
 *              - The \a addr parameter contains an invalid pointer.
 * \retval OTE_ERROR_OUT_OF_MEMORY
 *            Indicates the system ran out of resources.
 * \retval OTE_ERROR_GENERIC
 *            Indicates an unspecified error occurred.
 */
te_error_t te_ext_nv_get_map_addr(uint32_t id, void **addr);

#ifdef CONFIG_TRUSTY

/*!
 * Retrieves mapping of a specified memory range.
 *
 * This API returns the mapped address of the specified memory range
 * setup in the service's manifest via the ::TRUSTY_APP_CONFIG_MAP_MEM option.
 *
 * \param [in]  id     Memory region identifier specified in an
 *                     \c OTE_CONFIG_MAP_MEM configuration option in the
 *                     service's manifest.
 * \param [in]  size   The size of the address range to be mapped.
 * \param [out] addr   A pointer with the mapped address.
 *
 * \retval OTE_SUCCESS
 *            Indicates the operation completed successfully.
 * \retval OTE_ERROR_BAD_PARAMETERS
 *            Indicates one or more of the input parameters is invalid:
 *              - The \c OTE_CONFIG_MAP_MEM ID parameter is unknown
 *              - The \a addr parameter contains an invalid pointer.
 * \retval OTE_ERROR_OUT_OF_MEMORY
 *            Indicates the system ran out of resources.
 * \retval OTE_ERROR_GENERIC
 *            Indicates an unspecified error occurred.
 */

te_error_t te_ext_nv_get_map_addr_trusty(uint32_t id, void **addr, uint32_t size);

/*!
 * Unmaps the mapped block of memory
 *
 * \param [in] regs    A pointer with the mapped address.
 * \param [in] size    Length in bytes of the buffer specified
 *                     by the \a addr parameter.
 */

long te_ext_nv_get_unmap_addr_trusty(void *regs, uint32_t size);

#endif

/** @} */

#endif
