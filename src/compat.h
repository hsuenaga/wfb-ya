#ifndef __COMPAT_H__
#define __COMPAT_H__
#ifdef __APPLE__
/* Mac OS X */
#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#define DEF_WRX NULL
#define DEF_ERX NULL
#define DEF_ETX NULL
#define DEF_KEY "./gs.key"
#define	timespecsub(tsp, usp, vsp)				\
do {								\
	(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
	(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
	if ((vsp)->tv_nsec < 0) {				\
		(vsp)->tv_sec--;				\
		(vsp)->tv_nsec += 1000000000L;			\
	}							\
} while (/* CONSTCOND */ 0)
#else
/* Linux */
#include <endian.h>
#define DEF_WRX "wlan1"
#define DEF_ERX NULL
#define DEF_ETX NULL
#define DEF_KEY "./gs.key"
extern char *basename_r(const char *path, char *bname);
#define	timespecsub(tsp, usp, vsp)				\
do {								\
	(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
	(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
	if ((vsp)->tv_nsec < 0) {				\
		(vsp)->tv_sec--;				\
		(vsp)->tv_nsec += 1000000000L;			\
	}							\
} while (/* CONSTCOND */ 0)
#endif

#endif /* __COMPAT_H__ */
