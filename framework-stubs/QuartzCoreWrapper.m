// Wrapper for QuartzCore on OS X 10.9: supplies the symbols this OS's copy
// lacks, and re-exports the real one so everything else still resolves.
// Hand-written: the class list started from the symbols real binaries bind,
// but the constants and functions carry real values and implementations,
// so this file is maintained by hand and not regenerated.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

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
STUB_CLASS(CABackdropLayer)
STUB_CLASS(CAMetalLayer)

// Data constants. Where the real value is documented it is reproduced
// exactly, because callers compare and serialise these.
NSString * const kCACornerCurveCircular = @"circular";
NSString * const kCACornerCurveContinuous = @"continuous";
NSString * const kCAFilterDivideBlendMode = @"divideBlendMode";

// ---- hand-written ----

#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#import <objc/message.h>

/*
 * CALayer.delegate is a zeroing weak reference from macOS 10.12 onwards; on
 * this OS the property is declared `assign`, so the pointer dangles once the
 * delegate deallocates. QuartzCore reads that pointer straight out of the
 * layer -- actionForKey() messages it whenever a layer property animates or a
 * sublayer is removed -- so a delegate that outlives nothing crashes the
 * process on the next layout pass, with the freed object's reused memory read
 * as an isa.
 *
 * The zeroing is restored from the delegate's side. Setting a delegate hangs
 * an EHLayerBacklinks object off it, holding the layers that point back. That
 * object is released while the delegate is being destroyed, and clears every
 * layer still aimed at it.
 *
 * Backlinks retain their layers, so a layer stays alive as long as its
 * delegate does. That is the direction the ownership already runs -- a view
 * owns its layer, never the reverse -- so it extends a lifetime rather than
 * creating a cycle.
 */

static const void *kEHLayerBacklinks = &kEHLayerBacklinks;

@interface EHLayerBacklinks : NSObject {
@public
    // Not an object reference: the delegate is mid-destruction when this is
    // read, so it is only ever compared, never messaged.
    void *_owner;
    NSMutableArray *_layers;
}
@end

/*
 * -delegate is read through objc_msgSend typed to return void * rather than
 * id. A delegate that has begun deallocating may still be installed on a
 * layer, and an id return under ARC is retained and released on the spot,
 * which would resurrect and re-free it. A raw pointer is only compared.
 */
static void *EHRawDelegate(CALayer *layer) {
    void *(*rawMsgSend)(id, SEL) = (void *(*)(id, SEL))objc_msgSend;
    return rawMsgSend(layer, @selector(delegate));
}

@implementation EHLayerBacklinks

- (instancetype)initWithOwner:(void *)owner {
    if ((self = [super init])) {
        _owner = owner;
        _layers = [[NSMutableArray alloc] init];
    }
    return self;
}

- (void)addLayer:(CALayer *)layer {
    @synchronized (self) {
        if (![_layers containsObject:layer]) [_layers addObject:layer];
    }
}

- (void)dealloc {
    // Changing a layer property makes QuartzCore ask the current delegate for
    // an animation to run -- here, the very object being destroyed, whose
    // ivars are already gone by the time associations are removed. Disabling
    // actions for the duration skips that question, so the delegate is
    // detached without being messaged.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (CALayer *layer in _layers) {
        // A layer whose delegate has since been pointed elsewhere is not ours
        // to clear. Comparing the pointer does not dereference it.
        if (EHRawDelegate(layer) == _owner) [layer setDelegate:nil];
    }
    [CATransaction commit];
}

@end

static void (*eh_original_setDelegate)(CALayer *, SEL, id);

static void EHLayerSetDelegate(CALayer *self, SEL _cmd, id delegate) {
    eh_original_setDelegate(self, _cmd, delegate);
    if (!delegate) return;
    @synchronized (delegate) {
        EHLayerBacklinks *links = objc_getAssociatedObject(delegate, kEHLayerBacklinks);
        if (!links) {
            links = [[EHLayerBacklinks alloc] initWithOwner:(__bridge void *)delegate];
            objc_setAssociatedObject(delegate, kEHLayerBacklinks, links,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        [links addLayer:self];
    }
}

__attribute__((constructor))
static void EHInstallLayerDelegateZeroing(void) {
    Method m = class_getInstanceMethod([CALayer class], @selector(setDelegate:));
    if (!m) return;
    eh_original_setDelegate = (void (*)(CALayer *, SEL, id))method_getImplementation(m);
    method_setImplementation(m, (IMP)EHLayerSetDelegate);
}

// ---- CALayer corner selection (10.13) ----

static const void *kEHMaskedCorners = &kEHMaskedCorners;

@implementation CALayer (EHMaskedCorners)

// cornerRadius rounds every corner on this OS. The selection is recorded so a
// caller reads back what it set; a layer asking for a subset of corners gets
// all of them rounded, which is the closest this OS can draw.
- (void)setMaskedCorners:(NSUInteger)corners {
    objc_setAssociatedObject(self, kEHMaskedCorners, @(corners),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (NSUInteger)maskedCorners {
    id stored = objc_getAssociatedObject(self, kEHMaskedCorners);
    // All four corners, matching the radius this OS actually applies.
    return stored ? [stored unsignedIntegerValue] : 0xF;
}

@end

// ---- structural layer changes do not run implicit animations ----
//
// Replacing or detaching sublayers makes QuartzCore ask each affected layer's
// delegate for an animation to run. CALayer.delegate is unretained here (see
// above), and AppKit sets it on layers it owns without going through
// -setDelegate:, so those are outside what the zeroing above can reach. During
// teardown the delegate is frequently gone already, and the lookup messages
// freed memory:
//
//   objc_msgSend + 35
//   actionForKey(CALayer*, CA::Transaction*, NSString*)
//   CA::Layer::remove_sublayer(...)
//
// Suppressing actions for the duration of the change skips the lookup. It
// costs the implicit animation on a sublayer being added or removed, which is
// not something layout code relies on, and it is the only part of the delegate
// contract these paths use.

static void (*eh_original_setSublayers)(CALayer *, SEL, NSArray *);
static void (*eh_original_removeFromSuperlayer)(CALayer *, SEL);

/* Detaching a layer releases it, and that release can cascade far enough to
 * deallocate the delegate of a layer QuartzCore has not finished with. The
 * delegates are all still valid on the way in, so holding a reference to each
 * of them across the call keeps any of them from being freed midway. */
static void EHCollectDelegates(CALayer *layer, NSMutableArray *into, int depth) {
    if (!layer || depth > 32) return;
    id own = [layer delegate];
    if (own) [into addObject:own];
    // Detaching a layer detaches everything under it, and the release cascade
    // reaches those delegates too, so the whole subtree is collected.
    for (CALayer *sublayer in [layer sublayers])
        EHCollectDelegates(sublayer, into, depth + 1);
}

static NSArray *EHDelegatesInvolvedIn(CALayer *layer) {
    NSMutableArray *delegates = [NSMutableArray array];
    EHCollectDelegates(layer, delegates, 0);
    return delegates;
}

static void EHLayerSetSublayers(CALayer *self, SEL _cmd, NSArray *sublayers) {
    NSArray *held = EHDelegatesInvolvedIn(self);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    eh_original_setSublayers(self, _cmd, sublayers);
    [CATransaction commit];
    [held self];
}

static void EHLayerRemoveFromSuperlayer(CALayer *self, SEL _cmd) {
    NSArray *held = EHDelegatesInvolvedIn(self);
    CALayer *super = [self superlayer];
    NSArray *heldFromSuper = super ? EHDelegatesInvolvedIn(super) : nil;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    eh_original_removeFromSuperlayer(self, _cmd);
    [CATransaction commit];
    [held self];
    [heldFromSuper self];
}

__attribute__((constructor))
static void EHInstallStructuralActionSuppression(void) {
    Method s = class_getInstanceMethod([CALayer class], @selector(setSublayers:));
    if (s) {
        eh_original_setSublayers =
            (void (*)(CALayer *, SEL, NSArray *))method_getImplementation(s);
        method_setImplementation(s, (IMP)EHLayerSetSublayers);
    }
    Method r = class_getInstanceMethod([CALayer class], @selector(removeFromSuperlayer));
    if (r) {
        eh_original_removeFromSuperlayer =
            (void (*)(CALayer *, SEL))method_getImplementation(r);
        method_setImplementation(r, (IMP)EHLayerRemoveFromSuperlayer);
    }
}
