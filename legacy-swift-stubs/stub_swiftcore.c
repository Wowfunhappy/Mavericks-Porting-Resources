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
