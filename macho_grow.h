/*
 * macho_grow.h — make room in a Mach-O header so the load commands can expand.
 *
 * The problem: tools like change_dylib (and patch_macho's LC_DYLD_INFO_ONLY
 * insertion) write load commands in place, bounded by the file offset of the
 * first section's data. When the linker leaves little padding there (recent
 * Bun/JSC builds leave as few as 16 bytes), a longer dylib path or an extra
 * load command no longer fits.
 *
 * The fix, studied from LIEF (src/MachO/Binary.cpp `shift`) and llvm-objcopy
 * (MachOLayoutBuilder): make room by inserting page-aligned space after the
 * load commands. LIEF/llvm push every later segment to a HIGHER vm address and
 * then fix up everything that depended on those addresses — section-symbol
 * n_values, LC_MAIN, function-start deltas, relocations, rebase/bind/chained
 * targets. That is a lot of machinery — and we can't just run those tools on
 * 10.9 anyway: LIEF needs a modern-macOS C++ runtime and llvm-objcopy a
 * cross-built toolchain, while install_name_tool / optool / insert_dylib refuse
 * to grow the header at all. This header compiles with the stock 10.9 clang and
 * has no dependencies.
 *
 * We take a simpler, equivalent route available to any PIE executable with a
 * __PAGEZERO: instead of raising data, we LOWER the image base. We donate the
 * inserted bytes from __PAGEZERO and drop __TEXT's vmaddr by the same amount,
 * growing __TEXT's vm/file size. Net effect: every section and segment keeps
 * its ORIGINAL vm address, so no pointer, rebase, bind, n_value, or entry
 * address ever changes. The only fields that move are file offsets — which we
 * shift uniformly. (Borrowed from LIEF: the exhaustive list of offset fields.)
 *
 * Precondition: a MH_PIE executable with a __PAGEZERO at least `grow` bytes
 * large. (Always true for the Claude Code executable: 0x1_0000_0000 pagezero.)
 * Dylibs without a __PAGEZERO can't lower the base; mg_grow_header reports that
 * and leaves the buffer untouched so the caller can fall back / error cleanly.
 */
#ifndef MACHO_GROW_H
#define MACHO_GROW_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <mach-o/loader.h>

/* Load-command constants newer than the 10.9 SDK headers. */
#ifndef LC_DYLD_EXPORTS_TRIE
#define LC_DYLD_EXPORTS_TRIE        0x80000033
#endif
#ifndef LC_DYLD_CHAINED_FIXUPS
#define LC_DYLD_CHAINED_FIXUPS      0x80000034
#endif
#ifndef LC_DYLIB_CODE_SIGN_DRS
#define LC_DYLIB_CODE_SIGN_DRS      0x2B
#endif
#ifndef LC_LINKER_OPTIMIZATION_HINT
#define LC_LINKER_OPTIMIZATION_HINT 0x2E
#endif
/* dyld4-era section type: 4-byte initializer offsets FROM THE IMAGE BASE,
 * replacing the absolute pointers of S_MOD_INIT_FUNC_POINTERS. */
#ifndef S_INIT_FUNC_OFFSETS
#define S_INIT_FUNC_OFFSETS 0x16
#endif
#define MG_EXPORT_KIND_MASK        0x03
#define MG_EXPORT_REEXPORT         0x08
#define MG_EXPORT_STUB_AND_RESOLVER 0x10

#define MG_PAGE 0x1000UL

/* Lowest section file offset — this bounds the header pad. */
static uint32_t mg_first_sect_off(const uint8_t *buf) {
    const struct mach_header_64 *hdr = (const struct mach_header_64 *)buf;
    uint32_t first = UINT32_MAX;
    const uint8_t *lcp = buf + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lcp;
            const struct section_64 *sect = (const struct section_64 *)(lcp + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++)
                if (sect[j].offset && sect[j].offset < first) first = sect[j].offset;
        }
        lcp += lc->cmdsize;
    }
    return first == UINT32_MAX ? 4096 : first;
}

/* Shift one file-offset field down by `grow` if it points at/after `insert`. */
static void mg_bump(uint32_t *off, uint32_t insert, uint32_t grow) {
    if (*off >= insert) *off += grow;
}

/* ---- ULEB128, for the LC_FUNCTION_STARTS leading-delta re-encode ----------
 *
 * LC_FUNCTION_STARTS is a stream of ULEB128 deltas: the FIRST is relative to the
 * image base, the rest are function-to-function. Lowering the image base by N
 * (the grow trick) leaves every function's VM address fixed, so the later deltas
 * are unchanged, but the first must gain N or every reconstructed function
 * address comes out N low. We adjust it in place, preserving its byte width so
 * the blob — and all of __LINKEDIT after it — never moves. (When the widened
 * delta would need more bytes than the original encoding, we refuse rather than
 * resize LINKEDIT; see mg_grow_header.) */

/* Decode one ULEB128 at p (< end). Returns bytes consumed, 0 if malformed
 * (continuation runs past end, or > 10 bytes). *out = value. */
static int mg_uleb_decode(const uint8_t *p, const uint8_t *end, uint64_t *out) {
    uint64_t r = 0; int s = 0, n = 0;
    while (p + n < end && n < 10) {
        uint8_t b = p[n]; r |= (uint64_t)(b & 0x7f) << s; n++;
        if (!(b & 0x80)) { *out = r; return n; }
        s += 7;
    }
    return 0;
}

/* Minimal number of bytes to ULEB-encode v (>= 1). */
static int mg_uleb_minlen(uint64_t v) {
    int n = 1; while (v >= 0x80) { v >>= 7; n++; } return n;
}

/* Encode v into exactly `width` ULEB128 bytes at p, padding non-minimally with
 * continuation groups if width exceeds the minimal length. Returns 1 on success,
 * 0 if v does not fit in `width` bytes. */
static int mg_uleb_encode_fixed(uint8_t *p, uint64_t v, int width) {
    if (width < 1 || mg_uleb_minlen(v) > width) return 0;
    for (int i = 0; i < width; i++) {
        uint8_t b = (uint8_t)((v >> (7 * i)) & 0x7f);
        if (i < width - 1) b |= 0x80;   /* keep the stream going through the pad */
        p[i] = b;
    }
    return 1;
}

/* Re-encode the leading (base-relative) LC_FUNCTION_STARTS delta after lowering
 * the image base by `grow`: delta[0] += grow, keeping the leading delta's byte
 * width so blob size is unchanged and the trailing deltas are untouched.
 * Returns: 1 patched in place; 0 the widened delta needs more bytes than the
 * original leading encoding (caller must refuse — LINKEDIT resize unsupported);
 * -1 malformed blob (empty / bad leading ULEB). */
static int mg_reencode_funcstarts_base(uint8_t *blob, uint32_t size, uint32_t grow) {
    if (size == 0) return -1;
    uint64_t d0; int n0 = mg_uleb_decode(blob, blob + size, &d0);
    if (n0 == 0) return -1;
    uint64_t nd = d0 + grow;
    if (mg_uleb_minlen(nd) > n0) return 0;          /* would widen -> caller refuses */
    return mg_uleb_encode_fixed(blob, nd, n0) ? 1 : 0;
}

/* Decode the whole function-starts blob into absolute addresses given the image
 * base. Stops at a 0 delta (terminator/padding) or end. Returns count (<= max),
 * or -1 on a malformed ULEB. (Used by tests to assert the grow moved nothing.) */
static int mg_funcstarts_decode(const uint8_t *blob, uint32_t size,
                                uint64_t base, uint64_t *out, int max) {
    const uint8_t *p = blob, *end = blob + size; uint64_t addr = base; int n = 0;
    while (p < end && n < max) {
        uint64_t d; int c = mg_uleb_decode(p, end, &d);
        if (c == 0) return -1;
        p += c;
        if (d == 0) break;
        addr += d; out[n++] = addr;
    }
    return n;
}


/* ---- base-relative structures ---------------------------------------------
 *
 * Lowering the image base keeps every ABSOLUTE vm address fixed, which is what
 * makes this trick cheap. Values stored as an OFFSET FROM THE IMAGE BASE are the
 * exception: the base moved out from under them, so each must gain `grow`.
 * LC_FUNCTION_STARTS' leading delta (handled above) is one. These are the rest.
 *
 * Note S_MOD_INIT_FUNC_POINTERS needs nothing: those are absolute pointers that
 * dyld rebases, and their target addresses do not change. */

/* Add `grow` to every entry of every S_INIT_FUNC_OFFSETS section, or with
 * patch=0 just verify the pass would be sound. Without this, dyld4-era static
 * constructors are called at (base - grow) + offset and jump into whatever
 * precedes them. Returns 0 ok, -1 malformed/unsafe. */
static int mg_init_offsets_pass(uint8_t *buf, size_t fsize, uint32_t grow, int patch) {
    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if ((sect[j].flags & SECTION_TYPE) != S_INIT_FUNC_OFFSETS) continue;
                if (sect[j].size % 4) return -1;
                if ((uint64_t)sect[j].offset + sect[j].size > (uint64_t)fsize) return -1;
                uint32_t n = (uint32_t)(sect[j].size / 4);
                uint32_t *e = (uint32_t *)(buf + sect[j].offset);
                for (uint32_t k = 0; k < n; k++) {
                    if (!patch) { if (e[k] > UINT32_MAX - grow) return -1; }
                    else e[k] += grow;
                }
            }
        }
        lcp += lc->cmdsize;
    }
    return 0;
}

/* Walk the export trie looking for an exported address we would have to
 * re-encode. Addresses there are ULEB offsets from the image base; bumping one
 * can widen its encoding and force __LINKEDIT to be rebuilt, which this header
 * does not do. __mh_execute_header is exported at offset 0 and stays correct --
 * it names the header, which moved down with the base -- so a trie whose
 * addresses are all zero is safe to leave alone.
 * Returns 0 safe, 1 needs re-encoding, -1 malformed. */
static int mg_trie_scan(const uint8_t *trie, uint32_t size, uint32_t off, int depth) {
    if (depth > 128 || off >= size) return -1;
    const uint8_t *p = trie + off, *end = trie + size;
    uint64_t term; int n = mg_uleb_decode(p, end, &term);
    if (n == 0) return -1;
    p += n;
    if (term) {
        const uint8_t *tend = p + term;
        if (tend > end) return -1;
        uint64_t flags; n = mg_uleb_decode(p, end, &flags);
        if (n == 0) return -1;
        p += n;
        if (!(flags & MG_EXPORT_REEXPORT)) {       /* re-exports carry no address */
            uint64_t a; n = mg_uleb_decode(p, end, &a);
            if (n == 0) return -1;
            if (a != 0) return 1;
            if (flags & MG_EXPORT_STUB_AND_RESOLVER) {
                p += n;
                n = mg_uleb_decode(p, end, &a);
                if (n == 0) return -1;
                if (a != 0) return 1;
            }
        }
        p = tend;
    }
    if (p >= end) return -1;
    uint8_t nch = *p++;
    for (uint8_t i = 0; i < nch; i++) {
        while (p < end && *p) p++;
        if (p >= end) return -1;
        p++;
        uint64_t coff; n = mg_uleb_decode(p, end, &coff);
        if (n == 0) return -1;
        p += n;
        int r = mg_trie_scan(trie, size, (uint32_t)coff, depth + 1);
        if (r != 0) return r;
    }
    return 0;
}

/* Refuse the base-relative structures we relocate but do not rewrite.
 *
 * LC_DATA_IN_CODE entry offsets and __TEXT,__unwind_info function offsets are
 * measured from the image base, exactly like __init_offsets and the
 * function-starts leading delta. Lowering the base leaves every one of them
 * `grow` bytes low. Neither is read at load time -- they surface during
 * exception unwinding, crash reporting and debugging -- so a grown binary looks
 * perfectly healthy and is not. That is the worst defect this code can ship, so
 * refuse until the re-basers exist rather than succeed silently.
 * Returns 0 if nothing unhandled is present, -1 (with a message) otherwise. */
static int mg_audit_unrebased(const uint8_t *buf) {
    const struct mach_header_64 *h = (const struct mach_header_64 *)buf;
    const uint8_t *sp = buf + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)sp;
        if (lc->cmd == LC_DATA_IN_CODE) {
            const struct linkedit_data_command *d =
                (const struct linkedit_data_command *)sp;
            if (d->datasize) {
                fprintf(stderr, "macho_grow: LC_DATA_IN_CODE holds %u entries whose offsets "
                        "are measured from the image base; re-basing them is not implemented, "
                        "so growing would leave every range low. Reclaim header bytes instead "
                        "(change_dylib -strip-lc uuid -strip-lc codesig).\n",
                        d->datasize / 8);
                return -1;
            }
        }
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)sp;
            const struct section_64 *sect = (const struct section_64 *)(sp + sizeof *seg);
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if (sect[j].size &&
                    strncmp(sect[j].sectname, "__unwind_info", sizeof sect[j].sectname) == 0) {
                    fprintf(stderr, "macho_grow: %.16s,%.16s holds compact-unwind function "
                            "offsets measured from the image base; re-basing them is not "
                            "implemented, so growing would leave the unwind tables low. "
                            "Reclaim header bytes instead (change_dylib -strip-lc uuid "
                            "-strip-lc codesig).\n", sect[j].segname, sect[j].sectname);
                    return -1;
                }
            }
        }
        sp += lc->cmdsize;
    }
    return 0;
}

/* ---- verification: prove the grow moved nothing ---------------------------
 * Every structure below stores an offset FROM THE IMAGE BASE, so lowering the
 * base by `grow` must leave the RESOLVED address (base + offset) unchanged.
 * Snapshot those resolved addresses before the transform, recompute them after,
 * and compare. That catches a handler that did not run, one that ran twice, and
 * one that ran with the wrong delta -- without needing to know which.
 *
 * mg_collect walks the base-relative structures in load-command order, which is
 * deterministic and identical before and after, so element i means the same
 * thing in both snapshots. */
typedef struct { uint64_t *addr; uint32_t n; } mg_snapshot;

#define MG_SNAP_MAX 65536

static int mg_collect(const uint8_t *buf, size_t fsize, uint64_t *out, uint32_t max,
                      uint32_t *n_out) {
    const struct mach_header_64 *h = (const struct mach_header_64 *)buf;
    const uint8_t *sp = buf + sizeof *h;
    uint64_t base = 0;
    uint32_t n = 0;
    /* image base first: __TEXT is the segment mapping the header (fileoff 0, has content) */
    const uint8_t *q = sp;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)q;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)q;
            if (seg->fileoff == 0 && seg->filesize > 0) { base = seg->vmaddr; break; }
        }
        q += lc->cmdsize;
    }
    if (!base) return -1;

    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)sp;
        if (lc->cmd == LC_FUNCTION_STARTS) {
            const struct linkedit_data_command *d = (const struct linkedit_data_command *)sp;
            if (d->datasize) {
                if ((size_t)d->dataoff + d->datasize > fsize) return -1;
                uint64_t d0;
                if (mg_uleb_decode(buf + d->dataoff, buf + d->dataoff + d->datasize, &d0) == 0)
                    return -1;
                if (n >= max) return -1;
                out[n++] = base + d0;
            }
        }
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)sp;
            const struct section_64 *sect = (const struct section_64 *)(sp + sizeof *seg);
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if ((sect[j].flags & SECTION_TYPE) != S_INIT_FUNC_OFFSETS) continue;
                if ((size_t)sect[j].offset + sect[j].size > fsize) return -1;
                const uint32_t *e = (const uint32_t *)(buf + sect[j].offset);
                uint64_t cnt = sect[j].size / sizeof(uint32_t);
                for (uint64_t k = 0; k < cnt; k++) {
                    if (n >= max) return -1;
                    out[n++] = base + e[k];
                }
            }
        }
        sp += lc->cmdsize;
    }
    *n_out = n;
    return 0;
}

static int mg_snapshot_take(const uint8_t *buf, size_t fsize, mg_snapshot *s) {
    s->addr = (uint64_t *)malloc(MG_SNAP_MAX * sizeof(uint64_t));
    if (!s->addr) return -1;
    if (mg_collect(buf, fsize, s->addr, MG_SNAP_MAX, &s->n) != 0) {
        free(s->addr); s->addr = NULL; s->n = 0; return -1;
    }
    return 0;
}

static void mg_snapshot_free(mg_snapshot *s) { free(s->addr); s->addr = NULL; s->n = 0; }

/* 0 if every base-relative structure resolves exactly where it did before the
 * grow; -1 (with a message naming the first mismatch) otherwise. */
static int mg_verify(const uint8_t *buf, size_t fsize, const mg_snapshot *before) {
    uint64_t *now = (uint64_t *)malloc(MG_SNAP_MAX * sizeof(uint64_t));
    if (!now) return -1;
    uint32_t n = 0;
    if (mg_collect(buf, fsize, now, MG_SNAP_MAX, &n) != 0) {
        fprintf(stderr, "macho_grow: verify could not re-read the base-relative structures\n");
        free(now); return -1;
    }
    if (n != before->n) {
        fprintf(stderr, "macho_grow: verify found %u base-relative entries, %u before -- "
                        "the grow added or dropped one\n", n, before->n);
        free(now); return -1;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (now[i] == before->addr[i]) continue;
        int64_t moved = (int64_t)(now[i] - before->addr[i]);
        fprintf(stderr, "macho_grow: verify FAILED -- base-relative entry %u resolved to "
                        "%#llx before the grow and %#llx after (moved %+lld bytes). The grow "
                        "must leave every resolved address unchanged; refusing.\n",
                i, (unsigned long long)before->addr[i], (unsigned long long)now[i],
                (long long)moved);
        free(now); return -1;
    }
    free(now);
    return 0;
}

/*
 * Grow the header pad by at least `grow_req` bytes (rounded up to a page).
 * pbuf is realloc'd, pfsize updated. Returns 0 on success, -1 if the
 * precondition (PIE-style __PAGEZERO large enough) isn't met — in which case
 * the buffer and size are left unchanged.
 */
static int mg_grow_header(uint8_t **pbuf, size_t *pfsize, uint32_t grow_req) {
    uint8_t *buf = *pbuf;
    size_t fsize = *pfsize;
    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;

    uint32_t grow = (uint32_t)((grow_req + MG_PAGE - 1) & ~(MG_PAGE - 1));
    if (grow == 0) return 0;

    /* Validate preconditions before mutating anything. The image-base trick is
     * only sound for a PIE executable: it needs a __PAGEZERO to donate vm space
     * from, and it relies on the image being position-independent so that
     * lowering the base (every address shifts by the same amount) is a no-op at
     * load time. A non-PIE image, or a dylib/bundle (no __PAGEZERO), would
     * require fixing up absolute pointers — which this tool deliberately does
     * not do. Refuse loudly rather than silently corrupt. */
    if (hdr->magic != MH_MAGIC_64) {
        fprintf(stderr, "macho_grow: not a 64-bit Mach-O (magic=0x%x)\n", hdr->magic);
        return -1;
    }
    if (hdr->filetype != MH_EXECUTE) {
        fprintf(stderr, "macho_grow: only MH_EXECUTE is supported (filetype=%u); the "
                        "image-base trick needs a __PAGEZERO. Use the heavyweight "
                        "shift-up approach for dylibs/bundles (see HEADER_PAD_GROWTH.md)\n",
                hdr->filetype);
        return -1;
    }
    if (!(hdr->flags & MH_PIE)) {
        fprintf(stderr, "macho_grow: executable is not PIE (flags=0x%x); lowering the "
                        "image base would require fixing absolute relocations, which "
                        "this tool does not do (see HEADER_PAD_GROWTH.md)\n", hdr->flags);
        return -1;
    }

    uint32_t insert = mg_first_sect_off(buf);

    /* We insert space at `insert` (the first section's file offset) and shift
     * everything from there onward. That point must be at/after the end of the
     * load commands, or we'd memmove the tail of the LC table itself. A healthy
     * binary always satisfies this; refuse the anomalous case rather than
     * corrupt it. */
    uint32_t lc_end = (uint32_t)sizeof(*hdr) + hdr->sizeofcmds;
    if (insert < lc_end) {
        fprintf(stderr, "macho_grow: first section (%u) precedes end of load commands "
                        "(%u); refusing to grow a malformed header\n", insert, lc_end);
        return -1;
    }

    /* Locate the donor (__PAGEZERO) and the header-bearing segment (__TEXT). */
    struct segment_command_64 *pagezero = NULL, *text = NULL;
    uint8_t *lcp = buf + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            if (strcmp(seg->segname, "__PAGEZERO") == 0) pagezero = seg;
            else if (seg->fileoff == 0 && seg->filesize > 0) text = seg;
        }
        lcp += lc->cmdsize;
    }
    if (!text) {
        fprintf(stderr, "macho_grow: no __TEXT-like segment holds the header\n");
        return -1;
    }
    if (!pagezero || pagezero->vmsize < grow) {
        fprintf(stderr, "macho_grow: need a __PAGEZERO >= %u bytes to lower the image "
                        "base (image-base trick requires a PIE executable)\n", grow);
        return -1;
    }

    /* Locate LC_FUNCTION_STARTS and confirm its base-relative leading delta can
     * absorb `grow` without widening its ULEB encoding (the common case for a
     * page-sized grow). We check BEFORE mutating so a refusal leaves the buffer
     * untouched. dyld ignores function-starts, but avxemu uses it to find
     * function bounds for patch-safety; a stale leading delta makes every bound N
     * low and the emulator declines to patch (a SIGILL storm). fs_dataoff is the
     * ORIGINAL file offset; after the memmove the blob lives at fs_dataoff+grow. */
    uint32_t fs_dataoff = 0, fs_datasize = 0;
    {
        const uint8_t *sp = buf + sizeof(*hdr);
        for (uint32_t i = 0; i < hdr->ncmds; i++) {
            const struct load_command *lc = (const struct load_command *)sp;
            if (lc->cmd == LC_FUNCTION_STARTS) {
                const struct linkedit_data_command *ld =
                    (const struct linkedit_data_command *)sp;
                fs_dataoff = ld->dataoff; fs_datasize = ld->datasize;
                break;
            }
            sp += lc->cmdsize;
        }
    }
    if (fs_dataoff && fs_datasize) {
        uint64_t d0; int n0 = mg_uleb_decode(buf + fs_dataoff,
                                             buf + fs_dataoff + fs_datasize, &d0);
        if (n0 == 0) {
            fprintf(stderr, "macho_grow: malformed LC_FUNCTION_STARTS leading delta\n");
            return -1;
        }
        if (mg_uleb_minlen(d0 + grow) > n0) {
            fprintf(stderr, "macho_grow: grow of %u would widen the LC_FUNCTION_STARTS "
                            "leading delta (%llu -> %llu crosses a ULEB byte boundary); "
                            "in-place re-encode impossible and __LINKEDIT resize is not "
                            "implemented. Use a smaller grow.\n",
                    grow, (unsigned long long)d0, (unsigned long long)(d0 + grow));
            return -1;
        }
    }

    /* Audit the other base-relative structures before touching the buffer, so a
     * refusal leaves it pristine. Silently shipping a binary whose constructors
     * or exports are `grow` bytes low is far worse than failing here. */
    if (mg_audit_unrebased(buf) != 0) return -1;
    if (mg_init_offsets_pass(buf, fsize, grow, 0) != 0) {
        fprintf(stderr, "macho_grow: malformed S_INIT_FUNC_OFFSETS section; refusing to grow\n");
        return -1;
    }
    {
        uint32_t tr_off = 0, tr_size = 0;
        const uint8_t *sp = buf + sizeof(*hdr);
        for (uint32_t i = 0; i < hdr->ncmds; i++) {
            const struct load_command *lc = (const struct load_command *)sp;
            if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
                const struct dyld_info_command *d = (const struct dyld_info_command *)sp;
                tr_off = d->export_off; tr_size = d->export_size; break;
            }
            if (lc->cmd == LC_DYLD_EXPORTS_TRIE) {
                const struct linkedit_data_command *d = (const struct linkedit_data_command *)sp;
                tr_off = d->dataoff; tr_size = d->datasize; break;
            }
            sp += lc->cmdsize;
        }
        if (tr_off && tr_size) {
            int r = mg_trie_scan(buf + tr_off, tr_size, 0, 0);
            if (r != 0) {
                fprintf(stderr, "macho_grow: export trie %s; its addresses are offsets from "
                                "the image base and would need a ULEB re-encode that can "
                                "resize __LINKEDIT, which is not implemented. Reclaim header "
                                "bytes instead (change_dylib -strip-lc uuid -strip-lc codesig).\n",
                        r > 0 ? "exports a nonzero address" : "is malformed");
                return -1;
            }
        }
    }

    /* Phase 4 prep: snapshot every base-relative resolved address BEFORE touching
     * a byte, so the verify at the end has something to prove against. If we
     * cannot read them we cannot prove anything, so refuse rather than grow
     * blind. */
    mg_snapshot snap;
    if (mg_snapshot_take(buf, fsize, &snap) != 0) {
        fprintf(stderr, "macho_grow: could not snapshot the base-relative structures; "
                        "refusing to grow without a way to verify the result\n");
        return -1;
    }

    /* Insert `grow` zero bytes after the load commands, shifting file data down. */
    uint8_t *nbuf = (uint8_t *)realloc(buf, fsize + grow);
    if (!nbuf) { fprintf(stderr, "macho_grow: realloc failed\n"); mg_snapshot_free(&snap); return -1; }
    buf = nbuf;
    /* Hand the new pointer back IMMEDIATELY. realloc may have moved the block and
     * freed the old one, so from here on the caller's *pbuf would otherwise be
     * dangling on any failure return -- a double free waiting for whoever frees
     * on error. The paths below are "cannot happen" assertions, which is exactly
     * the kind of path that is never exercised until it is. */
    *pbuf = buf;
    hdr = (struct mach_header_64 *)buf;
    memmove(buf + insert + grow, buf + insert, fsize - insert);
    memset(buf + insert, 0, grow);

    /* Patch the header. Load commands live before `insert`, so memmove didn't
     * touch them; we walk them now and adjust only file-offset fields, plus the
     * three VM fields that keep every address fixed. */
    lcp = buf + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        switch (lc->cmd) {
        case LC_SEGMENT_64: {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            /* Identify segments by criteria, not by a saved pointer: realloc
             * above may have moved the buffer, invalidating the pointers found
             * during validation. The header-bearing segment is the one mapped
             * at file offset 0 with content (i.e. __TEXT, not __PAGEZERO). */
            if (strcmp(seg->segname, "__PAGEZERO") == 0) {
                seg->vmsize -= grow;            /* donate space below __TEXT */
            } else if (seg->fileoff == 0 && seg->filesize > 0) {
                seg->vmaddr  -= grow;           /* lower the image base */
                seg->vmsize  += grow;
                seg->filesize += grow;          /* fileoff stays 0 */
            } else if (seg->fileoff >= insert) {
                seg->fileoff += grow;           /* later segment: file moves, vm fixed */
            }
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++) {
                mg_bump(&sect[j].offset, insert, grow);   /* addr stays fixed */
                if (sect[j].reloff) mg_bump(&sect[j].reloff, insert, grow);
            }
            break;
        }
        case LC_SYMTAB: {
            struct symtab_command *c = (struct symtab_command *)lcp;
            mg_bump(&c->symoff, insert, grow);
            mg_bump(&c->stroff, insert, grow);
            break;
        }
        case LC_DYSYMTAB: {
            struct dysymtab_command *c = (struct dysymtab_command *)lcp;
            mg_bump(&c->tocoff, insert, grow);
            mg_bump(&c->modtaboff, insert, grow);
            mg_bump(&c->extrefsymoff, insert, grow);
            mg_bump(&c->indirectsymoff, insert, grow);
            mg_bump(&c->extreloff, insert, grow);
            mg_bump(&c->locreloff, insert, grow);
            break;
        }
        case LC_DYLD_INFO:
        case LC_DYLD_INFO_ONLY: {
            struct dyld_info_command *c = (struct dyld_info_command *)lcp;
            mg_bump(&c->rebase_off, insert, grow);
            mg_bump(&c->bind_off, insert, grow);
            mg_bump(&c->weak_bind_off, insert, grow);
            mg_bump(&c->lazy_bind_off, insert, grow);
            mg_bump(&c->export_off, insert, grow);
            break;
        }
        case LC_MAIN: {
            /* entryoff is a file offset within __TEXT; bumping it keeps the
             * entry's vm address fixed (base went down by the same amount). */
            struct entry_point_command *c = (struct entry_point_command *)lcp;
            uint32_t e = (uint32_t)c->entryoff;
            mg_bump(&e, insert, grow);
            c->entryoff = e;
            break;
        }
        case LC_FUNCTION_STARTS:
        case LC_DATA_IN_CODE:
        case LC_CODE_SIGNATURE:
        case LC_SEGMENT_SPLIT_INFO:
        case LC_DYLIB_CODE_SIGN_DRS:
        case LC_LINKER_OPTIMIZATION_HINT:
        case LC_DYLD_EXPORTS_TRIE:
        case LC_DYLD_CHAINED_FIXUPS: {
            struct linkedit_data_command *c = (struct linkedit_data_command *)lcp;
            mg_bump(&c->dataoff, insert, grow);
            break;
        }
        default:
            break;  /* LC_LOAD_DYLIB/DYLINKER/UUID/VERSION_MIN carry no file offsets */
        }
        lcp += lc->cmdsize;
    }

    /* Re-point the dyld4 initializer offsets: the base dropped by `grow`, the
     * constructors did not move, so each offset must gain `grow`. Section file
     * offsets were bumped in the walk above, so these read from the new home.
     * The pre-mutation audit proved this cannot overflow. */
    if (mg_init_offsets_pass(buf, fsize + grow, grow, 1) != 0) {
        fprintf(stderr, "macho_grow: internal error patching S_INIT_FUNC_OFFSETS after "
                        "passing the pre-check\n");
        mg_snapshot_free(&snap);
        return -1;
    }

    /* Re-encode the base-relative LC_FUNCTION_STARTS leading delta: the base
     * dropped by `grow`, so the first delta must gain `grow` to keep every
     * function's absolute address fixed. The blob moved with the memmove; it now
     * lives at fs_dataoff+grow. The pre-mutation check above already proved the
     * width is preserved, so this cannot widen — a nonzero return is a bug. */
    if (fs_dataoff && fs_datasize) {
        int r = mg_reencode_funcstarts_base(buf + fs_dataoff + grow, fs_datasize, grow);
        if (r != 1) {
            fprintf(stderr, "macho_grow: internal error re-encoding function-starts "
                            "leading delta (r=%d) after passing the width pre-check\n", r);
            mg_snapshot_free(&snap);
            return -1;
        }
    }

    /* Phase 4: prove it. Every base-relative structure must resolve exactly where
     * it did before. A mismatch means a handler did not run, ran twice, or ran
     * with the wrong delta -- all of which produce a binary that loads and is
     * wrong, so this is the last chance to catch it. */
    if (mg_verify(buf, fsize + grow, &snap) != 0) {
        mg_snapshot_free(&snap);
        /* The buffer has been transformed and is NOT safe to write. *pbuf already
         * points at it (set right after the realloc) so the caller can free it;
         * the nonzero return says: discard it. */
        return -1;
    }
    mg_snapshot_free(&snap);

    *pfsize = fsize + grow;   /* *pbuf was set right after the realloc */
    return 0;
}

#endif /* MACHO_GROW_H */
