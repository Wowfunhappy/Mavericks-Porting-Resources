// Wrapper for libxar on OS X 10.9: supplies the symbols this OS's copy
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

#include <stdlib.h>
#include <string.h>

// xar_get_safe_path was added when xar was hardened against path traversal:
// it returns the entry's path with any leading "/" and any ".." component
// removed, so an archive cannot write outside its destination. 10.9's libxar
// has only xar_get_path, so the sanitising is done here -- that security
// property is the entire reason the newer call exists and must not be lost by
// forwarding straight to the old one.
extern char *xar_get_path(void *f);

char *xar_get_safe_path(void *f) {
    char *raw = xar_get_path(f);
    if (!raw) return NULL;

    char *out = calloc(1, strlen(raw) + 1);
    if (!out) { free(raw); return NULL; }

    char *save = NULL;
    for (char *tok = strtok_r(raw, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        if (strcmp(tok, "..") == 0 || strcmp(tok, ".") == 0) continue;
        if (out[0]) strcat(out, "/");
        strcat(out, tok);
    }
    free(raw);
    return out;
}
