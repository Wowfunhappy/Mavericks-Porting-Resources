# libSystemWrapper build recipe

Builds a `libSystemWrapper.dylib` that re-exports 10.9's `libSystem.B.dylib`
while adding the modern libSystem symbols a 10.14+-SDK binary expects —
drawn from MacPorts Legacy Support for everything it covers, and from
`modern_api_polyfills.c` for the rest.

The wrapper sits between the target binary and 10.9's libSystem: the binary
has its `LC_LOAD_DYLIB` for `/usr/lib/libSystem.B.dylib` rewritten to
`@loader_path/libSystemWrapper.dylib` (see `change_dylib.c`), and the
wrapper re-exports libSystem plus its own additions.

## Prerequisites

- `modern_api_polyfills.c` (in this folder) — the stubs and shims.
- `libMacportsLegacySupport.a` — built from `macports/macports-legacy-support` (GitHub). Provides 10.10+ libSystem additions missing on 10.9 (e.g. `clock_gettime`, `getentropy`, `utimensat`).

## Link

```sh
clang -dynamiclib -o libSystemWrapper.dylib \
    modern_api_polyfills.c \
    -Wl,-reexport_library,/usr/lib/libSystem.B.dylib \
    -Wl,-force_load,"$LEGACY_STATIC" \
    -install_name "@loader_path/libSystemWrapper.dylib" \
    -compatibility_version 1.0.0 -current_version 1351.0.0 \
    -framework CoreFoundation -framework Security \
    -framework CoreVideo -framework CoreGraphics \
    -lobjc \
    -Wno-deprecated-declarations
```

Non-obvious bits:

- `-reexport_library` on `/usr/lib/libSystem.B.dylib` — the wrapper forwards every libSystem symbol unchanged. Without this the target binary would need all of libSystem re-stubbed.
- `-force_load` on `libMacportsLegacySupport.a` — pulls in the legacy-support archive wholesale so its symbols land in the wrapper's export table. Its coverage + `modern_api_polyfills.c` must together satisfy every libSystem-expected symbol the target binary imports.
- `current_version 1351.0.0` — matches libSystem's version on a recent macOS. Some binaries check this via `dlopen`-time version compare; setting it low causes dyld to refuse the load.
- Frameworks + `-lobjc` — `modern_api_polyfills.c`'s posix_spawn chdir shim and a few other polyfills call into CoreFoundation / Security / Objective-C runtime.

## Verify

```sh
otool -L libSystemWrapper.dylib                 # libSystem.B reexported + frameworks
nm -g libSystemWrapper.dylib | grep _mkostemp   # sample polyfill present
```

If the target binary still reports missing symbols at load time, they need
to be added to `modern_api_polyfills.c` (or covered by a newer
libMacportsLegacySupport). `DYLD_PRINT_APIS=1 DYLD_PRINT_BINDINGS=1` on the
target binary reveals the unresolved names.

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
