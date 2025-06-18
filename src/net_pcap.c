#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <pcap.h>

#include "net_core.h"
#include "rx_core.h"
#include "net_pcap.h"
#include "util_msg.h"

static ssize_t
netpcap_recv(pcap_t *pcap, struct pcap_pkthdr **hdr, void **rxbuf)
{
	assert(pcap);
	assert(hdr);
	assert(rxbuf);

	if (pcap_next_ex(pcap, hdr, (const u_char **)rxbuf) != 1) {
		p_err("pcap_next() failed: %s\n", pcap_geterr(pcap));
		return -1;
	}
	if ((*hdr)->caplen < (*hdr)->len) {
		p_info("packet is trancated.\n");
	}

	return (ssize_t)((*hdr)->caplen);
}

static void
netpcap_rx(evutil_socket_t fd, short event, void *arg)
{
	struct netpcap_context *ctx = (struct netpcap_context *)arg;
	struct pcap_pkthdr *hdr;
	void *rxbuf;
	ssize_t rxlen;

	rxlen = netpcap_recv(ctx->pcap, &hdr, &rxbuf);
	if (rxlen < 0) {
		p_err("recv_pcap() failed.\n");
		return;
	}

	rx_frame_pcap(ctx->rx_ctx, rxbuf, rxlen);

	return;
}

static int
netpcap_filter_initialize(pcap_t *pcap, uint32_t channel_id)
{
	struct bpf_program bpf_pg;
	char str_pg[BUFSIZ];

	assert(pcap);

	if (channel_id > 0) {
		snprintf(str_pg, sizeof(str_pg),
		    "ether[0xa:2] == 0x%04x && ether[0x0c:4] == 0x%08x",
		    WFB_SIG, channel_id);
	}
	else {
		snprintf(str_pg, sizeof(str_pg), "ether[0xa:2] == 0x%04x",
		    WFB_SIG);
	}

	if (pcap_compile(pcap, &bpf_pg, str_pg, 1, 0) == -1) {
		p_err("%s: %s\n", pcap_geterr(pcap), str_pg);
		return -1;
	}

	if (pcap_setfilter(pcap, &bpf_pg) == -1) {
		p_err("pcap_setfilter failed. %s\n", pcap_geterr(pcap));
		pcap_freecode(&bpf_pg);
		return -1;
	}

	pcap_freecode(&bpf_pg);
	return 0;
}

int
netpcap_initialize(struct netpcap_context *ctx,
    struct netcore_context *net_ctx,
    struct rx_context *rx_ctx,
    const char *dev, bool use_monitor)
{
	pcap_t *pcap = NULL;
	char errbuf[PCAP_ERRBUF_SIZE] = {'\0'};
	int rcv_buf_siz = BUFSIZ;
	int link_encap;
	const char *name, *desc;

	assert(ctx);
	assert(net_ctx);
	assert(rx_ctx);
	assert(dev);

	memset(ctx, 0, sizeof(*ctx));
	ctx->net_ctx = net_ctx;
	ctx->rx_ctx = rx_ctx;
	ctx->fd = -1;

	pcap = pcap_create(dev, errbuf);
	if (pcap == NULL) {
		p_err("Cannot initialize pcap: %s\n", errbuf);
		goto err;
	}
	if (pcap_set_buffer_size(pcap, rcv_buf_siz) != 0) {
		p_err("Cannot set buffer size: %s\n", pcap_geterr(pcap));
		goto err;
	}
	if (pcap_set_snaplen(pcap, PCAP_MTU) != 0) {
		p_err("Cannot set snap length: %s\n", pcap_geterr(pcap));
		goto err;
	}
	if (use_monitor) {
		if (pcap_set_rfmon(pcap, 1) != 0) {
			p_err("Cannot set rfmon: %s\n", pcap_geterr(pcap));
			goto err;
		}
	}
	else {
		if (pcap_set_promisc(pcap, 1) != 0) {
			p_err("Cannot set promisc: %s\n", pcap_geterr(pcap));
			goto err;
		}
	}
	if (pcap_set_timeout(pcap, 0) != 0) {
		p_err("Cannot set timeout: %s\n", pcap_geterr(pcap));
		goto err;
	}
	if (pcap_set_immediate_mode(pcap, 1) != 0) {
		p_err("Cannot set immediate mode: %s\n", pcap_geterr(pcap));
		goto err;
	}
	if (pcap_activate(pcap) != 0) {
		p_err("Cannot activate: %s\n", pcap_geterr(pcap));
		goto err;
	}
	if (pcap_setnonblock(pcap, 1, errbuf) != 0) {
		p_err("Cannot activate: %s\n", errbuf);
		goto err;
	}

	link_encap = pcap_datalink(pcap);
	name = pcap_datalink_val_to_name(link_encap);
	desc = pcap_datalink_val_to_description_or_dlt(link_encap);
	p_debug("Link Encap:  %s(%s)\n", name ? name : "Unknown", desc);
	if (link_encap != DLT_IEEE802_11_RADIO) {
		p_err("Not an IEEE802.11 device.\n");
		goto err;
	}

	if (netpcap_filter_initialize(pcap, rx_ctx->channel_id) < 0) {
		p_err("Cannot initialize filter.\n");
		goto err;
	}

	ctx->pcap = pcap;
	ctx->fd = pcap_get_selectable_fd(pcap);
	ctx->ev = netcore_rx_event_add(net_ctx, ctx->fd, netpcap_rx, ctx);
	if (ctx->ev == NULL) {
		p_err("Cannot register event.^n");
		goto err;
	}

	return ctx->fd;
err:
	if (pcap)
		pcap_close(pcap);
	return -1;
}

void
netpcap_deinitialize(struct netpcap_context *ctx)
{
	assert(ctx);

	if (ctx->ev) {
		netcore_rx_event_del(ctx->net_ctx, ctx->ev);
		ctx->ev = NULL;
	}
	if (ctx->pcap) {
		pcap_close(ctx->pcap);
		ctx->pcap = NULL;
		ctx->fd = -1;
	}
}
