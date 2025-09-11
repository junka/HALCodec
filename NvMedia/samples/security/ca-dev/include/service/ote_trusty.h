/*
 * Copyright (c) 2016-2020, NVIDIA CORPORATION. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/* Get time in micro seconds */
long trusty_gettime_us(uint64_t *time);

/* Get random number */
long trusty_random(void);

/*
 * Sends the request to BPMP and wait for the operation to complete
 * @mrq_id: MRQ ID of the operation
 * @mod_id: ID of the module as per BPMP ABI
 * @cmd: cmd to be sent to BPMP corresponding to MRQ
 * @payload(optional): payload is used to transfer a parameter in
 * both request and response messages based on command. Currently
 * only integer payload is supported. Can be null if not required.
 */
int32_t trusty_send_bpmp_msg(uint32_t mrq_id, uint32_t mod_id, uint32_t cmd,
		uint32_t *payload);
