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
#include <nvlog_lib.h>
#include <nvlogdiscovery.h>
#include <semaphore.h>

/* SHM needs 0666 given clients of logging can be from any user/group */
#define SHM_PERMISSIONS 0666
#define SEM_PERMISSIONS 0666

/* nanosec and millisec conversion constants */
#define ONE_K (1000)
#define MS_TO_NS (ONE_K * ONE_K)
#define S_TO_NS (ONE_K * MS_TO_NS)

#define NVLOG_SHM_DISC_MSG_SIZE ((sizeof(NvLogDiscoveryMsgT) + \
					sizeof(NvLogDiscoveryMsgNewEntityT)))
#define NVLOG_SHM_DISC_METADATA (sizeof(NvLogDiscShmMetadataT))

#define NVLOG_SHM_DNSTREAM_DISC_MSG_RECORDS 500
#define NVLOG_SHM_DNSTREAM_DISC_BUFSZ ((NVLOG_SHM_DISC_MSG_SIZE * \
					NVLOG_SHM_DNSTREAM_DISC_MSG_RECORDS) + \
					NVLOG_SHM_DISC_METADATA)

#define NVLOG_SHM_UPSTREAM_DISC_MSG_RECORDS 10
#define NVLOG_SHM_UPSTREAM_DISC_BUFSZ ((NVLOG_SHM_DISC_MSG_SIZE * \
					NVLOG_SHM_UPSTREAM_DISC_MSG_RECORDS) + \
					NVLOG_SHM_DISC_METADATA)

/* use nvidia green hexcode as 'init' signature for NvLogDiscShmMetadata */
#define NVLOG_DISCSHM_INIT_SIGNATURE 0x76B900

/* FIFO records in mapped SHM */
typedef char (*p_NvLogDiscMsgFifo)[NVLOG_SHM_DISC_MSG_SIZE];

/* SHM FIFO bookkeeping */
typedef struct NvLogDiscShmMetadata {
	uint32_t init;
	uint32_t read_offset;
	uint32_t write_offset;
	bool buffer_full;
} NvLogDiscShmMetadataT;

typedef struct NvLogDiscoveryShmDataCtxt {
	int descriptor;
	uint16_t max_records;
	uint32_t shm_size;
	uint32_t semTimeoutMs;
	sem_t *sem;
	p_NvLogDiscMsgFifo fifo_base;
	NvLogDiscShmMetadataT *metadata;
} NvLogDiscoveryShmDataCtxtT;

typedef struct NvLogDiscoveryPluginShmCtxt {
	//clientToEntity
	NvLogDiscoveryShmDataCtxtT clientToEntity;
	//entityToClient
	NvLogDiscoveryShmDataCtxtT entityToClient;
	//common
	pthread_t clientThreadId;
	uint32_t eu_id;
	uint32_t pid;
	uint32_t pid_ext;
	uint32_t shmDiscSemInitVal;
	uint32_t rwRetryCounter;
	uint32_t rwRetryDelayUs;
	uint32_t clientSleepUs;
	uint32_t semTimeoutMs;
	char shmEntityToClientNameDt[NVLOG_DT_STRING_PROP_LEN];
	char semEntityToClientNameDt[NVLOG_DT_STRING_PROP_LEN];
	char shmClientToEntityNameDt[NVLOG_DT_STRING_PROP_LEN];
	char semClientToEntityNameDt[NVLOG_DT_STRING_PROP_LEN];
} NvLogDiscoveryPluginShmCtxtT;

static void *SHMDiscoveryClientThread(void *param);
static int NvLogDiscShmInitialize(NvLogDiscoveryShmDataCtxtT *datactxt, uint32_t semTimeoutMs);
static int NvLogDiscShmRead(NvLogDiscoveryShmDataCtxtT *datactxt, p_NvLogDiscMsgFifo data);
static int NvLogDiscShmWrite(NvLogDiscoveryShmDataCtxtT *datactxt,
						p_NvLogDiscMsgFifo data);
static int SHMSendClientToEntityMsg(NvLogDiscoveryPluginT *plugin,
						uint32_t pid, uint32_t pid_ext,
						NvLogDiscoveryMsgT *msg);
static int NvLogDiscoveryPluginShmInit(NvLogDiscoveryPluginT *plugin,
						int discoveryMode, NvLogAppInstanceT *instance,
						DiscMsgHandlerT);
/* Set to non-blocking by default - enable with NVLOG_BLOCKING_DISCOVERY */
static int NvLogDiscoveryPluginShmQuery(NvLogDiscoveryPluginT *plugin,
						NvLogDiscoveryMsgT *sendMsg,
						NvLogDiscoveryMsgT *recvMsg, int recvMsgLen);

/* calculate timeout for semaphore wait */
static void calculate_timeout(uint32_t timeout_ms, struct timespec *ts);
