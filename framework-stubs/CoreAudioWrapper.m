// Wrapper for CoreAudio on OS X 10.9: supplies the symbols this OS's copy
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
STUB_CLASS(CATapDescription)

// ---- hand-written ----

#import <CoreAudio/CoreAudio.h>

// Aggregate devices are creatable on 10.9, but only through the HAL plug-in
// property interface, not these wrappers. Process taps (macOS 14) have no
// equivalent at all. Both report the documented "unsupported" status rather
// than inventing a device ID the caller would go on to use.
OSStatus AudioHardwareCreateAggregateDevice(CFDictionaryRef d, AudioObjectID *out) {
    (void)d; if (out) *out = kAudioObjectUnknown;
    return kAudioHardwareUnsupportedOperationError;
}
OSStatus AudioHardwareDestroyAggregateDevice(AudioObjectID id_) {
    (void)id_; return kAudioHardwareUnsupportedOperationError;
}
OSStatus AudioHardwareCreateProcessTap(id d, AudioObjectID *out) {
    (void)d; if (out) *out = kAudioObjectUnknown;
    return kAudioHardwareUnsupportedOperationError;
}
OSStatus AudioHardwareDestroyProcessTap(AudioObjectID id_) {
    (void)id_; return kAudioHardwareUnsupportedOperationError;
}
