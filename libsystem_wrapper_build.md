# libSystemWrapper build recipe

Builds a `libSystemWrapper.dylib` that re-exports 10.9's `libSystem.B.dylib`
while adding the modern libSystem symbols a 10.14+-SDK binary expects. Every
one of those additions now comes from a **single source of truth** —
`libMavericksLegacySupport.a` (see `mavericks-legacy-support/`). That library
provides both the MacPorts-derived gap-fillers (`clock_gettime`, `getentropy`,
`utimensat`, …) and the custom libSystem shims that used to live in the
standalone `modern_api_polyfills.c` (`__ulock_wait`, `kevent64`, the `dlopen`
`.node` rewriter, the Terminal underline `write` shim, `posix_spawn` chdir, …),
one shim per `src/*.c`.

> Historical note: there is no longer a separate `modern_api_polyfills.c` and no
> `libMacportsLegacySupport.a`. Both were folded into `mavericks-legacy-support`.
> If a binary reports a missing libSystem symbol, add it *there* (see that repo's
> README, "How to add a polyfill"), rebuild the archive, and relink this wrapper —
> do not reintroduce an inline shim file.

The wrapper sits between the target binary and 10.9's libSystem: the binary has
its `LC_LOAD_DYLIB` for `/usr/lib/libSystem.B.dylib` rewritten to
`@loader_path/libSystemWrapper.dylib` (see `change_dylib.c`), and the wrapper
re-exports libSystem plus the library's additions.

## Prerequisites

- `libMavericksLegacySupport.a` — build it from `mavericks-legacy-support/`:

  ```sh
  cd mavericks-legacy-support && make      # -> lib/libMavericksLegacySupport.a
  ```

  Its coverage must satisfy every libSystem-expected symbol the target binary
  imports; if not, add the missing polyfill to that library and rebuild.

## Link

```sh
LEGACY_STATIC="$PWD/mavericks-legacy-support/lib/libMavericksLegacySupport.a"

clang -dynamiclib -o libSystemWrapper.dylib \
    -Wl,-reexport_library,/usr/lib/libSystem.B.dylib \
    -Wl,-force_load,"$LEGACY_STATIC" \
    -install_name "@loader_path/libSystemWrapper.dylib" \
    -compatibility_version 1.0.0 -current_version 1356.0.0 \
    -framework CoreFoundation -framework Security \
    -framework CoreVideo -framework CoreGraphics \
    -framework CoreServices \
    -lobjc \
    -Wno-deprecated-declarations
```

Non-obvious bits:

- **No `.c` inputs.** All added symbols come from the force-loaded archive; the
  wrapper is pure link glue (reexport + archive + frameworks).
- `-reexport_library` on `/usr/lib/libSystem.B.dylib` — the wrapper forwards
  every libSystem symbol unchanged, so symbols the binary tags "(from libS)" but
  that already exist on 10.9 (e.g. `___exp10`) resolve transitively through this
  reexport trie without an explicit polyfill.
- `-force_load` on the whole archive — pulls in *every* member so all polyfills
  land in the wrapper's export table. This is also what activates the
  behavioral-override shims (`write`, `ioctl`, `kevent64`, `socket`, `connect`,
  `dlopen`): they override symbols that already exist on 10.9, so an ordinary
  consumer that links the archive *without* `-force_load` leaves them dormant,
  but this wrapper deliberately force-loads and thus interposes them.
- `-framework CoreServices` — required because the archive's `launchservices.o`
  (the 10.10 `LSCopyDefaultApplicationURLForURL` back-fill) references
  `LSCopyDefaultHandlerForURLScheme` / `LSFindApplicationForInfo`. The other
  frameworks (CoreFoundation, Security, CoreVideo, CoreGraphics, libobjc) satisfy
  the Security/ObjC-runtime custom polyfills.
- `current_version 1356.0.0` — matches libSystem's version on a recent macOS.
  Some binaries check this via `dlopen`-time version compare; setting it low
  causes dyld to refuse the load.

## Verify

```sh
otool -L libSystemWrapper.dylib                 # libSystem.B reexported + frameworks
nm -g libSystemWrapper.dylib | grep _os_log_create   # sample polyfill present
nm -g libSystemWrapper.dylib | grep ___ulock_wait    # crash symbol present
```

If the target binary still reports missing symbols at load time, add them to
`mavericks-legacy-support` and rebuild the archive.
`DYLD_PRINT_APIS=1 DYLD_PRINT_BINDINGS=1` on the target binary reveals the
unresolved names.

## Companion: libc++ / libc++abi pairing

`libSystemWrapper` alone doesn't cover modern C++ ABI — the target binary
usually also needs modern libc++ and libc++abi (from LLVM 22's libcxx
build, or equivalent) dropped next to it at `@loader_path`. Two quirks
worth noting when repackaging those:

- Promote `libc++.1.dylib`'s `LC_LOAD_DYLIB` on `libc++abi.1.dylib` to
  `LC_REEXPORT_DYLIB` (via `change_dylib -reexport`). Exception-related
  symbols (`runtime_error`, `bad_alloc`) are declared in `libc++` headers
  but defined in `libc++abi`; without the reexport dyld can't resolve
  them when the binary binds against `libc++.1.dylib`.
- `libc++abi.1.dylib` from an LLVM build carries a self-referential
  `@rpath/libc++abi.1.dylib` `LC_LOAD_DYLIB`. Don't delete it (bind opcode
  library ordinals would shift). Don't point it at itself via
  `@loader_path` (dyld deadlocks finding itself before it's fully loaded).
  Point it at `@loader_path/libc++.1.dylib` — a real sibling file — and
  the resulting cycle is one dyld resolves fine.
