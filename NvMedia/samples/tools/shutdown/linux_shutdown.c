/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include "linux_shutdown.h"
#include "tegra_hv_sysmgr.h"

static int hv_pm_ctl_fd;

#define STRING(x) system(#x)
#define ALOGE_CONSOLE(s) STRING(echo s > /dev/console)

static int hv_pm_ctl_do_reboot(bool is_shutdown)
{
	const char cmd_shutdown[] = "shutdown now" ;
	const char cmd_reboot[] = "reboot" ;
	int ret = 0;

	if (is_shutdown) {
		ALOGE_CONSOLE("hv_pm_ctl_do_reboot: Triggering shutdown");
		ret = system(cmd_shutdown);
	} else {
		ALOGE_CONSOLE("hv_pm_ctl_do_reboot: Triggering reboo");
		ret = system(cmd_reboot);
	}

	if(ret < 0) {
		ALOGE_CONSOLE("hv_pm_ctl_do_reboot: Failed to run cmd");
		ALOGE("hv_pm_ctl_do_reboot: Failed to run cmd %s\n",
				is_shutdown ? cmd_shutdown : cmd_reboot);
	}

	return ret;
}

static int hv_pm_ctl_do_suspend(void)
{
	const char cmd_suspend[] = "/bin/echo s2idle > /sys/power/mem_sleep && /bin/echo mem > /sys/power/state";
	int ret;
	FILE *file = fopen (SC7_CONF_FILE, "r");

	if (file != NULL) {
		ret = execute_block(SC7_SUSPEND_BLOCK, file);
		if (ret == -1)
			ALOGE("%s:Failed to execute Suspend Block\n", __func__);
	}
	ret = system(cmd_suspend);
	if(ret < 0) {
		ALOGE("%s: Failed to run cmd %s\n", __func__, cmd_suspend);
	}
	if (file != NULL) {
		ret = execute_block(SC7_RESUME_BLOCK, file);
		if (ret == -1)
			ALOGE("%s: Failed to execute Resume Block\n", __func__);
	}
	return ret;
}

static int hv_pm_ctl_do_resume(void)
{
	return 0;
}

static int hv_pm_ctl_recv_msg(struct hv_sysmgr_message *msg)
{
	int ret;

	ret = read(hv_pm_ctl_fd, msg, sizeof(*msg));
	if (ret == -1) {
		ALOGE("hv_pm_ctl_recv_msg: Failed to read data, %d\n",
				-errno);
		ret = -errno;
	} else if (ret != sizeof(*msg)) {
		ALOGE("hv_pm_ctl_recv_msg: Reading is not completed, "
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
			ALOGE_CONSOLE("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_SHUTDOWN");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			ret = hv_pm_ctl_do_reboot(true);
			break;
		case HV_SYSMGR_CMD_NORMAL_REBOOT:
			ALOGE_CONSOLE("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_REBOOT");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			ret = hv_pm_ctl_do_reboot(false);
			break;
		case HV_SYSMGR_CMD_NORMAL_SUSPEND:
			ALOGE_CONSOLE("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_SUSPEND");
			cmd->resp_id = HV_SYSMGR_RESP_ACCEPTED;
			hv_pm_ctl_send_msg(msg);
			hv_pm_ctl_do_suspend();

			/*
			 *  guest VMs don't get RESUME event from SYSMGR, so send resume
			 *  notification to other app just after waking from suspend.
			 */
			hv_pm_ctl_do_resume();
			break;
		case HV_SYSMGR_CMD_NORMAL_RESUME:
			ALOGE_CONSOLE("hv_pm_ctl_handle_msg: PM_CTL CMD, NORMAL_RESUME");
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
	int ret;

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
		goto error_close;
	}

	return 0;

error_close:
	close(hv_pm_ctl_fd);
	return -1;
}
