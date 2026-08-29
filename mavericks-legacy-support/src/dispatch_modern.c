/*
 * libdispatch entry points added after 10.9.
 *
 * Two of these are real implementations; the rest report the absence of a
 * facility 10.9 genuinely does not have, in the same terms the real API uses
 * for "nothing here".
 */

#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Block.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <stdlib.h>

#include "LegacySupport.h"

/* dispatch_block_create returns a block carrying cancellation state. The state
 * has to travel with the block itself, because dispatch_block_cancel is handed
 * only the block, so it is attached as an associated object -- a block is an
 * Objective-C object, so this works on 10.9.
 *
 * Cancellation here is the documented "a cancelled block that has not yet
 * begun does nothing when it runs". The wait/notify half of the API is not
 * provided: those need real barriers inside libdispatch, and a wrong answer
 * from dispatch_block_wait would deadlock or race the caller. */
static const char kCancelFlagKey;

dispatch_block_t dispatch_block_create(unsigned long flags, dispatch_block_t block) {
    (void)flags;
    if (!block) return NULL;

    /* The flag lives inside a CFMutableData so that releasing the block
     * releases the flag with it; a bare malloc here would leak once per
     * cancellable block created. */
    CFMutableDataRef holder = CFDataCreateMutable(kCFAllocatorDefault, sizeof(int));
    if (!holder) return NULL;
    CFDataSetLength(holder, sizeof(int));
    volatile int *cancelled = (volatile int *)CFDataGetMutableBytePtr(holder);
    *cancelled = 0;

    dispatch_block_t inner = Block_copy(block);
    dispatch_block_t wrapper = Block_copy(^{
        if (!*cancelled) inner();
    });
    objc_setAssociatedObject((id)wrapper, &kCancelFlagKey, (id)holder,
                             OBJC_ASSOCIATION_RETAIN);
    CFRelease(holder);
    return wrapper;
}

static volatile int *cancel_flag_for(dispatch_block_t block) {
    if (!block) return NULL;
    CFMutableDataRef holder =
        (CFMutableDataRef)objc_getAssociatedObject((id)block, &kCancelFlagKey);
    if (!holder) return NULL;
    return (volatile int *)CFDataGetMutableBytePtr(holder);
}

void dispatch_block_cancel(dispatch_block_t block) {
    volatile int *flag = cancel_flag_for(block);
    if (flag) __sync_lock_test_and_set(flag, 1);
}

long dispatch_block_testcancel(dispatch_block_t block) {
    volatile int *flag = cancel_flag_for(block);
    return flag && *flag ? 1 : 0;
}

/* The QoS-taking form of dispatch_block_create (10.10). 10.9 has no
 * quality-of-service classes, so the class and relative priority are dropped;
 * what the caller actually depends on -- a block it can cancel -- is
 * unaffected, and scheduling falls back to the single priority this OS has. */
dispatch_block_t dispatch_block_create_with_qos_class(unsigned long flags,
                                                      int qos_class,
                                                      int relative_priority,
                                                      dispatch_block_t block) {
    (void)qos_class;
    (void)relative_priority;
    return dispatch_block_create(flags, block);
}

/* This library's own <dispatch/dispatch.h> defines dispatch_activate as a
 * macro for dispatch_resume, which is what a consumer compiled against it
 * gets. A prebuilt binary binds the symbol instead, so the function has to
 * exist too -- and must agree with the macro, or the same call would mean
 * different things depending on how the caller was built.
 *
 * Undefining the macro first is essential: without it this definition compiles
 * as dispatch_resume, and the library would export a dispatch_resume that
 * shadows libdispatch's own for anything binding through the wrapper. */
#undef dispatch_activate
void dispatch_activate(dispatch_object_t object) {
    dispatch_resume(object);
}

/* 10.9 has no quality-of-service classes. Returning the attribute unchanged
 * keeps the queue's serial/concurrent nature, which is the part that affects
 * correctness; only the scheduling hint is lost. */
dispatch_queue_attr_t
dispatch_queue_attr_make_with_qos_class(dispatch_queue_attr_t attr,
                                        int qos_class, int relative_priority) {
    (void)qos_class;
    (void)relative_priority;
    return attr;
}

/* QOS_CLASS_DEFAULT. Every thread on 10.9 is scheduled the same way, so this
 * is not a placeholder -- it is the only true answer. */
int qos_class_self(void) {
    return 0x15;
}

/* Vouchers (activity tracing identities) do not exist on 10.9. The real API
 * returns NULL for "no voucher", and adopting NULL yields the previous
 * voucher, which is also NULL. */
void *voucher_copy(void) {
    return NULL;
}

void *voucher_adopt(void *voucher) {
    (void)voucher;
    return NULL;
}

/* os_release is os_object_release. With OS_OBJECT_USE_OBJC these are ordinary
 * Objective-C objects, so the release message is the real operation. */
void os_release(void *object) {
    if (!object) return;
    ((void (*)(id, SEL))objc_msgSend)((id)object, sel_registerName("release"));
}
