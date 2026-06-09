/*
 * memtest.c — end-to-end checks of the handler's effective-address computation
 * across addressing modes (disp8/disp32, SIB base+index*scale, RIP-relative,
 * high-register base/index) and a memory-destination store. Each runs the
 * instruction emulated (ud2-trapped) and compares to a scalar reference.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int avxemu_test_ud2;
extern void m_disp8(const void*, void*, void*);
extern void m_disp32(const void*, void*, void*);
extern void m_sib(const void*, void*, void*, uint64_t);
extern void m_riprel(const void*, void*);
extern void m_r13base(const void*, void*, void*);
extern void m_r14idx(const void*, void*, void*, uint64_t);
extern void m_extract_store(const void*, void*);
extern unsigned char m_ripdata[];

static uint64_t rng=0xfeed1234ull;
static uint64_t xs(void){uint64_t x=rng;x^=x<<13;x^=x>>7;x^=x<<17;return rng=x;}
static void fill(uint8_t*p,int n){ for(int i=0;i<n;i++) p[i]=(uint8_t)xs(); }

static int fails=0;
static void chk_add(const char*nm,const uint8_t*src1,const uint8_t*mem,const uint8_t*out){
    const uint32_t*a=(const uint32_t*)src1,*b=(const uint32_t*)mem,*o=(const uint32_t*)out;
    int ok=1; for(int i=0;i<8;i++) if(o[i]!=(uint32_t)(a[i]+b[i])) ok=0;
    printf("  %-22s %s\n", nm, ok?"PASS":"FAIL"); if(!ok)fails++;
}

int main(void){
    setvbuf(stdout,0,_IONBF,0);
    avxemu_test_ud2=1;
    void*bufv; if(posix_memalign(&bufv,32,2048))return 2; uint8_t*buf=bufv;
    uint8_t src1[32],out[32],mem[32];

    fill(src1,32); fill(mem,32); memcpy(buf+16,mem,32);  m_disp8(src1,buf,out);  chk_add("vpaddd disp8(%rsi)",src1,mem,out);
    fill(src1,32); fill(mem,32); memcpy(buf+0x200,mem,32);m_disp32(src1,buf,out); chk_add("vpaddd disp32(%rsi)",src1,mem,out);
    fill(src1,32); fill(mem,32); memcpy(buf+32,mem,32);   m_sib(src1,buf,out,8);  chk_add("vpaddd (%rsi,%rcx,4)",src1,mem,out);
    fill(src1,32); fill(mem,32); memcpy(m_ripdata,mem,32); m_riprel(src1,out);     chk_add("vpaddd data(%rip)",src1,mem,out);
    fill(src1,32); fill(mem,32); memcpy(buf,mem,32);      m_r13base(src1,buf,out); chk_add("vpaddd (%r13)",src1,mem,out);
    fill(src1,32); fill(mem,32); memcpy(buf+32,mem,32);   m_r14idx(src1,buf,out,8);chk_add("vpaddd (%rsi,%r14,4)",src1,mem,out);

    /* vextracti128 $1 -> memory : out16 = high 128 bits of src */
    { uint8_t src[32],o16[16]; fill(src,32); memset(o16,0xCC,16);
      m_extract_store(src,o16);
      int ok = memcmp(o16, src+16, 16)==0;
      printf("  %-22s %s\n","vextracti128 ->(%rsi)",ok?"PASS":"FAIL"); if(!ok)fails++; }

    printf("memtest: %s (%d failure%s)\n", fails?"FAILED":"all addressing modes OK", fails, fails==1?"":"s");
    return fails?1:0;
}
