/*
 * Stub CryptoKit framework for macOS 10.9 compatibility.
 * Exports Swift-mangled symbols with exact linker names via __asm__.
 */

#include <stdlib.h>

static char _stub_data[4096] __attribute__((aligned(16)));

/* Helper: define a stub function with the exact asm symbol name */
#define STUB_FUNC(csym, asmsym) \
    void csym(void) __asm__(asmsym); \
    void csym(void) { abort(); }

/* Helper: define a stub data symbol with the exact asm symbol name */
#define STUB_DATA(csym, asmsym) \
    void *csym __asm__(asmsym) __attribute__((visibility("default"))) = (void*)_stub_data;

/* ChaChaPoly */
STUB_FUNC(ck1, "_$s9CryptoKit03ChaC4PolyO4open_5using14authenticating10Foundation4DataVAC9SealedBoxV_AA12SymmetricKeyVxtKAG0I8ProtocolRzlFZ")
STUB_FUNC(ck2, "_$s9CryptoKit03ChaC4PolyO4seal_5using5nonce14authenticatingAC9SealedBoxVx_AA12SymmetricKeyVAC5NonceVSgq_tK10Foundation12DataProtocolRzAoPR_r0_lFZ")
STUB_FUNC(ck3, "_$s9CryptoKit03ChaC4PolyO5NonceV4dataAEx_tKc10Foundation12DataProtocolRzlufC")
STUB_FUNC(ck4, "_$s9CryptoKit03ChaC4PolyO5NonceVMa")
STUB_DATA(ck5, "_$s9CryptoKit03ChaC4PolyO5NonceVMn")
STUB_FUNC(ck6, "_$s9CryptoKit03ChaC4PolyO9SealedBoxV10ciphertext10Foundation4DataVvg")
STUB_FUNC(ck7, "_$s9CryptoKit03ChaC4PolyO9SealedBoxV3tag10Foundation4DataVvg")
STUB_FUNC(ck8, "_$s9CryptoKit03ChaC4PolyO9SealedBoxV5nonce10ciphertext3tagAeC5NonceV_xq_tKc10Foundation12DataProtocolRzAkLR_r0_lufC")

/* CryptoKitError */
STUB_DATA(ck9, "_$s9CryptoKit0aB5ErrorO21authenticationFailureyA2CmFWC")
STUB_FUNC(ck10, "_$s9CryptoKit0aB5ErrorOMa")

/* SymmetricKey */
STUB_FUNC(ck11, "_$s9CryptoKit12SymmetricKeyV4dataACx_tc10Foundation15ContiguousBytesRzlufC")
STUB_FUNC(ck12, "_$s9CryptoKit12SymmetricKeyVMa")

/* AES.GCM */
STUB_FUNC(ck13, "_$s9CryptoKit3AESO3GCMO4open_5using14authenticating10Foundation4DataVAE9SealedBoxV_AA12SymmetricKeyVxtKAI0I8ProtocolRzlFZ")
STUB_FUNC(ck14, "_$s9CryptoKit3AESO3GCMO4seal_5using5nonce14authenticatingAE9SealedBoxVx_AA12SymmetricKeyVAE5NonceVSgq_tK10Foundation12DataProtocolRzAqRR_r0_lFZ")
STUB_FUNC(ck15, "_$s9CryptoKit3AESO3GCMO5NonceV4dataAGx_tKc10Foundation12DataProtocolRzlufC")
STUB_FUNC(ck16, "_$s9CryptoKit3AESO3GCMO5NonceVMa")
STUB_DATA(ck17, "_$s9CryptoKit3AESO3GCMO5NonceVMn")
STUB_FUNC(ck18, "_$s9CryptoKit3AESO3GCMO9SealedBoxV10ciphertext10Foundation4DataVvg")
STUB_FUNC(ck19, "_$s9CryptoKit3AESO3GCMO9SealedBoxV3tag10Foundation4DataVvg")
STUB_FUNC(ck20, "_$s9CryptoKit3AESO3GCMO9SealedBoxV5nonce10ciphertext3tagAgE5NonceV_xq_tKc10Foundation12DataProtocolRzAmNR_r0_lufC")
STUB_FUNC(ck21, "_$s9CryptoKit3AESO3GCMO9SealedBoxVMa")
STUB_DATA(ck22, "_$s9CryptoKit3AESO3GCMO9SealedBoxVMn")
