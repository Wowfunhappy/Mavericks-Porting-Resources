// Hand-written stub Metal.framework for OS X 10.9.
//
// 10.9 predates Metal entirely and there is no underlying capability to shim:
// a GPU command API cannot be emulated behind the same interface. So this is a
// pure load-time stub. What makes it safe is that every entry point reports
// "no Metal device", which is a state real callers must already handle -- Macs
// without a Metal-capable GPU return exactly this. A caller that checks
// MTLCreateSystemDefaultDevice() for nil takes its fallback path; one that
// does not was going to crash on such a Mac anyway.
//
// Hand-written rather than generated because the C functions carry that
// meaning; the generator will not invent them.
#import <Foundation/Foundation.h>

// Unknown selectors forward to a no-op returning zero, so a caller that gets
// hold of a descriptor and configures it does not die on the first setter.
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

STUB_CLASS(MTLArgumentDescriptor)
STUB_CLASS(MTLBlitPassDescriptor)
STUB_CLASS(MTLCaptureDescriptor)
STUB_CLASS(MTLCaptureManager)
STUB_CLASS(MTLCommandBufferDescriptor)
STUB_CLASS(MTLCompileOptions)
STUB_CLASS(MTLComputePipelineDescriptor)
STUB_CLASS(MTLCounterSampleBufferDescriptor)
STUB_CLASS(MTLDepthStencilDescriptor)
STUB_CLASS(MTLFunctionConstantValues)
STUB_CLASS(MTLHeapDescriptor)
STUB_CLASS(MTLRenderPassDepthAttachmentDescriptor)
STUB_CLASS(MTLRenderPassDescriptor)
STUB_CLASS(MTLRenderPassStencilAttachmentDescriptor)
STUB_CLASS(MTLRenderPipelineColorAttachmentDescriptor)
STUB_CLASS(MTLRenderPipelineDescriptor)
STUB_CLASS(MTLSamplerDescriptor)
STUB_CLASS(MTLSharedEventListener)
STUB_CLASS(MTLStageInputOutputDescriptor)
STUB_CLASS(MTLStencilDescriptor)
STUB_CLASS(MTLTextureDescriptor)
STUB_CLASS(MTLVertexAttributeDescriptor)
STUB_CLASS(MTLVertexBufferLayoutDescriptor)
STUB_CLASS(MTLVertexDescriptor)

// Notification names. Nothing ever posts these, but a caller may register for
// them, and registering with a nil name raises.
NSString * const MTLDeviceWasAddedNotification = @"MTLDeviceWasAddedNotification";
NSString * const MTLDeviceRemovalRequestedNotification = @"MTLDeviceRemovalRequestedNotification";
NSString * const MTLDeviceWasRemovedNotification = @"MTLDeviceWasRemovedNotification";
NSString * const MTLCommandBufferEncoderInfoErrorKey = @"MTLCommandBufferEncoderInfoErrorKey";
NSString * const MTLCommonCounterSetTimestamp = @"MTLCommonCounterSetTimestamp";
NSString * const MTLCommonCounterTimestamp = @"MTLCommonCounterTimestamp";

// "This machine has no Metal device" -- the honest answer on 10.9.
id MTLCreateSystemDefaultDevice(void) { return nil; }
NSArray *MTLCopyAllDevices(void) { return @[]; }

// The observer variants return the same empty device list. The observer is
// never registered, so no notification can arrive naming a stale observer.
NSArray *MTLCopyAllDevicesWithObserver(id *observer, void (^handler)(id, NSString *)) {
    (void)handler;
    if (observer) *observer = nil;
    return @[];
}

void MTLRemoveDeviceObserver(id observer) { (void)observer; }
