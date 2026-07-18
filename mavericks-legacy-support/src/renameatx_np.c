/*
 * renameatx_np.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── renameatx_np (10.12+): renameat + best-effort flag handling. */
#define RENAME_SWAP    0x0002
#define RENAME_EXCL    0x0004
int renameatx_np(int fromfd, const char *from, int tofd, const char *to, unsigned int flags) {
    if (flags & RENAME_EXCL) {
        struct stat st;
        if (fstatat(tofd, to, &st, 0) == 0) { errno = EEXIST; return -1; }
    }
    return renameat(fromfd, from, tofd, to);
}

