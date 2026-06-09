/*
 * patchdiff.c — prove the in-memory patcher is byte-exact and safe.
 *
 * Runs the SAME scan/patch logic the dylib uses (lde_scan_func, patch=1) over a
 * copy of the binary's __text, then diffs the patched copy against the original.
 * Asserts that EVERY differing byte is F3->F0 and sits at a real lzcnt/tzcnt the
 * decoder agrees on — i.e. the patcher never touches an unrelated byte. Any
 * other kind of diff (count, value, location) is a hard failure.
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

    /* function starts */
    const uint8_t*p=buf+fs_off,*fe=p+fs_size; uint64_t addr=textseg_vm;
    uint64_t*fn=malloc((fs_size+1)*sizeof(uint64_t)); size_t nf=0;
    while(p<fe){ uint64_t d=uleb(&p,fe); if(!d)break; addr+=d; fn[nf++]=addr; }
    fn[nf]=text_vm+text_size;

    /* original text + a working copy we patch. The copy must span the whole
     * file-from-__text (not just the section) so switch jump-tables in the
     * trailing __TEXT data remain readable, exactly as in the live dylib. */
    size_t span = (size_t)(fsz - text_off);
    uint8_t *orig = buf + text_off;
    uint8_t *copy = malloc(span);
    memcpy(copy, orig, span);

    long found=0;
    for(size_t i=0;i<nf;i++){
        uint64_t fstart=fn[i],fend=fn[i+1];
        if(fstart<text_vm||fstart>=text_vm+text_size)continue;
        if(fend>text_vm+text_size)fend=text_vm+text_size;
        size_t res[256];
        found += lde_scan_func(copy, fstart-text_vm, fend-text_vm,
                               span, 1 /*patch*/, res, 256);
    }

    /* diff copy vs orig over the whole text section */
    long diffs=0, bad=0, not_zcnt=0;
    for(size_t k=0;k<text_size;k++){
        if(copy[k]==orig[k]) continue;
        diffs++;
        if(!(orig[k]==0xF3 && copy[k]==0xF0)){
            if(bad<10) printf("  BAD DIFF at +%zu: %02x -> %02x (not F3->F0)\n",k,orig[k],copy[k]);
            bad++; continue;
        }
        /* the F3 we flipped must begin a real lzcnt/tzcnt in the ORIGINAL bytes */
        int zk,o2; int len=x86_len(orig+k, orig+text_size, &zk, &o2);
        if(!(len>0 && zk && o2==0)){ if(not_zcnt<10) printf("  DIFF at +%zu not a zcnt start (zk=%d o2=%d len=%d)\n",k,zk,o2,len); not_zcnt++; }
    }

    printf("%s\n", argv[1]);
    printf("  found(patched): %ld   diffs: %ld   non-F3F0: %ld   non-zcnt: %ld\n",
           found, diffs, bad, not_zcnt);
    int okfail = (diffs==found) && (bad==0) && (not_zcnt==0);
    printf("  RESULT: %s (diffs==found:%d, all F3->F0:%d, all real zcnt:%d)\n",
           okfail?"PASS":"FAIL", diffs==found, bad==0, not_zcnt==0);
    return okfail?0:1;
}
