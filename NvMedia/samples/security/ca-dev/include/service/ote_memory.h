/*
 * Copyright (c) 2013-2015, NVIDIA CORPORATION. All rights reserved
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
 * <b> NVIDIA Trusted Little Kernel Interface: Memory Functions</b>
 *
 * @b Description: Declares the memory functions in the TLK.
 */

/**
 * @defgroup tlk_services_memalloc Memory Allocation
 *
 * Defines Trusted Little Kernel (TLK) memory allocation services functions.
 *
 * @ingroup tlk_services
 *
 * @{
 */

#ifndef __OTE_MEMORY_H
#define __OTE_MEMORY_H

/*!
 * Allocates memory of specified size.
 *
 * \param [in] size    Size in bytes of desired memory.
 *
 * \return A non-NULL pointer to the requested memory or
 *         NULL if the request to allocate memory failed.
 */
void *te_mem_alloc(uint32_t size);

/*!
 * Allocates and clears memory of specified size.
 *
 * \param [in] size    Size in bytes of desired memory.
 *
 * \return A non-NULL pointer to the requested memory or
 *         NULL if the request to allocate and clear memory failed.
 */
void *te_mem_calloc(uint32_t size);

/*!
 * Changes size of memory by specified amount.
 *
 * \param [in] buffer  Pointer to memory block to resize.
 * \param [in] size    New size in bytes of memory block.
 *
 * \return A non-NULL pointer to the newly resized memory or
 *         NULL if the request to resize failed.
 */
void *te_mem_realloc(void *buffer, uint32_t size);

/*!
 * Frees the specified memory.
 *
 * \param [in] buffer  A pointer to memory to free.
 */
void te_mem_free(void *buffer);

/*!
 * Fills specified memory range with specified value.
 *
 * \param [in] buffer  A pointer to memory to fill.
 * \param [in] value   Value to use for fill operation.
 * \param [in] size    Number of bytes to fill.
 */
void te_mem_fill(void *buffer, uint32_t value, uint32_t size);

/*!
 * Moves specified number of bytes from a source memory range to a destination
 * memory range.
 *
 * @note The two memory ranges may overlap.
 *
 * \param [out] dest  A pointer to destination buffer for move operation.
 * \param [in]  src   A pointer to source buffer for move operation.
 * \param [in]  size  Number of bytes to move.
 */
void te_mem_move(void *dest, const void *src, uint32_t size);

/*!
 * Compares two ranges of memory for equality.
 *
 * \param [in] buffer1  A pointer to first memory range to compare.
 * \param [in] buffer2  A pointer to second memory range to compare.
 * \param [in] size     Number of bytes to compare.
 *
 * \return An integer where:
 *    - 0 indicates the contents of the first memory range match
 *      match the contents of the second memory range.
 *    - < 0 indicates the contents of the first
 *      memory range are less than the contents of the second memory
*       range.
 *    - > 0 indicates the contents of the first memory
 *      range are greater than the contents of the second memory range.
 */
int te_mem_compare(const void *buffer1, const void *buffer2, uint32_t size);

/** @} */

#endif
