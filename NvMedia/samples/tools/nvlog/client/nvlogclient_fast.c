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

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <nvlog_lib.h>
#include <nvlogtrpt.h>
#include <nvlogdiscovery.h>
#include <sys/queue.h>

#define NVLOG_OUTPUT_FILE_PATH_MAX_LEN	NVLOG_DT_PATH_MAX_LEN
#define NVLOG_KB_TO_BYTES		1024U

int discoveryMsgHandler(NvLogDiscoveryMsgT *msg);
void sig_term_handler(int signum, siginfo_t *info, void *ptr);

FILE *output;
volatile bool terminate;

uint64_t file_size_counter;
uint64_t output_file_size;

void sig_term_handler(int signum, siginfo_t *info, void *ptr)
{
	NvLogOs_fprintf(stdout, "%s - received SIGTERM\n", __FILE__);
	fflush(output);
	terminate = 1;
}

int discoveryMsgHandler(NvLogDiscoveryMsgT *msg)
{
	int ret;
	if(!msg)
		goto error;

	if(msg->msgType == NVLOG_DISCOVERY_MSG_TYPE_NEW_ENTITY) {
		NvLogDiscoveryMsgNewEntityT *newEntityMsg = (NvLogDiscoveryMsgNewEntityT *)msg->msgData;
		NvLogInstanceT *instance = NvLogCreateClientInstance(msg->eu_id,
						msg->pid, msg->pid_ext, newEntityMsg);
		if (!instance) {
			NvLogOs_fprintf(stderr, "NvLogOsCreateClientInstance failed\n");
			goto error;
		}

		ret = NvLogTrptClientPluginInit(newEntityMsg->trpt_attr.transport,
				NVLOG_TRPT_MODE_SINK, instance);
		if (ret != NVLOG_SUCCESS) {
			NvLogOs_fprintf(stderr, "NvLogTrptInit failed (error = %d)\n", ret);
			goto error;
		}

		instance->initialized = true;
		return NVLOG_SUCCESS;
	}

error:
	return NVLOG_ERROR;
}

static int PrintHeader(void)
{
	int ret;

	ret = fprintf(output,"%10s%10s%10s%15s%15s\t%s\n", "EUID",
			"PID", "ENTITY ID", "SEQ",
			"TIMESTAMP", "MSG");
	if (ret < 0) {
		NvLogOs_fprintf(NULL, "%s - fprintf failed:, errno:%d\n", __func__, errno);
		goto exit;
	}

	file_size_counter += ret;

exit:
	return ret;
}

void PrintLogs(NvLogInstanceT *instance)
{
	char msgBuffer[NVLOG_MAX_MSG_LEN];
	int retval;
	NvLogTrptMsgT *msg = (NvLogTrptMsgT *)msgBuffer;

	if (NvLogTrprtIsPluginAbandoned(instance) == true)
		return;

	/* in real case might need to pause after x number of reads */
	while (1) {
		if((retval = NvLogTrptRecvMsg(instance, msg)) != NVLOG_SUCCESS) {

			if (retval != NVLOG_RETRY) {
				NvLogOs_fprintf(stderr, "NvLogTrptRecvMsg failed\n");

				retval = NvLogTrprtAbandonPlugin(instance);

				if (retval != NVLOG_SUCCESS) {
					NvLogOs_fprintf(stderr, "Failed to abandon transport plugin: %s\n",
							instance->trptPlugin->pluginName);
				} else {
					NvLogOs_fprintf(stderr, "Abandoned transport plugin: %s\n", instance->trptPlugin->pluginName);
				}
			}
			return;
		}

		retval = fprintf(output,"%10u%10u%10u%15d%15ld\t%s", msg->eu_id,
				msg->pid, msg->entity_id, msg->sequence,
				msg->timestamp, msg->logMsg);
		if (retval < 0) {
			NvLogOs_fprintf(NULL, "%s - fprintf failed: errno:%d\n",
					__func__, errno);
			NvLogOs_fprintf(NULL, "%s - nvlogclient_fast is exiting due to log file write error\n",
					__func__);
			exit(-1);
		}

		file_size_counter += retval;
		if (file_size_counter > output_file_size) {
			fseek(output, 0L, SEEK_SET);
			file_size_counter = 0;
			if (PrintHeader() < 0)
				return;
		}
	}
}

int main(int argc, char *argv[])
{
	static struct sigaction _sigact;
	terminate = 0;
	char output_file[NVLOG_OUTPUT_FILE_PATH_MAX_LEN];
	uint32_t poll_wait = 0U;
	uint32_t file_size_kb = 0U;
	int ret;
	char dtPath[NVLOG_DT_PATH_MAX_LEN];

	file_size_counter = 0;
	memset(&_sigact, 0, sizeof(_sigact));
	_sigact.sa_sigaction = sig_term_handler;
	_sigact.sa_flags = SA_SIGINFO;

	ret = sigaction(SIGTERM, &_sigact, NULL);
	if (ret != 0) {
		ret = errno;
		NvLogOs_fprintf(NULL, "%s - sigaction failed:, errno:%d\n", __func__, errno);
		goto error;
	}

	ret = NvLogClientInit(NULL, NULL, &discoveryMsgHandler);
	if (ret != NVLOG_SUCCESS) {
		NvLogOs_fprintf(stderr, "%s - NvLogClientInit failed (error : %d)\n",
						__func__, ret);
		goto error;
	}

	ret = NvLogOsGetCompPath(NVLOG_COMP_NAME_NVLOGCLIENT,
			dtPath, NVLOG_DT_PATH_MAX_LEN);
	if (ret) {
		NvLogOs_fprintf(stderr, "%s - NvLogClientInit failed to get DT path %d\n",
			__func__, ret);
		goto error;
	}

	if (argc > 1) {
		ret = NvLogOsStrnCpy(output_file, argv[1], NVLOG_OUTPUT_FILE_PATH_MAX_LEN);
		if (ret) {
			NvLogOs_fprintf(stderr,
				"%s - NvLogClientInit failed to copy output file DT prop : %d\n",
				__func__, ret);
			goto error;
		}
	}
	else {
		ret = NvLogOsGetDeviceTreePropStr(dtPath, "output_file", output_file,
				NVLOG_OUTPUT_FILE_PATH_MAX_LEN);
		if (ret) {
			NvLogOs_fprintf(stderr,
				"%s - NvLogClientInit failed to read output file DT prop : %d\n",
				__func__, ret);
			goto error;
		}
	}

	ret = NvLogOsGetDeviceTreePropU32(dtPath, "output_file_size_kb", &file_size_kb);
	if (ret) {
		NvLogOs_fprintf(stderr,
			"%s - NvLogClientInit failed to read output file size DT prop : %d\n",
			__func__, ret);
		goto error;
	}

	output_file_size = (uint64_t)file_size_kb * NVLOG_KB_TO_BYTES;

	output = fopen(output_file, "w");
	if (!output) {
		ret = errno;
		NvLogOs_fprintf(NULL, "%s - fopen failed:, errno:%d\n", __func__, errno);
		goto error;
	}

	/* print header */
	ret = PrintHeader();
	if (ret < 0)
		goto error;

	ret = NvLogOsGetDeviceTreePropU32(dtPath, "poll_wait", &poll_wait);
	if (ret) {
		NvLogOs_fprintf(stderr,
			"%s - NvLogClientInit failed to read poll wait DT prop : %d\n",
			__func__, ret);
		goto error;
	}

	while(!terminate) {
		NvLogOsIterateAllInstance(&PrintLogs);
		/*
		 * Small pauses to avoid busy loop during periods of (logging)
		 * inactivity.
		 * There is no single good value for wait. Waiting too long
		 * can miss messages, waiting too short can drain power.
		 */
		usleep(poll_wait);
	}

	return 0;

error:
	return ret;
}
