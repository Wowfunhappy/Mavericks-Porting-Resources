/*
 * Copyright (c) 2025
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACPORTS_OS_LOG_H_
#define _MACPORTS_OS_LOG_H_

/* MP support header */
#include "LegacySupport.h"

/* Do our SDK-related setup */

/* Include the primary system os/log.h (10.12+ only) */

/*
  os/log.h is the unified logging system, introduced in macOS 10.12, so it
  does not exist on 10.9.  Consumers (lldb, ...) use it only for best-effort
  diagnostic logging, so this shim supplies no-op replacements that compile
  and discard their arguments.  No logging facility is back-filled: lldb keeps
  its own logging channels (`log enable lldb ...`), which are unaffected.

  see https://developer.apple.com/documentation/os/logging
*/

__MP__BEGIN_DECLS

typedef struct os_log_s *os_log_t;
typedef unsigned char    os_log_type_t;

#define OS_LOG_TYPE_DEFAULT ((os_log_type_t)0x00)
#define OS_LOG_TYPE_INFO    ((os_log_type_t)0x01)
#define OS_LOG_TYPE_DEBUG   ((os_log_type_t)0x02)
#define OS_LOG_TYPE_ERROR   ((os_log_type_t)0x10)
#define OS_LOG_TYPE_FAULT   ((os_log_type_t)0x11)

/* Opaque log handles in the real API; here just well-defined null sentinels. */
#define OS_LOG_DEFAULT  ((os_log_t)0)
#define OS_LOG_DISABLED ((os_log_t)0)

static __inline os_log_t
os_log_create(const char *subsystem, const char *category)
{
    (void)subsystem; (void)category;
    return (os_log_t)0;
}

static __inline int
os_log_type_enabled(os_log_t oslog, os_log_type_t type)
{
    (void)oslog; (void)type;
    return 0;   /* nothing is ever enabled -> callers skip the formatting */
}

/* The logging entry points are variadic macros upstream; make them no-ops that
   still touch the log handle so the call sites type-check cleanly. */
#define os_log_with_type(log, type, ...) do { (void)(log); (void)(type); } while (0)
#define os_log(log, ...)        do { (void)(log); } while (0)
#define os_log_info(log, ...)   do { (void)(log); } while (0)
#define os_log_debug(log, ...)  do { (void)(log); } while (0)
#define os_log_error(log, ...)  do { (void)(log); } while (0)
#define os_log_fault(log, ...)  do { (void)(log); } while (0)

__MP__END_DECLS

#endif /* _MACPORTS_OS_LOG_H_ */
