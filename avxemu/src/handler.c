/*
 * handler.c — SIGILL trap-and-emulate.
 *
 * On #UD we decode the faulting instruction, pull its operands from the
 * signal's AVX mcontext (xmm + ymmh halves, GPRs, rflags), run the SSE-only
 * executor, write results back, and advance RIP past the instruction. Faults
 * we don't recognise (genuine ud2 asserts etc.) chain to the previous handler.
 *
 * The 256-bit register state lives in the mcontext on an AVX-capable CPU
 * (which the target is), so no software shadow register file is needed.
 */

#define _XOPEN_SOURCE 700
#include <signal.h>
#include <sys/ucontext.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>       /* open() in faulthist_dump */
#include <stdio.h>       /* rename() in faulthist_dump */
#ifndef MAP_ANON
#define MAP_ANON 0x1000   /* hidden under strict _XOPEN_SOURCE on macOS */
#endif
#include <mach/mach.h>
#include <mach/mach_time.h>  /* mach_absolute_time() in faultsnap_dump */
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "decode.h"
#include "regs.h"
#include "softfma.h"
#include "lde.h"
#include "regfile.h"

static struct sigaction g_old;
static int g_owned_sigill = 0;          /* our SIGILL handler is installed */

/*
 * Fault-driven relocation trigger (Task C). A 4-byte isolated scalar BMI op that
 * the eager trampoliner couldn't patch (no room for a 5-byte jmp) keeps trapping
 * forever at ~57us/round-trip. We count #UDs per RIP and, once a RIP crosses a
 * threshold, relocate its block (avxemu_relocate_block) so it stops faulting. The
 * table is a fixed open-addressed RIP->count map (no allocation in the handler).
 */
#define HOT_SLOTS 4096
#define HOT_THRESHOLD 50          /* faults at one RIP before we relocate it */
static struct { uint64_t rip; uint32_t n; } g_hot[HOT_SLOTS];
static int g_reloc_enabled = 1;   /* AVXEMU_RELOC=0 disables (A/B) */

/* AVXEMU_FAULTHIST=1 diagnostic: periodically dump the g_hot RIP->count table
 * (i.e. every site still faulting into the handler) to /tmp/faulthist.out so a
 * live spin can be probed for WHICH sites fault repeatedly (eager-scan misses,
 * relocation declines, JIT-emitted code). Async-signal-safe: open/write/close
 * + static buffers only, atomic rename like ophist. */
static int g_faulthist_on = -1;
static uint64_t g_faulthist_total;
static int faulthist_enabled(void){
    if (g_faulthist_on < 0){ const char *e = getenv("AVXEMU_FAULTHIST"); g_faulthist_on = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return g_faulthist_on;
}
static void fh_u64(int fd, uint64_t v){
    char b[24]; int i = 24;
    if (v == 0){ (void)write(fd, "0", 1); return; }
    while (v){ b[--i] = (char)('0' + (v % 10)); v /= 10; }
    (void)write(fd, b + i, (size_t)(24 - i));
}
static void fh_hex(int fd, uint64_t v){
    char b[18]; int i = 18;
    if (v == 0){ (void)write(fd, "0x0", 3); return; }
    while (v){ int d = (int)(v & 0xF); b[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v >>= 4; }
    b[--i] = 'x'; b[--i] = '0';
    (void)write(fd, b + i, (size_t)(18 - i));
}
/* AVXEMU_FAULTSNAP: register+string snapshot from inside the handler. */
static int g_faultsnap_on = -1;
static uint64_t g_faultsnap_total;
static int faultsnap_enabled(void){
    if (g_faultsnap_on < 0){ const char *e = getenv("AVXEMU_FAULTSNAP"); g_faultsnap_on = (e && e[0] && e[0] != '0') ? 1 : 0; }
    return g_faultsnap_on;
}
static uint64_t *gpr_ptr(_STRUCT_X86_THREAD_STATE64 *ss, int i);   /* defined below */
static void faultsnap_dump(uint64_t rip, _STRUCT_X86_THREAD_STATE64 *ss){
    int fd = open("/tmp/faultsnap.out", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    /* seq + monotonic time in the header so recurrence ACROSS the run is measurable */
    (void)write(fd, "--- snap ", 9); fh_u64(fd, g_faultsnap_total);
    (void)write(fd, " t ", 3); fh_u64(fd, mach_absolute_time());
    (void)write(fd, " rip ", 5); fh_hex(fd, rip); (void)write(fd, "\n", 1);
    for (int i = 0; i < 16; i++){
        uint64_t v = *gpr_ptr(ss, i);
        (void)write(fd, "r", 1); fh_u64(fd, (uint64_t)i); (void)write(fd, "=", 1); fh_hex(fd, v);
        /* plausible user pointer? try a guarded read and print printable runs */
        if (v >= 0x100000000ull && v < 0x800000000000ull){
            uint8_t buf[128]; mach_vm_size_t got = 0;
            if (mach_vm_read_overwrite(mach_task_self(), (mach_vm_address_t)(v & ~0xFull),
                                       sizeof buf, (mach_vm_address_t)(uintptr_t)buf, &got) == KERN_SUCCESS && got == sizeof buf){
                int run = 0;
                for (int k = 0; k <= (int)sizeof buf; k++){
                    int printable = k < (int)sizeof buf && buf[k] >= 0x20 && buf[k] < 0x7F;
                    if (printable) run++;
                    else {
                        if (run >= 6){ (void)write(fd, " \"", 2); (void)write(fd, buf + k - run, (size_t)run); (void)write(fd, "\"", 1); }
                        run = 0;
                    }
                }
            }
        }
        (void)write(fd, "\n", 1);
    }
    close(fd);
}

/* declined-relocation log: (rip, reason) recorded once per site when hot_bump
 * fires and avxemu_relocate_block returns 0. Reasons (reloc.c RELOC_DECLINE):
 * 1=pool-init 2=decode 3=not-faulting 4=window 5=patch-safety 6=pool-alloc
 * 7=rel32-range 8=site-write */
extern int avxemu_reloc_last_reason;
static struct { uint64_t rip; int reason; } g_declined[512];
static int g_ndeclined;
static void declined_log(uint64_t rip, int reason){
    if (g_ndeclined < (int)(sizeof g_declined / sizeof g_declined[0]))
        { g_declined[g_ndeclined].rip = rip; g_declined[g_ndeclined].reason = reason; g_ndeclined++; }
}
static void faulthist_dump(void){
    int fd = open("/tmp/faulthist.out.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    static const char hdr[] = "=== AVXEMU_FAULTHIST: still-faulting RIPs (count desc, top 48) ===\n";
    (void)write(fd, hdr, sizeof hdr - 1);
    /* top-48 selection over the fixed table; O(48*4096) compares, diagnostic-only */
    uint64_t seen_max = ~0ull; uint32_t seen_n = ~0u;
    for (int rank = 0; rank < 48; rank++){
        uint32_t bn = 0; uint64_t brip = 0;
        for (int k = 0; k < HOT_SLOTS; k++){
            uint32_t n = g_hot[k].n; uint64_t r = g_hot[k].rip;
            if (!r || n > seen_n || (n == seen_n && r >= seen_max)) continue;
            if (n > bn || (n == bn && r > brip)){ bn = n; brip = r; }
        }
        if (!brip) break;
        fh_hex(fd, brip); (void)write(fd, "\t", 1); fh_u64(fd, bn); (void)write(fd, "\n", 1);
        seen_n = bn; seen_max = brip;
    }
    (void)write(fd, "TOTAL_FAULTS\t", 13); fh_u64(fd, g_faulthist_total); (void)write(fd, "\n", 1);
    static const char dh[] = "=== relocation DECLINES (rip, reason: 1=pool-init 2=decode 3=not-faulting 4=window 5=patch-safety 6=pool-alloc 7=rel32 8=site-write) ===\n";
    (void)write(fd, dh, sizeof dh - 1);
    for (int i = 0; i < g_ndeclined; i++){
        fh_hex(fd, g_declined[i].rip); (void)write(fd, "\tR", 2);
        fh_u64(fd, (uint64_t)g_declined[i].reason); (void)write(fd, "\n", 1);
    }
    close(fd);
    (void)rename("/tmp/faulthist.out.tmp", "/tmp/faulthist.out");
}
static int hot_bump(uint64_t rip) {     /* returns 1 exactly when count crosses threshold */
    uint32_t i = (uint32_t)((rip * 0x9e3779b97f4a7c15ull) >> 52) & (HOT_SLOTS - 1);
    for (int probe = 0; probe < 8; probe++) {
        uint32_t k = (i + probe) & (HOT_SLOTS - 1);
        if (g_hot[k].rip == rip) return ++g_hot[k].n == HOT_THRESHOLD;
        if (g_hot[k].rip == 0)   { g_hot[k].rip = rip; g_hot[k].n = 1; return HOT_THRESHOLD == 1; }
    }
    return 0;
}

/*
 * The genuine libc sigaction. dlsym is unusable: dyld applies our own
 * interposition to *every* symbol resolution (RTLD_NEXT and explicit handles
 * alike) and hands back avxemu_sigaction, which would recurse forever. The real
 * sigaction *code* is untouched, though — we find its address by reading
 * libsystem_c.dylib's own symbol table (n_value + slide), immune to interpose.
 */
typedef int (*sigaction_fn)(int, const struct sigaction *, struct sigaction *);

static void *real_sym(const char *img, const char *name){
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *nm = _dyld_get_image_name(i);
        if (!nm || !strstr(nm, img)) continue;
        const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!mh || mh->magic != MH_MAGIC_64) continue;
        intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        uint64_t le_vm = 0, le_off = 0; uint32_t symoff = 0, nsyms = 0, stroff = 0;
        const struct load_command *lc = (const struct load_command *)(mh + 1);
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
                if (!strcmp(sg->segname, "__LINKEDIT")) { le_vm = sg->vmaddr; le_off = sg->fileoff; }
            } else if (lc->cmd == LC_SYMTAB) {
                const struct symtab_command *st = (const struct symtab_command *)lc;
                symoff = st->symoff; nsyms = st->nsyms; stroff = st->stroff;
            }
            lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
        }
        if (!symoff || !le_vm) return 0;
        const struct nlist_64 *syms = (const struct nlist_64 *)(le_vm + slide + (symoff - le_off));
        const char *strs = (const char *)(le_vm + slide + (stroff - le_off));
        for (uint32_t s = 0; s < nsyms; s++) {
            if (syms[s].n_un.n_strx == 0 || syms[s].n_value == 0) continue;
            if (!strcmp(strs + syms[s].n_un.n_strx, name)) return (void *)(syms[s].n_value + slide);
        }
    }
    return 0;
}

static sigaction_fn real_sigaction(void){
    static sigaction_fn p;
    if (!p) p = (sigaction_fn)real_sym("libsystem_c.dylib", "_sigaction");
    return p;
}

/*
 * Test hook: on an AVX2 host the target instructions don't fault, so the
 * fault-injection harness prefixes each one with `ud2`. When this is set, a
 * #UD whose bytes are 0F 0B is treated as "decode the instruction that follows
 * and skip past both." Zero in production (real #UD points straight at the
 * unsupported instruction).
 */
int avxemu_test_ud2 = 0;

static void emit(const char *s){ (void)write(2, s, strlen(s)); }

extern void avxemu_selftest(void);   /* selftest.c */
extern long avxemu_patch_lzcnt(void);/* patch_mem.c */
extern long avxemu_install_trampolines(void); /* tramp.c */

/* ---- crash handler state (for the SIMD over-read fixup) ---- */
static int g_owns_crash = 0;       /* we installed the SIGSEGV/SIGBUS handler */
static struct sigaction g_old_segv, g_old_bus;

/*
 * Emulated memory access. `seg` selects a segment override (1=gs, 2=fs): the
 * effective address from ea_rf() is the *flat* part (base+index+disp), and the
 * CPU adds the segment base, so we issue a real gs/fs-prefixed access rather
 * than trying to read the (kernel-private) segment base ourselves.
 */
static void mem_read(void *dst, uint64_t ea, int n, int seg){
    if (seg == 1)      { for (int i=0;i<n;i++){ uint8_t v; __asm__ volatile("movb %%gs:(%1),%0":"=r"(v):"r"(ea+(uint64_t)i)); ((uint8_t*)dst)[i]=v; } return; }
    else if (seg == 2) { for (int i=0;i<n;i++){ uint8_t v; __asm__ volatile("movb %%fs:(%1),%0":"=r"(v):"r"(ea+(uint64_t)i)); ((uint8_t*)dst)[i]=v; } return; }
    /*
     * Vector loads over-read: optimized code fetches a full 16/32-byte vector
     * past a buffer's logical end, relying on page granularity. That is fine on
     * hardware when the trailing page is mapped; here the byte-copy would fault.
     * Read the first page normally (a genuinely bad EA still faults, like native),
     * and zero-fill any over-read into an unmapped trailing page — those bytes
     * aren't used by a real over-read, so the result matches.
     */
    uint64_t end = ea + (uint64_t)n;
    uint64_t fp = ea & ~(uint64_t)0xfff, lp = (end - 1) & ~(uint64_t)0xfff;
    if (fp == lp) { memcpy(dst, (void*)ea, n); return; }
    uint8_t *d8 = (uint8_t*)dst;
    uint64_t b = fp + 0x1000; size_t n0 = (size_t)(b - ea);
    memcpy(d8, (void*)ea, n0);                       /* first page: fault if unmapped (genuine bad EA) */
    /* trailing page(s): vm_read_overwrite reads what's mapped and returns an
     * error instead of faulting (mincore is unreliable on 10.9). Zero the rest. */
    size_t tail = (size_t)(end - b); vm_size_t got = 0;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)b, tail,
                                         (vm_address_t)(d8 + n0), &got);
    if (kr != KERN_SUCCESS) got = 0;
    if (got < tail) memset(d8 + n0 + got, 0, tail - got);
}
static void mem_write(uint64_t ea, const void *src, int n, int seg){
    if (seg == 1)      for (int i=0;i<n;i++){ uint8_t v=((const uint8_t*)src)[i]; __asm__ volatile("movb %0,%%gs:(%1)"::"r"(v),"r"(ea+(uint64_t)i)); }
    else if (seg == 2) for (int i=0;i<n;i++){ uint8_t v=((const uint8_t*)src)[i]; __asm__ volatile("movb %0,%%fs:(%1)"::"r"(v),"r"(ea+(uint64_t)i)); }
    else               memcpy((void*)ea, src, n);
}
#define MEMRD(dstp,addr,nn) mem_read((dstp),(addr),(int)(nn),d->seg)
#define MEMWR(addr,srcp,nn) mem_write((addr),(srcp),(int)(nn),d->seg)

static int g_fixup_overread = 0;        /* map a zero page on a SIMD over-read fault */
static unsigned long g_overread_pages = 0;

/* Is address `a` in an unmapped GAP (vs. a mapped region — including a PROT_NONE
 * guard page, which must NOT be touched)? mach_vm_region returns the first region
 * at or after `a`; if its start is past `a`, then `a` is in a gap. */
static int addr_in_gap(uint64_t a){
    mach_vm_address_t ra = a; mach_vm_size_t sz = 0;
    vm_region_basic_info_data_64_t info; mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t obj;
    if (mach_vm_region(mach_task_self(), &ra, &sz, VM_REGION_BASIC_INFO_64,
                       (vm_region_info_t)&info, &cnt, &obj) != KERN_SUCCESS) return 1;
    return ra > a;
}

/*
 * Optimized SIMD code (in Bun's native code and in ours) reads a full vector
 * past a buffer's logical end, relying on the trailing page being mapped — true
 * on the AVX2 machines this code was written for, but on the target that page
 * can be unmapped. If a fault lands on an UNMAPPED GAP whose immediately
 * preceding page IS mapped (an over-read just off the end of a mapped region,
 * not a wild pointer and not a PROT_NONE guard page), map a zero page there and
 * let the instruction retry — reproducing the AVX2 machine's behaviour. The
 * over-read bytes are not used by correct code. Returns 1 if patched.
 */
static int try_fixup_overread(uint64_t fa){
    if (!g_fixup_overread || !fa) return 0;
    uint64_t pg = fa & ~(uint64_t)0xfff;
    if ((fa & 0xfff) > 256) return 0;              /* deep in the page: not a just-past-the-edge over-read */
    if (!addr_in_gap(pg)) return 0;                /* mapped (real data or a guard page): never touch */
    if (addr_in_gap(pg - 1)) return 0;             /* preceding page also unmapped: a wild pointer */
    if (g_overread_pages > 200000) return 0;       /* runaway guard */
    void *m = mmap((void*)pg, 0x1000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0);
    if (m == MAP_FAILED || m != (void*)pg) return 0;
    g_overread_pages++;
    return 1;
}

/*
 * SIGSEGV/SIGBUS front-line handler. Its sole job is the SIMD over-read fixup
 * above; anything it can't fix it passes straight to the previous handler so a
 * genuine crash still reports exactly as it would have.
 */
static void on_crash(int sig, siginfo_t *info, void *uctx){
    ucontext_t *uc = (ucontext_t *)uctx;
    uint64_t crip = uc ? (uint64_t)uc->uc_mcontext->__ss.__rip : 0;
    uint64_t fa = info ? (uint64_t)info->si_addr : 0;

    /* Data over-read off the end of a mapped region -> map a zero page, retry.
     * Skip code-fetch faults (fa==rip): those are wild jumps, not over-reads. */
    if ((sig==SIGSEGV || sig==SIGBUS) && fa != crip && try_fixup_overread(fa)) return;

    struct sigaction *o = (sig==SIGSEGV)?&g_old_segv:&g_old_bus;
    if (o->sa_flags & SA_SIGINFO){ if(o->sa_sigaction){ o->sa_sigaction(sig,info,uctx); return; } }
    else if (o->sa_handler==SIG_IGN) return;
    else if (o->sa_handler && o->sa_handler!=SIG_DFL){ o->sa_handler(sig); return; }
    /* no real handler to chain to: restore the default disposition (via the
     * genuine sigaction, not our interposed one), unblock, and re-raise so the
     * process dies cleanly instead of re-entering us on the retried fault. */
    struct sigaction da; memset(&da,0,sizeof da); da.sa_handler = SIG_DFL; sigemptyset(&da.sa_mask);
    real_sigaction()(sig, &da, 0);
    sigset_t um; sigemptyset(&um); sigaddset(&um, sig); sigprocmask(SIG_UNBLOCK, &um, 0);
    raise(sig);
}

/* macOS lays xmm0..15 and ymmh0..15 out consecutively; treat as arrays. */
static int read_mcontext_avx(ucontext_t *uc,
                             _STRUCT_XMM_REG **xmm, _STRUCT_XMM_REG **ymmh,
                             _STRUCT_X86_THREAD_STATE64 **ss) {
    /* AVX state is present when the saved mcontext is at least the AVX size. */
    if (uc->uc_mcsize < sizeof(_STRUCT_MCONTEXT_AVX64)) return 0;
    _STRUCT_MCONTEXT_AVX64 *mc = (_STRUCT_MCONTEXT_AVX64 *)uc->uc_mcontext;
    *xmm  = &mc->__fs.__fpu_xmm0;
    *ymmh = &mc->__fs.__fpu_ymmh0;
    *ss   = &mc->__ss;
    return 1;
}

static uint64_t *gpr_ptr(_STRUCT_X86_THREAD_STATE64 *ss, int i) {
    switch (i) {
    case 0: return &ss->__rax; case 1: return &ss->__rcx; case 2: return &ss->__rdx;
    case 3: return &ss->__rbx; case 4: return &ss->__rsp; case 5: return &ss->__rbp;
    case 6: return &ss->__rsi; case 7: return &ss->__rdi; case 8: return &ss->__r8;
    case 9: return &ss->__r9; case 10:return &ss->__r10; case 11:return &ss->__r11;
    case 12:return &ss->__r12; case 13:return &ss->__r13; case 14:return &ss->__r14;
    default:return &ss->__r15;
    }
}

static uint64_t ea_rf(const decoded *d, const avxemu_regfile *rf, uint64_t rip_next) {
    uint64_t addr = (uint64_t)(int64_t)d->disp;
    if (d->rip_rel) addr += rip_next;
    if (d->base  != OPND_NONE) addr += rf->gpr[d->base];
    if (d->index != OPND_NONE) addr += rf->gpr[d->index] * d->scale;
    return addr;
}

/*
 * The single emulation core, shared by the SIGILL handler and the trampoline.
 * Reads operands from rf (registers or memory), runs the SSE-only executor, and
 * writes results back into rf. rf->rip must be the instruction's own address so
 * RIP-relative memory operands resolve correctly. Returns 1 on success, 0 if the
 * op is recognised-but-unimplemented or a memory operand is implausibly sized.
 */
int avxemu_emulate(const decoded *d, avxemu_regfile *rf) {
    uint64_t rip_next = rf->rip + d->len;

    if (d->is_bmi) {
        uint64_t s1 = 0, s2 = 0, dst = 0, dst2 = 0, flags = rf->rflags, memv = 0;
        if (d->a_src == OPND_MEM || d->b_src == OPND_MEM) {
            if (d->mem_bytes < 1 || d->mem_bytes > (int)sizeof memv) return 0;
            uint64_t a = ea_rf(d, rf, rip_next); MEMRD(&memv, a, d->mem_bytes);
        }
        if (d->a_src == OPND_MEM) s1 = memv;
        else if (d->a_src >= 0)   s1 = rf->gpr[d->a_src];
        if (d->bmi_s1_rdx) s1 = rf->gpr[2];
        if (d->op == BMI_RORX)         s2 = d->imm;
        else if (d->b_src == OPND_MEM) s2 = memv;
        else if (d->b_src >= 0)        s2 = rf->gpr[d->b_src];
        if (!bmi_exec(d->op, d->opsize, s1, s2, &dst, &dst2, &flags)) return 0;
        if (d->dst_kind == DST_MEM) {
            uint64_t a = ea_rf(d, rf, rip_next); MEMWR(a, &dst, d->mem_bytes);
        } else {
            /* 16-bit ops (66-prefixed lzcnt/tzcnt/movbe) write only the low word
             * of the dst register on real hardware; 32/64-bit writes zero-extend. */
            if (d->dst >= 0)      rf->gpr[d->dst] = (d->opsize == 16)
                                      ? ((rf->gpr[d->dst] & ~0xFFFFull) | (dst & 0xFFFFull))
                                      : dst;
            if (d->bmi_dst2 >= 0) rf->gpr[d->bmi_dst2] = dst2;
            rf->rflags = flags;
        }
        return 1;
    }

    /* AVX2 masked move: per-lane conditional memory access (unmasked lanes are
     * neither read nor written, so they never fault). Mask = a_src register; the
     * lane's high bit selects. Load: dst<-mem (else 0). Store: mem<-b_src. */
    if (d->op == VPMASKMOVD || d->op == VPMASKMOVQ) {
        int lsz = (d->op == VPMASKMOVQ) ? 8 : 4;
        int lanes = (d->wide ? 32 : 16) / lsz;
        const uint8_t *mask = (const uint8_t *)rf->ymm[d->a_src];
        uint64_t ea = ea_rf(d, rf, rip_next);
        if (d->dst_kind == DST_MEM) {
            const uint8_t *src = (const uint8_t *)rf->ymm[d->b_src];
            for (int i = 0; i < lanes; i++) {
                int sign = (lsz == 8) ? (((const int64_t *)mask)[i] < 0) : (((const int32_t *)mask)[i] < 0);
                if (sign) mem_write(ea + (uint64_t)i * lsz, src + i * lsz, lsz, d->seg);
            }
        } else {
            uint8_t out[32]; memset(out, 0, 32);
            for (int i = 0; i < lanes; i++) {
                int sign = (lsz == 8) ? (((const int64_t *)mask)[i] < 0) : (((const int32_t *)mask)[i] < 0);
                if (sign) mem_read(out + i * lsz, ea + (uint64_t)i * lsz, lsz, d->seg);
            }
            memcpy(rf->ymm[d->dst], out, 16);
            if (d->wide) memcpy(rf->ymm[d->dst] + 16, out + 16, 16);
            else         memset(rf->ymm[d->dst] + 16, 0, 16);
        }
        return 1;
    }

    ymm256 A, B, C, OUT; uint64_t gpr_out = 0;
    memset(&A,0,sizeof A); memset(&B,0,sizeof B); memset(&C,0,sizeof C);
    if ((d->a_src == OPND_MEM || d->b_src == OPND_MEM || d->dst_kind == DST_MEM)
        && (d->mem_bytes < 1 || d->mem_bytes > (int)sizeof A.b)) return 0;
    if (d->a_src == OPND_MEM) { uint64_t a=ea_rf(d,rf,rip_next); MEMRD(A.b, a, d->mem_bytes); }
    else if (d->a_src >= 0)   memcpy(A.b, rf->ymm[d->a_src], 32);
    if (d->b_src == OPND_MEM) { uint64_t a=ea_rf(d,rf,rip_next); MEMRD(B.b, a, d->mem_bytes); }
    else if (d->b_src >= 0)   memcpy(B.b, rf->ymm[d->b_src], 32);
    if (d->c_src >= 0)        memcpy(C.b, rf->ymm[d->c_src], 32);
    if (d->shift_imm) { memset(B.b,0,32); B.b[0] = d->imm; }

    if (!vec_exec(d->op, d->type, &A, &B, &C, d->imm, &OUT, &gpr_out)) return 0;

    if (d->dst_kind == DST_GPR) {
        /* VPMOVMSKB r32, xmm (VEX.128) yields a 16-bit mask with the upper bits
         * zeroed; vec_exec always packs 32 bits (lo | hi<<16), where the hi half
         * is the stale upper 128 of the source ymm. Drop it for the 128-bit form
         * (same upper-zeroing the vector-dst path does when !wide). Without this,
         * scan loops (vpcmpeqb; vpmovmskb; bsf/test) see phantom high bits and
         * never terminate — the same failure family as the 16-bit lzcnt bug. */
        if (d->op == VPMOVMSKB && !d->wide) gpr_out &= 0xFFFFu;
        rf->gpr[d->dst] = gpr_out;
    } else if (d->dst_kind == DST_MEM) {
        uint64_t a = ea_rf(d, rf, rip_next); MEMWR(a, OUT.b, d->mem_bytes);
    } else {
        memcpy(rf->ymm[d->dst], OUT.b, 16);
        if (d->wide) memcpy(rf->ymm[d->dst] + 16, OUT.b + 16, 16);
        else         memset(rf->ymm[d->dst] + 16, 0, 16);   /* VEX.128 zeroes upper */
    }
    return 1;
}

static void chain(int sig, siginfo_t *info, void *uctx) {
    if (g_old.sa_flags & SA_SIGINFO) {
        if (g_old.sa_sigaction) { g_old.sa_sigaction(sig, info, uctx); return; }
    } else if (g_old.sa_handler == SIG_IGN) {
        return;
    } else if (g_old.sa_handler && g_old.sa_handler != SIG_DFL) {
        g_old.sa_handler(sig); return;
    }
    /* default: restore the genuine default disposition and re-raise so an
     * instruction we don't emulate surfaces as a real #UD. This MUST go through
     * the real sigaction, not our interposed signal()/sigaction: for an owned
     * SIGILL those only record a new chain target and leave on_sigill frontline,
     * so signal(sig, SIG_DFL); raise(sig); would re-enter this handler forever
     * (an infinite SIGILL loop — observed as a hang on any unimplemented insn,
     * e.g. AVX2 gathers). Mirrors the on_crash chain path. */
    struct sigaction da; memset(&da, 0, sizeof da);
    da.sa_handler = SIG_DFL; sigemptyset(&da.sa_mask);
    real_sigaction()(sig, &da, 0);
    raise(sig);
}

/*
 * cpuid faking. The target's no-AVX2 fallback path is broken (it spins), so we
 * make cpuid advertise AVX2 + BMI1/BMI2 as present: the program then takes its
 * AVX2 code path, whose faulting instructions the SIGILL handler emulates. We
 * can't interpose cpuid (it's an instruction, not a call), so we rewrite each
 * `0F A2` in the main image to `ud2`, record its address, and synthesise the
 * result in on_sigill: run the real cpuid, then OR in / mask out feature bits.
 * SET masks only add bits, so this is a no-op on a CPU that already has them.
 */
static uint64_t g_cpuid_addrs[16384];
static int g_cpuid_count = 0;
/* per-feature overrides applied to cpuid results: result = (real | set) & ~clr */
static uint32_t g_l1ecx_set=0, g_l1ecx_clr=0, g_l7ebx_set=0, g_l7ebx_clr=0;

static void avxemu_patch_cpuid(void){
    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!mh || mh->magic != MH_MAGIC_64) return;
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    uint64_t text_addr=0, text_size=0;
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    for (uint32_t i=0;i<mh->ncmds;i++){
        if (lc->cmd==LC_SEGMENT_64){
            const struct segment_command_64 *sg=(const struct segment_command_64 *)lc;
            if (!strcmp(sg->segname,"__TEXT")){
                const struct section_64 *s=(const struct section_64 *)(sg+1);
                for (uint32_t j=0;j<sg->nsects;j++) if(!strcmp(s[j].sectname,"__text")){ text_addr=s[j].addr; text_size=s[j].size; }
            }
        }
        lc=(const struct load_command *)((const char *)lc+lc->cmdsize);
    }
    if (!text_addr) return;
    uint8_t *text=(uint8_t *)(text_addr+slide), *end=text+text_size;
    uintptr_t lo=(uintptr_t)text & ~(uintptr_t)0xfff, hi=((uintptr_t)text+text_size+0xfff)&~(uintptr_t)0xfff;
    if (vm_protect(mach_task_self(),(vm_address_t)lo,(vm_size_t)(hi-lo),FALSE,
                   VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY)!=KERN_SUCCESS){ emit("avxemu: cpuid vm_protect failed\n"); return; }
    uint8_t *p=text;
    while (p<end){
        int zk,off; int len=x86_len(p,end,&zk,&off);
        if (len<=0) break;
        if (len==2 && p[0]==0x0F && p[1]==0xA2){
            if (g_cpuid_count < (int)(sizeof g_cpuid_addrs/sizeof g_cpuid_addrs[0])) g_cpuid_addrs[g_cpuid_count++]=(uint64_t)(uintptr_t)p;
            p[0]=0x0F; p[1]=0x0B;   /* ud2 */
        }
        p+=len;
    }
    vm_protect(mach_task_self(),(vm_address_t)lo,(vm_size_t)(hi-lo),FALSE,VM_PROT_READ|VM_PROT_EXECUTE);
}

/* Map a feature token to its cpuid bit and OR it into the set or clr mask. */
static void cpuid_feat(const char *name, int set){
    uint32_t *l1 = set ? &g_l1ecx_set : &g_l1ecx_clr;
    uint32_t *l7 = set ? &g_l7ebx_set : &g_l7ebx_clr;
    if      (!strcmp(name,"avx"))   *l1 |= (1u<<28);
    else if (!strcmp(name,"fma"))   *l1 |= (1u<<12);
    else if (!strcmp(name,"movbe")) *l1 |= (1u<<22);
    else if (!strcmp(name,"f16c"))  *l1 |= (1u<<29);
    else if (!strcmp(name,"avx2"))  *l7 |= (1u<<5);
    else if (!strcmp(name,"bmi1"))  *l7 |= (1u<<3);
    else if (!strcmp(name,"bmi2"))  *l7 |= (1u<<8);
    else if (!strcmp(name,"bmi"))   *l7 |= (1u<<3)|(1u<<8);
    else if (!strcmp(name,"all")) { *l1 |= (1u<<28)|(1u<<12)|(1u<<22)|(1u<<29); *l7 |= (1u<<5)|(1u<<3)|(1u<<8); }
}
static void cpuid_parse(const char *spec, int set){
    if (!spec) return;
    char buf[64]; int bi=0;
    for (const char *s=spec; ; s++){
        if (*s==',' || *s=='\0'){ buf[bi]=0; if (bi) cpuid_feat(buf,set); bi=0; if (!*s) break; }
        else if (bi < (int)sizeof buf-1) buf[bi++]=*s;
    }
}

static void on_sigill(int sig, siginfo_t *info, void *uctx) {
    ucontext_t *uc = (ucontext_t *)uctx;
    if (g_cpuid_count > 0) {                         /* cpuid trap check (before AVX-state read) */
        _STRUCT_X86_THREAD_STATE64 *ss0 = &uc->uc_mcontext->__ss;
        uint64_t rip0 = ss0->__rip;
        int lo=0, hi=g_cpuid_count-1, found=0;
        while (lo<=hi){ int mid=(lo+hi)>>1; uint64_t v=g_cpuid_addrs[mid];
            if (v==rip0){found=1;break;} else if (v<rip0) lo=mid+1; else hi=mid-1; }
        if (found){
            uint32_t leaf=(uint32_t)ss0->__rax, sub=(uint32_t)ss0->__rcx, a,b,c,d;
            __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(leaf),"c"(sub));
            if (leaf==1)                c = (c | g_l1ecx_set) & ~g_l1ecx_clr;
            else if (leaf==7 && sub==0) b = (b | g_l7ebx_set) & ~g_l7ebx_clr;
            ss0->__rax=a; ss0->__rbx=b; ss0->__rcx=c; ss0->__rdx=d; ss0->__rip+=2;
            return;
        }
    }
    _STRUCT_XMM_REG *xmm, *ymmh; _STRUCT_X86_THREAD_STATE64 *ss;
    if (!read_mcontext_avx(uc, &xmm, &ymmh, &ss)) { chain(sig, info, uctx); return; }

    uint64_t rip = ss->__rip;
    uint64_t base = rip;
    const uint8_t *ip = (const uint8_t *)rip;
    if (avxemu_test_ud2 && ip[0] == 0x0F && ip[1] == 0x0B) base = rip + 2;  /* skip injected ud2 */
    const uint8_t *bp = (const uint8_t *)base;
    decoded d;
    if (!decode(bp, &d)) { chain(sig, info, uctx); return; }   /* genuine #UD: let it surface */
    uint64_t rip_next = base + d.len;

    /* AVXEMU_FAULTHIST diagnostic: snapshot the still-faulting-RIP table periodically */
    if (faulthist_enabled() && ((++g_faulthist_total & 0x7FFF) == 0)) faulthist_dump();

    /* AVXEMU_FAULTSNAP diagnostic: every 1024th fault, dump rip + GPRs and any
     * printable string data the pointer-looking registers reach (vm_read-guarded,
     * in-process — works where a debugger can't attach through the fault stream).
     * Identifies WHAT data the hot loop is chewing on. */
    if (faultsnap_enabled() && ((++g_faultsnap_total & 0x3FF) == 0))
        faultsnap_dump(base, ss);

    /*
     * Fault-driven relocation — relocate BEFORE emulate. Once `base` has trapped
     * HOT_THRESHOLD times, overwrite [base, base+5) with a `jmp rel32` into a
     * relocated block that runs the op (natively/emulated) and jumps back.
     *
     * Why re-enter at `base` (not rip_next): the jmp we just wrote lives AT base
     * and MUST execute. Advancing RIP would skip it; worse, for a 4-byte faulting
     * op rip_next = base+4 lands on the LAST byte of the 5-byte E9 jmp, so the
     * kernel would resume one byte INSIDE the jmp and decode garbage (a
     * deterministic crash on the 50th hit of every such site). Re-entering at base
     * runs the op exactly once, via the patched jmp.
     *
     * Why relocate-before-emulate (not emulate -> write back -> rip=base): that
     * naive ordering DOUBLE-APPLIES dst==src ops. E.g. `tzcnt eax,eax`: emulate
     * sets eax=f(eax_orig), then re-entering the relocated block computes
     * f(f(eax_orig)). By relocating first and NOT emulating on success, the op
     * runs exactly once.
     *
     * avxemu_relocate_block reads only the instruction bytes at base (it
     * re-decodes and emits code); it never needs the emulated result, so calling
     * it before emulation is correct. If it declines (unsafe site, non-relocatable
     * op, or pool exhausted) we fall through and emulate this instance normally — a
     * declined site keeps faulting + emulating forever (the safe floor).
     *
     * Gating matches the old post-write-back trigger, just moved earlier: reached
     * only on the normal emulated path (the cpuid trap and every unrecognised-#UD
     * chain() return earlier), using the real faulting address `base`. hot_bump
     * fires once per site.
     */
    if (g_reloc_enabled && hot_bump(base)) {
        if (avxemu_relocate_block((uint8_t *)base)) {
            ss->__rip = (uint64_t)base;   /* re-enter: the patched jmp runs the op once */
            return;
        }
        /* relocation declined -> fall through and emulate this instance normally */
        if (faulthist_enabled()) declined_log(base, avxemu_reloc_last_reason);
    }

    /* Snapshot the signal's machine state into a register-file, run the shared
     * core, and write the (possibly modified) state back. The same core runs
     * from the trampoline path, so there is exactly one emulation code path. */
    avxemu_regfile rf;
    for (int i = 0; i < 16; i++) {
        memcpy(rf.ymm[i],      &xmm[i],  16);
        memcpy(rf.ymm[i] + 16, &ymmh[i], 16);
        rf.gpr[i] = *gpr_ptr(ss, i);
    }
    rf.rflags = ss->__rflags;
    rf.rip    = base;

    if (!avxemu_emulate(&d, &rf)) { chain(sig, info, uctx); return; }

    for (int i = 0; i < 16; i++) {
        memcpy(&xmm[i],  rf.ymm[i],      16);
        memcpy(&ymmh[i], rf.ymm[i] + 16, 16);
        *gpr_ptr(ss, i) = rf.gpr[i];
    }
    ss->__rflags = rf.rflags;
    ss->__rip    = rip_next;
}

/*
 * Stay the front-line SIGILL handler. Runtimes like Bun install their own crash
 * reporter on SIGILL during startup; if it wins, our emulated instructions #UD
 * straight into "illegal instruction" crash reports. We interpose sigaction()
 * (and signal()): a SIGILL registration is recorded as our chain target instead
 * of replacing us, so the runtime's reporter still fires for genuine #UDs while
 * we keep first crack at the ones we emulate. Everything else passes through.
 */
int avxemu_sigaction(int sig, const struct sigaction *act, struct sigaction *old){
    if (sig == SIGILL && g_owned_sigill && act && act->sa_sigaction != on_sigill) {
        if (old) *old = g_old;       /* report the handler we're chaining to */
        g_old = *act;                /* chain unrecognised #UDs here */
        return 0;
    }
    /* When our crash handler is installed (the over-read fixup), stay frontline
     * for the crash signals so we get first crack before the runtime's reporter
     * (and the reporter still fires for genuine crashes). */
    if (g_owns_crash && act && act->sa_sigaction != on_crash &&
        (sig == SIGSEGV || sig == SIGBUS)) {
        struct sigaction *slot = (sig==SIGSEGV)?&g_old_segv:&g_old_bus;
        if (old) *old = *slot;
        *slot = *act;
        return 0;
    }
    return real_sigaction()(sig, act, old);
}
void (*avxemu_signal(int sig, void (*h)(int)))(int){
    /* BSD signal() installs a persistent, SA_RESTART handler — match that so we
     * don't silently turn syscall restarts into EINTR for the caller. */
    struct sigaction a, o; memset(&a,0,sizeof a);
    a.sa_handler = h; a.sa_flags = SA_RESTART; sigemptyset(&a.sa_mask);
    if (sig == SIGILL && g_owned_sigill) {
        avxemu_sigaction(SIGILL, &a, &o);
        return (o.sa_flags & SA_SIGINFO) ? (void(*)(int))o.sa_sigaction : o.sa_handler;
    }
    if (real_sigaction()(sig, &a, &o) != 0) return SIG_ERR;
    return (o.sa_flags & SA_SIGINFO) ? (void(*)(int))o.sa_sigaction : o.sa_handler;
}
__attribute__((used)) static const struct { const void *r, *o; }
  _interpose_sigaction __attribute__((section("__DATA,__interpose"))) =
    { (const void*)avxemu_sigaction, (const void*)sigaction },
  _interpose_signal __attribute__((section("__DATA,__interpose"))) =
    { (const void*)avxemu_signal, (const void*)signal };

/*
 * Fallback interposition, for when dyld won't do it for us.
 *
 * 10.9's dyld registers __DATA,__interpose ONLY for DYLD_INSERT_LIBRARIES
 * images. Baked into a binary as a plain LC_LOAD_DYLIB — the tidier way to
 * attach the emulator, since it then cannot leak into child processes — the
 * overrides above are silently ignored: the runtime's crash reporter takes
 * SIGILL and the first emulated instruction becomes an "illegal instruction"
 * crash report. (Observed exactly that with Claude Code: Bun's crash banner,
 * then a hang.)
 *
 * So produce dyld's end state by hand. For each image, walk the lazy and
 * non-lazy symbol pointer sections through the indirect symbol table and
 * repoint the sigaction/signal slots at ours. Idempotent, so it costs nothing
 * when dyld did interpose — those slots already hold our addresses and are
 * skipped. AVXEMU_NO_REBIND=1 opts out.
 *
 * Deliberately not touched: our own image, and the system libraries under
 * /usr/lib and /System. The runtime and whatever it bundles are what install
 * crash handlers; rewriting libsystem's own call sites buys nothing and is a
 * good way to find a reentrancy problem.
 */
/* Our own image's mach header, found by asking which loaded image contains one
 * of our own functions.
 *
 * Not _mh_dylib_header: the linker defines that only for MH_DYLIB output, so a
 * reference to it fails to link every test target that pulls handler.o into an
 * executable ("Undefined symbols: __mh_dylib_header"). Neither weak_import nor
 * weak rescues it -- both still need the symbol to exist somewhere. dladdr()
 * would be the obvious alternative, but it and Dl_info are Darwin extensions
 * that this file's strict _XOPEN_SOURCE hides (same trap as MAP_ANON above).
 * The dyld image APIs are already in use here and are not hidden, so use those. */
static const struct mach_header_64 *avxemu_self_header(void) {
    static const struct mach_header_64 *self;
    static int done;
    if (done) return self;
    done = 1;
    const uintptr_t me = (uintptr_t)&avxemu_self_header;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header_64 *mh =
            (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!mh || mh->magic != MH_MAGIC_64) continue;
        const intptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const struct load_command *lc = (const struct load_command *)(mh + 1);
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *sg =
                    (const struct segment_command_64 *)lc;
                const uintptr_t lo = (uintptr_t)sg->vmaddr + (uintptr_t)slide;
                if (me >= lo && me < lo + (uintptr_t)sg->vmsize) { self = mh; break; }
            }
            lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
        }
        if (self) break;
    }
    return self;
}

static int rebind_sym_in_image(const struct mach_header_64 *mh, intptr_t slide,
                               const char *want, const void *repl) {
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    const struct symtab_command *st = 0;
    const struct dysymtab_command *dy = 0;
    uintptr_t le_base = 0;

    for (uint32_t c = 0; c < mh->ncmds; c++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (!strcmp(sg->segname, "__LINKEDIT"))
                le_base = (uintptr_t)slide + (uintptr_t)sg->vmaddr - (uintptr_t)sg->fileoff;
        } else if (lc->cmd == LC_SYMTAB) {
            st = (const struct symtab_command *)lc;
        } else if (lc->cmd == LC_DYSYMTAB) {
            dy = (const struct dysymtab_command *)lc;
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    if (!st || !dy || !le_base || !dy->nindirectsyms) return 0;

    const struct nlist_64 *syms = (const struct nlist_64 *)(le_base + st->symoff);
    const char *strs = (const char *)(le_base + st->stroff);
    const uint32_t *indirect = (const uint32_t *)(le_base + dy->indirectsymoff);

    int n = 0;
    lc = (const struct load_command *)(mh + 1);
    for (uint32_t c = 0; c < mh->ncmds; c++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            const struct section_64 *sect = (const struct section_64 *)(sg + 1);
            for (uint32_t s = 0; s < sg->nsects; s++) {
                uint32_t type = sect[s].flags & SECTION_TYPE;
                if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS)
                    continue;
                const void **slot = (const void **)((uintptr_t)slide + (uintptr_t)sect[s].addr);
                uint32_t nslots = (uint32_t)(sect[s].size / sizeof(void *));
                for (uint32_t k = 0; k < nslots; k++) {
                    uint32_t si = indirect[sect[s].reserved1 + k];
                    if (si >= dy->nindirectsyms + dy->indirectsymoff) continue;
                    if (si & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) continue;
                    if (si >= st->nsyms) continue;
                    const char *nm = strs + syms[si].n_un.n_strx;
                    if (strcmp(nm, want)) continue;
                    if (slot[k] == repl) continue;          /* dyld already did it */
                    uintptr_t page = (uintptr_t)&slot[k] & ~(uintptr_t)(PAGE_SIZE - 1);
                    if (mprotect((void *)page, PAGE_SIZE, PROT_READ | PROT_WRITE) != 0)
                        continue;
                    slot[k] = repl;
                    n++;
                }
            }
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    return n;
}

static void avxemu_rebind_image(const struct mach_header *mhp, intptr_t slide) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)mhp;
    if (!mh || mh->magic != MH_MAGIC_64) return;
    if (mh == avxemu_self_header()) return;                 /* never ourselves */

    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        if ((const struct mach_header_64 *)_dyld_get_image_header(i) != mh) continue;
        const char *nm = _dyld_get_image_name(i);
        if (nm && (!strncmp(nm, "/usr/lib/", 9) || !strncmp(nm, "/System/", 8))) return;
        break;
    }
    rebind_sym_in_image(mh, slide, "_sigaction", (const void *)avxemu_sigaction);
    rebind_sym_in_image(mh, slide, "_signal",    (const void *)avxemu_signal);
}

__attribute__((constructor))
static void avxemu_install(void) {
    if (getenv("AVXEMU_DISABLE")) return;     /* opt-out escape hatch */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_sigill;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (real_sigaction()(SIGILL, &sa, &g_old) != 0) {
        emit("avxemu: failed to install SIGILL handler\n");
        return;
    }
    g_owned_sigill = 1;

    /* Keep that ownership even when dyld won't interpose for us (see above).
     * Registering fires the callback for every image already loaded and for
     * each one loaded later, so dlopened plugins are covered too. */
    if (!getenv("AVXEMU_NO_REBIND"))
        _dyld_register_func_for_add_image(avxemu_rebind_image);

    /* Fault-driven relocation is on by default; AVXEMU_RELOC=0 disables it so the
     * A/B harness can compare emulate-only vs relocate. */
    { const char *r = getenv("AVXEMU_RELOC"); if (r) g_reloc_enabled = (r[0] != '0'); }

    /* Advertise AVX2 + BMI1/BMI2 through a trapped cpuid so the target program
     * takes its AVX2 code path (whose faulting instructions we emulate) rather
     * than its broken no-AVX2 fallback. The SET mask only ORs bits in, so on a
     * CPU that genuinely has them this changes nothing. Env vars tune it for
     * diagnostics; AVXEMU_CPUID_CLR wins over the default (clr is applied last). */
    g_l7ebx_set |= (1u<<5) | (1u<<3) | (1u<<8);   /* avx2 | bmi1 | bmi2 */
    { const char *fc = getenv("AVXEMU_FAKE_CPUID");
      if (fc && *fc) cpuid_parse("all", (fc[1]=='f') ? 0 : 1); }   /* on => set all; off => clear all */
    cpuid_parse(getenv("AVXEMU_CPUID_SET"), 1);
    cpuid_parse(getenv("AVXEMU_CPUID_CLR"), 0);
    if (g_l1ecx_set|g_l1ecx_clr|g_l7ebx_set|g_l7ebx_clr) avxemu_patch_cpuid();

    /* Rewrite lzcnt/tzcnt in the main binary to a faulting form so they get
     * emulated rather than silently running as bsr/bsf on a pre-Haswell CPU.
     * No-op on CPUs that actually have LZCNT. Runs after the handler is armed. */
    long n_lz = avxemu_patch_lzcnt();

    /* Rewrite runs of faulting instructions to non-faulting trampolines so they
     * don't trap. The thunk runs its emulation on a dedicated side stack, so it
     * adds no program-stack pressure. Un-trampolined sites (4-byte isolated,
     * dirty functions) still fault into the SIGILL handler above. */
    long n_tr = avxemu_install_trampolines();

    /* Are we actually emulating on this CPU? If so, install the crash handler so
     * SIMD over-reads (off the end of a buffer into an unmapped page, which a
     * real AVX2 machine tolerates because that page is mapped) are fixed up
     * rather than crashing. FORCEPATCH/FORCETRAMP force this on an AVX2 host so
     * the test suite can exercise the whole path. */
    int emulating = (n_lz > 0 || n_tr > 0 ||
                     getenv("AVXEMU_FORCEPATCH") || getenv("AVXEMU_FORCETRAMP"));
    if (emulating) {
        g_fixup_overread = 1;
        /* Alternate stack so a stack-overflow SIGSEGV (guard page) can still be
         * handled — the faulting thread's own stack is exhausted at that point. */
        static char altstk[64*1024];
        stack_t ss2; ss2.ss_sp = altstk; ss2.ss_size = sizeof altstk; ss2.ss_flags = 0;
        sigaltstack(&ss2, 0);
        struct sigaction ca; memset(&ca, 0, sizeof ca);
        ca.sa_sigaction = on_crash; ca.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK; sigemptyset(&ca.sa_mask);
        sigaction(SIGSEGV, &ca, &g_old_segv);
        sigaction(SIGBUS,  &ca, &g_old_bus);
        g_owns_crash = 1;
    }

    /* Preflight: validate the whole path on this exact CPU, then exit. */
    if (getenv("AVXEMU_SELFTEST")) avxemu_selftest();
}

/* Exposed for the fault-injection test harness. */
void avxemu_force_install(void) { avxemu_install(); }
