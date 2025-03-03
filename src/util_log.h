#ifndef __UTIL_LOG_H__
#define __UTIL_LOG_H__
#include <stdarg.h>

extern void __p_info(const char *fmt, ...);
extern void __p_err(const char *fmt, ...);

#define p_info(fmt, ...) do { \
	__p_info(fmt, ##__VA_ARGS__); \
} while (0);

#define p_err(fmt, ...) do { \
	__p_err("%s: ", __func__); \
	__p_err(fmt, ##__VA_ARGS__); \
} while (0);

#endif /* __UTIL_LOG_H__ */
