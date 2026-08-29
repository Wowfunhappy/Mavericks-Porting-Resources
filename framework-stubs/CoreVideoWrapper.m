// Wrapper for CoreVideo on OS X 10.9: supplies the symbols this OS's copy
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

// Data constants. Where the real value is documented it is reproduced
// exactly, because callers compare and serialise these.
NSString * const kCVImageBufferColorPrimaries_ITU_R_2020 = @"ITU_R_2020";
NSString * const kCVImageBufferTransferFunction_ITU_R_2020 = @"ITU_R_2020";
NSString * const kCVImageBufferTransferFunction_ITU_R_2100_HLG = @"ITU_R_2100_HLG";
NSString * const kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ = @"SMPTE_ST_2084_PQ";
NSString * const kCVImageBufferTransferFunction_SMPTE_ST_428_1 = @"SMPTE_ST_428_1";
NSString * const kCVImageBufferYCbCrMatrix_ITU_R_2020 = @"ITU_R_2020";
NSString * const kCVPixelBufferMetalCompatibilityKey = @"MetalCompatibility";

// ---- hand-written ----

#import <CoreVideo/CoreVideo.h>

// 10.9 has CVBufferGetAttachments, which returns a borrowed dictionary; the
// later Copy form returns an owned one. Copying is the whole difference.
CFDictionaryRef CVBufferCopyAttachments(CVBufferRef buffer, CVAttachmentMode mode) {
    if (!buffer) return NULL;
    CFDictionaryRef borrowed = CVBufferGetAttachments(buffer, mode);
    if (!borrowed) return NULL;
    return CFDictionaryCreateCopy(kCFAllocatorDefault, borrowed);
}

// These map an ISO/IEC integer code point to the CoreVideo constant naming it.
// Only code points 10.9 already has constants for can be answered; for a newer
// one there is no constant here to return, and NULL is what the real function
// returns for a code point it does not know.
CFStringRef CVColorPrimariesGetStringForIntegerCodePoint(int codePoint) {
    switch (codePoint) {
        case 1: return kCVImageBufferColorPrimaries_ITU_R_709_2;
        case 5: return kCVImageBufferColorPrimaries_EBU_3213;
        case 6: return kCVImageBufferColorPrimaries_SMPTE_C;
        default: return NULL;
    }
}
CFStringRef CVTransferFunctionGetStringForIntegerCodePoint(int codePoint) {
    switch (codePoint) {
        case 1: return kCVImageBufferTransferFunction_ITU_R_709_2;
        case 7: return kCVImageBufferTransferFunction_SMPTE_240M_1995;
        default: return NULL;
    }
}
CFStringRef CVYCbCrMatrixGetStringForIntegerCodePoint(int codePoint) {
    switch (codePoint) {
        case 1: return kCVImageBufferYCbCrMatrix_ITU_R_709_2;
        case 6: return kCVImageBufferYCbCrMatrix_ITU_R_601_4;
        case 7: return kCVImageBufferYCbCrMatrix_SMPTE_240M_1995;
        default: return NULL;
    }
}
