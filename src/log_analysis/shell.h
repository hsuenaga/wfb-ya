#ifndef __SHELL_H__
#define __SHELL_H__
#include <sys/queue.h>

struct shell_context;
struct shell_token;
typedef int(*shell_func_t)(struct shell_context *, struct shell_token *);

struct shell_cmd_tree;

struct shell_cmd_def {
	const char *token;

	struct shell_cmd_tree *sub_tree;
	shell_func_t func;
};

struct shell_cmd_tree {
	struct shell_cmd_def *cmds;
};

#define MAX_TOKEN 10
struct shell_token {
	char buf[BUFSIZ];
	const char *cur;
	int cur_idx;
	bool query;
	bool system;

	char *tok[MAX_TOKEN];
};
#define token_reset(t) do {					\
		(t)->cur_idx = 0;				\
		(t)->cur = (t)->tok[0];				\
	} while (/* CONSTCOND */ 0)
#define token_next(t) do {					\
		(t)->cur_idx++;					\
		if ((t)->cur_idx >= MAX_TOKEN) {		\
			(t)->cur_idx = MAX_TOKEN;		\
			(t)->cur = NULL;			\
		}						\
		else						\
			(t)->cur = (t)->tok[(t)->cur_idx];	\
	} while (/* CONSTCOND */ 0)

struct shell_context {
	FILE *fp_out;
	FILE *fp_in;
	struct log_store *ls;
	struct shell_cmd_tree top;
	struct shell_cmd_tree write;
	struct shell_cmd_tree *cur_tree;
};

extern int shell(const char *file_name);

#endif /* __SHELL_H__ */
