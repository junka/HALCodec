/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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

#define NVLOG_DISCOVERY_PROTOCOL_VERSION 1

/*
 * Discovery plugin modes
 *
 * Indicates the direction of plugin (upstream/downstream).
 * TODO: replace these different modes with simpler ones to indicate downstream
 *       vs upstream direction.
 */
#define NVLOG_DISCOVERY_MODE_ENTITY_TO_CLIENT 1
#define NVLOG_DISCOVERY_MODE_CLIENT_TO_SERVER 2
#define NVLOG_DISCOVERY_MODE_CLIENT 3
#define NVLOG_DISCOVERY_MODE_SERVER 4

#define NVLOG_DISCOVERY_MSG_TYPE_INIT 1
#define NVLOG_DISCOVERY_MSG_TYPE_NEW_ENTITY 2
#define NVLOG_DISCOVERY_MSG_TYPE_NEW_CLIENT 3
#define NVLOG_DISCOVERY_MSG_TYPE_RESPONSE_SUCCESS 4
#define NVLOG_DISCOVERY_MSG_TYPE_RESPONSE_FAILURE 5

struct NvLogDiscoveryMsg {
	uint16_t length;
	uint32_t discoveryMode;
	uint8_t version; // version of the discovery protocol
	uint8_t msgType;
	int32_t eu_id;
	uint32_t pid;
	uint32_t pid_ext; // entropy to allow pid re-use
	char msgData[0];
};

typedef struct NvLogDiscoveryMsgInit {
	int dummy;	// not used currently
} NvLogDiscoveryMsgInitT;

int NvLogDiscoveryPluginRegister(NvLogDiscoveryPluginT *disc);
NvLogDiscoveryPluginT *NvLogDiscoveryPluginFind(char * pluginName);
#ifdef ENABLE_HVRTOS_ENV
int NvLogDiscoveryInit(char *pluginName,
	int discoveryMode, NvLogAppInstanceT *instance, DiscMsgHandlerT);
#endif
int NvLogDiscoveryQuery(NvLogDiscoveryPluginT *plugin,
	NvLogDiscoveryMsgT *sendMsg, NvLogDiscoveryMsgT *recvMsg, int recvMsgLen);

int NvLogDiscoveryEntityInit(NvLogAppInstanceT *appInstance);
int NvLogDiscoveryClientInit(NvLogAppInstanceT *appInstance,
		DiscMsgHandlerT msgHandler);

#define DECLARE_DISCOVERY_PLUGIN(discoveryPlugin)  \
	void __register_discovery_##discoveryPlugin(void);  \
	void __register_discovery_##discoveryPlugin(void) { \
		NvLogDiscoveryPluginRegister(&discoveryPlugin); \
	}

#define REGISTER_DISCOVERY_PLUGIN(discoveryPlugin) \
	void __register_discovery_##discoveryPlugin(void);  \
	__register_discovery_##discoveryPlugin();
