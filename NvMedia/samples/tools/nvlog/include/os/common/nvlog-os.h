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
#ifndef NVLOG_GUESTVM_ENTITY
#include <tegra-hv-ivc.h>
#endif
#ifdef NVLOG_BUILD_LINUX
#include <sys/syscall.h>
#include <dirent.h>
#endif

int NvLogOsInit(void);
uint64_t NvLogOsGetTimeStamp(void);

uint32_t NvLogOsGetEuId(void);
uint32_t NvLogOsGetPid(void);
uint32_t NvLogOsGetTid(void);

void *NvLogOs_calloc(size_t nmemb, size_t size);
void NvLogOs_free(void *ptr);

#ifndef NVLOG_GUESTVM_ENTITY
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
#endif

/*
 * Function to check whether OS supports NvLog infrastructure.
 *
 * Input : none
 * Output: true, if OS supports NvLog infrastructure, false otherwise.
 */
bool NvLogOsSupportEnabled(void);

/*
 * @brief Function to copy source string to destination string.
 *
 * @param dest [out]			Destination string
 * @param src [in]			Source string to be copied
 * @param size [in]			Size of dest string
 *
 * Return error if size is less than 1.
 * Return error if terminating null byte ('\0') is not detected for the
 * destination string.
 *
 * @return 0 in case of success, < 0 in case of failure.
 */
int NvLogOsStrnCpy(char *dest, const char *src, int size);

/*
 * @brief Function to read U32 value from given Device Tree path.
 *
 * @param base_path [in]		Base path of the Device Tree property
 * @param prop_name [in]		Name of the Device Tree property
 * @param val [out]			U32 value read from Device Tree
 *
 * @return 0 in case of success, < 0 in case of failure.
 */
int NvLogOsGetDeviceTreePropU32(const char *base_path, const char *prop_name, uint32_t *val);

/*
 * @brief Function to read String value from given Device Tree path.
 *
 * @param base_path [in]		Base path of the Device Tree property
 * @param prop_name [in]		Name of the Device Tree property
 * @param str [out]			String value read from Device Tree
 * @param size [in]			Size of the str param above
 *
 * @return 0 in case of success, < 0 in case of failure.
 */
int NvLogOsGetDeviceTreePropStr(const char *base_path, const char *prop_name, char *str, int size);

/*
 * @brief Function to get name and paths of discovery plugin based on index.
 *
 * @param comp_name [in]		Name of the component, see NVLOG_COMP_NAME_*
 * @param stream_name [in]		Stream type, see NVLOG_STREAM_NAME_*
 * @param index [in]			Index of the discovery plugin
 * @param plugin_name [out]		Name of discovery plugin read from Device Tree
 * @param name_size [in]		Size of plugin_name param
 * @param plugin_instance_path [out]	Instance specific plugin path for instance properties
 * @param plugin_common_path [out]	Common plugin path for common properties
 * @param path_size [in]		Size of instance and common plugin path
 *
 * Returned paths can be further used to query device tree properties with
 * NvLogOsGetDeviceTreePropStr() and NvLogOsGetDeviceTreePropU32().
 *
 * Example -
 * If downstream discovery plugin with index 0 is queried for NVLOG_COMP_NAME_ENTITY,
 * return below properties :
 * plugin_instance_path = /nvlog/guest_vm/entity/downstream/discovery_plugin0
 * plugin_common_path = /nvlog/discovery_plugins/<plugin_name>
 * plugin_name = value read from /nvlog/discovery_plugins/<plugin_name>/plugin_register_name
 *
 * plugin_common_path is used to query properties which are common for all instances of
 * the plugin.
 * plugin_instance_path is used to query properties which are specific to comp_name
 * plugin instance.
 *
 * @return 0 in case of success, < 0 in case of failure.
 */
int NvLogOsGetDiscoveryPluginNameAndPath(const char *comp_name,
		const char *stream_name,
		int index,
		char *plugin_name, int name_size,
		char *plugin_instance_path,
		char *plugin_common_path, int path_size);

/*
 * @brief Function to get name and paths of transport plugin based on index.
 *
 * @param comp_name [in]		Name of the component, see NVLOG_COMP_NAME_*
 * @param stream_name [in]		Stream type, see NVLOG_STREAM_NAME_*
 * @param index [in]			Index of the transport plugin
 * @param plugin_name [out]		Name of transport plugin read from Device Tree
 * @param name_size [in]		Size of plugin_name param
 * @param plugin_instance_path [out]	Instance specific plugin path for instance properties
 * @param plugin_common_path [out]	Common plugin path for common properties
 * @param path_size [in]		Size of instance and common plugin path
 *
 * Returned paths can be further used to query device tree properties with
 * NvLogOsGetDeviceTreePropStr() and NvLogOsGetDeviceTreePropU32().
 *
 * Example -
 * If downstream transport plugin with index 0 is queried for NVLOG_COMP_NAME_ENTITY,
 * return below properties :
 * plugin_instance_path = /nvlog/guest_vm/entity/downstream/transport_plugin0
 * plugin_common_path = /nvlog/transport_plugins/<plugin_name>
 * plugin_name = value read from /nvlog/transport_plugins/<plugin_name>/plugin_register_name
 *
 * plugin_common_path is used to query properties which are common for all instances of
 * the plugin.
 * plugin_instance_path is used to query properties which are specific to comp_name
 * plugin instance.
 *
 * @return 0 in case of success, < 0 in case of failure.
 */
int NvLogOsGetTransportPluginNameAndPath(const char *comp_name,
		const char *stream_name,
		int index,
		char *plugin_name, int name_size,
		char *plugin_instance_path,
		char *plugin_common_path, int path_size);


/*
 * @brief Function to get Device Tree path of a component.
 *
 * @param comp_name [in]		Name of the component, see NVLOG_COMP_NAME_*
 * @param path [out]			Device Tree path of the component for querying properties
 * @param path_size [in]		Size of path param
 *
 * Returned path can be further used to query device tree properties with
 * NvLogOsGetDeviceTreePropStr() and NvLogOsGetDeviceTreePropU32().
 *
 * Example -
 * For component NVLOG_COMP_NAME_ENTITY, return /nvlog/guest_vm/entity
 * as the device tree path.
 *
 * @return 0 in case of success, < 0 in case of failure.
 */
int NvLogOsGetCompPath(const char *comp_name, char *path, int path_size);

/* stub defines for compiling non-linux code in linux */
#define CRITICAL 0
