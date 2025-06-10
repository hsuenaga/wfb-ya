#ifndef __LOG_SUMMARY_H__
#define __LOG_SUMMARY_H__
#include <stdio.h>
#include "log_raw.h"

int
summary_output(FILE *fp, struct log_store *ls);
#endif /* __LOG_SUMMARY_H__ */
