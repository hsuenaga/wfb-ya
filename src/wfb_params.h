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
#define WFB_PORT	"5742"
#define MAX_BLOCK_IDX	((1LLU << 55) - 1)

// Process Control
#define DEF_KEY_FILE "gs.key"
#define DEF_PID_FILE "/var/run/wfb_listener.pid"
#define DEF_CTRL_FILE "/var/run/wfb_listener.socket"

struct wfb_opt {
	const char *rx_wireless;
	const char *rx_wired;
	const char *tx_wired;
	const char *key_file;
	const char *mc_addr;
	const char *log_file;
	const char *pid_file;
	const char *ctrl_file;
	const char *query_param;
	const char *mc_port;
	bool local_play;
	bool rssi_overlay;
	bool use_monitor;
	bool no_fec;
	bool debug;
	bool daemon;
	bool kill_daemon;
	bool use_dns;
};

struct wfb_statistics {
	/* pcap */
	uint64_t pcap_libpcap_frame_error;
	uint64_t pcap_radiotap_frame_error;
	uint64_t pcap_bad_fcs;
	uint64_t pcap_80211_frame_error;
	uint64_t pcap_invalid_channel_id;
	uint64_t pcap_wfb_frame_error;
	uint64_t pcap_accept;

	/* UDP MC */
	uint64_t mc_udp_frame_error;
	uint64_t mc_udp_corrupted_frames;
	uint64_t mc_udp_wfb_frame_error;
	uint64_t mc_accept;

	/* IPC */
	uint64_t ipc_success;
	uint64_t ipc_error;

	/* handlers */
	uint64_t mirrored_frames;
	uint64_t decoded_frames;
	uint64_t reload;

	/* signals */
	uint64_t sighup;
};

extern struct wfb_opt wfb_options;
extern struct wfb_statistics wfb_stats;

#endif /* __WFB_PARAMS_H__ */
