// Wrapper for AVFoundation on OS X 10.9: supplies the symbols this OS's copy
// lacks, and re-exports the real one so everything else still resolves.
// Hand-written: the class list started from the symbols real binaries bind,
// but the constants and functions carry real values and implementations,
// so this file is maintained by hand and not regenerated.
#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>

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
STUB_CLASS(AVAudioApplication)
STUB_CLASS(AVCaptureDeviceDiscoverySession)

// Data constants. Where the real value is documented it is reproduced
// exactly, because callers compare and serialise these.
NSString * const AVAudioApplicationInputMuteStateChangeNotification = @"AVAudioApplicationInputMuteStateChangeNotification";
NSString * const AVCaptureDeviceTypeBuiltInWideAngleCamera = @"AVCaptureDeviceTypeBuiltInWideAngleCamera";
NSString * const AVCaptureDeviceTypeContinuityCamera = @"AVCaptureDeviceTypeContinuityCamera";
NSString * const AVCaptureDeviceTypeDeskViewCamera = @"AVCaptureDeviceTypeDeskViewCamera";
NSString * const AVCaptureDeviceTypeExternal = @"AVCaptureDeviceTypeExternal";
NSString * const AVCaptureDeviceTypeExternalUnknown = @"AVCaptureDeviceTypeExternalUnknown";
NSString * const AVCaptureSessionInterruptionEndedNotification = @"AVCaptureSessionInterruptionEndedNotification";
NSString * const AVCaptureSessionWasInterruptedNotification = @"AVCaptureSessionWasInterruptedNotification";
NSString * const AVVideoAllowFrameReorderingKey = @"AllowFrameReordering";
NSString * const AVVideoCodecTypeH264 = @"avc1";
NSString * const AVVideoCodecTypeHEVC = @"hvc1";
NSString * const AVVideoExpectedSourceFrameRateKey = @"ExpectedFrameRate";
NSString * const AVVideoH264EntropyModeCABAC = @"CABAC";
NSString * const AVVideoH264EntropyModeKey = @"H264EntropyMode";
