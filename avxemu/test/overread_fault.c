#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
int main(){
    uint8_t *base = mmap(0,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    munmap(base+0x1000,0x1000); memset(base,0xAB,0x1000);
    volatile uint64_t v;
    __asm__ volatile("movq (%1), %0":"=r"(v):"r"(base+0x1000+12));
    printf("read=0x%llx -> %s\n",(unsigned long long)v, v==0?"ok (fixed+retried)":"FAIL");
    return v?1:0;
}
