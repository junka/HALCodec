/*
 * Copyright (c) 2022-2023, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */
#ifndef _LINUX_SHUTDOWN_H
#define _LINUX_SHUTDOWN_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#ifdef DEBUG
#define ALOGD(fmt, arg...) printf(fmt, ##arg)
#else
#define ALOGD(fmt, arg...)
#endif

#define ALOGE(fmt, arg...) printf(fmt, ##arg)

#define HV_PM_CTL_PATH		"/dev/tegra_hv_pm_ctl"
#define SC7_CONF_FILE 		"/etc/shutdown/sc7.conf"
#define SC7_SUSPEND_BLOCK 	"Suspend Block"
#define SC7_RESUME_BLOCK	"Resume Block"

enum {
	SC7_BLOCK_END = 1,
	SC7_SEND_SIGNAL,
	SC7_RESTART_TASK,
};

int execute_block(const char* block, FILE *file);
#endif
