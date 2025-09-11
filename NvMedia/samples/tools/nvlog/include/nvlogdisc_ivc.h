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

/* Default timeout for SIVC mutex 1mS */
#define SIVC_MUTEX_TIMEOUT_US (1000)

/* connect/reply events are set but not used currently */
typedef struct NvLogDiscoveryPlugin_sivc_Ctxt {
	struct os_event sivc_reply_evt;
	struct os_event sivc_connect_evt;
	struct sivc_queue *sivcq_p;
	struct RtosCWrapMutex *sivc_mutex;
	bool sivc_connected;
	uint32_t eu_id;
	uint32_t pid;
	uint32_t pid_ext;
} NvLogDiscoveryPlugin_sivc_CtxtT;

extern NvLogDiscoveryPluginT logDiscoveryPluginMsgQueue;
void * DiscoveryClientThread(void *param);
int SendClientToEntityMsg(NvLogDiscoveryPluginT *plugin, uint32_t pid, NvLogDiscoveryMsgT *msg);
