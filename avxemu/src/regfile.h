#ifndef AVXEMU_REGFILE_H
#define AVXEMU_REGFILE_H

#include <stdint.h>
#include "decode.h"

/*
 * A flat snapshot of the machine state the emulator needs. Both entry points
 * fill one and hand it to avxemu_emulate():
 *   - the SIGILL handler copies it out of the signal's AVX mcontext;
 *   - the trampoline saver (tramp.s) spills the live registers into it.
 * ymm is contiguous 32-byte (low xmm + high half), gpr is x86 register order
 * (0=rax,1=rcx,2=rdx,3=rbx,4=rsp,5=rbp,6=rsi,7=rdi,8..15=r8..r15).
 */
typedef struct {
    uint8_t  ymm[16][32];
    uint64_t gpr[16];
    uint64_t rflags;
    uint64_t rip;        /* address of the instruction (EA base for rip-rel; resume = rip + d->len) */
} avxemu_regfile;

/* Emulate one decoded instruction against rf, in place. Returns 1 on success,
 * 0 if the op is recognised-but-unimplemented (caller should fall back). */
int avxemu_emulate(const decoded *d, avxemu_regfile *rf);

#endif /* AVXEMU_REGFILE_H */
