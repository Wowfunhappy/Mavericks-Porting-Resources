#!/bin/sh
# Hermetic test for change_dylib's library-ordinal renumbering (-insert, -delete).
#
# Two-level-namespace binaries record, for every undefined symbol, WHICH dylib it
# comes from — as a 1-based index into the LC_LOAD_DYLIB commands in load order
# (nlist n_desc, and the SET_DYLIB_ORDINAL opcodes in the LC_DYLD_INFO bind
# streams). Inserting or deleting a load command shifts those indices, so the
# tool must renumber them or the binary silently binds symbols to the wrong
# library. This builds real dylibs, rewrites a real executable, and RUNS it —
# a wrong ordinal shows up as a dyld "Symbol not found" or a wrong answer.
#
#   ./change_dylib_test.sh          (needs only clang + otool)
set -e
cd "$(dirname "$0")"
CC="${CC:-clang}"
T="${TMPDIR:-/tmp}/change_dylib_test.$$"
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT INT TERM

"$CC" -O2 -o "$T/change_dylib" change_dylib.c
fails=0
ok()   { echo "PASS $1"; }
bad()  { echo "FAIL $1: $2"; fails=$((fails+1)); }

# --- fixtures: three dylibs, and a main that calls into two of them ----------
cat > "$T/a.c" <<'EOF'
int a_sym(void) { return 11; }
EOF
cat > "$T/b.c" <<'EOF'
int b_sym(void) { return 22; }
EOF
cat > "$T/spare.c" <<'EOF'
int spare_sym(void) { return 99; }
EOF
cat > "$T/main.c" <<'EOF'
#include <stdio.h>
int a_sym(void); int b_sym(void);
int main(void) { int v = a_sym() + b_sym(); printf("%d\n", v); return v == 33 ? 0 : 1; }
EOF
for l in a b spare; do
    "$CC" -dynamiclib -O2 -install_name "@loader_path/lib$l.dylib" \
        "$T/$l.c" -o "$T/lib$l.dylib"
done

# ordinals as linked: 1=liba, 2=libb  (link order sets load-command order)
build_main() { "$CC" -O2 "$T/main.c" "$T/liba.dylib" "$T/libb.dylib" -o "$1"; }

# --- baseline ----------------------------------------------------------------
build_main "$T/main"
out=$(cd "$T" && ./main) && [ "$out" = "33" ] \
    && ok "baseline runs (33)" || bad "baseline" "got '$out'"

# --- 1. -insert puts the new dylib FIRST and renumbers ------------------------
# Without renumbering, a_sym's ordinal 1 now names libspare -> dyld aborts.
build_main "$T/main_ins"
"$T/change_dylib" "$T/main_ins" -grow -insert "@loader_path/libspare.dylib" >/dev/null || bad "tool run" "change_dylib failed"
first=$(otool -L "$T/main_ins" | sed -n '2p' | awk '{print $1}')
case "$first" in
    *libspare.dylib) ok "-insert: libspare is the first dependency" ;;
    *) bad "-insert order" "first dep is '$first'" ;;
esac
if out=$(cd "$T" && ./main_ins 2>&1) && [ "$out" = "33" ]; then
    ok "-insert: renumbered, binary still resolves a_sym/b_sym (33)"
else
    bad "-insert renumber" "got '$out'"
fi

# --- 2. -delete of an EARLIER dylib renumbers the survivors -------------------
# libspare is linked first but unreferenced; deleting it shifts liba 2->1,
# libb 3->2. Without renumbering, a_sym would be looked up in libb.
"$CC" -O2 "$T/main.c" "$T/libspare.dylib" "$T/liba.dylib" "$T/libb.dylib" -o "$T/main_del"
out=$(cd "$T" && ./main_del) && [ "$out" = "33" ] \
    || bad "delete fixture" "fixture itself broken: '$out'"
"$T/change_dylib" "$T/main_del" -delete "@loader_path/libspare.dylib" >/dev/null || bad "tool run" "change_dylib failed"
otool -L "$T/main_del" | grep -q libspare \
    && bad "-delete" "libspare still present" \
    || ok "-delete: libspare removed"
if out=$(cd "$T" && ./main_del 2>&1) && [ "$out" = "33" ]; then
    ok "-delete: renumbered, survivors still resolve (33)"
else
    bad "-delete renumber" "got '$out'"
fi

# --- 3. deleting a dylib that symbols still bind to must be refused ----------
build_main "$T/main_bad"
if "$T/change_dylib" "$T/main_bad" -delete "@loader_path/liba.dylib" >/dev/null 2>&1; then
    bad "-delete in-use" "tool accepted deleting a dylib that still has bound symbols"
else
    ok "-delete: refuses to orphan symbols bound to the deleted dylib"
fi

# --- 4. -insert composes with -change ----------------------------------------
build_main "$T/main_both"
"$T/change_dylib" "$T/main_both" -grow -insert "@loader_path/libspare.dylib" \
    -change "@loader_path/libb.dylib" "@loader_path/libb2.dylib" >/dev/null || bad "tool run" "change_dylib failed"
cp "$T/libb.dylib" "$T/libb2.dylib"
otool -L "$T/main_both" | grep -q libb2 \
    && ok "-insert + -change compose" || bad "compose" "libb2 not present"
if out=$(cd "$T" && ./main_both 2>&1) && [ "$out" = "33" ]; then
    ok "-insert + -change: still resolves (33)"
else
    bad "compose run" "got '$out'"
fi

# --- 5. two dylibs exporting the SAME symbol ---------------------------------
# The shape MF actually ships: libS.dylib wraps symbols libSystem also exports,
# so a stale ordinal can name a library that does resolve the symbol — just the
# wrong implementation. In practice dyld usually catches a stale ordinal first,
# because dyld_stub_binder lives in the last-linked dylib and its index goes out
# of range ("library ordinal too big"). Pinned here either way: after the
# rewrite this must still call libdup1.
cat > "$T/dup1.c" <<'EOF'
int dup_sym(void) { return 1; }
EOF
cat > "$T/dup2.c" <<'EOF'
int dup_sym(void) { return 2; }
EOF
cat > "$T/dupmain.c" <<'EOF'
#include <stdio.h>
int dup_sym(void);
int main(void) { int v = dup_sym(); printf("%d\n", v); return v == 1 ? 0 : 1; }
EOF
for l in dup1 dup2; do
    "$CC" -dynamiclib -O2 -install_name "@loader_path/lib$l.dylib" \
        "$T/$l.c" -o "$T/lib$l.dylib"
done
# ordinals: 1=libspare, 2=libdup1 (the one we bind to), 3=libdup2
"$CC" -O2 "$T/dupmain.c" "$T/libspare.dylib" "$T/libdup1.dylib" "$T/libdup2.dylib" \
    -o "$T/main_dup"
"$T/change_dylib" "$T/main_dup" -delete "@loader_path/libspare.dylib" >/dev/null \
    || bad "tool run" "change_dylib failed"
out=$(cd "$T" && ./main_dup 2>&1) || true
if [ "$out" = "1" ]; then
    ok "-delete: still calls libdup1 (no silent rebind to libdup2)"
else
    bad "-delete silent rebind" "got '$out' — bound to the wrong dylib"
fi

echo
[ "$fails" -eq 0 ] && { echo "change_dylib_test: all cases pass"; exit 0; }
echo "change_dylib_test: $fails FAILED"; exit 1
