#!/usr/bin/env python
"""
Set RW_HAS_CUSTOM_RR on the classes the Swift runtime realizes by hand on 10.9.

The ModernMavericks swift-runtime builds libswiftCore for OS X 10.9 and, because
objc4-532 has no objc_readClassPair, realizes runtime-instantiated generic
classes itself inside swift_instantiateObjCClass (its patches 0003-0005). That
minimal realization sets only RW_REALIZED:

    movl $0x80000000, (%rax)

objc4-532's own realizeClass also inherits RW_HAS_CUSTOM_RR from the superclass.
Without it, objc_retain and objc_release take libobjc's inlined side-table path
instead of sending -retain/-release, so an object gets two independent
reference counts: libobjc's and the one in the Swift object header. ObjC drains
its count to zero and calls dealloc while Swift still holds references, which
surfaces as

    Object 0x... of class _SwiftDeferredNSDictionary deallocated with
    non-zero retain count 2

on the first Swift Dictionary or Set that crosses into Objective-C -- which for
a GUI app is immediately.

Setting the bit unconditionally is safe: it only tells objc to dispatch
retain/release as messages rather than inline them, and a sent -retain resolves
to the right implementation whatever the class's ancestry. It is never wrong,
only slightly slower.

Bit 15 was established by observation on 10.9 rather than from a header: build a
class overriding retain/release, register a subclass of it, and see which flag
the subclass inherits.

Usage: patch_swift_custom_rr.py libswiftCore.dylib
"""
import sys, struct

RW_REALIZED = 0x80000000
RW_HAS_CUSTOM_RR = 0x00008000

# movl $imm32, (%rax)
OLD = b"\xc7\x00" + struct.pack("<I", RW_REALIZED)
NEW = b"\xc7\x00" + struct.pack("<I", RW_REALIZED | RW_HAS_CUSTOM_RR)


def instantiate_range(path, data):
    """File-offset range of swift_instantiateObjCClass.

    The byte pattern alone is not specific enough -- it also occurs in
    unrelated code -- so the search is confined to the one function that does
    the hand realization.
    """
    import subprocess
    addr = None
    out = subprocess.Popen(["nm", "-n", path],
                           stdout=subprocess.PIPE).communicate()[0]
    syms = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 3 and f[0].strip():
            try:
                syms.append((int(f[0], 16), f[2].decode() if isinstance(f[2], bytes) else f[2]))
            except ValueError:
                pass
    syms.sort()
    for i, (a, n) in enumerate(syms):
        if n == "_swift_instantiateObjCClass":
            end = syms[i + 1][0] if i + 1 < len(syms) else a + 0x400
            addr = (a, end)
            break
    if addr is None:
        return None

    # Map that vmaddr range to file offsets via __TEXT.
    ncmds = struct.unpack_from("<I", data, 16)[0]
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            segname = bytes(data[off + 8:off + 24]).rstrip(b"\0")
            vmaddr, vmsize, fileoff = struct.unpack_from("<QQQ", data, off + 24)
            if segname == b"__TEXT" and vmaddr <= addr[0] < vmaddr + vmsize:
                delta = fileoff - vmaddr
                return (addr[0] + delta, addr[1] + delta)
        off += cmdsize
    return None


def main(path):
    data = bytearray(open(path, "rb").read())
    if data[:4] != b"\xcf\xfa\xed\xfe":
        sys.stderr.write("not a 64-bit little-endian Mach-O\n")
        return 1

    span = instantiate_range(path, data)
    if span is None:
        sys.stderr.write("%s: swift_instantiateObjCClass not found\n" % path)
        return 2
    lo, hi = span

    hits = []
    start = lo
    while True:
        i = data.find(OLD, start, hi)
        if i < 0:
            break
        hits.append(i)
        start = i + 1

    if not hits:
        already = data.count(NEW, lo, hi)
        if already:
            print("%s: already patched (%d site(s))" % (path, already))
            return 0
        sys.stderr.write("%s: no 'movl $0x80000000, (%%rax)' found -- has the "
                         "runtime changed?\n" % path)
        return 2

    # The realization appears twice: once for the class, once for its metaclass.
    # More than a handful means the pattern is matching something else too, and
    # blind patching would corrupt unrelated code.
    if len(hits) > 4:
        sys.stderr.write("%s: %d candidate sites, expected 2 -- refusing\n"
                         % (path, len(hits)))
        return 3

    for i in hits:
        data[i:i + len(OLD)] = NEW
    open(path, "wb").write(bytes(data))
    print("%s: set RW_HAS_CUSTOM_RR at %d site(s): %s"
          % (path, len(hits), ", ".join("0x%x" % h for h in hits)))
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        sys.exit(1)
    sys.exit(main(sys.argv[1]))
