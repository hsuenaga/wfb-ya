#ifndef __FRAME_RADIOTAP_H__
#define __FRAME_RADIOTAP_H__
#include <stdint.h>
#include <stdbool.h>
#include "util_attribute.h"

struct radiotap_channel {
	uint16_t freq;
	uint16_t flags;
} __packed;

struct radiotap_fhss {
	uint8_t hop_set;
	uint8_t hop_pattern;
} __packed;

struct radiotap_xchannel {
	uint32_t flags;
	uint16_t freq;
	uint8_t channel;
	uint8_t max_power;
} __packed;

struct radiotap_mcs {
	uint8_t known;
	uint8_t flags;
	uint8_t index;
} __packed;

struct radiotap_ampdu {
	uint32_t ref;
	uint16_t flags;
	uint8_t delim;
	uint8_t reserved;
} __packed;

struct radiotap_vht {
	uint16_t known;
	uint8_t flags;
	uint8_t bandwidth;
	uint8_t mcs_nss[4];
	uint8_t coding;
	uint8_t group_id;
	uint16_t partial_aid;
} __packed;

struct radiotap_timestamp {
	uint64_t timestamp;
	uint16_t accuracy;
	uint8_t unit_position;
	uint8_t flags;
} __packed;

struct radiotap_he {
	uint16_t data1;
	uint16_t data2;
	uint16_t data3;
	uint16_t data4;
	uint16_t data5;
	uint16_t data6;
} __packed;

struct radiotap_he_mu {
	uint16_t flags1;
	uint16_t flags2;
	uint8_t ru_channel1[4];
	uint8_t ru_channel2[4];
} __packed;

struct radiotap_he_mu_oth {
	uint16_t per_user_1;
	uint16_t per_user_2;
	uint8_t per_user_position;
	uint8_t per_user_known;
} __packed;

struct radiotap_lsig {
	uint16_t data1;
	uint16_t data2;
} __packed;

struct radiotap_tlv {
	uint16_t type;
	uint16_t length;
	uint8_t data[];
} __packed;

struct radiotap_s1g {
	uint16_t known;
	uint16_t data1;
	uint16_t data2;
} __packed;

struct radiotap_u_sig {
	uint32_t common;
	uint32_t value_mask;
} __packed;

struct radiotap_eht {
	uint32_t known;
	uint32_t data[9];
	uint32_t user_info[];
} __packed;

struct radiotap_context {
	bool			has_fcs;
	bool			bad_fcs;

	struct {
		// Little endian.
		uint64_t *tsft;				// bit 0
		uint8_t *flags;				// bit 1
		uint8_t *rate;				// bit 2
		struct radiotap_channel *channel;	// bit 3
		struct radiotap_fhss *fhss;		// bit 4
		int8_t *dbm_antenna_signal;		// bit 5
		int8_t *dbm_antenna_noise;		// bit 6
		uint16_t *lock_quality;			// bit 7
		uint16_t *tx_att;			// bit 8
		uint16_t *db_tx_att;			// bit 9
		int8_t *tx_power;			// bit 10
		uint8_t *antenna;			// bit 11
		uint8_t *db_antenna_signal;		// bit 12
		uint8_t *db_antenna_noise;		// bit 13
		uint16_t *rx_flags;			// bit 14
		uint16_t *tx_flags;			// bit 15(Linux)
		uint8_t *_hw_queue;			// bit 15(OpenBSD)
		uint8_t *rts_retries;			// bit 16(Linux)
		uint8_t *_rssi;				// bit 16(OpenBSD)
		uint8_t *data_retries;			// bit 17(Linux?)
		struct radiotap_xchannel *_xchannel;	// bit 18(FreeBSD/OSX)
		struct radiotap_mcs *mcs;		// bit 19
		struct radiotap_ampdu *ampdu;		// bit 20
		struct radiotap_vht *vht;		// bit 21
		struct radiotap_timestamp *timestamp;	// bit 22
		struct radiotap_he *he;			// bit 23
		struct radiotap_he_mu *he_mu;		// bit 24
		struct radiotap_he_mu_oth *he_mu_oth;	// bit 25
		uint8_t *zero_length_psdu;		// bit 26
		struct radiotap_lsig *lsig;		// bit 27
		struct radiotap_tlv *tlv;		// bit 28
		// bit 29 radiotap namespace
		// bit 30 vendor namespace
		// bit 31 extended presence
	
		struct radiotap_s1g *s1g;		// bit 32
		struct radiotap_u_sig *u_sig;		// bit 33
		struct radiotap_eht *eht;		// bit 34
	} raw;
};

extern void radiotap_context_dump(const struct radiotap_context *ctx);
extern ssize_t radiotap_frame_parse(void *data, size_t size,
    struct radiotap_context *ctx);
#endif /* __FRAME_RADIOTAP_H__ */
