/*
 * exec_bmi.c — BMI1/BMI2/LZCNT/TZCNT/MOVBE scalar emulation (GPR domain).
 *
 * Plain integer code; compiles anywhere. Operand order per op follows Intel
 * (see each case). opsize is 32 or 64; 32-bit results are zero-extended to 64
 * (writing a 32-bit GPR clears the upper half). Only architecturally *defined*
 * flags are produced; the differential oracle masks undefined flag bits.
 */

#include "vexops.h"
#include "regs.h"

static inline uint64_t maskz(uint64_t v, int bits) {
    return bits >= 64 ? v : (v & ((1ull << bits) - 1));
}
static inline int signbit_n(uint64_t v, int bits) {
    return (int)((v >> (bits - 1)) & 1);
}

/* Set the architecturally-defined flags (CF/ZF/SF/OF); leave PF/AF as-is. */
static void set_flags(uint64_t *flags, int cf, int zf, int sf, int of) {
    uint64_t f = *flags & ~(FLAG_CF|FLAG_ZF|FLAG_SF|FLAG_OF);
    if (cf) f |= FLAG_CF;
    if (zf) f |= FLAG_ZF;
    if (sf) f |= FLAG_SF;
    if (of) f |= FLAG_OF;
    *flags = f;
}

static uint64_t pdep(uint64_t v, uint64_t mask, int bits) {
    uint64_t r = 0, k = 0;
    for (int i = 0; i < bits; i++)
        if ((mask >> i) & 1) { if ((v >> k) & 1) r |= (1ull << i); k++; }
    return r;
}
static uint64_t pext(uint64_t v, uint64_t mask, int bits) {
    uint64_t r = 0, k = 0;
    for (int i = 0; i < bits; i++)
        if ((mask >> i) & 1) { if ((v >> i) & 1) r |= (1ull << k); k++; }
    return r;
}

int bmi_exec(vex_op op, int opsize,
             uint64_t s1, uint64_t s2,
             uint64_t *dst, uint64_t *dst2, uint64_t *flags) {
    s1 = maskz(s1, opsize);
    s2 = maskz(s2, opsize);
    uint64_t r = 0;

    switch (op) {
    case BMI_ANDN: /* dst = ~src1 & src2 */
        r = maskz(~s1 & s2, opsize);
        set_flags(flags, 0, r==0, signbit_n(r,opsize), 0);
        break;
    case BMI_BLSI: /* (-src) & src ; CF = src!=0 */
        r = maskz((0 - s1) & s1, opsize);
        set_flags(flags, s1!=0, r==0, signbit_n(r,opsize), 0);
        break;
    case BMI_BLSR: /* (src-1) & src ; CF = src==0 */
        r = maskz((s1 - 1) & s1, opsize);
        set_flags(flags, s1==0, r==0, signbit_n(r,opsize), 0);
        break;
    case BMI_BLSMSK: /* (src-1) ^ src ; CF = src==0 */
        r = maskz((s1 - 1) ^ s1, opsize);
        set_flags(flags, s1==0, r==0, signbit_n(r,opsize), 0);
        break;
    case BMI_BZHI: { /* src1=value(rm), src2=index(vvvv) */
        uint64_t idx = s2 & 0xFF;
        if (idx >= (uint64_t)opsize) r = s1;
        else r = maskz(s1 & ((1ull << idx) - 1), opsize);
        set_flags(flags, idx > (uint64_t)(opsize-1), r==0, signbit_n(r,opsize), 0);
        break;
    }
    case BMI_BEXTR: { /* src1=value(rm), src2=ctrl(vvvv) */
        uint64_t start = s2 & 0xFF, len = (s2 >> 8) & 0xFF;
        if (start >= (uint64_t)opsize) r = 0;
        else { r = s1 >> start; if (len < (uint64_t)opsize) r &= (1ull << len) - 1; r = maskz(r, opsize); }
        set_flags(flags, 0, r==0, 0, 0);
        break;
    }
    case BMI_MULX: { /* src1=rdx, src2=rm ; *dst=low, *dst2=high ; no flags */
        if (opsize == 32) { uint64_t p = (uint64_t)(uint32_t)s1 * (uint32_t)s2;
            *dst = (uint32_t)p; *dst2 = (uint32_t)(p >> 32); }
        else { unsigned __int128 p = (unsigned __int128)s1 * s2;
            *dst = (uint64_t)p; *dst2 = (uint64_t)(p >> 64); }
        return 1;
    }
    case BMI_PDEP: r = pdep(s1, s2, opsize); *dst = r; return 1; /* src1=vvvv, mask=rm */
    case BMI_PEXT: r = pext(s1, s2, opsize); *dst = r; return 1;
    case BMI_RORX: { int n = (int)(s2 & (opsize-1)); /* src1=rm, imm=s2 ; no flags */
        if (n==0) r = s1; else r = maskz((s1 >> n) | (s1 << (opsize-n)), opsize);
        *dst = r; return 1; }
    case BMI_SHLX: r = maskz(s1 << (s2 & (opsize-1)), opsize); *dst = r; return 1;
    case BMI_SHRX: r = maskz(s1 >> (s2 & (opsize-1)), opsize); *dst = r; return 1;
    case BMI_SARX: { int n = (int)(s2 & (opsize-1));
        if (opsize==32) r = (uint64_t)(uint32_t)((int32_t)(uint32_t)s1 >> n);
        else r = (uint64_t)((int64_t)s1 >> n);
        *dst = r; return 1; }
    case BMI_TZCNT: { int c=0; if (s1==0) { c=opsize; set_flags(flags,1,0,0,0); }
        else { while (!((s1>>c)&1)) c++; set_flags(flags,0,c==0,0,0); }
        *dst = (uint64_t)c; return 1; }
    case BMI_LZCNT: { int c=0; if (s1==0) { c=opsize; set_flags(flags,1,0,0,0); }
        else { while (!((s1>>(opsize-1-c))&1)) c++; set_flags(flags,0,c==0,0,0); }
        *dst = (uint64_t)c; return 1; }
    case BMI_MOVBE: {
        if (opsize==16) r = (uint16_t)(((s1&0xff)<<8)|((s1>>8)&0xff));
        else if (opsize==32) r = __builtin_bswap32((uint32_t)s1);
        else r = __builtin_bswap64(s1);
        *dst = r; return 1; }
    default:
        return 0;
    }
    *dst = r;
    return 1;
}
