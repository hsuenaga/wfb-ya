#ifndef __LOG_MESSAGE_H__
#define __LOG_MESSAGE_H__
#include <stdio.h>
#include "log_raw.h"

extern int dump_message(FILE *fp, struct log_store *ls);
#endif /* __LOG_MESSAGE_H__ */
