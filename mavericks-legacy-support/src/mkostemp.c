/*
 * mkostemp.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── mkostemp (10.12+): mkstemp + fcntl for the O_CLOEXEC bit. */
int mkostemp(char *template_str, int flags) {
    DBG("mkostemp flags=0x%x", flags);
    int fd = mkstemp(template_str);
    if (fd < 0) return -1;
    if (flags & O_CLOEXEC) {
        int fl = fcntl(fd, F_GETFD);
        if (fl != -1) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
    }
    return fd;
}

