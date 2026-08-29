/*
 * Zeroing weak references for classes with custom retain/release, on 10.9.
 *
 * 10.9's objc refuses to form a weak reference to an instance of a class that
 * overrides retain or release, because it cannot see such a class's reference
 * count. AppKit is full of them -- NSTextView overrides -release -- and the
 * refusal is fatal:
 *
 *   objc[…]: Cannot form weak reference to instance (0x…) of class
 *   NSTextView. It is possible that this object was over-released, or is in
 *   the process of deallocation.
 *
 * The object is usually alive and well; objc simply declines. Later releases
 * lifted the restriction, so an application built against a newer SDK takes
 * weak references to these objects freely, and dies here on the first one.
 *
 * The restriction cannot be lifted from outside. objc asks such a class two
 * questions, -allowsWeakReference and -retainWeakReference, and answering the
 * second is impossible: objc calls it from weak_read_no_lock while holding the
 * SideTable lock, and any retain reachable from outside takes that same lock,
 * so the process deadlocks:
 *
 *   objc_loadWeakRetained -> weak_read_no_lock  (holds SideTable lock)
 *     -> -[NSObject retain] -> _objc_rootRetain_slow -> blocks on that lock
 *
 * So the weak reference is implemented here instead, for exactly the classes
 * objc will not handle. A registry maps each weak-slot address to its object,
 * and the object carries a sentinel: an associated object whose own -dealloc
 * zeroes every slot still naming it. The runtime releases associated objects
 * while destroying the instance, so the slots are cleared before the memory is
 * reused, and a weak reference reads as nil once its target is gone rather
 * than dangling.
 *
 * The sentinel rides on the instance rather than on its class deliberately.
 * Swizzling -dealloc per class needs a table with a bound, and any class left
 * out once that bound is reached silently stops zeroing -- which does not fail
 * where the bug is, it fails later, in whatever retains the dangling pointer.
 * Per class it also breaks -dealloc chaining: with a class and one of its
 * subclasses both swizzled, the shared hook cannot tell which of them it is
 * running for, and [super dealloc] re-enters the subclass's implementation
 * until the stack is gone. An instance carries exactly one sentinel, so
 * neither applies.
 *
 * Everything else is delegated to objc unchanged, so this affects only the
 * classes that would otherwise abort the process. It is narrower than the real
 * implementation in one way worth stating: zeroing happens as the instance is
 * destroyed rather than atomically against a concurrent retain, so a weak load
 * racing a deallocation on another thread can still see the object. The real
 * runtime closes that window with a lock this code cannot reach.
 */

#include <objc/runtime.h>
#include <objc/message.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdint.h>

/* objc4-532 marks a class that overrides retain/release with bit 15 of its
 * class_rw_t flags; the bit is inherited by subclasses during realization. */
#define MPLS_RW_HAS_CUSTOM_RR (1u << 15)

struct mpls_weak_class { void *isa, *superclass, *cache, *vtable; uintptr_t data; };
struct mpls_weak_rw { uint32_t flags; };

static int mpls_class_is_custom_rr(Class cls) {
    if (!cls) return 0;
    struct mpls_weak_rw *rw =
        (struct mpls_weak_rw *)(((struct mpls_weak_class *)cls)->data & ~(uintptr_t)3);
    return rw && (rw->flags & MPLS_RW_HAS_CUSTOM_RR);
}

/* ── the registry ── */

/* Which implementation owns a slot. Forwarding a destroy for a slot objc was
 * never given makes it search its table for a referent that is not there,
 * which faults in weak_unregister_no_lock instead of doing nothing; and slot
 * addresses are reused constantly as stack frames come and go, so the owner
 * has to be recorded per slot rather than inferred. */
enum mpls_weak_owner { MPLS_WEAK_OURS, MPLS_WEAK_OBJC };

struct mpls_weak_slot {
    id *location;
    id object;
    enum mpls_weak_owner owner;
    struct mpls_weak_slot *next;
};

#define MPLS_WEAK_BUCKETS 1024
static struct mpls_weak_slot *mpls_weak_buckets[MPLS_WEAK_BUCKETS];
static pthread_mutex_t mpls_weak_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t mpls_weak_hash(const void *p) {
    return ((uintptr_t)p >> 4) % MPLS_WEAK_BUCKETS;
}

/* Caller holds the lock. */
static void mpls_weak_forget(id *location) {
    struct mpls_weak_slot **pp = &mpls_weak_buckets[mpls_weak_hash(location)];
    while (*pp) {
        if ((*pp)->location == location) {
            struct mpls_weak_slot *dead = *pp;
            *pp = dead->next;
            free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Caller holds the lock. */
static void mpls_weak_remember(id *location, id object, enum mpls_weak_owner owner) {
    mpls_weak_forget(location);
    struct mpls_weak_slot *s = malloc(sizeof *s);
    if (!s) return;
    s->location = location;
    s->object = object;
    s->owner = owner;
    s->next = mpls_weak_buckets[mpls_weak_hash(location)];
    mpls_weak_buckets[mpls_weak_hash(location)] = s;
}

/* Caller holds the lock. Returns the slot, or NULL if nothing owns it. */
static struct mpls_weak_slot *mpls_weak_lookup(id *location) {
    struct mpls_weak_slot *s = mpls_weak_buckets[mpls_weak_hash(location)];
    for (; s; s = s->next) if (s->location == location) return s;
    return NULL;
}

static int mpls_weak_known(id *location) {
    struct mpls_weak_slot *s = mpls_weak_lookup(location);
    return s && s->owner == MPLS_WEAK_OURS;
}

/* ── the sentinel ── */

/* Zero every slot naming this object. */
static void mpls_weak_zero_for(id object) {
    pthread_mutex_lock(&mpls_weak_lock);
    for (size_t b = 0; b < MPLS_WEAK_BUCKETS; b++) {
        struct mpls_weak_slot **pp = &mpls_weak_buckets[b];
        while (*pp) {
            if ((*pp)->object == object && (*pp)->owner == MPLS_WEAK_OURS) {
                struct mpls_weak_slot *dead = *pp;
                *dead->location = nil;
                *pp = dead->next;
                free(dead);
            } else {
                pp = &(*pp)->next;
            }
        }
    }
    pthread_mutex_unlock(&mpls_weak_lock);
}

static Class mpls_sentinel_class;
static ptrdiff_t mpls_sentinel_offset;
static IMP mpls_nsobject_dealloc;
static const void *mpls_sentinel_key = &mpls_sentinel_key;

static void mpls_sentinel_dealloc(id self, SEL _cmd) {
    /* Unretained: the object is being destroyed and is only compared here. */
    id object = *(id *)((char *)self + mpls_sentinel_offset);
    if (object) mpls_weak_zero_for(object);
    ((void (*)(id, SEL))mpls_nsobject_dealloc)(self, _cmd);
}

static void mpls_sentinel_setup(void) {
    Class nsobject = objc_getClass("NSObject");
    if (!nsobject) return;
    mpls_nsobject_dealloc =
        class_getMethodImplementation(nsobject, sel_registerName("dealloc"));
    Class cls = objc_allocateClassPair(nsobject, "MPLSWeakSentinel", 0);
    if (cls) {
        class_addIvar(cls, "object", sizeof(id),
                      (uint8_t)__builtin_ctz(sizeof(id)), "^v");
        class_addMethod(cls, sel_registerName("dealloc"),
                        (IMP)mpls_sentinel_dealloc, "v@:");
        objc_registerClassPair(cls);
    } else {
        /* Already registered, so adopt it rather than going without: leaving
         * the class unset turns zeroing off for the whole process, and that
         * does not fail here, it fails in whatever later reads the slot. */
        cls = objc_getClass("MPLSWeakSentinel");
    }
    if (!cls) return;
    Ivar iv = class_getInstanceVariable(cls, "object");
    if (!iv) return;   /* Without the slot the sentinel cannot name its object. */
    mpls_sentinel_offset = ivar_getOffset(iv);
    mpls_sentinel_class = cls;
}

/* Give the object a sentinel if it has none. Must run without the registry
 * lock held: association takes locks of its own, and the sentinel's -dealloc
 * takes the registry lock. */
static void mpls_weak_attach_sentinel(id object) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, mpls_sentinel_setup);
    if (!mpls_sentinel_class || !mpls_sentinel_offset) return;
    if (objc_getAssociatedObject(object, mpls_sentinel_key)) return;
    id sentinel = ((id (*)(id, SEL))objc_msgSend)((id)mpls_sentinel_class,
                                                  sel_registerName("alloc"));
    sentinel = ((id (*)(id, SEL))objc_msgSend)(sentinel, sel_registerName("init"));
    if (!sentinel) return;
    *(id *)((char *)sentinel + mpls_sentinel_offset) = object;
    objc_setAssociatedObject(object, mpls_sentinel_key, sentinel,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    ((void (*)(id, SEL))objc_msgSend)(sentinel, sel_registerName("release"));
}

/* ── the real objc entry points ── */

static id (*mpls_real_storeWeak)(id *, id);
static id (*mpls_real_initWeak)(id *, id);
static id (*mpls_real_loadWeakRetained)(id *);
static id (*mpls_real_loadWeak)(id *);
static void (*mpls_real_destroyWeak)(id *);
static void (*mpls_real_copyWeak)(id *, id *);
static void (*mpls_real_moveWeak)(id *, id *);

static void mpls_weak_bind_real(void) {
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    void bind(void);
    /* RTLD_NOLOAD: libobjc is always already loaded, and this must never be
     * the thing that loads it. RTLD_DEFAULT would find these definitions. */
    if (mpls_real_storeWeak) return;
    (void)once;
    void *libobjc = dlopen("/usr/lib/libobjc.A.dylib", RTLD_LAZY | RTLD_NOLOAD);
    if (!libobjc) return;
    mpls_real_storeWeak = (id (*)(id *, id))dlsym(libobjc, "objc_storeWeak");
    mpls_real_initWeak = (id (*)(id *, id))dlsym(libobjc, "objc_initWeak");
    mpls_real_loadWeakRetained = (id (*)(id *))dlsym(libobjc, "objc_loadWeakRetained");
    mpls_real_loadWeak = (id (*)(id *))dlsym(libobjc, "objc_loadWeak");
    mpls_real_destroyWeak = (void (*)(id *))dlsym(libobjc, "objc_destroyWeak");
    mpls_real_copyWeak = (void (*)(id *, id *))dlsym(libobjc, "objc_copyWeak");
    mpls_real_moveWeak = (void (*)(id *, id *))dlsym(libobjc, "objc_moveWeak");
}

/* Does objc handle this object, or do we have to? */
static int mpls_weak_ours(id object) {
    return object && mpls_class_is_custom_rr(object_getClass(object));
}

/* ── replacements ── */

id objc_storeWeak(id *location, id object) {
    mpls_weak_bind_real();
    pthread_mutex_lock(&mpls_weak_lock);
    struct mpls_weak_slot *existing = mpls_weak_lookup(location);
    int had = existing && existing->owner == MPLS_WEAK_OBJC;
    pthread_mutex_unlock(&mpls_weak_lock);

    if (mpls_weak_ours(object)) {
        /* Leaving a registration behind would have objc zero this slot later. */
        if (had && mpls_real_destroyWeak) mpls_real_destroyWeak(location);
        mpls_weak_attach_sentinel(object);
        pthread_mutex_lock(&mpls_weak_lock);
        mpls_weak_remember(location, object, MPLS_WEAK_OURS);
        pthread_mutex_unlock(&mpls_weak_lock);
        *location = object;
        return object;
    }

    /* Handing the slot back to objc: drop anything this code was holding for
     * it first, then record that objc owns it from here on. */
    pthread_mutex_lock(&mpls_weak_lock);
    struct mpls_weak_slot *previous = mpls_weak_lookup(location);
    int wasOurs = previous && previous->owner == MPLS_WEAK_OURS;
    if (wasOurs) mpls_weak_forget(location);
    pthread_mutex_unlock(&mpls_weak_lock);
    if (wasOurs) *location = nil;

    id result = mpls_real_storeWeak ? mpls_real_storeWeak(location, object)
                                    : (*location = object);
    pthread_mutex_lock(&mpls_weak_lock);
    if (object) mpls_weak_remember(location, object, MPLS_WEAK_OBJC);
    else mpls_weak_forget(location);
    pthread_mutex_unlock(&mpls_weak_lock);
    return result;
}

id objc_initWeak(id *location, id object) {
    *location = nil;
    /* A fresh slot: whatever was recorded for this address belonged to a
     * variable that has since gone away. */
    pthread_mutex_lock(&mpls_weak_lock);
    mpls_weak_forget(location);
    pthread_mutex_unlock(&mpls_weak_lock);
    if (!object) return nil;
    if (mpls_weak_ours(object)) return objc_storeWeak(location, object);
    mpls_weak_bind_real();
    id result = mpls_real_initWeak ? mpls_real_initWeak(location, object)
                                   : objc_storeWeak(location, object);
    pthread_mutex_lock(&mpls_weak_lock);
    mpls_weak_remember(location, object, MPLS_WEAK_OBJC);
    pthread_mutex_unlock(&mpls_weak_lock);
    return result;
}

id objc_loadWeakRetained(id *location) {
    mpls_weak_bind_real();
    pthread_mutex_lock(&mpls_weak_lock);
    int ours = mpls_weak_known(location);
    id object = ours ? *location : nil;
    pthread_mutex_unlock(&mpls_weak_lock);
    if (!ours) return mpls_real_loadWeakRetained ? mpls_real_loadWeakRetained(location) : nil;
    if (!object) return nil;
    /* Safe here: no objc lock is held on this path. */
    return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName("retain"));
}

id objc_loadWeak(id *location) {
    id object = objc_loadWeakRetained(location);
    if (!object) return nil;
    return ((id (*)(id, SEL))objc_msgSend)(object, sel_registerName("autorelease"));
}

void objc_destroyWeak(id *location) {
    mpls_weak_bind_real();
    pthread_mutex_lock(&mpls_weak_lock);
    struct mpls_weak_slot *slot = mpls_weak_lookup(location);
    enum mpls_weak_owner owner = slot ? slot->owner : MPLS_WEAK_OURS;
    int known = slot != NULL;
    if (slot) mpls_weak_forget(location);
    pthread_mutex_unlock(&mpls_weak_lock);

    /* Only a slot objc was actually given is handed back to it. An unknown
     * slot is one this code zeroed, or one whose address has been reused;
     * either way objc has no registration for it. */
    if (known && owner == MPLS_WEAK_OBJC) {
        if (mpls_real_destroyWeak) mpls_real_destroyWeak(location);
        return;
    }
    *location = nil;
}

void objc_copyWeak(id *to, id *from) {
    id object = objc_loadWeakRetained(from);
    *to = nil;
    if (object) {
        objc_storeWeak(to, object);
        ((void (*)(id, SEL))objc_msgSend)(object, sel_registerName("release"));
    }
}

void objc_moveWeak(id *to, id *from) {
    objc_copyWeak(to, from);
    objc_destroyWeak(from);
}
