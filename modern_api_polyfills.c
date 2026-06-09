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
 *   - dlopen interposer: when a .node file is loaded, rewrite any
 *     LC_LOAD_DYLIB referencing /usr/lib/libSystem.B.dylib to point at this
 *     wrapper instead, so the addon's two-level namespace binds resolve
 *     against our re-export chain (which already includes polyfills for
 *     CCRandomGenerateBytes etc. from MacPorts legacy-support) rather than
 *     10.9's real libSystem — which halts the whole process with
 *     "Symbol not found: _CCRandomGenerateBytes" as soon as an unresolvable
 *     lazy bind fires inside the addon.
 *
 * Debug tracing: set MAV_PATCH_DEBUG=1 in the environment.
 */
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

/* Condvar-based __ulock emulation. One global mutex/cond pair; waiters
 * check the user's atomic word inside the mutex and cond_wait when the
 * value still matches. __ulock_wake broadcasts, every waiter re-checks
 * its own (addr, value) and either returns or waits again. Thundering-
 * herd cost per wake is O(N_waiters) — tolerable at the thread counts
 * Bun uses (~15). No polling, so idle CPU is truly zero. */
static pthread_mutex_t g_ulock_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_ulock_cv = PTHREAD_COND_INITIALIZER;

int __ulock_wait2(uint32_t op, void *addr, uint64_t value, uint64_t timeout_ns, uint64_t value2) {
    (void)value2;
    int is_64bit = (op & UL_OPERATION_MASK) == UL_COMPARE_AND_WAIT64;
    DBG("ulock_wait op=0x%x addr=%p val=%llu timeout=%lluns", op, addr, value, timeout_ns);

    /* Fast path: caller's value already differs — nothing to wait for. */
    uint64_t cur0 = is_64bit ? *(volatile uint64_t *)addr
                             : *(volatile uint32_t *)addr;
    if (cur0 != value) return 0;

    /* Compute absolute deadline for timed waits. */
    struct timespec deadline = {0, 0};
    int timed = timeout_ns != 0;
    if (timed) {
        struct timeval now; gettimeofday(&now, NULL);
        deadline.tv_sec  = now.tv_sec  + (time_t)(timeout_ns / 1000000000ULL);
        long add_nsec = now.tv_usec * 1000L + (long)(timeout_ns % 1000000000ULL);
        if (add_nsec >= 1000000000L) { deadline.tv_sec += 1; add_nsec -= 1000000000L; }
        deadline.tv_nsec = add_nsec;
    }

    pthread_mutex_lock(&g_ulock_mu);
    for (;;) {
        uint64_t cur = is_64bit ? *(volatile uint64_t *)addr
                                : *(volatile uint32_t *)addr;
        if (cur != value) {
            pthread_mutex_unlock(&g_ulock_mu);
            return 0;
        }
        int rc;
        if (timed) {
            rc = pthread_cond_timedwait(&g_ulock_cv, &g_ulock_mu, &deadline);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&g_ulock_mu);
                return -ETIMEDOUT;
            }
        } else {
            pthread_cond_wait(&g_ulock_cv, &g_ulock_mu);
        }
        /* Spurious or broadcast wake — loop will re-check the value. */
    }
}

int __ulock_wake(uint32_t op, void *addr, uint64_t wake_value) {
    (void)op; (void)addr; (void)wake_value;
    pthread_mutex_lock(&g_ulock_mu);
    pthread_cond_broadcast(&g_ulock_cv);
    pthread_mutex_unlock(&g_ulock_mu);
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

/* ── __chk_fail: 10.10+ FORTIFY_SOURCE bounds-check trap. Pulled in by
 * libMacportsLegacySupport's stpncpy_chk.o (and friends). Not present in
 * 10.9 libSystem, so we have to define it here or the link fails. Behave
 * the same as the system version: write to stderr and abort. */
__attribute__((noreturn))
void __chk_fail(void) {
    static const char msg[] = "*** buffer overflow detected (chk) ***\n";
    write(2, msg, sizeof(msg) - 1);
    abort();
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

/* DIAGNOSTIC: when CLAUDE_SHIM_TRACE=/path is set, log stash/drain/invalidate
 * operations to that file. Zero-cost when unset. */
static FILE *g_trace = NULL;
static int   g_trace_ready = 0;
static pthread_mutex_t g_trace_mu = PTHREAD_MUTEX_INITIALIZER;
static void trace_init(void) {
    if (g_trace_ready) return;
    g_trace_ready = 1;
    const char *p = getenv("MAV_KQ_TRACE");
    if (!p || !*p) return;
    g_trace = fopen(p, "a");
    if (g_trace) setvbuf(g_trace, NULL, _IOLBF, 0);
}
static void tlog(const char *fmt, ...) {
    if (!g_trace) return;
    pthread_mutex_lock(&g_trace_mu);
    struct timeval tv; gettimeofday(&tv, NULL);
    va_list ap; va_start(ap, fmt);
    fprintf(g_trace, "%ld.%06d ", (long)tv.tv_sec, tv.tv_usec);
    vfprintf(g_trace, fmt, ap);
    va_end(ap);
    fputc('\n', g_trace);
    pthread_mutex_unlock(&g_trace_mu);
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
        for (int i = 0; i < n; i++) {
            if (g_kq_pending[slot].count < MAX_PENDING_PER_KQ) {
                g_kq_pending[slot].ev[g_kq_pending[slot].count++] = evs[i];
                tlog("STASH kq=%d slot=%d ident=%llu filter=%d flags=0x%x fflags=0x%x data=%lld (count=%d)",
                     kq, slot, (unsigned long long)evs[i].ident, evs[i].filter,
                     evs[i].flags, evs[i].fflags, (long long)evs[i].data,
                     g_kq_pending[slot].count);
            } else {
                tlog("STASH-DROP(overflow) kq=%d slot=%d ident=%llu filter=%d flags=0x%x",
                     kq, slot, (unsigned long long)evs[i].ident, evs[i].filter, evs[i].flags);
            }
        }
        __atomic_store_n(&g_kq_stash_any, 1, __ATOMIC_RELEASE);
    } else {
        tlog("STASH-DROP(no-slot) kq=%d n=%d", kq, n);
    }
    pthread_mutex_unlock(&g_kq_mu);
}

/* Remove stashed events matching (ident, filter) on the given kq. Called
 * when the caller's changelist contains EV_DELETE/EV_DISABLE — without this,
 * a previously-stashed fire for an about-to-be-deleted filter would be
 * delivered after the filter's owning object (timer, poll) is freed. */
static void kq_invalidate_filter(int kq, uint64_t ident, int16_t filter) {
    if (!__atomic_load_n(&g_kq_stash_any, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&g_kq_mu);
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq != kq) continue;
        int out = 0, removed = 0;
        for (int j = 0; j < g_kq_pending[i].count; j++) {
            if (g_kq_pending[i].ev[j].ident == ident &&
                g_kq_pending[i].ev[j].filter == filter) {
                removed++;
                tlog("INVALIDATE kq=%d ident=%llu filter=%d (removed from stash)",
                     kq, (unsigned long long)ident, filter);
                continue;
            }
            if (out != j) g_kq_pending[i].ev[out] = g_kq_pending[i].ev[j];
            out++;
        }
        g_kq_pending[i].count = out;
        (void)removed;
        break;
    }
    pthread_mutex_unlock(&g_kq_mu);
}

/* On close(fd): neutralize stashed entries (udata→0) rather than wipe.
 * Matches what Bun's own us_internal_loop_update_pending_ready_polls does
 * during us_socket_close — the dispatcher's null-udata skip keeps the
 * freed poll out of the dispatch path, while the event stays in the
 * stream so counters/drains stay consistent. */
static void kq_forget_fd(int fd) {
    if (fd < 0) return;
    if (!__atomic_load_n(&g_kq_stash_any, __ATOMIC_ACQUIRE)) return;
    pthread_mutex_lock(&g_kq_mu);
    int any = 0;
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq == fd) {
            if (g_kq_pending[i].count > 0)
            g_kq_pending[i].kq = -1;
            g_kq_pending[i].count = 0;
            continue;
        }
        if (g_kq_pending[i].kq < 0) continue;
        for (int j = 0; j < g_kq_pending[i].count; j++) {
            if ((int)(g_kq_pending[i].ev[j].ident) == fd) {
                g_kq_pending[i].ev[j].udata = 0;
            }
        }
        any |= (g_kq_pending[i].count > 0) || (g_kq_pending[i].kq >= 0);
    }
    if (!any) __atomic_store_n(&g_kq_stash_any, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_kq_mu);
}

/* Drain a mach port that kevent64 just reported EVFILT_MACHPORT on.
 *
 * Bun registers EVFILT_MACHPORT with fflags=MACH_RCV_MSG|MACH_RCV_OVERWRITE,
 * expecting the kernel to dequeue the message into event.ext[0] as a
 * side-effect of kevent64 delivery. Modern kernels honour that. The 10.9
 * kernel appears not to — the port's 1-slot queue stays populated after
 * the fire. Bun's us_internal_accept_poll_event is a no-op on kqueue,
 * so nobody else drains either.
 *
 * Consequence: after the first wakeup on `loop->data.wakeup_async`, the
 * port remains full forever. Every subsequent us_internal_async_wakeup
 * (which sends with MACH_SEND_TIMEOUT=0) returns MACH_SEND_TIMED_OUT,
 * which Bun silently treats as "already pending"; no new EVFILT_MACHPORT
 * fire occurs because the port is level-triggered on new messages, and
 * the HTTP client thread parks in kevent64 forever. This is precisely
 * the "first prompt works, second prompt hangs after idle" symptom.
 *
 * Fix: after kevent64 returns an EVFILT_MACHPORT event, do a non-
 * blocking mach_msg(MACH_RCV_MSG|MACH_RCV_TIMEOUT, 0) on that port to
 * actually dequeue the message. We discard the payload — Bun never
 * inspects it either (it's purely a wakeup signal). The port's queue
 * is now empty, so the next sender's mach_msg succeeds and triggers a
 * fresh EVFILT_MACHPORT fire.
 *
 * Buffer size: Bun uses MACHPORT_BUF_LEN = 1024. Use the same here so
 * MACH_RCV_TOO_LARGE doesn't leave the message stuck. */
/* Port → portset lookup for EVFILT_MACHPORT translation. Bun creates
 * one wakeup port per loop; current Bun allocates exactly one async.
 * 32 slots leave headroom for future asyncs / multiple loops without
 * needing a hash table. Linear scan is trivial at this size. */
#define MP_MAP_SIZE 32
static struct {
    mach_port_t port;   /* original receive port Bun registered */
    mach_port_t pset;   /* portset we created to wrap it */
} g_mp_map[MP_MAP_SIZE];
static pthread_mutex_t g_mp_mu = PTHREAD_MUTEX_INITIALIZER;

static mach_port_t mp_lookup_pset(mach_port_t port) {
    pthread_mutex_lock(&g_mp_mu);
    mach_port_t r = MACH_PORT_NULL;
    for (int i = 0; i < MP_MAP_SIZE; i++) {
        if (g_mp_map[i].port == port) { r = g_mp_map[i].pset; break; }
    }
    pthread_mutex_unlock(&g_mp_mu);
    return r;
}

/* Given a receive-right port that Bun wants to register with
 * EVFILT_MACHPORT, ensure there's a portset wrapping it and return the
 * portset's name. Idempotent: repeated calls for the same port
 * return the same portset. */
static mach_port_t mp_get_or_create_pset(mach_port_t port) {
    if (port == MACH_PORT_NULL) return MACH_PORT_NULL;
    mach_port_t existing = mp_lookup_pset(port);
    if (existing != MACH_PORT_NULL) return existing;

    mach_port_t self = mach_task_self();
    mach_port_t pset = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(self, MACH_PORT_RIGHT_PORT_SET, &pset);
    if (kr != KERN_SUCCESS) return MACH_PORT_NULL;
    kr = mach_port_move_member(self, port, pset);
    if (kr != KERN_SUCCESS) {
        mach_port_deallocate(self, pset);
        return MACH_PORT_NULL;
    }

    pthread_mutex_lock(&g_mp_mu);
    int slot = -1;
    for (int i = 0; i < MP_MAP_SIZE; i++) {
        /* Someone else may have installed it while we were creating; prefer theirs. */
        if (g_mp_map[i].port == port) {
            mach_port_t other = g_mp_map[i].pset;
            pthread_mutex_unlock(&g_mp_mu);
            mach_port_move_member(self, port, MACH_PORT_NULL);
            mach_port_deallocate(self, pset);
            return other;
        }
        if (slot < 0 && g_mp_map[i].port == MACH_PORT_NULL) slot = i;
    }
    if (slot < 0) { /* table full — oh well */
        pthread_mutex_unlock(&g_mp_mu);
        mach_port_move_member(self, port, MACH_PORT_NULL);
        mach_port_deallocate(self, pset);
        return MACH_PORT_NULL;
    }
    g_mp_map[slot].port = port;
    g_mp_map[slot].pset = pset;
    pthread_mutex_unlock(&g_mp_mu);
    return pset;
}

/* The portset fires — we don't know which member port has a message
 * without receiving. A bare mach_msg(MACH_RCV_MSG, header_port=pset)
 * will dequeue from whichever member port has a message and set
 * msgh_local_port to that port. We discard the payload (Bun's wakeup
 * messages carry no body). */
/* Receive-and-discard any messages pending on a port/portset. Uses a
 * 64KB stack scratch; for anything larger we fall back to heap via
 * MACH_RCV_LARGE. Loops until the kernel reports the queue is empty. */
/* Receive-and-discard every message currently queued on a port or
 * portset. Uses a 64KB stack buffer; for anything larger (Bun's
 * wakeup messages aren't, but defensively handle it) MACH_RCV_LARGE
 * lets us grow to heap. Loops until the kernel reports empty. */
static void mp_drain_any(mach_port_t rcv_port) {
    uint8_t stack_buf[65536] __attribute__((aligned(16)));
    mach_msg_header_t *hdr = (mach_msg_header_t *)stack_buf;
    size_t cap = sizeof(stack_buf);
    void *heap = NULL;
    for (;;) {
        mach_msg_return_t kr = mach_msg(
            hdr,
            MACH_RCV_MSG | MACH_RCV_TIMEOUT | MACH_RCV_LARGE,
            0,
            (mach_msg_size_t)cap,
            rcv_port,
            0,
            MACH_PORT_NULL);
        if (kr == KERN_SUCCESS) {
            mach_msg_destroy(hdr);
            continue;
        }
        if (kr == MACH_RCV_TOO_LARGE) {
            size_t need = (size_t)hdr->msgh_size + sizeof(mach_msg_trailer_t) + 32;
            void *nh = heap ? realloc(heap, need) : malloc(need);
            if (!nh) break;
            heap = nh;
            hdr = (mach_msg_header_t *)heap;
            cap = need;
            continue;
        }
        break;  /* MACH_RCV_TIMED_OUT or other — done */
    }
    if (heap) free(heap);
}

/* Opt-in mach-port activity trace: set MAV_MACHPORT_LOG=/path to enable.
 * Kept as a callable (though currently uninstrumented) hook so a future
 * debug session can re-add log points without reintroducing the
 * forward-declaration churn. */
__attribute__((unused))
static FILE *machport_log(void) {
    static FILE *f = (FILE *)-1;
    if (f == (FILE *)-1) {
        const char *p = getenv("MAV_MACHPORT_LOG");
        f = (p && *p) ? fopen(p, "a") : NULL;
        if (f) setvbuf(f, NULL, _IOLBF, 0);
    }
    return f;
}
/* After kevent64 delivers EVFILT_MACHPORT, (a) drain the underlying
 * port queue — Bun's us_internal_accept_poll_event is a no-op on
 * kqueue and Bun normally relies on the kernel's
 * MACH_RCV_MSG|MACH_RCV_OVERWRITE side-effect to pull the message
 * out, which 10.9 doesn't implement — and (b) rewrite the ident back
 * from our wrapping portset to the caller-visible port. Without (a)
 * the port's 1-slot queue stays full and every subsequent
 * us_internal_async_wakeup send returns MACH_SEND_TIMED_OUT; Bun
 * interprets that as "already pending" so no new message is ever
 * sent, and the target thread's kevent64 parks forever. */
static void machport_drain_fired(struct kevent64_s *evs, int n) {
    for (int i = 0; i < n; i++) {
        if (evs[i].filter != EVFILT_MACHPORT) continue;
        if (evs[i].flags & EV_ERROR) continue;
        mach_port_t ident_port = (mach_port_t)evs[i].ident;
        if (ident_port == MACH_PORT_NULL) continue;
        mach_port_t original_port = MACH_PORT_NULL;
        pthread_mutex_lock(&g_mp_mu);
        for (int j = 0; j < MP_MAP_SIZE; j++) {
            if (g_mp_map[j].pset == ident_port) {
                original_port = g_mp_map[j].port;
                break;
            }
        }
        pthread_mutex_unlock(&g_mp_mu);
        mp_drain_any(ident_port);
        if (original_port != MACH_PORT_NULL) evs[i].ident = original_port;
    }
}

static int kq_drain(int kq, struct kevent64_s *out, int max_out) {
    pthread_mutex_lock(&g_kq_mu);
    int n = 0;
    for (int i = 0; i < KQ_TABLE_SIZE; i++) {
        if (g_kq_pending[i].kq == kq && g_kq_pending[i].count > 0) {
            int take = g_kq_pending[i].count;
            if (take > max_out) take = max_out;
            for (int j = 0; j < take; j++) {
                struct kevent64_s *ev = &g_kq_pending[i].ev[j];
                /* Drop fd-based stashed fires whose ident has been closed.
                 * uSockets ties us_poll_t lifetime to the socket fd: when
                 * Bun closes the socket, us_poll_free runs at end-of-tick
                 * and the stashed event's udata becomes a dangling pointer.
                 * fcntl(F_GETFD) returns -1/EBADF on a closed fd, which is
                 * a strong signal that the owning poll is gone. Delivering
                 * the event anyway crashes Bun's dispatcher in
                 * us_internal_dispatch_ready_poll (segfault at offset 0x30
                 * of the freed-and-reclaimed allocation). Silently dropping
                 * is correct: the consumer is no longer interested in the fd. */
                int16_t f = ev->filter;
                if ((f == EVFILT_READ || f == EVFILT_WRITE ||
                     f == EVFILT_VNODE) &&
                    fcntl((int)ev->ident, F_GETFD) < 0) {
                    tlog("DRAIN-DROP-STALE kq=%d ident=%llu filter=%d (fd closed)",
                         kq, (unsigned long long)ev->ident, f);
                    continue;
                }
                out[n++] = *ev;
                tlog("DRAIN kq=%d ident=%llu filter=%d flags=0x%x",
                     kq, (unsigned long long)ev->ident, f, ev->flags);
            }
            /* Shift the rest down */
            int remaining = g_kq_pending[i].count - take;
            for (int j = 0; j < remaining; j++) g_kq_pending[i].ev[j] = g_kq_pending[i].ev[j + take];
            g_kq_pending[i].count = remaining;
            break;
        }
    }
    /* If no slot has pending events, clear the any-flag so close() can
     * skip the mutex on its fast path. */
    int any = 0;
    for (int i = 0; i < KQ_TABLE_SIZE; i++)
        if (g_kq_pending[i].count > 0) { any = 1; break; }
    if (!any) __atomic_store_n(&g_kq_stash_any, 0, __ATOMIC_RELEASE);
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
    if (!g_trace_ready) trace_init();

    /* Before handing the changes to the kernel, drop any stashed events
     * that belong to filters being removed/disabled. Otherwise we could
     * later deliver a fire for a filter whose owner (timer cb, poll_t) the
     * caller is about to free, which causes a use-after-free in Bun's
     * dispatcher (null-deref in us_internal_socket_after_open when the
     * freed page has been reclaimed and zeroed). */
    for (int i = 0; i < nchanges; i++) {
        if (changelist[i].flags & (EV_DELETE | EV_DISABLE))
            kq_invalidate_filter(kq, changelist[i].ident, changelist[i].filter);
    }

    /* Translate Bun's EVFILT_MACHPORT registrations to 10.9's vintage
     * requirements.
     *
     * Modern macOS lets you register EVFILT_MACHPORT directly on a
     * receive-right port, with `fflags = MACH_RCV_MSG | MACH_RCV_OVERWRITE`
     * and `ext[0]` pointing at a user-supplied receive buffer that the
     * kernel fills in on delivery. 10.9 supports neither the direct-
     * on-receive-port registration nor the ext[] receive buffer: it
     * returns `EV_ERROR, data=EOPNOTSUPP` for any EV_ADD on a port that
     * isn't a *portset*. The net effect in Bun is that the HTTP client
     * thread's wakeup async never actually installs a filter — the
     * first request is processed by drainEvents() in the pre-tick
     * path, but the second request after the idle tries to wake a
     * thread whose kevent64 has nothing that will ever fire.
     *
     * Fix: at EV_ADD time, create a portset, move the receive port
     * into it, and substitute the portset's name as the kevent's
     * ident. Also strip fflags/ext[] since 10.9 ignores (or rejects)
     * those. Remember the port→portset mapping so we can (a) drain
     * the right port when the portset fires, and (b) clean up the
     * portset on EV_DELETE. */
    /* EVFILT_MACHPORT translation: wrap ports that use the 10.10+
     * `fflags = MACH_RCV_MSG | MACH_RCV_OVERWRITE` + `ext[]` receive-
     * buffer variant in a portset. Only these are rejected by 10.9's
     * kernel (EV_ERROR + EOPNOTSUPP); registrations with fflags==0
     * work natively (CoreFoundation's CFRunLoop, libdispatch mach
     * sources, etc.) and must be passed through unchanged — wrapping
     * them reroutes their receive semantics and breaks the owning
     * subsystem (e.g. CFRunLoop stops pumping, killing the main-
     * thread render loop). */
    struct kevent64_s mp_rewritten[nchanges > 0 ? nchanges : 1];
    int mp_rewrote = 0;
    for (int i = 0; i < nchanges; i++) {
        if (changelist[i].filter != EVFILT_MACHPORT) continue;
        if (!(changelist[i].flags & EV_ADD)) continue;
        /* Modern-style registration marker: MACH_RCV_MSG (0x02) or
         * MACH_RCV_OVERWRITE (0x1000) bits in fflags. Bare (fflags=0)
         * registrations are 10.9-native and untouched. */
        if ((changelist[i].fflags & (0x00000002u | 0x00001000u)) == 0) continue;
        mach_port_t port = (mach_port_t)changelist[i].ident;
        mach_port_t pset = mp_get_or_create_pset(port);
        if (pset == MACH_PORT_NULL) continue;
        if (!mp_rewrote) {
            for (int j = 0; j < nchanges; j++) mp_rewritten[j] = changelist[j];
            mp_rewrote = 1;
        }
        mp_rewritten[i].ident = pset;
        mp_rewritten[i].fflags = 0;
        mp_rewritten[i].ext[0] = 0;
        mp_rewritten[i].ext[1] = 0;
    }
    if (mp_rewrote) changelist = mp_rewritten;
    (void)mp_lookup_pset;

    /* Strip FLAG_ERROR_EVENTS and force zero timeout when it's set —
     * the modern kernel returns immediately after submitting changes in
     * that mode, regardless of the passed timeout. 10.9 doesn't know the
     * flag; if the caller passes NULL timeout it would block forever
     * waiting for events. (Bun always passes {0,0}, but keep this
     * defensive for other callers.)
     *
     * NOTE: we deliberately do NOT intercept KEVENT_FLAG_IMMEDIATE here.
     * In theory it should behave the same way (return immediately), but
     * empirically Bun's main loop combines IMMEDIATE with had_wakeups and
     * relies on 10.9's "flag ignored, respect caller timeout" behavior as
     * natural pacing. Forcing zero-timeout there uncorks an upstream JS
     * wakeup loop that runs main-thread-hot (366% CPU observed). Leave
     * IMMEDIATE to fall through untouched. */
    unsigned int kflags = flags & ~KEVENT_FLAG_ERROR_EVENTS;
    static const struct timespec zero_ts = {0, 0};
    const struct timespec *real_timeout = timeout;
    if (flags & KEVENT_FLAG_ERROR_EVENTS)
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
        /* FLAG_ERROR_EVENTS semantics: only EV_ERROR events are returned
         * to the caller; any non-error fires the filters produce are held
         * by the kernel and delivered on a subsequent wait. 10.9 doesn't
         * know this flag, so the old kernel returns non-errors on the
         * register call directly. We must hide those from the caller —
         * uSockets treats a non-zero return from this call as a
         * registration failure and frees the poll. Stash the non-error
         * fires; the next wait on this kq drains them. */
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
            rc = kept;
        }
        machport_drain_fired(eventlist, rc);
        return rc;
    }

    /* Case 2: pure wait — deliver stashed events first. Restricted to
     * nchanges==0 because when caller bundles changes + wait in one call,
     * the semantics of the wait are "see whatever the changes produce".
     * Mixing stashed-from-earlier events into that breaks Bun's event
     * dispatch logic (it expects fires in fresh temporal order). */
    int n_kept = 0;
    if (nchanges == 0 && nevents > 0) n_kept = kq_drain(kq, eventlist, nevents);

    const struct timespec *use_timeout = real_timeout;
    static const struct timespec zero_ts_wait = {0, 0};
    if (n_kept > 0) use_timeout = &zero_ts_wait;

    int rc2 = real_kevent64(kq, changelist, nchanges,
                            eventlist + n_kept, nevents - n_kept,
                            kflags, use_timeout);
    if (dbg_on() && nchanges == 0 && nevents > 0) {
        DBG("kevent64_wait kq=%d nevs=%d fl=0x%x to=%p n_kept=%d rc=%d",
            kq, nevents, kflags, (void*)real_timeout, n_kept, rc2);
        for (int i = 0; i < rc2 + n_kept && i < 3; i++)
            DBG("  ev[%d] fd=%llu filter=%d flags=0x%x fflags=0x%x data=%lld udata=0x%llx",
                i, eventlist[i].ident, eventlist[i].filter, eventlist[i].flags,
                eventlist[i].fflags, (long long)eventlist[i].data,
                (unsigned long long)eventlist[i].udata);
    }
    if (rc2 < 0) return n_kept > 0 ? n_kept : rc2;
    int total = n_kept + rc2;
    machport_drain_fired(eventlist, total);
    return total;
}

/* No close() wrapper.
 *
 * The "correct" modern-kernel emulation would drop stashed entries on
 * close(fd), since the real kernel auto-deregisters filters and
 * discards their queued fires. In practice that caused consistent
 * hangs of Bun's HTTP client thread after a 5-minute idle window,
 * because of an inter-thread race between Case 1 stash and a sibling
 * Case 2 drain that parks in real_kevent64 right as the stash entry
 * is being added. I tried three fixes for the race — a lock-release
 * barrier at kevent64 entry, a neutralize-on-close (udata→0) variant
 * that mirrors Bun's own us_internal_loop_update_pending_ready_polls
 * scrub, and an EVFILT_USER wakeup triggered from kq_stash — each
 * still reproduced the hang at least once. Keeping the stash intact
 * past close() makes the test pass reliably.
 *
 * UAF defense without close() interception: kq_drain validates each
 * fd-based stashed event's ident with fcntl(F_GETFD) before delivery
 * and drops it if the fd is closed. uSockets ties us_poll_t lifetime
 * to the socket fd (us_poll_free → close(fd)), so a closed fd is a
 * reliable signal that the udata is now a dangling pointer. Without
 * this, a fresh-machine first request would dequeue a stale fire and
 * crash Bun's dispatcher in us_internal_dispatch_ready_poll — segfault
 * at offset 0x30 of the freed-and-reclaimed allocation. */


/* ── dlopen interposer ──────────────────────────────────────────────────────
 *
 * Claude Code embeds native addons (image-processor.node, audio-capture.node,
 * etc.) inside its bunfs virtual filesystem. At process.dlopen time Bun
 * extracts one to a /var/folders/...T/.<hash>-<idx>.node temp file and calls
 * dlopen() on it. Those addons were built against a 10.14+ SDK and two-level-
 * bind symbols like _CCRandomGenerateBytes directly to /usr/lib/libSystem.B
 * .dylib, which on 10.9 does not export them. When dyld's first lazy-bind
 * thunk fires it can't resolve the symbol, calls dyld::halt(), and the whole
 * process dies with SIGILL — reported from Bun as a segfault in
 * Process_functionDlopen:520 (the dlopen call site).
 *
 * Fix: before handing a .node path to the real dlopen, rewrite every
 * LC_LOAD_DYLIB / LC_LOAD_WEAK_DYLIB that targets /usr/lib/libSystem.B.dylib
 * to point at this wrapper instead. The wrapper re-exports libSystem, so all
 * the addon's normal libc bindings still resolve; and our locally-defined
 * stubs (CCRandomGenerateBytes etc.) now satisfy the binds that libSystem
 * alone couldn't.
 *
 * We do the rewrite on a private copy in /tmp, dlopen that, then unlink —
 * dyld has already mapped the image by the time dlopen returns, so the file
 * can go away immediately.
 *
 * This wrapper exports its own strong `dlopen` symbol which shadows the one
 * re-exported from /usr/lib/libSystem.B.dylib; when the re-written main
 * claude binary two-level-binds `_dlopen` against @loader_path/libSystem-
 * Wrapper.dylib it resolves to ours. We reach the system dlopen via a
 * function pointer obtained from libdyld's image at constructor time with
 * NSLookupSymbolInImage (stable 10.4+ API), so calling "the real dlopen"
 * from inside our shim never recurses.
 *
 * __DATA,__interpose would be cleaner but on 10.9's dyld-239 it's only
 * applied for DYLD_INSERT_LIBRARIES images — not regular LC_LOAD_DYLIB
 * dependencies like this wrapper — so an interpose tuple here is a no-op.
 */

static char  g_wrapper_path[PATH_MAX] = {0};
static size_t g_wrapper_path_len      = 0;
static void *(*g_real_dlopen)(const char *, int) = NULL;

static void mav_dlopen_init_real(void) {
    uint32_t n = _dyld_image_count();
    for (uint32_t i = 0; i < n; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name) continue;
        if (!strstr(name, "/libdyld.dylib")) continue;
        const struct mach_header *mh = _dyld_get_image_header(i);
        NSSymbol sym = NSLookupSymbolInImage(mh, "_dlopen",
            NSLOOKUPSYMBOLINIMAGE_OPTION_BIND);
        if (sym) g_real_dlopen = (void *(*)(const char *, int))NSAddressOfSymbol(sym);
        return;
    }
}

__attribute__((constructor))
static void mav_dlopen_init(void) {
    Dl_info info;
    if (dladdr((void *)&mav_dlopen_init, &info) && info.dli_fname) {
        char resolved[PATH_MAX];
        if (realpath(info.dli_fname, resolved)) {
            size_t L = strlen(resolved);
            if (L < sizeof(g_wrapper_path)) {
                memcpy(g_wrapper_path, resolved, L + 1);
                g_wrapper_path_len = L;
            }
        }
    }
    mav_dlopen_init_real();
    DBG("dlopen_init: wrapper=%s real_dlopen=%p",
        g_wrapper_path_len ? g_wrapper_path : "(unknown)", (void *)g_real_dlopen);
}

/* Copy src into a fresh /tmp/claude-mav-...-XXXXXX.node, with every
 * LC_LOAD_DYLIB/LC_LOAD_WEAK_DYLIB/LC_REEXPORT_DYLIB that matches
 * /usr/lib/libSystem.B.dylib rewritten to point at g_wrapper_path. Grows
 * individual LCs into the header pad (space between the last LC and the
 * first section's file data) when the new path is longer than the old.
 *
 * Return: 0 = rewritten temp file written (path in dst), caller dlopens it
 *         1 = no libSystem LC_LOAD_DYLIB found (src is fine as-is)
 *        -1 = error (e.g. not a 64-bit Mach-O, or not enough header pad) */
static int mav_rewrite_node_libsystem(const char *src, char *dst, size_t dstsz) {
    int      rc       = -1;
    int      in_fd    = -1;
    int      out_fd   = -1;
    uint8_t *buf      = NULL;
    uint8_t *new_lcs  = NULL;
    dst[0] = '\0';

    static const char OLD_TARGET[] = "/usr/lib/libSystem.B.dylib";

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) { DBG("dlopen rewrite: open(%s): %s", src, strerror(errno)); goto done; }
    struct stat st;
    if (fstat(in_fd, &st) < 0) goto done;
    size_t fsize = (size_t)st.st_size;
    if (fsize < sizeof(struct mach_header_64)) goto done;

    buf = malloc(fsize);
    if (!buf) goto done;
    if (read(in_fd, buf, fsize) != (ssize_t)fsize) goto done;
    close(in_fd); in_fd = -1;

    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) { DBG("dlopen rewrite: %s not MH_MAGIC_64", src); goto done; }

    /* First section offset bounds the header pad. */
    uint32_t first_sect_off = UINT32_MAX;
    uint8_t *lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if (sect[j].offset > 0 && sect[j].offset < first_sect_off)
                    first_sect_off = sect[j].offset;
            }
        }
        lcp += lc->cmdsize;
    }
    if (first_sect_off == UINT32_MAX) first_sect_off = 4096;

    new_lcs = calloc(1, first_sect_off);
    if (!new_lcs) goto done;

    uint32_t new_off   = 0;
    int      modified  = 0;
    size_t   new_len   = g_wrapper_path_len + 1;  /* include NUL */

    lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        uint32_t cmdsize   = lc->cmdsize;
        uint32_t write_size = cmdsize;
        int      match      = 0;

        if (lc->cmd == LC_LOAD_DYLIB ||
            lc->cmd == LC_LOAD_WEAK_DYLIB ||
            lc->cmd == LC_REEXPORT_DYLIB) {
            struct dylib_command *dc = (struct dylib_command *)lcp;
            const char *name = (const char *)lcp + dc->dylib.name.offset;
            if (strcmp(name, OLD_TARGET) == 0) {
                match = 1;
                size_t base = dc->dylib.name.offset;
                uint32_t needed = (uint32_t)((base + new_len + 7) & ~7UL);
                if (needed < cmdsize) needed = cmdsize;
                write_size = needed;
            }
        }

        if (new_off + write_size > first_sect_off) {
            DBG("dlopen rewrite: %s LCs overflow pad (need %u, have %u)",
                src, new_off + write_size, first_sect_off);
            goto done;
        }

        memcpy(new_lcs + new_off, lcp, cmdsize);
        if (match) {
            struct dylib_command *ndc = (struct dylib_command *)(new_lcs + new_off);
            ndc->cmdsize = write_size;
            size_t base = ndc->dylib.name.offset;
            memset(new_lcs + new_off + base, 0, write_size - base);
            memcpy(new_lcs + new_off + base, g_wrapper_path, g_wrapper_path_len);
            modified++;
        }
        new_off += write_size;
        lcp += cmdsize;
    }

    if (!modified) { rc = 1; goto done; }

    /* Splice the rebuilt LC block back into buf. */
    memset(buf + sizeof(struct mach_header_64), 0,
           first_sect_off - sizeof(struct mach_header_64));
    memcpy(buf + sizeof(struct mach_header_64), new_lcs, new_off);
    hdr->sizeofcmds = new_off;

    snprintf(dst, dstsz, "/tmp/claude-mav-dlopen-%d-XXXXXX.node", (int)getpid());
    out_fd = mkstemps(dst, 5);
    if (out_fd < 0) { DBG("dlopen rewrite: mkstemps: %s", strerror(errno)); goto done; }
    if (write(out_fd, buf, fsize) != (ssize_t)fsize) goto done;
    close(out_fd); out_fd = -1;

    DBG("dlopen rewrite: %s → %s (%d LCs repointed to %s)",
        src, dst, modified, g_wrapper_path);
    rc = 0;
done:
    if (in_fd  >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    free(buf);
    free(new_lcs);
    if (rc == -1 && dst[0]) { unlink(dst); dst[0] = '\0'; }
    return rc;
}

void *dlopen(const char *path, int mode) {
    /* Lazily cache real_dlopen if the constructor hasn't run yet (some early
     * dlopens happen during libSystem bootstrap before our ctor fires). */
    if (!g_real_dlopen) mav_dlopen_init_real();
    if (!g_real_dlopen) return NULL;

    DBG("dlopen(%s, 0x%x)", path ? path : "(null)", mode);
    if (!path) return g_real_dlopen(path, mode);

    size_t pl = strlen(path);
    if (pl < 5 || memcmp(path + pl - 5, ".node", 5) != 0)
        return g_real_dlopen(path, mode);

    if (g_wrapper_path_len == 0) {
        DBG("dlopen: wrapper path unknown, passing %s through", path);
        return g_real_dlopen(path, mode);
    }

    char tmp[PATH_MAX];
    int rc = mav_rewrite_node_libsystem(path, tmp, sizeof(tmp));
    if (rc == 1) return g_real_dlopen(path, mode);
    if (rc != 0) {
        /* Rewrite failed — best effort, pass original through. dyld may halt,
         * but we've already done all we usefully can at this layer. */
        return g_real_dlopen(path, mode);
    }

    void *h = g_real_dlopen(tmp, mode);
    unlink(tmp);
    return h;
}
