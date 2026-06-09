/*
 * runanalyze.c — how do the *faulting* instructions cluster?
 *
 * Classifies each instruction that would #UD on the Ivy Bridge target
 * (lacks AVX2/FMA/BMI/LZCNT/MOVBE; has AVX1+F16C) and groups consecutive
 * faulting instructions into runs. A run >=5 bytes can be trampolined with one
 * jmp and no relocation (the thunk just emulates each instruction). A faulting
 * site whose whole run is <5 bytes is the only case that needs true relocation.
 */
#include "lde.h"
#include "decode.h"
#include "vexops.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t uleb(const uint8_t**p,const uint8_t*e){uint64_t r=0;int s=0;uint8_t b;
    do{if(*p>=e)return r;b=*(*p)++;r|=(uint64_t)(b&0x7f)<<s;s+=7;}while(b&0x80);return r;}

/* AVX2-only ops fault at ANY width; base-SSE ops only fault at 256-bit (VEX.L). */
static int avx2_only(vex_op op){
    switch(op){
    case VPSLLVD: case VPSLLVQ: case VPSRLVD: case VPSRLVQ: case VPSRAVD:
    case VPBROADCASTB: case VPBROADCASTW: case VPBROADCASTD: case VPBROADCASTQ:
    case VBROADCASTI128: case VPBLENDD:
    case VEXTRACTI128: case VINSERTI128: case VPERM2I128:
    case VPERMQ: case VPERMD: case VPERMPD: case VPERMPS:
        return 1;
    default: return 0;
    }
}
static int faults(const decoded *d){
    if(d->is_bmi) return 1;                       /* BMI1/2, LZCNT, TZCNT, MOVBE */
    if(d->op>=VFMADD132 && d->op<=VFNMSUB231) return 1;   /* FMA */
    if(d->op==VCVTPH2PS) return 0;                /* F16C present on target */
    if(avx2_only(d->op)) return 1;                /* AVX2-only at any width */
    return d->wide;                               /* base SSE: only 256-bit faults */
}

int main(int argc,char**argv){
    if(argc<2){fprintf(stderr,"usage: %s bin\n",argv[0]);return 2;}
    FILE*f=fopen(argv[1],"rb");if(!f){perror("bin");return 2;}
    fseek(f,0,SEEK_END);long fsz=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t*buf=malloc(fsz);if(fread(buf,1,fsz,f)!=(size_t)fsz)return 2;fclose(f);

    uint32_t ncmds=*(uint32_t*)(buf+16);
    uint64_t textseg_vm=0,text_vm=0,text_size=0;long text_off=0;uint32_t fs_off=0,fs_size=0;
    const uint8_t*lc=buf+32;
    for(uint32_t i=0;i<ncmds;i++){uint32_t cmd=*(uint32_t*)lc,sz=*(uint32_t*)(lc+4);
        if(cmd==0x19){if(!memcmp(lc+8,"__TEXT",7)){textseg_vm=*(uint64_t*)(lc+24);
            uint32_t ns=*(uint32_t*)(lc+64);const uint8_t*s=lc+72;
            for(uint32_t j=0;j<ns;j++,s+=80)if(!memcmp(s,"__text",7)){
                text_vm=*(uint64_t*)(s+32);text_size=*(uint64_t*)(s+40);text_off=*(uint32_t*)(s+48);}}}
        else if(cmd==0x26){fs_off=*(uint32_t*)(lc+8);fs_size=*(uint32_t*)(lc+12);}lc+=sz;}

    const uint8_t*p=buf+fs_off,*fe=p+fs_size;uint64_t addr=textseg_vm;
    uint64_t*fn=malloc((fs_size+1)*sizeof(uint64_t));size_t nf=0;
    while(p<fe){uint64_t d=uleb(&p,fe);if(!d)break;addr+=d;fn[nf++]=addr;}
    fn[nf]=text_vm+text_size;

    uint8_t*text=buf+text_off; size_t span=(size_t)(fsz-text_off);
    long faulting=0, in_run_ge5=0, isolated=0, runs=0, runs_ge5=0;
    long faulting_bmi=0, faulting_vec=0, runlen_hist[12]={0};
    for(size_t fi=0;fi<nf;fi++){
        uint64_t fs=fn[fi],fnd=fn[fi+1];
        if(fs<text_vm||fs>=text_vm+text_size)continue;
        if(fnd>text_vm+text_size)fnd=text_vm+text_size;
        size_t q=fs-text_vm, end=fnd-text_vm;
        /* one linear pass; track current run of faulting instrs */
        long run_bytes=0, run_count=0;
        while(q<end){
            int zk,o2; int llen=x86_len(text+q,text+span,&zk,&o2);
            if(llen<=0) break;
            decoded d; int dl=decode(text+q,&d);
            int isfault = (dl>0 && d.op && faults(&d));
            if(isfault){
                faulting++; if(d.is_bmi)faulting_bmi++; else faulting_vec++;
                run_bytes+=llen; run_count++;
            } else {
                if(run_count){ runs++; if(run_bytes<12)runlen_hist[run_bytes>11?11:run_bytes]++;
                    if(run_bytes>=5){runs_ge5++; in_run_ge5+=run_count;} else isolated+=run_count; }
                run_bytes=0; run_count=0;
            }
            q+=llen;
        }
        if(run_count){ runs++; if(run_bytes<12)runlen_hist[run_bytes>11?11:run_bytes]++;
            if(run_bytes>=5){runs_ge5++; in_run_ge5+=run_count;} else isolated+=run_count; }
    }
    printf("%s\n",argv[1]);
    printf("  faulting instructions (Ivy Bridge): %ld   (bmi/scalar=%ld vector=%ld)\n",faulting,faulting_bmi,faulting_vec);
    printf("  runs of consecutive faulting insns: %ld   (>=5 bytes: %ld)\n",runs,runs_ge5);
    printf("  faulting sites in a >=5-byte run (trampolineable, no relocation): %ld\n",in_run_ge5);
    printf("  faulting sites whose entire run is <5 bytes (need relocation):    %ld\n",isolated);
    printf("  run-byte-length histogram (1..>=11):"); for(int l=1;l<12;l++) printf(" %d:%ld",l,runlen_hist[l]); printf("\n");
    return 0;
}
