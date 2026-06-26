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
#ifndef MAP_ANON
#define MAP_ANON 0x1000   /* hidden under strict _XOPEN_SOURCE on macOS */
#endif
#include <mach/mach.h>
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
            if (d->dst >= 0)      rf->gpr[d->dst]      = dst;
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
    /* default: restore and re-raise so the genuine fault crashes as expected */
    signal(sig, SIG_DFL);
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
