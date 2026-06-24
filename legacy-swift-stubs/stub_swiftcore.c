/*
 * Stub libswiftCore.dylib for macOS 10.9 compatibility.
 */

#include <stdlib.h>
#include <stdio.h>

static char _stub_data[4096] __attribute__((aligned(16)));

#define STUB_FUNC(csym, asmsym) \
    void csym(void) __asm__(asmsym); \
    void csym(void) { abort(); }

#define STUB_DATA(csym, asmsym) \
    void *csym __asm__(asmsym) __attribute__((visibility("default"))) = (void*)_stub_data;

STUB_FUNC(sc1, "_$sS2SycfC")
STUB_FUNC(sc2, "_$sSZss17FixedWidthIntegerRzrlEyxqd__cSzRd__lufC")
STUB_DATA(sc3, "_$sSiN")
STUB_DATA(sc4, "_$sSiSZsMc")
STUB_DATA(sc5, "_$sSis17FixedWidthIntegersMc")

void sc6(void) __asm__("_$ss17_assertionFailure__4file4line5flagss5NeverOs12StaticStringV_SSAHSus6UInt32VtF");
void sc6(void) {
    fprintf(stderr, "Swift assertion failure (stub)\n");
    abort();
}

STUB_DATA(sc7, "_$ss5ErrorMp")
STUB_DATA(sc8, "_$ss5Int32VN")
STUB_DATA(sc9, "_$ss5Int32VSzsMc")

/* Swift runtime functions - no-op stubs since CryptoKit won't be called */
void *swift_retain(void *obj) { return obj; }
void swift_release(void *obj) { (void)obj; }
void *swift_bridgeObjectRetain(void *obj) { return obj; }
void *swift_errorRetain(void *err) { return err; }
void swift_errorRelease(void *err) { (void)err; }

/* swift_dynamicCast: returns false (cast failed) */
int swift_dynamicCast(void *dest, void *src, void *srcType,
                      void *targetType, unsigned flags) {
    (void)dest; (void)src; (void)srcType; (void)targetType; (void)flags;
    return 0;
}

/* swift_getTypeByMangledNameInContext: return NULL */
void *swift_getTypeByMangledNameInContext(const char *name, int nameLength,
                                          void *context, void *genericArgs) {
    (void)name; (void)nameLength; (void)context; (void)genericArgs;
    return NULL;
}

/* swift_getWitnessTable: return NULL */
void *swift_getWitnessTable(void *conformance, void *type, void *instantiationArgs) {
    (void)conformance; (void)type; (void)instantiationArgs;
    return NULL;
}

/* Metadata-access runtime entry points that newer Swift binaries reference
 * directly (a .NET host that links CryptoKit imports these even though it
 * never drives the Swift runtime). Safe no-op/passthrough values suffice for
 * load-time resolution; if any were actually called, these returns keep the
 * caller from faulting. */

/* MetadataResponse { const Metadata *Value; size_t State; }. MetadataState
 * Complete == 0, so {type, 0} reads as "metadata fully realised". */
typedef struct { const void *Value; unsigned long State; } swift_MetadataResponse;
swift_MetadataResponse swift_checkMetadataState(unsigned long request, const void *type) {
    (void)request;
    swift_MetadataResponse r; r.Value = type; r.State = 0; return r;
}
void *swift_getAssociatedTypeWitness(unsigned long request, void *wtable,
        const void *conformingType, const void *reqBase, const void *assocType) {
    (void)request; (void)wtable; (void)conformingType; (void)reqBase; (void)assocType;
    return NULL;
}
const void *swift_getAssociatedConformanceWitness(void *wtable, const void *conformingType,
        const void *assocType, const void *reqBase, const void *assoc) {
    (void)wtable; (void)conformingType; (void)assocType; (void)reqBase; (void)assoc;
    return NULL;
}
void *swift_getTypeByMangledNameInContextInMetadataState(unsigned long state, const char *name,
        unsigned long nameLength, const void *context, const void * const *genericArgs) {
    (void)state; (void)name; (void)nameLength; (void)context; (void)genericArgs;
    return NULL;
}
