#include "linux_shutdown.h"

int parse_line(char *line, char **argv);
int restart_task(char **argv);
char *read_line(char *s, int size, FILE *stream);
int enumerate_cmds(const char *line);
int sc7_sleep(unsigned long msec);
int execute_end_of_block(unsigned long *wait, int *done, const char* block);
int send_signal(unsigned long int *max_wait, int nargs, char **argv);

int parse_line(char *line, char **argv)
{
	int nargs = 0;

	while (*line != '\0') {
		while (*line == '\t' || *line == '\n' || *line == ' ')
			*line++ = '\0';
		*argv++ = line;
		nargs++;
		while (*line != '\0' && *line != ' ' &&
				*line != '\t' && *line != '\n')
			line++;
	}
	*argv = '\0';

	return nargs;
}

int restart_task(char **argv)
{
	pid_t  pid;

	if ((pid = fork()) < 0) {
		ALOGD("Forking a child process failed\n");
		return -1;
	} else if (pid == 0) {
		if (execvp(*argv, argv) < 0) {
			ALOGD("Failed to execute a command \n");
			return -1;
		}
	}

	return 0;
}

char *read_line(char *s, int size, FILE *stream)
{
	char *res = NULL;

	do {
		res = fgets(s, size, stream);
	} while (res != NULL && *s == '#');

	return res;
}

int enumerate_cmds(const char *line)
{
	if (strncmp("END", line, 3) == 0)
		return SC7_BLOCK_END;

	if (strncmp("send_signal", line, sizeof("send_signal") - 1) == 0)
		return SC7_SEND_SIGNAL;

	if (strncmp("restart_task", line, sizeof("restart_task") - 1) == 0)
		return SC7_RESTART_TASK;

	return -1;
}

int sc7_sleep(unsigned long msec)
{
	struct timespec ts;
	int res;

	ALOGD("sleep for %ld\n", msec);
	if (msec < 0)
	{
		errno = EINVAL;
		return -1;
	}

	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000;

	do {
		res = nanosleep(&ts, &ts);
	} while (res && errno == EINTR);

	return res;
}

int execute_end_of_block(unsigned long *wait, int *done, const char* block)
{

	ALOGD("Done executing %s commands\n", block);
	if (*wait > 0)
		sc7_sleep(*wait);
	*wait = 0;
	*done = 1;

	return 0;
}

int send_signal(unsigned long int *max_wait, int nargs, char **argv)
{
	char line[1024];
	unsigned long time_to_wait = 0;
	int i;

	ALOGD("send signal command\n");
	if (nargs <= 4) {
		ALOGD("Bad parameters for send signal\n");
		return -1;
	}
	time_to_wait = atoi(argv[1]);
	if (time_to_wait > *max_wait)
		*max_wait = time_to_wait;

	i = 2;
	strcpy(line, "pkill -f -");

	while (argv[i] != NULL) {
		strcat(line, argv[i]);
		strcat(line, " ");
		i++;

	}

	if (system(line) != -1)
		ALOGD("success executing the command %s\n", line);
	else
		ALOGD(" failed to execute the command %s\n", line);


	return 0;
}

int execute_block(const char* block, FILE *file)
{
	char line [1024];
	char line2[1024];
	char *argv[64];
	int done = 0;
	int cmd;
	int nargs = 0;
	int ret = 0;
	unsigned long max_wait_time = 0;

	if (file == NULL)
		return -1;

	while (done == 0 && read_line(line, sizeof(line), file) != NULL) {
		if (strncmp(block, line, sizeof(block) - 1) == 0) {
			ALOGD("Start executing %s block\n", block);
			if (read_line(line, sizeof(line), file) == NULL)
				continue;

			if (strncmp("BEGIN", line, 5) == 0) {

				while(done == 0 &&
				read_line(line, sizeof(line), file) != NULL) {
					strcpy(line2, line);
					nargs = parse_line(line2, argv);
					cmd = enumerate_cmds(line);

					switch(cmd) {
						case SC7_BLOCK_END:
							execute_end_of_block(
							&max_wait_time,
							&done, block);

							break;

						case SC7_SEND_SIGNAL:
							send_signal(
								&max_wait_time,
								nargs, argv
								   );
							break;

						case SC7_RESTART_TASK:
							ALOGD("restart task\n");
							restart_task(&argv[1]);
							break;

						default:
							ALOGD("unknown command\n");
							done = 1;
							break;
					}
				}
			}
		}
	}

	return ret;
}
