/*
 * Shim over Apple's Swift 5.1 back-deployment libswiftCore for OS X 10.9.
 *
 * Apple's back-deployment runtime (Xcode's usr/lib/swift-5.0/macosx, built
 * with LC_VERSION_MIN_MACOSX 10.9) is the runtime to use on 10.9: Apple built,
 * shipped and tested it against this OS's Objective-C runtime, so Swift/ObjC
 * bridging -- the hard part -- already works. It must be used together with
 * its own matching overlays: libswiftFoundation and friends inline stdlib
 * internals at compile time, so pairing them with a different libswiftCore
 * corrupts objects that cross between them.
 *
 * What it lacks is a handful of entry points that a *newer* Swift compiler
 * emits no matter how low the deployment target is set. This shim adds those
 * four and re-exports the real runtime for everything else. Each is a real
 * implementation or a conservative answer that the caller is required to
 * handle -- none of them fakes a success.
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ── Metadata lookup ──
 *
 * Swift 5.2 split the mangled-name lookup into a form that also states which
 * metadata state was reached. 5.1 has the older call, which always produces
 * complete metadata, so forwarding to it and reporting Complete is exact
 * rather than approximate.
 *
 * MetadataResponse is two words and comes back in rax:rdx. */
typedef struct {
    const void *value;
    size_t state;
} MetadataResponse;

#define METADATA_STATE_COMPLETE 0x00

extern const void *swift_getTypeByMangledNameInContext(
        const char *typeNameStart, size_t typeNameLength,
        const void *context, const void *const *genericArgs);

MetadataResponse swift_getTypeByMangledNameInContextInMetadataState(
        size_t request, const char *typeNameStart, size_t typeNameLength,
        const void *context, const void *const *genericArgs) {
    (void)request;
    MetadataResponse response;
    response.value = swift_getTypeByMangledNameInContext(typeNameStart, typeNameLength,
                                                         context, genericArgs);
    response.state = METADATA_STATE_COMPLETE;
    return response;
}

/* ── Dynamic replacement ──
 *
 * Called from the thunk in front of a function that could be replaced at
 * runtime by @_dynamicReplacement. NULL means "nothing has replaced this",
 * and the thunk then calls the original -- which is the correct answer here,
 * since without the 5.2+ replacement machinery nothing can ever register one. */
void *swift_getFunctionReplacement(char **replFnPtr, char *currFn) {
    (void)replFnPtr;
    (void)currFn;
    return NULL;
}

/* ── Stack promotion ──
 *
 * Asks whether an allocation of this size may live on the stack. False sends
 * it to the heap: always valid, never wrong, and the only honest answer when
 * the runtime cannot measure the remaining stack. */
bool swift_stdlib_isStackAllocationSafe(size_t byteCount, size_t alignment) {
    (void)byteCount;
    (void)alignment;
    return false;
}

/* ── Coroutine frames ──
 *
 * Swift 6 allocates the frame for a `_read`/`_modify` coroutine through this.
 * An allocator index of 0 means the general heap, which is the only allocator
 * available here; the frame is released with free() by the generated code, so
 * malloc is the matching implementation rather than a stand-in. */
void *swift_coroFrameAlloc(size_t size, uint64_t allocatorIndex) {
    (void)allocatorIndex;
    return malloc(size);
}
