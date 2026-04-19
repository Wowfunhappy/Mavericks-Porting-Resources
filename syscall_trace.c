/*
 * Syscall tracing via DYLD_INSERT_LIBRARIES on Darwin. Interposes a curated
 * set of libSystem entry points and logs them to stderr with a monotonic
 * timestamp. Useful for diagnosing ported modern-SDK binaries on 10.9
 * without needing dtrace (SIP-blocked in later OS versions too).
 *
 * Traced: socket, connect, kevent64 (changelist + returned events on inet
 * fds), read/write (inet + low-fd TTY content dump), write$NOCANCEL, send,
 * sendto, getsockopt SO_ERROR, close, recv, recvfrom, ioctl (TIOCGWINSZ /
 * TIOCGETA / TIOCSETA / FIONREAD decoded), tcsetattr, tcgetattr.
 *
 * Build:  clang -dynamiclib -o syscall_trace.dylib syscall_trace.c -lpthread
 * Use:    DYLD_INSERT_LIBRARIES=.../syscall_trace.dylib ./your_binary 2>trace.log
 */
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define INTERPOSE(rep, orig) \
    __attribute__((used)) static struct { const void *r; const void *o; } \
    _ip_##orig __attribute__((section("__DATA,__interpose"))) \
    = { (const void *)&(rep), (const void *)&(orig) }

static pthread_mutex_t log_mu = PTHREAD_MUTEX_INITIALIZER;
static void trace(const char *fmt, ...) {
    pthread_mutex_lock(&log_mu);
    struct timeval tv; gettimeofday(&tv, NULL);
    fprintf(stderr, "[t3 %lld.%06ld] ", (long long)tv.tv_sec, (long)tv.tv_usec);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fputc('\n', stderr);
    pthread_mutex_unlock(&log_mu);
}

__attribute__((constructor))
static void _init(void) { fprintf(stderr, "[t3] loaded pid=%d\n", getpid()); }

/* Track which fds are INET sockets so we can filter kevent tracing */
static int g_is_inet[4096];
static void mark_inet(int fd) { if (fd >= 0 && fd < 4096) __sync_lock_test_and_set(&g_is_inet[fd], 1); }
static int is_inet(int fd) { return (fd >= 0 && fd < 4096) ? g_is_inet[fd] : 0; }

static int my_socket(int d, int t, int p) {
    int fd = socket(d, t, p);
    trace("socket(%d,%d,%d)=%d", d, t, p, fd);
    if ((d == 2 || d == 30) && fd >= 0) mark_inet(fd);  /* AF_INET/AF_INET6 */
    return fd;
}
INTERPOSE(my_socket, socket);

static int my_connect(int fd, const struct sockaddr *a, socklen_t l) {
    int rc = connect(fd, a, l);
    if (is_inet(fd))
        trace("connect(fd=%d)=%d errno=%d", fd, rc, rc < 0 ? errno : 0);
    return rc;
}
INTERPOSE(my_connect, connect);

/* kevent64 filtered to events touching inet fds. */
#include <sys/event.h>
static int my_kevent64(int kq, const struct kevent64_s *chg, int nchg,
                       struct kevent64_s *evs, int nevs, unsigned int fl,
                       const struct timespec *to) {
    for (int i = 0; i < nchg; i++)
        if (is_inet((int)chg[i].ident))
            trace("kevent ADD kq=%d fd=%llu filter=%d flags=0x%x fflags=0x%x udata=0x%llx ext[0]=0x%llx ext[1]=0x%llx",
                  kq, chg[i].ident, chg[i].filter, chg[i].flags, chg[i].fflags,
                  (unsigned long long)chg[i].udata,
                  (unsigned long long)chg[i].ext[0], (unsigned long long)chg[i].ext[1]);
    int rc = kevent64(kq, chg, nchg, evs, nevs, fl, to);
    for (int i = 0; i < rc; i++)
        if (is_inet((int)evs[i].ident))
            trace("kevent FIRE kq=%d fd=%llu filter=%d flags=0x%x fflags=0x%x data=%lld udata=0x%llx ext[0]=0x%llx",
                  kq, evs[i].ident, evs[i].filter, evs[i].flags, evs[i].fflags,
                  (long long)evs[i].data, (unsigned long long)evs[i].udata,
                  (unsigned long long)evs[i].ext[0]);
    return rc;
}
INTERPOSE(my_kevent64, kevent64);

/* writes: trace inet fds AND small writes to TTY-looking low fds.
 * Avoid recursion: trace() → fprintf() → write() → back into my_write. */
static __thread int in_write_trace = 0;

static ssize_t my_write(int fd, const void *b, size_t n) {
    ssize_t rc = write(fd, b, n);
    if (in_write_trace) return rc;   /* we're writing our own trace message */
    if (fd == 2) return rc;          /* stderr = our trace target, skip */
    in_write_trace = 1;
    if (is_inet(fd)) {
        trace("write(fd=%d,n=%zu)=%zd", fd, n, rc);
    } else if (fd >= 0 && fd <= 12 && n > 0 && n < 512) {
        /* Print the write content for short writes to low fds — likely TTY. */
        char buf[600];
        size_t m = n < 480 ? n : 480;
        const unsigned char *p = (const unsigned char *)b;
        size_t j = 0;
        for (size_t i = 0; i < m && j < sizeof(buf) - 8; i++) {
            unsigned char c = p[i];
            if (c == 0x1b) { buf[j++]='^'; buf[j++]='['; }
            else if (c >= 0x20 && c < 0x7f) buf[j++] = (char)c;
            else if (c == '\r') { buf[j++]='\\'; buf[j++]='r'; }
            else if (c == '\n') { buf[j++]='\\'; buf[j++]='n'; }
            else { j += snprintf(buf+j, sizeof(buf)-j, "\\x%02x", c); }
        }
        buf[j] = 0;
        trace("tty-write(fd=%d,n=%zd)= '%s'", fd, rc, buf);
    }
    in_write_trace = 0;
    return rc;
}
INTERPOSE(my_write, write);

/* write$NOCANCEL for Darwin */
extern ssize_t write$NOCANCEL_(int, const void *, size_t) __asm("_write$NOCANCEL");
static ssize_t my_write_nc(int fd, const void *b, size_t n) {
    ssize_t rc = write$NOCANCEL_(fd, b, n);
    if (in_write_trace) return rc;
    if (fd == 2) return rc;
    in_write_trace = 1;
    if (fd >= 0 && fd <= 12 && n > 0 && n < 512) {
        char buf[600]; size_t m = n < 480 ? n : 480;
        const unsigned char *p = (const unsigned char *)b; size_t j = 0;
        for (size_t i = 0; i < m && j < sizeof(buf) - 8; i++) {
            unsigned char c = p[i];
            if (c == 0x1b) { buf[j++]='^'; buf[j++]='['; }
            else if (c >= 0x20 && c < 0x7f) buf[j++] = (char)c;
            else if (c == '\r') { buf[j++]='\\'; buf[j++]='r'; }
            else if (c == '\n') { buf[j++]='\\'; buf[j++]='n'; }
            else { j += snprintf(buf+j, sizeof(buf)-j, "\\x%02x", c); }
        }
        buf[j] = 0;
        trace("tty-writeN(fd=%d,n=%zd)= '%s'", fd, rc, buf);
    }
    in_write_trace = 0;
    return rc;
}
INTERPOSE(my_write_nc, write$NOCANCEL_);

static ssize_t my_send(int fd, const void *b, size_t n, int f) {
    ssize_t rc = send(fd, b, n, f);
    if (is_inet(fd)) trace("send(fd=%d,n=%zu,fl=%d)=%zd", fd, n, f, rc);
    return rc;
}
INTERPOSE(my_send, send);

static ssize_t my_sendto(int fd, const void *b, size_t n, int f,
                         const struct sockaddr *to, socklen_t tl) {
    ssize_t rc = sendto(fd, b, n, f, to, tl);
    if (is_inet(fd)) trace("sendto(fd=%d,n=%zu)=%zd", fd, n, rc);
    return rc;
}
INTERPOSE(my_sendto, sendto);

/* Additional tracing on inet fds: getsockopt, close, read, recv */
static int my_getsockopt(int fd, int lvl, int opt, void *v, socklen_t *l) {
    int rc = getsockopt(fd, lvl, opt, v, l);
    if (is_inet(fd) && opt == SO_ERROR && v && l && *l >= sizeof(int))
        trace("getsockopt(fd=%d, SO_ERROR)=%d err=%d", fd, rc, *(int *)v);
    else if (is_inet(fd))
        trace("getsockopt(fd=%d, lvl=%d, opt=%d)=%d", fd, lvl, opt, rc);
    return rc;
}
INTERPOSE(my_getsockopt, getsockopt);

static int my_close(int fd) {
    int inet = is_inet(fd);
    int rc = close(fd);
    if (inet) trace("close(fd=%d)=%d", fd, rc);
    return rc;
}
INTERPOSE(my_close, close);

static ssize_t my_read(int fd, void *b, size_t n) {
    ssize_t rc = read(fd, b, n);
    if (is_inet(fd)) trace("read(fd=%d,n=%zu)=%zd errno=%d", fd, n, rc, rc < 0 ? errno : 0);
    return rc;
}
INTERPOSE(my_read, read);

static ssize_t my_recv(int fd, void *b, size_t n, int f) {
    ssize_t rc = recv(fd, b, n, f);
    if (is_inet(fd)) trace("recv(fd=%d,n=%zu)=%zd", fd, n, rc);
    return rc;
}
INTERPOSE(my_recv, recv);

static ssize_t my_recvfrom(int fd, void *b, size_t n, int f,
                           struct sockaddr *s, socklen_t *sl) {
    ssize_t rc = recvfrom(fd, b, n, f, s, sl);
    if (is_inet(fd)) trace("recvfrom(fd=%d,n=%zu)=%zd", fd, n, rc);
    return rc;
}
INTERPOSE(my_recvfrom, recvfrom);

/* Trace TTY-affecting syscalls — ioctl for termios/winsize, tcsetattr,
 * and writes to any fd (not just inet). */
#include <sys/ioctl.h>
#include <termios.h>
static int my_ioctl(int fd, unsigned long req, void *arg) {
    int rc = ioctl(fd, req, arg);
    /* Decode winsize + errno to see what Ink sees for the terminal. */
    const char *name = "?";
    if (req == 0x40087468UL) name = "TIOCGWINSZ";
    else if (req == 0x40487413UL) name = "TIOCGETA";
    else if (req == 0x80487414UL) name = "TIOCSETA";
    else if (req == 0x80487415UL) name = "TIOCSETAW";
    else if (req == 0x4004667aUL) name = "FIONREAD";
    if (req == 0x40087468UL && rc == 0 && arg) {
        struct winsize { unsigned short rows, cols, xpixel, ypixel; } *ws = arg;
        trace("ioctl(%s fd=%d) = %d rows=%u cols=%u", name, fd, rc, ws->rows, ws->cols);
    } else if (req == 0x4004667aUL && rc == 0 && arg) {
        trace("ioctl(%s fd=%d) = %d nread=%d", name, fd, rc, *(int*)arg);
    } else {
        trace("ioctl(%s 0x%lx fd=%d) = %d errno=%d", name, req, fd, rc, rc < 0 ? errno : 0);
    }
    return rc;
}
INTERPOSE(my_ioctl, ioctl);

static int my_tcsetattr(int fd, int opt, const struct termios *t) {
    int rc = tcsetattr(fd, opt, t);
    trace("tcsetattr(fd=%d, opt=%d, lflag=0x%lx) = %d", fd, opt,
          (unsigned long)t->c_lflag, rc);
    return rc;
}
INTERPOSE(my_tcsetattr, tcsetattr);

static int my_tcgetattr(int fd, struct termios *t) {
    int rc = tcgetattr(fd, t);
    trace("tcgetattr(fd=%d) = %d", fd, rc);
    return rc;
}
INTERPOSE(my_tcgetattr, tcgetattr);

/* kevent64 on just INET sockets (filter=-2 EVFILT_WRITE) */
#include <sys/event.h>

/* (placeholder; write tracing is integrated into my_write above) */
