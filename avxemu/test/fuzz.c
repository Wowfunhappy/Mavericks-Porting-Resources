/*
 * fuzz.c — native-vs-emulated differential fuzzer.
 *
 * For each distinct register-only vector instruction in the real binary, run it
 *   (a) natively on this CPU, and
 *   (b) emulated (ud2-prefixed -> SIGILL handler),
 * with identical random YMM state, then compare ALL 16 output YMM registers.
 * This validates the *whole* pipeline — operand/register decoding (incl.
 * ymm8-15 via VEX extension bits), execution, and writeback (incl. upper-half
 * zeroing) — against the silicon, not just the math.
 *
 * Register-only (mod==3) instructions only: random GPR state would send memory
 * operands to unmapped addresses. Memory addressing is covered by injtest.
 *
 *   fuzz <binary> <cand.txt> <vmaddr_hex> <fileoff_dec>
 */
#include "decode.h"
#include "vexops.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

extern int avxemu_test_ud2;
extern const unsigned char fuzz_start[], fuzz_slot[], fuzz_epilogue[], fuzz_end[];

#define SLOT 24
static unsigned char *code;            /* RWX template buffer */
static long plen, elen;

/* assemble prologue + [ud2?]+insn+nop-pad(SLOT) + epilogue into code[] */
static void build(const uint8_t *insn, int ilen, int with_ud2){
    long o=0;
    memcpy(code+o, fuzz_start, plen); o+=plen;
    long s=o;
    if(with_ud2){ code[o++]=0x0F; code[o++]=0x0B; }
    memcpy(code+o, insn, ilen); o+=ilen;
    while(o < s+SLOT) code[o++]=0x90;
    memcpy(code+o, fuzz_epilogue, elen); o+=elen;
    /* x86 keeps I-cache coherent with the store stream across the upcoming
     * indirect call, so no explicit flush is needed. */
}

/* dedup set of encodings (FNV-1a of bytes) */
#define HN (1<<21)
static uint64_t *seen;
static int already(const uint8_t*b,int n){
    uint64_t h=1469598103934665603ull; for(int i=0;i<n;i++){ h^=b[i]; h*=1099511628211ull; }
    if(!h) h=1;
    uint64_t m=HN-1, i=h&m;
    while(seen[i]){ if(seen[i]==h) return 1; i=(i+1)&m; }
    seen[i]=h; return 0;
}

static uint64_t rng=0x12345678ull;
static uint64_t xs(void){uint64_t x=rng;x^=x<<13;x^=x>>7;x^=x<<17;return rng=x;}

int main(int argc,char**argv){
    if(argc<5){ fprintf(stderr,"usage: %s bin cand vmaddr fileoff\n",argv[0]); return 2; }
    uint64_t vmaddr=strtoull(argv[3],0,16); long fileoff=atol(argv[4]);
    FILE*bf=fopen(argv[1],"rb"); if(!bf){perror("bin");return 2;}
    fseek(bf,0,SEEK_END); long fsz=ftell(bf); fseek(bf,0,SEEK_SET);
    uint8_t*buf=malloc(fsz); if(fread(buf,1,fsz,bf)!=(size_t)fsz)return 2; fclose(bf);
    FILE*cf=fopen(argv[2],"r"); if(!cf){perror("cand");return 2;}

    plen=fuzz_slot-fuzz_start; elen=fuzz_end-fuzz_slot;
    code=mmap(0,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0);
    if(code==MAP_FAILED){perror("mmap");return 2;}
    seen=calloc(HN,sizeof*seen);
    void *st; if(posix_memalign(&st,32,1024)) return 2;
    uint8_t out_nat[512];
    avxemu_test_ud2=1;

    long tested=0,distinct=0,mism=0; int shown=0;
    char line[256],mnem[64]; unsigned long long start,end;
    while(fgets(line,sizeof line,cf)){
        if(sscanf(line,"%llx %63s %llx",&start,mnem,&end)!=3) continue;
        long ilen=(long)(end-start); if(ilen<=0||ilen>15) continue;
        long foff=(long)(start-vmaddr)+fileoff; if(foff<0||foff+15>fsz) continue;
        decoded d; int len=decode(buf+foff,&d);
        if(len<=0||d.is_bmi||d.has_mem||d.op==VPMOVMSKB||d.dst_kind!=DST_YMM) continue;
        if(already(buf+foff,ilen)) continue;
        distinct++;
        for(int it=0;it<8;it++){
            uint64_t *in=(uint64_t*)st; for(int k=0;k<64;k++) in[k]=xs();   /* 512 bytes input */
            memset((uint8_t*)st+512,0,512);
            build(buf+foff,ilen,0); ((void(*)(void*))code)(st); memcpy(out_nat,(uint8_t*)st+512,512);
            memset((uint8_t*)st+512,0,512);
            build(buf+foff,ilen,1); ((void(*)(void*))code)(st);
            tested++;
            if(memcmp(out_nat,(uint8_t*)st+512,512)!=0){
                mism++;
                if(shown++<10){
                    fprintf(stderr,"MISMATCH %-12s (op=%s) bytes=",mnem,vex_op_name(d.op));
                    for(int i=0;i<ilen;i++) fprintf(stderr,"%02x ",buf[foff+i]);
                    fprintf(stderr,"\n");
                }
                break;
            }
        }
    }
    fclose(cf);
    printf("distinct reg-only insns: %ld\n",distinct);
    printf("native-vs-emulated runs: %ld\n",tested);
    printf("state mismatches:        %ld\n",mism);
    return mism?1:0;
}
