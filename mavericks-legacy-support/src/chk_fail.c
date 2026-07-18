/*
 * chk_fail.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── __chk_fail: 10.10+ FORTIFY_SOURCE bounds-check trap. Pulled in by this
 * library's own stpncpy_chk.o (and friends). Not present in 10.9 libSystem,
 * so we have to define it here or the link fails. Behave the same as the
 * system version: write to stderr and abort. */
__attribute__((noreturn))
void __chk_fail(void) {
    static const char msg[] = "*** buffer overflow detected (chk) ***\n";
    write(2, msg, sizeof(msg) - 1);
    abort();
}

