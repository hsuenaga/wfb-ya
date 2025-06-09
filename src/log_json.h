#ifndef __LOG_JSON_H__
#define __LOG_JSON_H__
#include <stdio.h>
#include "log_raw.h"

enum json_keys_enum {
	KEY_SQ,
	KEY_TS,
	KEY_BI,
	KEY_FI,
	KEY_SN,
	KEY_FQ,
	KEY_DB,
	KEY_DS,
	KEY_IS_FEC,
	KEY_TYPE,
};
extern const char *json_keys[];

extern int json_serialize(FILE *fp, struct log_store *ls);
#endif /* __LOG_JSON_H__ */
