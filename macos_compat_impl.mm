/* Runtime implementations for macOS 10.9 compat categories */
#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <GameController/GameController.h>
#include <os/log.h>
#include "../../compat_include/macos_compat.h"

/* ── C-linkage function stubs ── */
extern "C" {
bool CGPreflightScreenCaptureAccess(void) { return true; }
bool CGRequestScreenCaptureAccess(void) { return true; }
os_log_t os_log_create(const char *s, const char *c) { (void)s; (void)c; return 0; }
int pthread_attr_set_qos_class_np(pthread_attr_t *a, unsigned int q, int r) { (void)a; (void)q; (void)r; return 0; }
}

extern "C" const float AVSpeechUtteranceDefaultSpeechRate = 0.5f;
extern "C" const float AVSpeechUtteranceMinimumSpeechRate = 0.0f;
extern "C" const float AVSpeechUtteranceMaximumSpeechRate = 1.0f;

/* ── CVDisplayLinkSetOutputHandler polyfill ── */

static CVReturn _cvdl_callback(CVDisplayLinkRef dl, const CVTimeStamp *now,
    const CVTimeStamp *out, CVOptionFlags fi, CVOptionFlags *fo, void *ctx) {
    CVDisplayLinkOutputHandler handler = (__bridge CVDisplayLinkOutputHandler)ctx;
    return handler(dl, now, out, fi, fo);
}

CVReturn CVDisplayLinkSetOutputHandler(CVDisplayLinkRef displayLink,
    CVDisplayLinkOutputHandler handler) {
    handler = [handler copy];
    return CVDisplayLinkSetOutputCallback(displayLink, _cvdl_callback,
        (__bridge void *)handler);
}

/* ── ObjC category stubs ── */

@implementation NSSavePanel (MavericksCompat)
- (void)setAllowedContentTypes:(NSArray *)types {
    NSMutableArray *exts = [NSMutableArray array];
    for (id type in types) {
        if ([type respondsToSelector:@selector(identifier)]) {
            NSString *ident = [type identifier];
            if (ident) [exts addObject:ident];
        }
    }
    if ([exts count] > 0) [self setAllowedFileTypes:exts];
}
@end

@implementation NSMenu (MavericksCompat)
- (NSInteger)userInterfaceLayoutDirection { return 0; }
- (void)setUserInterfaceLayoutDirection:(NSInteger)dir {}
@end

@implementation NSString (MavericksCompat)
- (BOOL)containsString:(NSString *)str {
    return [self rangeOfString:str].location != NSNotFound;
}
@end

@implementation GCController (MavericksCompat)
- (NSString *)productCategory { return @"Unknown"; }
@end

@implementation NSWorkspace (MavericksMissing)
- (BOOL)isVoiceOverEnabled { return NO; }
- (BOOL)isSwitchControlEnabled { return NO; }
- (BOOL)accessibilityDisplayShouldIncreaseContrast { return NO; }
- (BOOL)accessibilityDisplayShouldReduceMotion { return NO; }
- (BOOL)accessibilityDisplayShouldReduceTransparency { return NO; }
- (BOOL)accessibilityDisplayShouldDifferentiateWithoutColor { return NO; }
- (BOOL)accessibilityDisplayShouldInvertColors { return NO; }
@end

@implementation NSAppearance (MavericksCompat)
- (void)performAsCurrentDrawingAppearance:(void (^)(void))block { if (block) block(); }
- (NSString *)bestMatchFromAppearancesWithNames:(NSArray *)names {
    return names.count > 0 ? names[0] : @"NSAppearanceNameAqua";
}
@end

@implementation CAMetalLayer @end
@implementation UTType
+ (UTType *)plainText { return nil; }
+ (UTType *)data { return nil; }
+ (UTType *)typeWithFilenameExtension:(NSString *)ext { return nil; }
+ (UTType *)typeWithMIMEType:(NSString *)mime { return nil; }
- (NSString *)identifier { return @""; }
@end

@implementation AVSpeechSynthesizer @end
@implementation AVSpeechUtterance @end
@implementation AVSpeechSynthesisVoice @end
