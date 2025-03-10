#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net_pcap.h"
#include "rx_core.h"
#include "crypto_wfb.h"
#include "fec_wfb.h"
#include "util_log.h"

int
main(int argc, char *argv[])
{
	struct rx_context ctx;
	int fd;

	fd = netpcap_initialize("wlan1", 0);
	if (fd < 0) {
		p_err("Cannot Initialize pcap\n");
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

	rx_context_init(&ctx);
	if (netpcap_rx_start(fd, rx_frame, &ctx) < 0)
		exit(0);

	exit(1);
}
