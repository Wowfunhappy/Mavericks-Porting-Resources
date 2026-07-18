/*
 * ulock.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

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

/* __ulock_wait is the pre-10.15 entry point. Identical semantics to
 * __ulock_wait2, with two ABI differences: its timeout is in *microseconds*
 * (wait2's is nanoseconds) and it has no value2. Claude Code 2.1.214 began
 * importing it directly (earlier builds only used wait2). Forward to wait2
 * with the µs→ns conversion so both share the one condvar emulation; a 0
 * timeout means "wait forever" in both ABIs and passes through unchanged. */
int __ulock_wait(uint32_t op, void *addr, uint64_t value, uint32_t timeout_us) {
    return __ulock_wait2(op, addr, value, (uint64_t)timeout_us * 1000ULL, 0);
}

int __ulock_wake(uint32_t op, void *addr, uint64_t wake_value) {
    (void)op; (void)addr; (void)wake_value;
    pthread_mutex_lock(&g_ulock_mu);
    pthread_cond_broadcast(&g_ulock_cv);
    pthread_mutex_unlock(&g_ulock_mu);
    return 0;
}

