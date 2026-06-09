/*
 * bintest.c — validate the decoder against the *real* Claude Code binary.
 *
 * For every VEX/BMI instruction the disassembler found in __text, feed its real
 * bytes to decode() and check:
 *   - length: decode().len must equal the true instruction length (next addr -
 *     this addr). A wrong length corrupts RIP on the target, so this is the
 *     decisive check — run across every addressing mode/register the compiler
 *     actually emitted.
 *   - op consistency: our op name must be a prefix of the disassembler mnemonic.
 *   - coverage: instructions we return 0 for are bucketed by mnemonic so any
 *     AVX2/FMA/BMI form we *should* handle but don't shows up immediately.
 *
 * Input: a "start_hex mnemonic end_hex" list (from otool -tV) + the binary +
 * the __text vmaddr/fileoff so addresses map to file offsets.
 *
 *   bintest <binary> <cand.txt> <vmaddr_hex> <fileoff_dec>
 */
#include "decode.h"
#include "vexops.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_prefix(const char *full, const char *pre){
    size_t n=strlen(pre); return strncmp(full,pre,n)==0;
}

struct bucket { char name[32]; long count; };
static struct bucket buckets[512]; static int nb=0;
static void bump(const char *m){
    for(int i=0;i<nb;i++) if(!strcmp(buckets[i].name,m)){ buckets[i].count++; return; }
    if(nb<512){ strncpy(buckets[nb].name,m,31); buckets[nb].count=1; nb++; }
}
static int bk_cmp(const void*a,const void*b){ return (int)(((const struct bucket*)b)->count-((const struct bucket*)a)->count); }

int main(int argc,char**argv){
    if(argc<5){ fprintf(stderr,"usage: %s bin cand vmaddr fileoff\n",argv[0]); return 2; }
    uint64_t vmaddr=strtoull(argv[3],0,16); long fileoff=atol(argv[4]);
    FILE*bf=fopen(argv[1],"rb"); if(!bf){perror("bin");return 2;}
    fseek(bf,0,SEEK_END); long fsz=ftell(bf); fseek(bf,0,SEEK_SET);
    uint8_t*buf=malloc(fsz); if(fread(buf,1,fsz,bf)!=(size_t)fsz){perror("read");return 2;} fclose(bf);
    FILE*cf=fopen(argv[2],"r"); if(!cf){perror("cand");return 2;}

    long handled=0, lenfail=0, opfail=0, skipped=0, total=0;
    char line[256], mnem[64]; unsigned long long start,end;
    int shown_len=0, shown_op=0;
    while(fgets(line,sizeof line,cf)){
        if(sscanf(line,"%llx %63s %llx",&start,mnem,&end)!=3) continue;
        total++;
        long truelen=(long)(end-start);
        if(truelen<=0||truelen>15){ skipped++; continue; }       /* padding/data boundary */
        long foff=(long)(start-vmaddr)+fileoff;
        if(foff<0||foff+15>fsz){ skipped++; continue; }
        decoded d;
        int len=decode(buf+foff,&d);
        if(len<=0){ bump(mnem);                                   /* not ours (AVX1/AVX-512/SSE) */
            if(argc>=6 && !strcmp(mnem,argv[5])){
                fprintf(stderr,"  @%llx %-12s bytes=",start,mnem);
                for(int i=0;i<truelen&&i<12;i++) fprintf(stderr,"%02x ",buf[foff+i]);
                fprintf(stderr,"\n");
            }
            continue; }
        handled++;
        if(len!=truelen){
            lenfail++;
            if(shown_len++<12){
                fprintf(stderr,"LEN  @%llx %-12s mylen=%d true=%ld bytes=",start,mnem,len,truelen);
                for(int i=0;i<truelen&&i<12;i++) fprintf(stderr,"%02x ",buf[foff+i]);
                fprintf(stderr,"(op=%s)\n",vex_op_name(d.op));
            }
            continue;
        }
        if(!has_prefix(mnem,vex_op_name(d.op))){
            opfail++;
            if(shown_op++<12) fprintf(stderr,"OP   @%llx disasm=%-12s myop=%s\n",start,mnem,vex_op_name(d.op));
        }
    }
    fclose(cf);

    printf("candidates:        %ld\n",total);
    printf("skipped (pad/oob): %ld\n",skipped);
    printf("decoded by us:     %ld\n",handled);
    printf("  length mismatch: %ld\n",lenfail);
    printf("  op mismatch:     %ld\n",opfail);
    printf("not ours:          %ld distinct mnemonics\n",total-handled-skipped>0?(long)nb:0);
    qsort(buckets,nb,sizeof buckets[0],bk_cmp);
    printf("top 'not ours' mnemonics (expect AVX1/AVX-512/SSE-VEX only):\n");
    for(int i=0;i<nb&&i<512;i++) printf("    %8ld  %s\n",buckets[i].count,buckets[i].name);
    return (lenfail||opfail)?1:0;
}
