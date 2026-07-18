/*
 * fd_set_overflow.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── __darwin_check_fd_set_overflow: newer ABI check. No-op return 1 = "ok". */
int __darwin_check_fd_set_overflow(int fd, void *set, int unlimited_select) {
    DBG("fd_set_overflow_check fd=%d", fd);
    (void)set; (void)unlimited_select;
    return 1;
}

