/*
 * signpost.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── signposts: tracing no-ops ────────────────────────────────────────── */
void _os_signpost_emit_with_name_impl(void) { DBG("signpost_emit"); }
int os_signpost_enabled(void *log) { (void)log; DBG("signpost_enabled"); return 0; }

