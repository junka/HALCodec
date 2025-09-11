/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/reboot.h>

#include "tegra_hv_sysmgr.h"

#ifdef DEBUG
#define ALOGD(fmt, arg...) printf(fmt, ##arg)
#else
#define ALOGD(fmt, arg...)
#endif

#define ALOGE(fmt, arg...) printf(fmt, ##arg)

#define HV_PM_CTL_PATH		"/dev/tegra_hv_pm_ctl"
#define SYS_POWER_PATH		"/sys/power/state"
#define POWER_STRING		"mem\n"

static int hv_pm_ctl_fd;
static void sigterm(int signo);

void sigterm(int signo)
{
        ALOGD("hv_pm_ctl: recved SIGTERM\n");
}

static int hv_pm_ctl_do_reboot(bool is_shutdown)
{
	ALOGD("sending SIGTERM\n");
	kill(-1, SIGTERM);
	sleep(2);
	ALOGD("sending SIGKILL\n");
	kill(-1, SIGKILL);

	ALOGD("sync\n");
	sync();

	if (is_shutdown) {
		ALOGD("shutdown\n");
		reboot(RB_POWER_OFF);
	} else {
		ALOGD("reboot\n");
		reboot(RB_AUTOBOOT);
	}

	return 0;
}

static int hv_pm_ctl_do_suspend(void)
{
	int ret = 0;
	int power_fd = -1;

	ret = system("/etc/init.d/ebp_suspend.sh pre");
	if (ret < 0) {
		ALOGE("%s: suspend PRE notification failed\n", __func__);
	}

	ret = open(SYS_POWER_PATH, O_RDWR);
	if (ret < 0) {
		ALOGE("%s: Failed to open %s, %d\n", __func__,
				SYS_POWER_PATH, -errno);
		return ret;
	}
	power_fd = ret;

	ret = write(power_fd, POWER_STRING, sizeof(POWER_STRING));
	if (ret == -1) {
		ALOGE("%s: Failed to STR, %d\n", __func__, -errno);
		ret = -errno;
	}

	close(power_fd);
	return ret;
}

static int hv_pm_ctl_do_resume(void)
{
	int ret;

	ret = system("/etc/init.d/ebp_suspend.sh post");
	if (ret < 0) {
		ALOGE("%s: suspend POST notification failed\n", __func__);
	}

	return 0;
}

static int hv_pm_ctl_recv_msg(struct hv_sysmgr_message *msg)
{
	int ret;

	ret = read(hv_pm_ctl_fd, msg, sizeof(*msg));
	if (ret == -1) {
		ALOGE("hv_pm_ctl_send_msg: Failed to read data, %d\n",
				-errno);
		ret = -errno;
	} else if (ret != sizeof(*msg)) {
		ALOGE("hv_pm_ctl_send_msg: Reading is not completed, "
				"read size %d, expected size %d\n",
				ret, (int)sizeof(*msg));
		ret = -EIO;
	}

	return 0;
}

static int hv_pm_ctl_send_msg(struct hv_sysmgr_message *msg)
{
	int ret;

	ret = write(hv_pm_ctl_fd, msg, sizeof(*msg));
	if (ret == -1) {
		ALOGE("hv_pm_ctl_send_msg: Failed to write data, %d\n",
				-errno);
		ret = -errno;
	} else if (ret != sizeof(*msg)) {
		ALOGE("hv_pm_ctl_send_msg: Writing is not completed, "
				"written size %d, expected size %d\n",
				ret, (int)sizeof(*msg));
		ret = -EIO;
	}

	return 0;
}

static int hv_pm_ctl_handle_msg(struct hv_sysmgr_message *msg)
{
	struct hv_sysmgr_command *cmd =
		(struct hv_sysmgr_command *)&msg->client_data[0];
	int ret = 0;

	if (msg->msg_type != HV_SYSMGR_MSG_TYPE_VM_PM_CTL_CMD) {
		ALOGE("hv_pm_ctl_handle_msg: Message type is not VM_PM_CTL_CMD\n");
		return -EINVAL;
	}

	switch (cmd->cmd_id) {
		case HV_SYSMGR_CMD_NORMAL_SHUTDOWN:
			ALOGD("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_SHUTDOWN\n");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			ret = hv_pm_ctl_do_reboot(true);
			break;
		case HV_SYSMGR_CMD_NORMAL_REBOOT:
			ALOGD("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_REBOOT\n");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			ret = hv_pm_ctl_do_reboot(false);
			break;
		case HV_SYSMGR_CMD_NORMAL_SUSPEND:
			ALOGD("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_SUSPEND\n");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			hv_pm_ctl_do_suspend();

                        /*
                         *  guest VMs don't get RESUME event for SYSMGR, so send resume
                         *  notification to other app just after waking from suspend.
                         */
                        hv_pm_ctl_do_resume();
			break;
		case HV_SYSMGR_CMD_NORMAL_RESUME:
			ALOGD("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_RESUME\n");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			break;
		default:
			ALOGE("hv_pm_ctl_handle_msg: Unsupported PM_CTL CMD, 0x%x\n",
					cmd->cmd_id);
			cmd->resp_id = HV_SYSMGR_RESP_UNKNOWN_COMMAND;
			hv_pm_ctl_send_msg(msg);
			ret = -EINVAL;
	}

	return ret;
}

static int hv_pm_ctl_loop(void *arg)
{
	struct pollfd fds;
	struct hv_sysmgr_message msg;
	unsigned int read_mask = POLLIN | POLLPRI;
	unsigned int error_mask = POLLHUP;
	int ret = 0;

	fds.fd = hv_pm_ctl_fd;
	fds.events = read_mask;

	while (1) {
		ret = poll(&fds, 1, -1);
		if (ret < 0) {
			ALOGE("hv_pm_ctl_thread: Failed to poll %s, %d\n",
					HV_PM_CTL_PATH, ret);
			break;
		} else {
			if (fds.revents & error_mask) {
				ALOGE("hv_pm_ctl_thread: Error occurred on poll %s, 0x%x\n",
						HV_PM_CTL_PATH, fds.revents);
				continue;
			} else if (fds.revents & read_mask) {
				ret = hv_pm_ctl_recv_msg(&msg);
				if (ret < 0) {
					ALOGE("hv_pm_ctl_thread: Failed to receive msg, %d\n",
							ret);
					continue;
				}

				ret = hv_pm_ctl_handle_msg(&msg);
				if (ret < 0) {
					ALOGE("hv_pm_ctl_thread: Failed to handle msg, %d\n",
							ret);
					continue;
				}
			}
		}
	}

	return -1;
}

int main(void)
{
	int ret = 0;

        signal(SIGTERM, sigterm);

	ret = daemon(0, 0);
	if (ret < 0) {
		ALOGE("hv_pm_ctl_init: daemonization failed\n");
		return ret;
	}

	ret = open(HV_PM_CTL_PATH, O_RDWR);
	if (ret < 0) {
		ALOGE("hv_pm_ctl_init: Failed to open %s, %d\n",
				HV_PM_CTL_PATH, -errno);
		return ret;
	}
	hv_pm_ctl_fd = ret;

	ret = hv_pm_ctl_loop(NULL);
	if (ret) {
		ALOGE("hv_pm_ctl_init: hv_pm_ctl_loop failed, %d\n", ret);
		goto error;
	}

	return 0;

error:
	close(hv_pm_ctl_fd);
	return -1;
}
