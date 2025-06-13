#ifndef __WFB_PARAMS_H__
#define __WFB_PARAMS_H__
#include <stdint.h>
#include <stdbool.h>

// Ring buffer
#define RX_RING_SIZE	40

// Handlers
#define RX_MAX_MIRROR	3
#define RX_MAX_DECODE	3

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

struct wfb_opt {
	const char *rx_wireless;
	const char *rx_wired;
	const char *tx_wired;
	const char *key_file;
	const char *mc_addr;
	const char *log_file;
	const char *pid_file;
	uint16_t mc_port;
	bool local_play;
	bool use_monitor;
	bool no_fec;
	bool debug;
	bool daemon;
};

extern struct wfb_opt wfb_options;

#endif /* __WFB_PARAMS_H__ */
