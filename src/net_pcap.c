#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <pcap.h>

#include "net_pcap.h"
#include "util_log.h"

static char _errbuf[PCAP_ERRBUF_SIZE] = { '\0' };
static pcap_t *_pcap = NULL; // singleton at this time.

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
netpcap_initialize(const char *dev, uint32_t channel_id)
{
	int rcv_buf_siz = BUFSIZ;
	int link_encap;
	const char *name, *desc;

	assert(dev);

	if (_pcap)
		return pcap_get_selectable_fd(_pcap);

	_pcap = pcap_create(dev, _errbuf);
	if (_pcap == NULL) {
		p_err("Cannot initialize pcap: %s\n", _errbuf);
		goto err;
	}
	if (pcap_set_buffer_size(_pcap, rcv_buf_siz) != 0) {
		p_err("Cannot set buffer size: %s\n", pcap_geterr(_pcap));
		goto err;
	}
	if (pcap_set_snaplen(_pcap, PCAP_MTU) != 0) {
		p_err("Cannot set snap length: %s\n", pcap_geterr(_pcap));
		goto err;
	}
	if (pcap_set_promisc(_pcap, 1) != 0) {
		p_err("Cannot set promisc: %s\n", pcap_geterr(_pcap));
		goto err;
	}
	if (pcap_set_timeout(_pcap, -1) != 0) {
		p_err("Cannot set timeout: %s\n", pcap_geterr(_pcap));
		goto err;
	}
	if (pcap_set_immediate_mode(_pcap, 1) != 0) {
		p_err("Cannot set immediate mode: %s\n", pcap_geterr(_pcap));
		goto err;
	}
	if (pcap_activate(_pcap) != 0) {
		p_err("Cannot activate: %s\n", pcap_geterr(_pcap));
		goto err;
	}
	if (pcap_setnonblock(_pcap, 1, _errbuf) != 0) {
		p_err("Cannot activate: %s\n", _errbuf);
		goto err;
	}

	link_encap = pcap_datalink(_pcap);
	name = pcap_datalink_val_to_name(link_encap);
	desc = pcap_datalink_val_to_description_or_dlt(link_encap);
	p_info("Link Encap:  %s(%s)\n", name ? name : "Unknown", desc);
	if (link_encap != DLT_IEEE802_11_RADIO) {
		p_err("Not an IEEE802.11 device.\n");
		goto err;
	}

	if (netpcap_filter_initialize(_pcap, channel_id) < 0) {
		p_err("Cannot initialize filter.\n");
		goto err;
	}

	return pcap_get_selectable_fd(_pcap);
err:
	if (_pcap)
		pcap_close(_pcap);
	_pcap = NULL;
	return -1;
}

static ssize_t
netpcap_recv(struct pcap_pkthdr **hdr, void **rxbuf)
{
	if (hdr == NULL || rxbuf == NULL)
		return -1;

	if (pcap_next_ex(_pcap, hdr, (const u_char **)rxbuf) != 1) {
		p_err("pcap_next() failed: %s\n", pcap_geterr(_pcap));
		return -1;
	}
	if ((*hdr)->caplen < (*hdr)->len) {
		p_info("packet is trancated.\n");
	}

	return (ssize_t)((*hdr)->caplen);
}

static void
netpcap_rx_one(evutil_socket_t fd, short event, void *arg)
{
	struct netpcap_context *ctx = (struct netpcap_context *)arg;
	struct pcap_pkthdr *hdr;
	void *rxbuf;
	ssize_t rxlen;

	rxlen = netpcap_recv(&hdr, &rxbuf);
	if (rxlen < 0) {
		p_err("recv_pcap() failed.\n");
		if (event_base_loopcontinue(ctx->base) < 0) {
			event_base_loopbreak(ctx->base);
		}
		return;
	}
	if (ctx->cb)
		ctx->cb(rxbuf, rxlen, ctx->cb_arg);

	return;
}

int
netpcap_rx_start(int fd, int (*cb)(void *, size_t, void *), void *arg)
{
	struct netpcap_context ctx;

	ctx.base = event_base_new();
	ctx.fifo = event_new(ctx.base, fd, EV_READ|EV_PERSIST,
	    netpcap_rx_one, (void *)&ctx);
	ctx.cb = cb;
	ctx.cb_arg = arg;

	event_add(ctx.fifo, NULL);
	event_base_dispatch(ctx.base);
	// infinite loop
	event_free(ctx.fifo);
	event_base_free(ctx.base);
	p_info("finished.\n");

	return 0;
}
