/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION. All rights reserved.
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

#include <stdint.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <nvlog.h>
#include <nvlog-os.h>

#define NVLOG_MAX_NO_PLUGIN 32
#define NVLOG_MAX_SOURCE_NAME_LEN 32
#define NVLOG_MAX_QUEUE_FILENAME 256
#define NVLOG_MAX_SHMDISC_FILENAME 256

#define NVLOG_PLUGIN_NAME_MAXLEN 32

#define NVLOG_MAX_MSG_LEN 4096

#define NVLOG_PLUGIN_STATE_UNINITIALIZED 0
#define NVLOG_PLUGIN_STATE_INITIALIZED 1
#define NVLOG_PLUGIN_STATE_ABANDONED 2

#define NVLOG_DT_PATH_MAX_LEN		128
#define NVLOG_DT_STRING_PROP_LEN	64

#define NVLOG_COMP_NAME_NVLOGCLIENT			"nvlogclient"
#define NVLOG_COMP_NAME_ENTITY				"entity"

#define NVLOG_DIRECTION_UPSTREAM			"upstream"
#define NVLOG_DIRECTION_DOWNSTREAM			"downstream"

typedef struct NvLogTrptMsg NvLogTrptMsgT;
typedef struct NvLogTrptPlugin NvLogTrptPluginT;
typedef struct NvLogConf NvLogConfT;
typedef struct NvLogDiscoveryPlugin NvLogDiscoveryPluginT;
typedef struct NvLogAppInstance NvLogAppInstanceT;
typedef struct NvLogDiscoveryMsg NvLogDiscoveryMsgT;
typedef struct NvLogInstance NvLogInstanceT;
typedef int (*DiscMsgHandlerT)(NvLogDiscoveryMsgT *);

struct NvLogTrptPlugin {
	char pluginName[NVLOG_PLUGIN_NAME_MAXLEN];
#ifndef ENABLE_HVRTOS_ENV
	char pluginInstanceDtPath[NVLOG_DT_PATH_MAX_LEN];
	char pluginCommonDtPath[NVLOG_DT_PATH_MAX_LEN];
#endif
	int (*nvLogTrptPluginInit)(int, NvLogTrptPluginT *, NvLogInstanceT *);
	int (*nvLogTrptPluginSendMsg)(NvLogTrptPluginT *, NvLogTrptMsgT *);
	int (*nvLogTrptPluginRecvMsg)(NvLogTrptPluginT *, NvLogTrptMsgT *);
	int (*nvLogTrptPluginGetRecvCnt)(void);
	int mode;
	int state;
	void *context;
};

struct NvLogDiscoveryPlugin {
	char pluginName[NVLOG_PLUGIN_NAME_MAXLEN];
#ifndef ENABLE_HVRTOS_ENV
	char pluginInstanceDtPath[NVLOG_DT_PATH_MAX_LEN];
	char pluginCommonDtPath[NVLOG_DT_PATH_MAX_LEN];
#endif
	int (*nvLogDiscoveryPluginInit)(NvLogDiscoveryPluginT *,
						int, NvLogAppInstanceT *, DiscMsgHandlerT);
	int (*nvLogDiscoveryPluginQuery)(NvLogDiscoveryPluginT *,
						NvLogDiscoveryMsgT *, NvLogDiscoveryMsgT *, int);
	int discoveryMode;
	int state;
	int (*discoveryMsgHandler)(NvLogDiscoveryMsgT *);
	void *context;
};

struct NvLogConf {
	char transportPlugin[NVLOG_PLUGIN_NAME_MAXLEN];
	void *transportConfg;
};

/*TODO: can make transport specific items into unions*/
typedef struct NvLogOsInstance {
        uint32_t mem_id;        //mempool specific
        void *mem_base;
        uint64_t mem_offset;
        uint32_t mem_size;
} NvLogOsInstanceT;

struct NvLogInstance {
	bool initialized;
	uint32_t eu_id;
	uint32_t pid;
	uint32_t pid_ext;	//entropy to allow pid re-use.
	uint32_t tid;
	uint32_t tid_ext;	//entropy to allow tid re-use.
	uint32_t sequence_id;
	NvLogTrptPluginT *trptPlugin;
	NvLogDiscoveryPluginT *discPlugin;
	NvLogConfT *config;
	NvLogOsInstanceT logOsInstance;
	CIRCLEQ_ENTRY(NvLogInstance) instanceQueue;
};

/*circular dependency between this header and discovery.h*/
typedef struct NvLogDiscovery_trnspt_req {
	char transport[NVLOG_PLUGIN_NAME_MAXLEN];
	union {
		//only mempool transport needs additional attributes at this point
		struct {
			uint32_t id;
			uint32_t size;
			uint64_t offset;
		} mempool;
	};
} NvLogDiscoveryMsg_trpt_attrT;

typedef struct NvLogDiscoveryMsgNewEntity {
	uint32_t tid;
	uint32_t tid_ext;
	NvLogDiscoveryMsg_trpt_attrT trpt_attr;
} NvLogDiscoveryMsgNewEntityT;

#define NVLOG_APP_MODE_ENTITY 1
#define NVLOG_APP_MODE_CLIENT 2
#define NVLOG_APP_MODE_SERVER 3

struct NvLogAppDiscoveryList_elm;

typedef struct NvLogAppDiscoveryList_elm {
	NvLogDiscoveryPluginT *discPlugin;
	SLIST_ENTRY(NvLogAppDiscoveryList_elm) next;
} NvLogAppDiscoveryList_elmT;

typedef struct NvLogTrptClientPluginListEntry {
	char pluginName[NVLOG_DT_STRING_PROP_LEN];
	char pluginInstanceDtPath[NVLOG_DT_PATH_MAX_LEN];
	char pluginCommonDtPath[NVLOG_DT_PATH_MAX_LEN];
	SLIST_ENTRY(NvLogTrptClientPluginListEntry) next;
} NvLogTrptClientPluginListEntryT;

struct NvLogAppInstance {
	int mode;
	uint32_t eu_id;
	uint32_t pid;
	uint32_t pid_ext;	//entropy to allow pid re-use.
	SLIST_HEAD(trptListHead, NvLogTrptClientPluginListEntry) trptListHead;
	SLIST_HEAD(discListHead, NvLogAppDiscoveryList_elm) discListHead;
};

NvLogInstanceT *NvLogGetInstance(void);
int NvLogClientInit(char *discoveryPluginName, char *transportPluginName,
				DiscMsgHandlerT msgHandle);
NvLogInstanceT *NvLogCreateClientInstance(uint32_t eu_id, uint32_t pid,
						uint32_t pid_ext,
						NvLogDiscoveryMsgNewEntityT *msg);
NvLogAppInstanceT *NvLogGetAppInstance(void);

NvLogInstanceT *NvLogOsCreateClientInstance(void);
NvLogInstanceT *NvLogOsFindInstance(uint32_t tid);
int NvLogOsInsertInstance(NvLogInstanceT *LogInstance);
NvLogInstanceT *NvLogOsGetInstance(void);
NvLogInstanceT *NvLogOsCreateInstance(void);
typedef void (*PrintLogsT)(NvLogInstanceT *instance);
void PrintLogs(NvLogInstanceT *instance);
int NvLogOsIterateAllInstance(PrintLogsT callback);
