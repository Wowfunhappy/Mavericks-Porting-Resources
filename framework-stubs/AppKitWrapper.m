// Wrapper for AppKit on OS X 10.9: supplies the symbols this OS's copy
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
    - (void)forwardInvocation:(NSInvocation *)invocation { }

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
STUB_CLASS(NSLayoutGuide)
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

// NSVisualEffectView must be a real view: it is put in a view hierarchy and
// laid out. Without the blur it is an ordinary transparent view, which draws
// nothing but occupies the right rectangle.
STUB_SUBCLASS(NSVisualEffectView, NSView)

// Data constants. Where the real value is documented it is reproduced
// exactly, because callers compare and serialise these.
NSString * const NSAppearanceNameDarkAqua = @"NSAppearanceNameDarkAqua";
NSString * const NSAppearanceNameVibrantDark = @"NSAppearanceNameVibrantDark";
NSString * const NSAppearanceNameVibrantLight = @"NSAppearanceNameVibrantLight";
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