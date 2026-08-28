// Wrapper for CoreFoundation.framework on OS X 10.9. Adds the empty-dictionary
// singleton that later runtimes export, and re-exports the real framework for
// everything else.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

// Later runtimes hand out one shared empty dictionary rather than allocating a
// new one, and a binary compiled against them refers to its storage directly:
// the address of the symbol is the object. So it is laid out as one, with a
// class pointer first, and that pointer is filled in by a constructor, which
// runs before any code that could reach it.
@interface EHEmptyDictionary : NSDictionary
@end

@implementation EHEmptyDictionary

// The three primitives a NSDictionary subclass has to answer; the rest of the
// interface is built on them by the superclass.
- (NSUInteger)count { return 0; }
- (id)objectForKey:(id)key { (void)key; return nil; }
- (NSEnumerator *)keyEnumerator { return [[NSArray array] objectEnumerator]; }

// A constant object is never allocated and must never be freed, however many
// times it is retained.
- (instancetype)retain { return self; }
- (oneway void)release { }
- (instancetype)autorelease { return self; }
- (NSUInteger)retainCount { return NSUIntegerMax; }

@end

struct EHConstantObject { Class isa; };
struct EHConstantObject __NSDictionary0__struct = { (Class)0 };

__attribute__((constructor))
static void EHInstallEmptyDictionary(void) {
    __NSDictionary0__struct.isa = [EHEmptyDictionary class];
}
