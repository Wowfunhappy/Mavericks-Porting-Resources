/*
 * bugfind.c — pinpoint LDE length bugs without a disassembler oracle.
 *
 * For each function that doesn't decode exactly to its LC_FUNCTION_STARTS
 * boundary, find the first instruction whose length, adjusted by some small
 * delta, lets the rest of the function decode cleanly to the boundary. That
 * instruction's opcode bytes + delta tell me exactly what encoding I got wrong.
 * Tallies them so systematic bugs surface.
 */
#include "lde.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t uleb(const uint8_t **p, const uint8_t *e) {
    uint64_t r = 0; int s = 0; uint8_t b;
    do { if (*p >= e) return r; b = *(*p)++; r |= (uint64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80);
    return r;
}
static int allpad(uint8_t *q, uint8_t *e) {
    for (; q < e; q++) if (*q != 0 && *q != 0xCC && *q != 0x90) return 0; return 1;
}
/* decode [q,qe); 1 if it reaches qe exactly (padding-tolerant), else 0 */
static int clean(uint8_t *q, uint8_t *qe) {
    while (q < qe) {
        if ((size_t)(qe - q) <= 16 && allpad(q, qe)) return 1;
        int zk, off; int len = x86_len(q, qe, &zk, &off);
        if (len <= 0) return 0;
        q += len;
    }
    return q == qe;
}

struct tally { char key[24]; long n; };
static struct tally T[512]; static int nt = 0;
static void bump(const uint8_t *p, int delta) {
    char k[24]; snprintf(k, sizeof k, "%02x %02x %02x %02x d=%+d", p[0], p[1], p[2], p[3], delta);
    for (int i = 0; i < nt; i++) if (!strcmp(T[i].key, k)) { T[i].n++; return; }
    if (nt < 512) { strncpy(T[nt].key, k, 23); T[nt].n = 1; nt++; }
}
static int tcmp(const void *a, const void *b) { return (int)(((struct tally*)b)->n - ((struct tally*)a)->n); }

int main(int c, char **v) {
    FILE *f = fopen(v[1], "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz); if (fread(b, 1, sz, f) != (size_t)sz) return 2;
    uint32_t nc = *(uint32_t*)(b+16); uint64_t tsv=0,tv=0; long to=0; uint64_t ts=0; uint32_t fo=0,fss=0;
    const uint8_t *lc = b+32;
    for (uint32_t i=0;i<nc;i++){ uint32_t cmd=*(uint32_t*)lc,s=*(uint32_t*)(lc+4);
        if(cmd==0x19&&!memcmp(lc+8,"__TEXT",7)){tsv=*(uint64_t*)(lc+24);uint32_t ns=*(uint32_t*)(lc+64);const uint8_t*se=lc+72;
            for(uint32_t j=0;j<ns;j++,se+=80)if(!memcmp(se,"__text",7)){tv=*(uint64_t*)(se+32);ts=*(uint64_t*)(se+40);to=*(uint32_t*)(se+48);}}
        else if(cmd==0x26){fo=*(uint32_t*)(lc+8);fss=*(uint32_t*)(lc+12);} lc+=s; }
    const uint8_t *p=b+fo,*fe=p+fss; uint64_t ad=tsv; uint64_t*fn=malloc((fss+1)*8); size_t nf=0;
    while(p<fe){uint64_t d=uleb(&p,fe);if(!d)break;ad+=d;fn[nf++]=ad;} fn[nf]=tv+ts;

    long dirty=0, pinned=0, unpinned=0;
    for (size_t i=0;i<nf;i++){ uint64_t fs=fn[i],fend=fn[i+1];
        if(fs<tv||fs>=tv+ts)continue; if(fend>tv+ts)fend=tv+ts;
        uint8_t *q=b+to+(fs-tv),*qe=b+to+(fend-tv);
        if (clean(q,qe)) continue;
        dirty++;
        /* find first instruction whose length is off by delta */
        uint8_t *r=q; int found=0;
        while(r<qe){
            int zk,off; int len=x86_len(r,qe,&zk,&off); if(len<=0)break;
            for(int delta=-6;delta<=6;delta++){ if(delta==0)continue;
                uint8_t *nx=r+len+delta; if(nx<r||nx>qe)continue;
                if(clean(nx,qe)){ bump(r,delta); found=1; break; }
            }
            if(found)break;
            r+=len;
        }
        if(found)pinned++; else unpinned++;
    }
    qsort(T,nt,sizeof T[0],tcmp);
    printf("dirty=%ld pinned=%ld unpinned(embedded-data?)=%ld\n", dirty, pinned, unpinned);
    printf("top mismeasured instruction patterns (bytes... delta, count):\n");
    for(int i=0;i<nt&&i<25;i++) printf("  %-22s  %ld\n", T[i].key, T[i].n);
    return 0;
}
