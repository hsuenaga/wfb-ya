#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/param.h>

#include "log_analysis.h"
#include "log_raw.h"
#include "log_csv.h"
#include "log_json.h"
#include "log_summary.h"

#include "wfb_params.h"
#include "util_log.h"

struct wfb_opt wfb_options = {
	.debug = false
};

struct log_analysis_opt options = {
	.file_name_in = NULL,
	.file_name_out = NULL,
	.out_type = OUTPUT_CSV,
};

static void
print_help(const char *path)
{
	char name[MAXPATHLEN];

	assert(path);

	basename_r(path, name);
	printf("%s --WFB-YA log analyzer\n", name);
	printf("\n");
	printf("Synopsis:\n");
	printf("\t%s [-f <name>] [-o <name>] [-t <type>] [-d]\n", name);
	printf("Options:\n");
	printf("\t-f <name> ... specify input file name. default: STDIN\n");
	printf("\t-o <name> ... specify output file name. default: STDOUT\n");
	printf("\t-t <type> ... specify output file format. default: csv\n");
	printf("\t-d ... enable debug log.\n");
	printf("Output Foramt <type>:\n");
	printf("\tcsv .. comma separated values(default).\n");
	printf("\tjson .. javascript object.\n");
	printf("\tsummary .. summary values.\n");
	printf("\tnone .. no output. error check only.\n");
}

static void
parse_options(int *argc0, char **argv0[])
{
	int argc = *argc0;
	char **argv = *argv0;
	int ch;

	while ((ch = getopt(argc, argv, "df:o:t:h")) != -1) {
		switch (ch) {
			case 'd':
				wfb_options.debug = 1;
				break;
			case 'f':
				options.file_name_in = optarg;
				break;
			case 'o':
				options.file_name_out = optarg;
				break;
			case 't':
				if (strcasecmp(optarg, "csv") == 0) {
					options.out_type = OUTPUT_CSV;
				}
				else if (strcasecmp(optarg, "json") == 0) {
					options.out_type = OUTPUT_JSON;
				}
				else if (strcasecmp(optarg, "summary") == 0) {
					options.out_type = OUTPUT_SUMMARY;
				}
				else if (strcasecmp(optarg, "none") == 0) {
					options.out_type = OUTPUT_NONE;
				}
				else {
					fprintf(stderr,
					    "Invalid output type %s.\n",
					    optarg);
					exit(0);
				}
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

int
main(int argc, char *argv[])
{
	FILE *fp_in = NULL, *fp_out = NULL;
	struct log_store *ls;

	parse_options(&argc, &argv);

	if (options.file_name_in) {
		fp_in = fopen(options.file_name_in, "r");
		if (fp_in == NULL) {
			p_err("Invalid file name: %s\n", options.file_name_in);
			exit(0);
		}
	}
	if (options.file_name_out) {
		fp_out = fopen(options.file_name_out, "w");
		if (fp_out == NULL) {
			p_err("Invalid file name: %s\n", options.file_name_out);
			exit(0);
		}
	}

	ls = load_log(fp_in);
	if (fp_in)
		fclose(fp_in);
	if (ls == NULL) {
		p_err("Invalid log file.\n");
		exit(0);
	}

	switch (options.out_type) {
		case OUTPUT_CSV:
			csv_serialize(fp_out, ls);
			break;
		case OUTPUT_JSON:
			json_serialize(fp_out, ls);
			break;
		case OUTPUT_SUMMARY:
			summary_output(fp_out, ls);
			break;
		case OUTPUT_NONE:
		default:
			break;
	}

	exit(1);
}
