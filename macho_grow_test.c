/*
 * macho_grow_test.c — hermetic tests for the LC_FUNCTION_STARTS base re-encode
 * that macho_grow performs when it lowers the image base.
 *
 * THE bug this pins: change_dylib -grow lowers __TEXT.vmaddr by N to make header
 * room while keeping every section's VM address fixed. LC_FUNCTION_STARTS encodes
 * its FIRST delta relative to the image base, so after the grow that delta is N
 * too small and every function address avxemu reconstructs is N low — it then
 * can't map faulting instructions to functions and declines to patch them (a
 * SIGILL storm). The fix: add N to the leading delta, preserving its byte width
 * so the blob size is unchanged.
 *
 * Ground truth here is hand-computed (small ULEB values, synthetic function
 * address lists), so the test is host-agnostic. Build:
 *   clang -O2 -Wno-unused-function -o /tmp/mgtest macho_grow_test.c && /tmp/mgtest
 */
#include "macho_grow.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(cond, msg, ...) do { if (!(cond)) { \
    printf("FAIL: " msg "\n", ##__VA_ARGS__); fails++; } } while (0)

/* ---- ULEB128 primitives ---- */
static void test_uleb_decode(void) {
    uint64_t v; int n;
    uint8_t a[] = {0x00};                 n = mg_uleb_decode(a, a+1, &v); CHECK(n==1 && v==0,      "uleb 0x00 -> 0 (got n=%d v=%llu)", n, (unsigned long long)v);
    uint8_t b[] = {0x7f};                 n = mg_uleb_decode(b, b+1, &v); CHECK(n==1 && v==127,    "uleb 0x7f -> 127");
    uint8_t c[] = {0x80,0x01};            n = mg_uleb_decode(c, c+2, &v); CHECK(n==2 && v==128,    "uleb 80 01 -> 128");
    uint8_t d[] = {0xc0,0x15};            n = mg_uleb_decode(d, d+2, &v); CHECK(n==2 && v==2752,   "uleb c0 15 -> 2752 (the 2.1.227 leading delta)");
    uint8_t e[] = {0xff,0x7f};            n = mg_uleb_decode(e, e+2, &v); CHECK(n==2 && v==16383,  "uleb ff 7f -> 16383");
    uint8_t f[] = {0x80,0x80,0x01};       n = mg_uleb_decode(f, f+3, &v); CHECK(n==3 && v==16384,  "uleb 80 80 01 -> 16384");
    /* runs off the end (continuation bit set, no more bytes) -> malformed */
    uint8_t g[] = {0x80};                 n = mg_uleb_decode(g, g+1, &v); CHECK(n==0,              "uleb truncated -> 0 (got n=%d)", n);
}

static void test_uleb_minlen(void) {
    CHECK(mg_uleb_minlen(0)==1,       "minlen(0)=1");
    CHECK(mg_uleb_minlen(127)==1,     "minlen(127)=1");
    CHECK(mg_uleb_minlen(128)==2,     "minlen(128)=2");
    CHECK(mg_uleb_minlen(2752)==2,    "minlen(2752)=2");
    CHECK(mg_uleb_minlen(16383)==2,   "minlen(16383)=2");
    CHECK(mg_uleb_minlen(16384)==3,   "minlen(16384)=3");
    CHECK(mg_uleb_minlen(6848)==2,    "minlen(6848)=2  (2752 + one page)");
}

static void test_uleb_encode_fixed(void) {
    uint8_t buf[8]; uint64_t v; int n;
    /* minimal width */
    CHECK(mg_uleb_encode_fixed(buf, 6848, 2)==1, "encode 6848 in 2 bytes ok");
    n = mg_uleb_decode(buf, buf+2, &v); CHECK(n==2 && v==6848, "  round-trips to 6848");
    CHECK(buf[0]==0xc0 && buf[1]==0x35, "  bytes are c0 35 (expected 2.1.227 fixed leading delta)");
    /* non-minimal padding: 2752 forced into 3 bytes */
    memset(buf,0xAA,sizeof buf);
    CHECK(mg_uleb_encode_fixed(buf, 2752, 3)==1, "encode 2752 padded to 3 bytes ok");
    n = mg_uleb_decode(buf, buf+3, &v); CHECK(n==3 && v==2752, "  padded still decodes to 2752 in 3 bytes");
    /* does not fit: 16384 needs 3, width 2 -> refuse */
    CHECK(mg_uleb_encode_fixed(buf, 16384, 2)==0, "encode 16384 in 2 bytes refused");
}

/* ---- the leading-delta re-encode ---- */
static void test_reencode_same_width(void) {
    /* leading delta 2752 (c0 15) + a tail that must be preserved verbatim */
    uint8_t blob[] = {0xc0,0x15, /*tail*/ 0x50, 0x81,0x01, 0x00};
    uint8_t saved[sizeof blob]; memcpy(saved, blob, sizeof blob);
    int r = mg_reencode_funcstarts_base(blob, sizeof blob, 0x1000);
    CHECK(r==1, "reencode +0x1000 succeeds in place (got %d)", r);
    CHECK(blob[0]==0xc0 && blob[1]==0x35, "leading delta became c0 35 (2752+4096=6848)");
    CHECK(memcmp(blob+2, saved+2, sizeof blob - 2)==0, "tail bytes untouched");
}

static void test_reencode_widen_refuses(void) {
    /* leading delta 16000 (0x3e80): 16000+4096=20096 needs 3 bytes, was 2 -> refuse */
    uint8_t blob[] = {0x80,0x7d, /*tail*/ 0x40, 0x00};   /* 0x80,0x7d = 16000 */
    uint64_t chk; int n = mg_uleb_decode(blob, blob+2, &chk);
    CHECK(n==2 && chk==16000, "precondition: leading delta decodes to 16000");
    uint8_t saved[sizeof blob]; memcpy(saved, blob, sizeof blob);
    int r = mg_reencode_funcstarts_base(blob, sizeof blob, 0x1000);
    CHECK(r==0, "reencode refuses when the delta would widen (got %d)", r);
    CHECK(memcmp(blob, saved, sizeof blob)==0, "blob left untouched on refusal");
}

static void test_reencode_nonminimal_original_preserved(void) {
    /* leading delta 2752 encoded NON-minimally in 3 bytes (c0 95 00); +0x1000 must
     * stay 3 bytes and still decode correctly. */
    uint8_t blob[] = {0xc0,0x95,0x00, /*tail*/ 0x50, 0x00};
    int r = mg_reencode_funcstarts_base(blob, sizeof blob, 0x1000);
    CHECK(r==1, "reencode of a non-minimally-encoded leading delta succeeds");
    uint64_t v; int n = mg_uleb_decode(blob, blob+3, &v);
    CHECK(n==3 && v==6848, "leading delta still 3 bytes, decodes to 6848 (got n=%d v=%llu)", n, (unsigned long long)v);
    CHECK(blob[3]==0x50, "tail preserved");
}

static void test_reencode_malformed(void) {
    uint8_t empty[1] = {0};
    CHECK(mg_reencode_funcstarts_base(empty, 0, 0x1000)==-1, "empty blob -> -1");
    uint8_t trunc[] = {0x80};   /* continuation with no successor */
    CHECK(mg_reencode_funcstarts_base(trunc, 1, 0x1000)==-1, "truncated leading ULEB -> -1");
}

/* ---- THE INVARIANT: the grow must move no function ---- */
static void build_funcstarts(uint8_t *out, int *outlen, uint64_t base,
                             const uint64_t *addrs, int n) {
    int len = 0; uint64_t prev = base;
    for (int i = 0; i < n; i++) {
        int w = mg_uleb_minlen(addrs[i] - prev);
        mg_uleb_encode_fixed(out + len, addrs[i] - prev, w);
        len += w; prev = addrs[i];
    }
    out[len++] = 0x00;   /* terminator */
    *outlen = len;
}

static void test_invariant_addresses_preserved(void) {
    const uint64_t base = 0x100000000ull;
    const uint32_t N = 0x1000;
    /* a realistic ascending function list; first delta 0xac0 stays 2 bytes under +N */
    uint64_t addrs[] = { base+0xac0, base+0xb30, base+0x1200, base+0x1abc, base+0x2f00 };
    int n = (int)(sizeof addrs / sizeof addrs[0]);

    uint8_t blob[64]; int blen; build_funcstarts(blob, &blen, base, addrs, n);
    uint32_t orig_blen = (uint32_t)blen;

    /* grow: lower the base by N and re-encode the leading delta */
    int r = mg_reencode_funcstarts_base(blob, (uint32_t)blen, N);
    CHECK(r==1, "invariant setup: reencode succeeds");
    CHECK((uint32_t)blen == orig_blen, "blob size unchanged by reencode");

    /* decode at the LOWERED base; every absolute address must be identical */
    uint64_t got[16]; int gn = mg_funcstarts_decode(blob, (uint32_t)blen, base - N, got, 16);
    CHECK(gn == n, "same function count after grow (got %d want %d)", gn, n);
    for (int i = 0; i < n && i < gn; i++)
        CHECK(got[i] == addrs[i], "function[%d] address preserved: got %#llx want %#llx",
              i, (unsigned long long)got[i], (unsigned long long)addrs[i]);
}

/* ---- __TEXT,__init_offsets re-base ----
 * Entries are offsets from the mach header, so lowering the base leaves them
 * all `grow` too small. Sections are matched by TYPE (S_INIT_FUNC_OFFSETS)
 * rather than by name: the name is a linker convention, the type is what the
 * format guarantees. Driven against a synthetic image because the 10.9
 * toolchain cannot emit an __init_offsets section to build a fixture from. */
static void test_init_offsets_rebase(void) {
    static uint8_t img[8192];
    memset(img, 0, sizeof img);
    struct mach_header_64 *h = (struct mach_header_64 *)img;
    h->magic = MH_MAGIC_64;
    h->ncmds = 1;
    struct segment_command_64 *seg = (struct segment_command_64 *)(img + sizeof *h);
    seg->cmd = LC_SEGMENT_64;
    seg->cmdsize = sizeof(*seg) + sizeof(struct section_64);
    strcpy(seg->segname, "__TEXT");
    seg->nsects = 1;
    h->sizeofcmds = seg->cmdsize;
    struct section_64 *s = (struct section_64 *)((uint8_t *)seg + sizeof *seg);
    strncpy(s->sectname, "__init_offsets", sizeof s->sectname);
    strncpy(s->segname, "__TEXT", sizeof s->segname);
    s->offset = 4096;
    s->size = 3 * sizeof(uint32_t);
    s->flags = S_INIT_FUNC_OFFSETS;
    uint32_t *e = (uint32_t *)(img + 4096);
    e[0] = 0x1000; e[1] = 0x2000; e[2] = 0x3000;

    CHECK(mg_init_offsets_pass(img, sizeof img, 0x1000, 1) == 0, "init_offsets patch returns 0");
    CHECK(e[0] == 0x2000 && e[1] == 0x3000 && e[2] == 0x4000,
          "every entry gained grow (got %u %u %u)", e[0], e[1], e[2]);

    /* A section of another type must be left alone, even named __init_offsets. */
    s->flags = S_REGULAR;
    e[0] = 0x1000;
    CHECK(mg_init_offsets_pass(img, sizeof img, 0x1000, 1) == 0 && e[0] == 0x1000,
          "sections of other types untouched (got %u)", e[0]);

    /* An entry that would wrap is refused by the audit, before anything moves. */
    s->flags = S_INIT_FUNC_OFFSETS;
    e[0] = 0xffffffffu;
    CHECK(mg_init_offsets_pass(img, sizeof img, 0x1000, 0) == -1, "overflowing entry refused");
}

/* ---- the whole grow, end to end ----
 * Every other case here calls one helper directly. That is how a duplicated
 * re-base survived review: two functions each added `grow` to the same
 * __init_offsets entries, mg_grow_header called both, and no test ran the path
 * that used them. This builds the smallest image mg_grow_header will accept and
 * checks the entries afterwards, so any second application shows up as 2*grow.
 *
 * Deliberately carries no LC_FUNCTION_STARTS and no export trie: both are
 * audited separately, and leaving them out keeps this about the one structure.
 */
/* opts: MG_T_DICE adds an LC_DATA_IN_CODE whose entries are base-relative;
 * MG_T_UNWIND adds a __TEXT,__unwind_info section. macho_grow rebases neither,
 * so a grow of an image carrying either must refuse rather than corrupt it. */
/* offsets within the synthetic __unwind_info section */
#define UW_PERS_OFF  28
#define UW_IDX_OFF   32
#define UW_LSDA_OFF  56
#define UW_LSDA_END  64
#define UW_PAGE_OFF  72
#define UW_ENT_OFF   80
#define UW32(b, secoff, off) (*(uint32_t *)((b) + (secoff) + (off)))

#define MG_T_DICE   1
#define MG_T_UNWIND 2
#define MG_T_TRIE   4
#define MG_T_UNKNOWN_LC 8    /* a load command we have never classified */
#define MG_T_LOH   16    /* LC_LINKER_OPTIMIZATION_HINT: base-relative, unhandled */
#define MG_T_ODDSECT 32  /* a section whose TYPE we do not know */
#define TRIE_OFF    7168
static uint8_t *build_image(size_t *fsize_out, uint32_t *sect_off_out, int opts) {
    const size_t fsize = 8192;
    uint8_t *buf = (uint8_t *)calloc(1, fsize);

    struct mach_header_64 *h = (struct mach_header_64 *)buf;
    h->magic = MH_MAGIC_64;
    h->filetype = MH_EXECUTE;
    h->flags = MH_PIE;
    h->ncmds = 2;

    struct segment_command_64 *pz = (struct segment_command_64 *)(buf + sizeof *h);
    pz->cmd = LC_SEGMENT_64;
    pz->cmdsize = sizeof *pz;
    strcpy(pz->segname, "__PAGEZERO");
    pz->vmaddr = 0;
    pz->vmsize = 0x100000000ull;   /* room to lower the base into */
    pz->fileoff = 0;
    pz->filesize = 0;              /* filesize 0 keeps it out of the __TEXT probe */

    struct segment_command_64 *tx = (struct segment_command_64 *)((uint8_t *)pz + pz->cmdsize);
    tx->cmd = LC_SEGMENT_64;
    tx->cmdsize = sizeof *tx + (opts & MG_T_UNWIND ? 2 : 1) * sizeof(struct section_64);
    strcpy(tx->segname, "__TEXT");
    tx->vmaddr = 0x100000000ull;
    tx->vmsize = fsize;
    tx->fileoff = 0;
    tx->filesize = fsize;
    tx->nsects = (opts & MG_T_UNWIND) ? 2 : 1;

    struct section_64 *sc = (struct section_64 *)((uint8_t *)tx + sizeof *tx);
    strncpy(sc->sectname, "__init_offsets", sizeof sc->sectname);
    strncpy(sc->segname, "__TEXT", sizeof sc->segname);
    sc->addr = 0x100001000ull;
    sc->size = 2 * sizeof(uint32_t);
    sc->offset = 4096;
    sc->flags = S_INIT_FUNC_OFFSETS;   /* a real one carries both name and type */

    h->sizeofcmds = (uint32_t)(pz->cmdsize + tx->cmdsize);

    if (opts & MG_T_UNWIND) {
        struct section_64 *uw = sc + 1;
        strncpy(uw->sectname, "__unwind_info", sizeof uw->sectname);
        strncpy(uw->segname,  "__TEXT",        sizeof uw->segname);
        uw->addr = 0x100002000ull;
        uw->size = 128;
        uw->offset = 5120;
        uw->flags = S_REGULAR;   /* the TYPE says nothing here; the NAME is what matters */

        /* A compact-unwind section with one of every field family, so the test
         * can tell a handler that bumps the right things from one that bumps
         * everything. Layout mirrors the real format. */
        uint32_t *h32 = (uint32_t *)(buf + uw->offset);
        h32[0] = 1;                 /* version */
        h32[1] = 0;                 /* commonEncodingsArraySectionOffset */
        h32[2] = 0;                 /* commonEncodingsArrayCount */
        h32[3] = UW_PERS_OFF;       /* personalityArraySectionOffset */
        h32[4] = 1;                 /* personalityArrayCount */
        h32[5] = UW_IDX_OFF;        /* indexSectionOffset */
        h32[6] = 2;                 /* indexCount (one real entry + the sentinel) */

        UW32(buf, uw->offset, UW_PERS_OFF)      = 0x9000;   /* base-relative -> GOT */

        UW32(buf, uw->offset, UW_IDX_OFF + 0)   = 0x1000;   /* functionOffset  BASE-REL */
        UW32(buf, uw->offset, UW_IDX_OFF + 4)   = UW_PAGE_OFF; /* page   section-rel */
        UW32(buf, uw->offset, UW_IDX_OFF + 8)   = UW_LSDA_OFF; /* lsda   section-rel */
        UW32(buf, uw->offset, UW_IDX_OFF + 12)  = 0x8000;   /* sentinel fnOff  BASE-REL */
        UW32(buf, uw->offset, UW_IDX_OFF + 16)  = 0;        /* sentinel has no page */
        UW32(buf, uw->offset, UW_IDX_OFF + 20)  = UW_LSDA_END;

        UW32(buf, uw->offset, UW_LSDA_OFF + 0)  = 0x1100;   /* lsda functionOffset BASE-REL */
        UW32(buf, uw->offset, UW_LSDA_OFF + 4)  = 0x7000;   /* lsdaOffset          BASE-REL */

        UW32(buf, uw->offset, UW_PAGE_OFF + 0)  = 3;        /* kind = COMPRESSED */
        *(uint16_t *)(buf + uw->offset + UW_PAGE_OFF + 4) = 8;  /* entryPageOffset */
        *(uint16_t *)(buf + uw->offset + UW_PAGE_OFF + 6) = 2;  /* entryCount */
        /* Compressed entries: low 24 bits are a DELTA from this page's own
         * first-level functionOffset. Invariant under a uniform bump -- bumping
         * them is the silent corruption this test exists to catch. */
        UW32(buf, uw->offset, UW_ENT_OFF + 0)   = 0x00000010u | (1u << 24);
        UW32(buf, uw->offset, UW_ENT_OFF + 4)   = 0x00000040u | (2u << 24);
    }

    uint8_t *lcend = (uint8_t *)tx + tx->cmdsize;

    if (opts & MG_T_DICE) {
        struct linkedit_data_command *dc = (struct linkedit_data_command *)lcend;
        dc->cmd = LC_DATA_IN_CODE;
        dc->cmdsize = sizeof *dc;
        dc->dataoff = 6144;
        dc->datasize = 16;       /* two 8-byte entries */
        h->ncmds++; h->sizeofcmds += dc->cmdsize; lcend += dc->cmdsize;
        /* two data_in_code_entry: { uint32 offset; uint16 length; uint16 kind }.
         * Only `offset` is base-relative; length and kind must survive intact. */
        UW32(buf, dc->dataoff, 0) = 0x1500;
        *(uint16_t *)(buf + dc->dataoff +  4) = 0x20;
        *(uint16_t *)(buf + dc->dataoff +  6) = 4;      /* DICE_KIND_JUMP_TABLE32 */
        UW32(buf, dc->dataoff, 8) = 0x2500;
        *(uint16_t *)(buf + dc->dataoff + 12) = 0x40;
        *(uint16_t *)(buf + dc->dataoff + 14) = 4;
    }

    if (opts & MG_T_TRIE) {
        struct dyld_info_command *di = (struct dyld_info_command *)lcend;
        di->cmd = LC_DYLD_INFO_ONLY;
        di->cmdsize = sizeof *di;
        di->export_off = TRIE_OFF;
        di->export_size = 17;
        h->ncmds++; h->sizeofcmds += di->cmdsize; lcend += di->cmdsize;

        /* A hand-built export trie, 17 bytes:
         *   root: no terminal, two children "A" -> 8, "B" -> 13
         *   node A: terminal, flags 0, address 0x1000 (2-byte ULEB)
         *   node B: terminal, flags 0, address 0 -- the __mh_execute_header
         *           case, which names the header and must STAY 0. */
        static const uint8_t trie[17] = {
            0x00, 0x02,
            'A', 0x00, 8,
            'B', 0x00, 13,
            0x03, 0x00, 0x80, 0x20, 0x00,     /* A: termsz 3, flags 0, addr 0x1000, 0 kids */
            0x02, 0x00, 0x00, 0x00            /* B: termsz 2, flags 0, addr 0,      0 kids */
        };
        memcpy(buf + TRIE_OFF, trie, sizeof trie);
    }

    if (opts & MG_T_UNKNOWN_LC) {
        struct load_command *xc = (struct load_command *)lcend;
        xc->cmd = 0x7fff;                 /* not a real load command */
        xc->cmdsize = sizeof *xc;
        h->ncmds++; h->sizeofcmds += xc->cmdsize; lcend += xc->cmdsize;
    }
    if (opts & MG_T_LOH) {
        struct linkedit_data_command *lc2 = (struct linkedit_data_command *)lcend;
        lc2->cmd = LC_LINKER_OPTIMIZATION_HINT;
        lc2->cmdsize = sizeof *lc2;
        lc2->dataoff = 6656; lc2->datasize = 8;
        h->ncmds++; h->sizeofcmds += lc2->cmdsize; lcend += lc2->cmdsize;
    }
    if (opts & MG_T_ODDSECT) sc->flags = 0x7e;   /* unknown SECTION_TYPE */

    uint32_t *e = (uint32_t *)(buf + sc->offset);
    e[0] = 0x1000; e[1] = 0x2000;

    *fsize_out = fsize;
    *sect_off_out = sc->offset;
    return buf;
}

static uint8_t *build_growable_image(size_t *fsize_out, uint32_t *sect_off_out) {
    return build_image(fsize_out, sect_off_out, 0);
}

/* After a grow, find __init_offsets again -- its file offset moved with the data. */
static uint32_t *find_init_offsets(uint8_t *buf) {
    struct mach_header_64 *h = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof *seg);
            for (uint32_t j = 0; j < seg->nsects; j++)
                if ((sect[j].flags & SECTION_TYPE) == S_INIT_FUNC_OFFSETS)
                    return (uint32_t *)(buf + sect[j].offset);
        }
        lcp += lc->cmdsize;
    }
    return NULL;
}

static void test_grow_applies_init_offsets_once(void) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_growable_image(&fsize, &sect_off);
    const uint32_t grow = 0x1000;

    int r = mg_grow_header(&buf, &fsize, grow);
    CHECK(r == 0, "mg_grow_header succeeds on the synthetic image (got %d)", r);
    if (r != 0) { free(buf); return; }

    uint32_t *e = find_init_offsets(buf);
    CHECK(e != NULL, "__init_offsets still locatable after the grow");
    if (e) {
        CHECK(e[0] == 0x1000 + grow, "entry 0 gained grow exactly once: got %#x want %#x",
              e[0], 0x1000 + grow);
        CHECK(e[1] == 0x2000 + grow, "entry 1 gained grow exactly once: got %#x want %#x",
              e[1], 0x2000 + grow);
    }
    free(buf);
}

/* ---- refuse what we cannot rebase ----
 * Both structures below store offsets from the image base, exactly like
 * __init_offsets and the function-starts leading delta. macho_grow relocates
 * LC_DATA_IN_CODE's blob but never rewrites the offsets inside it, and does not
 * mention __unwind_info at all. Until handlers exist, growing such an image MUST
 * fail: a silent success ships a binary whose data-in-code ranges and
 * compact-unwind entries are all `grow` bytes low, which nothing notices until
 * something unwinds. A refusal leaves the caller's buffer byte-identical.
 */
static void check_refused_unchanged(const char *what, int opts) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_image(&fsize, &sect_off, opts);
    size_t fsize0 = fsize;
    uint8_t *before = (uint8_t *)malloc(fsize0);
    memcpy(before, buf, fsize0);

    int r = mg_grow_header(&buf, &fsize, 0x1000);
    CHECK(r == -1, "%s: mg_grow_header refuses (got %d)", what, r);
    CHECK(fsize == fsize0, "%s: size unchanged on refusal (got %zu want %zu)",
          what, fsize, fsize0);
    if (fsize == fsize0)
        CHECK(memcmp(before, buf, fsize0) == 0,
              "%s: buffer byte-identical on refusal", what);
    free(before);
    free(buf);
}

static uint8_t *find_dice(uint8_t *buf) {
    struct mach_header_64 *h = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_DATA_IN_CODE)
            return buf + ((struct linkedit_data_command *)lcp)->dataoff;
        lcp += lc->cmdsize;
    }
    return NULL;
}

/* Every entry's `offset` is measured from the image base; `length` and `kind`
 * are not offsets at all. A handler that treats the entry as three bumpable
 * words would pass a "did it change" test and corrupt every range. */
static void test_grow_rebases_data_in_code(void) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_image(&fsize, &sect_off, MG_T_DICE);
    const uint32_t g = 0x1000;

    int r = mg_grow_header(&buf, &fsize, g);
    CHECK(r == 0, "grow succeeds on an image with LC_DATA_IN_CODE (got %d)", r);
    if (r != 0) { free(buf); return; }

    uint8_t *d = find_dice(buf);
    CHECK(d != NULL, "LC_DATA_IN_CODE still locatable after the grow");
    if (!d) { free(buf); return; }
    CHECK(*(uint32_t *)(d + 0) == 0x1500 + g, "entry 0 offset gains grow: got %#x",
          *(uint32_t *)(d + 0));
    CHECK(*(uint32_t *)(d + 8) == 0x2500 + g, "entry 1 offset gains grow: got %#x",
          *(uint32_t *)(d + 8));
    CHECK(*(uint16_t *)(d +  4) == 0x20 && *(uint16_t *)(d +  6) == 4,
          "entry 0 length/kind UNTOUCHED");
    CHECK(*(uint16_t *)(d + 12) == 0x40 && *(uint16_t *)(d + 14) == 4,
          "entry 1 length/kind UNTOUCHED");
    free(buf);
}

/* After a grow, __unwind_info's file offset moved with the data. */
static uint8_t *find_unwind(uint8_t *buf) {
    struct mach_header_64 *h = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof *seg);
            for (uint32_t j = 0; j < seg->nsects; j++)
                if (strncmp(sect[j].sectname, "__unwind_info", 16) == 0)
                    return buf + sect[j].offset;
        }
        lcp += lc->cmdsize;
    }
    return NULL;
}

/* The handler must bump the four base-relative field families and leave the
 * compressed second-level entries ALONE -- those are deltas from their own
 * page's first-level functionOffset, so a uniform bump leaves them correct and
 * bumping them corrupts the tables silently. */
static void test_grow_rebases_unwind_info(void) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_image(&fsize, &sect_off, MG_T_UNWIND);
    const uint32_t g = 0x1000;

    int r = mg_grow_header(&buf, &fsize, g);
    CHECK(r == 0, "grow succeeds on an image with __unwind_info (got %d)", r);
    if (r != 0) { free(buf); return; }

    uint8_t *u = find_unwind(buf);
    CHECK(u != NULL, "__unwind_info still locatable after the grow");
    if (!u) { free(buf); return; }
    uint32_t *at = (uint32_t *)u;
#define UW_IS(off, want, what) \
    CHECK(*(uint32_t *)(u + (off)) == (uint32_t)(want), \
          "%s: got %#x want %#x", what, *(uint32_t *)(u + (off)), (uint32_t)(want))

    UW_IS(UW_PERS_OFF,      0x9000 + g, "personality entry gains grow");
    UW_IS(UW_IDX_OFF + 0,   0x1000 + g, "first-level functionOffset gains grow");
    UW_IS(UW_IDX_OFF + 12,  0x8000 + g, "sentinel functionOffset gains grow");
    UW_IS(UW_LSDA_OFF + 0,  0x1100 + g, "LSDA functionOffset gains grow");
    UW_IS(UW_LSDA_OFF + 4,  0x7000 + g, "LSDA lsdaOffset gains grow");

    /* section-relative fields must NOT move */
    UW_IS(UW_IDX_OFF + 4,  UW_PAGE_OFF, "page section-offset unchanged");
    UW_IS(UW_IDX_OFF + 8,  UW_LSDA_OFF, "LSDA section-offset unchanged");
    UW_IS(3 * 4,           UW_PERS_OFF, "personality section-offset unchanged");

    /* THE trap: compressed entries are deltas and must be untouched */
    UW_IS(UW_ENT_OFF + 0, 0x00000010u | (1u << 24), "compressed entry 0 UNTOUCHED");
    UW_IS(UW_ENT_OFF + 4, 0x00000040u | (2u << 24), "compressed entry 1 UNTOUCHED");
    (void)at;
#undef UW_IS
    free(buf);
}

/* ---- mg_verify: the grow must move nothing ----
 * The invariant is not "the entries changed by grow", it is "the RESOLVED
 * addresses did not change". Stating it that way is what makes the check catch
 * bugs it was not written for: a handler that never ran, one that ran twice
 * (PR #10 -- two correct __init_offsets re-basers met in a merge and composed
 * into 2*grow), or one that ran with the wrong delta all look the same to it.
 *
 * The two failing cases below are the point. A verify that cannot fail is not a
 * verify, so each one perturbs the grown image by exactly one handler's worth of
 * work and asserts mg_verify rejects it.
 */
static void test_verify_accepts_a_correct_grow(void) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_growable_image(&fsize, &sect_off);
    mg_snapshot snap;
    CHECK(mg_snapshot_take(buf, fsize, &snap) == 0, "snapshot taken before the grow");
    CHECK(snap.n == 2, "snapshot found both __init_offsets entries (got %u)", snap.n);

    int r = mg_grow_header(&buf, &fsize, 0x1000);
    CHECK(r == 0, "grow succeeds (got %d)", r);
    if (r == 0)
        CHECK(mg_verify(buf, fsize, &snap) == 0, "verify ACCEPTS a correct grow");
    mg_snapshot_free(&snap);
    free(buf);
}

/* Perturb every __init_offsets entry by `delta` after a correct grow, then
 * demand mg_verify notices. delta=+grow is the double-apply; -grow is a handler
 * that never ran. */
static void check_verify_rejects(const char *what, int32_t delta) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_growable_image(&fsize, &sect_off);
    mg_snapshot snap;
    if (mg_snapshot_take(buf, fsize, &snap) != 0) { free(buf); CHECK(0, "%s: snapshot", what); return; }
    if (mg_grow_header(&buf, &fsize, 0x1000) != 0) {
        mg_snapshot_free(&snap); free(buf); CHECK(0, "%s: grow", what); return;
    }
    uint32_t *e = find_init_offsets(buf);
    if (e) { e[0] = (uint32_t)(e[0] + delta); e[1] = (uint32_t)(e[1] + delta); }
    CHECK(mg_verify(buf, fsize, &snap) == -1, "verify REJECTS %s", what);
    mg_snapshot_free(&snap);
    free(buf);
}

static void test_verify_rejects_double_apply(void) {
    check_verify_rejects("a double-applied re-base (the PR #10 defect)", 0x1000);
}

static void test_verify_rejects_handler_that_never_ran(void) {
    check_verify_rejects("a handler that never ran", -0x1000);
}

/* Coverage, not just correctness: a handler is only as safe as verify's
 * willingness to contradict it. If mg_collect ever stops walking compact unwind,
 * the count assertion fails here rather than silently going unwatched. */
static void test_verify_watches_unwind_info(void) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_image(&fsize, &sect_off, MG_T_UNWIND);
    mg_snapshot snap;
    CHECK(mg_snapshot_take(buf, fsize, &snap) == 0, "snapshot with unwind taken");
    /* 2 __init_offsets + 1 personality + 2 first-level (incl. sentinel)
     * + 2 LSDA fields = 7. The compressed entries are deltas and must NOT
     * be counted -- if they were, this would be 9. */
    CHECK(snap.n == 7, "verify watches all 7 base-relative unwind+init fields (got %u)", snap.n);

    if (mg_grow_header(&buf, &fsize, 0x1000) != 0) {
        CHECK(0, "grow succeeded"); mg_snapshot_free(&snap); free(buf); return;
    }
    /* Perturb one unwind field the handler is responsible for. */
    uint8_t *u = find_unwind(buf);
    if (u) *(uint32_t *)(u + UW_IDX_OFF) += 4;
    CHECK(mg_verify(buf, fsize, &snap) == -1,
          "verify REJECTS a perturbed first-level functionOffset");
    mg_snapshot_free(&snap);
    free(buf);
}

/* The export trie stores each address as a ULEB offset from the image base. The
 * fix that makes this tractable: adding `grow` never widens the encoding on any
 * real binary (measured across all 670 entries of Claude Code 2.1.263 at 4K, 8K
 * and 16K), so the address is re-encoded at its ORIGINAL byte width and the trie
 * -- and every __LINKEDIT offset after it -- keeps its size.
 *
 * __mh_execute_header is exported at 0 and must stay 0: it names the header,
 * which moved down with the base, so 0 is still correct. */
static uint8_t *find_trie(uint8_t *buf) {
    struct mach_header_64 *h = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_DYLD_INFO_ONLY || lc->cmd == LC_DYLD_INFO)
            return buf + ((struct dyld_info_command *)lcp)->export_off;
        lcp += lc->cmdsize;
    }
    return NULL;
}

static uint32_t trie_size(uint8_t *buf) {
    struct mach_header_64 *h = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof *h;
    for (uint32_t i = 0; i < h->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_DYLD_INFO_ONLY || lc->cmd == LC_DYLD_INFO)
            return ((struct dyld_info_command *)lcp)->export_size;
        lcp += lc->cmdsize;
    }
    return 0;
}

static void test_grow_rebases_export_trie(void) {
    size_t fsize; uint32_t sect_off;
    uint8_t *buf = build_image(&fsize, &sect_off, MG_T_TRIE);
    const uint32_t g = 0x1000;

    int r = mg_grow_header(&buf, &fsize, g);
    CHECK(r == 0, "grow succeeds on an image with an export trie (got %d)", r);
    if (r != 0) { free(buf); return; }

    CHECK(trie_size(buf) == 17, "trie size UNCHANGED (got %u) -- no __LINKEDIT resize",
          trie_size(buf));
    uint8_t *t = find_trie(buf);
    CHECK(t != NULL, "export trie still locatable");
    if (!t) { free(buf); return; }
    /* node A's address, still a 2-byte ULEB at the same place */
    uint64_t a = 0; int n = mg_uleb_decode(t + 10, t + 17, &a);
    CHECK(n == 2, "node A address still encoded in 2 bytes (got %d)", n);
    CHECK(a == 0x1000 + g, "node A address gains grow: got %#llx want %#llx",
          (unsigned long long)a, (unsigned long long)(0x1000 + g));
    CHECK(t[15] == 0x00, "__mh_execute_header-style export STAYS 0 (got %#x)", t[15]);
    free(buf);
}

/* ---- unknown means unsafe ----
 * The handlers above cover what we know. This is about what we do not: a load
 * command or section type nobody classified might carry offsets from the image
 * base exactly as __init_offsets and compact unwind do, and there is no way to
 * tell by looking at a number. Growing anyway is how LC_DATA_IN_CODE and
 * __unwind_info were silently corrupted for months. So the default is refusal,
 * and adding support for something means adding it to the table on purpose.
 */
static void test_grow_refuses_unknown_load_command(void) {
    check_refused_unchanged("an unclassified load command", MG_T_UNKNOWN_LC);
}

/* Known to carry base-relative ULEB payloads, and we do not re-base them.
 * Refusing is the honest answer, not silence. */
static void test_grow_refuses_linker_optimization_hint(void) {
    check_refused_unchanged("LC_LINKER_OPTIMIZATION_HINT", MG_T_LOH);
}

static void test_grow_refuses_unknown_section_type(void) {
    check_refused_unchanged("an unclassified section type", MG_T_ODDSECT);
}

int main(void) {
    test_uleb_decode();
    test_uleb_minlen();
    test_uleb_encode_fixed();
    test_reencode_same_width();
    test_reencode_widen_refuses();
    test_reencode_nonminimal_original_preserved();
    test_reencode_malformed();
    test_invariant_addresses_preserved();
    test_init_offsets_rebase();
    test_grow_applies_init_offsets_once();
    test_grow_rebases_data_in_code();
    test_grow_rebases_export_trie();
    test_grow_refuses_unknown_load_command();
    test_grow_refuses_linker_optimization_hint();
    test_grow_refuses_unknown_section_type();
    test_grow_rebases_unwind_info();
    test_verify_watches_unwind_info();
    test_verify_accepts_a_correct_grow();
    test_verify_rejects_double_apply();
    test_verify_rejects_handler_that_never_ran();
    if (fails) { printf("macho_grow_test: %d FAILURE(S)\n", fails); return 1; }
    printf("macho_grow_test: all cases pass\n");
    return 0;
}
