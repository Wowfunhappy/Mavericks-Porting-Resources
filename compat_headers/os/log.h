/* os/log.h — compat for macOS < 10.12 */
#ifndef _COMPAT_OS_LOG_H_
#define _COMPAT_OS_LOG_H_
#include <stdint.h>
#include <stdbool.h>

typedef struct os_log_s *os_log_t;
typedef uint8_t os_log_type_t;

#define OS_LOG_TYPE_DEFAULT 0x00
#define OS_LOG_TYPE_INFO    0x01
#define OS_LOG_TYPE_DEBUG   0x02
#define OS_LOG_TYPE_ERROR   0x10
#define OS_LOG_TYPE_FAULT   0x11

#define OS_LOG_DEFAULT ((os_log_t)0)
#define OS_LOG_DISABLED ((os_log_t)0)

#ifdef __cplusplus
extern "C" {
#endif
os_log_t os_log_create(const char *subsystem, const char *category);
bool os_log_type_enabled(os_log_t log, os_log_type_t type);
#ifdef __cplusplus
}
#endif

#define os_log(log, format, ...)
#define os_log_info(log, format, ...)
#define os_log_debug(log, format, ...)
#define os_log_error(log, format, ...)
#define os_log_fault(log, format, ...)
#define os_log_with_type(log, type, format, ...)

#endif /* _COMPAT_OS_LOG_H_ */
