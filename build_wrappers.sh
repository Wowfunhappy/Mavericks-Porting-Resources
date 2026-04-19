#!/bin/bash
set -e

APP="Ex-Zodiac.app/Contents/MacOS"
LEGACY_STATIC="/Users/Jonathan/Developer/MacPorts-Legacy-Support/lib/libMacportsLegacySupport.a"

echo "=== Building framework wrappers ==="

# ── CoreGraphics wrapper ──
cat > /tmp/cg_stubs.c << 'CEOF'
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

void *CGDirectDisplayCopyCurrentMetalDevice(CGDirectDisplayID display) {
	(void)display;
	return NULL;
}

CFStringRef CGColorSpaceGetName(CGColorSpaceRef space) {
	if (!space) return NULL;
	CGColorSpaceModel model = CGColorSpaceGetModel(space);
	switch (model) {
		case kCGColorSpaceModelRGB: return CFSTR("kCGColorSpaceSRGB");
		case kCGColorSpaceModelCMYK: return CFSTR("kCGColorSpaceGenericCMYK");
		case kCGColorSpaceModelMonochrome: return CFSTR("kCGColorSpaceGenericGrayGamma2_2");
		default: return CFSTR("kCGColorSpaceSRGB");
	}
}

const CFStringRef kCGColorSpaceDCIP3 = (CFStringRef)__builtin_constant_p("kCGColorSpaceDCIP3") ? CFSTR("kCGColorSpaceDCIP3") : CFSTR("kCGColorSpaceDCIP3");
const CFStringRef kCGColorSpaceDisplayP3 = (CFStringRef)__builtin_constant_p("kCGColorSpaceDisplayP3") ? CFSTR("kCGColorSpaceDisplayP3") : CFSTR("kCGColorSpaceDisplayP3");
const CFStringRef kCGColorSpaceExtendedLinearSRGB = (CFStringRef)__builtin_constant_p("x") ? CFSTR("kCGColorSpaceExtendedLinearSRGB") : CFSTR("kCGColorSpaceExtendedLinearSRGB");
const CFStringRef kCGColorSpaceExtendedSRGB = (CFStringRef)__builtin_constant_p("x") ? CFSTR("kCGColorSpaceExtendedSRGB") : CFSTR("kCGColorSpaceExtendedSRGB");
const CFStringRef kCGColorSpaceITUR_709 = (CFStringRef)__builtin_constant_p("x") ? CFSTR("kCGColorSpaceITUR_709") : CFSTR("kCGColorSpaceITUR_709");
CEOF

clang -dynamiclib -o "$APP/libCoreGraphicsWrapper.dylib" \
  /tmp/cg_stubs.c \
  -Wl,-reexport_library,/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics \
  -install_name "@loader_path/libCoreGraphicsWrapper.dylib" \
  -compatibility_version 64.0.0 -current_version 1957.0.0 \
  -framework CoreFoundation \
  -Wno-deprecated-declarations 2>&1
echo "  CoreGraphics wrapper: OK"

# ── QuartzCore wrapper ──
cat > /tmp/qc_stubs.m << 'MEOF'
#import <QuartzCore/QuartzCore.h>
@interface CAMetalLayer : CALayer
@property (nonatomic) BOOL presentsWithTransaction;
@property (nonatomic) BOOL framebufferOnly;
@property (nonatomic) BOOL wantsExtendedDynamicRangeContent;
@property (nonatomic, retain) id device;
@property (nonatomic) unsigned long long pixelFormat;
@property (nonatomic) CGSize drawableSize;
@property (nonatomic) int displaySyncEnabled;
@property (nonatomic) int maximumDrawableCount;
@end
@implementation CAMetalLayer
@synthesize presentsWithTransaction;
@synthesize framebufferOnly;
@synthesize wantsExtendedDynamicRangeContent;
@synthesize device;
@synthesize pixelFormat;
@synthesize drawableSize;
@synthesize displaySyncEnabled;
@synthesize maximumDrawableCount;
- (id)nextDrawable { return nil; }
- (BOOL)isOpaque { return NO; }
- (id)initWithLayer:(id)layer {
    self = [super initWithLayer:layer];
    if (self) {
        self.opaque = NO;
        self.backgroundColor = CGColorGetConstantColor(kCGColorClear);
    }
    return self;
}
- (id)init {
    self = [super init];
    if (self) {
        self.opaque = NO;
        self.backgroundColor = CGColorGetConstantColor(kCGColorClear);
    }
    return self;
}
@end
MEOF

clang -dynamiclib -o "$APP/libQuartzCoreWrapper.dylib" \
  /tmp/qc_stubs.m \
  -Wl,-reexport_library,/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore \
  -install_name "@loader_path/libQuartzCoreWrapper.dylib" \
  -compatibility_version 1.2.0 -current_version 1193.18.35 \
  -framework QuartzCore -framework Foundation \
  -Wno-deprecated-declarations 2>&1
echo "  QuartzCore wrapper: OK"

# ── AppKit wrapper ──
cat > /tmp/appkit_stubs.m << 'MEOF'
#import <Cocoa/Cocoa.h>

/* NSString constants */
NSString *NSAccessibilityTabButtonSubrole_stub __asm("_NSAccessibilityTabButtonSubrole") = @"AXTabButton";
NSString *NSPasteboardTypeFileURL_stub __asm("_NSPasteboardTypeFileURL") = @"public.file-url";
NSString *NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification_stub
    __asm("_NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification")
    = @"NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification";

/* NSWorkspace accessibility methods */
@interface NSWorkspace (LegacyStubs)
@end
@implementation NSWorkspace (LegacyStubs)
- (BOOL)accessibilityDisplayShouldIncreaseContrast { return NO; }
- (BOOL)accessibilityDisplayShouldDifferentiateWithoutColor { return NO; }
- (BOOL)accessibilityDisplayShouldReduceTransparency { return NO; }
- (BOOL)accessibilityDisplayShouldReduceMotion { return NO; }
- (BOOL)accessibilityDisplayShouldInvertColors { return NO; }
- (BOOL)isVoiceOverEnabled { return NO; }
- (BOOL)isSwitchControlEnabled { return NO; }
@end

/* NSAppearance stub for dark mode checks */
@interface NSAppearance (LegacyStub)
@end
@implementation NSAppearance (LegacyStub)
- (NSString *)bestMatchFromAppearancesWithNames:(NSArray *)names {
    return names.count > 0 ? names[0] : @"NSAppearanceNameAqua";
}
@end

/* NSGridView stub */
@interface NSGridView : NSView
@end
@implementation NSGridView
@end
MEOF

clang -dynamiclib -o "$APP/libAppKitWrapper.dylib" \
  /tmp/appkit_stubs.m \
  -Wl,-reexport_library,/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit \
  -install_name "@loader_path/libAppKitWrapper.dylib" \
  -compatibility_version 45.0.0 -current_version 2685.10.108 \
  -framework Cocoa \
  -Wno-deprecated-declarations 2>&1
echo "  AppKit wrapper: OK"

# ── CoreVideo wrapper ──
cat > /tmp/cv_stubs.c << 'CEOF'
#include <CoreVideo/CoreVideo.h>
#include <Block.h>

typedef CVReturn (^CVDisplayLinkOutputHandler)(
    CVDisplayLinkRef displayLink,
    const CVTimeStamp *inNow,
    const CVTimeStamp *inOutputTime,
    CVOptionFlags flagsIn,
    CVOptionFlags *flagsOut
);

static CVReturn _cvdl_block_callback(
    CVDisplayLinkRef displayLink,
    const CVTimeStamp *inNow,
    const CVTimeStamp *inOutputTime,
    CVOptionFlags flagsIn,
    CVOptionFlags *flagsOut,
    void *context)
{
    CVDisplayLinkOutputHandler handler = (CVDisplayLinkOutputHandler)context;
    return handler(displayLink, inNow, inOutputTime, flagsIn, flagsOut);
}

CVReturn CVDisplayLinkSetOutputHandler(
    CVDisplayLinkRef displayLink,
    CVDisplayLinkOutputHandler handler)
{
    handler = Block_copy(handler);
    return CVDisplayLinkSetOutputCallback(displayLink, _cvdl_block_callback, handler);
}
CEOF

clang -dynamiclib -o "$APP/libCoreVideoWrapper.dylib" \
  /tmp/cv_stubs.c \
  -Wl,-reexport_library,/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo \
  -install_name "@loader_path/libCoreVideoWrapper.dylib" \
  -compatibility_version 1.2.0 -current_version 706.46.5 \
  -framework CoreVideo \
  -Wno-deprecated-declarations 2>&1
echo "  CoreVideo wrapper: OK"

# ── CoreFoundation wrapper ──
cat > /tmp/cf_stubs.c << 'CEOF'
#include <CoreFoundation/CoreFoundation.h>

/* ___NSDictionary0__ singleton empty dict */
static CFDictionaryRef _empty_dict = NULL;
void *__NSDictionary0_val __asm("___NSDictionary0__") = NULL;

__attribute__((constructor))
static void _init_cf_stubs(void) {
    _empty_dict = CFDictionaryCreate(
        kCFAllocatorDefault, NULL, NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    __NSDictionary0_val = (void *)_empty_dict;
}
CEOF

clang -dynamiclib -o "$APP/libCoreFoundationWrapper.dylib" \
  /tmp/cf_stubs.c \
  -Wl,-reexport_library,/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation \
  -install_name "@loader_path/libCoreFoundationWrapper.dylib" \
  -compatibility_version 150.0.0 -current_version 4040.1.255 \
  -framework CoreFoundation \
  -Wno-deprecated-declarations 2>&1
echo "  CoreFoundation wrapper: OK"

# ── libSystem wrapper (re-export + legacy support) ──
# Use the MacPorts Legacy Support library's approach
clang -dynamiclib -o "$APP/libSystemWrapper.dylib" \
  -Wl,-reexport_library,/usr/lib/libSystem.B.dylib \
  -Wl,-force_load,"$LEGACY_STATIC" \
  -install_name "@loader_path/libSystemWrapper.dylib" \
  -compatibility_version 1.0.0 -current_version 1356.0.0 \
  -framework CoreFoundation -framework Security -framework CoreVideo -framework CoreGraphics -lobjc \
  -Wno-deprecated-declarations 2>&1
echo "  libSystem wrapper: OK"

# ── libc++ wrapper ──
cat > /tmp/cxx_stubs.c << 'CEOF'
#include <pthread.h>
#include <stdlib.h>

/* shared_mutex_base */
void _ZNSt3__119__shared_mutex_baseC1Ev(void *s) __asm("__ZNSt3__119__shared_mutex_baseC1Ev");
void _ZNSt3__119__shared_mutex_baseC1Ev(void *s) { pthread_rwlock_init((pthread_rwlock_t*)s, NULL); }
void _ZNSt3__119__shared_mutex_base4lockEv(void *s) __asm("__ZNSt3__119__shared_mutex_base4lockEv");
void _ZNSt3__119__shared_mutex_base4lockEv(void *s) { pthread_rwlock_wrlock((pthread_rwlock_t*)s); }
void _ZNSt3__119__shared_mutex_base6unlockEv(void *s) __asm("__ZNSt3__119__shared_mutex_base6unlockEv");
void _ZNSt3__119__shared_mutex_base6unlockEv(void *s) { pthread_rwlock_unlock((pthread_rwlock_t*)s); }
void _ZNSt3__119__shared_mutex_base11lock_sharedEv(void *s) __asm("__ZNSt3__119__shared_mutex_base11lock_sharedEv");
void _ZNSt3__119__shared_mutex_base11lock_sharedEv(void *s) { pthread_rwlock_rdlock((pthread_rwlock_t*)s); }
void _ZNSt3__119__shared_mutex_base13unlock_sharedEv(void *s) __asm("__ZNSt3__119__shared_mutex_base13unlock_sharedEv");
void _ZNSt3__119__shared_mutex_base13unlock_sharedEv(void *s) { pthread_rwlock_unlock((pthread_rwlock_t*)s); }

/* shared_timed_mutex */
void _ZNSt3__118shared_timed_mutexC1Ev(void *s) __asm("__ZNSt3__118shared_timed_mutexC1Ev");
void _ZNSt3__118shared_timed_mutexC1Ev(void *s) { pthread_rwlock_init((pthread_rwlock_t*)s, NULL); }
void _ZNSt3__118shared_timed_mutex4lockEv(void *s) __asm("__ZNSt3__118shared_timed_mutex4lockEv");
void _ZNSt3__118shared_timed_mutex4lockEv(void *s) { pthread_rwlock_wrlock((pthread_rwlock_t*)s); }
void _ZNSt3__118shared_timed_mutex6unlockEv(void *s) __asm("__ZNSt3__118shared_timed_mutex6unlockEv");
void _ZNSt3__118shared_timed_mutex6unlockEv(void *s) { pthread_rwlock_unlock((pthread_rwlock_t*)s); }
void _ZNSt3__118shared_timed_mutex11lock_sharedEv(void *s) __asm("__ZNSt3__118shared_timed_mutex11lock_sharedEv");
void _ZNSt3__118shared_timed_mutex11lock_sharedEv(void *s) { pthread_rwlock_rdlock((pthread_rwlock_t*)s); }
void _ZNSt3__118shared_timed_mutex13unlock_sharedEv(void *s) __asm("__ZNSt3__118shared_timed_mutex13unlock_sharedEv");
void _ZNSt3__118shared_timed_mutex13unlock_sharedEv(void *s) { pthread_rwlock_unlock((pthread_rwlock_t*)s); }

/* Sized deallocation */
void _ZdlPvm_impl(void *p, size_t s) __asm("__ZdlPvm");
void _ZdlPvm_impl(void *p, size_t s) { (void)s; free(p); }
void _ZdaPvm_impl(void *p, size_t s) __asm("__ZdaPvm");
void _ZdaPvm_impl(void *p, size_t s) { (void)s; free(p); }
CEOF

clang -dynamiclib -o "$APP/libcxxWrapper.dylib" \
  /tmp/cxx_stubs.c \
  -Wl,-reexport_library,/usr/lib/libc++.1.dylib \
  -install_name "@loader_path/libcxxWrapper.dylib" \
  -compatibility_version 1.0.0 -current_version 2000.63.0 \
  -Wno-deprecated-declarations 2>&1
echo "  libc++ wrapper: OK"

# ── libobjc wrapper ──
cat > /tmp/objc_stubs.c << 'CEOF'
#include <objc/objc.h>
#include <objc/message.h>
extern id objc_retainAutoreleasedReturnValue(id obj);
id objc_unsafeClaimAutoreleasedReturnValue(id obj) {
    return objc_retainAutoreleasedReturnValue(obj);
}
CEOF

clang -dynamiclib -o "$APP/libobjcWrapper.dylib" \
  /tmp/objc_stubs.c \
  -Wl,-reexport_library,/usr/lib/libobjc.A.dylib \
  -install_name "@loader_path/libobjcWrapper.dylib" \
  -compatibility_version 1.0.0 -current_version 228.0.0 \
  -lobjc \
  -Wno-deprecated-declarations 2>&1
echo "  libobjc wrapper: OK"

echo ""
echo "=== Rewriting binary load commands ==="

BINARY="$APP/Ex-Zodiac"

/usr/local/bin/mp-install_name_tool -change \
  "/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics" \
  "@loader_path/libCoreGraphicsWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore" \
  "@loader_path/libQuartzCoreWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit" \
  "@loader_path/libAppKitWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo" \
  "@loader_path/libCoreVideoWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
  "@loader_path/libCoreFoundationWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/usr/lib/libSystem.B.dylib" \
  "@loader_path/libSystemWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/usr/lib/libc++.1.dylib" \
  "@loader_path/libcxxWrapper.dylib" "$BINARY"

/usr/local/bin/mp-install_name_tool -change \
  "/usr/lib/libobjc.A.dylib" \
  "@loader_path/libobjcWrapper.dylib" "$BINARY"

echo "All load commands updated."
echo ""
echo "=== Verifying ==="
otool -L "$BINARY" | head -20
