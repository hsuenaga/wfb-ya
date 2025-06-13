#ifndef __LOG_JSON_H__
#define __LOG_JSON_H__
#include <stdio.h>
#include "log_raw.h"

enum json_keys_enum {
	KEY_SQ, /* sequence */
	KEY_TS, /* timestamp */
	KEY_BI, /* block index */
	KEY_FI, /* fragment index */
	KEY_SN, /* source node */
	KEY_FQ, /* frequency */
	KEY_DB, /* dbm */
	KEY_DS, /* data size */
	KEY_IS_FEC, /* fec applied */
	KEY_TYPE, /* event type */
	KEY_N_ETHER, /* number of ethernet frames */
	KEY_N_H265, /* number of h265 frames */
	KEY_IS_PARITY, /* is parity frame */
	KEY_IS_LOST, /* has lost frame */
};
extern const char *json_keys[];

extern int json_serialize(FILE *fp, struct log_store *ls);
extern int json_serialize_block(FILE *fp, struct log_store *ls);
#endif /* __LOG_JSON_H__ */
