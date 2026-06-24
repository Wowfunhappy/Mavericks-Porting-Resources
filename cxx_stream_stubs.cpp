/*
 * libc++ stream symbols that 10.9's /usr/lib/libc++.1.dylib never exported,
 * but a modern (libc++-ABI) binary imports.
 *
 * 10.9 shipped libc++ but did *not* explicitly instantiate the char stream
 * classes in the dylib, and the openmode parameter changed mangling over time
 * (`ios_base::openmode` -> plain `unsigned int`, i.e. `...4openEPKcj`). A
 * binary built against a 10.14+ SDK therefore fails to load with e.g.
 *
 *   dyld: Symbol not found: __ZNSt3__113basic_filebufIcNS_11char_traitsIcEEE4openEPKcj
 *   Expected in: .../libcxxWrapper.dylib
 *
 * Rather than hand-stub vtables, force the compiler to *explicitly
 * instantiate* the missing stream classes using 10.9's own libc++ headers.
 * That emits real, correct implementations (ctors, dtors, vtables, VTTs,
 * member functions) with exactly the mangling the binary expects.
 *
 * Build into the libc++ wrapper, exporting ONLY the symbols the target binary
 * is actually missing (so everything else still resolves through the
 * re-exported real libc++):
 *
 *   # 1. discover what the binary needs that 10.9 lacks
 *   mp-nm -u BIN | awk '{print $NF}' | grep '^__Z' | sort -u > need.txt
 *   for L in /usr/lib/libc++.1.dylib /usr/lib/libc++abi.dylib; do \
 *       mp-nm -gU "$L"; done | awk '{print $NF}' | grep '^__Z' | sort -u > have.txt
 *   comm -23 need.txt have.txt > missing.txt          # the export list
 *
 *   # 2. build the wrapper (export only `missing.txt`, re-export real libc++)
 *   clang++ -std=c++11 -O2 -fPIC -c cxx_stream_stubs.cpp -o cxx_stream_stubs.o
 *   clang++ -dynamiclib -o libcxxWrapper.dylib cxx_stream_stubs.o \
 *       -Wl,-reexport_library,/usr/lib/libc++.1.dylib \
 *       -Wl,-exported_symbols_list,missing.txt \
 *       -install_name @loader_path/libcxxWrapper.dylib \
 *       -nostdlib -lSystem
 *
 * If a future binary needs a stream type not covered here (wide-char streams,
 * ostringstream, ...), add the corresponding `template class` line — the
 * exported-symbols filter keeps the wrapper minimal regardless.
 */
#include <fstream>
#include <sstream>

namespace std {
    template class __1::basic_filebuf<char>;
    template class __1::basic_ifstream<char>;
    template class __1::basic_stringbuf<char, char_traits<char>, allocator<char> >;
    template class __1::basic_stringstream<char, char_traits<char>, allocator<char> >;
}
