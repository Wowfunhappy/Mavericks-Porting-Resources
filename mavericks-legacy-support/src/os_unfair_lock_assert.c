/*
 * os_unfair_lock_assert.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── os_unfair_lock_assert_owner: legacy-support has lock/unlock/trylock but
 * not the owner-assert. Stub as a no-op (the check would abort on mismatch). */
typedef struct { uint32_t _os_unfair_lock_opaque; } os_unfair_lock, *os_unfair_lock_t;
void os_unfair_lock_assert_owner(os_unfair_lock_t lock) { (void)lock; }

