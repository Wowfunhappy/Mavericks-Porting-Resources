/*
 * pthread_self_is_exiting.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── pthread extras. The qos-class family (pthread_set_qos_class_self_np et al.)
 * now comes from libMavericksLegacySupport; defining it here too would be a
 * duplicate symbol under -force_load. Only self_is_exiting_np, which the archive
 * doesn't cover, remains here. */
int pthread_self_is_exiting_np(void) { return 0; }

