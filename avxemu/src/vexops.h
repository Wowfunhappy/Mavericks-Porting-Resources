#ifndef AVXEMU_VEXOPS_H
#define AVXEMU_VEXOPS_H

#include <stdint.h>
#include "ymm.h"

/*
 * Internal opcode ids for the AVX2-delta the emulator must cover on an AVX1
 * target: AVX2 integer SIMD, FMA3, F16C, and the BMI1/BMI2/LZCNT/MOVBE scalar
 * ops. (AVX1 SIMD and SSE-VEX run native on the target; AVX-512 is
 * runtime-dispatched and dead there — neither is emulated.)
 *
 * Operand convention for the vector executor vec_exec(op, type, a,b,c, imm,...):
 *   a = src1  (VEX.vvvv)        b = src2 (ModRM.rm)        c = src3 / old dst
 * Unused operands are ignored per-op. This mirrors Intel's 3-operand VEX form
 * (dst, src1, src2); imm-form ops (vpshufd, vpermq, ...) put their vector
 * source in b.
 */
typedef enum {
    VEX_INVALID = 0,

    /* ---- integer add / sub (wrap + saturating) ---- */
    VPADDB, VPADDW, VPADDD, VPADDQ,
    VPSUBB, VPSUBW, VPSUBD, VPSUBQ,
    VPADDSB, VPADDSW, VPADDUSB, VPADDUSW,
    VPSUBSB, VPSUBSW, VPSUBUSB, VPSUBUSW,

    /* ---- bitwise logic ---- */
    VPAND, VPANDN, VPOR, VPXOR,

    /* ---- compare ---- */
    VPCMPEQB, VPCMPEQW, VPCMPEQD, VPCMPEQQ,
    VPCMPGTB, VPCMPGTW, VPCMPGTD, VPCMPGTQ,

    /* ---- min / max ---- */
    VPMINUB, VPMINUW, VPMINUD, VPMINSB, VPMINSW, VPMINSD,
    VPMAXUB, VPMAXUW, VPMAXUD, VPMAXSB, VPMAXSW, VPMAXSD,

    /* ---- multiply / multiply-add ---- */
    VPMULLW, VPMULLD, VPMULHW, VPMULHUW, VPMULHRSW, VPMULDQ, VPMULUDQ,
    VPMADDWD, VPMADDUBSW,

    /* ---- average / SAD / abs / sign / hadd ---- */
    VPAVGB, VPAVGW, VPSADBW,
    VPABSB, VPABSW, VPABSD,
    VPSIGNB, VPSIGNW, VPSIGND,
    VPHADDD,

    /* ---- shifts: scalar count (imm or xmm), variable, whole-lane byte ---- */
    VPSLLW, VPSLLD, VPSLLQ, VPSRLW, VPSRLD, VPSRLQ, VPSRAW, VPSRAD,
    VPSLLVD, VPSLLVQ, VPSRLVD, VPSRLVQ, VPSRAVD,
    VPSLLDQ, VPSRLDQ,

    /* ---- shuffles ---- */
    VPSHUFB, VPSHUFD, VPSHUFLW, VPSHUFHW,

    /* ---- pack / unpack ---- */
    VPACKSSWB, VPACKSSDW, VPACKUSWB, VPACKUSDW,
    VPUNPCKLBW, VPUNPCKHBW, VPUNPCKLWD, VPUNPCKHWD,
    VPUNPCKLDQ, VPUNPCKHDQ, VPUNPCKLQDQ, VPUNPCKHQDQ,

    /* ---- align / blend / movemask ---- */
    VPALIGNR, VPBLENDW, VPBLENDD, VPBLENDVB, VPMOVMSKB,

    /* ---- cross-lane: broadcast / extend / lane / permute ---- */
    VPBROADCASTB, VPBROADCASTW, VPBROADCASTD, VPBROADCASTQ, VBROADCASTI128,
    VPMOVZXBW, VPMOVZXBD, VPMOVZXBQ, VPMOVZXWD, VPMOVZXWQ, VPMOVZXDQ,
    VPMOVSXBW, VPMOVSXBD, VPMOVSXBQ, VPMOVSXWD, VPMOVSXWQ, VPMOVSXDQ,
    VEXTRACTI128, VINSERTI128, VPERM2I128,
    VPERMQ, VPERMD, VPERMPD, VPERMPS,

    /* ---- F16C ---- */
    VCVTPH2PS,

    /* ---- AVX2 masked move (per-lane conditional mem access; handled in handler.c) ---- */
    VPMASKMOVD, VPMASKMOVQ,

    /* ---- FMA3 (type chooses ss/sd/ps/pd) ---- */
    VFMADD132, VFMADD213, VFMADD231,
    VFMSUB132, VFMSUB213, VFMSUB231,
    VFNMADD132, VFNMADD213, VFNMADD231,
    VFNMSUB132, VFNMSUB213, VFNMSUB231,

    /* ---- BMI1 / BMI2 / LZCNT / TZCNT / MOVBE (GPR domain) ---- */
    BMI_ANDN, BMI_BLSI, BMI_BLSR, BMI_BLSMSK, BMI_BZHI, BMI_BEXTR,
    BMI_MULX, BMI_PDEP, BMI_PEXT, BMI_RORX, BMI_SARX, BMI_SHLX, BMI_SHRX,
    BMI_TZCNT, BMI_LZCNT, BMI_MOVBE,

    VEX_OP_COUNT
} vex_op;

/* FMA element type. */
enum { FT_SS = 0, FT_SD = 1, FT_PS = 2, FT_PD = 3 };

/*
 * Vector executor: out = op(a, b, c, imm). Returns 1 if handled, 0 if the op
 * is not a vector op / not implemented. gpr_out receives the 32-bit result of
 * VPMOVMSKB (zero-extended). `type` is only consulted for FMA ops.
 *
 * Implemented SSE-only in exec.c (compiled -mno-avx), so it is representative
 * of code that runs on the target CPU.
 */
int vec_exec(vex_op op, int type,
             const ymm256 *a, const ymm256 *b, const ymm256 *c,
             uint8_t imm, ymm256 *out, uint64_t *gpr_out);

/*
 * Scalar (BMI/LZCNT/TZCNT/MOVBE) executor. opsize is 32 or 64.
 *   dst   = primary result
 *   dst2  = MULX high half (else untouched)
 *   flags = updated for the ops that write RFLAGS (CF/ZF/SF/OF/PF), else copied
 * src1/src2 follow each op's Intel operand order (see exec_bmi.c).
 */
int bmi_exec(vex_op op, int opsize,
             uint64_t src1, uint64_t src2,
             uint64_t *dst, uint64_t *dst2, uint64_t *flags);

/* Human-readable mnemonic for diagnostics / unimplemented-op reporting. */
const char *vex_op_name(vex_op op);

#endif /* AVXEMU_VEXOPS_H */
