/*
 * write_underline.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── write() shim: cancel the spurious underline that 10.9's Terminal.app
 * turns on when an app emits CSI >4m. The sequence is XTMODKEYS reset — a
 * private-mode SGR used by modern TUIs (Ink, kitty-aware readlines, etc.)
 * which terminals newer than ~10.11 parse and ignore, but 10.9's Terminal
 * skips the ">" and treats the remaining "4m" as SGR 4 (enable underline),
 * so every line after the prelude comes out underlined.
 *
 * Append CSI 24m (explicit underline-off) after every occurrence, matching
 * the defensive sed fix that works at the JS source level:
 *
 *   sed 's/">4m"/&+"\\x1b[24m"/' cli.js
 *
 * — but done at the libc boundary so the fix doesn't depend on bundle
 * layout. Fast path skips any write that doesn't contain an ESC byte;
 * large writes (>8KB) are also skipped since terminal-control writes are
 * small. */
#include <string.h>
static ssize_t write_inject_cancel(int fd, const char *buf, size_t n,
                                   ssize_t (*real)(int, const void *, size_t)) {
    static const char needle[] = "\x1b[>4m";
    static const size_t needle_len = sizeof(needle) - 1;
    static const char inject[] = "\x1b[24m";
    static const size_t inject_len = sizeof(inject) - 1;

    /* Count occurrences. */
    size_t count = 0;
    const char *scan = buf;
    const char *end = buf + n;
    while (scan + needle_len <= end) {
        const char *m = memmem(scan, end - scan, needle, needle_len);
        if (!m) break;
        count++;
        scan = m + needle_len;
    }
    if (count == 0) return real(fd, buf, n);

    size_t newsize = n + count * inject_len;
    char stackbuf[1024];
    char *newbuf = newsize <= sizeof(stackbuf) ? stackbuf : malloc(newsize);
    if (!newbuf) return real(fd, buf, n);  /* best-effort fallback */

    char *dst = newbuf;
    const char *src = buf;
    while (src < end) {
        const char *m = memmem(src, end - src, needle, needle_len);
        if (!m) {
            memcpy(dst, src, end - src);
            dst += end - src;
            break;
        }
        size_t before = m - src + needle_len;
        memcpy(dst, src, before);
        dst += before;
        memcpy(dst, inject, inject_len);
        dst += inject_len;
        src = m + needle_len;
    }

    /* Write the whole injected buffer. Loop on partial / EINTR so the
     * caller sees its full n bytes as accepted. */
    size_t written = 0;
    ssize_t last = 0;
    while (written < newsize) {
        last = real(fd, newbuf + written, newsize - written);
        if (last < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (last == 0) break;
        written += (size_t)last;
    }
    if (newbuf != stackbuf) free(newbuf);

    if (written >= newsize) return (ssize_t)n;
    if (written == 0 && last < 0) return last;
    /* Partial write — report original-byte progress proportional to the
     * injected-byte progress. Rare for TTYs; approximation is OK. */
    return (ssize_t)((written * n) / newsize);
}

ssize_t write_wrapper(int fd, const void *buf, size_t n) __asm("_write");
ssize_t write_wrapper(int fd, const void *buf, size_t n) {
    static ssize_t (*real)(int, const void *, size_t) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "write");
    if (n < 5 || n > 8192 || !memchr(buf, 0x1b, n)) return real(fd, buf, n);
    return write_inject_cancel(fd, (const char *)buf, n, real);
}

ssize_t write_nocancel_wrapper(int fd, const void *buf, size_t n) __asm("_write$NOCANCEL");
ssize_t write_nocancel_wrapper(int fd, const void *buf, size_t n) {
    static ssize_t (*real)(int, const void *, size_t) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "write$NOCANCEL");
    if (n < 5 || n > 8192 || !memchr(buf, 0x1b, n)) return real(fd, buf, n);
    return write_inject_cancel(fd, (const char *)buf, n, real);
}

