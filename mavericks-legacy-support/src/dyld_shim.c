/*
 * dyld_shared_cache_iterate_text (10.10) --
 * custom polyfill (not from macports-legacy-support).
 *
 * Stub: report no shared cache.
 */

int dyld_shared_cache_iterate_text(const void *uuid, void (*callback)(const void *info)) {
	(void)uuid; (void)callback;
	return -1;
}

/*
 * dyld shared-cache introspection -- _dyld_get_shared_cache_range (10.13),
 * _dyld_get_shared_cache_uuid (10.12).  Best-effort: report "no shared cache",
 * so callers (e.g. lldb's HostInfoMacOSX) fall back to inspecting dylibs.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <uuid/uuid.h>

uint8_t *_dyld_get_shared_cache_range(size_t *length) {
	if (length) *length = 0;
	return (uint8_t *)0;
}

bool _dyld_get_shared_cache_uuid(uuid_t uuid) {
	if (uuid) memset(uuid, 0, sizeof(uuid_t));
	return false;
}
