#ifndef __NET_PCAP_H__
#define __NET_PCAP_H__
#include <stdint.h>
#include <pcap.h>
#include <event2/event.h>

#define WIFI_MTU	4045
#define RTAP_SIZ	256 // RTL8812AU/EU
#define PCAP_MTU	(WIFI_MTU + RTAP_SIZ)
#define WFB_SIG		0x5742

struct netpcap_context {
	struct event_base *base;
	struct event *fifo;

	int (*cb)(void *data, size_t size, void *arg);
	void *cb_arg;
};

extern int netpcap_initialize(const char *dev, uint32_t channel_id);
extern int netpcap_capture_start(int fd,
    int(*cb)(void *, size_t, void *), void *cb_arg);

#endif /* __NET_PCAP_H__ */
