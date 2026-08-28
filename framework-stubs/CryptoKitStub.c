/*
 * Hand-written stub CryptoKit.framework for OS X 10.9.
 *
 * CryptoKit is Swift-only, and its API surface is Swift metadata -- nominal
 * type descriptors, protocol conformances, metadata accessors. Those cannot be
 * synthesised in C in any form a caller could actually use, and the algorithms
 * behind them (AES-GCM, SHA-256) are reachable on 10.9 only through entirely
 * different APIs with no shared calling convention. So this resolves the link
 * and nothing more.
 *
 * Every function aborts rather than returning a wrong answer: silently
 * returning zeroed key material or an empty digest from a crypto primitive is
 * the most dangerous possible failure mode. A caller that reaches one of these
 * dies loudly at the call site, which is where the problem can be seen.
 *
 * Symbols are exactly those real binaries bind; the list lives in
 * frameworks.json. Descriptor symbols (Mn/Mc/Mp/WC) are data and point at a
 * zeroed block, because they are dereferenced during loading, not called.
 */

#include <stdio.h>
#include <stdlib.h>

static char _stub_data[4096] __attribute__((aligned(16)));

#define STUB_FUNC(csym, asmsym) \
    void csym(void) __asm__(asmsym); \
    void csym(void) { \
        fprintf(stderr, "CryptoKit stub: %s is not implemented on 10.9\n", asmsym); \
        abort(); \
    }

#define STUB_DATA(csym, asmsym) \
    void *csym __asm__(asmsym) __attribute__((visibility("default"))) = (void *)_stub_data;

STUB_DATA(ck0, "_$s9CryptoKit0aB5ErrorO22incorrectParameterSizeyA2CmFWC")
STUB_FUNC(ck1, "_$s9CryptoKit0aB5ErrorOMa")
STUB_DATA(ck2, "_$s9CryptoKit0aB5ErrorOs0C0AAMc")
STUB_FUNC(ck3, "_$s9CryptoKit12HashFunctionP6update13bufferPointerySW_tFTj")
STUB_FUNC(ck4, "_$s9CryptoKit12HashFunctionP8finalize6DigestQzyFTj")
STUB_FUNC(ck5, "_$s9CryptoKit12HashFunctionPxycfCTj")
STUB_FUNC(ck6, "_$s9CryptoKit12SHA256DigestVMa")
STUB_DATA(ck7, "_$s9CryptoKit12SHA256DigestVMn")
STUB_DATA(ck8, "_$s9CryptoKit12SHA256DigestVSTAAMc")
STUB_FUNC(ck9, "_$s9CryptoKit12SymmetricKeyV15withUnsafeBytesyxxSWKXEKlF")
STUB_FUNC(ck10, "_$s9CryptoKit12SymmetricKeyV4dataACx_tc10Foundation15ContiguousBytesRzlufC")
STUB_FUNC(ck11, "_$s9CryptoKit12SymmetricKeyV4sizeAcA0cD4SizeV_tcfC")
STUB_FUNC(ck12, "_$s9CryptoKit12SymmetricKeyVMa")
STUB_DATA(ck13, "_$s9CryptoKit12SymmetricKeyVMn")
STUB_FUNC(ck14, "_$s9CryptoKit16SymmetricKeySizeV7bits256ACvgZ")
STUB_FUNC(ck15, "_$s9CryptoKit16SymmetricKeySizeVMa")
STUB_FUNC(ck16, "_$s9CryptoKit3AESO3GCMO4open_5using10Foundation4DataVAE9SealedBoxV_AA12SymmetricKeyVtKFZ")
STUB_FUNC(ck17, "_$s9CryptoKit3AESO3GCMO4seal_5using5nonceAE9SealedBoxVx_AA12SymmetricKeyVAE5NonceVSgtK10Foundation12DataProtocolRzlFZ")
STUB_FUNC(ck18, "_$s9CryptoKit3AESO3GCMO5NonceVMa")
STUB_DATA(ck19, "_$s9CryptoKit3AESO3GCMO5NonceVMn")
STUB_FUNC(ck20, "_$s9CryptoKit3AESO3GCMO9SealedBoxV8combined10Foundation4DataVSgvg")
STUB_FUNC(ck21, "_$s9CryptoKit3AESO3GCMO9SealedBoxV8combinedAG10Foundation4DataV_tcfC")
STUB_FUNC(ck22, "_$s9CryptoKit3AESO3GCMO9SealedBoxVMa")
STUB_DATA(ck23, "_$s9CryptoKit6SHA256VAA12HashFunctionAAMc")
STUB_FUNC(ck24, "_$s9CryptoKit6SHA256VMa")
