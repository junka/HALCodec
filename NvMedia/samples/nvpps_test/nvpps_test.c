/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
/*#include <linux/time64.h>*/
#include <nvpps_ioctl.h>

static int			fd = -1;
static int			shutdown = 0;
static int			use_signal = 0;
static int			ioctl_testcase;
static char			*dev_name;
static const char		short_options[] = "d:tscT:";
static const struct option 	long_options[] = {
        { "device",	required_argument, 	NULL, 'd' },
        { "timer",  	no_argument, 		NULL, 't' },
	{ "signal", 	no_argument, 		NULL, 's' },
	{ "counter",	no_argument, 		NULL, 'c' },
	{ "test",	required_argument, 		NULL, 'T' },
        { 0, 0, 0, 0 }
};


static void usage(FILE *fp, int argc, char **argv)
{
	fprintf(fp,
		"Usage: %s [options]\n\n"
		"Options:\n"
		"-d | --device name   NVPPS device name [%s]\n"
		"-t | --timer         use timer mode\n"
		"-s | --signal        use signal\n"
		"-c | --counter       tsc in counter mode instead of nsec\n"
		"-T | --test name     Where name is \"gettimestamp\" or \"getevent\" to run the corresponding test\n"
		"",
		argv[0], dev_name);
}


static void signal_hdlr(int sig)
{
	shutdown = 1;
}



static __s64 tsc_2_ptp_delta(__s64 tsc, __s64 ptp)
{
	static __s64	tsc_prev = 0;
	static __s64	ptp_prev = 0;
	__s64		ptp_delta, tsc_delta;

	ptp_delta = ptp - ptp_prev;
	tsc_delta = tsc - tsc_prev;
	/* remember the previous value */
	tsc_prev = tsc;
	ptp_prev = ptp;
	return tsc_delta - ptp_delta;
}


static void signal_io(int sig)
{
	struct nvpps_timeevent	ts;

	/* get the timestamps */
	if (fd >= 0) {
		if (ioctl(fd, NVPPS_GETEVENT, &ts) != 0) {
			fprintf(stderr, "SIGIO : ioctl failed for NVPPS_GETEVENT err %s\n", strerror(errno));
		} else {
			fprintf(stdout, "S: evt, %d, tsc, %llu, phc, %llu, delta, %lld, latency, %llu, tsc_res_ns, %llu, evt_mode, %d, tsc_mode, %d\n", ts.evt_nb, ts.tsc, ts.ptp, tsc_2_ptp_delta((__s64)ts.tsc, (__s64)ts.ptp), ts.irq_latency, ts.tsc_res_ns, ts.evt_mode, ts.tsc_mode);
		}
	}
}


int main(int argc, char **argv)
{
	struct nvpps_params	params = {NVPPS_MODE_GPIO, NVPPS_TSC_NSEC};
	struct nvpps_version	version;
	struct nvpps_timestamp_struct tstamp;

	dev_name = "/dev/nvpps0";
	ioctl_testcase = 0;

	printf("nvpps test ...\n");

	/* get option */
	for (;;) {
		int idx;
		int c;

		c = getopt_long(argc, argv,
				short_options, long_options, &idx);

		if (-1 == c) {
			break;
		}

		switch (c) {
			case 0: /* getopt_long() flag */
				break;

			case 'd':
				dev_name = optarg;
				break;

			case 't':
				params.evt_mode = NVPPS_MODE_TIMER;
				break;

			case 's':
				use_signal = 1;
				break;

			case 'c':
				params.tsc_mode = NVPPS_TSC_COUNTER;
				break;

			case 'T':
				if (strcmp(optarg, "getevent") == 0) {
					ioctl_testcase = 0;
				}
				else if (strcmp(optarg, "gettimestamp") == 0) {
					ioctl_testcase = 1;
				}
				else {
					usage(stderr, argc, argv);
					exit(EXIT_FAILURE);
				}
				break;

			default:
				usage(stderr, argc, argv);
				exit(EXIT_FAILURE);
		}
	}

	/* open the device */
	fd = open(dev_name, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "failed to open the device %s errno = %d", dev_name, errno);
		return errno;
	}

	/* get the version */
	if (ioctl(fd, NVPPS_GETVERSION, &version) != 0) {
		fprintf(stderr, "ioctl failed for NVPPS_GETVERSION err %s\n", strerror(errno));
		return errno;
	}

	fprintf(stdout, "version(%d:%d) api(%d:%d) expected version(%d:%d) api(%d:%d)\n", version.version.major, version.version.minor, version.api.major, version.api.minor, NVPPS_VERSION_MAJOR, NVPPS_VERSION_MINOR, NVPPS_API_MAJOR, NVPPS_API_MINOR);

	/* set the mode */
	if (ioctl(fd, NVPPS_SETPARAMS, &params) != 0) {
		fprintf(stderr, "ioctl failed for NVPPS_SETPARAMS err %s\n", strerror(errno));
	}

	fprintf(stdout, "evt_mode(%d) tsc_mode(%d)\n", params.evt_mode, params.tsc_mode);

	/* set the signal handler */
	signal(SIGINT, signal_hdlr);
	signal(SIGTERM, signal_hdlr);
	if (use_signal) {
		int	oflags;

		signal(SIGIO, signal_io);

		fcntl(fd, F_SETOWN, getpid());
		oflags = fcntl(fd, F_GETFL);
		fcntl(fd, F_SETFL, oflags | FASYNC);
	}


	if (ioctl_testcase == 1) {
		int k = 0;

		fprintf(stdout, "Run NVPPS_GETTIMESTAMP Test for 10 iterations\n");
		while (k < 10) {
			usleep(50000 * k); /* increment 50ms on each iteration */
			if (ioctl(fd, NVPPS_GETTIMESTAMP, &tstamp) != 0) {
				fprintf(stderr, "ioctl failed for NVPPS_GETTIMESTAMP err %s\n", strerror(errno));
				exit(EXIT_FAILURE);
			} else {
				fprintf(stdout, "Itr: %d clk_id %d; tsc sec %lld, nsec %ld; ptp sec %lld, nsec %ld\n",
						k, tstamp.clockid, tstamp.kernel_ts.tv_sec, tstamp.kernel_ts.tv_nsec, tstamp.hw_ptp_ts.tv_sec, tstamp.hw_ptp_ts.tv_nsec);
				/* Toggle clockid b/w Kernel CLOCK_MONOTONIC and CLOCK_REALTIME to test fetching corresponding timestamps  */
				tstamp.clockid = tstamp.clockid ? 0 : 1 ;
			}
			k++;
		}
		return 0;
	}

	/* wait for the PPS event */
	fprintf(stdout, "Run NVPPS_GETEVENT Test continuously\n");
	while (!shutdown) {
		fd_set			fds;
		struct timeval		tv;
		int 			r;
		struct nvpps_timeevent	ts;

		if (use_signal) {
			usleep(200 * 1000);
		} else {
			FD_ZERO(&fds);
			FD_SET(fd, &fds);

			/* timeout */
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			/* wait for the event */
			r = select(fd + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				if (EINTR == errno) {
					continue;
				} else {
					fprintf(stderr, "select failed err %s\n", strerror(errno));
				}
			}

			if (0 == r) {
				fprintf(stderr, "select timeout\n");
			}

			/* get the timestamps */
			if (ioctl(fd, NVPPS_GETEVENT, &ts) != 0) {
				fprintf(stderr, "ioctl failed for NVPPS_GETEVENT err %s\n", strerror(errno));
			} else {
				fprintf(stdout, "evt, %d, tsc, %llu, phc, %llu, sec_phc %llu, delta, %lld, latency %llu, ptp_offset %lld, tsc_res_ns, %llu, evt_mode, %d, tsc_mode, %d\n", ts.evt_nb, ts.tsc, ts.ptp, ts.secondary_ptp, tsc_2_ptp_delta((__s64)ts.tsc, (__s64)ts.ptp), ts.irq_latency, (__s64)(ts.secondary_ptp - ts.ptp), ts.tsc_res_ns, ts.evt_mode, ts.tsc_mode);
			}
		}
	}

	/* close the driver */
	close(fd);

	return 0;
}
