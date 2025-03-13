#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <assert.h>

#include <sys/param.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "compat.h"

#include "net_core.h"
#include "net_pcap.h"
#include "net_inet6.h"
#include "rx_core.h"
#include "crypto_wfb.h"
#include "fec_wfb.h"
#include "decode_h265.h"
#include "util_log.h"

struct wfb_opt {
	const char *rx_wireless;
	const char *txrx_wired;
	const char *key_file;
	bool local_play;
	bool use_monitor;
} options;

static void
print_help(const char *path)
{
	char name[MAXPATHLEN];

	assert(path);

	basename_r(path, name);
	printf("%s -- WFB-NG based Rx and Multicast Tx\n", name);
	printf("\n");
	printf("Synopsis:\n");
	printf("\t%s [-w <dev>] [-e <dev>] [-k <file>] [-l] [-m] [-d]\n", name);
	printf("Options:\n");
	printf("\t-w <dev> ... specify Wireless Rx device. default: %s\n",
	    DEF_WRX ? DEF_WRX : "none");
	printf("\t-e <dev> ... specify Ethernet Tx/Rx device. default: %s\n",
	    DEF_ERX ? DEF_ERX : "none");
	printf("\t-k <file> ... specify cipher key. default: %s\n",
	    DEF_KEY ? DEF_KEY : "none");
	printf("\t-l ... enable local play. default: disable\n");
	printf("\t-m ... use RFMonitor mode instead of Promiscous mode.\n");
	printf("\t-d ... enable debug output.\n");
	printf("\t-h ... print help(this).\n");
	printf("\n");
	printf("If tx device is not specified, the progaram decode the stream.\n");
}

static void
parse_options(int *argc0, char **argv0[])
{
	int argc = *argc0;
	char **argv = *argv0;
	int ch;

	options.rx_wireless = DEF_WRX;
	options.txrx_wired = DEF_ERX;
	options.key_file = DEF_KEY;
	debug = 0;

	while ((ch = getopt(argc, argv, "w:e:k:lmdh")) != -1) {
		switch (ch) {
			case 'w':
				options.rx_wireless = optarg;
				break;
			case 'e':
				options.txrx_wired = optarg;
				break;
			case 'k':
				options.key_file = optarg;
				break;
			case 'l':
				options.local_play = true;
				break;
			case 'm':
				options.use_monitor = true;
				break;
			case 'd':
				debug = 1;
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

	if (!options.rx_wireless && !options.txrx_wired) {
		fprintf(stderr, "Please specify at least one Rx device.\n");
		exit(0);
	}

	return;
}


static int
_main(int argc, char *argv[])
{
	struct netcore_context net_ctx;
	struct netpcap_context pcap_ctx;
	struct netinet6_context in6_ctx;
	struct rx_context rx_ctx;
	struct decode_h265_context d_ctx;
	uint32_t wfb_ch = 0;
	int fd;

	parse_options(&argc, &argv);

	p_debug("Initalizing netcore.\n");
	if (netcore_initialize(&net_ctx) < 0) {
		p_err("Cannot Initialize netcore\n");
		exit(0);
	}

	p_debug("Initalizing crypto.\n");
	if (crypto_wfb_init("./gs.key") < 0) {
		p_err("Cannot Initialize crypto\n");
		exit(0);
	}

	p_debug("Initalizing fec.\n");
	if (fec_wfb_init() < 0) {
		p_err("Cannot Initialize FEC\n");
		exit(0);
	}

	if (options.local_play) {
		p_debug("Initalizing decoder.\n");
		if (decode_h265_context_init(&d_ctx) < 0) {
			p_err("Cannot Initialize Decoder\n");
			exit(0);
		}
	}

	p_debug("Initalizing rx.\n");
	if (options.local_play) {
		if (rx_context_init(&rx_ctx, wfb_ch,
		    decode_h265, &d_ctx, NULL, NULL) < 0) {
			p_err("Cannot Initialize Rx\n");
			exit(0);
		}
	}
	else if (options.txrx_wired) {
		if (rx_context_init(&rx_ctx, wfb_ch,
		    NULL, NULL, netinet6_tx, &in6_ctx) < 0) {
			p_err("Cannot Initialize Rx\n");
			exit(0);
		}
	}
	else {
		if (rx_context_init(&rx_ctx, wfb_ch,
		    NULL, NULL, NULL, NULL) < 0) {
			p_err("Cannot Initialize Rx\n");
			exit(0);
		}
	}

	if (options.rx_wireless) {
		p_debug("Initalizing pcap.\n");
		fd = netpcap_initialize(&pcap_ctx, &net_ctx, &rx_ctx,
		    options.rx_wireless, wfb_ch, options.use_monitor);
		if (fd < 0) {
			p_err("Cannot Initialize pcap\n");
			exit(0);
		}
	}

	if (options.txrx_wired) {
		p_debug("Initalizing inet6.\n");
		fd = netinet6_initialize(&in6_ctx, &net_ctx, &rx_ctx,
		    options.txrx_wired, wfb_ch);
		if (fd < 0) {
			p_err("Cannot Initialize inet6\n");
			exit(0);
		}
	}

	p_debug("Start thread.\n");
	netcore_thread_start(&net_ctx);
	p_debug("Wainting for thread complete.\n");
	netcore_thread_join(&net_ctx);

	netcore_deinitialize(&net_ctx);
	netpcap_deinitialize(&pcap_ctx);
	netinet6_deinitialize(&in6_ctx);

	// XXX: more cleanup
	exit(1);
}

int
main(int argc, char *argv[])
{
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
	return gst_macos_main((GstMainFunc) _main, argc, argv, NULL);
#else
	return _main(argc, argv);
#endif
}
