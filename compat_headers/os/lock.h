/* os/lock.h — compatibility shim for macOS < 10.12 */
#ifndef _COMPAT_OS_LOCK_H_
#define _COMPAT_OS_LOCK_H_

#include <libkern/OSAtomic.h>
#include <stdbool.h>

#define OS_UNFAIR_LOCK_INIT OS_SPINLOCK_INIT
typedef OSSpinLock os_unfair_lock;
typedef OSSpinLock *os_unfair_lock_t;

#ifdef __cplusplus
extern "C" {
#endif
void os_unfair_lock_lock(os_unfair_lock_t lock);
bool os_unfair_lock_trylock(os_unfair_lock_t lock);
void os_unfair_lock_unlock(os_unfair_lock_t lock);
#ifdef __cplusplus
}
#endif

#endif
