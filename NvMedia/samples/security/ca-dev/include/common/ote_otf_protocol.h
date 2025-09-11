/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved
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

#ifndef __OTF_PROTOCOL_IMPL_H
#define __OTF_PROTOCOL_IMPL_H

#define OTF_SET_AES_KEY 0x1U
#define OTF_ERASE_AES_KEY 0x2U
#define OTF_SET_AES_KEY_AT 0x3U
#define OTF_RESET_SECURE_TIMER 0x4U
#define OTF_REPROGRAM_KEYS 0x5U
#define OTF_SET_DSI_PANEL_VPR_POLICY 0x6U
#define OTF_IS_KEY_PROVISIONED 0x7U

/* Unit test interfaces start at 0x100 */
#define OTF_RUN_SESSION_UNIT_TESTS 0x100U

#endif
