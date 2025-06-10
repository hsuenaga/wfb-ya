#ifndef __LOG_CSV_H__
#define __LOG_CSV_H__
#include <stdio.h>
#include "log_raw.h"

int csv_serialize(FILE *fp, struct log_store *ls);
#endif /* __LOG_CSV_H__ */
