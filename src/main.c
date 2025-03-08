#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_core.h"
#include "net_pcap.h"
#include "net_inet6.h"
#include "rx_core.h"
#include "crypto_wfb.h"
#include "fec_wfb.h"
#include "decode_h265.h"
#include "util_log.h"

int
main(int argc, char *argv[])
{
	struct netcore_context net_ctx;
	struct netpcap_context pcap_ctx;
	struct netinet6_context in6_ctx;
	struct rx_context rx_ctx;
	struct decode_h265_context d_ctx;
	uint32_t wfb_ch = 0;
	int fd;

	debug = 1;

	if (netcore_initialize(&net_ctx) < 0) {
		p_err("Cannot Initialize netcore\n");
		exit(0);
	}

	if (crypto_wfb_init("./gs.key") < 0) {
		p_err("Cannot Initialize crypto\n");
		exit(0);
	}

	if (fec_wfb_init() < 0) {
		p_err("Cannot Initialize FEC\n");
		exit(0);
	}

	if (decode_h265_context_init(&d_ctx) < 0) {
		p_err("Cannot Initialize Decoder\n");
		exit(0);
	}

#if 0
	if (rx_context_init(&rx_ctx, wfb_ch, decode_h265, &d_ctx) < 0) {
		p_err("Cannot Initialize Rx\n");
		exit(0);
	}
#else
	if (rx_context_init(&rx_ctx, wfb_ch, netinet6_tx, &in6_ctx) < 0) {
		p_err("Cannot Initialize Rx\n");
		exit(0);
	}
#endif

	fd = netpcap_initialize(&pcap_ctx, &net_ctx, &rx_ctx, "wlan1", wfb_ch);
	if (fd < 0) {
		p_err("Cannot Initialize pcap\n");
		exit(0);
	}

	fd = netinet6_initialize(&in6_ctx, &net_ctx, &rx_ctx, "eth0", wfb_ch);
	if (fd < 0) {
		p_err("Cannot Initialize inet6\n");
		exit(0);
	}

	netcore_thread_start(&net_ctx);
	netcore_thread_join(&net_ctx);

	netcore_deinitialize(&net_ctx);
	netpcap_deinitialize(&pcap_ctx);
	netinet6_deinitialize(&in6_ctx);

	// XXX: more cleanup
	exit(1);
}
