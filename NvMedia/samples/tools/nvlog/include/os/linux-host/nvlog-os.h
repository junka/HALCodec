/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once
#include <sys/queue.h>
#include <stdint.h>
#include <sys/syscall.h>

int NvLogOsInit(void);
uint64_t NvLogOsGetTimeStamp(void);

uint32_t NvLogOsGetEuId(void);
uint32_t NvLogOsGetPid(void);
uint32_t NvLogOsGetTid(void);

void *NvLogOs_calloc(size_t nmemb, size_t size);
void NvLogOs_free(void *ptr);

/*
 * List of reserved mempool IDs.
 * Concurrency protection not needed since access is done only when plugins
 * are initialized (which is serial by nature)
 */
typedef struct NvLog_mempool_elm {
	uint32_t mem_id;
	void *base;
	struct tegra_hv_ivm_cookie *ck;
	SLIST_ENTRY(NvLog_mempool_elm) next;
} NvLog_mempool_elmT;

/*
 * Helper function for mempool reservation/mapping
 *
 * If mempool was previously reserved/mapped - return mapped address
 * else reserve,map and return mapped address.
 *
 * This can help with abstracting OS-specific APIs for mempool reservation and
 * mapping.  However, main purpose is to allow a multiple plugin instances
 * (example : nvlogtrpt_tracebuf_mem) to share the same mempool (that is split
 * into chunks).
 *
 * Input : mempool ID
 * Output : pointer to mapped address range for the entire mempool / NULL
 *
 */
void *NvLogOs_mempool_reserve_map(uint32_t mem_id);

/* stub defines for compiling non-linux code in linux */
#define CRITICAL 0
