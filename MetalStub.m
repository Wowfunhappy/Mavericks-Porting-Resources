/*
 * MetalStub.m — Stub Metal.framework for macOS 10.9
 *
 * Provides all Metal ObjC classes, C functions, and data symbols
 * that the Godot 4.5 engine binary references from Metal.framework.
 * These are stubs — the actual rendering path will use OpenGL via ANGLE.
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

/* ── Data symbols (NSString constants) ── */

NSString *MTLCommandBufferEncoderInfoErrorKey = @"MTLCommandBufferEncoderInfoErrorKey";
NSString *MTLCommonCounterSetTimestamp = @"MTLCommonCounterSetTimestamp";
NSString *MTLCommonCounterTimestamp = @"MTLCommonCounterTimestamp";

/* ── C functions ── */

id MTLCreateSystemDefaultDevice(void) {
	return nil;
}

NSArray *MTLCopyAllDevices(void) {
	return @[];
}

/* ── Stub ObjC classes ── */
/* Each class is a minimal NSObject subclass so dyld can resolve the class symbol. */

#define METAL_STUB_CLASS(name) \
	@interface name : NSObject @end \
	@implementation name @end

METAL_STUB_CLASS(MTLArgumentDescriptor)
METAL_STUB_CLASS(MTLBlitPassDescriptor)
METAL_STUB_CLASS(MTLCaptureDescriptor)
METAL_STUB_CLASS(MTLCaptureManager)
METAL_STUB_CLASS(MTLCommandBufferDescriptor)
METAL_STUB_CLASS(MTLCompileOptions)
METAL_STUB_CLASS(MTLComputePipelineDescriptor)
METAL_STUB_CLASS(MTLCounterSampleBufferDescriptor)
METAL_STUB_CLASS(MTLDepthStencilDescriptor)
METAL_STUB_CLASS(MTLFunctionConstantValues)
METAL_STUB_CLASS(MTLHeapDescriptor)
METAL_STUB_CLASS(MTLRenderPassDepthAttachmentDescriptor)
METAL_STUB_CLASS(MTLRenderPassDescriptor)
METAL_STUB_CLASS(MTLRenderPassStencilAttachmentDescriptor)
METAL_STUB_CLASS(MTLRenderPipelineColorAttachmentDescriptor)
METAL_STUB_CLASS(MTLRenderPipelineDescriptor)
METAL_STUB_CLASS(MTLSamplerDescriptor)
METAL_STUB_CLASS(MTLSharedEventListener)
METAL_STUB_CLASS(MTLStageInputOutputDescriptor)
METAL_STUB_CLASS(MTLStencilDescriptor)
METAL_STUB_CLASS(MTLTextureDescriptor)
METAL_STUB_CLASS(MTLVertexAttributeDescriptor)
METAL_STUB_CLASS(MTLVertexBufferLayoutDescriptor)
METAL_STUB_CLASS(MTLVertexDescriptor)

/*
 * CAMetalLayer — stub subclass of CALayer (normally from QuartzCore, added 10.11)
 * With DYLD_FORCE_FLAT_NAMESPACE, this satisfies the reference from QuartzCore.
 */
/* CAMetalLayer is in the QuartzCore wrapper */
