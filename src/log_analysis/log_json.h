#ifndef __LOG_JSON_H__
#define __LOG_JSON_H__
#include <stdio.h>
#include "log_raw.h"

extern int json_serialize(FILE *fp, struct log_store *ls);
extern int json_serialize_block(FILE *fp, struct log_store *ls);
#endif /* __LOG_JSON_H__ */
