// Wrapper for AppKit on OS X 10.9: supplies the symbols this OS's copy
// lacks, and re-exports the real one so everything else still resolves.
// Hand-written: the class list started from the symbols real binaries bind,
// but the constants and functions carry real values and implementations,
// so this file is maintained by hand and not regenerated.
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

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
// Base classes first: a subclass needs its superclass already declared, and
// NSCollectionViewLayout (10.11) is not bound by anything here but is needed
// as the base the two concrete layouts inherit from.
STUB_CLASS(NSCollectionViewLayout)
STUB_CLASS(NSGestureRecognizer)

STUB_CLASS(NSAccessibilityCustomAction)
STUB_CLASS(NSAccessibilityElement)
STUB_CLASS(NSCollectionViewLayoutAttributes)
STUB_CLASS(NSCustomTouchBarItem)
STUB_CLASS(NSDataAsset)
STUB_CLASS(NSImageSymbolConfiguration)
STUB_CLASS(NSPopoverTouchBarItem)
STUB_CLASS(NSSplitViewItem)
STUB_CLASS(NSTouchBar)
STUB_CLASS(NSTouchBarItem)
STUB_CLASS(NSWorkspaceOpenConfiguration)

STUB_SUBCLASS(NSClickGestureRecognizer, NSGestureRecognizer)
STUB_SUBCLASS(NSPanGestureRecognizer, NSGestureRecognizer)
STUB_SUBCLASS(NSCollectionViewFlowLayout, NSCollectionViewLayout)
STUB_SUBCLASS(NSCollectionViewGridLayout, NSCollectionViewLayout)
STUB_SUBCLASS(NSSplitViewController, NSViewController)
STUB_SUBCLASS(NSTabViewController, NSViewController)

// NSVisualEffectView is implemented further down: it needs real properties
// and a background, not a forwarding stub.

// Data constants. Where the real value is documented it is reproduced
// exactly, because callers compare and serialise these.
NSString * const NSCollectionElementKindSectionFooter = @"UICollectionElementKindSectionFooter";
NSString * const NSCollectionElementKindSectionHeader = @"UICollectionElementKindSectionHeader";
NSString * const NSImageNameTouchBarCommunicationAudioTemplate = @"NSImageNameTouchBarCommunicationAudioTemplate";
NSString * const NSImageNameTouchBarCommunicationVideoTemplate = @"NSImageNameTouchBarCommunicationVideoTemplate";
NSString * const NSImageNameTouchBarPauseTemplate = @"NSImageNameTouchBarPauseTemplate";
NSString * const NSImageNameTouchBarPlayTemplate = @"NSImageNameTouchBarPlayTemplate";
NSString * const NSImageNameTouchBarRecordStopTemplate = @"NSImageNameTouchBarRecordStopTemplate";
NSString * const NSImageNameTouchBarSearchTemplate = @"NSImageNameTouchBarSearchTemplate";
NSString * const NSPasteboardTypeFileURL = @"public.file-url";
NSString * const NSPasteboardTypeURL = @"public.url";
NSString * const NSTextListMarkerDecimal = @"decimal";
NSString * const NSTextListMarkerDisc = @"disc";
NSString * const NSTouchBarItemIdentifierFlexibleSpace = @"NSTouchBarItemIdentifierFlexibleSpace";
NSString * const NSTouchBarItemIdentifierOtherItemsProxy = @"NSTouchBarItemIdentifierOtherItemsProxy";
NSString * const NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification = @"NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification";

// ---- hand-written ----

// Font weights are plain CGFloat constants; these are the documented values.
const CGFloat NSFontWeightUltraLight = -0.80;
const CGFloat NSFontWeightLight      = -0.40;
const CGFloat NSFontWeightRegular    =  0.00;
const CGFloat NSFontWeightMedium     =  0.23;
const CGFloat NSFontWeightSemibold   =  0.30;
const CGFloat NSFontWeightBold       =  0.40;

// Sentinels meaning "no preference"; both are documented values.
const CGFloat NSViewNoIntrinsicMetric = -1.0;
const CGFloat NSSplitViewItemUnspecifiedDimension = CGFLOAT_MAX;

// Converts a rect from a view's coordinates to screen coordinates for
// accessibility. 10.9 has every piece of this; only the convenience is new.
NSRect NSAccessibilityFrameInView(NSView *parentView, NSRect frame) {
    if (!parentView) return NSZeroRect;
    NSRect inWindow = [parentView convertRect:frame toView:nil];
    NSWindow *window = [parentView window];
    if (!window) return inWindow;
    return [window convertRectToScreen:inWindow];
}

// Kept from an earlier port: 10.9 has no haptics hardware, so the manager
// exists only to satisfy the bind.
STUB_CLASS(NSHapticFeedbackManager)

// Appearance names 10.9 does not have. 10.9 ships only Aqua, Content and
// LightContent -- no dark mode, no vibrancy.
//
// These keep their real values. Giving them Aqua's value instead makes
// +appearanceNamed: return something, but it also makes every "is this the
// dark appearance?" test succeed, because the running appearance really is
// named NSAppearanceNameAqua. An app that asks then paints its dark palette
// -- light text -- onto a light background, and the window renders blank.
//
// The nil is fixed where it actually arises, in +appearanceNamed:, below.
NSString * const NSAppearanceNameDarkAqua = @"NSAppearanceNameDarkAqua";
NSString * const NSAppearanceNameVibrantDark = @"NSAppearanceNameVibrantDark";
NSString * const NSAppearanceNameVibrantLight = @"NSAppearanceNameVibrantLight";

// +[NSAppearance appearanceNamed:] returns nil for a name this OS does not
// know, and callers do not expect nil: it travels until something rejects it.
// Every appearance that exists here is Aqua, so an unknown name resolves to
// Aqua -- the honest answer -- while the *names* stay distinct, so a caller
// comparing them still learns that this is not the dark appearance.
static NSAppearance *(*eh_original_appearanceNamed)(id, SEL, NSString *);

static NSAppearance *EHAppearanceNamed(id self, SEL _cmd, NSString *name) {
    NSAppearance *appearance = eh_original_appearanceNamed(self, _cmd, name);
    if (appearance) return appearance;
    return eh_original_appearanceNamed(self, _cmd, NSAppearanceNameAqua);
}

__attribute__((constructor))
static void EHInstallAppearanceFallback(void) {
    Class meta = objc_getMetaClass("NSAppearance");
    if (!meta) return;
    SEL sel = sel_registerName("appearanceNamed:");
    IMP previous = class_replaceMethod(meta, sel, (IMP)EHAppearanceNamed, "@@:@");
    if (previous)
        eh_original_appearanceNamed = (NSAppearance *(*)(id, SEL, NSString *))previous;
}

// Weight-taking system font selectors, added in 10.11. A caller that finds
// them missing falls back to naming a font family directly -- Zoom asks for
// "SFProText-Light", which 10.9 also lacks -- and +[NSFont fontWithName:size:]
// answers nil for an unknown family. The nil then reaches -[NSTextView
// setFont:], which raises "nil NSFont given".
//
// Implementing them keeps callers on their primary path. 10.9's system font is
// Lucida Grande, which comes in regular and bold only, so the continuous
// weight axis collapses onto those two. That is a real loss of typographic
// fidelity and not a stub: every call returns a usable font of about the right
// heaviness.
@implementation NSFont (EHWeightedSystemFonts)

+ (NSFont *)systemFontOfSize:(CGFloat)size weight:(CGFloat)weight {
    NSFont *font = (weight >= NSFontWeightSemibold)
                 ? [NSFont boldSystemFontOfSize:size]
                 : [NSFont systemFontOfSize:size];
    // systemFontOfSize: itself returns nil for a non-positive size, and a
    // caller asking for one still must not receive nil here.
    return font ?: [NSFont systemFontOfSize:[NSFont systemFontSize]];
}

+ (NSFont *)monospacedDigitSystemFontOfSize:(CGFloat)size weight:(CGFloat)weight {
    // 10.9 has no tabular-figures variant to ask for, so this differs from the
    // real thing only in that digits are not guaranteed equal-width.
    return [self systemFontOfSize:size weight:weight];
}

@end

// NSColor factories 10.9 lacks. Each has a real answer here, so each is
// implemented rather than stubbed; returning nil is what breaks callers, and
// a nil colour travels a long way before anything rejects it:
//
//   *** setObjectForKey: object cannot be nil (key: fill)
@implementation NSColor (EHModernColorFactories)

// 10.15. A dynamic colour resolves itself against whatever appearance is
// current when it is drawn. 10.9 has exactly one appearance, so resolving it
// once, now, gives the same answer it would give every time later.
+ (NSColor *)colorWithName:(NSString *)name
           dynamicProvider:(NSColor * (^)(NSAppearance *))provider {
    (void)name;
    if (!provider) return nil;
    NSAppearance *appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
    return provider(appearance);
}

// 10.12. There is no Display P3 colour space here, and sRGB is the widest this
// OS offers -- the same substitution the system makes for a P3 asset on a
// display that cannot show it. Colours outside sRGB's gamut are clipped.
+ (NSColor *)colorWithDisplayP3Red:(CGFloat)red green:(CGFloat)green
                              blue:(CGFloat)blue alpha:(CGFloat)alpha {
    return [NSColor colorWithSRGBRed:red green:green blue:blue alpha:alpha];
}

// 10.12. Built from the calibrated-HSB form 10.9 does have, then converted
// into the requested space, which is exactly what the real one produces.
+ (NSColor *)colorWithColorSpace:(NSColorSpace *)space
                             hue:(CGFloat)hue
                      saturation:(CGFloat)saturation
                      brightness:(CGFloat)brightness
                           alpha:(CGFloat)alpha {
    NSColor *hsb = [NSColor colorWithCalibratedHue:hue saturation:saturation
                                        brightness:brightness alpha:alpha];
    if (!space) return hsb;
    NSColor *converted = [hsb colorUsingColorSpace:space];
    return converted ?: hsb;
}

@end

// NSStackView's arranged-subview API, added in 10.11.
//
// 10.9 has the same capability under the older gravity-based names, so these
// are real implementations over -addView:inGravity: and friends, not stubs.
// Without them a caller's views never join the stack, and the failure surfaces
// somewhere else entirely -- at the next call that expects them to be there:
//
//   View <ZMLabel: 0x...> is not (and has to be) in stack view <NSStackView: 0x...>
//
// NSStackViewGravityTop and NSStackViewGravityLeading are both 1, so one
// constant serves whichever orientation the stack has.
#define EH_STACK_GRAVITY_FIRST 1

@implementation NSStackView (EHArrangedSubviews)

// The 10.11 API turns off autoresizing translation for the view it adopts;
// -addView:inGravity: does not. Left on, the view keeps its autoresizing mask
// as constraints, those fight the stack's own, and AppKit resolves the
// conflict by breaking constraints until something collapses to nothing:
//
//   "<NSAutoresizingMaskLayoutConstraint ZMButton.(null) == 0>"
//
// so the view is present but zero-sized, and the window looks empty.
// Adding a view that is already arranged moves it to the end; it does not add
// it twice. Appending unconditionally puts the same view in the list twice and
// AppKit refuses the result:
//
//   Parameter to -setSubviews: contained one or more duplicate entries
//
// Callers rely on the re-add being harmless, so this has to match.
- (void)addArrangedSubview:(NSView *)view {
    if (!view) return;
    [view setTranslatesAutoresizingMaskIntoConstraints:NO];
    [self addView:view inGravity:EH_STACK_GRAVITY_FIRST];
}

- (void)insertArrangedSubview:(NSView *)view atIndex:(NSInteger)index {
    if (!view) return;
    [view setTranslatesAutoresizingMaskIntoConstraints:NO];
    [self insertView:view atIndex:index inGravity:EH_STACK_GRAVITY_FIRST];
}

- (void)removeArrangedSubview:(NSView *)view {
    // The 10.11 method takes the view out of the arrangement but leaves it a
    // subview; -removeView: does both. Callers overwhelmingly discard the view
    // straight after, and leaving it arranged would be the worse error.
    if (view) [self removeView:view];
}

- (NSArray *)arrangedSubviews {
    return [self views];
}

// 10.11. Distribution controls how the stack shares space along its axis.
// 10.9 gives the surplus to whichever arranged view will take it, so a row of
// equally sized cells -- the six boxes of a verification code, say -- comes
// out as one full-width box and five of zero width, with nothing to click.
//
// FillEqually is reproduced by constraining every arranged view to match the
// first along the axis, which is what it means. The remaining distributions
// differ from 10.9's behaviour only in how leftover space is shared, so they
// are recorded and the stack keeps its own behaviour.

static const void *kEHDistribution = &kEHDistribution;
static const void *kEHDistributionConstraints = &kEHDistributionConstraints;
static const void *kEHDistributionViews = &kEHDistributionViews;

#define EH_STACK_DISTRIBUTION_FILL_EQUALLY 1

- (void)eh_applyDistribution {
    NSArray *arrangedNow = [self views];
    NSArray *builtFor = objc_getAssociatedObject(self, kEHDistributionViews);
    if (builtFor && [builtFor isEqualToArray:arrangedNow]) return;
    objc_setAssociatedObject(self, kEHDistributionViews, [arrangedNow copy],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    NSArray *previous = objc_getAssociatedObject(self, kEHDistributionConstraints);
    if (previous) {
        [self removeConstraints:previous];
        objc_setAssociatedObject(self, kEHDistributionConstraints, nil,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    NSArray *arranged = [self views];
    if ([arranged count] < 2) return;
    NSLayoutAttribute axis =
        ([self orientation] == NSUserInterfaceLayoutOrientationHorizontal)
            ? NSLayoutAttributeWidth : NSLayoutAttributeHeight;
    BOOL equally = [objc_getAssociatedObject(self, kEHDistribution) integerValue]
                       == EH_STACK_DISTRIBUTION_FILL_EQUALLY;
    // An arranged view with no intrinsic size and no size constraint of its own
    // says nothing about how wide it should be. 10.9 resolves that by giving
    // the first such view the whole run and the rest nothing, which is never
    // what the caller meant -- a row of six code-entry boxes comes out as one
    // full-width box and five of zero width. When every arranged view is
    // unsized, sharing the run between them is the only reading that produces
    // a usable result, and it matches what later releases lay out.
    if (!equally) {
        BOOL allUnsized = YES;
        for (NSView *view in arranged) {
            NSSize intrinsic = [view intrinsicContentSize];
            CGFloat along = (axis == NSLayoutAttributeWidth) ? intrinsic.width
                                                            : intrinsic.height;
            if (along != NSViewNoInstrinsicMetric) { allUnsized = NO; break; }
            for (NSLayoutConstraint *c in [view constraints]) {
                if ([c firstAttribute] != axis) continue;
                if ([c firstItem] != view) continue;
                if ([c secondItem]) continue;   /* a ratio, not a fixed size */
                allUnsized = NO;
                break;
            }
            if (!allUnsized) break;
        }
        if (!allUnsized) return;
    }
    NSView *reference = [arranged objectAtIndex:0];
    NSMutableArray *equal = [NSMutableArray array];
    for (NSUInteger i = 1; i < [arranged count]; i++)
        [equal addObject:[NSLayoutConstraint
            constraintWithItem:[arranged objectAtIndex:i] attribute:axis
                     relatedBy:NSLayoutRelationEqual
                        toItem:reference attribute:axis
                    multiplier:1.0 constant:0.0]];
    [self addConstraints:equal];
    objc_setAssociatedObject(self, kEHDistributionConstraints, equal,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)setDistribution:(NSInteger)distribution {
    objc_setAssociatedObject(self, kEHDistribution, @(distribution),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(self, kEHDistributionViews, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [self eh_applyDistribution];
}

- (NSInteger)distribution {
    return [objc_getAssociatedObject(self, kEHDistribution) integerValue];
}

@end

// ---- re-adding an arranged view moves it instead of duplicating it ----
//
// Adding a view a stack already arranges is expected to move it to the new
// position. Here it appends a second reference, and the stack rejects its own
// subview list on the next layout pass:
//
//   Parameter to -setSubviews: contained one or more duplicate entries
//
// The two primitives every other entry point funnels through take the view out
// first, so callers that reach them directly are covered too.

static void (*eh_original_addViewInGravity)(NSStackView *, SEL, NSView *, NSInteger);
static void (*eh_original_insertViewAtIndexInGravity)(NSStackView *, SEL, NSView *, NSInteger, NSInteger);

static void EHStackAddViewInGravity(NSStackView *self, SEL _cmd,
                                    NSView *view, NSInteger gravity) {
    if (view && [[self views] containsObject:view]) [self removeView:view];
    eh_original_addViewInGravity(self, _cmd, view, gravity);
    [self eh_applyDistribution];
}

static void EHStackInsertViewAtIndexInGravity(NSStackView *self, SEL _cmd,
                                              NSView *view, NSInteger index,
                                              NSInteger gravity) {
    if (view && [[self views] containsObject:view]) {
        [self removeView:view];
        // Removing the view shortens the gravity it came from, so an index
        // that was in range before can now be past the end.
        NSInteger count = (NSInteger)[[self viewsInGravity:gravity] count];
        if (index > count) index = count;
    }
    eh_original_insertViewAtIndexInGravity(self, _cmd, view, index, gravity);
    [self eh_applyDistribution];
}

__attribute__((constructor))
static void EHInstallStackViewDeduplication(void) {
    Method add = class_getInstanceMethod([NSStackView class],
                                         @selector(addView:inGravity:));
    if (add) {
        eh_original_addViewInGravity = (void (*)(NSStackView *, SEL, NSView *, NSInteger))
            method_getImplementation(add);
        method_setImplementation(add, (IMP)EHStackAddViewInGravity);
    }
    Method ins = class_getInstanceMethod([NSStackView class],
                                         @selector(insertView:atIndex:inGravity:));
    if (ins) {
        eh_original_insertViewAtIndexInGravity =
            (void (*)(NSStackView *, SEL, NSView *, NSInteger, NSInteger))
            method_getImplementation(ins);
        method_setImplementation(ins, (IMP)EHStackInsertViewAtIndexInGravity);
    }
}

// ---- Auto Layout anchors and layout guides (10.11), activation (10.10) ----
//
// Modern layout code is written almost entirely in terms of anchors:
//
//   [view.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor constant:8].active = YES;
//
// None of that exists on 10.9, which has only the verbose
// +constraintWithItem:attribute:relatedBy:toItem:attribute:multiplier:constant:
// and requires constraints to be added to a view by hand. An app built against
// a newer SDK therefore lays out nothing: its containers come out zero-width,
// its contents pile up far below the window, and the window looks blank while
// the accessibility tree shows every control present and correctly sized.
//
// These are real implementations over the 10.9 constraint API, not stubs. An
// anchor is a (item, attribute) pair; a constraint built from two anchors is
// exactly the constraint the verbose call would have produced; activating one
// adds it to the nearest view that encloses both items, which is what AppKit
// does internally.
//
// A layout guide is backed by an invisible, non-interactive view, which is the
// standard way to express one before NSLayoutGuide existed: it occupies no
// pixels and takes no clicks, but the constraint system can refer to it.

@interface NSLayoutAnchor : NSObject
@property (nonatomic, weak, readonly) id eh_item;
@property (nonatomic, readonly) NSLayoutAttribute eh_attribute;
@end

// Declared ahead of use: -safeAreaLayoutGuide below builds its constraints
// with these, and the implementations come later in the file.
@interface NSLayoutConstraint (EHActivation)
- (void)setActive:(BOOL)active;
- (BOOL)isActive;
+ (void)activateConstraints:(NSArray *)constraints;
+ (void)deactivateConstraints:(NSArray *)constraints;
@end

@interface NSLayoutXAxisAnchor : NSLayoutAnchor @end
@interface NSLayoutYAxisAnchor : NSLayoutAnchor @end
@interface NSLayoutDimension : NSLayoutAnchor @end

@interface EHLayoutGuideView : NSView @end
@implementation EHLayoutGuideView
- (void)drawRect:(NSRect)dirtyRect { (void)dirtyRect; }
- (NSView *)hitTest:(NSPoint)point { (void)point; return nil; }   // never takes a click
- (BOOL)isOpaque { return NO; }
@end

// The view a constraint can actually name: a view is itself, a guide is its
// backing view.
static NSView *EHConstraintItemView(id item) {
    if ([item isKindOfClass:[NSView class]]) return item;
    if ([item respondsToSelector:@selector(eh_backingView)])
        return [item performSelector:@selector(eh_backingView)];
    return nil;
}

@implementation NSLayoutAnchor {
    __weak id _item;
    NSLayoutAttribute _attribute;
}
@synthesize eh_item = _item, eh_attribute = _attribute;

+ (instancetype)eh_anchorForItem:(id)item attribute:(NSLayoutAttribute)attribute {
    NSLayoutAnchor *anchor = [[self alloc] init];
    if (anchor) { anchor->_item = item; anchor->_attribute = attribute; }
    return anchor;
}

- (NSLayoutConstraint *)eh_constraintTo:(NSLayoutAnchor *)other
                              relation:(NSLayoutRelation)relation
                            multiplier:(CGFloat)multiplier
                              constant:(CGFloat)constant {
    NSView *first = EHConstraintItemView(_item);
    NSView *second = other ? EHConstraintItemView(other.eh_item) : nil;
    if (!first) return nil;
    return [NSLayoutConstraint constraintWithItem:first
                                        attribute:_attribute
                                        relatedBy:relation
                                           toItem:second
                                        attribute:other ? other.eh_attribute : NSLayoutAttributeNotAnAttribute
                                       multiplier:multiplier
                                         constant:constant];
}

- (NSLayoutConstraint *)constraintEqualToAnchor:(NSLayoutAnchor *)anchor {
    return [self eh_constraintTo:anchor relation:NSLayoutRelationEqual multiplier:1 constant:0];
}
- (NSLayoutConstraint *)constraintEqualToAnchor:(NSLayoutAnchor *)anchor constant:(CGFloat)c {
    return [self eh_constraintTo:anchor relation:NSLayoutRelationEqual multiplier:1 constant:c];
}
- (NSLayoutConstraint *)constraintGreaterThanOrEqualToAnchor:(NSLayoutAnchor *)anchor {
    return [self eh_constraintTo:anchor relation:NSLayoutRelationGreaterThanOrEqual multiplier:1 constant:0];
}
- (NSLayoutConstraint *)constraintGreaterThanOrEqualToAnchor:(NSLayoutAnchor *)anchor constant:(CGFloat)c {
    return [self eh_constraintTo:anchor relation:NSLayoutRelationGreaterThanOrEqual multiplier:1 constant:c];
}
- (NSLayoutConstraint *)constraintLessThanOrEqualToAnchor:(NSLayoutAnchor *)anchor {
    return [self eh_constraintTo:anchor relation:NSLayoutRelationLessThanOrEqual multiplier:1 constant:0];
}
- (NSLayoutConstraint *)constraintLessThanOrEqualToAnchor:(NSLayoutAnchor *)anchor constant:(CGFloat)c {
    return [self eh_constraintTo:anchor relation:NSLayoutRelationLessThanOrEqual multiplier:1 constant:c];
}
@end

@implementation NSLayoutXAxisAnchor @end
@implementation NSLayoutYAxisAnchor @end

@implementation NSLayoutDimension
- (NSLayoutConstraint *)constraintEqualToConstant:(CGFloat)c {
    return [self eh_constraintTo:nil relation:NSLayoutRelationEqual multiplier:1 constant:c];
}
- (NSLayoutConstraint *)constraintGreaterThanOrEqualToConstant:(CGFloat)c {
    return [self eh_constraintTo:nil relation:NSLayoutRelationGreaterThanOrEqual multiplier:1 constant:c];
}
- (NSLayoutConstraint *)constraintLessThanOrEqualToConstant:(CGFloat)c {
    return [self eh_constraintTo:nil relation:NSLayoutRelationLessThanOrEqual multiplier:1 constant:c];
}
- (NSLayoutConstraint *)constraintEqualToAnchor:(NSLayoutDimension *)a multiplier:(CGFloat)m {
    return [self eh_constraintTo:a relation:NSLayoutRelationEqual multiplier:m constant:0];
}
- (NSLayoutConstraint *)constraintEqualToAnchor:(NSLayoutDimension *)a multiplier:(CGFloat)m constant:(CGFloat)c {
    return [self eh_constraintTo:a relation:NSLayoutRelationEqual multiplier:m constant:c];
}
- (NSLayoutConstraint *)constraintGreaterThanOrEqualToAnchor:(NSLayoutDimension *)a multiplier:(CGFloat)m {
    return [self eh_constraintTo:a relation:NSLayoutRelationGreaterThanOrEqual multiplier:m constant:0];
}
- (NSLayoutConstraint *)constraintLessThanOrEqualToAnchor:(NSLayoutDimension *)a multiplier:(CGFloat)m {
    return [self eh_constraintTo:a relation:NSLayoutRelationLessThanOrEqual multiplier:m constant:0];
}
@end

// ---- NSLayoutGuide ----

@interface NSLayoutGuide : NSObject
@property (nonatomic, weak) NSView *owningView;
@property (nonatomic, copy) NSString *identifier;
- (NSView *)eh_backingView;
@end

@implementation NSLayoutGuide {
    EHLayoutGuideView *_backing;
}
@synthesize owningView = _owningView, identifier = _identifier;

- (NSView *)eh_backingView {
    if (!_backing) {
        _backing = [[EHLayoutGuideView alloc] initWithFrame:NSZeroRect];
        [_backing setTranslatesAutoresizingMaskIntoConstraints:NO];
    }
    return _backing;
}

- (void)setOwningView:(NSView *)view {
    _owningView = view;
    NSView *backing = [self eh_backingView];
    if (view && [backing superview] != view) [view addSubview:backing];
}

- (NSRect)frame { return _backing ? [_backing frame] : NSZeroRect; }

- (NSLayoutXAxisAnchor *)leadingAnchor  { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeLeading]; }
- (NSLayoutXAxisAnchor *)trailingAnchor { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeTrailing]; }
- (NSLayoutXAxisAnchor *)leftAnchor     { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeLeft]; }
- (NSLayoutXAxisAnchor *)rightAnchor    { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeRight]; }
- (NSLayoutYAxisAnchor *)topAnchor      { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeTop]; }
- (NSLayoutYAxisAnchor *)bottomAnchor   { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeBottom]; }
- (NSLayoutDimension *)widthAnchor      { return [NSLayoutDimension eh_anchorForItem:self attribute:NSLayoutAttributeWidth]; }
- (NSLayoutDimension *)heightAnchor     { return [NSLayoutDimension eh_anchorForItem:self attribute:NSLayoutAttributeHeight]; }
- (NSLayoutXAxisAnchor *)centerXAnchor  { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeCenterX]; }
- (NSLayoutYAxisAnchor *)centerYAnchor  { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeCenterY]; }
@end

// ---- anchors on NSView ----

@implementation NSView (EHLayoutAnchors)

- (NSLayoutXAxisAnchor *)leadingAnchor  { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeLeading]; }
- (NSLayoutXAxisAnchor *)trailingAnchor { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeTrailing]; }
- (NSLayoutXAxisAnchor *)leftAnchor     { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeLeft]; }
- (NSLayoutXAxisAnchor *)rightAnchor    { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeRight]; }
- (NSLayoutYAxisAnchor *)topAnchor      { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeTop]; }
- (NSLayoutYAxisAnchor *)bottomAnchor   { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeBottom]; }
- (NSLayoutDimension *)widthAnchor      { return [NSLayoutDimension eh_anchorForItem:self attribute:NSLayoutAttributeWidth]; }
- (NSLayoutDimension *)heightAnchor     { return [NSLayoutDimension eh_anchorForItem:self attribute:NSLayoutAttributeHeight]; }
- (NSLayoutXAxisAnchor *)centerXAnchor  { return [NSLayoutXAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeCenterX]; }
- (NSLayoutYAxisAnchor *)centerYAnchor  { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeCenterY]; }

// 10.9 has one baseline attribute; first and last coincide for a single-line
// view and differ only for multi-line text, where this is an approximation.
- (NSLayoutYAxisAnchor *)firstBaselineAnchor { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeBaseline]; }
- (NSLayoutYAxisAnchor *)lastBaselineAnchor  { return [NSLayoutYAxisAnchor eh_anchorForItem:self attribute:NSLayoutAttributeBaseline]; }

- (void)addLayoutGuide:(NSLayoutGuide *)guide { [guide setOwningView:self]; }
- (void)removeLayoutGuide:(NSLayoutGuide *)guide {
    [[guide eh_backingView] removeFromSuperview];
    [guide setOwningView:nil];
}

// 10.14. There are no safe-area insets on 10.9 -- no notch, no rounded
// corners -- so the safe area is the whole view, and a guide pinned to its
// edges says exactly that.
- (NSLayoutGuide *)safeAreaLayoutGuide {
    static const char key;
    NSLayoutGuide *guide = objc_getAssociatedObject(self, &key);
    if (!guide) {
        guide = [[NSLayoutGuide alloc] init];
        [self addLayoutGuide:guide];
        NSView *backing = [guide eh_backingView];
        [NSLayoutConstraint activateConstraints:@[
            [backing.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [backing.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [backing.topAnchor constraintEqualToAnchor:self.topAnchor],
            [backing.bottomAnchor constraintEqualToAnchor:self.bottomAnchor]]];
        objc_setAssociatedObject(self, &key, guide, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return guide;
}

- (NSEdgeInsets)safeAreaInsets { return NSEdgeInsetsMake(0, 0, 0, 0); }

@end

// ---- constraint activation ----

@implementation NSLayoutConstraint (EHActivation)

// The real implementation installs a constraint on the closest view that
// encloses both of its items; that is the view whose layout the constraint
// belongs to.
static NSView *EHNearestCommonAncestor(NSView *a, NSView *b) {
    if (!a) return b;
    if (!b) return a;
    for (NSView *candidate = a; candidate; candidate = [candidate superview])
        if ([b isDescendantOf:candidate]) return candidate;
    return a;
}

- (void)setActive:(BOOL)active {
    static const char hostKey;
    NSView *first = EHConstraintItemView([self firstItem]);
    NSView *second = EHConstraintItemView([self secondItem]);
    if (active) {
        NSView *host = EHNearestCommonAncestor(first, second);
        if (!host) return;
        objc_setAssociatedObject(self, &hostKey, host, OBJC_ASSOCIATION_ASSIGN);
        [host addConstraint:self];
    } else {
        NSView *host = objc_getAssociatedObject(self, &hostKey);
        if (!host) host = EHNearestCommonAncestor(first, second);
        [host removeConstraint:self];
        objc_setAssociatedObject(self, &hostKey, nil, OBJC_ASSOCIATION_ASSIGN);
    }
}

- (BOOL)isActive {
    static const char hostKey;
    return objc_getAssociatedObject(self, &hostKey) != nil;
}

+ (void)activateConstraints:(NSArray *)constraints {
    for (NSLayoutConstraint *c in constraints) [c setActive:YES];
}

+ (void)deactivateConstraints:(NSArray *)constraints {
    for (NSLayoutConstraint *c in constraints) [c setActive:NO];
}

@end

// ---- NSStackView: hidden arranged subviews are excluded from layout ----
//
// From 10.11 onward a stack view ignores an arranged subview whose isHidden is
// YES: the view takes no space and the others divide it. 10.9 lays hidden views
// out as if they were visible, so an app that shows one of several stacked
// pages by hiding the rest gets the reverse of what it asked for -- the hidden
// page receives the full width and the visible ones collapse:
//
//   ZMTabView            0x660          visible
//   NSView (ZuiNSView)   0x660          visible
//   NSView (ZuiNSView)   1224x660       HIDDEN
//
// which is a window that renders blank while its contents exist and measure
// correctly.
//
// 10.9 already has the mechanism -- visibility priority -- so the fix is to
// keep it in step with isHidden. NSStackViewVisibilityPriorityNotVisible (0)
// detaches a view from the layout; MustHold (1000) keeps it.
//
// Priority is declared float, not CGFloat; passing a double would put the
// value in the wrong half of the register.
@interface NSStackView (EHHiddenArrangedSubviews)
- (void)setVisibilityPriority:(float)priority forView:(NSView *)view;
- (float)visibilityPriorityForView:(NSView *)view;
@end

#define EH_STACK_PRIORITY_NOT_VISIBLE 0.0f
#define EH_STACK_PRIORITY_MUST_HOLD   1000.0f

static void (*eh_original_stack_layout)(id, SEL);

static void EHStackViewLayout(NSStackView *self, SEL _cmd) {
    // Changing a priority marks the stack as needing layout again, so the
    // update is done once per pass and re-entry is skipped.
    static const char guardKey;
    if (!objc_getAssociatedObject(self, &guardKey)) {
        objc_setAssociatedObject(self, &guardKey, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        for (NSView *view in [self views]) {
            float wanted = [view isHidden] ? EH_STACK_PRIORITY_NOT_VISIBLE
                                           : EH_STACK_PRIORITY_MUST_HOLD;
            if ([self visibilityPriorityForView:view] != wanted)
                [self setVisibilityPriority:wanted forView:view];
        }
        objc_setAssociatedObject(self, &guardKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    eh_original_stack_layout(self, _cmd);
}

__attribute__((constructor))
static void EHInstallStackViewHiddenHandling(void) {
    Class cls = [NSStackView class];
    SEL sel = sel_registerName("layout");
    Method existing = class_getInstanceMethod(cls, sel);
    if (!existing) return;
    IMP previous = class_replaceMethod(cls, sel, (IMP)EHStackViewLayout,
                                       method_getTypeEncoding(existing));
    if (!previous) previous = method_getImplementation(existing);
    eh_original_stack_layout = (void (*)(id, SEL))previous;
}

// -[NSGraphicsContext CGContext], added in 10.10.
//
// 10.9 exposes the same object through -graphicsPort, typed void *. Code
// written against the newer name gets nothing back here, and then draws into a
// null context, which CoreGraphics reports once per call:
//
//   <Error>: CGContextDrawLinearGradient: invalid context 0x0. This is a
//   serious error...
//
// Nothing crashes -- the drawing simply does not happen, so vector artwork and
// gradients come out blank. The two are the same context; only the accessor is
// new.
@implementation NSGraphicsContext (EHCGContext)
- (CGContextRef)CGContext {
    return (CGContextRef)[self graphicsPort];
}
@end

// ---- NSViewController lifecycle and containment (10.10) ----
//
// 10.9's NSViewController loads a view and stops there. Everything added in
// 10.10 is missing: viewDidLoad, the appear/disappear callbacks, isViewLoaded,
// and child-view-controller containment.
//
// Modern code puts essentially all of a controller's setup in -viewDidLoad. On
// 10.9 that method is simply never called, so every such controller ends up
// with a loaded but empty view -- and because the view exists and has a sane
// class, nothing reports an error. A window built this way comes up blank while
// its view hierarchy looks structurally correct.
//
// So: call the lifecycle at the points AppKit would.
//
//   viewDidLoad     once, immediately after the view is first loaded. The flag
//                   is set before the call, because -viewDidLoad implementations
//                   reach for self.view and would otherwise recurse forever.
//   appear/disappear driven by the view actually entering or leaving a window,
//                   which is what those callbacks mean.
//
// Containment is stored on the controllers themselves; 10.9 has nowhere to put
// it. The transition method is the degenerate version -- swap the views, then
// run the completion -- with no animation, which no caller depends on for
// correctness.

static void *const kEHViewLoaded = (void *)&kEHViewLoaded;
static void *const kEHOwningController = (void *)&kEHOwningController;
static void *const kEHAppeared = (void *)&kEHAppeared;
static void *const kEHChildren = (void *)&kEHChildren;
static void *const kEHParent = (void *)&kEHParent;

@interface NSViewController (EHLifecycle)
- (BOOL)isViewLoaded;
- (void)addChildViewController:(NSViewController *)child;
- (void)removeFromParentViewController;
- (NSArray *)childViewControllers;
- (NSViewController *)parentViewController;
@end

// The view outlives its controller often enough that an unsafe-unretained
// association here is a use-after-free waiting to happen: the view is still in
// a hierarchy, gets moved between windows, and the callback messages a freed
// controller. A zeroing weak reference reads as nil instead.
@interface EHWeakBox : NSObject
@property (nonatomic, weak) id target;
@end
@implementation EHWeakBox
@end

static NSView *(*eh_original_vc_view)(id, SEL);

static NSView *EHViewControllerView(NSViewController *self, SEL _cmd) {
    BOOL alreadyLoaded = objc_getAssociatedObject(self, kEHViewLoaded) != nil;
    NSView *view = eh_original_vc_view(self, _cmd);
    if (alreadyLoaded || !view) return view;

    // Mark loaded first: -viewDidLoad almost always touches self.view.
    objc_setAssociatedObject(self, kEHViewLoaded, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    // Remember the owner so the view can drive appear/disappear.
    EHWeakBox *box = [[EHWeakBox alloc] init];
    box.target = self;
    objc_setAssociatedObject(view, kEHOwningController, box, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    if ([self respondsToSelector:@selector(viewDidLoad)])
        ((void (*)(id, SEL))objc_msgSend)(self, @selector(viewDidLoad));
    return view;
}

static void (*eh_original_did_move_to_window)(id, SEL);

static void EHViewDidMoveToWindow(NSView *self, SEL _cmd) {
    eh_original_did_move_to_window(self, _cmd);
    EHWeakBox *box = objc_getAssociatedObject(self, kEHOwningController);
    id controller = box.target;
    if (!controller) return;

    BOOL appeared = objc_getAssociatedObject(controller, kEHAppeared) != nil;
    if ([self window] && !appeared) {
        objc_setAssociatedObject(controller, kEHAppeared, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if ([controller respondsToSelector:@selector(viewWillAppear)])
            ((void (*)(id, SEL))objc_msgSend)(controller, @selector(viewWillAppear));
        if ([controller respondsToSelector:@selector(viewDidAppear)])
            ((void (*)(id, SEL))objc_msgSend)(controller, @selector(viewDidAppear));
    } else if (![self window] && appeared) {
        objc_setAssociatedObject(controller, kEHAppeared, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if ([controller respondsToSelector:@selector(viewWillDisappear)])
            ((void (*)(id, SEL))objc_msgSend)(controller, @selector(viewWillDisappear));
        if ([controller respondsToSelector:@selector(viewDidDisappear)])
            ((void (*)(id, SEL))objc_msgSend)(controller, @selector(viewDidDisappear));
    }
}

@implementation NSViewController (EHLifecycle)

- (BOOL)isViewLoaded {
    return objc_getAssociatedObject(self, kEHViewLoaded) != nil;
}

- (NSArray *)childViewControllers {
    return objc_getAssociatedObject(self, kEHChildren) ?: @[];
}

- (NSViewController *)parentViewController {
    EHWeakBox *box = objc_getAssociatedObject(self, kEHParent);
    return box.target;
}

- (void)addChildViewController:(NSViewController *)child {
    if (!child) return;
    NSMutableArray *children = objc_getAssociatedObject(self, kEHChildren);
    if (!children) {
        children = [NSMutableArray array];
        objc_setAssociatedObject(self, kEHChildren, children, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    if (![children containsObject:child]) [children addObject:child];
    EHWeakBox *parentBox = [[EHWeakBox alloc] init];
    parentBox.target = self;
    objc_setAssociatedObject(child, kEHParent, parentBox, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)removeFromParentViewController {
    EHWeakBox *box = objc_getAssociatedObject(self, kEHParent);
    NSViewController *parent = box.target;
    if (!parent) return;
    NSMutableArray *children = objc_getAssociatedObject(parent, kEHChildren);
    [children removeObject:self];
    objc_setAssociatedObject(self, kEHParent, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)transitionFromViewController:(NSViewController *)from
                    toViewController:(NSViewController *)to
                             options:(NSUInteger)options
                   completionHandler:(void (^)(void))completion {
    (void)options;
    NSView *fromView = from ? [from view] : nil;
    NSView *toView = to ? [to view] : nil;
    if (fromView && toView) {
        NSView *container = [fromView superview];
        if (container) {
            [toView setFrame:[fromView frame]];
            [toView setAutoresizingMask:[fromView autoresizingMask]];
            [container addSubview:toView];
        }
        [fromView removeFromSuperview];
    }
    if (completion) completion();
}

@end

__attribute__((constructor))
static void EHInstallViewControllerLifecycle(void) {
    Method view = class_getInstanceMethod([NSViewController class], @selector(view));
    if (view) {
        eh_original_vc_view = (NSView *(*)(id, SEL))method_getImplementation(view);
        method_setImplementation(view, (IMP)EHViewControllerView);
    }
    Method moved = class_getInstanceMethod([NSView class], @selector(viewDidMoveToWindow));
    if (moved) {
        eh_original_did_move_to_window = (void (*)(id, SEL))method_getImplementation(moved);
        method_setImplementation(moved, (IMP)EHViewDidMoveToWindow);
    }
}

// -[NSTabViewItem setViewController:] / -viewController, added in 10.10.
//
// 10.9's NSTabViewItem holds a view, not a view controller. Code that hands it
// a controller instead finds the selector missing, the item keeps no view at
// all, and the tab displays nothing -- with no error, because the item itself
// is perfectly valid.
//
// Assigning the controller's view is what the real implementation does, and it
// also drives the controller's own lifecycle: reading vc.view loads the view,
// which is where -viewDidLoad gets called from.
static void *const kEHTabItemController = (void *)&kEHTabItemController;

@implementation NSTabViewItem (EHViewController)

- (void)setViewController:(NSViewController *)controller {
    objc_setAssociatedObject(self, kEHTabItemController, controller,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (!controller) { [self setView:nil]; return; }
    [self setView:[controller view]];
    // The real one mirrors the controller's title onto the item unless the item
    // already carries its own label.
    NSString *title = [controller title];
    if (title.length && ![[self label] length]) [self setLabel:title];
}

- (NSViewController *)viewController {
    return objc_getAssociatedObject(self, kEHTabItemController);
}

@end

// -[NSTextField maximumNumberOfLines] and
// -setAllowsDefaultTighteningForTruncation:, both added in 10.11.
//
// Without the line limit a label that was meant to be one line wraps freely.
// Combined with a narrow preferredMaxLayoutWidth the measured height runs to
// thousands of points, the label pushes everything around it out of the way,
// and the layout silently comes apart -- labels 5000pt tall were how this
// showed up here.
//
// 10.9 expresses the same intent through usesSingleLineMode and the cell's
// wrapping and line-break mode, so one line maps exactly. A limit above one
// cannot be enforced precisely, and wrapping normally is the closest honest
// behaviour; what matters is that the label stops growing without bound.
static void *const kEHMaximumNumberOfLines = (void *)&kEHMaximumNumberOfLines;

@implementation NSTextField (EHLineLimit)

- (void)setMaximumNumberOfLines:(NSInteger)maximumNumberOfLines {
    objc_setAssociatedObject(self, kEHMaximumNumberOfLines,
                             @(maximumNumberOfLines), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    // usesSingleLineMode is a cell property in this SDK, not a control one.
    NSCell *cell = [self cell];
    BOOL single = (maximumNumberOfLines == 1);
    if ([cell respondsToSelector:@selector(setUsesSingleLineMode:)])
        [(id)cell setUsesSingleLineMode:single];
    [cell setWraps:!single];
    [cell setLineBreakMode:single ? NSLineBreakByTruncatingTail
                                  : NSLineBreakByWordWrapping];
}

- (NSInteger)maximumNumberOfLines {
    NSNumber *stored = objc_getAssociatedObject(self, kEHMaximumNumberOfLines);
    return stored ? [stored integerValue] : 0;
}

// Purely typographic: squeeze inter-character spacing slightly before giving up
// and truncating. 10.9 always truncates, which is the same text with slightly
// different spacing -- nothing downstream depends on it.
- (void)setAllowsDefaultTighteningForTruncation:(BOOL)allows { (void)allows; }
- (BOOL)allowsDefaultTighteningForTruncation { return NO; }

@end

// Let text views be weakly referenced.
//
// 10.9 refuses a weak reference to an instance whose class overrides retain or
// release, and NSTextView overrides -release. That makes every NSTextView
// subclass -- including an app's own label classes -- ineligible, and AppKit
// itself then trips over the restriction: NSStackViewContainer keeps a
// view-to-spacer NSMapTable with weak keys, so adding such a label to a stack
// view aborts the process:
//
//   -[NSStackViewContainer afterSpacerForView:] -> objc_storeWeak
//   Cannot form weak reference to instance of class ZMTextLabel
//
// objc asks the class this question and takes its word for it, so answering
// yes is enough. It is also true: the restriction is about objc not being able
// to see a custom refcount, but NSTextView's -release still runs through the
// ordinary machinery, so the weak entry is registered and cleared at dealloc
// exactly as it is for any other object -- verified here by releasing a text
// view to zero and watching the weak slot go nil rather than dangle.
//
// -retainWeakReference is deliberately not provided. objc calls it from
// weak_read_no_lock while holding the SideTable lock, and every retain
// reachable from outside takes that same lock, so any implementation would
// deadlock. Without it a weak *load* yields nil, which is a lesser fault than
// hanging, and the failing path here only ever stores.
@implementation NSTextView (EHWeakReferenceSupport)
- (BOOL)allowsWeakReference { return YES; }
@end

// ---- control convenience constructors (10.12) ----
//
// Callers build a control and lay it out in one expression:
//
//     [[NSTextField labelWithString:title] make].centerX.equalTo(self);
//
// Without these the class method is unimplemented, the expression starts from
// nil, and the first field access off the resulting nil object faults.
//
// Each returns an autoreleased, sized-to-fit control configured the way the
// documented constructor does.

@implementation NSTextField (EHConvenienceConstructors)

+ (instancetype)labelWithString:(NSString *)stringValue {
    NSTextField *label = [[self alloc] initWithFrame:NSZeroRect];
    [label setStringValue:stringValue ?: @""];
    [label setEditable:NO];
    [label setSelectable:NO];
    [label setBezeled:NO];
    [label setBordered:NO];
    [label setDrawsBackground:NO];
    [label setBackgroundColor:[NSColor clearColor]];
    [label setTextColor:[NSColor controlTextColor]];
    [label setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [[label cell] setUsesSingleLineMode:YES];
    [[label cell] setLineBreakMode:NSLineBreakByClipping];
    [label sizeToFit];
    return label;
}

+ (instancetype)wrappingLabelWithString:(NSString *)stringValue {
    NSTextField *label = [self labelWithString:stringValue];
    [label setSelectable:YES];
    [[label cell] setUsesSingleLineMode:NO];
    [[label cell] setLineBreakMode:NSLineBreakByWordWrapping];
    [label sizeToFit];
    return label;
}

+ (instancetype)labelWithAttributedString:(NSAttributedString *)attributedStringValue {
    NSTextField *label = [self labelWithString:@""];
    // The attributed string carries its own font, colour and wrapping, so the
    // single-line clipping a plain label sets up would override it.
    [[label cell] setUsesSingleLineMode:NO];
    [[label cell] setLineBreakMode:NSLineBreakByWordWrapping];
    if (attributedStringValue) [label setAttributedStringValue:attributedStringValue];
    [label sizeToFit];
    return label;
}

+ (instancetype)textFieldWithString:(NSString *)stringValue {
    NSTextField *field = [[self alloc] initWithFrame:NSZeroRect];
    [field setStringValue:stringValue ?: @""];
    [field setEditable:YES];
    [field setSelectable:YES];
    [field setBezeled:YES];
    [field setBezelStyle:NSTextFieldSquareBezel];
    [field setDrawsBackground:YES];
    [field setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
    [field sizeToFit];
    return field;
}

@end

@implementation NSButton (EHConvenienceConstructors)

+ (instancetype)buttonWithTitle:(NSString *)title target:(id)target action:(SEL)action {
    NSButton *button = [[self alloc] initWithFrame:NSZeroRect];
    [button setTitle:title ?: @""];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setButtonType:NSMomentaryPushInButton];
    [button setTarget:target];
    [button setAction:action];
    [button sizeToFit];
    return button;
}

+ (instancetype)buttonWithImage:(NSImage *)image target:(id)target action:(SEL)action {
    NSButton *button = [[self alloc] initWithFrame:NSZeroRect];
    [button setImage:image];
    [button setBezelStyle:NSRoundedBezelStyle];
    [button setButtonType:NSMomentaryPushInButton];
    [button setTarget:target];
    [button setAction:action];
    [button sizeToFit];
    return button;
}

+ (instancetype)buttonWithTitle:(NSString *)title
                          image:(NSImage *)image
                         target:(id)target
                         action:(SEL)action {
    NSButton *button = [self buttonWithTitle:title target:target action:action];
    [button setImage:image];
    [button sizeToFit];
    return button;
}

+ (instancetype)checkboxWithTitle:(NSString *)title target:(id)target action:(SEL)action {
    NSButton *button = [[self alloc] initWithFrame:NSZeroRect];
    [button setTitle:title ?: @""];
    [button setBezelStyle:NSRegularSquareBezelStyle];
    [button setButtonType:NSSwitchButton];
    [button setTarget:target];
    [button setAction:action];
    [button sizeToFit];
    return button;
}

+ (instancetype)radioButtonWithTitle:(NSString *)title target:(id)target action:(SEL)action {
    NSButton *button = [[self alloc] initWithFrame:NSZeroRect];
    [button setTitle:title ?: @""];
    [button setBezelStyle:NSRegularSquareBezelStyle];
    [button setButtonType:NSRadioButton];
    [button setTarget:target];
    [button setAction:action];
    [button sizeToFit];
    return button;
}

@end

@implementation NSImageView (EHConvenienceConstructors)

+ (instancetype)imageViewWithImage:(NSImage *)image {
    NSRect frame = NSZeroRect;
    if (image) frame.size = [image size];
    NSImageView *view = [[self alloc] initWithFrame:frame];
    [view setImage:image];
    [view setImageScaling:NSImageScaleProportionallyUpOrDown];
    return view;
}

@end

// ---- the application menu's settings item is named "Preferences" here ----
//
// Applications built against macOS 13 and later ship this item titled
// "Settings", which is the name that release introduced. On this OS the item
// is "Preferences", and that is what users look for and what the Cmd-, key
// equivalent is documented as opening.
//
// Only the application menu -- the first submenu of the main menu -- is
// considered, so an in-app menu that legitimately offers "Settings" keeps its
// own wording. The trailing ellipsis is preserved in whichever form the item
// already uses.

static void EHRenameSettingsItemInApplicationMenu(void) {
    NSMenu *mainMenu = [NSApp mainMenu];
    if ([mainMenu numberOfItems] == 0) return;
    NSMenu *appMenu = [[mainMenu itemAtIndex:0] submenu];
    for (NSMenuItem *item in [appMenu itemArray]) {
        NSString *title = [item title];
        if (![title hasPrefix:@"Settings"]) continue;
        NSString *suffix = [title substringFromIndex:[@"Settings" length]];
        // Anything other than a bare ellipsis is a different item that merely
        // starts with the same word.
        if ([suffix length] != 0 &&
            ![suffix isEqualToString:@"..."] &&
            ![suffix isEqualToString:@"…"]) continue;
        [item setTitle:[@"Preferences" stringByAppendingString:suffix]];
    }
}

@interface EHPreferencesMenuRenamer : NSObject
@end

@implementation EHPreferencesMenuRenamer
- (void)renameNow:(NSNotification *)note {
    (void)note;
    EHRenameSettingsItemInApplicationMenu();
}
@end

__attribute__((constructor))
static void EHInstallPreferencesMenuRename(void) {
    static EHPreferencesMenuRenamer *renamer;
    renamer = [[EHPreferencesMenuRenamer alloc] init];
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:renamer
               selector:@selector(renameNow:)
                   name:NSApplicationDidFinishLaunchingNotification
                 object:nil];
    // Applications that build their menus after launch, or rebuild them when
    // signing in, would otherwise keep the original title until relaunch.
    // Tracking begins before the menu is drawn, so the title is right by the
    // time it is on screen however late the menu was assembled.
    [center addObserver:renamer
               selector:@selector(renameNow:)
                   name:NSMenuDidBeginTrackingNotification
                 object:nil];
    [center addObserver:renamer
               selector:@selector(renameNow:)
                   name:NSApplicationDidBecomeActiveNotification
                 object:nil];
}

// ---- NSTextField text layout and placeholder on the field (10.10) ----
//
// These moved up from the cell to the control. Code written against the newer
// SDK sets them on the field, where they go unimplemented and the field keeps
// wrapping, or shows no placeholder at all.

@implementation NSTextField (EHCellForwardedProperties)

- (void)setLineBreakMode:(NSLineBreakMode)mode { [[self cell] setLineBreakMode:mode]; }
- (NSLineBreakMode)lineBreakMode { return [[self cell] lineBreakMode]; }

- (void)setUsesSingleLineMode:(BOOL)flag { [[self cell] setUsesSingleLineMode:flag]; }
- (BOOL)usesSingleLineMode { return [[self cell] usesSingleLineMode]; }

- (void)setPlaceholderString:(NSString *)placeholder {
    [[self cell] setPlaceholderString:placeholder];
}
- (NSString *)placeholderString { return [[self cell] placeholderString]; }

- (void)setPlaceholderAttributedString:(NSAttributedString *)placeholder {
    [[self cell] setPlaceholderAttributedString:placeholder];
}
- (NSAttributedString *)placeholderAttributedString {
    return [[self cell] placeholderAttributedString];
}

- (void)setAllowsDefaultTighteningForTruncation:(BOOL)flag { (void)flag; }
- (BOOL)allowsDefaultTighteningForTruncation { return NO; }

@end

// ---- NSScrollView content and scroller insets (10.10) ----
//
// Layout code offsets scrollable content by insetting it rather than by
// padding the document view. Unimplemented, the content sits under the header
// or the scrollers instead of clear of them.

static const void *kEHContentInsets = &kEHContentInsets;
static const void *kEHScrollerInsets = &kEHScrollerInsets;
static const void *kEHAutoAdjustsInsets = &kEHAutoAdjustsInsets;

@implementation NSScrollView (EHContentInsets)

- (void)setContentInsets:(NSEdgeInsets)insets {
    objc_setAssociatedObject(self, kEHContentInsets,
                             [NSValue valueWithBytes:&insets objCType:@encode(NSEdgeInsets)],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    // The clip view is what actually positions the document, so insetting its
    // frame within the scroll view reproduces the effect the property has.
    NSClipView *clip = [self contentView];
    if (!clip) return;
    NSRect bounds = [self bounds];
    NSRect frame = NSMakeRect(bounds.origin.x + insets.left,
                              bounds.origin.y + insets.bottom,
                              bounds.size.width - insets.left - insets.right,
                              bounds.size.height - insets.top - insets.bottom);
    if (frame.size.width < 0) frame.size.width = 0;
    if (frame.size.height < 0) frame.size.height = 0;
    [clip setFrame:frame];
}

- (NSEdgeInsets)contentInsets {
    NSEdgeInsets insets = NSEdgeInsetsMake(0, 0, 0, 0);
    NSValue *stored = objc_getAssociatedObject(self, kEHContentInsets);
    if (stored) [stored getValue:&insets];
    return insets;
}

// The scrollers overlay the content on this OS rather than reserving space,
// so the inset is recorded for callers that read it back and changes nothing.
- (void)setScrollerInsets:(NSEdgeInsets)insets {
    objc_setAssociatedObject(self, kEHScrollerInsets,
                             [NSValue valueWithBytes:&insets objCType:@encode(NSEdgeInsets)],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (NSEdgeInsets)scrollerInsets {
    NSEdgeInsets insets = NSEdgeInsetsMake(0, 0, 0, 0);
    NSValue *stored = objc_getAssociatedObject(self, kEHScrollerInsets);
    if (stored) [stored getValue:&insets];
    return insets;
}

// Automatic adjustment tracks a window's full-size content area, which this OS
// does not have, so the insets a caller set explicitly are the only ones.
- (void)setAutomaticallyAdjustsContentInsets:(BOOL)flag {
    objc_setAssociatedObject(self, kEHAutoAdjustsInsets, @(flag),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (BOOL)automaticallyAdjustsContentInsets {
    return [objc_getAssociatedObject(self, kEHAutoAdjustsInsets) boolValue];
}

@end

// ---- NSViewController appearance callbacks (10.10) ----
//
// Subclasses override these and chain to super. With no implementation to
// chain to, every such call goes to forwarding.

@implementation NSViewController (EHAppearanceCallbacks)
- (void)viewDidLoad { }
- (void)viewWillAppear { }
- (void)viewDidAppear { }
- (void)viewWillDisappear { }
- (void)viewDidDisappear { }
- (void)viewWillLayout { }
- (void)viewDidLayout { }
@end

// ---- NSWindow titlebar presentation (10.10) ----

static const void *kEHTitleVisibility = &kEHTitleVisibility;
static const void *kEHTitlebarTransparent = &kEHTitlebarTransparent;

@implementation NSWindow (EHTitlebarPresentation)

// NSWindowTitleHidden blanks the title text; the titlebar itself stays, since
// hiding it entirely needs a full-size content view this OS does not offer.
- (void)setTitleVisibility:(NSInteger)visibility {
    objc_setAssociatedObject(self, kEHTitleVisibility, @(visibility),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (visibility != 0) {
        objc_setAssociatedObject(self, kEHTitleVisibility, @(visibility),
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [self setTitle:@""];
    }
}

- (NSInteger)titleVisibility {
    return [objc_getAssociatedObject(self, kEHTitleVisibility) integerValue];
}

- (void)setTitlebarAppearsTransparent:(BOOL)flag {
    objc_setAssociatedObject(self, kEHTitlebarTransparent, @(flag),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (BOOL)titlebarAppearsTransparent {
    return [objc_getAssociatedObject(self, kEHTitlebarTransparent) boolValue];
}

@end

// ---- NSStackView hidden-view detachment ----

@implementation NSStackView (EHDetachesHiddenViews)
// Hidden arranged views are already excluded from the layout here, which is
// what this property asks for when it is on. Callers set it either way and
// read it back rarely.
- (void)setDetachesHiddenViews:(BOOL)flag { (void)flag; }
- (BOOL)detachesHiddenViews { return YES; }
@end

// ---- NSTextContainer size (10.11) ----
//
// The designated initialiser and the property were both renamed from
// "containerSize" to "size". Text measurement written against the newer name
// gets nil back from the initialiser, and the measuring code falls back to
// whatever height it proposed -- so a label reports the full bounding height
// it was offered rather than the one line it needs.

@implementation NSTextContainer (EHModernSize)

- (instancetype)initWithSize:(NSSize)size {
    return [self initWithContainerSize:size];
}

- (void)setSize:(NSSize)size { [self setContainerSize:size]; }
- (NSSize)size { return [self containerSize]; }

@end

// ---- fitting a view to a proposed size ----
//
// Layout code measures a view by proposing a bound and asking what it would
// actually take, then chains to the base implementation for anything it does
// not measure itself:
//
//     override func sizeThatFits(_ size: NSSize) -> NSSize {
//         var fitted = super.sizeThatFits(size)   // <- nothing to chain to
//
// With no base implementation the call goes to forwarding and the caller keeps
// the height it proposed. Proposed heights are deliberately generous -- 5000
// points is a common one -- so every label in a stack claims 5000 points and
// the content ends up thousands of points tall and scrolled out of sight.

@implementation NSView (EHSizeThatFits)

// The proposed size is an upper bound, not an answer. A view with no
// constraints and no intrinsic size that reports the whole bound as its
// desired size takes all of it: put six such views in a row and the first one
// is full width and the rest are nothing. Its current size is the honest
// answer -- it is the size something already chose for it -- and the bound is
// used only when there is nothing else to go on.
- (NSSize)sizeThatFits:(NSSize)size {
    NSSize fitting = [self fittingSize];
    NSSize current = [self frame].size;
    if (fitting.width <= 0)
        fitting.width = current.width > 0 ? current.width : size.width;
    if (fitting.height <= 0)
        fitting.height = current.height > 0 ? current.height : size.height;
    return fitting;
}

@end

@implementation NSControl (EHSizeThatFits)

// A control's cell measures itself against a bound, which is what makes the
// answer depend on the proposed width: a wrapping label asked for 520 points
// reports the height its text needs at that width.
- (NSSize)sizeThatFits:(NSSize)size {
    NSCell *cell = [self cell];
    if (!cell) {
        NSSize fitting = [self fittingSize];
        if (fitting.width <= 0) fitting.width = size.width;
        if (fitting.height <= 0) fitting.height = size.height;
        return fitting;
    }
    NSSize width = size;
    if (width.width <= 0) width.width = CGFLOAT_MAX;
    if (width.height <= 0) width.height = CGFLOAT_MAX;
    return [cell cellSizeForBounds:NSMakeRect(0, 0, width.width, width.height)];
}

@end

// ---- a frame the layout engine has no opinion about is kept ----
//
// A view can have constraints for its size but none for its position, while
// its superview places it by setting a frame. The engine still assigns that
// view an origin -- it has nothing to go on, so it picks (0,0) -- and applies
// it after the superview's own layout has run, discarding the placement:
//
//   setFrame (205,0 24x24)   <- the superview places the view
//   setFrame   (0,0 24x24)   <- the engine overwrites it
//
// so a row's trailing icon lands on top of the label beside it.
//
// The frame the application asked for is remembered, and restored after
// layout only where the engine has collapsed a view it cannot place or size:
// an unpositioned view goes to the origin, an unsized one to nothing at all,
// and a row of text fields that gets both ends up as six zero-sized boxes
// piled on one point. A view whose frame any constraint determines is left
// alone.

static const void *kEHManualFrame = &kEHManualFrame;
static void (*eh_original_setFrame)(NSView *, SEL, NSRect);
static void (*eh_original_setFrameOrigin)(NSView *, SEL, NSPoint);
static void (*eh_original_setFrameSize)(NSView *, SEL, NSSize);
static void (*eh_original_layout)(NSView *, SEL);
static __thread BOOL eh_restoring_origin;

static void EHRememberManualFrame(NSView *view, NSRect frame) {
    if (eh_restoring_origin) return;
    if ([view translatesAutoresizingMaskIntoConstraints]) return;
    // A degenerate frame is what the engine produces when it has nothing to
    // go on, so remembering one would defeat the restore.
    BOOL placed = frame.origin.x != 0 || frame.origin.y != 0;
    BOOL sized = frame.size.width > 0 && frame.size.height > 0;
    if (!placed && !sized) return;
    objc_setAssociatedObject(view, kEHManualFrame,
                             [NSValue valueWithRect:frame],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void EHViewSetFrame(NSView *self, SEL _cmd, NSRect frame) {
    eh_original_setFrame(self, _cmd, frame);
    EHRememberManualFrame(self, frame);
}

static void EHViewSetFrameOrigin(NSView *self, SEL _cmd, NSPoint origin) {
    eh_original_setFrameOrigin(self, _cmd, origin);
    NSRect frame = [self frame];
    frame.origin = origin;
    EHRememberManualFrame(self, frame);
}

static void EHViewSetFrameSize(NSView *self, SEL _cmd, NSSize size) {
    eh_original_setFrameSize(self, _cmd, size);
    NSRect frame = [self frame];
    frame.size = size;
    EHRememberManualFrame(self, frame);
}

static void EHViewLayout(NSView *self, SEL _cmd) {
    eh_original_layout(self, _cmd);
    for (NSView *sub in [self subviews]) {
        if ([sub translatesAutoresizingMaskIntoConstraints]) continue;
        NSRect frame = [sub frame];
        BOOL collapsedToOrigin = (frame.origin.x == 0 && frame.origin.y == 0);
        BOOL collapsedToNothing = (frame.size.width <= 0 || frame.size.height <= 0);
        if (!collapsedToOrigin && !collapsedToNothing) continue;
        NSValue *stored = objc_getAssociatedObject(sub, kEHManualFrame);
        if (!stored) continue;
        NSRect wanted = [stored rectValue];
        // Only a view the engine cannot lay out is restored; anything a
        // constraint determines keeps what it was given.
        if (![sub hasAmbiguousLayout]) continue;
        NSRect restored = frame;
        if (collapsedToOrigin && (wanted.origin.x != 0 || wanted.origin.y != 0))
            restored.origin = wanted.origin;
        if (collapsedToNothing && wanted.size.width > 0 && wanted.size.height > 0)
            restored.size = wanted.size;
        if (NSEqualRects(restored, frame)) continue;
        eh_restoring_origin = YES;
        [sub setFrame:restored];
        eh_restoring_origin = NO;
    }
}

__attribute__((constructor))
static void EHInstallManualOriginPreservation(void) {
    Method m = class_getInstanceMethod([NSView class], @selector(setFrame:));
    if (m) {
        eh_original_setFrame = (void (*)(NSView *, SEL, NSRect))method_getImplementation(m);
        method_setImplementation(m, (IMP)EHViewSetFrame);
    }
    Method o = class_getInstanceMethod([NSView class], @selector(setFrameOrigin:));
    if (o) {
        eh_original_setFrameOrigin = (void (*)(NSView *, SEL, NSPoint))method_getImplementation(o);
        method_setImplementation(o, (IMP)EHViewSetFrameOrigin);
    }
    Method z = class_getInstanceMethod([NSView class], @selector(setFrameSize:));
    if (z) {
        eh_original_setFrameSize = (void (*)(NSView *, SEL, NSSize))method_getImplementation(z);
        method_setImplementation(z, (IMP)EHViewSetFrameSize);
    }
    Method l = class_getInstanceMethod([NSView class], @selector(layout));
    if (l) {
        eh_original_layout = (void (*)(NSView *, SEL))method_getImplementation(l);
        method_setImplementation(l, (IMP)EHViewLayout);
    }
}

// ---- NSVisualEffectView (10.10) ----
//
// There is no backdrop blur on this OS, so the effect cannot be reproduced.
// What matters for a ported application is that the view is opaque where the
// design expects a panel: a purely transparent stand-in leaves toolbars,
// popovers and sidebars showing whatever is behind the window.
//
// Each material is drawn as the flat colour it approximates, and a mask image
// clips the fill so rounded panels keep their shape.

enum {
    EHMaterialAppearanceBased = 0, EHMaterialLight = 1, EHMaterialDark = 2,
    EHMaterialTitlebar = 3, EHMaterialSelection = 4, EHMaterialMenu = 5,
    EHMaterialPopover = 6, EHMaterialSidebar = 7, EHMaterialHeaderView = 10,
    EHMaterialSheet = 11, EHMaterialWindowBackground = 12, EHMaterialHUDWindow = 13,
    EHMaterialFullScreenUI = 15, EHMaterialToolTip = 17, EHMaterialContentBackground = 18,
    EHMaterialUnderWindowBackground = 21, EHMaterialUnderPageBackground = 22
};

@interface NSVisualEffectView : NSView
@property NSInteger material;
@property NSInteger blendingMode;
@property NSInteger state;
@property(retain) NSImage *maskImage;
@property(getter=isEmphasized) BOOL emphasized;
@property NSInteger interiorBackgroundStyle;
@end

@implementation NSVisualEffectView

@synthesize material = _material;
@synthesize blendingMode = _blendingMode;
@synthesize state = _state;
@synthesize maskImage = _maskImage;
@synthesize emphasized = _emphasized;
@synthesize interiorBackgroundStyle = _interiorBackgroundStyle;

// Vibrancy blends text with what is behind it, which without the blur behind
// it renders as washed-out text on a flat panel.
- (BOOL)allowsVibrancy { return NO; }

- (NSColor *)eh_fillColor {
    switch (_material) {
        case EHMaterialDark:
        case EHMaterialHUDWindow:
            return [NSColor colorWithCalibratedWhite:0.15 alpha:0.92];
        case EHMaterialToolTip:
            return [NSColor colorWithCalibratedWhite:0.98 alpha:0.95];
        case EHMaterialTitlebar:
        case EHMaterialHeaderView:
            return [NSColor colorWithCalibratedWhite:0.91 alpha:0.98];
        case EHMaterialSelection:
            return [NSColor selectedControlColor];
        case EHMaterialMenu:
        case EHMaterialPopover:
        case EHMaterialSheet:
            return [NSColor colorWithCalibratedWhite:0.97 alpha:0.97];
        case EHMaterialSidebar:
        case EHMaterialUnderWindowBackground:
        case EHMaterialUnderPageBackground:
            return [NSColor colorWithCalibratedWhite:0.93 alpha:0.97];
        default:
            return [NSColor windowBackgroundColor];
    }
}

- (void)drawRect:(NSRect)dirtyRect {
    [[self eh_fillColor] set];
    NSRectFillUsingOperation(dirtyRect, NSCompositeSourceOver);
    if (_maskImage) {
        // Keeping the fill only where the mask is opaque gives the panel the
        // shape the mask describes.
        [_maskImage drawInRect:[self bounds]
                      fromRect:NSZeroRect
                     operation:NSCompositeDestinationIn
                      fraction:1.0];
    }
}

EH_STUB_FORWARDING

@end

// ---- image stretching (10.10) ----

static const void *kEHCapInsets = &kEHCapInsets;
static const void *kEHResizingMode = &kEHResizingMode;

@implementation NSImage (EHResizing)

// Nine-part stretching is applied by the drawing call on this OS rather than
// being carried on the image, so the values are recorded and read back.
- (void)setCapInsets:(NSEdgeInsets)insets {
    objc_setAssociatedObject(self, kEHCapInsets,
                             [NSValue valueWithBytes:&insets objCType:@encode(NSEdgeInsets)],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (NSEdgeInsets)capInsets {
    NSEdgeInsets insets = NSEdgeInsetsMake(0, 0, 0, 0);
    NSValue *stored = objc_getAssociatedObject(self, kEHCapInsets);
    if (stored) [stored getValue:&insets];
    return insets;
}

- (void)setResizingMode:(NSInteger)mode {
    objc_setAssociatedObject(self, kEHResizingMode, @(mode),
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (NSInteger)resizingMode {
    return [objc_getAssociatedObject(self, kEHResizingMode) integerValue];
}

@end

// ---- application-wide appearance (10.14) ----

static const void *kEHAppAppearance = &kEHAppAppearance;

@implementation NSApplication (EHAppearance)

- (void)setAppearance:(NSAppearance *)appearance {
    objc_setAssociatedObject(self, kEHAppAppearance, appearance,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (NSAppearance *)appearance {
    return objc_getAssociatedObject(self, kEHAppAppearance);
}

// This OS has one system appearance, so unless the application has chosen one
// the effective appearance is Aqua.
- (NSAppearance *)effectiveAppearance {
    NSAppearance *chosen = objc_getAssociatedObject(self, kEHAppAppearance);
    return chosen ?: [NSAppearance appearanceNamed:NSAppearanceNameAqua];
}

@end

@implementation NSAppearance (EHBestMatch)

// Only the appearances this OS actually has can match; a caller offering
// newer names alongside them gets the one it can have.
- (NSString *)bestMatchFromAppearancesWithNames:(NSArray *)names {
    for (NSString *candidate in names)
        if ([[NSAppearance appearanceNamed:candidate] isEqual:self]) return candidate;
    for (NSString *candidate in names)
        if ([candidate isEqualToString:NSAppearanceNameAqua]) return candidate;
    return [names count] ? [names objectAtIndex:0] : nil;
}

@end

// ---- accessibility display preferences (10.10) ----

@implementation NSWorkspace (EHAccessibilityDisplay)
// None of these preferences exist on this OS, so nothing is being asked for.
- (BOOL)accessibilityDisplayShouldReduceTransparency { return NO; }
- (BOOL)accessibilityDisplayShouldIncreaseContrast { return NO; }
- (BOOL)accessibilityDisplayShouldDifferentiateWithoutColor { return NO; }
- (BOOL)accessibilityDisplayShouldReduceMotion { return NO; }
- (BOOL)accessibilityDisplayShouldInvertColors { return NO; }
- (BOOL)isVoiceOverEnabled { return NO; }
@end

// ---- constraints do not outlive the hierarchy they describe ----
//
// A constraint lives on the nearest view enclosing both of its items. Moving a
// view elsewhere, or taking it out entirely, leaves any such constraint on an
// ancestor it no longer spans. Nothing complains at the time, because a view
// outside a window has no layout engine to validate against; the constraint
// sits there until the ancestor is added to a window, and then AppKit checks
// what it is holding and throws:
//
//   Unable to install constraint on view. Does the constraint reference
//   something from outside the subtree of the view? That's illegal.
//   constraint:<ZMConfToolbarView.leading == ZMMTBaseView.leading>
//
// which surfaces far from the move that caused it. Later releases drop these
// constraints as part of the move, so code that re-parents a view expects not
// to have to. Every ancestor is swept, not just the immediate superview, since
// the constraint may have been installed several levels up.

static void (*eh_original_addSubview)(NSView *, SEL, NSView *);
static void (*eh_original_addSubviewPositioned)(NSView *, SEL, NSView *, NSInteger, NSView *);
static void (*eh_original_removeFromSuperview)(NSView *, SEL);

static BOOL EHConstraintSpansSubtree(NSLayoutConstraint *constraint, NSView *root) {
    id first = [constraint firstItem], second = [constraint secondItem];
    if ([first isKindOfClass:[NSView class]] && [(NSView *)first isDescendantOf:root])
        return YES;
    if ([second isKindOfClass:[NSView class]] && [(NSView *)second isDescendantOf:root])
        return YES;
    return NO;
}

static void EHDropConstraintsForDepartingView(NSView *view) {
    for (NSView *ancestor = [view superview]; ancestor; ancestor = [ancestor superview]) {
        NSArray *held = [ancestor constraints];
        if ([held count] == 0) continue;
        NSMutableArray *departing = [NSMutableArray array];
        for (NSLayoutConstraint *constraint in held)
            if (EHConstraintSpansSubtree(constraint, view)) [departing addObject:constraint];
        if ([departing count]) [ancestor removeConstraints:departing];
    }
}

static void EHViewAddSubview(NSView *self, SEL _cmd, NSView *view) {
    if (view && [view superview] && [view superview] != self)
        EHDropConstraintsForDepartingView(view);
    eh_original_addSubview(self, _cmd, view);
}

static void EHViewAddSubviewPositioned(NSView *self, SEL _cmd, NSView *view,
                                       NSInteger place, NSView *otherView) {
    if (view && [view superview] && [view superview] != self)
        EHDropConstraintsForDepartingView(view);
    eh_original_addSubviewPositioned(self, _cmd, view, place, otherView);
}

static void EHViewRemoveFromSuperview(NSView *self, SEL _cmd) {
    EHDropConstraintsForDepartingView(self);
    eh_original_removeFromSuperview(self, _cmd);
}

__attribute__((constructor))
static void EHInstallConstraintHierarchyCleanup(void) {
    Method add = class_getInstanceMethod([NSView class], @selector(addSubview:));
    if (add) {
        eh_original_addSubview =
            (void (*)(NSView *, SEL, NSView *))method_getImplementation(add);
        method_setImplementation(add, (IMP)EHViewAddSubview);
    }
    Method positioned = class_getInstanceMethod([NSView class],
                            @selector(addSubview:positioned:relativeTo:));
    if (positioned) {
        eh_original_addSubviewPositioned =
            (void (*)(NSView *, SEL, NSView *, NSInteger, NSView *))
            method_getImplementation(positioned);
        method_setImplementation(positioned, (IMP)EHViewAddSubviewPositioned);
    }
    Method remove = class_getInstanceMethod([NSView class], @selector(removeFromSuperview));
    if (remove) {
        eh_original_removeFromSuperview =
            (void (*)(NSView *, SEL))method_getImplementation(remove);
        method_setImplementation(remove, (IMP)EHViewRemoveFromSuperview);
    }
}

// ---- an equal-fill stack keeps its shares as its contents change ----
//
// Arranged views arrive through several entry points, including bulk setters,
// so the equal-size constraints are rebuilt from -updateConstraints, which
// runs before every layout pass whatever route the views took. The rebuild is
// skipped unless the arranged set actually differs from the one the current
// constraints were built for.

static void (*eh_original_stackUpdateConstraints)(NSStackView *, SEL);

static void EHStackUpdateConstraints(NSStackView *self, SEL _cmd) {
    [self eh_applyDistribution];
    eh_original_stackUpdateConstraints(self, _cmd);
}

__attribute__((constructor))
static void EHInstallStackDistributionRefresh(void) {
    Method m = class_getInstanceMethod([NSStackView class], @selector(updateConstraints));
    if (!m) return;
    eh_original_stackUpdateConstraints =
        (void (*)(NSStackView *, SEL))method_getImplementation(m);
    method_setImplementation(m, (IMP)EHStackUpdateConstraints);
}

// ---- a text view initialised without a container still gets one ----
//
// -initWithFrame: builds the whole text network -- storage, layout manager and
// container. -initWithFrame:textContainer: does not: it takes the container as
// given, and if that container has no layout manager behind it the view ends
// up with no layout manager and no text storage either. Such a view draws,
// takes focus and reports itself editable, but it has no -inputContext, so
// every keystroke goes nowhere.
//
// Later releases complete the network in both of the cases handled here -- a
// nil container, and a bare container with nothing behind it -- so a subclass
// that hands up a container it just allocated, which is the ordinary way to
// write one, is silently inert on this OS. The missing pieces are built and
// the container attached before the original initialiser sees it.
//
// The network is also kept alive by the view. A text view only owns the
// network it built itself; one handed a container does not, and the storage
// and layout manager behind that container are typically locals in the
// caller's initialiser. When that initialiser returns they are released, the
// container goes with them, and the view is left holding nothing -- it reports
// a nil text container, nil storage and nil -inputContext a moment after being
// constructed with a complete network. Later releases keep the network for the
// view's lifetime, so it is retained here to match.

static const void *kEHOwnedTextStorage = &kEHOwnedTextStorage;
static const void *kEHOwnedLayoutManager = &kEHOwnedLayoutManager;
static const void *kEHOwnedTextContainer = &kEHOwnedTextContainer;
static id (*eh_original_textViewInitWithContainer)(NSTextView *, SEL, NSRect, NSTextContainer *);

static void EHRetainTextNetwork(NSTextView *view, NSTextStorage *fallbackStorage) {
    if (!view) return;
    NSTextContainer *container = [view textContainer];
    NSLayoutManager *manager = [container layoutManager];
    NSTextStorage *storage = [manager textStorage] ?: fallbackStorage;
    if (container) objc_setAssociatedObject(view, kEHOwnedTextContainer, container,
                                            OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (manager) objc_setAssociatedObject(view, kEHOwnedLayoutManager, manager,
                                          OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    if (storage) objc_setAssociatedObject(view, kEHOwnedTextStorage, storage,
                                          OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static id EHTextViewInitWithFrameTextContainer(NSTextView *self, SEL _cmd,
                                               NSRect frame,
                                               NSTextContainer *container) {
    // A container that already has a layout manager brings its own network,
    // which still has to be kept alive for the view.
    if (container && [container layoutManager]) {
        NSTextView *given =
            eh_original_textViewInitWithContainer(self, _cmd, frame, container);
        EHRetainTextNetwork(given, nil);
        return given;
    }

    NSTextStorage *storage = [[NSTextStorage alloc] init];
    NSLayoutManager *manager = [[NSLayoutManager alloc] init];
    [storage addLayoutManager:manager];

    NSTextContainer *complete = container;
    if (!complete) {
        complete = [[NSTextContainer alloc]
            initWithContainerSize:NSMakeSize(frame.size.width, CGFLOAT_MAX)];
        [complete setWidthTracksTextView:YES];
    }
    [manager addTextContainer:complete];

    NSTextView *view = eh_original_textViewInitWithContainer(self, _cmd, frame, complete);
    EHRetainTextNetwork(view, storage);
    return view;
}

__attribute__((constructor))
static void EHInstallTextViewNetwork(void) {
    Method m = class_getInstanceMethod([NSTextView class],
                                       @selector(initWithFrame:textContainer:));
    if (!m) return;
    eh_original_textViewInitWithContainer =
        (id (*)(NSTextView *, SEL, NSRect, NSTextContainer *))method_getImplementation(m);
    method_setImplementation(m, (IMP)EHTextViewInitWithFrameTextContainer);
}
