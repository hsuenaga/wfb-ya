#ifndef __DAEMON_H__
#define __DAEMON_H__
extern int create_daemon(const char *pid_file);
extern int kill_daemon(const char *pid_file);
#endif /* __DAEMON_H__ */
