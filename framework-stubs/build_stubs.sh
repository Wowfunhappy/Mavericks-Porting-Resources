#!/bin/bash
# Build the stub/wrapper dylibs described by frameworks.json into a directory.
#
# Usage: build_stubs.sh OUTDIR [name ...]
#
# Each stub is built with an @rpath install name, so a consumer only needs an
# LC_RPATH that reaches OUTDIR -- app bundles normally already have one -- and
# the LC_LOAD_DYLIB path it replaces gets shorter, which avoids having to grow
# the Mach-O header.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"; shift || true
[ -n "$OUT" ] || { echo "usage: $0 OUTDIR [name ...]" >&2; exit 1; }
mkdir -p "$OUT"

# Frameworks a stub itself needs. Keep this minimal: linking a stub against a
# framework it does not use drags that framework into every consumer's load.
frameworks_for() {
    case "$1" in
        Network) echo "-framework Foundation -framework SystemConfiguration" ;;
        CryptoKit) echo "" ;;
        *) echo "-framework Foundation" ;;
    esac
}

# A stand-in must declare at least the dylib versions its consumers require,
# or dyld rejects it before looking up a single symbol.
vflags_for() {
    python -c "
import json
e=json.load(open('$HERE/frameworks.json'))['$1']
cv=e.get('compatibility_version'); cur=e.get('current_version')
out=[]
if cv: out.append('-compatibility_version '+cv)
if cur: out.append('-current_version '+cur)
print(' '.join(out))"
}

names="$*"
[ -n "$names" ] || names=$(python -c "
import json,sys
spec=json.load(open('$HERE/frameworks.json'))
print(' '.join(n for n,e in sorted(spec.items()) if e.get('kind') in ('stub','reexport','wrapper')))")

for name in $names; do
    kind=$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('kind','stub'))")
    lib=$(python -c "
import json;spec=json.load(open('$HERE/frameworks.json'))
print(spec['$name'].get('library','lib${name}Stub.dylib'))")

    # A "reexport" framework is present on 10.9 but at a different path -- it
    # moved into or out of an umbrella. Re-exporting the real one from a short
    # @rpath name keeps every consumer's LC_LOAD_DYLIB shorter than the path it
    # replaces, so no Mach-O header has to grow (which dylibs cannot do).
    if [ "$kind" = "reexport" ]; then
        # ld refuses to link a subframework directly, so where the framework
        # now lives inside an umbrella, re-export the umbrella: it re-exports
        # the subframework in turn, and two-level lookups follow that chain.
        umbrella=$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('reexport_framework',''))")
        target=$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name']['reexport'])")
        echo 'static int _shim_unused __attribute__((unused));' > "$OUT/.shim_$name.c"
        if [ -n "$umbrella" ]; then
            clang -dynamiclib -O2 -o "$OUT/$lib" "$OUT/.shim_$name.c" \
                -install_name "@rpath/$lib" -Wl,-reexport_framework,"$umbrella" \
                $(vflags_for $name)
            echo "  $name -> $lib (re-exports $umbrella, which re-exports it)"
        else
            clang -dynamiclib -O2 -o "$OUT/$lib" "$OUT/.shim_$name.c" \
                -install_name "@rpath/$lib" -Wl,-reexport_library,"$target" \
                $(vflags_for $name)
            echo "  $name -> $lib (re-exports $target)"
        fi
        rm -f "$OUT/.shim_$name.c"
        continue
    fi

    # A "wrapper" framework exists on 10.9 but is missing symbols. It re-exports
    # the real library and adds only those, so a consumer's other binds still
    # resolve to the genuine implementation. Symbols coming from a static
    # archive are named with -u so the linker pulls exactly those objects.
    if [ "$kind" = "wrapper" ]; then
        # A file implementing constant objects overrides retain/release, which
        # ARC forbids; such an entry opts out with "arc": false.
        arc="-fobjc-arc"
        [ "$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('arc',True))")" = "False" ] && arc=""
        real=$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name']['framework'])")
        archive=$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('archive',''))")
        src=$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('source',''))")
        # Additional archives a wrapper links whole. Held in a variable rather
        # than expanded inline because these paths contain spaces.
        extra1=$(python -c "
import json,os
e=json.load(open('$HERE/frameworks.json'))['$name']
a=e.get('extra_archives',[])
print(a[0] if a and a[0].startswith('/') else (os.path.join('$HERE','..',a[0]) if a else ''))")
        # Export a symbol under a second name. Used where the expected name
        # must not also be a class name -- see libSystemWrapper.m.
        aflags=$(python -c "
import json
e=json.load(open('$HERE/frameworks.json'))['$name']
print(' '.join('-Wl,-alias,%s,%s'%(a,b) for a,b in e.get('alias_symbols',[])))")
        # Some wrappers link an archive that drags in far more than they need.
        # "restrict_exports" limits what the wrapper advertises to its declared
        # symbols, so incidental definitions stay private instead of shadowing
        # the real library for every consumer.
        expflag=""
        if [ "$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('restrict_exports',False))")" = "True" ]; then
            explist="$OUT/.exports_$name.txt"
            python -c "
import json
e=json.load(open('$HERE/frameworks.json'))['$name']
open('$explist','w').write('\n'.join(e['symbols'])+'\n')"
            expflag="-Wl,-exported_symbols_list,$explist"
        fi
        uflags=""
        if [ -n "$archive" ]; then
            case "$archive" in /*) ;; *) archive="$HERE/../$archive" ;; esac
            uflags=$(python -c "
import json,subprocess
e=json.load(open('$HERE/frameworks.json'))['$name']
have=set()
out=subprocess.Popen(['nm','-g','$archive'],stdout=subprocess.PIPE).communicate()[0]
for l in out.splitlines():
    p=l.split()
    if len(p)>=3 and p[-2] in ('T','S','D','B'): have.add(p[-1])
print(' '.join('-Wl,-u,'+s for s in sorted(set(e['symbols']) & have)))")
        fi
        driver=clang; stdflag=""
        # noexcept and the sized-deallocation signatures need C++11; this
        # toolchain still defaults to C++98.
        case "$src" in *.cpp) driver=clang++; arc=""; stdflag="-std=c++11" ;; esac
        $driver -dynamiclib $arc $stdflag -O2 -o "$OUT/$lib" \
            ${src:+"$HERE/$src"} \
            -install_name "@rpath/$lib" \
            $(vflags_for $name) \
            $uflags $aflags $expflag ${archive:+"$archive"} ${extra1:+"$extra1"} \
            -Wl,-reexport_library,"$real" \
            $(python -c "
import json;e=json.load(open('$HERE/frameworks.json'))['$name']
fw=e.get('link_frameworks')
out=['-framework '+f for f in (['Foundation'] if fw is None else fw)]
out+=['-l'+l for l in e.get('link_libs',[])]
print(' '.join(out))") \
            -Wno-deprecated-declarations
        echo "  $name -> $lib (re-exports $(basename $real))"
        continue
    fi

    src=$(python -c "
import json;spec=json.load(open('$HERE/frameworks.json'));e=spec['$name']
print(e.get('source', '${name}Stub.m'))")
    [ -f "$HERE/$src" ] || { echo "  $name: no source ($src)" >&2; continue; }
    stubarc="-fobjc-arc"
    [ "$(python -c "
import json;print(json.load(open('$HERE/frameworks.json'))['$name'].get('arc',True))")" = "False" ] && stubarc=""
    case "$src" in *.c) stubarc="" ;; esac
    clang -dynamiclib $stubarc -O2 \
        -o "$OUT/$lib" "$HERE/$src" \
        -install_name "@rpath/$lib" \
        $(vflags_for "$name") \
        $(frameworks_for "$name") \
        -Wno-deprecated-declarations
    echo "  $name -> $lib"
done
