/*
 * Copyright (c) 2008-2014 Travis Geiselbrecht
 * Copyright (c) 2019 NVIDIA CORPORATION.  All Rights Reserved.
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
#ifndef __OTE_SECURE_HEAP_H
#define __OTE_SECURE_HEAP_H

#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>

#include <service/ote_service.h>

#define MIN_SECURE_HEAP_SIZE (4 * PAGE_SIZE)

struct secure_heap_stats {
	bool heap_initialized;
	void* heap_start;
	size_t heap_len;
	size_t heap_free;
	size_t heap_max_chunk;
	size_t heap_low_watermark;
};

#if defined(__cplusplus)
extern "C" {
#endif

te_error_t te_secure_heap_init(size_t heap_size);
te_error_t te_secure_heap_destroy(void);

void *te_secure_malloc(size_t size);
void *te_secure_memalign(size_t boundary, size_t size);
void *te_secure_calloc(size_t count, size_t size);
void te_secure_free(void *ptr);

bool te_secure_heap_range_check(void *ptr);
void te_secure_heap_get_stats(struct secure_heap_stats *ptr);

#if defined(__cplusplus)
}
#endif

#endif
