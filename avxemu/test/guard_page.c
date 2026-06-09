#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
int main(){
    uint8_t *base = mmap(0,0x2000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    mprotect(base+0x1000,0x1000,PROT_NONE);           /* guard page (mapped, no access) */
    volatile uint64_t v; __asm__ volatile("movq (%1),%0":"=r"(v):"r"(base+0x1000+12));
    printf("MASKED guard page (BAD): v=0x%llx\n",(unsigned long long)v); return 0;
}
