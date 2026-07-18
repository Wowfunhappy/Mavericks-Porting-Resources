# MavericksLegacySupport

A library that back-fills functions, types, and macros that newer macOS
releases added but **OS X 10.9 Mavericks** lacks, so that modern software can
build and run on Mavericks.

It is a stripped-down spin of MacPorts'
[`macports-legacy-support`](https://github.com/macports/macports-legacy-support).
**The polyfill code is copied verbatim from MacPorts** — none of it is
reinvented here. What has been removed is the *plumbing* that lets the upstream
project target every OS from 10.4 to the present:

* No SDK-version detection (`sdkversion.h`, the `AvailabilityMacros` hacks).
* No target-version detection.
* No two-flag (`__MPLS_SDK_*` / `__MPLS_LIB_*`) gate on every feature.
* No code paths for any OS other than 10.9.

Because we target exactly one OS, every one of those version conditionals has a
constant value, so the sources and wrapper headers were run through `unifdef`
to delete the dead branches, leaving just the 10.9 implementation. The result
is the same battle-tested MacPorts code, minus the multi-OS scaffolding.

The trade-off: it is correct **only** when built on/for 10.9 (Intel, i386 or
x86_64). That is the point.

## What's included

Everything from `macports-legacy-support` that is actually missing on 10.9.

**Header-only additions (no library code):**

| Header | Adds |
|--------|------|
| `assert.h` | `static_assert` |
| `sys/mman.h` | `MAP_ANONYMOUS` |
| `sys/attr.h` | `VOL_CAP_INT_CLONE` |
| `sys/fcntl.h` | `O_CLOEXEC` fallback, `AT_FDCWD` / `AT_*` constants |
| `mach/machine.h` | `CPU_TYPE_ARM`, `CPU_SUBTYPE_ARM64E` |

**Library functions:**

| Area | Functions |
|------|-----------|
| `time.h` | `clock_gettime`, `clock_getres`, `clock_settime`, `clock_gettime_nsec_np`, `timespec_get`, `clockid_t`/`CLOCK_*` |
| `mach/mach_time.h` | `mach_approximate_time`, `mach_continuous_time`, `mach_continuous_approximate_time` |
| `sys/random.h` | `getentropy` |
| `os/lock.h` | `os_unfair_lock` + `os_unfair_lock_lock`/`_trylock`/`_unlock` |
| `dirent.h` | `fdopendir` (incl. `$INODE64` / `$UNIX2003` variants) |
| `sys/fcntl.h` | `openat` |
| `sys/unistd.h` | `faccessat`, `fchownat`, `linkat`, `readlinkat`, `symlinkat`, `unlinkat`, `getattrlistat`, `setattrlistat` |
| `sys/stat.h` | `fstatat`, `fchmodat`, `mkdirat`, `utimensat`, `futimens` (`UTIME_NOW`/`UTIME_OMIT`) |
| `sys/stdio.h` | `renameat` |
| `sys/fsgetpath.h` | `fsgetpath` |
| `sys/clonefile.h` | `clonefile`, `clonefileat`, `fclonefileat` (fail with `ENOTSUP` — no APFS on 10.9) |
| `stdio.h` | `fmemopen`, `open_memstream` |
| `pthread.h` | `pthread_chdir_np`, `pthread_fchdir_np` (opt-in, see below) |
| `unistd.h` | `sysconf(_SC_PHYS_PAGES)` |
| (`pthread.h`) | bug-fix override for `pthread_get_stacksize_np` (returns the wrong size for the main thread on 10.9) |

The `*at` calls are implemented as MacPorts does — by `fchdir`-ing into the
directory fd around the underlying call — because the `*at` *syscalls* do not
exist on the 10.9 kernel.

### Custom (non-MacPorts) polyfills

Some symbols modern binaries import are missing on 10.9 beyond what upstream
MacPorts covers (Security/ObjC-runtime/libnotify/compiler-rt entry points).
Those live in their own per-area sources — e.g. `src/security.c`
(`SecTrustEvaluateWithError`, `SecTrustCopyCertificateChain`, and a
`SecTrustEvaluate` override that drops the revocation policy which crashes on
10.9), `src/objc_runtime.c`, `src/os_version.c` (`@available`), `src/jit.c`,
`src/os_log.c`, `src/notify.c`, `src/aligned_alloc.c`, etc. The Security/ObjC
ones pull in **Security**, **CoreFoundation**, and **libobjc**, which the
Makefile links into the dylibs (`WFHLIBS`); static-lib consumers must link
those frameworks themselves.

This group also absorbed the libSystem shims that used to live in a standalone
`modern_api_polyfills.c` (built for the Claude Code / Bun port). They share the
`src/mav_shim_debug.h` helper (runtime tracing via `MAV_PATCH_DEBUG=1`) and fall
into two kinds:

* **Gap-fillers** — provide a symbol 10.9 lacks entirely: `src/ulock.c`
  (`__ulock_wait{,2}`/`__ulock_wake`), `src/mkostemp.c`,
  `src/posix_spawn_chdir.c`, `src/msg_x.c` (`recvmsg_x`/`sendmsg_x`),
  `src/preadv_pwritev_nocancel.c`, `src/renameatx_np.c`, `src/timingsafe_bcmp.c`,
  `src/fd_set_overflow.c`, `src/chk_fail.c`, `src/signpost.c`,
  `src/os_unfair_lock_assert.c`, `src/pthread_self_is_exiting.c`.
* **Behavioral overrides** — replace a symbol that *does* exist on 10.9 to fix
  broken/absent modern behavior: `src/kevent64_shim.c` (modern kqueue
  `KEVENT_FLAG_ERROR_EVENTS` / `EVFILT_MACHPORT` semantics, plus `socket`/
  `connect` trace hooks), `src/dlopen_interpose.c` (rewrites `.node`
  add-ons' libSystem `LC_LOAD_DYLIB` to this wrapper), `src/write_underline.c`
  (cancels a 10.9 Terminal underline misparse), `src/ioctl_winsize.c`
  (`TIOCGWINSZ` fd-0 fallback). Because these override existing symbols, a
  consumer that plain-links the archive won't pull them (nothing is undefined);
  they activate only under `-force_load` (or the `system-lib` dylib), which is
  exactly how `libSystemWrapper` interposes them.

## Building & installing

```sh
make                 # builds lib/libMavericksLegacySupport.{a,dylib}
make test            # builds + runs the functional test on real 10.9
make install         # installs under /usr/local (override with PREFIX=...)
make system-lib      # optional libSystem-reexporting drop-in (see below)
make ARCHS="x86_64 i386"   # fat build
```

Headers install to `$PREFIX/include/MavericksLegacySupport/`; libraries to
`$PREFIX/lib`.

## Using it

Put our include directory **ahead** of the system one so our wrappers shadow
the system headers (each then chains back to the real header via
`#include_next`), and link the library:

```sh
cc -I$PREFIX/include/MavericksLegacySupport myprog.c \
   -L$PREFIX/lib -lMavericksLegacySupport -o myprog
```

Your code just uses the standard names:

```c
#include <time.h>
#include <sys/random.h>
#include <fcntl.h>

struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);          /* provided by us on 10.9 */
unsigned char key[32];
getentropy(key, sizeof key);                  /* provided by us on 10.9 */
int fd = openat(dirfd, "f", O_RDONLY);        /* provided by us on 10.9 */
```

`pthread_chdir_np` / `pthread_fchdir_np` are gated behind
`-D_MACPORTS_LEGACY_PTHREAD_CHDIR=1` (matching upstream), since other SDKs/OSes
declare them.

### The `system-lib` drop-in

`make system-lib` also builds `libMavericksLegacySystem.B.dylib`, which exports
our additions **and** re-exports all of `/usr/lib/libSystem.B.dylib`, so it can
stand in for libSystem — e.g. inject it into an existing binary:

```sh
DYLD_INSERT_LIBRARIES=$PREFIX/lib/libMavericksLegacySystem.B.dylib ./modern_binary
```

## How to add a polyfill

The whole point of this layout is that it tracks upstream. To add another
function that 10.9 is missing, port it from `macports-legacy-support` rather
than writing your own:

1. **Copy the files.** Grab the relevant `src/<name>.c` (and any wrapper header
   under `include/`) from `macports-legacy-support`, verbatim.

2. **De-gate them.** Strip the MacPorts plumbing so only the 10.9 path remains:
   * Replace `#include "MacportsLegacySupport.h"` with `#include "LegacySupport.h"`
     and delete any `#include <_macports_extras/sdkversion.h>`.
   * Delete the version `#if`s. The mechanical way is `unifdef`, defining each
     `__MPLS_*` flag to the value its expression takes for target/SDK 1090
     (e.g. `__MPLS_LIB_SUPPORT_FOO__=1`, `__MPLS_LIB_FIX_TIGER_PPC64__=0`).
     Leave `__MPLS_64BIT` and `__MPLS_HAVE_STAT64` undefined — they are genuine
     compile-time conditions (`LegacySupport.h` / `sys/stat.h` define them).
   * `LegacySupport.h` is the slim replacement for `MacportsLegacySupport.h`:
     it provides `__MP__BEGIN_DECLS`, `__MPLS_64BIT`, and the fixed
     `__MPLS_TARGET_OSVER`/`__MPLS_SDK_MAJOR` (= 1090) constants.

3. **Drop them in.** `src/*.c` is compiled automatically and the whole
   `include/` tree is installed as-is, so neither the Makefile nor any manifest
   needs editing. Add a check to `test/test_polyfills.c` to cover it.

A purely header-only addition (a macro or type, no code) is just step 1–3 on a
single wrapper header — see `sys/mman.h` for the minimal shape.

### Notes & gotchas

* **`#include_next`** is what lets a wrapper add to a header instead of
  replacing it; it works because our `-I` dir is searched before `/usr/include`.
* **Headers Apple added wholesale** (`os/lock.h`) don't exist on 10.9, so their
  wrappers have no `#include_next`.
* The internal helpers `src/compiler.h`, `src/util.h`, `src/atcalls.h`, and
  `src/dirfuncs_compat.{c,h}` are MacPorts support files, de-gated the same way.

## Where this library is used (provenance)

**This repo is the single source of truth for 10.9 polyfills.** If you hit a
missing symbol anywhere, add it *here* — do not write another inline shim
elsewhere, or the symbol ends up defined in two places.

It is consumed two ways:

1. **Injection** into a *prebuilt* binary you didn't compile — load
   `libMavericksLegacySupport.dylib` (or the libSystem-reexporting
   `…System.B.dylib`) with `DYLD_FORCE_FLAT_NAMESPACE=1` +
   `DYLD_INSERT_LIBRARIES=…`.
2. **Static link** into things you compile with the **clang-22 / Rust toolchain**
   in `/Users/Jonathan/Developer/Compilers`. A copy of `libMavericksLegacySupport.a`
   is *vendored* into `toolchains/clang-22/lib/` and auto-linked by that clang's
   `clang.cfg` (it supersedes the old `libMacportsLegacySupport.a`, `libpolyfill.a`,
   and `rust/shim/libmpls_extras.a`, which are retired).

After adding a polyfill, refresh the vendored copy:

```sh
cd "/Users/Jonathan/Developer/Mavericks Porting Resources/mavericks-legacy-support"
make
cp lib/libMavericksLegacySupport.a /Users/Jonathan/Developer/Compilers/toolchains/clang-22/lib/
```

A link-time symbol only needs the archive refreshed. If the *source being
compiled* also needs the **declaration** (the 10.9 SDK headers won't have it for
a newer API), add it to the toolchain's force-include header
`toolchains/clang-22/include/macos10_9_compat.h` (or, for Rust, `rust/shim/include`).

## License

ISC-style (`LICENSE`). The polyfill sources and wrapper headers are derived
from `macports-legacy-support` and retain their original copyright notices and
terms; some carry the Apple Public Source License, as noted in the files.
