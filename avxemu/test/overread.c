/*
 * overread.c — a vector memory load that runs off the end of a mapped page into
 * an unmapped one (a SIMD over-read) must not fault: the emulator reads the
 * mapped bytes and zero-fills the rest. Mirrors the target's vpermd crash.
 */
#include "decode.h"
#include "regfile.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main(void){
    /* two contiguous pages; unmap the second so reads past page 1 fault */
    uint8_t *base = mmap(0, 0x2000, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (base == MAP_FAILED) { printf("mmap failed\n"); return 1; }
    munmap(base + 0x1000, 0x1000);
    for (int i=0;i<0x1000;i++) base[i] = (uint8_t)(i*7+1);

    /* vpaddd (%rax),%ymm1,%ymm0 : dst=ymm0, a=ymm1, b=[rax] (32-byte load) */
    uint8_t code[]={0xc5,0xf5,0xfe,0x00};
    decoded d; if (decode(code,&d)<=0 || d.b_src!=OPND_MEM || d.mem_bytes!=32){ printf("decode FAIL\n"); return 1; }

    avxemu_regfile rf; memset(&rf,0,sizeof rf);
    uint64_t ea = (uint64_t)(base + 0x1000 - 20);     /* 20 bytes before the unmapped page */
    rf.gpr[0] = ea;                                    /* rax */
    for (int i=0;i<32;i++) rf.ymm[1][i] = (uint8_t)(100+i);  /* ymm1 */
    rf.rip = (uint64_t)code;

    if (!avxemu_emulate(&d, &rf)) { printf("emulate returned 0\n"); return 1; }

    /* expected B = 20 bytes from the page then 12 zeros; OUT(d32 lanes) = ymm1 + B */
    uint8_t B[32]; for (int i=0;i<20;i++) B[i]=base[0x1000-20+i]; for (int i=20;i<32;i++) B[i]=0;
    int bad=0;
    for (int l=0;l<8;l++){
        uint32_t a,b,o; memcpy(&a,&rf.ymm[1][l*4],4); memcpy(&b,&B[l*4],4); memcpy(&o,&rf.ymm[0][l*4],4);
        if (o != (uint32_t)(a+b)) bad=1;
    }
    printf("over-read (no fault) + result: %s\n", bad?"FAIL":"ok");
    printf("OVERREAD TOTAL: %d failure(s)\n", bad);
    return bad?1:0;
}
