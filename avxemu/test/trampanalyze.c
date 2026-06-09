/*
 * trampanalyze.c — feasibility data for in-place trampolining.
 *
 * Walks the binary and, for every instruction the emulator recognizes (i.e. one
 * that faults on the target and currently goes through SIGILL), buckets it by:
 *   - length (a jmp rel32 trampoline needs >=5 bytes in place, no relocation)
 *   - reg-only vs memory operand
 *   - vector(256/128) vs BMI/scalar
 * so we can see what fraction is safely trampolineable without relocating
 * neighbouring instructions.
 */
#include "lde.h"
#include "decode.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t uleb(const uint8_t **p,const uint8_t*e){uint64_t r=0;int s=0;uint8_t b;
    do{if(*p>=e)return r;b=*(*p)++;r|=(uint64_t)(b&0x7f)<<s;s+=7;}while(b&0x80);return r;}

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
    long emulatable=0, ge5=0, lt5=0, mem=0, reg=0, bmi=0, vec=0, lenhist[20]={0};
    long ge5_reg=0, ge5_mem=0;
    for(size_t i=0;i<nf;i++){
        uint64_t fs=fn[i],fnd=fn[i+1];
        if(fs<text_vm||fs>=text_vm+text_size)continue;
        if(fnd>text_vm+text_size)fnd=text_vm+text_size;
        size_t q=fs-text_vm, end=fnd-text_vm;
        while(q<end){
            int zk,o2; int llen=x86_len(text+q,text+span,&zk,&o2);
            if(llen<=0){break;}
            decoded d; int dl=decode(text+q,&d);
            if(dl>0 && d.op){                       /* emulator recognizes -> faults/handled */
                emulatable++; if(dl<20)lenhist[dl]++;
                int has_mem=(d.a_src==OPND_MEM||d.b_src==OPND_MEM||d.dst_kind==DST_MEM);
                if(has_mem)mem++; else reg++;
                if(d.is_bmi)bmi++; else vec++;
                if(dl>=5){ge5++; if(has_mem)ge5_mem++; else ge5_reg++;} else lt5++;
            }
            q+=llen;
        }
    }
    printf("%s\n",argv[1]);
    printf("  emulator-recognized sites: %ld\n",emulatable);
    printf("  >=5 bytes (trampolineable in place): %ld   <5 bytes (need relocation): %ld\n",ge5,lt5);
    printf("    of >=5: reg-only=%ld  memory=%ld\n",ge5_reg,ge5_mem);
    printf("  class: bmi/scalar=%ld  vector=%ld   operand: reg=%ld mem=%ld\n",bmi,vec,reg,mem);
    printf("  length histogram:"); for(int l=2;l<16;l++) if(lenhist[l]) printf(" %d:%ld",l,lenhist[l]); printf("\n");
    return 0;
}
