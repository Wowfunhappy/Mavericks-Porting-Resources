/*
 * tramp.c — trampoline dispatch, thunk pool, and thunk builder.
 *
 * A "run" is a maximal sequence of consecutive faulting instructions. Each run
 * gets one thunk (tramp.s template + an appended run_record). The patched site
 * jumps to the thunk; the thunk snapshots state into a regfile and calls
 * avxemu_tramp_dispatch(), which emulates each instruction of the run in order.
 */
#include "regfile.h"
#include "decode.h"
#include "vexops.h"
#include "lde.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <cpuid.h>

/* run_record: laid out immediately after the thunk code (see tramp.s). */
typedef struct { uint64_t addr; decoded dec; } tramp_insn;   /* addr feeds rf->rip per instruction */
typedef struct { uint32_t n; uint32_t pad; tramp_insn insns[]; } run_record;

extern char avxemu_tt_start[], avxemu_tt_end[];
extern char avxemu_tt_dispatchptr[], avxemu_tt_resumeptr[], avxemu_tt_record[];

static void emit(const char *s){ (void)write(2, s, strlen(s)); }

/* The actual emulation, run on the side stack. */
static void tramp_emulate_run(const void *recordp, void *rfp) {
    const run_record *r = (const run_record *)recordp;
    avxemu_regfile *rf = (avxemu_regfile *)rfp;
    for (uint32_t i = 0; i < r->n; i++) {
        rf->rip = r->insns[i].addr;
        if (!avxemu_emulate(&r->insns[i].dec, rf)) {
            /* Scanner only trampolines ops we emulate, so this is a bug net. */
            emit("avxemu: tramp dispatch: emulate failed mid-run\n");
            return;
        }
    }
}

/*
 * Per-thread side stack for the emulation. A trampolined instruction can fire at
 * any JS recursion depth; running the (multi-KB) emulation on the program stack
 * would add pressure exactly where JSC is near its limit. Moving it to a private
 * stack means a thunk costs only its tiny register-spill frame on the program
 * stack, never the emulation itself.
 */
#define SIDE_SZ (256u * 1024)
extern void avxemu_run_on_stack(uint8_t *base, size_t sz,
                                void (*fn)(const void *, void *), const void *a, void *b);

/* Per-thread side-stack pointer. pthread TSD (not __thread): getspecific is in
 * the baseline system libs, so it is never trampolined and never mallocs — safe
 * to call from a thunk even one that fired inside Bun's lzcnt-using allocator. */
static pthread_key_t g_side_key;
static int g_side_key_ok = 0;

/* Called from the thunk (tramp.s) with the live state spilled into rf. */
void avxemu_tramp_dispatch(const run_record *r, avxemu_regfile *rf) {
    if (!g_side_key_ok) { tramp_emulate_run(r, rf); return; }   /* no key: program stack */
    uint8_t *side = (uint8_t *)pthread_getspecific(g_side_key);
    if (!side) {
        void *m = mmap(0, SIDE_SZ, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (m == MAP_FAILED) { tramp_emulate_run(r, rf); return; }
        side = (uint8_t *)m;
        pthread_setspecific(g_side_key, side);
    }
    avxemu_run_on_stack(side, SIDE_SZ, tramp_emulate_run, r, rf);
}

/* ---- thunk pool: RWX, bump-allocated. The patcher reaches it with jmp rel32,
 * so it must land within +/-2GB of __text; avxemu_pool_init() takes a hint. ---- */
static uint8_t *g_pool; static size_t g_used, g_cap;

int avxemu_pool_init(void *hint, size_t cap) {
    void *p = mmap(hint, cap, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return 0;
    g_pool = (uint8_t *)p; g_cap = cap; g_used = 0;
    return 1;
}
void *avxemu_pool_base(void){ return g_pool; }
size_t avxemu_pool_used(void){ return g_used; }

/*
 * Build a thunk for a run of n decoded instructions (each with its original
 * address) that resumes at `resume`. Returns the thunk entry, or NULL if the
 * pool is exhausted. Only data slots are written; the template code is verbatim.
 */
void *avxemu_build_thunk(const tramp_insn *insns, int n, uint64_t resume) {
    size_t code  = (size_t)(avxemu_tt_record - avxemu_tt_start);   /* code + 2 ptr slots */
    size_t recsz = sizeof(run_record) + (size_t)n * sizeof(tramp_insn);
    size_t need  = (code + recsz + 15) & ~(size_t)15;
    if (!g_pool || g_used + need > g_cap) return 0;

    uint8_t *t = g_pool + g_used; g_used += need;
    memcpy(t, avxemu_tt_start, code);
    *(void   **)(t + (avxemu_tt_dispatchptr - avxemu_tt_start)) = (void *)avxemu_tramp_dispatch;
    *(uint64_t *)(t + (avxemu_tt_resumeptr   - avxemu_tt_start)) = resume;

    run_record *r = (run_record *)(t + (avxemu_tt_record - avxemu_tt_start));
    r->n = (uint32_t)n; r->pad = 0;
    for (int i = 0; i < n; i++) r->insns[i] = insns[i];
    return t;
}

/* ----------------------------------------------------------------------------
 * Eager installer: at load, rewrite each run of faulting instructions to a jmp
 * into a thunk, so it never traps. Runs single-threaded in the constructor,
 * before the patched code can execute.
 * -------------------------------------------------------------------------- */

static int g_lack_avx2 = 1, g_lack_fma = 1, g_lack_bmi = 1, g_lack_f16c = 0;

static int avx2_only_op(vex_op op) {
    switch (op) {
    case VPSLLVD: case VPSLLVQ: case VPSRLVD: case VPSRLVQ: case VPSRAVD:
    case VPBROADCASTB: case VPBROADCASTW: case VPBROADCASTD: case VPBROADCASTQ:
    case VBROADCASTI128: case VPBLENDD:
    case VEXTRACTI128: case VINSERTI128: case VPERM2I128:
    case VPERMQ: case VPERMD: case VPERMPD: case VPERMPS: return 1;
    default: return 0;
    }
}
/* Does the instruction #UD on this CPU (and so currently go through SIGILL)? */
static int tramp_faults(const decoded *d) {
    if (d->is_bmi) return g_lack_bmi;
    if (d->op >= VFMADD132 && d->op <= VFNMSUB231) return g_lack_fma;
    if (d->op == VCVTPH2PS) return g_lack_f16c;
    if (avx2_only_op(d->op)) return g_lack_avx2;
    return d->wide && g_lack_avx2;        /* base SSE integer op: only 256-bit faults */
}

static void detect_features(void) {
    if (getenv("AVXEMU_FORCETRAMP")) {    /* dev: treat everything emulatable as faulting */
        g_lack_avx2 = g_lack_fma = g_lack_bmi = g_lack_f16c = 1; return;
    }
    unsigned a, b, c, d;
    if (__get_cpuid(1, &a, &b, &c, &d)) { g_lack_fma = !(c & (1u<<12)); g_lack_f16c = !(c & (1u<<29)); }
    unsigned a7 = 0, b7 = 0, c7 = 0, d7 = 0;
    __cpuid_count(7, 0, a7, b7, c7, d7);
    g_lack_avx2 = !(b7 & (1u<<5));
    int bmi1 = (b7 & (1u<<3)) != 0, bmi2 = (b7 & (1u<<8)) != 0;
    unsigned a8, b8, c8, d8; int lz = __get_cpuid(0x80000001u, &a8, &b8, &c8, &d8) && (c8 & (1u<<5));
    g_lack_bmi = !(bmi1 && bmi2 && lz);   /* conservative: any BMI-ish missing -> emulate the family */
}

#define MAXRUN 64

/* Scan one cleanly-decoding function and trampoline its faulting runs. Returns
 * the number of jmps written. Patches go to the live (already-writable) text. */
/* Place the trampoline for one gathered run of faulting instructions: pick the
 * first start whose 5-byte jmp can't corrupt a branch target, build a thunk for
 * [s,ni), and overwrite that start with `jmp thunk`. Returns 1 if patched. */
static long emit_run(uint8_t *text, size_t fstart, size_t fend,
                     const tramp_insn *insns, const size_t *offs, int ni,
                     size_t re, const uint8_t *tgt, int has_indirect) {
    int s = 0;
    while (s < ni) {
        size_t soff = offs[s]; int slen = insns[s].dec.len;
        if (re - soff < 5) { s = ni; break; }            /* can't fit a 5-byte jmp -> rest traps */
        if (slen >= 5) break;                            /* jmp lands inside insn[s]: always safe */
        size_t nb = soff + 4;                            /* slen==4: next insn start is in the jmp */
        if (!has_indirect && (nb >= fend || !tgt[nb - fstart])) break;
        s++;                                              /* unsafe 4-byte start -> it traps; try next */
    }
    if (s < ni && re - offs[s] >= 5) {
        void *thunk = avxemu_build_thunk(insns + s, ni - s, (uint64_t)(text + re));
        if (thunk) {
            uint8_t *site = text + offs[s];
            int64_t rel = (int64_t)((uint8_t *)thunk - (site + 5));
            if (rel >= INT32_MIN && rel <= INT32_MAX) {
                site[0] = 0xE9; int32_t r32 = (int32_t)rel; memcpy(site + 1, &r32, 4);
                return 1;
            }
        }
    }
    return 0;
}

/* Gather the maximal run of physically-consecutive faulting instructions at q
 * (each must also be reachable code when `code` is given), fill insns/offs, and
 * return the run-end offset. Faulting ops never branch, so the run always falls
 * through to real code at the returned offset. */
static size_t gather_run(uint8_t *text, size_t fstart, size_t fend, size_t q,
                         const uint8_t *code, tramp_insn *insns, size_t *offs, int *pni) {
    int ni = 0; size_t p = q;
    while (p < fend && ni < MAXRUN && (!code || code[p - fstart])) {
        int z2, oo; int l2 = x86_len(text + p, text + fend, &z2, &oo);
        if (l2 <= 0) break;
        decoded d2; int dl2 = decode(text + p, &d2);
        if (!(dl2 > 0 && d2.op && tramp_faults(&d2))) break;
        offs[ni] = p; insns[ni].addr = (uint64_t)(text + p); insns[ni].dec = d2;
        ni++; p += l2;
    }
    *pni = ni;
    return p;
}

/*
 * Trampoline one function's faulting runs. A function that decodes cleanly
 * linearly is walked straight through; one that doesn't (embedded jump tables)
 * is mapped by recursive descent so its reachable code is covered too instead of
 * being skipped wholesale. Faulting (AVX2/BMI/FMA) instructions never branch, so
 * a run of physically-consecutive ones always falls through to real code — the
 * resume point is safe in both modes.
 */
static long scan_function(uint8_t *text, size_t fstart, size_t fend, size_t readable) {
    size_t n = fend - fstart;
    uint8_t *tgt = calloc(n, 1);
    if (!tgt) return 0;
    long patched = 0;

    /* pass 1: clean linear decode? collect direct-branch targets + indirect flag */
    int has_indirect = 0, clean = 1;
    size_t q = fstart;
    while (q < fend) {
        if (fend - q <= 15) { int pad = 1; for (size_t r = q; r < fend; r++) if (text[r]&&text[r]!=0xCC&&text[r]!=0x90){pad=0;break;} if (pad) break; }
        int zk, o2; int len = x86_len(text + q, text + fend, &zk, &o2);
        if (len <= 0) { clean = 0; break; }
        int term; long t; int ind;
        lde_cflow(text + q, text + fend, len, (long)q, &term, &t, &ind);
        if (ind) has_indirect = 1;
        if (t >= (long)fstart && t < (long)fend) tgt[t - fstart] = 1;
        q += len;
    }

    if (clean) {
        /* pass 2: walk linearly; trampoline each maximal faulting run */
        q = fstart;
        while (q < fend) {
            if (fend - q <= 15) { int pad = 1; for (size_t r = q; r < fend; r++) if (text[r]&&text[r]!=0xCC&&text[r]!=0x90){pad=0;break;} if (pad) break; }
            int zk, o2; int len = x86_len(text + q, text + fend, &zk, &o2);
            if (len <= 0) break;
            decoded d; int dl = decode(text + q, &d);
            if (!(dl > 0 && d.op && tramp_faults(&d))) { q += len; continue; }
            tramp_insn insns[MAXRUN]; size_t offs[MAXRUN]; int ni;
            size_t re = gather_run(text, fstart, fend, q, 0, insns, offs, &ni);
            patched += emit_run(text, fstart, fend, insns, offs, ni, re, tgt, has_indirect);
            q = re;
        }
    } else {
        /* dirty: recursive-descent reachability map, then trampoline faulting runs
         * inside reachable code only (jump-table data is never visited or patched). */
        uint8_t *code = calloc(n, 1);
        memset(tgt, 0, n);                               /* linear targets past the desync are bogus */
        if (code && lde_rd_map(text, fstart, fend, readable, code, tgt, &has_indirect)) {
            size_t off = 0;
            while (off < n) {
                if (!code[off]) { off++; continue; }
                size_t qq = fstart + off;
                int zk, o2; int len = x86_len(text + qq, text + fend, &zk, &o2);
                decoded d; int dl = (len > 0) ? decode(text + qq, &d) : 0;
                if (!(dl > 0 && d.op && tramp_faults(&d))) { off++; continue; }
                tramp_insn insns[MAXRUN]; size_t offs[MAXRUN]; int ni;
                size_t re = gather_run(text, fstart, fend, qq, code, insns, offs, &ni);
                patched += emit_run(text, fstart, fend, insns, offs, ni, re, tgt, has_indirect);
                off = re - fstart;
            }
        }
        free(code);
    }
    free(tgt);
    return patched;
}

static uint64_t uleb_t(const uint8_t **p, const uint8_t *e) {
    uint64_t r = 0; int s = 0; uint8_t x;
    do { if (*p >= e) return r; x = *(*p)++; r |= (uint64_t)(x & 0x7f) << s; s += 7; } while (x & 0x80);
    return r;
}

/* Install trampolines over the main executable. Returns jmps written. */
long avxemu_install_trampolines(void) {
    detect_features();
    if (!g_lack_avx2 && !g_lack_fma && !g_lack_bmi && !g_lack_f16c) return 0;  /* nothing faults here */

    /* per-thread side-stack key, created here (single-threaded) before any thunk runs */
    if (!g_side_key_ok && pthread_key_create(&g_side_key, 0) == 0) g_side_key_ok = 1;

    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!mh || mh->magic != MH_MAGIC_64) return 0;
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);

    uint64_t tseg_vm = 0, tseg_sz = 0, text_addr = 0, text_size = 0, le_vm = 0, le_off = 0;
    uint32_t fs_off = 0, fs_size = 0;
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (!strcmp(sg->segname, "__TEXT")) {
                tseg_vm = sg->vmaddr; tseg_sz = sg->vmsize;
                const struct section_64 *sc = (const struct section_64 *)(sg + 1);
                for (uint32_t j = 0; j < sg->nsects; j++)
                    if (!strcmp(sc[j].sectname, "__text")) { text_addr = sc[j].addr; text_size = sc[j].size; }
            } else if (!strcmp(sg->segname, "__LINKEDIT")) { le_vm = sg->vmaddr; le_off = sg->fileoff; }
        } else if (lc->cmd == LC_FUNCTION_STARTS) {
            const struct linkedit_data_command *ld = (const struct linkedit_data_command *)lc;
            fs_off = ld->dataoff; fs_size = ld->datasize;
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    if (!text_addr || !fs_off) return 0;

    uint8_t *text = (uint8_t *)(text_addr + slide);
    /* switch-table resolution may read entries in __const after __text */
    size_t readable = (size_t)((tseg_vm + tseg_sz) - text_addr);

    /* thunk pool, placed near __text so jmp rel32 reaches it */
    size_t pool_sz = 96u << 20;
    void *hint = (void *)(((uintptr_t)(text + text_size) + 0x100000) & ~(uintptr_t)0xfff);
    if (!avxemu_pool_init(hint, pool_sz)) return 0;

    /* make __text writable for the patch pass (COPY: __TEXT maxprot lacks write) */
    mach_port_t task = mach_task_self();
    uintptr_t lo = (uintptr_t)text & ~(uintptr_t)0xfff;
    uintptr_t hi = ((uintptr_t)text + text_size + 0xfff) & ~(uintptr_t)0xfff;
    if (vm_protect(task, (vm_address_t)lo, (vm_size_t)(hi - lo), FALSE,
                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS)
        return 0;

    const uint8_t *fp = (const uint8_t *)(le_vm + slide + (fs_off - le_off));
    const uint8_t *fe = fp + fs_size;
    uint64_t addr = tseg_vm; uint64_t prev = 0; long total = 0;
    while (fp < fe) {
        uint64_t delta = uleb_t(&fp, fe); if (!delta) break;
        addr += delta;
        if (prev && prev >= text_addr && prev < text_addr + text_size) {
            uint64_t fend = addr; if (fend > text_addr + text_size) fend = text_addr + text_size;
            total += scan_function(text, prev - text_addr, fend - text_addr, readable);
        }
        prev = addr;
    }
    if (prev && prev >= text_addr && prev < text_addr + text_size) {
        total += scan_function(text, prev - text_addr, text_size, readable);
    }

    vm_protect(task, (vm_address_t)lo, (vm_size_t)(hi - lo), FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    return total;
}
