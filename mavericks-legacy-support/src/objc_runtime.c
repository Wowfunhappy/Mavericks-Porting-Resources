/*
 * ObjC runtime functions added in 10.14+ / 11.0+ --
 * custom polyfill (not from macports-legacy-support).
 */

#include <objc/objc.h>
#include <objc/message.h>
#include <objc/runtime.h>

/* objc_alloc_init (added ~10.14.4) — combines [cls alloc] and [obj init] */
id objc_alloc_init(Class cls) {
	id obj = ((id(*)(Class, SEL))objc_msgSend)(cls, sel_getUid("alloc"));
	return ((id(*)(id, SEL))objc_msgSend)(obj, sel_getUid("init"));
}

/* objc_alloc (added ~10.14) — optimized [cls alloc] */
id objc_alloc(Class cls) {
	return ((id(*)(Class, SEL))objc_msgSend)(cls, sel_getUid("alloc"));
}

/* objc_opt_class (added macOS 11) — optimized [obj class] */
Class objc_opt_class(id obj) {
	if (!obj) return Nil;
	return ((Class(*)(id, SEL))objc_msgSend)(obj, sel_getUid("class"));
}

/* objc_opt_isKindOfClass (added macOS 11) */
BOOL objc_opt_isKindOfClass(id obj, Class cls) {
	if (!obj) return NO;
	return ((BOOL(*)(id, SEL, Class))objc_msgSend)(obj, sel_getUid("isKindOfClass:"), cls);
}

/* objc_opt_respondsToSelector (added macOS 11) */
BOOL objc_opt_respondsToSelector(id obj, SEL sel) {
	if (!obj) return NO;
	return ((BOOL(*)(id, SEL, SEL))objc_msgSend)(obj, sel_getUid("respondsToSelector:"), sel);
}

/* objc_unsafeClaimAutoreleasedReturnValue (added 10.11) */
extern id objc_retainAutoreleasedReturnValue(id obj);
id objc_unsafeClaimAutoreleasedReturnValue(id obj) {
	return objc_retainAutoreleasedReturnValue(obj);
}

/*
 * objc_loadClassref resolves a lazy class reference. The compiler emits these
 * for a class it can only name at runtime: the slot holds either the class
 * itself, or -- with the low bit set -- a pointer to a stub function that
 * returns the class. This is the documented protocol, implemented rather than
 * stubbed, because the caller uses whatever comes back as a Class.
 */
Class objc_loadClassref(void **ref) {
    if (!ref) return Nil;
    uintptr_t slot = (uintptr_t)*ref;
    if (slot & 1) {
        Class (*resolver)(void) = (Class (*)(void))(slot & ~(uintptr_t)1);
        Class cls = resolver();
        *ref = (void *)cls;     /* memoise, as the runtime does */
        return cls;
    }
    return (Class)slot;
}

/*
 * The compiler emits these two in place of ordinary message sends when the
 * deployment target is new enough. Both have exact semantics, so both are
 * implemented rather than stubbed.
 */

/* [Cls new] */
id objc_opt_new(Class cls) {
    id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    return cls ? send((id)cls, sel_registerName("new")) : nil;
}

/* [obj self], which is the object itself. */
id objc_opt_self(id object) {
    return object;
}
