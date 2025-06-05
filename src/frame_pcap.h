#ifndef __FRAME_PCAP_H__
#define __FRAME_PCAP_H__
struct pcap_context {
	void *data;
	uint32_t caplen;
};

extern void pcap_context_dump(struct pcap_context *ctx);
extern ssize_t pcap_frame_parse(void *rxbuf, size_t size,
    struct pcap_context *ctx);
#endif /* __FRAME_PCAP_H__ */
