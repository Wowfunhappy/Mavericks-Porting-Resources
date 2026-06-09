#ifndef AVXEMU_REGS_H
#define AVXEMU_REGS_H

#include <stdint.h>
#include "ymm.h"

/*
 * The slice of architectural state the emulator reads and writes.
 *
 * The SIGILL handler populates this from the signal's mcontext, runs exec(),
 * and writes back. The differential oracle builds it directly. Same struct,
 * same exec() — so the oracle tests exactly the code path the handler uses.
 *
 * GPR indexing follows the x86-64 hardware register-number encoding
 * (0=rax,1=rcx,2=rdx,3=rbx,4=rsp,5=rbp,6=rsi,7=rdi,8..15=r8..r15), which is
 * what ModRM/SIB/VEX.vvvv decode to.
 */
typedef struct {
    ymm256   ymm[16];   /* vector regs; xmm_i is the low 16 bytes of ymm_i  */
    uint64_t gpr[16];   /* general-purpose regs, hardware encoding order    */
    uint64_t rflags;    /* only CF/PF/ZF/SF/OF are modeled where ops set them */
    uint64_t rip;       /* updated by the handler, not exec()               */
} cpu_state;

/* RFLAGS bit positions (the ones BMI/logic ops touch). */
#define FLAG_CF (1ull << 0)
#define FLAG_PF (1ull << 2)
#define FLAG_ZF (1ull << 6)
#define FLAG_SF (1ull << 7)
#define FLAG_OF (1ull << 11)

#endif /* AVXEMU_REGS_H */
