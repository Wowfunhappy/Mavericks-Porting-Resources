/*
 * selftest.c — in-dylib preflight. Run on the *target* with
 *   DYLD_INSERT_LIBRARIES=.../libavxemu.dylib AVXEMU_SELFTEST=1 /usr/bin/true
 * to confirm the whole trap/decode/emulate/writeback path works on that exact
 * CPU (its AVX1 mcontext layout, the handler, every operand shape) before
 * trusting it under Claude Code. Prints PASS/FAIL per shape and exits.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

extern int avxemu_test_ud2;
extern void     e2e_vpaddd(const void*, const void*, void*);
extern void     e2e_vpaddd_mem(const void*, const void*, void*);
extern void     e2e_fma213pd(const void*, const void*, void*);
extern uint64_t e2e_shlx(uint64_t, uint64_t);
extern uint64_t e2e_mulx(uint64_t, uint64_t, uint64_t*);
extern void     e2e_movbe_store(uint32_t, void*);
extern void     e2e_pad_vpaddd(const void*, const void*, void*);
extern uint64_t e2e_patched_lzcnt(uint64_t);
extern uint64_t e2e_patched_tzcnt(uint64_t);
extern uint64_t e2e_st_shlx_mem(const void*, uint64_t);
extern uint64_t e2e_st_andn_mem(const void*, uint64_t);

static int report(const char *name, int ok, int *fail) {
    fprintf(stderr, "avxemu selftest: %-18s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) (*fail)++;
    return ok;
}

void avxemu_selftest(void) {
    int fail = 0, prev = avxemu_test_ud2;
    avxemu_test_ud2 = 1;
    fprintf(stderr, "avxemu selftest: validating emulator on this CPU...\n");

    /* vector reg-reg */
    {
        uint8_t s1[32], s2[32], out[32]; int ok = 1;
        for (int i = 0; i < 32; i++) { s1[i] = (uint8_t)(i*7+1); s2[i] = (uint8_t)(i*3+9); }
        memset(out, 0xCC, 32);
        e2e_vpaddd(s1, s2, out);
        uint32_t *a=(uint32_t*)s1,*b=(uint32_t*)s2,*o=(uint32_t*)out;
        for (int i=0;i<8;i++) if (o[i]!=(uint32_t)(a[i]+b[i])) ok=0;
        report("vpaddd (reg)", ok, &fail);
    }
    /* vector memory operand */
    {
        uint8_t s1[32], mem[32], out[32]; int ok = 1;
        for (int i=0;i<32;i++){ s1[i]=(uint8_t)(i*5+2); mem[i]=(uint8_t)(i*11+3); }
        memset(out,0xCC,32);
        e2e_vpaddd_mem(s1, mem, out);
        uint32_t *a=(uint32_t*)s1,*b=(uint32_t*)mem,*o=(uint32_t*)out;
        for (int i=0;i<8;i++) if (o[i]!=(uint32_t)(a[i]+b[i])) ok=0;
        report("vpaddd (mem)", ok, &fail);
    }
    /* FMA (dst-as-source) */
    {
        double s1[4],s2[4],dst[4]; int ok=1;
        for (int i=0;i<4;i++){ s1[i]=i+1.5; s2[i]=i*0.25-1.0; dst[i]=i*3.0+0.5; }
        double c_in[4]; memcpy(c_in,dst,sizeof c_in);
        e2e_fma213pd(s1,s2,dst);   /* dst = s1*dst + s2 */
        for (int i=0;i<4;i++){ double ref=fma(s1[i],c_in[i],s2[i]); if (dst[i]!=ref) ok=0; }
        report("vfmadd213pd", ok, &fail);
    }
    /* BMI scalar */
    {
        uint64_t v=0x123456789ABCDEF0ull, cnt=12;
        int ok = (e2e_shlx(v,cnt) == (v<<(cnt&63)));
        report("shlx", ok, &fail);
    }
    /* BMI two-dest + memory store */
    {
        uint64_t a=0xDEADBEEFCAFEBABEull, b=0x1122334455667788ull, hi=0;
        uint64_t lo=e2e_mulx(a,b,&hi);
        unsigned __int128 p=(unsigned __int128)a*b;
        int ok = (lo==(uint64_t)p) && (hi==(uint64_t)(p>>64));
        report("mulx", ok, &fail);
    }
    /* MOVBE store: writes the register big-endian, so 0x11223344 -> 11 22 33 44 */
    {
        uint32_t v=0x11223344; uint8_t out[4]={0};
        e2e_movbe_store(v, out);
        int ok = out[0]==0x11 && out[1]==0x22 && out[2]==0x33 && out[3]==0x44;
        report("movbe (store)", ok, &fail);
    }
    /* VEX with a legacy padding prefix */
    {
        uint8_t s1[32],s2[32],o[32]; int ok=1;
        for (int i=0;i<32;i++){ s1[i]=(uint8_t)(i+1); s2[i]=(uint8_t)(2*i+5); }
        memset(o,0xCC,32);
        e2e_pad_vpaddd(s1,s2,o);
        uint32_t *a=(uint32_t*)s1,*b=(uint32_t*)s2,*r=(uint32_t*)o;
        for (int i=0;i<8;i++) if (r[i]!=(uint32_t)(a[i]+b[i])) ok=0;
        report("vpaddd (67-padded)", ok, &fail);
    }

    /* Patched-form lzcnt/tzcnt — the in-memory patcher's output. The zero input
     * is the crux: plain bsr/bsf (what an un-patched lzcnt silently becomes on
     * this CPU) leaves the destination undefined there; correct lzcnt/tzcnt give
     * the operand width. If this passes, the patch+emulate path is sound here. */
    {
        int ok = 1;
        if (e2e_patched_lzcnt(0x100ull) != 55) ok = 0;   /* 64-1-8 */
        if (e2e_patched_lzcnt(0ull)     != 64) ok = 0;   /* zero case */
        if (e2e_patched_lzcnt(~0ull)    != 0)  ok = 0;
        report("lzcnt (patched)", ok, &fail);
    }
    {
        int ok = 1;
        if (e2e_patched_tzcnt(0x100ull) != 8)  ok = 0;
        if (e2e_patched_tzcnt(0ull)     != 64) ok = 0;   /* zero case */
        if (e2e_patched_tzcnt(0x8000000000000000ull) != 63) ok = 0;
        report("tzcnt (patched)", ok, &fail);
    }

    /* BMI with a memory operand — the shape that aborted __memcpy_chk on the
     * target. shlx reads the memory operand as src1 (s1); andn reads it as src2
     * (s2). Hardcoded references since the target has no native BMI to compare. */
    {
        uint64_t m = 0x0000000012345678ull;
        int ok = (e2e_st_shlx_mem(&m, 8) == (m << 8));
        report("shlx (mem)", ok, &fail);
    }
    {
        uint64_t m = 0x00000000FF00FF00ull, src1 = 0x0F0F0F0Full;
        int ok = (e2e_st_andn_mem(&m, src1) == ((~src1) & m));
        report("andn (mem)", ok, &fail);
    }

    avxemu_test_ud2 = prev;
    fprintf(stderr, "avxemu selftest: %s (%d failure%s)\n",
            fail ? "FAILED" : "all shapes OK", fail, fail==1?"":"s");
    fflush(stderr);
    _exit(fail ? 1 : 0);
}
