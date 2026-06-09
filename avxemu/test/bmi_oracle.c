/*
 * bmi_oracle.c — differential test of bmi_exec() vs real BMI1/BMI2/LZCNT/MOVBE.
 * COMPILE -mbmi -mbmi2 -mlzcnt -mmovbe on a Haswell+ host.
 *
 * Values are checked against hardware (intrinsics, or inline asm for ops with
 * no intrinsic). Flag-setting ops also capture hardware RFLAGS and compare the
 * architecturally-defined bits (CF/ZF/SF/OF).
 */
#include "vexops.h"
#include "regs.h"
#include <immintrin.h>
#include <x86intrin.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t rng=0xdeadbeef12345ull;
static uint64_t xs(void){uint64_t x=rng;x^=x<<13;x^=x>>7;x^=x<<17;return rng=x;}

/* hardware exec + flag capture for the 3 categories we need */
#define DEFINED (FLAG_CF|FLAG_ZF|FLAG_SF|FLAG_OF)

static uint64_t hw_andn(uint64_t a,uint64_t b,uint64_t*f){uint64_t r,ff;__asm__ volatile("andnq %3,%2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a),"r"(b):"cc");*f=ff;return r;}
static uint64_t hw_blsi(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("blsiq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
static uint64_t hw_blsr(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("blsrq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
static uint64_t hw_blsmsk(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("blsmskq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
static uint64_t hw_bzhi(uint64_t a,uint64_t i,uint64_t*f){uint64_t r,ff;__asm__ volatile("bzhiq %3,%2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a),"r"(i):"cc");*f=ff;return r;}
static uint64_t hw_bextr(uint64_t a,uint64_t c,uint64_t*f){uint64_t r,ff;__asm__ volatile("bextrq %3,%2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a),"r"(c):"cc");*f=ff;return r;}
static uint64_t hw_tzcnt(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("tzcntq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
static uint64_t hw_lzcnt(uint64_t a,uint64_t*f){uint64_t r,ff;__asm__ volatile("lzcntq %2,%0\n\tpushfq\n\tpopq %1":"=&r"(r),"=&r"(ff):"r"(a):"cc");*f=ff;return r;}
static uint64_t hw_rorx(uint64_t a,int i){uint64_t r;__asm__ volatile("rorxq %2,%1,%0":"=r"(r):"r"(a),"i"(13));(void)i;return r;} /* fixed imm 13 */
static uint64_t hw_shlx(uint64_t a,uint64_t c){uint64_t r;__asm__ volatile("shlxq %2,%1,%0":"=r"(r):"r"(a),"r"(c));return r;}
static uint64_t hw_shrx(uint64_t a,uint64_t c){uint64_t r;__asm__ volatile("shrxq %2,%1,%0":"=r"(r):"r"(a),"r"(c));return r;}
static uint64_t hw_sarx(uint64_t a,uint64_t c){uint64_t r;__asm__ volatile("sarxq %2,%1,%0":"=r"(r):"r"(a),"r"(c));return r;}
static uint64_t hw_movbe(uint64_t a){return __builtin_bswap64(a);}

static int fails=0;
static void check(const char*nm,int ok){ if(!ok){ printf("  %-8s FAIL\n",nm); fails++; } }

int main(void){
    const int N=300000;
    int e_andn=0,e_blsi=0,e_blsr=0,e_blsmsk=0,e_bzhi=0,e_bextr=0,e_mulx=0,e_pdep=0,e_pext=0,
        e_rorx=0,e_shlx=0,e_shrx=0,e_sarx=0,e_tzcnt=0,e_lzcnt=0,e_movbe=0;
    for(int t=0;t<N;t++){
        uint64_t a=xs(),b=xs();
        /* bias some toward small / zero to hit edge flags */
        if((t&7)==0) a&= (xs()&63);
        uint64_t d,d2,fl,hf,hr; uint64_t fl0=0;

        hr=hw_andn(a,b,&hf); fl=fl0; bmi_exec(BMI_ANDN,64,a,b,&d,&d2,&fl);
        if(d!=hr || (fl&DEFINED)!=(hf&DEFINED)) e_andn++;
        hr=hw_blsi(a,&hf); fl=fl0; bmi_exec(BMI_BLSI,64,a,0,&d,&d2,&fl);
        if(d!=hr || (fl&DEFINED)!=(hf&DEFINED)) e_blsi++;
        hr=hw_blsr(a,&hf); fl=fl0; bmi_exec(BMI_BLSR,64,a,0,&d,&d2,&fl);
        if(d!=hr || (fl&DEFINED)!=(hf&DEFINED)) e_blsr++;
        hr=hw_blsmsk(a,&hf); fl=fl0; bmi_exec(BMI_BLSMSK,64,a,0,&d,&d2,&fl);
        if(d!=hr || (fl&DEFINED)!=(hf&DEFINED)) e_blsmsk++;
        hr=hw_bzhi(a,b,&hf); fl=fl0; bmi_exec(BMI_BZHI,64,a,b,&d,&d2,&fl);
        if(d!=hr || (fl&DEFINED)!=(hf&DEFINED)) e_bzhi++;
        hr=hw_bextr(a,b,&hf); fl=fl0; bmi_exec(BMI_BEXTR,64,a,b,&d,&d2,&fl);
        if(d!=hr || (fl&(FLAG_ZF))!=(hf&(FLAG_ZF))) e_bextr++; /* ZF defined */
        { uint64_t hi,lo=_mulx_u64(a,b,(unsigned long long*)&hi); fl=fl0;
          bmi_exec(BMI_MULX,64,a,b,&d,&d2,&fl); if(d!=lo||d2!=hi) e_mulx++; }
        { uint64_t h=_pdep_u64(a,b); fl=fl0; bmi_exec(BMI_PDEP,64,a,b,&d,&d2,&fl); if(d!=h)e_pdep++; }
        { uint64_t h=_pext_u64(a,b); fl=fl0; bmi_exec(BMI_PEXT,64,a,b,&d,&d2,&fl); if(d!=h)e_pext++; }
        { uint64_t h=hw_rorx(a,13); fl=fl0; bmi_exec(BMI_RORX,64,a,13,&d,&d2,&fl); if(d!=h)e_rorx++; }
        { uint64_t h=hw_shlx(a,b); fl=fl0; bmi_exec(BMI_SHLX,64,a,b,&d,&d2,&fl); if(d!=h)e_shlx++; }
        { uint64_t h=hw_shrx(a,b); fl=fl0; bmi_exec(BMI_SHRX,64,a,b,&d,&d2,&fl); if(d!=h)e_shrx++; }
        { uint64_t h=hw_sarx(a,b); fl=fl0; bmi_exec(BMI_SARX,64,a,b,&d,&d2,&fl); if(d!=h)e_sarx++; }
        hr=hw_tzcnt(a,&hf); fl=fl0; bmi_exec(BMI_TZCNT,64,a,0,&d,&d2,&fl);
        if(d!=hr || (fl&(FLAG_CF|FLAG_ZF))!=(hf&(FLAG_CF|FLAG_ZF))) e_tzcnt++;
        hr=hw_lzcnt(a,&hf); fl=fl0; bmi_exec(BMI_LZCNT,64,a,0,&d,&d2,&fl);
        if(d!=hr || (fl&(FLAG_CF|FLAG_ZF))!=(hf&(FLAG_CF|FLAG_ZF))) e_lzcnt++;
        { uint64_t h=hw_movbe(a); fl=fl0; bmi_exec(BMI_MOVBE,64,a,0,&d,&d2,&fl); if(d!=h)e_movbe++; }
    }
    check("andn",!e_andn); check("blsi",!e_blsi); check("blsr",!e_blsr); check("blsmsk",!e_blsmsk);
    check("bzhi",!e_bzhi); check("bextr",!e_bextr); check("mulx",!e_mulx); check("pdep",!e_pdep);
    check("pext",!e_pext); check("rorx",!e_rorx); check("shlx",!e_shlx); check("shrx",!e_shrx);
    check("sarx",!e_sarx); check("tzcnt",!e_tzcnt); check("lzcnt",!e_lzcnt); check("movbe",!e_movbe);
    printf("BMI: %s (%d failing)\n", fails?"FAIL":"all ok", fails);
    return fails?1:0;
}
