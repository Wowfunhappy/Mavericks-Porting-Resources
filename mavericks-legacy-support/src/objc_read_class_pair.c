/*
 * objc_readClassPair for OS X 10.9 -- kept in its own translation unit.
 *
 * A consumer must ask for this symbol deliberately, because merely defining it
 * changes behaviour elsewhere. The ModernMavericks Swift runtime checks at run
 * time whether objc_readClassPair exists and, when it does, hands class
 * realization to it -- bypassing the realization it does itself, which is the
 * path that project validated on real 10.9. Linking this in by accident, as a
 * side effect of pulling some neighbouring objc helper out of the same object
 * file, silently swaps a tested code path for this one.
 *
 * So: link it only where nothing else provides class realization.
 *
 */

#include <stdlib.h>
#include <stdint.h>
#include <objc/objc.h>
#include <objc/runtime.h>

/* objc4-532.2 class_rw_t (x86_64), 48 bytes. */
struct mpls_objc_class_rw {
    uint32_t flags;
    uint32_t version;
    const void *ro;
    void *method_list_or_lists;
    void *properties;
    const void *protocols;
    void *firstSubclass;
    void *nextSiblingClass;
};

/* objc4-532.2 class_t (x86_64), 40 bytes. */
struct mpls_objc_class {
    struct mpls_objc_class *isa;
    struct mpls_objc_class *superclass;
    void *cache;
    void *vtable;
    uintptr_t data_NEVER_USE;   /* data() is this with the low 2 bits masked */
};

#define MPLS_RW_REALIZED (1u << 31)

/* objc4-532 propagates these from the superclass when it realizes a class, and
 * the minimal realization below has to do the same. Bit 15 is the important
 * one: it marks the class as having its own retain/release, which makes
 * objc_retain send -retain instead of using libobjc's side table.
 *
 * A Swift class inherits retain/release from SwiftObject, so it must carry
 * this bit. Without it, ObjC retains land in the side table while Swift's own
 * retains land in the object header -- two counters for one object, and the
 * object is released twice, which surfaces as
 *
 *   malloc: *** error for object 0x...: pointer being freed was not allocated
 *
 * on the first Dictionary or Set to cross into Objective-C.
 *
 * The bit positions were established by observation on this OS rather than
 * taken from a header: build a class that overrides retain/release, register a
 * subclass of it, and see which flag the subclass inherits. */
#define MPLS_RW_HAS_CUSTOM_RR  (1u << 15)
#define MPLS_RW_HAS_CUSTOM_AWZ (1u << 17)
#define MPLS_RW_INHERITED_MASK (MPLS_RW_HAS_CUSTOM_RR | MPLS_RW_HAS_CUSTOM_AWZ)

extern struct objc_cache _objc_empty_cache;
extern void *_objc_empty_vtable;

/* Flags a realized class carries that a newly realized subclass must inherit.
 * Returns 0 if the superclass is missing or not itself realized yet. */
static uint32_t mpls_inherited_flags(struct mpls_objc_class *cls) {
    if (!cls || !cls->superclass) return 0;
    uintptr_t data = cls->superclass->data_NEVER_USE;
    struct mpls_objc_class_rw *super_rw =
        (struct mpls_objc_class_rw *)(data & ~(uintptr_t)3);
    if (!super_rw || !(super_rw->flags & MPLS_RW_REALIZED)) return 0;
    return super_rw->flags & MPLS_RW_INHERITED_MASK;
}

static void mpls_minimal_realize(void *clsPtr) {
    struct mpls_objc_class *cls = (struct mpls_objc_class *)clsPtr;
    if (!cls) return;

    uintptr_t data = cls->data_NEVER_USE;
    struct mpls_objc_class_rw *maybeRW =
        (struct mpls_objc_class_rw *)(data & ~(uintptr_t)3);

    /* An unrealized class still points at its ro, whose flags never have bit 31
     * set -- so this both detects the ro case and skips a class already done. */
    if (maybeRW && (maybeRW->flags & MPLS_RW_REALIZED)) return;

    const void *ro = maybeRW;
    struct mpls_objc_class_rw *rw = calloc(1, sizeof *rw);
    if (!rw) return;
    /* One method list, so no RW_METHOD_ARRAY. */
    rw->flags = MPLS_RW_REALIZED | mpls_inherited_flags(cls);
    rw->version = 0;
    rw->ro = ro;

    cls->cache = &_objc_empty_cache;
    cls->vtable = &_objc_empty_vtable;
    /* setData, preserving the is-swift low bits that objc-532 masks off. */
    cls->data_NEVER_USE = (uintptr_t)rw | (data & (uintptr_t)3);
}

/* objc4-532 class_ro_t (x86_64): baseMethods sits at offset 32. */
struct mpls_objc_method {
    void *name;          /* a SEL once registered; a char* before that */
    const char *types;
    IMP imp;
};
struct mpls_objc_method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct mpls_objc_method first;
};

/* Realizing the class is not enough on its own: objc4-532 builds a realized
 * class's method list during realizeClass, and the minimal realization above
 * leaves it empty. A class with no methods answers nothing, so a Swift generic
 * class standing in for NSDictionary fails as
 *
 *   -[NSDictionary count]: method only defined for abstract class
 *
 * the moment Foundation asks it anything. Rather than reproduce objc-532's
 * method-list layout -- which varies with whether categories have been
 * attached -- the methods are handed to objc through class_addMethod, which
 * builds whatever representation this runtime uses and updates the class's
 * flags as it goes.
 *
 * A method name is a char * before objc registers it and a SEL afterwards, and
 * a SEL is a pointer to that same string, so sel_registerName accepts either. */
static void mpls_attach_base_methods(Class cls) {
    if (!cls) return;
    struct mpls_objc_class *c = (struct mpls_objc_class *)cls;
    struct mpls_objc_class_rw *rw =
        (struct mpls_objc_class_rw *)(c->data_NEVER_USE & ~(uintptr_t)3);
    if (!rw || !rw->ro) return;

    struct mpls_objc_method_list *ml =
        *(struct mpls_objc_method_list **)((const char *)rw->ro + 32);
    if (!ml || !ml->count) return;

    uint32_t entsize = ml->entsize_and_flags & ~(uint32_t)3;
    if (entsize < sizeof(struct mpls_objc_method)) return;

    const char *base = (const char *)&ml->first;
    for (uint32_t i = 0; i < ml->count; i++) {
        struct mpls_objc_method *m =
            (struct mpls_objc_method *)(base + (size_t)i * entsize);
        if (!m->name || !m->imp) continue;
        class_addMethod(cls, sel_registerName((const char *)m->name),
                        m->imp, m->types);
    }
}

Class objc_readClassPair(Class cls, const void *info) {
    (void)info;
    if (!cls) return Nil;
    /* Both halves of the pair: the class and its metaclass. */
    mpls_minimal_realize((void *)cls);
    mpls_minimal_realize((void *)object_getClass((id)cls));
    /* Instance methods on the class, class methods on the metaclass. */
    mpls_attach_base_methods(cls);
    mpls_attach_base_methods((Class)object_getClass((id)cls));
    /* The caller asserts the returned class is the one it passed in. */
    return cls;
}

/*
 * _objc_realizeClassFromSwift (macOS 10.14.4) -- the entry point the Swift
 * runtime uses to hand a class it has just laid out to the Objective-C
 * runtime.
 *
 * Swift checks for it before deciding how to set up a class whose superclass
 * is an Objective-C class. When it is present, Swift computes the field
 * offsets and instance size against the superclass first
 * (initClassFieldOffsetVector, initObjCClass) and then calls this to realize
 * the result. When it is absent, Swift falls back to requiring the class to
 * have arrived with a fixed instance size already baked in, and a class
 * compiled for a newer deployment target has not:
 *
 *   class ZMJarvisManager does not have a fragile layout;
 *   the deployment target was newer than this OS
 *
 * which is fatal. Providing it is therefore not merely an optimisation -- it
 * selects the path that computes the layout instead of demanding one.
 *
 * By the time this runs Swift has already done the layout work, so what is
 * left is exactly what objc_readClassPair does above: realize the class and
 * its metaclass, and give objc their methods.
 */
Class _objc_realizeClassFromSwift(Class cls, void *previously) {
    (void)previously;   /* the caller's bookkeeping, not ours */
    return objc_readClassPair(cls, NULL);
}
