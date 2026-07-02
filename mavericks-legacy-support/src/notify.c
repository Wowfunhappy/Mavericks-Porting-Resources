/*
 * notify_is_valid_token (added macOS 10.10) --
 * custom polyfill (not from macports-legacy-support).
 *
 * Go's runtime calls this at startup solely to force libnotify to
 * allocate its globals before any fork() (go.dev/issue/33565); the
 * call must reach the real libnotify or forked children can hang in
 * _notify_fork_child. notify_get_state performs the same token-table
 * lookup the real function does, so its status also gives us the
 * correct return value.
 */

#include <notify.h>
#include <stdbool.h>
#include <stdint.h>

bool notify_is_valid_token(int token) {
	uint64_t state;
	return notify_get_state(token, &state) != NOTIFY_STATUS_INVALID_TOKEN;
}
