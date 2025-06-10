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
#else
/* Linux */
#include <endian.h>
#define DEF_WRX "wlan1"
#define DEF_ERX NULL
#define DEF_ETX NULL
#define DEF_KEY "./gs.key"
extern char *basename_r(const char *path, char *bname);
#endif

#ifndef NELEMS
#define NELEMS(x) (sizeof((x))/sizeof((x)[0]))
#endif

#ifndef timespecclear
#define	timespecclear(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#endif

#ifndef timespecisset
#define	timespecisset(tvp)	((tvp)->tv_sec || (tvp)->tv_nsec)
#endif

#ifndef timespeccmp
#define	timespeccmp(tvp, uvp, cmp)					\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	((tvp)->tv_nsec cmp (uvp)->tv_nsec) :				\
	((tvp)->tv_sec cmp (uvp)->tv_sec))
#endif

#ifndef timespecadd
#define	timespecadd(tvp, uvp, vvp)					\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec		\
		(vvp)->tv_nsec = (tvp)->tv_nsec + (uvp)->tv_nsec;	\
		if ((vvp)->tv_nsec >= 1000000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_nsec -= 1000000000;			\
		}							\
	} while (0)
#endif

#ifndef timespecsub
#define	timespecsub(tvp, uvp, vvp)					\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_nsec = (tvp)->tv_nsec - (uvp)->tv_nsec;	\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)
#endif

#endif /* __COMPAT_H__ */
