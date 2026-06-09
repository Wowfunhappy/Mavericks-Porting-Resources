#ifndef AVXEMU_DECODE_H
#define AVXEMU_DECODE_H

#include <stdint.h>
#include "vexops.h"

/* Source/destination descriptors. >=0 is a register number; these sentinels
 * mark "unused" and "the one memory operand". */
#define OPND_NONE (-1)
#define OPND_MEM  (-2)

enum { DST_YMM = 0, DST_GPR, DST_MEM };

typedef struct {
    vex_op  op;
    int     type;        /* FMA element type (FT_*) */
    uint8_t len;         /* total instruction length in bytes */

    /* vector operands, mapped to vec_exec(a,b,c). OPND_* sentinels apply. */
    int8_t  a_src, b_src, c_src;

    /* destination */
    int8_t  dst;         /* register number, or OPND_MEM */
    uint8_t dst_kind;    /* DST_YMM / DST_GPR / DST_MEM */
    uint8_t wide;        /* 1 => 256-bit (VEX.L), 0 => 128-bit */

    uint8_t imm, has_imm;

    /* memory operand (when some slot / dst is OPND_MEM) */
    uint8_t has_mem;
    int8_t  base, index; /* register numbers or OPND_NONE */
    uint8_t scale;       /* 1/2/4/8 */
    int32_t disp;
    uint8_t rip_rel;
    uint8_t seg;         /* segment override: 0=none, 1=gs (0x65), 2=fs (0x64) */
    uint8_t mem_bytes;   /* bytes to load/store for the memory operand */
    uint8_t shift_imm;   /* scalar-count shift whose count is `imm` */

    /* BMI/scalar */
    uint8_t is_bmi;
    uint8_t opsize;      /* 32 or 64 */
    int8_t  bmi_dst2;    /* MULX high-half dest register, else OPND_NONE */
    uint8_t bmi_s1_rdx;  /* MULX: src1 is implicit RDX */
} decoded;

/*
 * Decode one instruction at p (length-limited to <=15). Returns the
 * instruction length on success, or 0 if it is not an instruction this
 * emulator handles (the handler then chains to the next SIGILL handler).
 */
int decode(const uint8_t *p, decoded *d);

#endif /* AVXEMU_DECODE_H */
