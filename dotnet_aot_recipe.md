# Porting a .NET 9 self-contained binary to macOS 10.9

A SingleFile/NativeAOT-published .NET app for `osx-x64` (e.g.
DepotDownloader, DotNet tools) is essentially a Mach-O apphost statically
linked against coreclr + a bundle of `.dll` payloads. Getting one to start on
10.9 needs three categories of work:

## 1. Mach-O reshape

Same as any modern binary: `patch_macho` (chained fixups → LC_DYLD_INFO_ONLY)
+ `add_version_min` (LC_VERSION_MIN_MACOSX 10.9). dyld 1 in 10.9 doesn't
parse LC_DYLD_CHAINED_FIXUPS / LC_BUILD_VERSION; both tools elsewhere in this
repo handle that.

## 2. Library wrappers

The binary declares dependencies on standard frameworks plus `libCryptoKit`
and Swift overlays. Redirect each via `change_dylib`:

| Original                             | Wrapper                                           |
|--------------------------------------|---------------------------------------------------|
| `/usr/lib/libSystem.B.dylib`         | `libSystemWrapper.dylib` (re-exports + polyfills) |
| `/usr/lib/libobjc.A.dylib`           | `libobjcWrapper.dylib` (adds `objc_alloc_init`)   |
| `/usr/lib/libc++.1.dylib`            | `libcxxWrapper.dylib` (re-exports + iostream stubs)|
| `Security.framework`                 | `libSecurityWrapper.dylib` (see below)            |
| `CryptoKit.framework`                | `libCryptoKit.dylib` stub (`legacy-swift-stubs/`)  |
| `libswiftCore.dylib` & overlays      | `legacy-swift-stubs/libswift*.dylib` stubs         |

The `legacy-swift-stubs/` and `libsystem_wrapper_build.md` recipes already
cover most of the work. Two new wrappers are needed for .NET specifically:

### libSystemWrapper additions (`dotnet_polyfills.c`)

`modern_api_polyfills.c` covers most of what modern macOS binaries expect,
but .NET pokes a few specific 10.9-missing facilities that aren't in there:

- `sysconf(_SC_PHYS_PAGES)` — Linux-only on macOS. .NET's
  `GCToOSInterface::Initialize` returns failure if it gets `-1`; emulate
  via `hw.memsize / page_size`.
- `sysctlnametomib("kern.memorystatus_level")` — sysctl doesn't exist on 10.9;
  return a sentinel MIB and have the matching `sysctl()` interposer report
  "no memory pressure" (`100`).
- `vm_remap` with `VM_FLAGS_RANDOM_ADDR` (0x8) — 10.9's kernel rejects the
  flag with `KERN_INVALID_ARGUMENT`. Strip it. Also widen `max_protection`
  on the remapped region so the caller's follow-up `mprotect(RW)` succeeds
  — the W^X JIT pipeline depends on this.
- `preadv` / `pwritev` — added in 10.10; emulate via `pread`/`pwrite` loops.

### libSecurityWrapper (`security_wrapper_stubs.c`)

.NET 9 uses Apple Security framework directly for X.509 trust on macOS.
Three categories of stubs are needed:

1. **`SecCertificateCopyKey` (10.14+)** → call 10.9's
   `SecCertificateCopyPublicKey` and unwrap the OSStatus.
2. **`SecTrustEvaluate` override** — replace the modern policies (which can
   include Apple-pinning rules that 10.9's CSSM trust engine returns
   `errSecInvalidItemRef` on) with a basic X.509 policy before evaluating.
   Without this, every TLS handshake fails with "The specified item is no
   longer valid. It may have been deleted from the keychain." `SecTrustEvaluateWithError` (10.14+) wraps it.
3. **Modern unified `SecKey*` API** — `SecKeyCopyAttributes`,
   `SecKeyVerifySignature`, etc., plus the `kSecKeyAlgorithm*` constants.
   Most can return NULL/false; .NET's TLS path only requires the trust
   evaluator and `SecCertificateCopyKey` to succeed.

The wrapper must be a **re-export** of `Security.framework` (use
`change_dylib -reexport` after linking, since the linker emits LC_LOAD_DYLIB
for `-framework`). Otherwise the binary's bind ordinals — pinned to "Security"
when patch_macho rewrites the chained fixups — won't see your stubs.

## 3. C++ static-init runner & ObjC selref fixup (`init_runner.c`)

Modern Mach-O binaries put C++ static-constructor function pointers in a new
`__TEXT,__init_offsets` section (4-byte image-relative offsets) instead of
the classic `__DATA,__mod_init_func` (8-byte absolute pointers). 10.9's dyld
ignores the new section; the constructors never run, and `CObjectType`
globals like `otThread(...)` end up zero-initialised. .NET PAL then reads
`m_pProcessLocalDataSize=0`, the lazy-allocate path returns NULL, and
`CreateThreadObject` segfaults dereferencing it.

`init_runner.c` (in this repo) walks `__init_offsets` and invokes each entry
itself, from a `constructor(101)` in libSystemWrapper that fires before
main. While walking the binary's segments it also iterates `__objc_selrefs`
and rewrites each entry from a c-string pointer to `sel_registerName(...)` —
10.9's libobjc skips that for modern image-info flags, so without the manual
fixup `objc_msgSend` dispatches with raw string pointers and the runtime
errors out with "selector does not match".

## 4. Quirks

- The legacy support archive defines its own `_sysconf` and a NULL-returning
  `_SecCertificateCopyKey`. Either omit `sysconf.o` from `-force_load` (extract
  the archive, `rm sysconf.o sysconf.dl.o`, repack) or strip individual
  symbols via `ld -r -unexported_symbol`. Otherwise duplicate-symbol errors
  block the link.
- A `_force_load` Mach-O archive's symbols don't override identically-named
  symbols in your wrapper's own `.c` files — but they DO win over symbols
  resolved through dyld at runtime when the archive is linked into the same
  dylib. Resolve overrides by stripping at link time, not linking order.
