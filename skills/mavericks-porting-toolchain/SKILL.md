---
name: mavericks-porting-toolchain
description: Use when making a modern-SDK macOS x86_64 binary (recent 14.x+ SDK, statically-linked runtime, single-file executable like Bun/Zig/Rust/.NET NativeAOT outputs) launch or run on OS X 10.9 Mavericks. Triggers on SIGILL at launch, dyld "can't parse the binary", "Symbol not found" for libSystem, crashes in early runtime init before main, or any "this runs on modern macOS but not 10.9" failure. Also when adding a reusable porting artifact back to this repo.
---

# Mavericks Porting Toolchain

This repo is a kit of reusable resources for getting modern-SDK macOS binaries to launch and run on OS X 10.9 Mavericks (x86_64). Map the failure symptom to a stage, then use the named resource. **Read the referenced `.md` recipe and the source file before acting** — they carry the non-obvious details (load-command growth, reexport ordinals, link order, duplicate-symbol quirks) that make the difference.

## Symptom → resource map

| Symptom | What's actually wrong | Primary resource(s) |
|---|---|---|
| `SIGILL` on the first few instructions | CPU lacks AVX2/FMA3/BMI that a 14.x build emits | `avxemu/` — trap-and-emulate layer; load via `DYLD_INSERT_LIBRARIES` only on pre-Haswell (AVX1) CPUs |
| dyld "can't parse the binary" / load fails | Mach-O uses chained fixups & `LC_BUILD_VERSION` that 10.9 dyld can't read | `patch_macho.c` → then `add_version_min.c`; `fix_macho.c` for fat binaries / strip-only |
| "Symbol not found" for libSystem (or framework) symbols | 14.x-SDK binary imports symbols absent on 10.9 | `change_dylib.c` to redirect to a wrapper + `libsystem_wrapper_build.md` + grow `modern_api_polyfills.c`; per-framework wrappers via `build_wrappers.sh` |
| .NET NativeAOT NULL-deref before `main` | C++ static ctors in `__TEXT,__init_offsets` never ran (10.9 dyld ignores the dyld4-era section) | `init_runner.c`; full chain in `dotnet_aot_recipe.md` + `dotnet_polyfills.c` + `security_wrapper_stubs.c` |
| `Intl.*` / break-iteration / ICU errors | binary wants modern ICU; 10.9's libicucore is old | `icu_wrapper_build.md` (real static ICU 78, unversioned ABI) |
| Mystery hang / wrong behavior / TLS handshake fails | missing or changed syscall/framework behavior | `syscall_trace.c` to diagnose, then grow the relevant wrapper/polyfill |

## Order of operations

Run only the stages a binary needs — the symptom map says which. One binary usually hits several stages at once, and the stages are cumulative in this order: a `SIGILL` (stage 4) or a "can't parse" (stage 1) blocks you from even observing the symbol/init failures behind it, so clear earlier stages before judging later ones. A modern single-file binary almost always needs stages 1–2 before any stage-3 runtime fix is reachable.

0. **Triage.** `otool -l` (look for `LC_DYLD_CHAINED_FIXUPS`, `LC_BUILD_VERSION`, `LC_DYLD_EXPORTS_TRIE`, `__TEXT,__init_offsets`); `otool -L` (deps to wrap); `sysctl machdep.cpu.leaf7_features` (is AVX2 present on the target?).
1. **Mach-O reshape.** `patch_macho.c` (chained fixups → `LC_DYLD_INFO_ONLY`), then `add_version_min.c` (re-add `LC_VERSION_MIN_MACOSX 10.9` — patch_macho strips the platform load command and 10.9 dyld needs the signal back). `fix_macho.c` for fat binaries or strip-only.
2. **Library redirection + wrappers.** `change_dylib.c` to repoint each `LC_LOAD_DYLIB` at a `@loader_path/…Wrapper.dylib`. Build wrappers per `libsystem_wrapper_build.md` / `icu_wrapper_build.md`, using `build_wrappers.sh` as the worked template. For any still-unresolved symbol, add it to `modern_api_polyfills.c` (`DYLD_PRINT_APIS=1 DYLD_PRINT_BINDINGS=1` reveals names).
3. **Runtime / init fixups (.NET etc.).** `init_runner.c` (runs `__init_offsets` ctors and fixes up `__objc_selrefs`), `dotnet_polyfills.c`, `security_wrapper_stubs.c`; follow `dotnet_aot_recipe.md` for link-order / duplicate-symbol quirks.
4. **CPU emulation (SIGILL).** Build `avxemu/` (`./build.sh` → `libavxemu.dylib`), self-test on the target (`AVXEMU_SELFTEST=1`), then load via `DYLD_INSERT_LIBRARIES` only on AVX1 CPUs (`AVXEMU_DISABLE=1` bypasses). Floor is AVX1 (Sandy/Ivy Bridge); no-AVX targets are out of scope.
5. **Iterate.** `syscall_trace.c` to find where a still-broken binary diverges; feed fixes back into the wrappers/polyfills.

## Inventory

**Mach-O tools**
- `patch_macho.c` — chained fixups (macOS 12+) → traditional `LC_DYLD_INFO_ONLY`; also handles `LC_BUILD_VERSION`.
- `add_version_min.c` — append `LC_VERSION_MIN_MACOSX 10.9` after reshape.
- `fix_macho.c` — `-change` dylib paths and `-strip_build_version`; thin + fat (universal).
- `change_dylib.c` — rewrite/grow/delete `LC_LOAD_DYLIB`; `-reexport` promotion.

**Wrapper recipes / builders**
- `build_wrappers.sh` — worked end-to-end example building framework wrappers (CoreGraphics, QuartzCore, Metal, libSystem, libc++, libobjc…) and applying the `change_dylib` redirects. App-specific; use as a template.
- `libsystem_wrapper_build.md` — build `libSystemWrapper.dylib` (reexport 10.9 libSystem + force_load MacPorts Legacy Support + `modern_api_polyfills.c`).
- `icu_wrapper_build.md` — build `libicucoreWrapper.dylib` from static ICU 78 (`--disable-renaming` to match Apple's unversioned ABI).
- `dotnet_aot_recipe.md` — full .NET 9 NativeAOT / SingleFile porting playbook.

**Polyfill / stub sources**
- `modern_api_polyfills.c` — large catalog of modern libSystem shims (ulock/futex, mkostemp, posix_spawn chdir, kevent64 error-events, terminal SGR fix, `.node` dlopen interposer). The file you grow when a symbol is unresolved.
- `dotnet_polyfills.c` — .NET-specific pokes: `sysconf(_SC_PHYS_PAGES)`, synthetic `kern.memorystatus_level`, `vm_remap` `VM_FLAGS_RANDOM_ADDR` strip for W^X JIT setup.
- `security_wrapper_stubs.c` — Security framework: `SecCertificateCopyKey` (via 10.9's `SecCertificateCopyPublicKey`), `SecKey*` family stubs, SSL ALPN stubs.
- `init_runner.c` — runs `__TEXT,__init_offsets` ctors + fixes `__objc_selrefs`, from a `constructor(101)` in libSystemWrapper.
- `MetalStub.m` — stub Metal.framework (rendering falls back to OpenGL via ANGLE).
- `macos_compat.h` / `macos_compat_impl.mm` — compile-time compat shims for building from source (screen-capture stubs, `NSOperatingSystemVersion`, `API_AVAILABLE` no-ops).
- `legacy-swift-stubs/` — libswiftCore / Foundation / CryptoKit / extras stubs + Makefile.
- `compat_headers/` — headers absent from the 10.9 SDK: `os/lock.h`, `os/log.h`, `sys/random.h`, `UniformTypeIdentifiers/`, `CoreHaptics/`.

**Diagnostics**
- `syscall_trace.c` — `DYLD_INSERT_LIBRARIES` syscall tracer (sockets / kevent / read-write / ioctl / termios) for when dtrace isn't available.

**CPU emulation**
- `avxemu/` — trap-and-emulate + in-memory-rewrite layer running AVX2/FMA/BMI binaries on AVX1 (Sandy/Ivy Bridge). `./build.sh` builds, tests, and emits `libavxemu.dylib`. Env: `AVXEMU_SELFTEST`, `AVXEMU_DISABLE`, `AVXEMU_FORCEPATCH`/`AVXEMU_FORCETRAMP` (dev/test). See `avxemu/README.md`.

## Related skill

`mavericks-compatibility-lore` — corrections to Mavericks behavior that is surprising or contradicts common/modern-macOS guidance. It's currently seeded with kernel-extension and code-signing examples, but its scope is any wrong-for-10.9 folklore, not just kexts. Consult it whenever a 10.9 behavior surprises you during porting — and add new surprises to it (see `CLAUDE.md`).
