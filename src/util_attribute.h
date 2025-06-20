#ifndef __UTIL_ATTRIBUTE_H__
#define __UTIL_ATTRIBUTE_H__

#define __packed __attribute__((packed))
#define __printf __attribute__((format(printf, 1, 2)))
#define __fprintf __attribute__((format(printf, 2, 3)))
#ifndef __printflike
#define __printflike(x, y) __attribute__((format(printf, (x), (y))))
#endif
#ifndef __unused
#define __unused __attribute__((unused))
#endif

#endif /* __UTIL_ATTRIBUTE_H__ */
