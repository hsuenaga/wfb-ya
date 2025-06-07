#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/param.h>
#include <arpa/inet.h>

#include "log_analysis.h"

#include "rx_core.h"
#include "wfb_params.h"
#include "util_log.h"
#include "compat.h"

struct wfb_opt wfb_options = {
	.debug = false
};

struct log_analysis_opt options = {
	.file_name = NULL 
};

struct log_data *data = NULL;

static void
print_help(const char *path)
{
	char name[MAXPATHLEN];

	assert(path);

	basename_r(path, name);
	printf("%s --WFB-YA log analyzer\n", name);
	printf("\n");
	printf("Synopsis:\n");
	printf("\t%s [-f <name>] [-d]\n", name);
	printf("Options:\n");
	printf("\t-f <name> ... specify file name. default: STDIN\n");
	printf("\t-d ... enable debug log.\n");
}

static void
parse_options(int *argc0, char **argv0[])
{
	int argc = *argc0;
	char **argv = *argv0;
	int ch;

	while ((ch = getopt(argc, argv, "df:h")) != -1) {
		switch (ch) {
			case 'd':
				wfb_options.debug = 1;
				break;
			case 'f':
				options.file_name = optarg;
				break;
			case 'h':
			case '?':
			default:
				print_help(argv[0]);
				exit(0);
		}
	}
	argc -= optind;
	argv += optind;
	*argc0 = argc;
	*argv0 = argv;

	return;
}

void
dump_header(struct rx_log_header *hd)
{
	assert(hd);

	p_debug("--- New Header---\n");
	p_debug("SEQ: %llu\n", hd->seq);
	p_debug("TimeStamp: %ld.%09ld\n", hd->ts.tv_sec, hd->ts.tv_nsec);
	p_debug("Size: %u\n", hd->size);
	p_debug("Block: %llu\n", hd->block_idx);
	p_debug("Fragemnt: %u\n", hd->fragment_idx);
	p_debug("FEC_K: %u\n", hd->fec_k);
	p_debug("FEC_N: %u\n", hd->fec_n);
}

struct log_data *
log_data_alloc(struct log_store *ds, uint64_t key)
{
	struct log_data *d;

	STAILQ_FOREACH(d, &ds->dh, next) {
		if (d->key == key) {
			p_debug("Overwrite existing entry.\n");
			return d;
		}
	}
	p_debug("Create new entry.\n");

	d = (struct log_data *)malloc(sizeof(*d));
	if (d == NULL)
		return NULL;

	memset(d, 0, sizeof(*d));
	d->key = key;
	STAILQ_INSERT_TAIL(&ds->dh, d, next);

	return d;
}

int
parse_payload(FILE *fp, struct log_data *data, ssize_t size)
{
	assert(fp);
	assert(size >= 0 && size < WIFI_MTU);

	if (size == 0)
		return 0;
	if (data->buf != NULL) {
		p_info("Duplicated payload.\n");
		return 0;
	}

	data->buf = malloc(size);
	if (data->buf == NULL)
		return -1;

	if (fread(data->buf, size, 1, fp) <= 0) {
		free(data->buf);
		data->buf = NULL;
		data->size = 0;
		if (feof(fp)) {
			p_debug("End of File\n");
			return -1;
		}
		else if (ferror(fp)) {
			p_err("%s\n", strerror(errno));
			return -1;
		}
		p_err("Unknown I/O failure.\n");
		return -1;
	}

	return 0;
}

ssize_t
parse_header(FILE *fp, struct log_store *ds)
{
	struct rx_log_header hd;
	struct log_data *d;
	static struct timespec epoch = {
		.tv_sec = 0,
		.tv_nsec = 0
	};

	assert(fp);

	if (fread(&hd, sizeof(hd), 1, fp) <= 0) {
		if (feof(fp)) {
			p_debug("End of File\n");
			return -1;
		}
		else if (ferror(fp)) {
			p_err("%s\n", strerror(errno));
			return -1;
		}
		p_err("Unknown I/O failure.\n");
		return -1;
	}
	if (epoch.tv_sec == 0) {
		epoch = hd.ts;
	}
	timespecsub(&hd.ts, &epoch, &hd.ts);
	dump_header(&hd);

	d = log_data_alloc(ds, hd.seq);
	if (hd.size > 0)
		d->size = hd.size;
	if (d->ts.tv_sec == 0 && d->ts.tv_nsec == 0)
		d->ts = hd.ts;
	d->block_idx = hd.block_idx;
	d->fragment_idx = hd.fragment_idx;
	if (hd.freq != 0)
		d->freq = hd.freq;
	if (hd.dbm != INT16_MIN)
		d->dbm = hd.dbm;

	if (hd.rx_src.sin6_family == AF_INET6) {
		d->rx_src = hd.rx_src;
	}

	if (parse_payload(fp, d, hd.size) < 0)
		return -1;

	return hd.size;
}

int
serialize_log(struct log_store *ds)
{
	struct log_data *d;
	char s_addr[INET6_ADDRSTRLEN];

	p_info("\"Sequence\", \"TimeStamp\", \"Block Index\", \"Fragment Index\", \"Src Node\", \"Frequency\", \"dBm\", \"Data Size\"\n");
	STAILQ_FOREACH(d, &ds->dh, next) {
		if (d->rx_src.sin6_family == AF_INET6) {
			inet_ntop(AF_INET6, &d->rx_src.sin6_addr, s_addr, sizeof(s_addr));
		}
		else {
			s_addr[0] = '\0';
		}
		p_info("%u, %llu.%09llu, %llu, %llu, %s, %u, %d, %u\n",
		    d->key, d->ts.tv_sec, d->ts.tv_nsec,
		    d->block_idx, d->fragment_idx,
		    s_addr, d->freq, d->dbm, d->size);
	}

	return 0;
}

int
parse_log(FILE *fp)
{
	struct log_store ds;
	ssize_t size;

	assert(fp);

	STAILQ_INIT(&ds.dh);

	for (;;) {
		size = parse_header(fp, &ds);
		if (size < 0)
			break;
	}

	serialize_log(&ds);

	return 0;
}

int
main(int argc, char *argv[])
{
	FILE *fp;

	parse_options(&argc, &argv);

	if (options.file_name) {
		fp = fopen(options.file_name, "r");
		if (fp == NULL) {
			p_err("Invalid file name: %s\n", options.file_name);
			exit(0);
		}
	}
	else {
		fp = stdin;
	}

	parse_log(fp);

	fclose(fp);
	exit(1);
}
