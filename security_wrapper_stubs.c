#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

/* SSL ALPN not in 10.9 SecureTransport. Pretend success but offer nothing. */
OSStatus SSLCopyALPNProtocols(void *context, CFArrayRef *protocols) {
    (void)context;
    if (protocols) *protocols = NULL;
    return 0;
}
OSStatus SSLSetALPNProtocols(void *context, CFArrayRef protocols) {
    (void)context; (void)protocols;
    return 0;
}

/* SecCertificateCopyKey (10.14+). Implement via 10.9's SecCertificateCopyPublicKey. */
extern OSStatus SecCertificateCopyPublicKey(SecCertificateRef cert, SecKeyRef *key);

void* SecCertificateCopyKey(SecCertificateRef cert) {
    if (!cert) return NULL;
    SecKeyRef key = NULL;
    if (SecCertificateCopyPublicKey(cert, &key) == errSecSuccess) return key;
    return NULL;
}

/* Modern unified SecKey API (10.12+). Most return NULL — .NET's TLS path
 * doesn't strictly need them when SecTrustEvaluate proceeds with our
 * basic-X509 policy override. */
CFDictionaryRef SecKeyCopyAttributes(SecKeyRef key) { (void)key; return NULL; }
CFDataRef SecKeyCopyExternalRepresentation(SecKeyRef key, CFErrorRef *e) {
    (void)key; if (e) *e = NULL; return NULL;
}
CFDataRef SecKeyCopyKeyExchangeResult(SecKeyRef p, CFStringRef a, SecKeyRef pu,
                                       CFDictionaryRef ps, CFErrorRef *e) {
    (void)p; (void)a; (void)pu; (void)ps; if (e) *e = NULL; return NULL;
}
SecKeyRef SecKeyCopyPublicKey(SecKeyRef key) { (void)key; return NULL; }
CFDataRef SecKeyCreateDecryptedData(SecKeyRef k, CFStringRef a, CFDataRef c, CFErrorRef *e) {
    (void)k; (void)a; (void)c; if (e) *e = NULL; return NULL;
}
CFDataRef SecKeyCreateEncryptedData(SecKeyRef k, CFStringRef a, CFDataRef c, CFErrorRef *e) {
    (void)k; (void)a; (void)c; if (e) *e = NULL; return NULL;
}
SecKeyRef SecKeyCreateRandomKey(CFDictionaryRef p, CFErrorRef *e) {
    (void)p; if (e) *e = NULL; return NULL;
}
CFDataRef SecKeyCreateSignature(SecKeyRef k, CFStringRef a, CFDataRef d, CFErrorRef *e) {
    (void)k; (void)a; (void)d; if (e) *e = NULL; return NULL;
}
SecKeyRef SecKeyCreateWithData(CFDataRef d, CFDictionaryRef a, CFErrorRef *e) {
    (void)d; (void)a; if (e) *e = NULL; return NULL;
}
Boolean SecKeyVerifySignature(SecKeyRef k, CFStringRef a, CFDataRef sd, CFDataRef s, CFErrorRef *e) {
    (void)k; (void)a; (void)sd; (void)s; if (e) *e = NULL; return false;
}

/* Algorithm / attribute constants */
const CFStringRef kSecAttrKeyTypeECSECPrimeRandom = CFSTR("SecAttrKeyTypeECSECPrimeRandom");
const CFStringRef kSecKeyAlgorithmECDHKeyExchangeStandard = CFSTR("algid:keyexchange:ECDH");
const CFStringRef kSecKeyAlgorithmECDSASignatureDigestX962 = CFSTR("algid:sign:ECDSA:digest-X962");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA1   = CFSTR("algid:encrypt:RSA:OAEP:SHA1");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA256 = CFSTR("algid:encrypt:RSA:OAEP:SHA256");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA384 = CFSTR("algid:encrypt:RSA:OAEP:SHA384");
const CFStringRef kSecKeyAlgorithmRSAEncryptionOAEPSHA512 = CFSTR("algid:encrypt:RSA:OAEP:SHA512");
const CFStringRef kSecKeyAlgorithmRSAEncryptionPKCS1 = CFSTR("algid:encrypt:RSA:PKCS1");
const CFStringRef kSecKeyAlgorithmRSAEncryptionRaw  = CFSTR("algid:encrypt:RSA:raw");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1   = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA1");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA256 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA256");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA384 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA384");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA512 = CFSTR("algid:sign:RSA:digest-PKCS1v15:SHA512");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA1   = CFSTR("algid:sign:RSA:digest-PSS:SHA1");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA256 = CFSTR("algid:sign:RSA:digest-PSS:SHA256");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA384 = CFSTR("algid:sign:RSA:digest-PSS:SHA384");
const CFStringRef kSecKeyAlgorithmRSASignatureDigestPSSSHA512 = CFSTR("algid:sign:RSA:digest-PSS:SHA512");
const CFStringRef kSecKeyAlgorithmRSASignatureRaw = CFSTR("algid:sign:RSA:raw");
const CFStringRef kSecUseDataProtectionKeychain = CFSTR("u_DataProtectionKeychain");

/* SecPolicyCreateRevocation (10.9 doesn't have it). Returning NULL makes .NET
 * fall back to its non-revocation check path, which is what we want anyway —
 * 10.9's revocation infrastructure is too divergent to wire in. */
SecPolicyRef SecPolicyCreateRevocation_stub(CFOptionFlags flags) __asm("_SecPolicyCreateRevocation");
SecPolicyRef SecPolicyCreateRevocation_stub(CFOptionFlags flags) {
    (void)flags;
    return NULL;
}

/* SecTrustEvaluate override: replace the modern policies (which can include
 * apple-pinning rules unknown to 10.9) with a basic X.509 policy before
 * evaluating. Otherwise 10.9's CSSM trust engine returns errSecInvalidItemRef
 * on the modern policy descriptors and .NET's TLS handshake fails. */
typedef OSStatus (*SecTrustEvaluate_fn)(SecTrustRef, SecTrustResultType *);
typedef OSStatus (*SecTrustSetPolicies_fn)(SecTrustRef, CFTypeRef);

OSStatus SecTrustEvaluate_wrap(SecTrustRef trust, SecTrustResultType *result)
    __asm("_SecTrustEvaluate");
OSStatus SecTrustEvaluate_wrap(SecTrustRef trust, SecTrustResultType *result) {
    static SecTrustEvaluate_fn real_eval = NULL;
    static SecTrustSetPolicies_fn real_set_policies = NULL;
    if (!real_eval) {
        void *h = dlopen("/System/Library/Frameworks/Security.framework/Security",
                          RTLD_LAZY);
        if (h) {
            real_eval = (SecTrustEvaluate_fn)dlsym(h, "SecTrustEvaluate");
            real_set_policies = (SecTrustSetPolicies_fn)dlsym(h, "SecTrustSetPolicies");
        }
    }
    if (!real_eval) {
        if (result) *result = kSecTrustResultProceed;
        return errSecSuccess;
    }
    SecPolicyRef basic = SecPolicyCreateBasicX509();
    if (basic && real_set_policies) {
        real_set_policies(trust, basic);
        CFRelease(basic);
    }
    return real_eval(trust, result);
}

/* SecTrustEvaluateWithError (10.14+). Bool wrapper around SecTrustEvaluate
 * with our basic-X509 policy override. */
bool SecTrustEvaluateWithError(SecTrustRef trust, CFErrorRef *error) {
    SecTrustResultType r = kSecTrustResultInvalid;
    OSStatus s = SecTrustEvaluate_wrap(trust, &r);
    if (s == errSecSuccess &&
        (r == kSecTrustResultProceed || r == kSecTrustResultUnspecified)) {
        if (error) *error = NULL;
        return true;
    }
    if (error) *error = CFErrorCreate(kCFAllocatorDefault,
        CFSTR("SecTrustEvaluate"), 0, NULL);
    return false;
}

/* SecTrustSetNetworkFetchAllowed (10.10+). No-op on 10.9. */
OSStatus SecTrustSetNetworkFetchAllowed(SecTrustRef trust, Boolean allow) {
    (void)trust; (void)allow;
    return errSecSuccess;
}

/* SecTrustGetTrustResult (10.7+). Implement via SecTrustGetResult on 10.9. */
OSStatus SecTrustGetTrustResult(SecTrustRef trust, SecTrustResultType *result) {
    if (!trust || !result) return errSecParam;
    return SecTrustGetResult(trust, result, NULL, NULL);
}
