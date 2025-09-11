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
#include <unistd.h>
#include <nvlog.h>
#include <stdlib.h>

#define MS_TO_US (1000)
#define SLEEP_MS (100)

int main(int argc, char *argv[])
{
	int count=0;
	char *input = NULL;
	size_t size, ret;
	long verbose_flag = 0;

	if (argc > 2) {
		NvLogOs_fprintf(stderr, "%s - Incorrect usage\n", argv[0]);
		NvLogOs_fprintf(stderr, "Usage : %s <verbose flag>\n", argv[0]);
		exit(-1);
	}

	if (argc > 1) {
		verbose_flag = strtol(argv[1], NULL, 0);
	}

	ret = NvLogInit("DISCSHM");
	if (ret != NVLOG_SUCCESS) {
		NvLogOs_fprintf(stderr, "%s - NvLogInit failed (error = %lu)\n", __FILE__, ret);
		return -1;
	}

	while(1) {
		size = 0;
		ret = getline(&input, &size, stdin);
		if ((int32_t)ret != -1) {
			++count;
			if (verbose_flag == 1)
				NvLogOs_fprintf(stdout, "sending seq %d - len %ld - %s", count,
						ret, input);
			NvLogPrintf("%s", input);
			free(input);
			input = NULL;
			/* Give pause of 100mS every 30 messages */
			if (!(count%30)) {
				usleep(SLEEP_MS * MS_TO_US);
			}
		} else {
			break;
		}
	}

	return 0;
}
