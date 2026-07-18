/*
 * ioctl_winsize.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── TIOCGWINSZ shim: on macOS 10.9, `ioctl(TIOCGWINSZ, 0, ...)` on an
 * inherited stdin that points at a pty returns rows=0, cols=0 — even when the
 * same pty queried via fd=1 or fd=2 reports the correct dimensions. Bun's
 * TTY runtime reads stdin's size first; Ink sees a 0×0 "terminal" and aborts
 * the TUI mount. Fall through to fd=1 (or fd=2) when fd=0 reports zero size. */
#include <sys/ioctl.h>
#include <termios.h>
struct _shim_winsize {
    unsigned short rows, cols, xpixel, ypixel;
};
int ioctl_wrapper(int fd, unsigned long req, void *arg) __asm("_ioctl");
int ioctl_wrapper(int fd, unsigned long req, void *arg) {
    static int (*real_ioctl)(int, unsigned long, void *) = NULL;
    if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    int rc = real_ioctl(fd, req, arg);
    if (req == TIOCGWINSZ && rc == 0 && arg) {
        struct _shim_winsize *ws = (struct _shim_winsize *)arg;
        if (ws->rows == 0 && ws->cols == 0) {
            /* Try other standard fds; the tty device is shared and one of them
             * typically has the correct size even when stdin reports 0. */
            int try_fd[] = { fd == 0 ? 1 : 0, fd == 2 ? 1 : 2 };
            for (int i = 0; i < 2; i++) {
                int alt = try_fd[i];
                if (alt == fd) continue;
                int rc2 = real_ioctl(alt, req, arg);
                if (rc2 == 0 && (ws->rows || ws->cols)) {
                    DBG("TIOCGWINSZ fd=%d was 0x0, fd=%d gave %ux%u",
                        fd, alt, ws->cols, ws->rows);
                    return 0;
                }
            }
            /* Last resort: open /dev/tty and query that. */
            int tty = open("/dev/tty", 0);
            if (tty >= 0) {
                int rc3 = real_ioctl(tty, req, arg);
                close(tty);
                if (rc3 == 0 && (ws->rows || ws->cols)) {
                    DBG("TIOCGWINSZ fd=%d was 0x0, /dev/tty gave %ux%u",
                        fd, ws->cols, ws->rows);
                    return 0;
                }
            }
            /* Still zero — fall through. Callers treat 0x0 as "detached"
             * and degrade, which is at least a consistent state. */
            DBG("TIOCGWINSZ fd=%d: nothing has a real size", fd);
        }
    }
    return rc;
}

