#ifndef __WFB_PARAMS_H__
#define __WFB_PARAMS_H__

// Ring buffer
#define RX_RING_SIZE	40

// Radiotap
#define RTAP_SIZ	256 // RTL8812AU/EU

// Frame sizes
#define WIFI_MTU	4045
#define PCAP_MTU	(WIFI_MTU + RTAP_SIZ)
#define INET6_MTU	PCAP_MTU

// WFB Protocol
#define WFB_SIG		0x5742
#define WFB_ADDR6	"ff02::5742"
#define WFB_PORT	5742
#define MAX_BLOCK_IDX	((1LLU << 55) - 1)

#endif /* __WFB_PARAMS_H__ */
