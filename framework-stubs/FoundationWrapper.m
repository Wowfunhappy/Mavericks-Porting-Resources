// Wrapper for Foundation on OS X 10.9: supplies the symbols this OS's copy
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
STUB_CLASS(NSDateComponentsFormatter)
STUB_CLASS(NSPresentationIntent)
STUB_CLASS(NSURLQueryItem)

// Data constants. Where the real value is documented it is reproduced
// exactly, because callers compare and serialise these.
NSString * const NSPresentationIntentAttributeName = @"NSPresentationIntent";

// ---- hand-written ----

// NSEdgeInsets exists on 10.9; these two helpers do not.
const NSEdgeInsets NSEdgeInsetsZero = {0.0, 0.0, 0.0, 0.0};

BOOL NSEdgeInsetsEqual(NSEdgeInsets a, NSEdgeInsets b) {
    return a.top == b.top && a.left == b.left &&
           a.bottom == b.bottom && a.right == b.right;
}

// Kept from an earlier port: a file-protection class that has no effect on
// 10.9 (there is no data protection), but whose symbol binaries still bind.
NSString * const NSFileProtectionComplete = @"NSFileProtectionComplete";

// Assigning nil through a dictionary subscript.
//
// Modern Foundation documents dict[key] = nil as removing the key. 10.9's
// -setObject:forKeyedSubscript: forwards straight to -setObject:forKey:, which
// raises:
//
//   *** setObjectForKey: object cannot be nil (key: fill)
//
// Code written against the modern behaviour uses the idiom freely -- reading a
// value that may be absent and writing it back is enough to hit it. This
// installs the documented behaviour.
//
// The replacement is applied to whichever classes actually implement the
// selector, because NSMutableDictionary is a class cluster: a category on the
// abstract class would be shadowed by a concrete subclass that defines its own.
#import <objc/runtime.h>
#import <objc/message.h>

static void (*eh_original_setObjectForKeyedSubscript)(id, SEL, id, id);

static void EHSetObjectForKeyedSubscript(id self, SEL _cmd, id object, id key) {
    if (object == nil) {
        ((void (*)(id, SEL, id))objc_msgSend)(self, sel_registerName("removeObjectForKey:"), key);
        return;
    }
    eh_original_setObjectForKeyedSubscript(self, _cmd, object, key);
}

__attribute__((constructor))
static void EHInstallNilSubscriptRemoval(void) {
    SEL sel = sel_registerName("setObject:forKeyedSubscript:");
    const char *names[] = { "__NSDictionaryM", "NSMutableDictionary", NULL };
    for (int i = 0; names[i]; i++) {
        Class cls = objc_getClass(names[i]);
        if (!cls) continue;
        // Only classes that define it themselves; replacing an inherited
        // method here would install it on the wrong class.
        unsigned int count = 0;
        Method *methods = class_copyMethodList(cls, &count);
        int defines = 0;
        for (unsigned int m = 0; m < count; m++)
            if (method_getName(methods[m]) == sel) defines = 1;
        free(methods);
        if (!defines) continue;

        IMP previous = class_replaceMethod(cls, sel, (IMP)EHSetObjectForKeyedSubscript, "v@:@@");
        if (previous && !eh_original_setObjectForKeyedSubscript)
            eh_original_setObjectForKeyedSubscript =
                (void (*)(id, SEL, id, id))previous;
    }
}

// Opt-in OS version spoof, for probing version gates.
//
// Set EH_FAKE_OS_VERSION to something like 10.15.7 and NSProcessInfo will
// report it. Nothing here changes unless that variable is set.
//
// This exists because applications gate features on the OS version rather than
// on whether an API is present, and a port that supplies the API still fails
// the check. It is a diagnostic instrument first: turn it on to learn whether a
// missing feature is a real capability gap or only a version test. Leaving it
// on in normal use is risky in the obvious way -- code that believes it is on a
// newer system will reach for things that genuinely are not here.
#import <objc/runtime.h>
#import <stdlib.h>

// NSOperatingSystemVersion and the three accessors below all arrived in 10.10,
// so the type has to be declared here as well.
typedef struct {
    NSInteger majorVersion;
    NSInteger minorVersion;
    NSInteger patchVersion;
} NSOperatingSystemVersion;

@interface NSProcessInfo (EHVersionSpoof)
- (NSOperatingSystemVersion)operatingSystemVersion;
- (BOOL)isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion)version;
@end

static NSOperatingSystemVersion eh_fake_version;
static BOOL eh_fake_version_active;

static NSOperatingSystemVersion EHFakeOperatingSystemVersion(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    return eh_fake_version;
}

static BOOL EHFakeAtLeastVersion(id self, SEL _cmd, NSOperatingSystemVersion wanted) {
    (void)self; (void)_cmd;
    if (eh_fake_version.majorVersion != wanted.majorVersion)
        return eh_fake_version.majorVersion > wanted.majorVersion;
    if (eh_fake_version.minorVersion != wanted.minorVersion)
        return eh_fake_version.minorVersion > wanted.minorVersion;
    return eh_fake_version.patchVersion >= wanted.patchVersion;
}

static NSString *EHFakeVersionString(id self, SEL _cmd) {
    (void)self; (void)_cmd;
    return [NSString stringWithFormat:@"Version %ld.%ld.%ld",
            (long)eh_fake_version.majorVersion,
            (long)eh_fake_version.minorVersion,
            (long)eh_fake_version.patchVersion];
}

__attribute__((constructor))
static void EHInstallVersionSpoof(void) {
    const char *wanted = getenv("EH_FAKE_OS_VERSION");
    if (!wanted || !*wanted) return;

    long major = 10, minor = 15, patch = 0;
    sscanf(wanted, "%ld.%ld.%ld", &major, &minor, &patch);
    eh_fake_version.majorVersion = major;
    eh_fake_version.minorVersion = minor;
    eh_fake_version.patchVersion = patch;
    eh_fake_version_active = YES;

    Class cls = [NSProcessInfo class];
    Method m = class_getInstanceMethod(cls, @selector(operatingSystemVersion));
    if (m) class_replaceMethod(cls, @selector(operatingSystemVersion),
                               (IMP)EHFakeOperatingSystemVersion, method_getTypeEncoding(m));
    m = class_getInstanceMethod(cls, @selector(isOperatingSystemAtLeastVersion:));
    if (m) class_replaceMethod(cls, @selector(isOperatingSystemAtLeastVersion:),
                               (IMP)EHFakeAtLeastVersion, method_getTypeEncoding(m));
    m = class_getInstanceMethod(cls, @selector(operatingSystemVersionString));
    if (m) class_replaceMethod(cls, @selector(operatingSystemVersionString),
                               (IMP)EHFakeVersionString, method_getTypeEncoding(m));
}

// ---- naming the selectors an application swallows ----
//
// Applications commonly install -forwardingTargetForSelector: on NSObject so
// an unrecognized selector returns nil instead of aborting. That keeps a
// process alive on an older OS, but it also means a missing API produces no
// error: a view builder returns nil, the panel it was building comes out
// blank, and nothing is logged.
//
// Setting EH_LOG_MISSING_SELECTORS in the environment prints each selector
// that reaches forwarding, once per class and selector pair:
//
//   EH-MISSING: +[NSTextField labelWithString:]
//
// which is the list of APIs the port still owes the application. The hook is
// installed only when the variable is set, so a normal run pays nothing.
//
// It is re-installed as each image loads: the application's own category on
// NSObject arrives after this library and would otherwise replace the hook,
// and re-installing chains onto whatever is current rather than displacing it.

#import <objc/runtime.h>
#import <objc/message.h>
#import <mach-o/dyld.h>

static id (*eh_original_forwarding_instance)(id, SEL, SEL);
static id (*eh_original_forwarding_class)(id, SEL, SEL);

// Built from C only. Formatting the message with NSString, or keeping the
// seen set in an NSSet, sends messages of its own; any one of them that is
// itself unrecognized re-enters this hook and recurses until the stack runs
// out. The recursion guard covers the same hazard inside the original
// implementation this chains onto.
#define EH_MISSING_TABLE_SIZE 1024
static char *eh_missing_seen[EH_MISSING_TABLE_SIZE];
static OSSpinLock eh_missing_lock = OS_SPINLOCK_INIT;
static __thread int eh_missing_depth;

static void EHNoteMissingSelector(id self, SEL aSelector, id target) {
    Class cls = object_getClass(self);
    BOOL isClassMethod = class_isMetaClass(cls);
    const char *clsName = isClassMethod ? class_getName((Class)self) : class_getName(cls);
    // A returned target means the application absorbed the message rather
    // than letting it abort, so it is named here too: either way the class
    // does not implement the selector.
    const char *absorbedBy = target ? class_getName(object_getClass(target)) : NULL;
    char key[512];
    snprintf(key, sizeof(key), "%c[%s %s]%s%s",
             isClassMethod ? '+' : '-', clsName, sel_getName(aSelector),
             absorbedBy ? " absorbed by " : "", absorbedBy ? absorbedBy : "");

    unsigned long h = 5381;
    for (const char *p = key; *p; p++) h = h * 33 + (unsigned char)*p;

    OSSpinLockLock(&eh_missing_lock);
    BOOL fresh = YES;
    size_t i = (size_t)(h % EH_MISSING_TABLE_SIZE);
    for (size_t probe = 0; probe < EH_MISSING_TABLE_SIZE; probe++) {
        size_t slot = (i + probe) % EH_MISSING_TABLE_SIZE;
        if (!eh_missing_seen[slot]) { eh_missing_seen[slot] = strdup(key); break; }
        if (strcmp(eh_missing_seen[slot], key) == 0) { fresh = NO; break; }
    }
    OSSpinLockUnlock(&eh_missing_lock);

    if (fresh) {
        fprintf(stderr, "EH-MISSING: %s\n", key);
        fflush(stderr);
    }
}

static id EHForwardingTargetInstance(id self, SEL _cmd, SEL aSelector) {
    if (eh_missing_depth) return nil;
    eh_missing_depth++;
    id target = eh_original_forwarding_instance
        ? eh_original_forwarding_instance(self, _cmd, aSelector) : nil;
    EHNoteMissingSelector(self, aSelector, target);
    eh_missing_depth--;
    return target;
}

static id EHForwardingTargetClass(id self, SEL _cmd, SEL aSelector) {
    if (eh_missing_depth) return nil;
    eh_missing_depth++;
    id target = eh_original_forwarding_class
        ? eh_original_forwarding_class(self, _cmd, aSelector) : nil;
    EHNoteMissingSelector(self, aSelector, target);
    eh_missing_depth--;
    return target;
}

static void EHInstallForwardingLogging(const struct mach_header *mh, intptr_t slide) {
    (void)mh; (void)slide;
    SEL sel = @selector(forwardingTargetForSelector:);
    Method m = class_getInstanceMethod([NSObject class], sel);
    if (m && method_getImplementation(m) != (IMP)EHForwardingTargetInstance) {
        eh_original_forwarding_instance =
            (id (*)(id, SEL, SEL))method_getImplementation(m);
        method_setImplementation(m, (IMP)EHForwardingTargetInstance);
    }
    Method cm = class_getClassMethod([NSObject class], sel);
    if (cm && method_getImplementation(cm) != (IMP)EHForwardingTargetClass) {
        eh_original_forwarding_class =
            (id (*)(id, SEL, SEL))method_getImplementation(cm);
        method_setImplementation(cm, (IMP)EHForwardingTargetClass);
    }
}

__attribute__((constructor))
static void EHMaybeLogMissingSelectors(void) {
    if (!getenv("EH_LOG_MISSING_SELECTORS")) return;
    _dyld_register_func_for_add_image(EHInstallForwardingLogging);
}

// ---- NSString substring test (10.10) ----

@implementation NSString (EHContainsString)

- (BOOL)containsString:(NSString *)str {
    // An empty needle is contained in every string, which is what
    // -rangeOfString: reports by returning NSNotFound for it.
    if ([str length] == 0) return YES;
    return [self rangeOfString:str].location != NSNotFound;
}

- (BOOL)localizedCaseInsensitiveContainsString:(NSString *)str {
    if ([str length] == 0) return YES;
    return [self rangeOfString:str
                      options:NSCaseInsensitiveSearch
                        range:NSMakeRange(0, [self length])
                       locale:[NSLocale currentLocale]].location != NSNotFound;
}

@end
