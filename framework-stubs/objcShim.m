// Wrapper for /usr/lib/libobjc.A.dylib: supplies the Objective-C runtime
// helpers this OS's copy predates, and re-exports the real library so
// everything else still resolves.
//
// Hand-written (not generated): each of these has exact, documented semantics,
// so it is implemented rather than stubbed. Clang emits calls to them in place
// of ordinary message sends when the deployment target is new enough — a binary
// built that way loads fine here and then dies the first time one of the calls
// is reached, because the symbol is bound lazily.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <objc/message.h>

// The compiler emits this for [[Cls alloc] init].
id objc_alloc_init(Class cls) {
    id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    return send(send((id)cls, @selector(alloc)), @selector(init));
}

// ... and this for [Cls new].
id objc_opt_new(Class cls) {
    id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    return send((id)cls, @selector(new));
}

// [obj self], which is the object itself.
id objc_opt_self(id object) {
    return object;
}

// [obj class]. A class object answers with itself rather than its metaclass,
// which is why this is not simply object_getClass.
Class objc_opt_class(id object) {
    if (!object) return Nil;
    Class cls = object_getClass(object);
    return class_isMetaClass(cls) ? (Class)object : cls;
}

BOOL objc_opt_isKindOfClass(id object, Class cls) {
    BOOL (*send)(id, SEL, Class) = (BOOL (*)(id, SEL, Class))objc_msgSend;
    return object ? send(object, @selector(isKindOfClass:), cls) : NO;
}

BOOL objc_opt_respondsToSelector(id object, SEL selector) {
    BOOL (*send)(id, SEL, SEL) = (BOOL (*)(id, SEL, SEL))objc_msgSend;
    return object ? send(object, @selector(respondsToSelector:), selector) : NO;
}
