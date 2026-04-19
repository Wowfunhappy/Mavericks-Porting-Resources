/* sys/random.h — compat for macOS < 10.12 */
#ifndef _COMPAT_SYS_RANDOM_H_
#define _COMPAT_SYS_RANDOM_H_
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int getentropy(void *buf, size_t buflen);
#ifdef __cplusplus
}
#endif
#endif
