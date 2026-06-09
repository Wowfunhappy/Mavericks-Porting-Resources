/*
 * patchtest.c — end-to-end validation of the patched (F0/lock) lzcnt/tzcnt path.
 *
 *  (1) Decoder: the F0-prefixed bytes the in-memory patcher emits must decode
 *      as LZCNT/TZCNT with the right length and operands.
 *  (2) Fault path: execute those bytes (they #UD on this host) and confirm the
 *      production handler emulates them to the same value real lzcnt/tzcnt give.
 *
 * Built with -mlzcnt so the hardware reference is unambiguous. The handler is
 * installed but auto-patching is disabled (AVXEMU_NOPATCH) — we hand-embed the
 * patched bytes, so the test never rewrites its own text.
 */
#include "decode.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void avxemu_force_install(void);
extern int  avxemu_test_ud2;

extern uint64_t e2e_plzcnt64(uint64_t);
extern uint64_t e2e_ptzcnt64(uint64_t);
extern uint32_t e2e_plzcnt32(uint32_t);
extern uint32_t e2e_ptzcnt32(uint32_t);
extern uint64_t e2e_plzcnt_mem(const uint64_t *);

static uint64_t hw_lzcnt64(uint64_t x){ uint64_t r; __asm__("lzcntq %1,%0":"=r"(r):"r"(x)); return r; }
static uint64_t hw_tzcnt64(uint64_t x){ uint64_t r; __asm__("tzcntq %1,%0":"=r"(r):"r"(x)); return r; }
static uint32_t hw_lzcnt32(uint32_t x){ uint32_t r; __asm__("lzcntl %1,%0":"=r"(r):"r"(x)); return r; }
static uint32_t hw_tzcnt32(uint32_t x){ uint32_t r; __asm__("tzcntl %1,%0":"=r"(r):"r"(x)); return r; }

static int test_decode(void){
    struct { uint8_t b[6]; int len; vex_op op; const char *nm; } v[] = {
        {{0xF0,0x48,0x0F,0xBD,0xC7},5,BMI_LZCNT,"lzcnt r64"},
        {{0xF0,0x48,0x0F,0xBC,0xC7},5,BMI_TZCNT,"tzcnt r64"},
        {{0xF0,0x0F,0xBD,0xC7},4,BMI_LZCNT,"lzcnt r32"},
        {{0xF0,0x0F,0xBC,0xC7},4,BMI_TZCNT,"tzcnt r32"},
        {{0xF0,0x48,0x0F,0xBD,0x07},5,BMI_LZCNT,"lzcnt (mem)"},
    };
    int fail=0;
    for(unsigned i=0;i<sizeof v/sizeof v[0];i++){
        decoded d; int len=decode(v[i].b,&d);
        int ok = len==v[i].len && d.op==v[i].op && d.is_bmi;
        if(!ok){ printf("  DECODE FAIL %-12s got op=%d len=%d (want op=%d len=%d)\n",
                 v[i].nm,d.op,len,v[i].op,v[i].len); fail++; }
    }
    printf("decode patched form: %d/%lu ok\n",(int)(sizeof v/sizeof v[0])-fail,sizeof v/sizeof v[0]);
    return fail;
}

static int test_path(void){
    /* a spread of inputs: 0, all-ones, every single-bit, and mixed patterns */
    uint64_t inputs[80]; int n=0;
    inputs[n++]=0; inputs[n++]=~0ull; inputs[n++]=1; inputs[n++]=0x8000000000000000ull;
    for(int b=0;b<64;b++) inputs[n++]=1ull<<b;
    inputs[n++]=0x00000000FFFFFFFFull; inputs[n++]=0xFFFFFFFF00000000ull;
    inputs[n++]=0x0123456789ABCDEFull; inputs[n++]=0xFEDCBA9876543210ull;
    inputs[n++]=0x00FF00FF00FF00FFull; inputs[n++]=0xDEADBEEFull;

    int fail=0, checked=0;
    for(int i=0;i<n;i++){
        uint64_t x=inputs[i];
        uint64_t e64=e2e_plzcnt64(x), r64=hw_lzcnt64(x);
        if(e64!=r64){ printf("  lzcnt64(%016llx) emu=%llu hw=%llu MISMATCH\n",(unsigned long long)x,(unsigned long long)e64,(unsigned long long)r64); fail++; }
        uint64_t et=e2e_ptzcnt64(x), rt=hw_tzcnt64(x);
        if(et!=rt){ printf("  tzcnt64(%016llx) emu=%llu hw=%llu MISMATCH\n",(unsigned long long)x,(unsigned long long)et,(unsigned long long)rt); fail++; }
        uint32_t x32=(uint32_t)x;
        uint32_t el=e2e_plzcnt32(x32), rl=hw_lzcnt32(x32);
        if(el!=rl){ printf("  lzcnt32(%08x) emu=%u hw=%u MISMATCH\n",x32,el,rl); fail++; }
        uint32_t etz=e2e_ptzcnt32(x32), rtz=hw_tzcnt32(x32);
        if(etz!=rtz){ printf("  tzcnt32(%08x) emu=%u hw=%u MISMATCH\n",x32,etz,rtz); fail++; }
        uint64_t em=e2e_plzcnt_mem(&x), rm=hw_lzcnt64(x);
        if(em!=rm){ printf("  lzcnt(mem)(%016llx) emu=%llu hw=%llu MISMATCH\n",(unsigned long long)x,(unsigned long long)em,(unsigned long long)rm); fail++; }
        checked+=5;
    }
    printf("fault->emulate patched lzcnt/tzcnt: %d/%d ok\n",checked-fail,checked);
    return fail;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    setenv("AVXEMU_NOPATCH","1",1);   /* we embed patched bytes by hand */
    avxemu_test_ud2 = 0;              /* production path: decode at the faulting PC */
    avxemu_force_install();
    printf("== patched (F0/lock) lzcnt/tzcnt ==\n");
    int f1=test_decode();
    int f2=test_path();
    int tot=f1+f2;
    printf("\nPATCHTEST TOTAL: %d failure(s)\n",tot);
    return tot?1:0;
}
