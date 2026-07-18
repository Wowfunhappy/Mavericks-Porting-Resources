/*
 * preadv_pwritev_nocancel.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── preadv/pwritev $NOCANCEL variants: use the non-cancellable by aliasing. */
ssize_t preadv$NOCANCEL(int fd, const struct iovec *iov, int iovcnt, off_t offset) __asm("_preadv$NOCANCEL");
ssize_t preadv$NOCANCEL(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    off_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = pread(fd, iov[i].iov_base, iov[i].iov_len, offset + total);
        if (n < 0) return total ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}
ssize_t pwritev$NOCANCEL(int fd, const struct iovec *iov, int iovcnt, off_t offset) __asm("_pwritev$NOCANCEL");
ssize_t pwritev$NOCANCEL(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    off_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = pwrite(fd, iov[i].iov_base, iov[i].iov_len, offset + total);
        if (n < 0) return total ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

