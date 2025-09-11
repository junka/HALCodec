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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdarg.h>

/* NVLOG API error values */
#define NVLOG_SUCCESS 0
#define NVLOG_ERROR -1
#define NVLOG_RETRY -2
#define NVLOG_TIMEOUT -3
//TBD: define detailed error codes

/* Message priority definitions */
#define NVLOG_INFO      0
#define NVLOG_WARN      1
#define NVLOG_ERR       2

#define NVLOG_PRIORITY_DEFAULT  NVLOG_WARN

/*
 * Description for NvLogInit()
 *   - Applications need to call this API (on a per-process basis) at early
 *     stages before using any other nvlog APIs or spawning threads that might
 *     use nvlog APIs.
 *   - Initialize library and perform necessary handshakes to talk to nvlog
 *     backend services.
 * Parameters
 *   - name of the discovery plugin to be used
 * Return values
 *   - NVLOG_SUCCESS for success
 *   - NVLOG_ERROR for failure
 */
int NvLogInit(char *discoveryPluginName);

/*
 * Description for NvLogPriorityPrintf()
 *   - Log a message at specified priority level.
 * Parameters
 *   - Param `priority` can be any of NVLOG_INFO, NVLOG_WARN, NVLOG_ERR levels
 *   - Param `format` is printf style format string
 *   - Param `...` additional parameters based on format string
 * Return values
 *   - NVLOG_ERROR for failure
 *   - >0 indicating number of characters printed (including the null byte used
 *     to end output to strings).
 */
int NvLogPriorityPrintf(uint8_t priority, const char* format, ...);


/*
 * Description for NvLogvPriorityPrintf()
 *   - va_list arg equivalent of NvLogPriorityPrintf()
 */
int NvLogvPriorityPrintf(uint8_t priority, const char* format, va_list args);

/*
 * Description for NvLogPrintf()
 *   - Log a message at default priority level (NVLOG_WARN).
 * Parameters
 *   - Param `format` is printf style format string
 *   - Param `...` additional parameters based on format string
 * Return values
 *   - NVLOG_ERROR for failure
 *   - >0 indicating number of characters printed (including the null byte used
 *     to end output to strings).
 */
int NvLogPrintf(const char* format, ...);

/*
 * Description for NvLogvPrintf()
 *   - va_list arg equivalent of NvLogPrintf()
 */
int NvLogvPrintf(const char* format, va_list args);

int NvLogOs_fprintf(FILE *stream, const char *format, ...);
#ifdef __cplusplus
}
#endif
