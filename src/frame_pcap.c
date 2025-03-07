#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <event2/event-config.h>
#include <event2/event.h>

#include "net_pcap.h"
#include "frame_pcap.h"
#include "frame_radiotap.h"
#include "frame_ieee80211.h"
#include "frame_wfb.h"
#include "util_log.h"

void
pcap_context_dump(struct pcap_context *ctx)
{
	assert(ctx);

	p_info("Capture Data: 0x%p\n", ctx->data);
	p_info("Capture Length: %u\n", ctx->caplen);
}

ssize_t
pcap_frame_parse(void *rxbuf, size_t rxlen, struct pcap_context *ctx)
{
	assert(rxbuf);
	assert(ctx);

	ctx->data = rxbuf;
	ctx->caplen = rxlen;

	// XXX: pcap header is not allocated in the rxbuf.
	return 0;
}
