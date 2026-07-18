/*
 * timingsafe_bcmp.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── timingsafe_bcmp: constant-time byte compare. */
int timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
    const unsigned char *p1 = b1, *p2 = b2;
    unsigned int ret = 0;
    for (size_t i = 0; i < n; i++) ret |= p1[i] ^ p2[i];
    return (ret != 0);
}

