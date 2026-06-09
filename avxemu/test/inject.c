/*
 * inject.c — end-to-end validation on an AVX2 host.
 *
 *  (1) Decoder: read the real encodings in stubs.s, decode each, and check the
 *      op id and length (length must equal the assembler's actual encoding).
 *  (2) Fault injection: call e2e_* stubs whose `ud2` traps into the real signal
 *      handler (test mode), which decodes + emulates the following instruction
 *      against the live mcontext. Compare results to a scalar reference.
 */
#include "vexops.h"
#include "decode.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint64_t dec_labels[];
extern uint64_t dec_n;
extern int avxemu_test_ud2;

extern void     e2e_vpaddd(const void*, const void*, void*);
extern void     e2e_vpaddd_mem(const void*, const void*, void*);
extern uint64_t e2e_shlx(uint64_t, uint64_t);
extern uint64_t e2e_mulx(uint64_t, uint64_t, uint64_t*);

struct exp { vex_op op; const char *nm; };
static struct exp expected[] = {
    {VPADDD,"vpaddd"},{VPAND,"vpand"},{VPSUBQ,"vpsubq"},{VPCMPEQB,"vpcmpeqb"},
    {VPSHUFB,"vpshufb"},{VPMAXSD,"vpmaxsd"},{VPSHUFD,"vpshufd"},{VPERMQ,"vpermq"},
    {VPSLLD,"vpslld(imm)"},{VPSLLD,"vpslld(xmm)"},{VPSLLVD,"vpsllvd"},{VPBROADCASTB,"vpbroadcastb"},
    {VPMOVZXBW,"vpmovzxbw"},{VEXTRACTI128,"vextracti128"},{VINSERTI128,"vinserti128"},
    {VPERM2I128,"vperm2i128"},{VPBLENDD,"vpblendd"},{VPALIGNR,"vpalignr"},{VPBLENDVB,"vpblendvb"},
    {VPMOVMSKB,"vpmovmskb"},{VFMADD213,"vfmadd213pd"},{VFMADD231,"vfmadd231sd"},{VCVTPH2PS,"vcvtph2ps"},
    {BMI_ANDN,"andn"},{BMI_MULX,"mulx"},{BMI_SHLX,"shlx"},{BMI_RORX,"rorx"},{BMI_TZCNT,"tzcnt"},
    {BMI_PDEP,"pdep"},{VPADDD,"vpaddd(mem)"},
};

static int test_decoder(void){
    int fail=0; int n=(int)dec_n;
    for(int i=0;i<n;i++){
        const uint8_t *p=(const uint8_t*)dec_labels[i];
        int asm_len=(int)(dec_labels[i+1]-dec_labels[i]);
        decoded d;
        int len=decode(p,&d);
        int ok = len==asm_len && d.op==expected[i].op;
        if(!ok){ printf("  DECODE FAIL %-14s got op=%d len=%d, want op=%d len=%d\n",
            expected[i].nm, d.op, len, expected[i].op, asm_len); fail++; }
    }
    printf("decoder: %d/%d ok\n", n-fail, n);
    return fail;
}

static int test_e2e(void){
    int fail=0;
    /* vpaddd reg-reg */
    {
        uint8_t s1[32],s2[32],out[32];
        for(int i=0;i<32;i++){ s1[i]=(uint8_t)(i*7+1); s2[i]=(uint8_t)(i*3+9); }
        memset(out,0xCC,32);
        e2e_vpaddd(s1,s2,out);
        uint32_t *a=(uint32_t*)s1,*b=(uint32_t*)s2,*o=(uint32_t*)out; int bad=0;
        for(int i=0;i<8;i++) if(o[i]!=(uint32_t)(a[i]+b[i])) bad=1;
        printf("  e2e vpaddd reg : %s\n", bad?"FAIL":"ok"); fail+=bad;
    }
    /* vpaddd mem source */
    {
        uint8_t s1[32],mem[32],out[32];
        for(int i=0;i<32;i++){ s1[i]=(uint8_t)(i*5+2); mem[i]=(uint8_t)(i*11+3); }
        memset(out,0xCC,32);
        e2e_vpaddd_mem(s1,mem,out);
        uint32_t *a=(uint32_t*)s1,*b=(uint32_t*)mem,*o=(uint32_t*)out; int bad=0;
        for(int i=0;i<8;i++) if(o[i]!=(uint32_t)(a[i]+b[i])) bad=1;
        printf("  e2e vpaddd mem : %s\n", bad?"FAIL":"ok"); fail+=bad;
    }
    /* shlx */
    {
        uint64_t v=0x123456789ABCDEF0ull, cnt=12;
        uint64_t r=e2e_shlx(v,cnt);
        uint64_t ref=v<<(cnt&63);
        printf("  e2e shlx       : %s\n", r==ref?"ok":"FAIL"); fail+=(r!=ref);
    }
    /* mulx */
    {
        uint64_t a=0xDEADBEEFCAFEBABEull, b=0x1122334455667788ull, hi=0;
        uint64_t lo=e2e_mulx(a,b,&hi);
        unsigned __int128 p=(unsigned __int128)a*b;
        int bad = (lo!=(uint64_t)p) || (hi!=(uint64_t)(p>>64));
        printf("  e2e mulx       : %s\n", bad?"FAIL":"ok"); fail+=bad;
    }
    return fail;
}

int main(void){
    setvbuf(stdout, NULL, _IONBF, 0);
    avxemu_test_ud2 = 1;     /* injected ud2 precedes each target instruction */
    printf("== decoder (real encodings) ==\n");
    int f1=test_decoder();
    printf("== fault-injection end-to-end (real SIGILL -> handler) ==\n");
    int f2=test_e2e();
    int tot=f1+f2;
    printf("\nINJECT TOTAL: %d failure(s)\n", tot);
    return tot?1:0;
}
