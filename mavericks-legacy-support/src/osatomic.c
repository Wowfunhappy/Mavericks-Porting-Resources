/*
 * OSAtomic increment/decrement.
 *
 * 10.9's <libkern/OSAtomic.h> declares these inline, so a translation unit
 * that includes the header never emits a call. A binary built against a
 * later SDK, where they are ordinary exported functions, does -- and then
 * finds nothing to bind to. These are the real operations, not stubs.
 */

#include <stdint.h>

int32_t OSAtomicIncrement32(volatile int32_t *value) {
    return __sync_add_and_fetch(value, 1);
}

int32_t OSAtomicDecrement32(volatile int32_t *value) {
    return __sync_sub_and_fetch(value, 1);
}

/* The Barrier forms add a full memory barrier; __sync_* built-ins are already
 * full barriers, so the two differ only in name. */
int32_t OSAtomicIncrement32Barrier(volatile int32_t *value) {
    return __sync_add_and_fetch(value, 1);
}

int32_t OSAtomicDecrement32Barrier(volatile int32_t *value) {
    return __sync_sub_and_fetch(value, 1);
}
