// Wrapper for CoreFoundation on OS X 10.9: supplies the symbols this OS's copy
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

#import <objc/runtime.h>

// Later runtimes hand out shared empty collections rather than allocating new
// ones, and a binary compiled against them refers to the storage directly: the
// address of the symbol is the object. So each is laid out as one, with a class
// pointer first, filled in by a constructor that runs before any code that
// could reach it.
@interface EHEmptyArray : NSArray @end
@implementation EHEmptyArray
- (NSUInteger)count { return 0; }
- (id)objectAtIndex:(NSUInteger)i { (void)i; return nil; }
- (instancetype)retain { return self; }
- (oneway void)release { }
- (instancetype)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }
@end

@interface EHEmptyDict : NSDictionary @end
@implementation EHEmptyDict
- (NSUInteger)count { return 0; }
- (id)objectForKey:(id)k { (void)k; return nil; }
- (NSEnumerator *)keyEnumerator { return [[NSArray array] objectEnumerator]; }
- (instancetype)retain { return self; }
- (oneway void)release { }
- (instancetype)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }
@end

struct EHConstantObject { Class isa; };

// Two spellings, two conventions -- established by how callers actually reach
// them, not by guesswork:
//
//   __NSArray0__ / __NSDictionary0__  hold a *pointer* to the shared empty
//       collection. A binary bound to these loads the word at the symbol and
//       messages that, so the symbol is a variable and not the object. (Get
//       this wrong and the message lands on the class object instead, which
//       shows up as "+[EHEmptyArray ...]: unrecognized selector sent to
//       class".)
//
//   __NSDictionary0__struct           *is* the object, laid out with its class
//       pointer first, because a binary bound to that spelling takes the
//       symbol's address as the object. Kept from an earlier port.
//
// The pointer spellings and the struct spelling refer to the same instance, so
// identity comparisons hold whichever way a caller arrives.
id __NSArray0__ = nil;
id __NSDictionary0__ = nil;
struct EHConstantObject __NSDictionary0__struct = { (Class)0 };

__attribute__((constructor))
static void EHInstallEmptyCollections(void) {
    __NSDictionary0__struct.isa = [EHEmptyDict class];
    __NSArray0__ = [[EHEmptyArray alloc] init];
    __NSDictionary0__ = (id)&__NSDictionary0__struct;
}
