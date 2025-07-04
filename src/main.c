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
#include "daemon.h"

#include "wfb_params.h"
#include "net_core.h"
#include "net_pcap.h"
#include "net_inet.h"
#include "rx_core.h"
#include "rx_log.h"
#include "crypto_wfb.h"
#include "fec_wfb.h"
#include "wfb_ipc.h"
#ifdef ENABLE_GSTREAMER
#include "wfb_gst.h"
#endif

#include "util_msg.h"

struct wfb_opt wfb_options = {
	.rx_wireless = DEF_WRX,
	.tx_wired = DEF_ETX,
	.rx_wired = DEF_ERX,
	.key_file = DEF_KEY_FILE,
	.mc_addr = WFB_ADDR6,
	.mc_port = WFB_PORT,
	.local_play = false,
	.use_monitor = false,
	.no_fec = false,
	.log_file = NULL,
	.pid_file = DEF_PID_FILE,
	.ctrl_file = DEF_CTRL_FILE,
	.debug = false
};

struct wfb_statistics wfb_stats;

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
	printf("\t[-P <pid_file>] [-S <ipc_socket>]\n");
	printf("\t[-s <param>] [-r]\n");
	printf("\t[-l] [-m] [-n] [-d] [-D] [-s] [-h]\n");
	printf("Options:\n");
	printf("\t-w <dev> ... specify Wireless Rx device. default: %s\n",
	    DEF_WRX ? DEF_WRX : "none");
	printf("\t-e <dev> ... specify Ethernet Rx device. default: %s\n",
	    DEF_ERX ? DEF_ERX : "none");
	printf("\t-E <dev> ... specify Ethernet Tx device. default: %s\n",
	    DEF_ERX ? DEF_ERX : "none");
	printf("\t-a <addr> ... specify Multicast address . default: %s\n",
	    WFB_ADDR6);
	printf("\t-p <port> ... specify Multicast port . default: %s\n",
	    WFB_PORT);
	printf("\t-k <file> ... specify cipher key. default: %s\n",
	    DEF_KEY_FILE ? DEF_KEY_FILE : "none");
	printf("\t-P <pid_file> ... specify pid file(when -D). default: %s\n",
	    DEF_PID_FILE ? DEF_PID_FILE : "none");
	printf("\t-S <ipc_socket> ... specify IPC socket. default: %s\n",
	    DEF_CTRL_FILE ? DEF_CTRL_FILE : "none");
#ifdef ENABLE_GSTREAMER
	printf("\t-l ... enable local play. default: disable\n");
	printf("\t-r ... enable rssi overlay. default: disable\n");
#endif
	printf("\t-L ... traffic log file name. default: (none)\n");
	printf("\t-m ... use RFMonitor mode instead of Promiscous mode.\n");
	printf("\t-n ... don't apply FEC decode.\n");
	printf("\t-D ... run as daemon.\n");
	printf("\t-K ... kill daemon.\n");
	printf("\t-s <param> ... send query via IPC.\n");
	printf("\t-d ... enable debug output.\n");
	printf("\t-h ... print help(this).\n");
	printf("\n");
	printf("Queries(<param>):\n");
	printf("\tping ... check liveness only\n");
	printf("\tstat ... show internal counters\n");
	printf("\texit ... exit process\n");
	printf("\tquit ... exit process\n");
	printf("\n");
}

static void
load_environment(void)
{
	char *v;

	v = getenv("WFB_IPC_PATH");
	if (v) {
		wfb_options.ctrl_file = v;
	}
	v = getenv("WFB_PID_PATH");
	if (v) {
		wfb_options.pid_file = v;
	}
	v = getenv("WFB_KEY_PATH");
	if (v) {
		wfb_options.key_file = v;
	}
	v = getenv("WFB_RX_WIRED");
	if (v) {
		wfb_options.rx_wired = v;
	}
	v = getenv("WFB_TX_WIRED");
	if (v) {
		wfb_options.tx_wired = v;
	}
	v = getenv("WFB_RX_WIRELESS");
	if (v) {
		wfb_options.rx_wireless = v;
	}

	v = getenv("WFB_MULTICAST");
	if (v) {
		wfb_options.mc_addr = v;
	}
	v = getenv("WFB_PORT");
	if (v) {
		wfb_options.mc_port = v;
	}
}

static void
parse_options(int *argc0, char **argv0[])
{
	int argc = *argc0;
	char **argv = *argv0;
	int ch;

	while ((ch = getopt(argc, argv, "w:e:E:a:p:k:L:P:S:s:DKlrmndh")) != -1) {
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
				wfb_options.mc_port = optarg;
				break;
			case 'k':
				wfb_options.key_file = optarg;
				break;
			case 'l':
#ifdef ENABLE_GSTREAMER
				wfb_options.local_play = true;
#else
				fprintf(stderr, "gstreamer is disabled by compile option.\n");
				exit(EXIT_FAILURE);
#endif
				break;
			case 'r':
#ifdef ENABLE_GSTREAMER
				wfb_options.rssi_overlay = true;
#else
				fprintf(stderr, "gstreamer is disabled by compile option.\n");
				exit(EXIT_FAILURE);
#endif
				break;
			case 'D':
				wfb_options.daemon = true;
				break;
			case 'K':
				wfb_options.kill_daemon = true;
				break;
			case 'P':
				wfb_options.pid_file = optarg;
				break;
			case 'S':
				wfb_options.ctrl_file = optarg;
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
			case 's':
				wfb_options.query_param = optarg;
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

	if (wfb_options.query_param)
		return;
	if (wfb_options.kill_daemon)
		return;

	if (!wfb_options.rx_wireless && !wfb_options.rx_wired) {
		fprintf(stderr, "Please specify at least one Rx device.\n");
		exit(EXIT_FAILURE);
	}

	return;
}


static int
_main(int argc, char *argv[])
{
	struct netcore_context net_ctx;
	struct ipc_rx_context ipc_ctx;
	struct netpcap_context pcap_ctx;
	struct netinet_rx_context inrx_ctx;
	struct netinet_tx_context intx_ctx;
	struct rx_context rx_ctx;
#ifdef ENABLE_GSTREAMER
	struct wfb_gst_context gst_ctx;
#endif
	uint32_t wfb_ch = 0;
	int fd;

	load_environment();
	parse_options(&argc, &argv);

	if (wfb_options.kill_daemon) {
		if (kill_daemon(wfb_options.pid_file) < 0)
			exit(EXIT_FAILURE);
		exit(EXIT_SUCCESS);
	}

	if (wfb_options.query_param) {
		if (ipc_tx(wfb_options.ctrl_file, wfb_options.query_param) < 0)
			exit(EXIT_FAILURE);
		exit(EXIT_SUCCESS);
	}

	if (wfb_options.daemon) {
		if (create_daemon(wfb_options.pid_file) < 0)
			exit(EXIT_FAILURE);
	}

	p_debug("Initalizing netcore.\n");
	if (netcore_initialize(&net_ctx) < 0) {
		p_err("Cannot Initialize netcore\n");
		exit(EXIT_FAILURE);
	}

	p_debug("Initializing IPC.\n");
	if (ipc_rx_initialize(&ipc_ctx, &net_ctx, wfb_options.ctrl_file) < 0) {
		p_err("Cannot Initialize IPC.\n");
		exit(EXIT_FAILURE);
	}

	p_debug("Initalizing crypto.\n");
	if (crypto_wfb_init(wfb_options.key_file) < 0) {
		p_err("Cannot Initialize crypto\n");
		exit(EXIT_FAILURE);
	}

	p_debug("Initalizing fec.\n");
	if (fec_wfb_init() < 0) {
		p_err("Cannot Initialize FEC\n");
		exit(EXIT_FAILURE);
	}

	p_debug("Initalizing rx parser.\n");
	if (rx_context_initialize(&rx_ctx, wfb_ch) < 0) {
		p_err("Cannot Initialize Rx.\n");
		exit(EXIT_FAILURE);
	}
	msg_set_hook(rx_log_hook, &rx_ctx);

	if (wfb_options.tx_wired) {
		p_debug("Initalizing inet tx.\n");
		netinet_tx_initialize(&intx_ctx, &net_ctx, wfb_options.tx_wired);
		if (rx_context_set_mirror(&rx_ctx, netinet_tx, &intx_ctx) < 0) {
			p_err("Cannot Attach NetRx\n");
			exit(EXIT_FAILURE);
		}
	}

#ifdef ENABLE_GSTREAMER
	if (wfb_options.local_play) {
		// Create new thread and waiting for data.
		p_debug("Initalizing decoder.\n");
		if (wfb_gst_context_init_live(&gst_ctx) < 0) {
			p_err("Cannot Initialize Decoder\n");
			exit(EXIT_FAILURE);
		}
		if (wfb_gst_thread_start(&gst_ctx) < 0) {
			p_err("Cannot Start Decoder thread\n");
			exit(EXIT_FAILURE);
		}
		if (rx_context_set_decode(&rx_ctx,
		    wfb_gst_handler, &gst_ctx) < 0) {
			p_err("Cannot Attach Decoder\n");
			exit(EXIT_FAILURE);
		}
	}
#endif

	if (wfb_options.rx_wireless) {
		p_debug("Initalizing pcap rx.\n");
		fd = netpcap_initialize(&pcap_ctx, &net_ctx, &rx_ctx,
		    wfb_options.rx_wireless, wfb_options.use_monitor);
		if (fd < 0) {
			p_err("Cannot Initialize PCAP Rx\n");
			exit(EXIT_FAILURE);
		}
	}

	if (wfb_options.rx_wired) {
		p_debug("Initalizing inet rx.\n");
		fd = netinet_rx_initialize(&inrx_ctx, &net_ctx, &rx_ctx,
		    wfb_options.rx_wired);
		if (fd < 0) {
			p_err("Cannot Initialize Inet6 Rx\n");
			exit(EXIT_FAILURE);
		}
	}

	p_debug("Start netcore thread.\n");
	netcore_thread_start(&net_ctx);
	p_debug("Waiting for netcore thread complete.\n");
	netcore_thread_join(&net_ctx);
	p_debug("netcore thread completed.\n");

#ifdef ENABLE_GSTREAMER
	if (wfb_options.local_play) {
		p_debug("Waiting for local_play thread complete.\n");
		wfb_gst_eos(&gst_ctx);
		wfb_gst_thread_join(&gst_ctx);
		p_debug("local_play thread completed.\n");
	}
#endif

	if (wfb_options.rx_wired) {
		p_debug("Deinitalizing inet rx.\n");
		netinet_rx_deinitialize(&inrx_ctx);
	}
	if (wfb_options.tx_wired) {
		p_debug("Deinitalizing inet tx.\n");
		netinet_tx_deinitialize(&intx_ctx);
	}
	if (wfb_options.rx_wireless) {
		netpcap_deinitialize(&pcap_ctx);
	}
	p_debug("Deinitalizing rx parser.\n");
	rx_context_deinitialize(&rx_ctx);
	p_debug("Deinitalizing netcore.\n");
	netcore_deinitialize(&net_ctx);

	// XXX: more cleanup
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
