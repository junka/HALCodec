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
#include <nvlogtrpt_struct.h>

#define NVLOG_TRPT_MODE_SOURCE 1
#define NVLOG_TRPT_MODE_SINK 2

/* Message payload type definitions */
#define NVLOG_MSG_TYPE_TEXT 1

#define NVLOG_TRPT_PROTOCOL_VERSION 1

#define DECLARE_TRPT_PLUGIN(trptPlugin)  \
	void __register_trpt_##trptPlugin(void);  \
	void __register_trpt_##trptPlugin(void) { \
		NvLogTrptPluginRegister(&trptPlugin); \
	}

#define REGISTER_TRPT_PLUGIN(trptPlugin)  \
	void __register_trpt_##trptPlugin(void);  \
	__register_trpt_##trptPlugin();

int NvLogTrptPluginRegister(NvLogTrptPluginT *trpt);
NvLogTrptPluginT *NvLogTrptPluginFind(char* pluginName);
int NvLogTrptInit(char *pluginName, int mode, NvLogInstanceT *instance);
int NvLogTrptEntityInit(NvLogInstanceT *instance);
int NvLogTrptClientInit(struct trptListHead *trptListHead);
int NvLogTrptClientPluginInit(char *pluginName, int mode, NvLogInstanceT *instance);
int NvLogTrptSendMsg(NvLogInstanceT *instance, NvLogTrptMsgT *msg);
int NvLogTrptRecvMsg(NvLogInstanceT *instance, NvLogTrptMsgT *msg);
int NvLogTrprtAbandonPlugin(NvLogInstanceT *instance);
bool NvLogTrprtIsPluginAbandoned(NvLogInstanceT *instance);
