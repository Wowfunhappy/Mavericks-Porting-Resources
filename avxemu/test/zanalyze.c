/*
 * zanalyze.c — characterize every lzcnt/tzcnt site to design the fast path.
 * For each site: instruction length, and whether the *next* instruction is a
 * flags consumer (Jcc / SETcc / CMOVcc) — i.e. whether a trampoline must
 * replicate lzcnt/tzcnt's ZF/CF. Also tallies operand form (reg vs mem).
 */
#include "lde.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t uleb(const uint8_t **p, const uint8_t *end){
    uint64_t r=0; int s=0; uint8_t b;
    do{ if(*p>=end) return r; b=*(*p)++; r|=(uint64_t)(b&0x7f)<<s; s+=7; }while(b&0x80);
    return r;
}

/* Is the instruction at p a flags consumer (reads ZF/CF/etc.)? */
static int is_flag_consumer(const uint8_t *p, const uint8_t *end){
    const uint8_t *q=p;
    while(q<end){ uint8_t c=*q;
        if(c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65)q++; else break; }
    if(q<end && (*q&0xF0)==0x40) q++;                 /* REX */
    if(q>=end) return 0;
    if(*q>=0x70 && *q<=0x7F) return 1;                /* Jcc short */
    if(*q==0x0F && q+1<end){
        uint8_t o=q[1];
        if(o>=0x80 && o<=0x8F) return 1;              /* Jcc near */
        if(o>=0x90 && o<=0x9F) return 1;              /* SETcc */
        if(o>=0x40 && o<=0x4F) return 1;              /* CMOVcc */
    }
    return 0;
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

    long total=0, lenhist[16]={0}, mem=0, reg=0, flagcons=0, isz32=0, isz64=0;
    uint8_t*text=buf+text_off;
    for(size_t i=0;i<nf;i++){
        uint64_t fstart=fn[i],fend=fn[i+1];
        if(fstart<text_vm||fstart>=text_vm+text_size)continue;
        if(fend>text_vm+text_size)fend=text_vm+text_size;
        size_t res[256];
        long rn=lde_scan_func(text,fstart-text_vm,fend-text_vm,(size_t)(fsz-text_off),0,res,256);
        for(long k=0;k<rn;k++){
            size_t off=res[k];
            int zk,o2; int len=x86_len(text+off,text+(fend-text_vm),&zk,&o2);
            if(len<=0||len>15){ continue; }
            total++; lenhist[len]++;
            /* operand form: find modrm to see reg(mod==3) vs mem */
            const uint8_t*q=text+off; const uint8_t*qe=text+(fend-text_vm);
            int rexw=0;
            while(q<qe){ uint8_t c=*q; if(c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65)q++; else break;}
            if(q<qe&&(*q&0xF0)==0x40){rexw=(*q>>3)&1;q++;}
            if(q+2<qe && q[0]==0x0F && (q[1]==0xBD||q[1]==0xBC)){
                uint8_t modrm=q[2];
                if((modrm>>6)==3) reg++; else mem++;
            }
            if(rexw)isz64++; else isz32++;
            if(is_flag_consumer(text+off+len, qe)) flagcons++;
        }
    }
    printf("total sites: %ld\n",total);
    printf("length histogram:\n");
    for(int l=1;l<=12;l++) if(lenhist[l]) printf("  %2d bytes: %ld\n",l,lenhist[l]);
    printf("operand: reg=%ld mem=%ld\n",reg,mem);
    printf("opsize: 32-bit=%ld 64-bit=%ld\n",isz32,isz64);
    printf("sites whose NEXT insn consumes flags (Jcc/SETcc/CMOVcc): %ld\n",flagcons);
    printf("sites >=5 bytes (fit jmp rel32): %ld\n", total - lenhist[1]-lenhist[2]-lenhist[3]-lenhist[4]);
    printf("sites <5 bytes (too short for jmp rel32): %ld\n", lenhist[4]+lenhist[3]);
    return 0;
}
