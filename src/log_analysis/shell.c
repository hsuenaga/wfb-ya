#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "../util_msg.h"

#include "log_json.h"
#include "log_csv.h"
#include "log_summary.h"
#include "log_message.h"
#include "log_hist.h"
#include "log_filter.h"
#ifdef ENABLE_GSTREAMER
#include "log_h265.h"
#endif
#include "log_raw.h"

#include "shell.h"

static struct log_store *
load_file(const char *file_name)
{
	FILE *fp;
	struct log_store *ls;

	assert(file_name);

	fp = fopen(file_name, "r");
	if (fp == NULL) {
		p_info("Cannot open file %s: %s\n", file_name, strerror(errno));
		return NULL;
	}
	p_info("Loading %s...\n", file_name);
	ls = load_log(fp);
	fclose(fp);

	if (ls == NULL) {
		p_info("Load error.\n");
		return NULL;
	}
	p_info("%u packets loaded.\n", ls->n_pkts);

	return ls;
}

static const char *
expand_token(struct shell_token *token)
{
	static char buf[BUFSIZ];
	char *bufp = buf;
	size_t size = sizeof(buf);
	bool sep = false;
	int i;

	bufp[0] = '\0';
	for (i = 0; i < MAX_TOKEN; i++) {
		int n;

		if (token->tok[i] == NULL)
			break;
		n = snprintf(bufp, size, "%s%s", sep ? " " : "", token->tok[i]);
		if (n < 0)
			break;
		bufp += n;
		size -= n;
		sep = true;
	}

	return buf;
}

static int
shell_show_csv(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	csv_serialize(ctx->fp_out, ctx->ls);

	return 0;
}

static int
shell_show_json(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	json_serialize(ctx->fp_out, ctx->ls);

	return 0;
}

static int
shell_show_json_block(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	json_serialize_block(ctx->fp_out, ctx->ls);

	return 0;
}

static int
shell_show_message(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	dump_message(ctx->fp_out, ctx->ls);

	return 0;
}

static int
shell_show_hist(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	log_hist(ctx->ls);

	return 0;
}

static int
shell_filter_dbm(struct shell_context *ctx, struct shell_token *token)
{
	char *endptr;
	long dbm;
	int nfec;

	token_next(token);

	if (token->cur == NULL || token->cur[0] == '\0') {
		p_info("Missing argument\n");
		p_info("%s <dbm>\n", expand_token(token));
		return -1;
	}
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}
	dbm = strtol(token->cur, &endptr, 10);
	if (*endptr != '\0') {
		p_info("Invalid argument %s.\n", token->cur);
		return -1;
	}
	if (dbm < INT8_MIN || dbm > INT8_MAX) {
		p_info("dbm out of range.\n");
		return -1;
	}

	if (log_filter_reset(ctx->ls) < 0) {
		p_info("filter reset failed.\n");
		return -1;
	}
	if (log_filter_dbm(ctx->ls, dbm) < 0) {
		p_info("filter failed.\n");
		return -1;
	}

	return 0;
}

static int
shell_write_csv(struct shell_context *ctx, struct shell_token *token)
{
	FILE *fp;

	token_next(token);
	if (token->cur == NULL) {
		p_info("Missing argument\n");
		p_info("%s <file_name>\n", expand_token(token));
		return -1;
	}
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	fp = fopen(token->cur, "w");
	if (fp == NULL) {
		p_info("Cannot open file: %s\n", token->cur);
		return -1;
	}
	csv_serialize(fp, ctx->ls);
	fclose(fp);

	return 0;
}

static int
shell_write_json(struct shell_context *ctx, struct shell_token *token)
{
	FILE *fp;

	token_next(token);
	if (token->cur == NULL) {
		p_info("Missing argument\n");
		p_info("%s <file_name>\n", expand_token(token));
		return -1;
	}
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	fp = fopen(token->cur, "w");
	if (fp == NULL) {
		p_info("Cannot open file: %s\n", token->cur);
		return -1;
	}
	json_serialize(fp, ctx->ls);
	fclose(fp);

	return 0;
}

static int
shell_write_json_block(struct shell_context *ctx, struct shell_token *token)
{
	FILE *fp;

	token_next(token);
	if (token->cur == NULL) {
		p_info("Missing argument\n");
		p_info("%s <file_name>\n", expand_token(token));
		return -1;
	}
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	fp = fopen(token->cur, "w");
	if (fp == NULL) {
		p_info("Cannot open file: %s\n", token->cur);
		return -1;
	}
	json_serialize_block(fp, ctx->ls);
	fclose(fp);

	return 0;
}

#ifdef ENABLE_GSTREAMER
static int
shell_write_mp4(struct shell_context *ctx, struct shell_token *token)
{
	token_next(token);
	if (token->cur == NULL) {
		p_info("Missing argument\n");
		p_info("%s <file_name>\n", expand_token(token));
		return -1;
	}
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}
	p_info("Write to %s\n", token->cur);
	write_mp4(token->cur, ctx->ls);
	return 0;
}

static int
shell_play(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}
	play_h265(ctx->ls);
	return 0;
}
#endif

static int
shell_stat(struct shell_context *ctx, struct shell_token *token)
{
	if (!ctx->ls) {
		p_info("No file loaded.\n");
		return -1;
	}

	summary_output(ctx->fp_out, ctx->ls);
	return 0;
}

static int
shell_ls(struct shell_context *ctx, struct shell_token *token)
{
	int rc;

	rc = system("ls");
	if (rc < 0) {
		p_err("system() failed: %s\n", strerror(errno));
	}

	// ignore result code.
	return 0;
}

static int
shell_load(struct shell_context *ctx, struct shell_token *token)
{
	FILE *fp;
	struct log_store *ls;

	token_next(token);
	if (token->cur == NULL) {
		p_info("Missing argument.\n");
		p_info("%s <file_name>\n", expand_token(token));
		return -1;
	}

	ls = load_file(token->cur);
	if (ls == NULL) {
		return -1;
	}
	if (ctx->ls) {
		free_log(ctx->ls);
	}
	ctx->ls = ls;
	return 0;
}

static int
shell_exit(struct shell_context *ctx, struct shell_token *token)
{
	p_info("Exiting..\n");
	exit(0);
}

static int
shell_help(struct shell_context *ctx, struct shell_token *token)
{
	struct shell_cmd_def *cmd;
	bool sep = false;


	if (ctx->cur_tree != &ctx->top) {
		p_info("Arguments:\n");
	}
	else {
		p_info("Comands:\n");
	}
	for (cmd = ctx->cur_tree->cmds; cmd->token; cmd++) {
		p_info("%s%s", sep ? " " : "", cmd->token);
		sep = true;
	}
	p_info("\n");

	return 0;
}

static struct shell_cmd_def write_cmds[] = {
	{ "csv", NULL, shell_write_csv },
	{ "json", NULL, shell_write_json },
	{ "json_block", NULL, shell_write_json_block },
#ifdef ENABLE_GSTREAMER
	{ "mp4", NULL, shell_write_mp4 },
#endif
	{NULL, NULL, NULL}
};

static struct shell_cmd_tree write_tree = {
	.cmds = write_cmds
};

static struct shell_cmd_def show_cmds[] = {
	{ "csv", NULL, shell_show_csv },
	{ "json", NULL, shell_show_json },
	{ "json_block", NULL, shell_show_json_block },
	{ "message", NULL, shell_show_message },
	{ "hist", NULL, shell_show_hist },
	{NULL, NULL, NULL}
};

static struct shell_cmd_tree show_tree = {
	.cmds = show_cmds
};

static struct shell_cmd_def filter_cmds[] = {
	{ "dbm", NULL, shell_filter_dbm },
	{NULL, NULL, NULL}
};

static struct shell_cmd_tree filter_tree = {
	.cmds = filter_cmds
};

static struct shell_cmd_def top_level[] = {
	{ "ls", NULL, shell_ls },
	{ "write", &write_tree, NULL},
	{ "show", &show_tree, NULL},
	{ "filter", &filter_tree, NULL},
#ifdef ENABLE_GSTREAMER
	{ "play", NULL, shell_play },
#endif
	{ "stat", NULL, shell_stat },
	{ "load", NULL, shell_load },
	{ "exit", NULL, shell_exit },
	{ "quit", NULL, shell_exit },
	{ "help", NULL, shell_help },
	{ "?", NULL, shell_help },
	{ NULL, NULL , NULL }
};

static int
shell_register_cmd(struct shell_cmd_tree *tree, struct shell_cmd_def *cmds)
{
	tree->cmds = cmds;
	return 0;
}

static int
shell_init(struct shell_context *ctx)
{
	ctx->ls = NULL;
	ctx->fp_in = stdin;
	ctx->fp_out = stdout;
	shell_register_cmd(&ctx->top, top_level);

	return 0;
}

static int
shell_invoke(struct shell_context *ctx,
    struct shell_cmd_tree *tree, struct shell_token *token)
{
	struct shell_cmd_tree *next = NULL;
	shell_func_t func = NULL;
	struct shell_cmd_def *def;

	if (tree == NULL)
		tree = &ctx->top;
	ctx->cur_tree = tree;

	if (token->cur == NULL) {
		shell_help(ctx, token);
		return -1;
	}

	for (def = tree->cmds; def->token; def++) {
		if (strcasecmp(token->cur, def->token) == 0) {
			func = def->func;
			next = def->sub_tree;
			break;
		}
	}

	if (func && !token->query) {
		return func(ctx, token);
	}

	if (!next) {
		if (!token->query) {
			p_info("Unknown command: %s\n", token->cur);
		}
		else {
			shell_help(ctx, token);
		}
		return -1;
	}

	token_next(token);
	return shell_invoke(ctx, next, token);
}

int
shell_lex(struct shell_token *token)
{
	const char *sep = " ";
	char *lasts = NULL;
	char *ptr = token->buf;
	char *nl;

	nl = strchr(token->buf, '\n');
	if (nl)
		*nl = '\0';

	for (ptr = strtok_r(token->buf, sep, &lasts);
	     ptr;
	     ptr = strtok_r(NULL, sep, &lasts)) {
		token->tok[token->cur_idx++] = ptr;
		if (token->cur_idx >= MAX_TOKEN)
			break;
	}
	token_reset(token);
	return 0;
}

struct shell_token *
shell_read(FILE *fp_in, FILE *fp_out)
{
	static struct shell_token token;
	char *bufp;
	int ch;

	if (fp_out)
		fprintf(fp_out, "> ");

	memset(&token, 0, sizeof(token));
	token_reset(&token);
	token.tok[0] = token.buf;
	bufp = token.buf;
	while( (ch = fgetc(fp_in)) && bufp < &token.buf[BUFSIZ]) {
		if (ch == EOF) {
			fputc('\n', fp_out);
			return NULL;
		}
		if (ch == '\n') {
			if (token.query) {
				*--bufp = '\0';
			}
			break;
		}
		if (token.system) {
			*bufp++ = ch;
			*bufp = '\0';
			continue;
		}
		if (bufp == token.buf && ch == '!') {
			token.system = true;
			continue;
		}
		if (ch == '?') {
			token.query = true;
		}
		else {
			token.query = false;
		}
		*bufp++ = ch;
		*bufp = '\0';
	}
	if (token.system && token.buf[0] != '\0') {
		int rc;

		rc = system(token.buf);
		if (rc < 0) {
			p_err("system() failed: %s\n", strerror(errno));
		}
		// ignore result code
	}
	else {
		shell_lex(&token);
	}

	return &token;
}

int
shell(const char *file_name)
{
	struct shell_context ctx;
	struct shell_token *token;

	if (shell_init(&ctx) < 0) {
		p_err("Cannot initialize shell.\n");
		return -1;
	}

	if (file_name) {
		ctx.ls = load_file(file_name);
	}

	while ( (token = shell_read(ctx.fp_in, ctx.fp_out))) {
		if (token->system)
			continue;
		shell_invoke(&ctx, NULL, token);
	}

	return 0;
}
