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

#include "../wfb_params.h"
#include "../util_msg.h"
#include "../compat.h"

#include "log_analysis.h"
#include "log_raw.h"
#include "log_csv.h"
#include "log_json.h"
#include "log_summary.h"
#ifdef ENABLE_GSTREAMER
#include "log_h265.h"
#endif
#include "log_message.h"
#include "shell.h"

struct wfb_opt wfb_options = {
	.debug = false
};

struct log_analysis_opt options = {
	.file_name_in = NULL,
	.file_name_out = NULL,
	.out_type = OUTPUT_CSV,
	.local_play = false,
	.interactive = false,
	.dump_message = false,
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
	printf("\t%s [-f <name>] [-o <name>] [-t <type>] [-l] [-i [<name>]]"
	    " [-m] [-d]\n", name);
	printf("Options:\n");
	printf("\t-f <name> ... specify input file name. default: STDIN\n");
	printf("\t-o <name> ... specify output file name. default: STDOUT\n");
	printf("\t-t <type> ... specify output file format. default: csv\n");
#ifdef ENABLE_GSTREAMER
	printf("\t-l ... enable local play(GStreamer)\n");
#endif
	printf("\t-i [<name>] ... interactive mode."
	    " file <name> will be loaded.\n");
	printf("\t-r ... enable RSSI overlay.\n");
	printf("\t-m ... dump messages.\n");
	printf("\t-d ... enable debug log.\n");
	printf("Output Foramt <type>:\n");
	printf("\tcsv .. comma separated values(default).\n");
	printf("\tjson .. javascript object(per sequence).\n");
	printf("\tjson_block .. javascript object(per block).\n");
	printf("\tsummary .. summary values.\n");
#ifdef ENABLE_GSTREAMER
	printf("\tmp4 .. write MP4 video.\n");
#endif
	printf("\tnone .. no output. error check only.\n");
}

static void
parse_options(int *argc0, char **argv0[])
{
	int argc = *argc0;
	char **argv = *argv0;
	int ch;

	while ((ch = getopt(argc, argv, "f:o:t:w:li:mrdh")) != -1) {
		switch (ch) {
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
				else if (strcasecmp(optarg, "json_block") == 0){
					options.out_type = OUTPUT_JSON_BLOCK;
				}
				else if (strcasecmp(optarg, "summary") == 0) {
					options.out_type = OUTPUT_SUMMARY;
				}
#ifdef ENABLE_GSTREAMER
				else if (strcasecmp(optarg, "mp4") == 0) {
					options.out_type = OUTPUT_MP4;
				}
#endif
				else if (strcasecmp(optarg, "none") == 0) {
					options.out_type = OUTPUT_NONE;
				}
				else {
					fprintf(stderr,
					    "Invalid output type %s.\n",
					    optarg);
					exit(EXIT_FAILURE);
				}
				break;
			case 'l':
#ifdef ENABLE_GSTREAMER
				options.local_play = true;
#else
				fprintf(stderr, "GStreamer is diabled.\n");
				exit(0);
#endif
				break;
			case 'm':
				options.dump_message = true;
				break;
			case 'r':
				wfb_options.rssi_overlay = true;
				break;
			case 'i':
				options.interactive = true;
				options.file_name_in = optarg;
				break;
			case 'd':
				wfb_options.debug = 1;
				break;
			case 'h':
				print_help(argv[0]);
				exit(EXIT_SUCCESS);
			case '?':
			default:
				print_help(argv[0]);
				exit(EXIT_FAILURE);
		}
	}
	argc -= optind;
	argv += optind;
	*argc0 = argc;
	*argv0 = argv;

	return;
}

int
_main(int argc, char *argv[])
{
	FILE *fp_in = NULL, *fp_out = NULL;
	struct log_store *ls;

	parse_options(&argc, &argv);

	if (options.interactive) {
		if (shell(options.file_name_in) < 0)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	if (options.file_name_in) {
		fp_in = fopen(options.file_name_in, "r");
		if (fp_in == NULL) {
			p_err("Invalid file name: %s\n", options.file_name_in);
			exit(EXIT_FAILURE);
		}
	}
	if (options.file_name_out && !options.local_play) {
		fp_out = fopen(options.file_name_out, "w");
		if (fp_out == NULL) {
			p_err("Invalid file name: %s\n", options.file_name_out);
			exit(EXIT_FAILURE);
		}
	}

	ls = load_log(fp_in);
	if (fp_in)
		fclose(fp_in);
	if (ls == NULL) {
		p_err("Invalid log file.\n");
		exit(EXIT_FAILURE);
	}

#ifdef ENABLE_GSTREAMER
	if (options.local_play) {
		play_h265(ls);
		exit(EXIT_SUCCESS);
	}
#endif

	if (options.dump_message) {
		dump_message(fp_out, ls);
		exit(EXIT_SUCCESS);
	}

	switch (options.out_type) {
		case OUTPUT_CSV:
			csv_serialize(fp_out, ls);
			break;
		case OUTPUT_JSON:
			json_serialize(fp_out, ls);
			break;
		case OUTPUT_JSON_BLOCK:
			json_serialize_block(fp_out, ls);
			break;
		case OUTPUT_SUMMARY:
			summary_output(fp_out, ls);
			break;
		case OUTPUT_MP4:
#ifdef ENABLE_GSTREAMER
			if (fp_out)
				fclose(fp_out);
			write_mp4(options.file_name_out, ls);
			break;
#endif
		case OUTPUT_NONE:
		default:
			break;
	}

	exit(EXIT_SUCCESS);
}

int
main(int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE \
    && defined(ENABLE_GSTREAMER)
	return gst_macos_main((GstMainFunc) _main, argc, argv, NULL);
#else
	return _main(argc, argv);
#endif
}
