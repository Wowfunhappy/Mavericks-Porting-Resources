/*
 * Polyfills for modern-macOS libSystem APIs that 10.9 doesn't provide, for
 * use when running single-file binaries built against a 10.14+ SDK (Bun,
 * modern Zig/Rust outputs, etc.) on Mavericks. Designed to be linked into
 * a wrapper dylib that sits between the binary and 10.9's libSystem.
 *
 * Covered:
 *   - __ulock_wait / __ulock_wait2 / __ulock_wake (10.12+ futex)
 *   - mkostemp (10.12+)
 *   - posix_spawn_file_actions_add{chdir,fchdir}_np (10.15+) — honored via
 *     chdir-under-lock in a posix_spawn/posix_spawnp wrapper
 *   - preadv$NOCANCEL / pwritev$NOCANCEL
 *   - recvmsg_x / sendmsg_x (10.10+ vector send/recv)
 *   - renameatx_np (10.12+)
 *   - timingsafe_bcmp
 *   - __darwin_check_fd_set_overflow
 *   - TIOCGWINSZ shim (fd=0 returning 0x0 falls back to fd=1 / /dev/tty)
 *   - write() / write$NOCANCEL shim that appends CSI 24m after any CSI >4m
 *     in output (10.9 Terminal misparses the XTMODKEYS-reset private-mode
 *     sequence as SGR 4 = underline on; the append cancels the accident)
 *   - kevent64 KEVENT_FLAG_ERROR_EVENTS shim with event stashing (fixes
 *     uSockets connect-completion loss on 10.9's kqueue, and Bun stdin
 *     EV_DISPATCH events that would otherwise be dropped)
 *   - os_signpost no-ops, os_unfair_lock_assert_owner no-op
 *   - pthread_self_is_exiting_np / pthread_set_qos_class_self_np
 *
 * Debug tracing: set MAV_PATCH_DEBUG=1 in the environment.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/event.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/time.h>
#include <mach/mach_time.h>
#include <crt_externs.h>
#define environ (*_NSGetEnviron())

/* Debug log toggled by MAV_PATCH_DEBUG env var. */
static int g_dbg = -1;
static inline int dbg_on(void) {
    if (g_dbg < 0) g_dbg = (getenv("MAV_PATCH_DEBUG") != NULL);
    return g_dbg;
}
#define DBG(fmt, ...) do { if (dbg_on()) fprintf(stderr, "[patch] " fmt "\n", ##__VA_ARGS__); } while (0)

/* ── signposts: tracing no-ops ────────────────────────────────────────── */
void _os_signpost_emit_with_name_impl(void) { DBG("signpost_emit"); }
int os_signpost_enabled(void *log) { (void)log; DBG("signpost_enabled"); return 0; }

/* ── os_unfair_lock_assert_owner: legacy-support has lock/unlock/trylock but
 * not the owner-assert. Stub as a no-op (the check would abort on mismatch). */
typedef struct { uint32_t _os_unfair_lock_opaque; } os_unfair_lock, *os_unfair_lock_t;
void os_unfair_lock_assert_owner(os_unfair_lock_t lock) { (void)lock; }

/* ── ulock (10.12+) is a futex-like kernel primitive. 10.9's kernel lacks it.
 * Darwin's __ulock_wait returns -errno (negative int); Bun's Zig runtime
 * panics if it gets anything it doesn't recognize. Emulate by spin-polling
 * the address with exponential-ish backoff — no cross-thread coordination, so
 * we can't deadlock our own stub. Inefficient but correct: we wake when the
 * caller's atomic value changes. */
#include <sys/time.h>

#define UL_OPERATION_MASK           0x000000FF
#define UL_COMPARE_AND_WAIT         1
#define UL_COMPARE_AND_WAIT_SHARED  3
#define UL_COMPARE_AND_WAIT64       5

int __ulock_wait2(uint32_t op, void *addr, uint64_t value, uint64_t timeout_ns, uint64_t value2) {
    (void)value2;
    int is_64bit = (op & UL_OPERATION_MASK) == UL_COMPARE_AND_WAIT64;
    DBG("ulock_wait op=0x%x addr=%p val=%llu timeout=%lluns", op, addr, value, timeout_ns);

    /* Tight polling — low latency so Bun's inter-thread signals (especially
     * for TTY I/O and render-tick) don't stall. */
    uint64_t elapsed_ns = 0;
    useconds_t sleep_us = 1;
    for (;;) {
        uint64_t cur = is_64bit ? *(volatile uint64_t *)addr
                                : *(volatile uint32_t *)addr;
        if (cur != value) return 0;
        if (timeout_ns && elapsed_ns >= timeout_ns) return -ETIMEDOUT;
        usleep(sleep_us);
        elapsed_ns += (uint64_t)sleep_us * 1000;
        if (sleep_us < 50) sleep_us *= 2;   /* cap at 50us */
    }
}

int __ulock_wake(uint32_t op, void *addr, uint64_t wake_value) {
    (void)op; (void)addr; (void)wake_value;
    /* Waiters poll the memory themselves; no wake needed. */
    return 0;
}

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

/* ── posix_spawn chdir actions (10.15+): record the requested path but there's
 * no kernel support, so the child won't actually chdir. Return 0 so callers
 * that only check the action-builder's return code don't bail out. */
/* Track chdir requests keyed on the posix_spawn_file_actions_t pointer. When
 * posix_spawn is called, our wrapper below looks up the path and does a real
 * fork/chdir/execve instead. Tiny static table (8 entries) since we don't
 * expect many concurrent spawns. */
#define CHDIR_TABLE_SIZE 16
static struct {
    const void *acts;
    char path[1024];
    int fd;              /* -1 if chdir by path, else fchdir */
} g_chdir_table[CHDIR_TABLE_SIZE];
static pthread_mutex_t g_chdir_mu = PTHREAD_MUTEX_INITIALIZER;

static void chdir_set(const void *acts, const char *path, int fd) {
    pthread_mutex_lock(&g_chdir_mu);
    for (int i = 0; i < CHDIR_TABLE_SIZE; i++) {
        if (g_chdir_table[i].acts == NULL || g_chdir_table[i].acts == acts) {
            g_chdir_table[i].acts = acts;
            g_chdir_table[i].fd = fd;
            if (path) { strncpy(g_chdir_table[i].path, path, sizeof(g_chdir_table[i].path)-1); g_chdir_table[i].path[sizeof(g_chdir_table[i].path)-1] = 0; }
            else      { g_chdir_table[i].path[0] = 0; }
            break;
        }
    }
    pthread_mutex_unlock(&g_chdir_mu);
}
static int chdir_get(const void *acts, char *path_out, size_t plen, int *fd_out) {
    pthread_mutex_lock(&g_chdir_mu);
    int found = 0;
    for (int i = 0; i < CHDIR_TABLE_SIZE; i++) {
        if (g_chdir_table[i].acts == acts) {
            if (path_out) { strncpy(path_out, g_chdir_table[i].path, plen); path_out[plen-1] = 0; }
            if (fd_out) *fd_out = g_chdir_table[i].fd;
            g_chdir_table[i].acts = NULL;    /* consume */
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_chdir_mu);
    return found;
}

int posix_spawn_file_actions_addchdir_np(void *acts, const char *path) {
    DBG("posix_spawn_addchdir path=%s acts=%p", path, acts);
    chdir_set(acts, path, -1);
    return 0;
}
int posix_spawn_file_actions_addfchdir_np(void *acts, int fd) {
    DBG("posix_spawn_addfchdir fd=%d acts=%p", fd, acts);
    chdir_set(acts, NULL, fd);
    return 0;
}

/* Override posix_spawn/spawnp to honor pending chdir. If a chdir is registered
 * for the given actions, fall back to fork/chdir/execve so the child's CWD is
 * actually changed. Otherwise delegate to the real libSystem impl via
 * RTLD_NEXT. */
#include <dlfcn.h>
typedef int (*spawn_fn)(pid_t *, const char *, const void *, const void *,
                        char *const[], char *const[]);

/* Serialize posix_spawn when a chdir is requested: briefly change the
 * parent's CWD, call the real posix_spawn (so all file-actions and attrs get
 * handled by libSystem — stdin/stdout pipes, signal masks, process groups,
 * etc.), then restore. Children fork() inside posix_spawn, so they inherit
 * our transient CWD without other threads racing on it (we hold the lock).
 *
 * Trade-off: concurrent spawns with different chdirs are serialized, and any
 * thread that calls getcwd() during the window sees the transient path.
 * Acceptable in practice — this cost only applies to spawns that actually
 * request a chdir, and Bun doesn't do a high-frequency spawn workload. */
static pthread_mutex_t g_spawn_cwd_mu = PTHREAD_MUTEX_INITIALIZER;

static int spawn_common(spawn_fn real, pid_t *pid, const char *path_or_file,
                        const void *acts, const void *attrs,
                        char *const argv[], char *const envp[], int searchpath) {
    (void)searchpath;
    char cd[1024] = {0};
    int cd_fd = -1;
    int have_cd = acts ? chdir_get(acts, cd, sizeof(cd), &cd_fd) : 0;

    if (!have_cd || (cd[0] == 0 && cd_fd < 0))
        return real(pid, path_or_file, acts, attrs, argv, envp);

    if (dbg_on()) {
        fprintf(stderr, "[patch] spawn_wrapper real+chdir path=%s cwd=%s fd=%d argv=",
                path_or_file, cd, cd_fd);
        for (int i = 0; argv && argv[i]; i++) fprintf(stderr, " %s", argv[i]);
        fprintf(stderr, "\n");
    }
    pthread_mutex_lock(&g_spawn_cwd_mu);
    int saved_fd = open(".", O_RDONLY);
    if (saved_fd < 0) {
        pthread_mutex_unlock(&g_spawn_cwd_mu);
        return errno;
    }
    int chdir_rc = (cd_fd >= 0) ? fchdir(cd_fd) : chdir(cd);
    if (chdir_rc < 0) {
        int e = errno;
        close(saved_fd);
        pthread_mutex_unlock(&g_spawn_cwd_mu);
        return e;
    }
    int rc = real(pid, path_or_file, acts, attrs, argv, envp);
    if (fchdir(saved_fd) < 0) {
        /* Best-effort: log but don't fail the spawn (child already forked). */
        DBG("spawn_wrapper WARN: failed to restore cwd: %s", strerror(errno));
    }
    close(saved_fd);
    pthread_mutex_unlock(&g_spawn_cwd_mu);
    return rc;
}

int posix_spawn_wrapper(pid_t *pid, const char *path, const void *acts,
                        const void *attrs, char *const argv[], char *const envp[]) __asm("_posix_spawn");
int posix_spawn_wrapper(pid_t *pid, const char *path, const void *acts,
                        const void *attrs, char *const argv[], char *const envp[]) {
    static spawn_fn real = NULL;
    if (!real) real = (spawn_fn)dlsym(RTLD_NEXT, "posix_spawn");
    return spawn_common(real, pid, path, acts, attrs, argv, envp, 0);
}
int posix_spawnp_wrapper(pid_t *pid, const char *file, const void *acts,
                         const void *attrs, char *const argv[], char *const envp[]) __asm("_posix_spawnp");
int posix_spawnp_wrapper(pid_t *pid, const char *file, const void *acts,
                         const void *attrs, char *const argv[], char *const envp[]) {
    static spawn_fn real = NULL;
    if (!real) real = (spawn_fn)dlsym(RTLD_NEXT, "posix_spawnp");
    return spawn_common(real, pid, file, acts, attrs, argv, envp, 1);
}

/* ── pthread extras */
int pthread_self_is_exiting_np(void) { return 0; }
int pthread_set_qos_class_self_np(unsigned int qos_class, int relative_priority) {
    (void)qos_class; (void)relative_priority; return 0;
}

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

/* ── recvmsg_x / sendmsg_x (10.10+): vectorized msghdr batch. Loop on recvmsg/sendmsg. */
struct msghdr_x {
    void *msg_name;
    uint32_t msg_namelen;
    struct iovec *msg_iov;
    int msg_iovlen;
    void *msg_control;
    uint32_t msg_controllen;
    int msg_flags;
    size_t msg_datalen;
};
ssize_t recvmsg_x(int s, struct msghdr_x *msgp, unsigned int cnt, int flags) {
    DBG("recvmsg_x fd=%d cnt=%u flags=0x%x", s, cnt, flags);
    ssize_t total = 0;
    for (unsigned int i = 0; i < cnt; i++) {
        struct msghdr mh = {0};
        mh.msg_name = msgp[i].msg_name;
        mh.msg_namelen = msgp[i].msg_namelen;
        mh.msg_iov = msgp[i].msg_iov;
        mh.msg_iovlen = msgp[i].msg_iovlen;
        mh.msg_control = msgp[i].msg_control;
        mh.msg_controllen = msgp[i].msg_controllen;
        mh.msg_flags = msgp[i].msg_flags;
        ssize_t n = recvmsg(s, &mh, flags);
        if (n < 0) return total ? (ssize_t)total : -1;
        msgp[i].msg_datalen = n;
        msgp[i].msg_namelen = mh.msg_namelen;
        msgp[i].msg_controllen = mh.msg_controllen;
        msgp[i].msg_flags = mh.msg_flags;
        total++;
        if (n == 0) break;
    }
    return total;
}
ssize_t sendmsg_x(int s, struct msghdr_x *msgp, unsigned int cnt, int flags) {
    DBG("sendmsg_x fd=%d cnt=%u flags=0x%x", s, cnt, flags);
    ssize_t total = 0;
    for (unsigned int i = 0; i < cnt; i++) {
        struct msghdr mh = {0};
        mh.msg_name = msgp[i].msg_name;
        mh.msg_namelen = msgp[i].msg_namelen;
        mh.msg_iov = msgp[i].msg_iov;
        mh.msg_iovlen = msgp[i].msg_iovlen;
        mh.msg_control = msgp[i].msg_control;
        mh.msg_controllen = msgp[i].msg_controllen;
        mh.msg_flags = msgp[i].msg_flags;
        ssize_t n = sendmsg(s, &mh, flags);
        if (n < 0) return total ? (ssize_t)total : -1;
        total++;
    }
    return total;
}

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

/* ── timingsafe_bcmp: constant-time byte compare. */
int timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
    const unsigned char *p1 = b1, *p2 = b2;
    unsigned int ret = 0;
    for (size_t i = 0; i < n; i++) ret |= p1[i] ^ p2[i];
    return (ret != 0);
}

/* ── __darwin_check_fd_set_overflow: newer ABI check. No-op return 1 = "ok". */
int __darwin_check_fd_set_overflow(int fd, void *set, int unlimited_select) {
    DBG("fd_set_overflow_check fd=%d", fd);
    (void)set; (void)unlimited_select;
    return 1;
}

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

    size_t count = 0;
    const char *scan = buf, *end = buf + n;
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
    if (!newbuf) return real(fd, buf, n);

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

/* ── kevent64 shim: 10.9's kqueue doesn't know KEVENT_FLAG_ERROR_EVENTS or
 * KEVENT_FLAG_IMMEDIATE (both added in 10.10), so when uSockets calls
 * kevent64(kq, changes, n, changes, n, FLAG=0x2, NULL) expecting "only return
 * EV_ERROR events, then return immediately", 10.9 instead:
 *
 *   (a) returns any ready event (e.g. a just-registered writable socket's
 *       EVFILT_WRITE fire). uSockets discards those returns, and because it
 *       registered with EV_ONESHOT the event is consumed and never re-fires
 *       — the main loop never learns the socket is writable, and the HTTP
 *       client sits there for 4 seconds until timing out.
 *
 *   (b) BLOCKS FOREVER when no events are ready (e.g. registering a fresh
 *       timer whose deadline is in the future). Without FLAG_ERROR_EVENTS
 *       semantics and with NULL timeout, kevent64 waits until any event
 *       arrives on the kqueue. uSockets expects the call to return as soon
 *       as the changes are submitted; instead the main thread pins here
 *       forever, starving the event loop. This is the "idle for a few
 *       minutes, next request hangs" symptom — a JS setTimeout or
 *       undici pool timer registers during the quiet window, hits this
 *       path with nothing to fire, and parks the loop.
 *
 * Fix: handle (a) by stashing non-error events for later delivery (see
 * kq_stash below). Handle (b) by forcing a zero timeout on the real call
 * whenever FLAG_ERROR_EVENTS or FLAG_IMMEDIATE is set — that's what modern
 * kqueue does natively when those flags are present, regardless of what
 * the caller passes in `timeout`. */
#define KEVENT_FLAG_ERROR_EVENTS 0x2
#define KEVENT_FLAG_IMMEDIATE    0x1

extern int kevent64(int kq, const struct kevent64_s *changelist, int nchanges,
                    struct kevent64_s *eventlist, int nevents,
                    unsigned int flags, const struct timespec *timeout);
int kevent64_wrapper(int kq, const struct kevent64_s *changelist, int nchanges,
                     struct kevent64_s *eventlist, int nevents,
                     unsigned int flags, const struct timespec *timeout) __asm("_kevent64");

/* Per-kqueue queue of events we intercepted during add-only calls (where the
 * caller asked for error-events only but the kernel fired a ready event).
 * Delivered on the next normal wait on that kqueue. Small tables since bun
 * uses only a handful of kqueues. */
#define KQ_TABLE_SIZE      16
#define MAX_PENDING_PER_KQ 32
static struct {
    int kq;                                      /* -1 if slot unused */
    int count;
    struct kevent64_s ev[MAX_PENDING_PER_KQ];
} g_kq_pending[KQ_TABLE_SIZE];
static pthread_mutex_t g_kq_mu = PTHREAD_MUTEX_INITIALIZER;
static volatile int g_kq_stash_any = 0;   /* fast-path: non-zero iff any slot in use */

__attribute__((constructor))
static void kq_pending_init(void) {
    for (int i = 0; i < KQ_TABLE_SIZE; i++) g_kq_pending[i].kq = -1;
}

static void kq_stash(int kq, const struct kevent64_s *evs, int n) {
    pthread_mutex_lock(&g_kq_mu);
    int slot = -1;
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq == kq) { slot = i; break; }
        if (slot < 0 && g_kq_pending[i].kq == -1) slot = i;
    }
    if (slot >= 0) {
        g_kq_pending[slot].kq = kq;
        for (int i = 0; i < n && g_kq_pending[slot].count < MAX_PENDING_PER_KQ; i++)
            g_kq_pending[slot].ev[g_kq_pending[slot].count++] = evs[i];
        __atomic_store_n(&g_kq_stash_any, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&g_kq_mu);
}

/* Drop stash state involving fd. Called from close_wrapper so we never
 * deliver a stashed event whose udata points at freed memory:
 *   - If fd is a kqueue, discard the whole slot.
 *   - Otherwise, fd may be a socket ident for a stashed event; remove those
 *     entries. Their udata was a poll pointer the caller is about to free
 *     (us_poll_free follows close() for sockets). Timers use a heap
 *     pointer as ident, so close() doesn't match them — those are handled
 *     by the EV_DELETE pre-invalidation in kevent64_wrapper below.
 *
 * The leading stash-any check avoids the lock for the common case of
 * closing sockets/files that have never been registered with kevent64. */
static void kq_forget_fd(int fd) {
    if (fd < 0) return;
    if (!__atomic_load_n(&g_kq_stash_any, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&g_kq_mu);
    int any = 0;
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq == fd) {
            g_kq_pending[i].kq = -1;
            g_kq_pending[i].count = 0;
            continue;
        }
        if (g_kq_pending[i].kq < 0) continue;
        int out = 0;
        for (int j = 0; j < g_kq_pending[i].count; j++) {
            if ((int)(g_kq_pending[i].ev[j].ident) == fd) continue;
            if (out != j) g_kq_pending[i].ev[out] = g_kq_pending[i].ev[j];
            out++;
        }
        g_kq_pending[i].count = out;
        any |= (g_kq_pending[i].count > 0) || (g_kq_pending[i].kq >= 0);
    }
    if (!any) __atomic_store_n(&g_kq_stash_any, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_kq_mu);
}

/* Remove stashed events matching (ident, filter) on the given kq. Called
 * when the caller's changelist contains EV_DELETE/EV_DISABLE — without this,
 * a previously-stashed fire for an about-to-be-deleted filter would be
 * delivered after the filter's owning object (timer cb, poll_t) is freed. */
static void kq_invalidate_filter(int kq, uint64_t ident, int16_t filter) {
    if (!__atomic_load_n(&g_kq_stash_any, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&g_kq_mu);
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq != kq) continue;
        int out = 0;
        for (int j = 0; j < g_kq_pending[i].count; j++) {
            if (g_kq_pending[i].ev[j].ident == ident &&
                g_kq_pending[i].ev[j].filter == filter) continue;
            if (out != j) g_kq_pending[i].ev[out] = g_kq_pending[i].ev[j];
            out++;
        }
        g_kq_pending[i].count = out;
        break;
    }
    pthread_mutex_unlock(&g_kq_mu);
}

static int kq_drain(int kq, struct kevent64_s *out, int max_out) {
    pthread_mutex_lock(&g_kq_mu);
    int n = 0;
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq == kq && g_kq_pending[i].count > 0) {
            int take = g_kq_pending[i].count;
            if (take > max_out) take = max_out;
            for (int j = 0; j < take; j++) out[n++] = g_kq_pending[i].ev[j];
            /* Shift the rest down */
            int remaining = g_kq_pending[i].count - take;
            for (int j = 0; j < remaining; j++) g_kq_pending[i].ev[j] = g_kq_pending[i].ev[j + take];
            g_kq_pending[i].count = remaining;
            break;
        }
    }
    pthread_mutex_unlock(&g_kq_mu);
    return n;
}

int kevent64_wrapper(int kq, const struct kevent64_s *changelist, int nchanges,
                     struct kevent64_s *eventlist, int nevents,
                     unsigned int flags, const struct timespec *timeout) {
    static int (*real_kevent64)(int, const struct kevent64_s *, int,
                                struct kevent64_s *, int, unsigned int,
                                const struct timespec *) = NULL;
    if (!real_kevent64) real_kevent64 = dlsym(RTLD_NEXT, "kevent64");

    /* Before handing the changes to the kernel, drop any stashed events
     * that belong to filters being removed/disabled. Otherwise we could
     * later deliver a fire for a filter whose owner (timer cb, poll_t) the
     * caller is about to free, which causes a use-after-free in Bun's
     * uSockets dispatcher (null-deref in us_internal_socket_after_open when
     * the freed page has been reclaimed and zeroed). */
    for (int i = 0; i < nchanges; i++) {
        if (changelist[i].flags & (EV_DELETE | EV_DISABLE))
            kq_invalidate_filter(kq, changelist[i].ident, changelist[i].filter);
    }

    /* Strip flags 10.9 doesn't understand. Both FLAG_ERROR_EVENTS and
     * FLAG_IMMEDIATE imply "don't block for events" — the modern kernel
     * handles that via the flag; the old one needs us to emulate by
     * substituting a zero timeout on the real call. */
    unsigned int kflags = flags & ~(KEVENT_FLAG_ERROR_EVENTS | KEVENT_FLAG_IMMEDIATE);
    static const struct timespec zero_ts = {0, 0};
    const struct timespec *real_timeout = timeout;
    if (flags & (KEVENT_FLAG_ERROR_EVENTS | KEVENT_FLAG_IMMEDIATE))
        real_timeout = &zero_ts;

    /* Case 1: add-only call with FLAG_ERROR_EVENTS semantics. The caller (e.g.
     * Bun's uSockets / FilePoll registration) wants to register filters and
     * receive only real EV_ERROR events. On 10.9 kqueue doesn't know that
     * flag and hands back any ready events — the caller discards non-errors,
     * so they'd be lost.
     *
     * Stash every non-error event for delivery on the next normal wait.
     * Caveat for level-triggered filters: the event would re-fire on its
     * own, so stashing means the caller may receive it twice — but Bun's
     * Poll.onUpdateKQueue handlers are idempotent, so that's safe. What we
     * MUST NOT do is drop EV_ONESHOT (filter is removed after fire) or
     * EV_DISPATCH (filter is disabled after fire until re-enabled) events,
     * because those never re-fire — and in particular Bun registers stdin
     * with EV_DISPATCH, so dropping it here is exactly what breaks
     * interactive input on 10.9. */
    if ((flags & KEVENT_FLAG_ERROR_EVENTS) && nchanges > 0) {
        int rc = real_kevent64(kq, changelist, nchanges, eventlist, nevents, kflags, real_timeout);
        if (rc > 0) {
            int kept = 0;
            struct kevent64_s to_stash[MAX_PENDING_PER_KQ];
            int n_stash = 0;
            for (int i = 0; i < rc; i++) {
                if (eventlist[i].flags & EV_ERROR) {
                    eventlist[kept++] = eventlist[i];
                } else if (n_stash < MAX_PENDING_PER_KQ) {
                    to_stash[n_stash++] = eventlist[i];
                }
            }
            if (n_stash) kq_stash(kq, to_stash, n_stash);
            DBG("kevent64_shim add-only kq=%d rc=%d kept=%d stashed=%d",
                kq, rc, kept, n_stash);
            rc = kept;
        }
        return rc;
    }

    /* Case 2: normal wait. Deliver any stashed events first. */
    if (nchanges == 0 && nevents > 0) {
        int n_stashed = kq_drain(kq, eventlist, nevents);
        if (n_stashed > 0) {
            DBG("kevent64_shim delivered %d stashed events from kq=%d", n_stashed, kq);
            /* Got events without blocking — don't wait. */
            return n_stashed;
        }
    }

    int rc2 = real_kevent64(kq, changelist, nchanges, eventlist, nevents, kflags, real_timeout);
    if (dbg_on() && nchanges == 0 && nevents > 0) {
        DBG("kevent64_wait kq=%d nevs=%d fl=0x%x to=%p rc=%d",
            kq, nevents, kflags, (void*)real_timeout, rc2);
        for (int i = 0; i < rc2 && i < 3; i++)
            DBG("  ev[%d] fd=%llu filter=%d flags=0x%x fflags=0x%x data=%lld udata=0x%llx",
                i, eventlist[i].ident, eventlist[i].filter, eventlist[i].flags,
                eventlist[i].fflags, (long long)eventlist[i].data,
                (unsigned long long)eventlist[i].udata);
    }
    return rc2;
}

/* close() shim — drops any kevent64 stash state referencing `fd` before the
 * real close runs. Without this, a socket whose EVFILT_WRITE fire got stashed
 * during its EV_ADD+KEVENT_FLAG_ERROR_EVENTS registration (see the wrapper
 * above) but that's closed before the next kevent64 wait would have its poll
 * pointer delivered after free() — leading to a null-pointer dispatch inside
 * Bun's us_internal_socket_after_open on macOS zones that zero freed pages. */
int close_wrapper(int fd) __asm("_close");
int close_wrapper(int fd) {
    static int (*real)(int) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "close");
    kq_forget_fd(fd);
    return real(fd);
}

int close_nocancel_wrapper(int fd) __asm("_close$NOCANCEL");
int close_nocancel_wrapper(int fd) {
    static int (*real)(int) = NULL;
    if (!real) real = dlsym(RTLD_NEXT, "close$NOCANCEL");
    kq_forget_fd(fd);
    return real(fd);
}
