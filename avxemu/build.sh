#!/bin/sh
# Build the AVX2/FMA/BMI emulator, run all differential + end-to-end tests, and
# produce the shippable libavxemu.dylib.
#
# The runtime core is SSE4.2-only with NO avx/fma codegen, so it is
# representative of (and runnable on) the target CPU. Tests are built with real
# AVX2/FMA/BMI to get ground truth on a Haswell+ host.
#
#   ./build.sh            build + test + produce $OUT/libavxemu.dylib
#   ./build.sh install    also copy the dylib into the Mavericks Forever public dir
set -e
cd "$(dirname "$0")"
CC="${CC:-clang}"
OUT="${OUT:-/tmp/avxemu}"
PUBLIC="${PUBLIC:-/Users/Jonathan/Developer/Mavericks Forever/public/claude}"
mkdir -p "$OUT"

CORE="-O2 -Wall -std=c11 -msse4.2 -mno-avx -mno-fma -Isrc"
HOSTT="-O2 -std=c11 -mavx2 -mfma -mbmi -mbmi2 -mlzcnt -mf16c -Isrc"
quiet() { grep -vE "warning:|note:|implicitly declaring|please include|^ *\^|^ *~|generated\." || true; }

echo "[1] compiling runtime core (SSE-only)..."
PURE=""                      # the emulator's hot path — must be VEX-free
for f in exec exec_bmi softfma names decode lde patch_mem tramp handler; do
    "$CC" $CORE -c src/$f.c -o "$OUT/$f.o" 2>&1 | quiet
    PURE="$PURE $OUT/$f.o"
done
"$CC" $CORE -c src/selftest.c -o "$OUT/selftest_c.o" 2>&1 | quiet
"$CC" -c src/selftest.s -o "$OUT/selftest.o"          # contains VEX in e2e stubs (inert)
"$CC" -c src/tramp.s -o "$OUT/tramp_s.o"              # trampoline thunk template (VEX save/restore)
# asm objects that must accompany the (VEX-free) core wherever it is linked
ASM="$OUT/selftest.o $OUT/selftest_c.o $OUT/tramp_s.o"

echo "[2] verifying runtime core emits no VEX (must be empty)..."
leak=$(otool -tV $PURE 2>/dev/null | awk -F'\t' '$2 ~ /^v/{print $2}' | sort -u)
[ -z "$leak" ] && echo "    clean" || { echo "    VEX LEAK: $leak"; exit 1; }

echo "[3] vector + FMA oracle..."
"$CC" $HOSTT test/oracle.c "$OUT/exec.o" "$OUT/exec_bmi.o" "$OUT/softfma.o" "$OUT/names.o" -o "$OUT/oracle" 2>&1 | quiet
"$OUT/oracle" | tail -1

echo "[4] BMI oracle..."
"$CC" $HOSTT test/bmi_oracle.c "$OUT/exec_bmi.o" "$OUT/names.o" -o "$OUT/bmi_oracle" 2>&1 | quiet
"$OUT/bmi_oracle"

echo "[5] decoder + fault-injection end-to-end..."
"$CC" -c test/stubs.s -o "$OUT/stubs.o"
"$CC" $CORE -c test/inject.c -o "$OUT/inject.o" 2>&1 | quiet
"$CC" $CORE $PURE $ASM "$OUT/stubs.o" "$OUT/inject.o" -o "$OUT/inject"
"$OUT/inject" | tail -1

echo "[6] memory addressing modes (EA computation)..."
"$CC" -c test/memtest.s -o "$OUT/memtest_s.o"
"$CC" $CORE -c test/memtest.c -o "$OUT/memtest.o" 2>&1 | quiet
"$CC" $CORE "$OUT/memtest.o" "$OUT/memtest_s.o" $PURE $ASM -o "$OUT/memtest"
"$OUT/memtest" | tail -1

echo "[6b] patched (F0/lock) lzcnt/tzcnt fault->emulate vs hardware..."
"$CC" -c test/patch_stubs.s -o "$OUT/patch_stubs.o"
"$CC" $HOSTT -c test/patchtest.c -o "$OUT/patchtest.o" 2>&1 | quiet
"$CC" $CORE "$OUT/patchtest.o" "$OUT/patch_stubs.o" $PURE $ASM -o "$OUT/patchtest"
"$OUT/patchtest" | tail -2

echo "[6c] BMI with memory operand: emulated vs native (mem_bytes / operand source)..."
"$CC" -c test/bmimem.s -o "$OUT/bmimem_s.o"
"$CC" $CORE -c test/bmimem.c -o "$OUT/bmimem.o" 2>&1 | quiet
"$CC" $CORE "$OUT/bmimem.o" "$OUT/bmimem_s.o" $PURE $ASM -o "$OUT/bmimem"
"$OUT/bmimem" | tail -1

echo "[6d] trampoline thunk: emulate + full register/flag/rsp preservation..."
"$CC" -c test/tramp_harness.s -o "$OUT/tramp_harness.o"
"$CC" $CORE -c test/tramptest.c -o "$OUT/tramptest.o" 2>&1 | quiet
"$CC" $CORE "$OUT/tramptest.o" "$OUT/tramp_harness.o" $PURE $ASM -o "$OUT/tramptest"
"$OUT/tramptest" | tail -1

echo "[6e] SIMD over-read across an unmapped page (page-safe vector load)..."
"$CC" $CORE -c test/overread.c -o "$OUT/overread.o" 2>&1 | quiet
"$CC" $CORE "$OUT/overread.o" $PURE $ASM -o "$OUT/overread"
"$OUT/overread" | tail -1

echo "[7] decoder + differential fuzzer vs the real Claude binary (if present)..."
BINARY="${CLAUDE_BIN:-$HOME/.local/share/claude/versions/2.1.166}"
if [ -f "$BINARY" ]; then
    info=$(otool -l "$BINARY" | awk '/sectname __text/{t=1} t&&/addr /{a=$2} t&&/offset /{print a, $2; t=0}')
    vm=$(echo "$info" | awk '{print $1}'); fo=$(echo "$info" | awk '{print $2}')
    otool -tV "$BINARY" 2>/dev/null | awk -F'\t' '
        NR>1 && pc { print prevaddr, prevmnem, $1 }
        { prevaddr=$1; prevmnem=$2; pc=($2 ~ /^(v|mulx|pdep|pext|bzhi|sarx|shlx|shrx|rorx|andn|bextr|blsi|blsmsk|blsr|tzcnt|lzcnt|movbe)/) }
    ' > "$OUT/cand.txt"
    "$CC" $CORE -c test/bintest.c -o "$OUT/bintest.o" 2>&1 | quiet
    "$CC" $CORE "$OUT/bintest.o" "$OUT/decode.o" "$OUT/names.o" -o "$OUT/bintest"
    "$OUT/bintest" "$BINARY" "$OUT/cand.txt" "$vm" "$fo" | grep -E "decoded|mismatch" | sed 's/^/    /'
    "$CC" -c test/fuzz.s -o "$OUT/fuzz_s.o"
    "$CC" $CORE -c test/fuzz.c -o "$OUT/fuzz.o" 2>&1 | quiet
    "$CC" $CORE "$OUT/fuzz.o" "$OUT/fuzz_s.o" $PURE $ASM -o "$OUT/fuzz"
    "$OUT/fuzz" "$BINARY" "$OUT/cand.txt" "$vm" "$fo" | grep -E "distinct|runs|mismatch" | sed 's/^/    /'

    echo "[7b] lzcnt/tzcnt patch safety + decoder coverage over the real binary..."
    "$CC" $CORE -c src/lde.c -o "$OUT/lde.o" 2>&1 | quiet   # ensure fresh
    "$CC" -O2 -std=c11 -Isrc test/patchdiff.c "$OUT/lde.o" -o "$OUT/patchdiff" 2>&1 | quiet
    "$OUT/patchdiff" "$BINARY" | sed 's/^/    /'
    "$CC" -O2 -std=c11 -Isrc test/zdecode.c "$OUT/lde.o" "$OUT/decode.o" "$OUT/names.o" -o "$OUT/zdecode" 2>&1 | quiet
    "$OUT/zdecode" "$BINARY" | sed 's/^/    /'
else
    echo "    (binary not found at $BINARY — skipping; set CLAUDE_BIN to enable)"
fi

echo "[8] self-test via the dylib path (AVXEMU_SELFTEST)..."
"$CC" -dynamiclib -O2 -msse4.2 -mno-avx -mno-fma \
    -install_name "\$HOME/.local/share/claude-mavericks/libavxemu.dylib" \
    $PURE $ASM -o "$OUT/libavxemu.dylib"
AVXEMU_SELFTEST=1 DYLD_INSERT_LIBRARIES="$OUT/libavxemu.dylib" /usr/bin/true || \
    { echo "    self-test FAILED"; exit 1; }

echo "[8a] runtime fault handler: SIMD over-read fixup + guard-page safety..."
"$CC" -O0 test/overread_fault.c -o "$OUT/overread_fault"
"$CC" -O0 test/guard_page.c      -o "$OUT/guard_page"
AVXEMU_FORCEPATCH=1 DYLD_INSERT_LIBRARIES="$OUT/libavxemu.dylib" "$OUT/overread_fault" >/dev/null 2>&1 \
    && echo "    over-read across unmapped page: fixed + retried — PASS" \
    || { echo "    over-read fixup FAILED"; exit 1; }
gp=0   # must crash (not 0): a PROT_NONE guard page must NOT be masked
AVXEMU_FORCEPATCH=1 DYLD_INSERT_LIBRARIES="$OUT/libavxemu.dylib" "$OUT/guard_page" >"$OUT/guard.out" 2>/dev/null || gp=$?
if [ "$gp" != 0 ] && ! grep -q MASKED "$OUT/guard.out"; then
    echo "    PROT_NONE guard page left intact (not masked) — PASS"
else
    echo "    guard page was masked — FAIL"; exit 1
fi

if [ -f "$BINARY" ]; then
    echo "[8b] trampoline end-to-end: forced-trampolined output == native (real binary)..."
    "$BINARY" --help >"$OUT/nat.txt" 2>/dev/null || true
    AVXEMU_FORCETRAMP=1 DYLD_INSERT_LIBRARIES="$OUT/libavxemu.dylib" \
        "$BINARY" --help >"$OUT/tramp.txt" 2>"$OUT/tramp.err" || true
    if cmp -s "$OUT/nat.txt" "$OUT/tramp.txt"; then
        echo "    output IDENTICAL to native ($(wc -c <"$OUT/nat.txt") bytes) — PASS"
    else
        echo "    output DIFFERS from native — FAIL"; exit 1
    fi
fi

echo "    -> $OUT/libavxemu.dylib"
if [ "$1" = "install" ]; then
    cp "$OUT/libavxemu.dylib" "$PUBLIC/libavxemu.dylib"
    echo "    installed to $PUBLIC/libavxemu.dylib"
fi
echo "OK"
