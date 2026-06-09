/*
 * zdecode.c — cross-check the emulator's decoder (decode.c) against the length
 * decoder (lde.c) over EVERY real lzcnt/tzcnt encoding in the binary, in both
 * the original F3 form and the patched F0/lock form. For each site the emulator
 * must (a) recognise it as LZCNT/TZCNT, (b) agree on the length, and (c) give
 * the same answer for the patched byte. Covers the long memory/SIB/disp forms
 * that the otool-based bintest can't reach (otool can't decode lzcnt at all).
 */
#include "lde.h"
#include "decode.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t uleb(const uint8_t **p, const uint8_t *end){
    uint64_t r=0; int s=0; uint8_t b;
    do{ if(*p>=end) return r; b=*(*p)++; r|=(uint64_t)(b&0x7f)<<s; s+=7; }while(b&0x80);
    return r;
}

int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"usage: %s bin\n",argv[0]);return 2;}
    FILE*f=fopen(argv[1],"rb"); if(!f){perror("bin");return 2;}
    fseek(f,0,SEEK_END); long fsz=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t*buf=malloc(fsz); if(fread(buf,1,fsz,f)!=(size_t)fsz)return 2; fclose(f);

    uint32_t ncmds=*(uint32_t*)(buf+16);
    uint64_t textseg_vm=0,text_vm=0,text_size=0; long text_off=0; uint32_t fs_off=0,fs_size=0;
    const uint8_t*lc=buf+32;
    for(uint32_t i=0;i<ncmds;i++){ uint32_t cmd=*(uint32_t*)lc,sz=*(uint32_t*)(lc+4);
        if(cmd==0x19){ if(!memcmp(lc+8,"__TEXT",7)){ textseg_vm=*(uint64_t*)(lc+24);
            uint32_t nsect=*(uint32_t*)(lc+64); const uint8_t*s=lc+72;
            for(uint32_t j=0;j<nsect;j++,s+=80) if(!memcmp(s,"__text",7)){
                text_vm=*(uint64_t*)(s+32); text_size=*(uint64_t*)(s+40); text_off=*(uint32_t*)(s+48);} } }
        else if(cmd==0x26){ fs_off=*(uint32_t*)(lc+8); fs_size=*(uint32_t*)(lc+12);} lc+=sz; }

    const uint8_t*p=buf+fs_off,*fe=p+fs_size; uint64_t addr=textseg_vm;
    uint64_t*fn=malloc((fs_size+1)*sizeof(uint64_t)); size_t nf=0;
    while(p<fe){ uint64_t d=uleb(&p,fe); if(!d)break; addr+=d; fn[nf++]=addr; }
    fn[nf]=text_vm+text_size;

    uint8_t *text=buf+text_off;
    long total=0, op_mismatch=0, len_mismatch=0, patched_mismatch=0, lenhist[16]={0};
    for(size_t i=0;i<nf;i++){
        uint64_t fstart=fn[i],fend=fn[i+1];
        if(fstart<text_vm||fstart>=text_vm+text_size)continue;
        if(fend>text_vm+text_size)fend=text_vm+text_size;
        size_t res[256];
        long rn=lde_scan_func(text,fstart-text_vm,fend-text_vm,(size_t)(fsz-text_off),0,res,256);
        for(long k=0;k<rn;k++){
            size_t off=res[k];
            int zk,o2; int llen=x86_len(text+off,text+(fend-text_vm),&zk,&o2);
            if(llen<=0||!zk) continue;
            total++; if(llen<16) lenhist[llen]++;

            /* (1) emulator decoder on the original F3 bytes */
            decoded d; int dlen=decode(text+off,&d);
            int want = (zk==1)?BMI_LZCNT:BMI_TZCNT;
            if(!(d.is_bmi && d.op==want)){ if(op_mismatch<8) printf("  OP mismatch @+%zx: emu op=%d want=%d\n",off,d.op,want); op_mismatch++; }
            if(dlen!=llen){ if(len_mismatch<8) printf("  LEN mismatch @+%zx: emu=%d lde=%d\n",off,dlen,llen); len_mismatch++; }

            /* (2) emulator decoder on the patched F0 form */
            uint8_t tmp[16]; int n = (llen<16)?llen:15; memcpy(tmp,text+off,n);
            if(o2>=0 && o2<n && tmp[o2]==0xF3) tmp[o2]=0xF0;
            decoded d2; int dlen2=decode(tmp,&d2);
            if(!(d2.is_bmi && d2.op==want && dlen2==llen)){
                if(patched_mismatch<8) printf("  PATCHED mismatch @+%zx: op=%d len=%d (want op=%d len=%d)\n",off,d2.op,dlen2,want,llen);
                patched_mismatch++;
            }
        }
    }
    printf("%s\n  sites: %ld  op_mismatch: %ld  len_mismatch: %ld  patched_mismatch: %ld\n",
           argv[1], total, op_mismatch, len_mismatch, patched_mismatch);
    printf("  length histogram:");
    for(int l=1;l<13;l++) if(lenhist[l]) printf(" %d:%ld",l,lenhist[l]);
    printf("\n  RESULT: %s\n", (op_mismatch==0&&len_mismatch==0&&patched_mismatch==0)?"PASS":"FAIL");
    return (op_mismatch||len_mismatch||patched_mismatch)?1:0;
}
