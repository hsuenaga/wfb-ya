#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <assert.h>

#include <sys/param.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "compat.h"

#include "wfb_params.h"
#include "net_core.h"
#include "net_pcap.h"
#include "net_inet6.h"
#include "rx_core.h"
#include "crypto_wfb.h"
#include "fec_wfb.h"
#ifdef ENABLE_GSTREAMER
#include "decode_h265.h"
#endif

#include "util_msg.h"

struct wfb_opt wfb_options = {
	.rx_wireless = DEF_WRX,
	.tx_wired = DEF_ETX,
	.rx_wired = DEF_ERX,
	.key_file = DEF_KEY,
	.mc_addr = WFB_ADDR6,
	.mc_port = WFB_PORT,
	.local_play = false,
	.use_monitor = false,
	.no_fec = false,
	.log_file = NULL,
	.debug = false
};

static void
print_help(const char *path)
{
	char name[MAXPATHLEN];

	assert(path);

	basename_r(path, name);
	printf("%s -- WFB-NG based Rx and Multicast Tx\n", name);
	printf("\n");
	printf("Synopsis:\n");
	printf("\t%s [-w <dev>] [-e <dev>] [-E <dev>]\n", name);
        printf("\t[-a <addr>] [-p <port>] [-k <file>]\n");
	printf("\t[-l] [-m] [-n] [-d] [-h]\n");
	printf("Options:\n");
	printf("\t-w <dev> ... specify Wireless Rx device. default: %s\n",
	    DEF_WRX ? DEF_WRX : "none");
	printf("\t-e <dev> ... specify Ethernet Rx device. default: %s\n",
	    DEF_ERX ? DEF_ERX : "none");
	printf("\t-E <dev> ... specify Ethernet Tx device. default: %s\n",
	    DEF_ERX ? DEF_ERX : "none");
	printf("\t-a <addr> ... specify Multicast address . default: %s\n",
	    WFB_ADDR6);
	printf("\t-p <port> ... specify Multicast port . default: %u\n",
	    WFB_PORT);
	printf("\t-k <file> ... specify cipher key. default: %s\n",
	    DEF_KEY ? DEF_KEY : "none");
#ifdef ENABLE_GSTREAMER
	printf("\t-l ... enable local play. default: disable\n");
#endif
	printf("\t-L ... log file name. default: (none)\n");
	printf("\t-m ... use RFMonitor mode instead of Promiscous mode.\n");
	printf("\t-n ... don't apply FEC decode.\n");
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

	while ((ch = getopt(argc, argv, "w:e:E:a:p:k:L:lmndh")) != -1) {
		long val;

		switch (ch) {
			case 'w':
				wfb_options.rx_wired = NULL;
				wfb_options.rx_wireless = optarg;
				break;
			case 'e':
				wfb_options.rx_wireless = NULL;
				wfb_options.rx_wired = optarg;
				break;
			case 'E':
				wfb_options.tx_wired = optarg;
				break;
			case 'a':
				wfb_options.mc_addr = optarg;
				break;
			case 'p':
				val = strtol(optarg, NULL, 10);
				if (val <= 0 || val > 0xffff) {
					fprintf(stderr,
					    "Invalid port %s\n", optarg);
					exit(0);
				}
				wfb_options.mc_port = (uint16_t)val;
				break;
			case 'k':
				wfb_options.key_file = optarg;
				break;
			case 'l':
#ifdef ENABLE_GSTREAMER
				wfb_options.local_play = true;
#else
				fprintf(stderr, "gstreamer is disabled by compile option.\n");
				exit(0);
#endif
				break;
			case 'L':
				wfb_options.log_file = optarg;
				break;
			case 'm':
				wfb_options.use_monitor = true;
				break;
			case 'n':
				wfb_options.no_fec = true;
				break;
			case 'd':
				wfb_options.debug = true;
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

	if (!wfb_options.rx_wireless && !wfb_options.rx_wired) {
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
	struct netinet6_rx_context in6r_ctx;
	struct netinet6_tx_context in6t_ctx;
	struct rx_context rx_ctx;
#ifdef ENABLE_GSTREAMER
	struct decode_h265_context d_ctx;
#endif
	uint32_t wfb_ch = 0;
	int fd;

	parse_options(&argc, &argv);

	p_debug("Initalizing netcore.\n");
	if (netcore_initialize(&net_ctx) < 0) {
		p_err("Cannot Initialize netcore\n");
		exit(0);
	}

	p_debug("Initalizing crypto.\n");
	if (crypto_wfb_init(wfb_options.key_file) < 0) {
		p_err("Cannot Initialize crypto\n");
		exit(0);
	}

	p_debug("Initalizing fec.\n");
	if (fec_wfb_init() < 0) {
		p_err("Cannot Initialize FEC\n");
		exit(0);
	}

	p_debug("Initalizing rx parser.\n");
	if (rx_context_init(&rx_ctx, wfb_ch) < 0) {
		p_err("Cannot Initialize Rx.\n");
		exit(0);
	}

	p_debug("Initializing tx components\n");
	if (wfb_options.tx_wired) {
		netinet6_tx_initialize(&in6t_ctx, wfb_options.tx_wired);
		if (rx_context_set_mirror(&rx_ctx, netinet6_tx, &in6t_ctx) < 0) {
			p_err("Cannot Attach NetRx\n");
			exit(0);
		}
	}

#ifdef ENABLE_GSTREAMER
	if (wfb_options.local_play) {
		// Create new thread and waiting for data.
		p_debug("Initalizing decoder.\n");
		if (decode_h265_context_init(&d_ctx) < 0) {
			p_err("Cannot Initialize Decoder\n");
			exit(0);
		}
		if (decode_h265_thread_start(&d_ctx) < 0) {
			p_err("Cannot Start Decoder thread\n");
			exit(0);
		}
		if (rx_context_set_decode(&rx_ctx, decode_h265, &d_ctx) < 0) {
			p_err("Cannot Attach Decoder\n");
			exit(0);
		}
	}
#endif

	p_debug("Initializing rx components\n");
	if (wfb_options.rx_wireless) {
		p_debug("Initalizing pcap rx.\n");
		fd = netpcap_initialize(&pcap_ctx, &net_ctx, &rx_ctx,
		    wfb_options.rx_wireless, wfb_options.use_monitor);
		if (fd < 0) {
			p_err("Cannot Initialize PCAP Rx\n");
			exit(0);
		}
	}

	if (wfb_options.rx_wired) {
		p_debug("Initalizing inet6 rx.\n");
		fd = netinet6_rx_initialize(&in6r_ctx, &net_ctx, &rx_ctx,
		    wfb_options.rx_wired);
		if (fd < 0) {
			p_err("Cannot Initialize Inet6 Rx\n");
			exit(0);
		}
	}

	p_debug("Start thread.\n");
	netcore_thread_start(&net_ctx);
	p_debug("Wainting for thread complete.\n");
	netcore_thread_join(&net_ctx);

	netcore_deinitialize(&net_ctx);
	netpcap_deinitialize(&pcap_ctx);
	if (wfb_options.rx_wired) {
		netinet6_rx_deinitialize(&in6r_ctx);
	}

	// XXX: more cleanup
	exit(1);
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
