/*
 * notify.h wrapper -- declares notify_is_valid_token (added macOS 10.10),
 * polyfilled for 10.9 by src/notify.c.
 */

#ifndef _MAVERICKS_LEGACY_NOTIFY_H_
#define _MAVERICKS_LEGACY_NOTIFY_H_

#include_next <notify.h>

#include <stdbool.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

bool notify_is_valid_token(int val);

__END_DECLS

#endif /* _MAVERICKS_LEGACY_NOTIFY_H_ */
