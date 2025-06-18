#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "util_msg.h"

static pid_t
read_pid(int fd)
{
	char buf[BUFSIZ];
	ssize_t r;
	char *nl;
	uintmax_t v;

	r = read(fd, buf, sizeof(buf));
	if (r < 0)
		return -1;
	if (r >= sizeof(buf))
		return -1;
	buf[r] = '\0';
	nl = strchr(buf, '\n');
	if (nl)
		*nl = '\0';

	if (sscanf(buf, "%ju", &v) != 1)
		return -1;

	return (pid_t)v;
}

static int
write_pid(int fd, pid_t pid)
{
	char buf[BUFSIZ];
	int len;
	ssize_t r;

	if (pid < 0)
		return -1;

	len = snprintf(buf, sizeof(buf), "%ju\n", (uintmax_t)pid);
	if (len >= sizeof(buf))
		return -1;

	r = write(fd, buf, len);
	if (r != len)
		return -1;

	return 0;
}

static pid_t
read_pid_file(const char *pid_file)
{
	pid_t pid;
	int fd;

	fd = open(pid_file, O_RDONLY|O_CLOEXEC);
	if (fd < 0) {
		p_err("Cannot open pid file %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	if (flock(fd, LOCK_SH) < 0) {
		p_err("Cannot lock pid_file: %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	pid = read_pid(fd);
	if (pid <= 0) {
		p_err("Invalid PID %ju.\n", (intmax_t)pid);
		close(fd);
		return -1;
	}

	close(fd);

	return pid;
}

static int
create_pid_file(const char *pid_file)
{
	pid_t pid;
	int fd;

	fd = open(pid_file, O_RDWR|O_CREAT|O_CLOEXEC, 0644);
	if (fd < 0) {
		p_err("Cannot open pid file %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	if (flock(fd, LOCK_SH) < 0) {
		p_err("Cannot lock pid_file: %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	pid = read_pid(fd);
	if (pid > 0 && kill(pid, 0) == 0) {
		p_err("Another process running.\n");
		close(fd);
		return -1;
	}

	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		p_err("Another process running.\n");
		close(fd);
		return -1;
	}

	if (ftruncate(fd, 0) < 0) {
		p_err("Cannot truncate pid file %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		p_err("Cannot seek pid_file: %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	pid = getpid();
	if (write_pid(fd, pid) < 0) {
		p_err("Cannot write pid file %s: %s\n",
		    pid_file, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;

}

int
create_daemon(const char *pid_file)
{
	pid_t pid;
	int fd;

	pid = fork();
	if (pid < 0) {
		p_err("fork() failed: %s\n", strerror(errno));
		return -1;
	}
	if (pid > 0) {
		exit(1);
	}

	if (create_pid_file(pid_file) < 0)
		return -1;

	if (setpgid(0, 0) < 0) {
		p_err("setpgid() failed: %s\n", strerror(errno));
		return -1;
	}

	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
		p_err("signal() failed: %s\n", strerror(errno));
		return -1;
	}

	if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
		p_err("signal() failed: %s\n", strerror(errno));
		return -1;
	}

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		p_err("signal() failed: %s\n", strerror(errno));
		return -1;
	}

	(void)umask(S_IWGRP | S_IWOTH);

	for (fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
		(void)close(fd);
	}

	return 0;
}

int
kill_daemon(const char *pid_file)
{
	pid_t pid;
	int timeout = 30;

	pid = read_pid_file(pid_file);
	if (pid < 0)
		return -1;

	if (kill(pid, SIGTERM) < 0) {
		p_err("Cannot send signal to pid %ju: %s\n",
		    (uintmax_t)pid, strerror(errno));
		return -1;
	}

	while (kill(pid, 0) == 0) {
		sleep(1);
		if (--timeout <= 0) {
			p_err("Cannot terminate process %ju\n", (uintmax_t)pid);
			return -1;
		}
	}

	return 0;
}
