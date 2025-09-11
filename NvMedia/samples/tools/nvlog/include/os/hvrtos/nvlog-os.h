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
#include <os_semantics.h>
#include <common-c.h>
#include <vsc_debug.h>
#include <printfn.h>
#include <tegra-hv-ivc.h>

#undef stderr
#define stderr ((void *)NULL)
#define MAX_FPRINTF_LEN 100
#define KB (1024)

int NvLogOsInit(void);
uint64_t NvLogOsGetTimeStamp(void);

uint32_t NvLogOsGetEuId(void);
uint32_t NvLogOsGetPid(void);
uint32_t NvLogOsGetTid(void);

void *NvLogOs_calloc(size_t nmemb, size_t size);
void NvLogOs_free(void *ptr);

bool is_tid_valid(uint32_t tid);

/* TODO: make this a configuration item
 * Plugin should get this info. from the top level application/environment
 * using the logging library/plugin. Since we don't have this configuration
 * option currently, allow this interface to query downstream discovery IVC
 * name.
 *
 * size should be minimimum of MAX_NVLOG_NAME_LEN (32) bytes.
 */
int NvLogOs_GetDiscoveryIVCName(char *ivcName, size_t size);

#define NvLogOs_fprintf(stream, format, ...) \
	dprintf(CRITICAL, format, __VA_ARGS__);

/* TODO: can this be obtained during init using B::GetCpuCount()?
 * Indicates maximum number of entities allowed for this chip in any
 * valid configuration.
 */
#define MAX_ENTITIES_ALLOWED 12

// tracebuf supports max. 16KB
#define MAX_BUFFER_SIZE (16 * KB)

/* memory for run-time allocation - in bytes */
#define HEAP_ALLOCATION (8 * KB)
/* timeout for allocation mutex - 1mS */
#define ALLOCATION_TIMEOUT_US (1 * 1000)

/* metadata for heap allocation */
typedef struct NvLog_allocation {
	uint64_t base;
	uint64_t free_offset;
	uint32_t size;
} NvLog_allocationT;

/* Stub APIs */
void *NvLogOs_mempool_reserve_map(uint32_t mem_id);

static inline int NvLogOsGetDiscoveryPluginNameAndPath(
		__attribute__ ((unused)) const char *comp_name,
		__attribute__ ((unused)) const char *stream_name,
		__attribute__ ((unused)) int index,
		__attribute__ ((unused)) char *plugin_name,
		__attribute__ ((unused)) int name_size,
		__attribute__ ((unused)) char *plugin_instance_path,
		__attribute__ ((unused)) char *plugin_common_path,
		__attribute__ ((unused)) int path_size)
{
	return -1;
}
static inline int NvLogOsGetTransportPluginNameAndPath(
		__attribute__ ((unused)) const char *comp_name,
		__attribute__ ((unused)) const char *stream_name,
		__attribute__ ((unused)) int index,
		__attribute__ ((unused)) char *plugin_name,
		__attribute__ ((unused)) int name_size,
		__attribute__ ((unused)) char *plugin_instance_path,
		__attribute__ ((unused)) char *plugin_common_path,
		__attribute__ ((unused)) int path_size)
{
	return -1;
}

static inline int NvLogOsGetDiscoveryPluginPathFromName(
		__attribute__ ((unused)) const char *comp_name,
		__attribute__ ((unused)) const char *stream_name,
		__attribute__ ((unused)) char *plugin_name,
		__attribute__ ((unused)) char *plugin_instance_path,
		__attribute__ ((unused)) char *plugin_common_path,
		__attribute__ ((unused)) int path_size)
{
	return -1;
}
static inline int NvLogOsGetTransportPluginPathFromName(
		__attribute__ ((unused)) const char *comp_name,
		__attribute__ ((unused)) const char *stream_name,
		__attribute__ ((unused)) char *plugin_name,
		__attribute__ ((unused)) char *plugin_instance_path,
		__attribute__ ((unused)) char *plugin_common_path,
		__attribute__ ((unused)) int path_size)
{
	return -1;
}
