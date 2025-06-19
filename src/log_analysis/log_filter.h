#ifndef __LOG_FILTER_H__
#define __LOG_FILTER_H__
int log_filter_reset(struct log_store *ls);
int log_filter_dbm(struct log_store *ls, int8_t cut_off);
#endif /* __LOG_FILTER_H__ */
