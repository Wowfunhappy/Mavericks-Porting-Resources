/*
 * mav_shim_debug.h -- shared plumbing for the libSystem shims that were split
 * out of the former standalone modern_api_polyfills.c and folded into this
 * library (one shim per src file: ulock.c, kevent64_shim.c, dlopen_interpose.c,
 * write_underline.c, posix_spawn_chdir.c, ...).
 *
 * It carries (a) the common system includes those shims share and (b) the DBG
 * tracing helper they all use. Tracing is toggled at runtime by setting
 * MAV_PATCH_DEBUG=1 in the environment; every message is prefixed "[patch] ".
 *
 * dbg_on() caches the env lookup in a function-local static, so each
 * translation unit that includes this header gets its own private copy with no
 * cross-file symbol (nothing is exported from here).
 */
#ifndef MAV_SHIM_DEBUG_H
#define MAV_SHIM_DEBUG_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/event.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/time.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/message.h>
#include <mach/port.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <sys/param.h>  /* PATH_MAX */
#include <crt_externs.h>

static inline int dbg_on(void) {
    static int g = -1;
    if (g < 0) g = (getenv("MAV_PATCH_DEBUG") != NULL);
    return g;
}
#define DBG(fmt, ...) do { if (dbg_on()) fprintf(stderr, "[patch] " fmt "\n", ##__VA_ARGS__); } while (0)

#endif /* MAV_SHIM_DEBUG_H */
