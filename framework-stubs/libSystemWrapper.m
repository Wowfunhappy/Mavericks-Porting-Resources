// Hand-written wrapper for /usr/lib/libSystem.B.dylib on OS X 10.9.
//
// Almost everything this adds comes from mavericks-legacy-support, which is
// the single source of truth for 10.9 polyfills and is linked in by the build
// (see "archive" in frameworks.json). What is here is the part that cannot
// live in a C library: two Objective-C classes.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// With OS_OBJECT_USE_OBJC, dispatch objects are Objective-C objects, and a
// binary built against a later SDK binds their class symbols directly. 10.9's
// libdispatch has the classes but does not export the symbols, so something
// has to be exported here.
//
// Declaring classes named OS_dispatch_queue / OS_dispatch_source would be the
// obvious move and is wrong: the runtime would then hold two classes of each
// name, warn that "one of the two will be used, which one is undefined", and
// leave isKindOfClass: answering NO for genuine dispatch objects.
//
// Instead the classes are declared under private names, the expected symbols
// are aliased onto them, and at load time each is spliced into the real
// class's ancestry -- taking the real class's superclass as its own, then
// becoming that class's superclass. The hierarchy is unchanged apart from one
// extra link that adds no ivars and no methods, so real dispatch objects
// genuinely are kind-of the exported class, which is what callers test.
@interface EHDispatchQueueShim : NSObject @end
@implementation EHDispatchQueueShim @end

@interface EHDispatchSourceShim : NSObject @end
@implementation EHDispatchSourceShim @end

// The aliasing itself is done at link time (-Wl,-alias, driven by
// "alias_symbols" in frameworks.json): an assembler .set here does not survive
// into the export trie.

static void EHSpliceInto(const char *realName, Class shim) {
    Class real = objc_getClass(realName);
    if (!real || !shim || real == shim) return;
    // Already spliced (a second load of this image), or the shim is somehow
    // already in the chain -- either way there is nothing to do.
    for (Class c = real; c; c = class_getSuperclass(c))
        if (c == shim) return;
    Class realSuper = class_getSuperclass(real);
    if (!realSuper) return;
    class_setSuperclass(shim, realSuper);
    class_setSuperclass(real, shim);
}

__attribute__((constructor))
static void EHInstallDispatchClassShims(void) {
    EHSpliceInto("OS_dispatch_queue", [EHDispatchQueueShim class]);
    EHSpliceInto("OS_dispatch_source", [EHDispatchSourceShim class]);
}
