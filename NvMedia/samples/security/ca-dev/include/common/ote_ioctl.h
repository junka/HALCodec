/*
 * Copyright (c) 2013-2016, NVIDIA CORPORATION. All rights reserved
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

/**
 * @file
 * <b> NVIDIA Trusted Little Kernel Interface: IOCTL Values</b>
 *
 * @b Description: Declares TLK IOCTL values.
 */

/**
 * @defgroup tlk_common_ioctl IOCTL
 *
 * Defines Trusted Little Kernel (TLK) IOCTL numbers for use by
 * Trusted Application libraries.
 *
 * @ingroup ote_common
 *
 * @{
 */

#ifndef __OTE_IOCTL_H
#define __OTE_IOCTL_H

/**
 * Defines IOCTL syscall interface parameters.
 */
enum {
	OTE_IOCTL_GET_MAP_MEM_ADDR	= 1UL,
	OTE_IOCTL_V_TO_P		= 2UL,
	OTE_IOCTL_CACHE_MAINT		= 3UL,
	OTE_IOCTL_TA_TO_TA_REQUEST	= 4UL,
	OTE_IOCTL_GET_PROPERTY		= 5UL,
	OTE_IOCTL_GET_DEVICE_ID		= 6UL,
	OTE_IOCTL_GET_TIME_US		= 7UL,
	OTE_IOCTL_GET_RAND32		= 8UL,
	OTE_IOCTL_SS_REQUEST		= 9UL,
	OTE_IOCTL_TASK_REQUEST		= 10UL,
	OTE_IOCTL_SS_GET_CONFIG		= 11UL,
	OTE_IOCTL_REGISTER_TA_EVENT	= 12UL,
	OTE_IOCTL_TASK_PANIC		= 13UL,
	OTE_IOCTL_GET_ROOT_OF_TRUST	= 14UL,
	OTE_IOCTL_ROLLBACK_CHECK	= 15UL,
	OTE_IOCTL_MAP_EKS_TO_USER	= 16UL,
};

/** @} */
#endif
