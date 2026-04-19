/* Comprehensive macOS 10.9 compatibility header for Godot */
#ifndef _GODOT_MACOS_COMPAT_H_
#define _GODOT_MACOS_COMPAT_H_

#include <AvailabilityMacros.h>

/* API_AVAILABLE macro may not work properly on 10.9 SDK */
#ifndef API_AVAILABLE
#define API_AVAILABLE(...)
#endif
#ifndef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#endif

/* CGScreenCapture (10.15+) - weak stubs (ObjC only to avoid polluting C++ namespace) */
#ifdef __OBJC__
#include <CoreGraphics/CoreGraphics.h>
#ifdef __cplusplus
extern "C" {
#endif
extern bool CGPreflightScreenCaptureAccess(void);
extern bool CGRequestScreenCaptureAccess(void);
#ifdef __cplusplus
}
#endif
#endif

/* NSOperatingSystemVersion (10.10+) */
typedef struct {
    long majorVersion;
    long minorVersion;
    long patchVersion;
} NSOperatingSystemVersion;

/* NSBitmapImageFileType renamed in 10.12 */
#ifndef NSBitmapImageFileTypePNG
#define NSBitmapImageFileTypePNG NSPNGFileType
#endif

/* NSAlertStyle renamed in 10.12 */
#ifndef NSAlertStyleWarning
#define NSAlertStyleWarning NSWarningAlertStyle
#define NSAlertStyleCritical NSCriticalAlertStyle
#define NSAlertStyleInformational NSInformationalAlertStyle
#endif

/* NSTextAlignment on 10.9 */
#ifndef NSTextAlignmentCenter
#define NSTextAlignmentCenter NSCenterTextAlignment
#define NSTextAlignmentLeft NSLeftTextAlignment
#define NSTextAlignmentRight NSRightTextAlignment
#endif

/* NSWindow styles renamed in 10.12 */
#ifndef NSWindowStyleMaskTitled
#define NSWindowStyleMaskTitled NSTitledWindowMask
#define NSWindowStyleMaskClosable NSClosableWindowMask
#define NSWindowStyleMaskMiniaturizable NSMiniaturizableWindowMask
#define NSWindowStyleMaskResizable NSResizableWindowMask
#define NSWindowStyleMaskFullScreen NSFullScreenWindowMask
#define NSWindowStyleMaskBorderless NSBorderlessWindowMask
#endif
#ifndef NSWindowStyleMaskFullSizeContentView
#define NSWindowStyleMaskFullSizeContentView (1 << 15)
#define NSWindowStyleMaskUtilityWindow NSUtilityWindowMask
#define NSWindowStyleMaskNonactivatingPanel NSNonactivatingPanelMask
#endif

/* NSEvent types renamed in 10.12 */
#ifndef NSEventTypeLeftMouseDown
#define NSEventTypeLeftMouseDown NSLeftMouseDown
#define NSEventTypeLeftMouseUp NSLeftMouseUp
#define NSEventTypeRightMouseDown NSRightMouseDown
#define NSEventTypeRightMouseUp NSRightMouseUp
#define NSEventTypeMouseMoved NSMouseMoved
#define NSEventTypeLeftMouseDragged NSLeftMouseDragged
#define NSEventTypeRightMouseDragged NSRightMouseDragged
#define NSEventTypeMouseEntered NSMouseEntered
#define NSEventTypeMouseExited NSMouseExited
#define NSEventTypeKeyDown NSKeyDown
#define NSEventTypeKeyUp NSKeyUp
#define NSEventTypeFlagsChanged NSFlagsChanged
#define NSEventTypeScrollWheel NSScrollWheel
#define NSEventTypeOtherMouseDown NSOtherMouseDown
#define NSEventTypeOtherMouseUp NSOtherMouseUp
#define NSEventTypeOtherMouseDragged NSOtherMouseDragged
#define NSEventModifierFlagShift NSShiftKeyMask
#define NSEventModifierFlagControl NSControlKeyMask
#define NSEventModifierFlagOption NSAlternateKeyMask
#define NSEventModifierFlagCommand NSCommandKeyMask
#define NSEventModifierFlagCapsLock NSAlphaShiftKeyMask
#define NSEventModifierFlagDeviceIndependentFlagsMask NSDeviceIndependentModifierFlagsMask
#define NSEventModifierFlagNumericPad NSNumericPadKeyMask
#define NSEventModifierFlagFunction NSFunctionKeyMask
#endif
#ifndef NSEventTypeSystemDefined
#define NSEventTypeSystemDefined NSSystemDefined
#define NSEventTypeApplicationDefined NSApplicationDefined
#endif
#ifndef NSEventMaskAny
#define NSEventMaskAny NSAnyEventMask
#endif
#ifndef NSEventMaskLeftMouseDown
#define NSEventMaskLeftMouseDown NSLeftMouseDownMask
#define NSEventMaskRightMouseDown NSRightMouseDownMask
#define NSEventMaskOtherMouseDown NSOtherMouseDownMask
#endif

/* NSEventModifierFlags type (10.12+) */
#ifdef __OBJC__
typedef NSUInteger NSEventModifierFlags;
#endif

/* NSEventSubtype (10.12+) */
#ifdef __OBJC__
#ifndef NSEventSubtypeTabletPoint
typedef NSInteger NSEventSubtype;
#define NSEventSubtypeTabletPoint NSTabletPointEventSubtype
#define NSEventSubtypeTabletProximity NSTabletProximityEventSubtype
#endif
#ifndef NSPointingDeviceTypeEraser
#define NSPointingDeviceTypeEraser NSEraserPointingDevice
#endif
#endif

/* NSWindowTitle visibility (10.10+) */
#ifndef NSWindowTitleHidden
#define NSWindowTitleHidden 1
#define NSWindowTitleVisible 0
#endif

/* NSWindowTabbingMode (10.12+) */
#ifndef NSWindowTabbingModeDisallowed
enum { NSWindowTabbingModeAutomatic = 0, NSWindowTabbingModePreferred = 1, NSWindowTabbingModeDisallowed = 2 };
#endif

/* NSControlStateValue (10.13+) */
#ifndef NSControlStateValueOn
#define NSControlStateValueOn NSOnState
#define NSControlStateValueOff NSOffState
#define NSControlStateValueMixed NSMixedState
#endif

/* kVK_RightCommand (Carbon) */
#ifndef kVK_RightCommand
#define kVK_RightCommand 0x36
#endif

/* NSOpenGLContextParameter renamed in 10.12 */
#ifndef NSOpenGLContextParameterSurfaceOpacity
#define NSOpenGLContextParameterSurfaceOpacity NSOpenGLCPSurfaceOpacity
#endif
#ifndef NSOpenGLContextParameterSwapInterval
#define NSOpenGLContextParameterSwapInterval NSOpenGLCPSwapInterval
#endif

/* NSPasteboardTypeFileURL (10.13+) */
#ifdef __OBJC__
#ifndef NSPasteboardTypeFileURL
#define NSPasteboardTypeFileURL NSFilenamesPboardType
#endif

/* NSWorkspace accessibility notification (10.10+) */
#ifndef NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
#define NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification @"NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification"
#endif

#endif /* __OBJC__ */

/* CVDisplayLinkSetOutputHandler (macOS 12+) */
#ifdef __OBJC__
#import <CoreVideo/CoreVideo.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef CVReturn (^CVDisplayLinkOutputHandler)(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp*, CVOptionFlags, CVOptionFlags*);
extern CVReturn CVDisplayLinkSetOutputHandler(CVDisplayLinkRef, CVDisplayLinkOutputHandler) __attribute__((weak_import));
#ifdef __cplusplus
}
#endif
#endif

/* ---- ObjC category declarations for missing methods ---- */
#ifdef __OBJC__
#import <AppKit/AppKit.h>

/* NSWorkspace accessibility (10.10+) */
@interface NSWorkspace (MavericksMissing)
- (BOOL)isVoiceOverEnabled;
- (BOOL)isSwitchControlEnabled;
- (BOOL)accessibilityDisplayShouldIncreaseContrast;
- (BOOL)accessibilityDisplayShouldReduceMotion;
- (BOOL)accessibilityDisplayShouldReduceTransparency;
- (BOOL)accessibilityDisplayShouldDifferentiateWithoutColor;
- (BOOL)accessibilityDisplayShouldInvertColors;
@end

/* CALayerDelegate protocol (formalized in 10.12) */
@protocol CALayerDelegate <NSObject>
@optional
- (void)displayLayer:(CALayer *)layer;
- (void)drawLayer:(CALayer *)layer inContext:(CGContextRef)ctx;
- (void)layoutSublayersOfLayer:(CALayer *)layer;
- (id<CAAction>)actionForLayer:(CALayer *)layer forKey:(NSString *)event;
@end

/* UTType stub (11.0+) */
@interface UTType : NSObject
@property (class, readonly) UTType *plainText;
@property (class, readonly) UTType *data;
@property (readonly, copy) NSString *identifier;
+ (UTType *)typeWithFilenameExtension:(NSString *)ext;
+ (UTType *)typeWithMIMEType:(NSString *)mime;
@end
#define UTTypeData ([UTType data])

/* NSWindow methods (10.10-10.12+) */
@interface NSWindow (MavericksCompat)
- (void)setTabbingMode:(NSInteger)mode;
- (NSInteger)windowTitlebarLayoutDirection;
- (void)performWindowDragWithEvent:(NSEvent *)event;
- (void)setTitlebarAppearsTransparent:(BOOL)flag;
- (void)setTitleVisibility:(NSInteger)visibility;
@property (readonly, strong) NSWindow *sheet;
@end

/* NSApplication.effectiveAppearance (10.14+) */
@interface NSApplication (MavericksCompat)
@property (readonly, strong) NSAppearance *effectiveAppearance;
@end

/* NSAppearance (11.0+) */
@interface NSAppearance (MavericksCompat)
- (void)performAsCurrentDrawingAppearance:(void (^)(void))block;
- (NSString *)bestMatchFromAppearancesWithNames:(NSArray *)names;
@end

/* NSColor (10.10-10.14+) */
@interface NSColor (MavericksCompat)
@property (class, readonly, strong) NSColor *controlAccentColor;
@property (class, readonly, strong) NSColor *secondaryLabelColor;
@end

/* NSStatusBarButton (10.10+) */
@interface NSStatusBarButton : NSButton
@property NSImageScaling imageScaling;
@end
@interface NSStatusItem (MavericksCompat)
@property (readonly, strong) NSStatusBarButton *button;
@end

/* NSTextField (10.12+) */
@interface NSTextField (MavericksCompat)
+ (NSTextField *)labelWithString:(NSString *)str;
@end

/* NSSavePanel (11.0+) */
@interface NSSavePanel (MavericksCompat)
- (void)setAllowedContentTypes:(NSArray *)types;
@end

/* NSMenu (10.11+) */
@interface NSMenu (MavericksCompat)
@property (nonatomic) NSInteger userInterfaceLayoutDirection;
@end


/* AVAuthorization (10.14+) — camera module disabled, stubs not needed */

#endif /* __OBJC__ */


/* GCControllerPlayerIndex (typedef added in 10.12, was NSInteger before) */
#ifdef __OBJC__
#ifndef GCControllerPlayerIndex
typedef NSInteger GCControllerPlayerIndex;
/* GCControllerPlayerIndexUnset is defined in system GCController.h enum */
#endif
#endif
/* AssertMacros.h 'check' macro conflicts with C++ identifiers.
   Must be AFTER all system includes since they may re-define it. */
#ifdef check
#undef check
#endif


/* QoS classes (10.10+) */
#ifndef QOS_CLASS_USER_INTERACTIVE
#define QOS_CLASS_USER_INTERACTIVE 0x21
#define QOS_CLASS_USER_INITIATED 0x19
#define QOS_CLASS_DEFAULT 0x15
#define QOS_CLASS_UTILITY 0x11
#define QOS_CLASS_BACKGROUND 0x09
#define QOS_CLASS_UNSPECIFIED 0x00
#endif

/* pthread_attr_set_qos_class_np (10.10+) */
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
int pthread_attr_set_qos_class_np(pthread_attr_t *attr, unsigned int qos_class, int relative_priority);
#ifdef __cplusplus
}
#endif


/* IOKit HID transport constants (added in later macOS versions) */
#ifndef kIOHIDTransportUSBValue
#define kIOHIDTransportUSBValue "USB"
#endif
#ifndef kIOHIDTransportBluetoothValue
#define kIOHIDTransportBluetoothValue "Bluetooth"
#endif
#ifndef kIOHIDTransportI2CValue
#define kIOHIDTransportI2CValue "I2C"
#endif
#ifndef kIOHIDTransportSPIValue
#define kIOHIDTransportSPIValue "SPI"
#endif


/* GCController.productCategory (10.15+) */
#ifdef __OBJC__
#import <GameController/GameController.h>
@interface GCController (MavericksCompat)
@property (nonatomic, readonly) NSString *productCategory;
@end

/* NSString.containsString: (10.10+) */
@interface NSString (MavericksCompat)
- (BOOL)containsString:(NSString *)str;
@end
#endif


/* Disable SDL MFi joystick on 10.9 (requires 10.15+ GameController APIs) */
#ifdef SDL_JOYSTICK_MFI
#undef SDL_JOYSTICK_MFI
#endif

#endif /* _GODOT_MACOS_COMPAT_H_ */
