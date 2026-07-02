/*
 * Real implementations of the modern unified SecKey* API (added 10.12+) for
 * macOS 10.9, backed by the OpenSSL (libcrypto 0.9.8) that ships with 10.9.
 *
 * WHY: .NET's Apple crypto PAL (System.Security.Cryptography.Native.Apple,
 * statically linked into a NativeAOT/SingleFile binary) performs RSA public-key
 * operations by calling SecKeyCreateWithData + SecKeyCreateEncryptedData /
 * SecKeyCreateSignature. The TLS *trust* path (SecTrustEvaluate) doesn't need
 * these, so security_wrapper_stubs.c originally NULL-stubbed them — which is
 * fine for anonymous work but makes any real cryptographic op fail with
 * "Error occurred during a cryptographic operation." The load-bearing case is
 * authenticated Steam login: DepotDownloader RSA-encrypts the password with
 * Steam's public key (PKCS#1 v1.5) via SecKeyCreateEncryptedData.
 *
 * Design: our SecKeyRef is a magic-tagged CFData carrying an OpenSSL RSA*, so
 * it survives CFRetain/CFRelease from .NET's SafeHandle. Every override first
 * checks the tag; anything that isn't ours (e.g. a real key from
 * SecCertificateCopyPublicKey) is delegated to the genuine framework function.
 *
 * Build into libSecurityWrapper alongside security_wrapper_stubs.c (which keeps
 * the kSecKeyAlgorithm* constants and the SecTrust overrides) and link -lcrypto.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/objects.h>
#include <openssl/err.h>

/* Algorithm constants live in security_wrapper_stubs.c. */
extern const CFStringRef kSecKeyAlgorithmRSAEncryptionRaw;
extern const CFStringRef kSecKeyAlgorithmRSAEncryptionPKCS1;
extern const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA1;
extern const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA256;
extern const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1;
extern const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256;
extern const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384;
extern const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512;

#define MK_MAGIC 0x724B7952u   /* 'RyKr' */
typedef struct { uint32_t magic; RSA *rsa; int isPublic; } MyKey;

static void *real_sym(const char *name) {
    static void *sec = NULL;
    if (!sec) sec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
    return sec ? dlsym(sec, name) : NULL;
}

static int mk_is(CFTypeRef k) {
    if (!k || CFGetTypeID(k) != CFDataGetTypeID()) return 0;
    if ((size_t)CFDataGetLength((CFDataRef)k) != sizeof(MyKey)) return 0;
    return ((const MyKey *)CFDataGetBytePtr((CFDataRef)k))->magic == MK_MAGIC;
}
static RSA *mk_rsa(CFTypeRef k) { return ((const MyKey *)CFDataGetBytePtr((CFDataRef)k))->rsa; }
static int  mk_pub(CFTypeRef k) { return ((const MyKey *)CFDataGetBytePtr((CFDataRef)k))->isPublic; }

static SecKeyRef mk_wrap(RSA *rsa, int isPublic) {
    if (!rsa) return NULL;
    MyKey m; m.magic = MK_MAGIC; m.rsa = rsa; m.isPublic = isPublic;
    return (SecKeyRef)CFDataCreate(kCFAllocatorDefault, (const UInt8 *)&m, sizeof(m));
}
static CFErrorRef mk_err(const char *msg) {
    CFStringRef s = CFStringCreateWithCString(kCFAllocatorDefault, msg, kCFStringEncodingUTF8);
    const void *k[1] = { kCFErrorLocalizedDescriptionKey }; const void *v[1] = { s };
    CFDictionaryRef ui = CFDictionaryCreate(kCFAllocatorDefault, k, v, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFErrorRef e = CFErrorCreate(kCFAllocatorDefault, CFSTR("NSOSStatusErrorDomain"), -50, ui);
    if (s) CFRelease(s); if (ui) CFRelease(ui);
    return e;
}

/* ── key import ─────────────────────────────────────────────────────────── */
SecKeyRef SecKeyCreateWithData(CFDataRef data, CFDictionaryRef attributes, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!data || !attributes) { if (error) *error = mk_err("bad params"); return NULL; }
    CFStringRef type  = (CFStringRef)CFDictionaryGetValue(attributes, kSecAttrKeyType);
    CFStringRef klass = (CFStringRef)CFDictionaryGetValue(attributes, kSecAttrKeyClass);
    int isPublic = klass && CFEqual(klass, kSecAttrKeyClassPublic);

    if (type && CFEqual(type, kSecAttrKeyTypeRSA)) {
        const unsigned char *p = (const unsigned char *)CFDataGetBytePtr(data);
        long len = CFDataGetLength(data);
        const unsigned char *pp = p;
        RSA *rsa = NULL;
        if (isPublic) {
            rsa = d2i_RSAPublicKey(NULL, &pp, len);          /* PKCS#1 RSAPublicKey */
            if (!rsa) { pp = p; rsa = d2i_RSA_PUBKEY(NULL, &pp, len); } /* X.509 SPKI */
        } else {
            rsa = d2i_RSAPrivateKey(NULL, &pp, len);         /* PKCS#1 RSAPrivateKey */
        }
        if (!rsa) { if (error) *error = mk_err("RSA import failed"); return NULL; }
        return mk_wrap(rsa, isPublic);
    }
    /* EC / other: 10.9 has no unified importer; unsupported. */
    if (error) *error = mk_err("unsupported key type");
    return NULL;
}

size_t SecKeyGetBlockSize(SecKeyRef key) {
    if (mk_is(key)) return (size_t)RSA_size(mk_rsa(key));
    size_t (*real)(SecKeyRef) = (size_t (*)(SecKeyRef))real_sym("SecKeyGetBlockSize");
    return real ? real(key) : 0;
}

/* ── encrypt / decrypt ──────────────────────────────────────────────────── */
static int rsa_padding_for(CFStringRef alg) {
    if (CFEqual(alg, kSecKeyAlgorithmRSAEncryptionPKCS1)) return RSA_PKCS1_PADDING;
    if (CFEqual(alg, kSecKeyAlgorithmRSAEncryptionRaw))   return RSA_NO_PADDING;
    /* OAEP: libcrypto 0.9.8's RSA_public_encrypt only does SHA-1 MGF1/OAEP. */
    return RSA_PKCS1_OAEP_PADDING;
}

CFDataRef SecKeyCreateEncryptedData(SecKeyRef key, CFStringRef algorithm, CFDataRef plaintext, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!mk_is(key) || !plaintext) { if (error) *error = mk_err("bad key/plaintext"); return NULL; }
    RSA *rsa = mk_rsa(key);
    int rsz = RSA_size(rsa);
    unsigned char *out = (unsigned char *)malloc(rsz);
    if (!out) return NULL;
    int n = RSA_public_encrypt((int)CFDataGetLength(plaintext),
                               (const unsigned char *)CFDataGetBytePtr(plaintext),
                               out, rsa, rsa_padding_for(algorithm));
    if (n < 0) { free(out); if (error) *error = mk_err("RSA_public_encrypt failed"); return NULL; }
    CFDataRef r = CFDataCreate(kCFAllocatorDefault, out, n);
    free(out);
    return r;
}

CFDataRef SecKeyCreateDecryptedData(SecKeyRef key, CFStringRef algorithm, CFDataRef ciphertext, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!mk_is(key) || !ciphertext) { if (error) *error = mk_err("bad key/ciphertext"); return NULL; }
    RSA *rsa = mk_rsa(key);
    int rsz = RSA_size(rsa);
    unsigned char *out = (unsigned char *)malloc(rsz);
    if (!out) return NULL;
    int n = RSA_private_decrypt((int)CFDataGetLength(ciphertext),
                                (const unsigned char *)CFDataGetBytePtr(ciphertext),
                                out, rsa, rsa_padding_for(algorithm));
    if (n < 0) { free(out); if (error) *error = mk_err("RSA_private_decrypt failed"); return NULL; }
    CFDataRef r = CFDataCreate(kCFAllocatorDefault, out, n);
    free(out);
    return r;
}

/* ── sign / verify (digest PKCS#1 v1.5) ─────────────────────────────────── */
static int nid_for_sig(CFStringRef alg) {
    if (CFEqual(alg, kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1))   return NID_sha1;
    if (CFEqual(alg, kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256)) return NID_sha256;
    if (CFEqual(alg, kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384)) return NID_sha384;
    if (CFEqual(alg, kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512)) return NID_sha512;
    return NID_undef;
}

CFDataRef SecKeyCreateSignature(SecKeyRef key, CFStringRef algorithm, CFDataRef digest, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!mk_is(key) || !digest) { if (error) *error = mk_err("bad key/digest"); return NULL; }
    int nid = nid_for_sig(algorithm);
    if (nid == NID_undef) { if (error) *error = mk_err("unsupported signature alg"); return NULL; }
    RSA *rsa = mk_rsa(key);
    unsigned int siglen = RSA_size(rsa);
    unsigned char *sig = (unsigned char *)malloc(siglen);
    if (!sig) return NULL;
    if (RSA_sign(nid, (const unsigned char *)CFDataGetBytePtr(digest),
                 (unsigned int)CFDataGetLength(digest), sig, &siglen, rsa) != 1) {
        free(sig); if (error) *error = mk_err("RSA_sign failed"); return NULL;
    }
    CFDataRef r = CFDataCreate(kCFAllocatorDefault, sig, siglen);
    free(sig);
    return r;
}

Boolean SecKeyVerifySignature(SecKeyRef key, CFStringRef algorithm, CFDataRef signedData,
                              CFDataRef signature, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!mk_is(key) || !signedData || !signature) return false;
    int nid = nid_for_sig(algorithm);
    if (nid == NID_undef) return false;
    RSA *rsa = mk_rsa(key);
    int ok = RSA_verify(nid, (const unsigned char *)CFDataGetBytePtr(signedData),
                        (unsigned int)CFDataGetLength(signedData),
                        (unsigned char *)CFDataGetBytePtr(signature),
                        (unsigned int)CFDataGetLength(signature), rsa);
    return ok == 1;
}

/* ── export / misc ──────────────────────────────────────────────────────── */
CFDataRef SecKeyCopyExternalRepresentation(SecKeyRef key, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!mk_is(key)) return NULL;
    RSA *rsa = mk_rsa(key);
    unsigned char *buf = NULL;
    int len = mk_pub(key) ? i2d_RSAPublicKey(rsa, &buf) : i2d_RSAPrivateKey(rsa, &buf);
    if (len < 0 || !buf) return NULL;
    CFDataRef r = CFDataCreate(kCFAllocatorDefault, buf, len);
    OPENSSL_free(buf);
    return r;
}

SecKeyRef SecKeyCopyPublicKey(SecKeyRef key) {
    if (!mk_is(key)) return NULL;
    RSA *pub = RSAPublicKey_dup(mk_rsa(key));
    return mk_wrap(pub, 1);
}

CFDictionaryRef SecKeyCopyAttributes(SecKeyRef key) {
    if (!mk_is(key)) return NULL;
    int bits = RSA_size(mk_rsa(key)) * 8;
    CFNumberRef sz = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bits);
    const void *k[3] = { kSecAttrKeyType, kSecAttrKeyClass, kSecAttrKeySizeInBits };
    const void *v[3] = { kSecAttrKeyTypeRSA,
                         mk_pub(key) ? kSecAttrKeyClassPublic : kSecAttrKeyClassPrivate, sz };
    CFDictionaryRef d = CFDictionaryCreate(kCFAllocatorDefault, k, v, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (sz) CFRelease(sz);
    return d;
}

SecKeyRef SecKeyCreateRandomKey(CFDictionaryRef parameters, CFErrorRef *error) {
    if (error) *error = NULL;
    if (!parameters) return NULL;
    CFStringRef type = (CFStringRef)CFDictionaryGetValue(parameters, kSecAttrKeyType);
    if (!type || !CFEqual(type, kSecAttrKeyTypeRSA)) { if (error) *error = mk_err("unsupported"); return NULL; }
    int bits = 2048;
    CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(parameters, kSecAttrKeySizeInBits);
    if (n) CFNumberGetValue(n, kCFNumberIntType, &bits);
    RSA *rsa = RSA_generate_key(bits, RSA_F4, NULL, NULL);
    if (!rsa) { if (error) *error = mk_err("RSA_generate_key failed"); return NULL; }
    return mk_wrap(rsa, 0);
}

/* EC key exchange — not supported on 10.9's OpenSSL path here. */
CFDataRef SecKeyCopyKeyExchangeResult(SecKeyRef priv, CFStringRef algorithm, SecKeyRef pub,
                                      CFDictionaryRef parameters, CFErrorRef *error) {
    (void)priv; (void)algorithm; (void)pub; (void)parameters;
    if (error) *error = NULL;
    return NULL;
}
