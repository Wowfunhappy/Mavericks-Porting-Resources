# libicucoreWrapper build recipe

Builds a `libicucoreWrapper.dylib` that provides modern ICU symbols (for
Intl.*, break iteration, etc.) while remaining ABI-compatible with Apple's
unversioned `libicucore`. Pick this over stub libraries that return
`U_UNSUPPORTED_ERROR` for missing APIs — real ICU is always better: stubs
give you runtime errors the host binary wasn't prepared to see, while a
proper build just works.

Two steps: build ICU 78 statically, then link a dylib that force-loads it.

## 1. Build ICU 78 statically

Source: upstream `unicode-org/icu` tag `release-78-*`, or `firefox-dynasty/intl/icu/source` which tracks upstream.

Key configure flags — the non-obvious ones:

- `--disable-renaming` — exports unversioned `u_foo` symbols instead of `u_foo_78`, matching Apple's libicucore ABI. **Required** for the wrapper's symbols to be picked up by code linked against the system libicucore.
- `--enable-static --disable-shared` — we link statically into the wrapper dylib.
- Disable tests/samples/extras/layout to keep the build tractable.

```sh
export CC=/path/to/clang-22
export CXX=/path/to/clang++-22
export CXXFLAGS="-std=c++17 -stdlib=libc++ -mmacosx-version-min=10.9"
export CFLAGS="-mmacosx-version-min=10.9"
export LDFLAGS="-stdlib=libc++"
./runConfigureICU macOS \
    --disable-renaming \
    --disable-tests --disable-samples \
    --disable-extras --disable-layout --disable-layoutex \
    --enable-static --disable-shared \
    --prefix="$ICU_BUILD/install"
make -j6
```

Output: `lib/libicuuc.a`, `lib/libicui18n.a`, `lib/libicudata.a`.

Note: `libicudata.a` sometimes lands in `source/stubdata/` instead of `source/lib/` if pkgdata didn't run — check both. A real data lib is ~15 MB; a stub is ~1 KB.

Takes ~20–40 min on typical hardware.

## 2. Link the wrapper dylib

```sh
clang++-22 -stdlib=libc++ \
  -dynamiclib \
  -install_name @loader_path/libicucoreWrapper.dylib \
  -compatibility_version 76.1 -current_version 76.1 \
  -mmacosx-version-min=10.9 \
  -Wl,-force_load,"$ICU_BUILD/lib/libicui18n.a" \
  -Wl,-force_load,"$ICU_BUILD/lib/libicuuc.a" \
  -Wl,-force_load,"$ICU_BUILD/lib/libicudata.a" \
  -Wl,-rpath,@loader_path \
  -L"$WRAPPERS_DIR" -lc++ \
  -o libicucoreWrapper.dylib
```

Non-obvious bits:

- `-force_load` on all three static libs — without it, only referenced symbols get pulled in and the dylib won't export the full ICU surface the host binary expects.
- `-stdlib=libc++` with a modern libc++ (LLVM 22's) — ICU 78 needs C++17 ABI that 10.9's libc++ can't provide; the wrapper must be paired with a modern libc++.1.dylib at `@loader_path`.
- `compatibility_version 76.1` — matches what modern binaries expect from libicucore.

## Verify

```sh
otool -L libicucoreWrapper.dylib          # deps only @loader_path/libc++
nm -g libicucoreWrapper.dylib | grep -c ' T _u'   # should be thousands
```

Smoke-test specific modern ICU symbols the host binary expects (e.g.
`ubrk_clone`, `ulistfmt_openForType`, `unumf_openForSkeletonAndLocale`).
