/*
 * Stub libswiftFoundation.dylib for macOS 10.9 compatibility.
 */

#include <stdlib.h>

static char _stub_data[4096] __attribute__((aligned(16)));

#define STUB_FUNC(csym, asmsym) \
    void csym(void) __asm__(asmsym); \
    void csym(void) { abort(); }

#define STUB_DATA(csym, asmsym) \
    void *csym __asm__(asmsym) __attribute__((visibility("default"))) = (void*)_stub_data;

STUB_DATA(sf1, "_$s10Foundation4DataV11DeallocatorO4noneyA2EmFWC")
STUB_FUNC(sf2, "_$s10Foundation4DataV11DeallocatorOMa")
STUB_FUNC(sf3, "_$s10Foundation4DataV11bytesNoCopy5count11deallocatorACSv_SiAC11DeallocatorOtcfC")
STUB_FUNC(sf4, "_$s10Foundation4DataV5countSivg")
STUB_FUNC(sf5, "_$s10Foundation4DataV9copyBytes2to5countySpys5UInt8VG_SitF")
STUB_DATA(sf6, "_$s10Foundation4DataVAA0B8ProtocolAAMc")
STUB_DATA(sf7, "_$s10Foundation4DataVAA15ContiguousBytesAAWP")
STUB_DATA(sf8, "_$s10Foundation4DataVN")
