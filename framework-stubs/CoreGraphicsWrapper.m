// Wrapper for CoreGraphics on OS X 10.9: supplies the symbols this OS's copy
// lacks, and re-exports the real one so everything else still resolves.
// Hand-written: the class list started from the symbols real binaries bind,
// but the constants and functions carry real values and implementations,
// so this file is maintained by hand and not regenerated.
#import <Cocoa/Cocoa.h>

// A stub class has to tolerate any selector a real caller uses. Declaring
// only the class would abort the process with "unrecognized selector" the
// first time one is called, so unknown selectors are forwarded to a no-op
// and return zero (nil / NO / 0). respondsToSelector: is left honest, so
// callers that feature-detect still take their unsupported path.
#define EH_STUB_FORWARDING \
    + (NSMethodSignature *)methodSignatureForSelector:(SEL)sel { \
        NSMethodSignature *known = [super methodSignatureForSelector:sel]; \
        return known ?: [NSMethodSignature signatureWithObjCTypes:"@@:@@@@@@@@"]; } \
    + (void)forwardInvocation:(NSInvocation *)invocation { } \
    - (NSMethodSignature *)methodSignatureForSelector:(SEL)sel { \
        NSMethodSignature *known = [super methodSignatureForSelector:sel]; \
        return known ?: [NSMethodSignature signatureWithObjCTypes:"@@:@@@@@@@@"]; } \
    - (void)forwardInvocation:(NSInvocation *)invocation { } \
    /* A stub must tolerate key-value coding as well as selectors. Callers \
     * configure these classes with setValue:forKey:, and KVC does not go \
     * through forwardInvocation: -- it raises instead, which is fatal: \
     *   this class is not key value coding-compliant for the key clear */ \
    - (void)setValue:(id)value forUndefinedKey:(NSString *)key { (void)value; (void)key; } \
    - (id)valueForUndefinedKey:(NSString *)key { (void)key; return nil; } \
    + (void)setValue:(id)value forUndefinedKey:(NSString *)key { (void)value; (void)key; } \
    + (id)valueForUndefinedKey:(NSString *)key { (void)key; return nil; }

#define STUB_CLASS(name) \
    @interface name : NSObject @end \
    @implementation name \
    EH_STUB_FORWARDING \
    @end

#define STUB_SUBCLASS(name, super) \
    @interface name : super @end \
    @implementation name \
    EH_STUB_FORWARDING \
    @end

// ---- hand-written ----

// No Metal on 10.9, so no display has a Metal device. NULL is the same answer
// a real Mac without a Metal-capable GPU gives.
void *CGDirectDisplayCopyCurrentMetalDevice(CGDirectDisplayID display) {
    (void)display; return NULL;
}

// 10.9 posts events to a process by Carbon PSN. The pid form is newer, but the
// translation is available, so this is a real implementation.
void CGEventPostToPid(pid_t pid, CGEventRef event) {
    if (!event) return;
    ProcessSerialNumber psn = {0, kNoProcess};
    if (GetProcessForPID(pid, &psn) == noErr) CGEventPostToPSN(&psn, event);
}
