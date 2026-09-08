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
#define MG_T_DICE   1
#define MG_T_UNWIND 2
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
        uw->size = 64;
        uw->offset = 5120;
        uw->flags = S_REGULAR;   /* the TYPE says nothing here; the NAME is what matters */
    }

    if (opts & MG_T_DICE) {
        struct linkedit_data_command *dc =
            (struct linkedit_data_command *)((uint8_t *)tx + tx->cmdsize);
        dc->cmd = LC_DATA_IN_CODE;
        dc->cmdsize = sizeof *dc;
        dc->dataoff = 6144;
        dc->datasize = 16;       /* two 8-byte entries */
        h->ncmds = 3;
        h->sizeofcmds += dc->cmdsize;
        uint32_t *d = (uint32_t *)(buf + dc->dataoff);
        d[0] = 0x1500; d[2] = 0x2500;   /* entry.offset, base-relative */
    }

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

static void test_grow_refuses_data_in_code(void) {
    check_refused_unchanged("LC_DATA_IN_CODE", MG_T_DICE);
}

static void test_grow_refuses_unwind_info(void) {
    check_refused_unchanged("__TEXT,__unwind_info", MG_T_UNWIND);
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
    test_grow_refuses_data_in_code();
    test_grow_refuses_unwind_info();
    test_verify_accepts_a_correct_grow();
    test_verify_rejects_double_apply();
    test_verify_rejects_handler_that_never_ran();
    if (fails) { printf("macho_grow_test: %d FAILURE(S)\n", fails); return 1; }
    printf("macho_grow_test: all cases pass\n");
    return 0;
}
