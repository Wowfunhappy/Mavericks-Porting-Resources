/*
 * decode.c — decode the AVX2/FMA/F16C (VEX) and BMI/LZCNT/MOVBE instructions
 * that fault on an AVX1 target into a normalized `decoded` (op + operand
 * descriptors + length). Anything else returns 0 (handler chains onward).
 *
 * Only the instruction forms that actually appear / can fault are decoded;
 * the classifier is by (map, pp, W, opcode).
 */

#include "decode.h"
#include <string.h>

/* parse ModRM (+SIB+disp) at p. rexX/rexB are the extension bits (0/1).
 * Fills memory fields; *rm_reg set to the rm register (if mod==3) else -1.
 * Returns bytes consumed. */
static int parse_modrm(const uint8_t *p, int rexX, int rexB,
                       decoded *d, int *rm_reg) {
    uint8_t modrm = p[0];
    int mod = modrm >> 6, rm = modrm & 7;
    int n = 1;
    *rm_reg = -1;
    d->has_mem = 0; d->base = OPND_NONE; d->index = OPND_NONE;
    d->scale = 1; d->disp = 0; d->rip_rel = 0;

    if (mod == 3) { *rm_reg = rm + (rexB ? 8 : 0); return n; }

    d->has_mem = 1;
    if (rm == 4) {                       /* SIB */
        uint8_t sib = p[n++];
        int ss = sib >> 6, idx = (sib >> 3) & 7, base = sib & 7;
        d->scale = 1 << ss;
        if (!(idx == 4 && rexX == 0)) d->index = idx + (rexX ? 8 : 0);
        if (base == 5 && mod == 0) { d->disp = *(const int32_t*)(p+n); n += 4; d->base = OPND_NONE; }
        else d->base = base + (rexB ? 8 : 0);
    } else if (mod == 0 && rm == 5) {    /* RIP-relative */
        d->rip_rel = 1; d->disp = *(const int32_t*)(p+n); n += 4;
    } else {
        d->base = rm + (rexB ? 8 : 0);
    }
    if (mod == 1) { d->disp = (int8_t)p[n]; n += 1; }
    else if (mod == 2) { d->disp = *(const int32_t*)(p+n); n += 4; }
    return n;
}

/* set a vector dst=reg, a=vvvv, b=rm(reg or mem), c=none */
static void wire_vvv(decoded *d, int reg, int vvvv, int rm_reg) {
    d->dst = reg; d->dst_kind = DST_YMM;
    d->a_src = vvvv;
    d->b_src = (rm_reg < 0) ? OPND_MEM : rm_reg;
    d->c_src = OPND_NONE;
}

static int decode_inner(const uint8_t *p, decoded *d);

/*
 * Compilers pad VEX instructions for alignment with legacy prefixes that are
 * legal before a VEX prefix (address-size 0x67 and the segment overrides).
 * Skip them, decode the real instruction, and fold the padding into the length.
 * (66/F2/F3/REX/LOCK are *not* legal VEX padding, so we don't skip those.)
 */
int decode(const uint8_t *p, decoded *d) {
    int pad = 0, seg = 0;
    while (pad < 8) {
        uint8_t c = p[pad];
        /* 0x67 and the cs/ss/ds/es overrides are no-ops in 64-bit -> true padding.
         * fs/gs DO change the effective address, so record them, don't discard. */
        if (c==0x67||c==0x2E||c==0x36||c==0x3E||c==0x26) pad++;
        else if (c==0x65) { seg = 1; pad++; }   /* gs */
        else if (c==0x64) { seg = 2; pad++; }   /* fs */
        else break;
    }
    int r = decode_inner(p + pad, d);
    if (r > 0) { d->len = (uint8_t)(d->len + pad); d->seg = (uint8_t)seg; return d->len; }
    return 0;
}

static int decode_inner(const uint8_t *p, decoded *d) {
    memset(d, 0, sizeof *d);
    d->a_src = d->b_src = d->c_src = OPND_NONE;
    d->dst = OPND_NONE; d->bmi_dst2 = OPND_NONE; d->base = OPND_NONE; d->index = OPND_NONE;

    /* ---- legacy-encoded: tzcnt / lzcnt (F3 0F BC/BD), movbe (0F38 F0/F1) ---- */
    {
        const uint8_t *q = p; int rexW = 0, rexR = 0, rexX = 0, rexB = 0;
        int has66 = 0, hasF3 = 0, hasLock = 0; int prefixes = 0;
        for (;;) {
            uint8_t c = *q;
            if (c == 0x66) { has66 = 1; q++; prefixes++; }
            else if (c == 0xF3) { hasF3 = 1; q++; prefixes++; }
            else if (c == 0xF2) { q++; prefixes++; }
            else if (c == 0xF0) { hasLock = 1; q++; prefixes++; }   /* our patched lzcnt/tzcnt */
            else if ((c & 0xF0) == 0x40) { rexW=(c>>3)&1; rexR=(c>>2)&1; rexX=(c>>1)&1; rexB=c&1; q++; }
            else break;
            if (prefixes > 8) break;
        }
        /* lzcnt/tzcnt: F3 0F BD/BC, OR the F0-prefixed form we patch in so the
         * instruction faults on a CPU that would otherwise run it as bsr/bsf. */
        if (q[0] == 0x0F && (hasF3 || hasLock) && (q[1] == 0xBC || q[1] == 0xBD)) {
            int reg, rm; int adv = parse_modrm(q+2, rexX, rexB, d, &rm);
            reg = ((q[2]>>3)&7) + (rexR?8:0);
            d->op = (q[1]==0xBC) ? BMI_TZCNT : BMI_LZCNT;
            d->is_bmi = 1; d->opsize = rexW ? 64 : 32;
            d->dst = reg; d->dst_kind = DST_GPR;
            d->a_src = (rm<0)?OPND_MEM:rm; d->mem_bytes = d->opsize/8;
            d->len = (uint8_t)((q+2-p) + adv);
            return d->len;
        }
        if (q[0] == 0x0F && q[1] == 0x38 && (q[2]==0xF0 || q[2]==0xF1)) {
            int reg, rm; int adv = parse_modrm(q+3, rexX, rexB, d, &rm);
            reg = ((q[3]>>3)&7) + (rexR?8:0);
            d->op = BMI_MOVBE; d->is_bmi = 1; d->opsize = rexW?64:(has66?16:32);
            d->mem_bytes = d->opsize/8;
            if (q[2]==0xF0) {           /* load: reg <- bswap(mem) */
                d->dst = reg; d->dst_kind = DST_GPR;
                d->a_src = (rm<0)?OPND_MEM:rm;
            } else {                    /* store: mem <- bswap(reg) */
                d->dst = OPND_MEM; d->dst_kind = DST_MEM;
                d->a_src = reg;
            }
            d->len = (uint8_t)((q+3-p) + adv);
            return d->len;
        }
    }

    /* ---- VEX ---- */
    int vex3; int mapsel, pp, W=0, L, vvvv, Rb, Xb, Bb;
    const uint8_t *q;
    if (p[0] == 0xC5) {            /* 2-byte VEX */
        uint8_t b1 = p[1];
        Rb = !((b1>>7)&1); Xb = 0; Bb = 0;
        vvvv = (~(b1>>3)) & 0xF; L = (b1>>2)&1; pp = b1 & 3;
        mapsel = 1; vex3 = 0; q = p + 2;
    } else if (p[0] == 0xC4) {     /* 3-byte VEX */
        uint8_t b1 = p[1], b2 = p[2];
        Rb = !((b1>>7)&1); Xb = !((b1>>6)&1); Bb = !((b1>>5)&1);
        mapsel = b1 & 0x1F;
        W = (b2>>7)&1; vvvv = (~(b2>>3))&0xF; L=(b2>>2)&1; pp=b2&3;
        vex3 = 1; q = p + 3;
    } else {
        return 0;
    }
    (void)vex3;
    int rexR = Rb?1:0, rexX = Xb?1:0, rexB = Bb?1:0;
    uint8_t opcode = q[0];
    int reg, rm;
    int adv = parse_modrm(q+1, rexX, rexB, d, &rm);
    reg = ((q[1]>>3)&7) + (rexR?8:0);
    const uint8_t *after = q + 1 + adv;
    d->wide = L ? 1 : 0;
    d->opsize = W ? 64 : 32;

    int imm_bytes = 0;
    int handled = 0;

    #define VVV(OP)   do{ d->op=(OP); wire_vvv(d,reg,vvvv,rm); handled=1; }while(0)
    #define VVI(OP)   do{ d->op=(OP); d->dst=reg; d->dst_kind=DST_YMM; d->a_src=OPND_NONE; \
                          d->b_src=(rm<0)?OPND_MEM:rm; imm_bytes=1; }while(0)
    #define VVVI(OP)  do{ d->op=(OP); wire_vvv(d,reg,vvvv,rm); imm_bytes=1; }while(0)
    #define UNB(OP)   do{ d->op=(OP); d->dst=reg; d->dst_kind=DST_YMM; d->a_src=OPND_NONE; \
                          d->b_src=(rm<0)?OPND_MEM:rm; handled=1; }while(0)
    #define UNA(OP)   do{ d->op=(OP); d->dst=reg; d->dst_kind=DST_YMM; \
                          d->a_src=(rm<0)?OPND_MEM:rm; d->b_src=OPND_NONE; handled=1; }while(0)

    if (mapsel == 1 && pp == 1) {            /* 66 0F */
        switch (opcode) {
        case 0xFC: VVV(VPADDB); break; case 0xFD: VVV(VPADDW); break;
        case 0xFE: VVV(VPADDD); break; case 0xD4: VVV(VPADDQ); break;
        case 0xF8: VVV(VPSUBB); break; case 0xF9: VVV(VPSUBW); break;
        case 0xFA: VVV(VPSUBD); break; case 0xFB: VVV(VPSUBQ); break;
        case 0xEC: VVV(VPADDSB); break; case 0xED: VVV(VPADDSW); break;
        case 0xDC: VVV(VPADDUSB); break; case 0xDD: VVV(VPADDUSW); break;
        case 0xE8: VVV(VPSUBSB); break; case 0xE9: VVV(VPSUBSW); break;
        case 0xD8: VVV(VPSUBUSB); break; case 0xD9: VVV(VPSUBUSW); break;
        case 0xDB: VVV(VPAND); break; case 0xDF: VVV(VPANDN); break;
        case 0xEB: VVV(VPOR); break; case 0xEF: VVV(VPXOR); break;
        case 0x74: VVV(VPCMPEQB); break; case 0x75: VVV(VPCMPEQW); break;
        case 0x76: VVV(VPCMPEQD); break;
        case 0x64: VVV(VPCMPGTB); break; case 0x65: VVV(VPCMPGTW); break;
        case 0x66: VVV(VPCMPGTD); break;
        case 0xDA: VVV(VPMINUB); break; case 0xEA: VVV(VPMINSW); break;
        case 0xDE: VVV(VPMAXUB); break; case 0xEE: VVV(VPMAXSW); break;
        case 0xE4: VVV(VPMULHUW); break; case 0xE5: VVV(VPMULHW); break;
        case 0xD5: VVV(VPMULLW); break; case 0xF4: VVV(VPMULUDQ); break;
        case 0xF5: VVV(VPMADDWD); break;
        case 0xE0: VVV(VPAVGB); break; case 0xE3: VVV(VPAVGW); break;
        case 0xF6: VVV(VPSADBW); break;
        case 0x60: VVV(VPUNPCKLBW); break; case 0x61: VVV(VPUNPCKLWD); break;
        case 0x62: VVV(VPUNPCKLDQ); break; case 0x6C: VVV(VPUNPCKLQDQ); break;
        case 0x68: VVV(VPUNPCKHBW); break; case 0x69: VVV(VPUNPCKHWD); break;
        case 0x6A: VVV(VPUNPCKHDQ); break; case 0x6D: VVV(VPUNPCKHQDQ); break;
        case 0x63: VVV(VPACKSSWB); break; case 0x6B: VVV(VPACKSSDW); break;
        case 0x67: VVV(VPACKUSWB); break;
        case 0xF1: VVV(VPSLLW); break; case 0xF2: VVV(VPSLLD); break; case 0xF3: VVV(VPSLLQ); break;
        case 0xD1: VVV(VPSRLW); break; case 0xD2: VVV(VPSRLD); break; case 0xD3: VVV(VPSRLQ); break;
        case 0xE1: VVV(VPSRAW); break; case 0xE2: VVV(VPSRAD); break;
        case 0x70: VVI(VPSHUFD); handled=1; break;
        case 0xD7: d->op=VPMOVMSKB; d->dst=reg; d->dst_kind=DST_GPR; d->b_src=rm; d->opsize=32; handled=1; break;
        case 0x71: case 0x72: case 0x73: {   /* shift by imm; reg field selects sub-op, vvvv=dst, rm=src */
            int ext = (q[1]>>3)&7; imm_bytes=1;
            d->dst = vvvv; d->dst_kind = DST_YMM; d->a_src = (rm<0)?OPND_MEM:rm; d->b_src=OPND_NONE;
            d->shift_imm = 1;
            if (opcode==0x71){ if(ext==6)d->op=VPSLLW; else if(ext==2)d->op=VPSRLW; else if(ext==4)d->op=VPSRAW; else return 0; }
            else if (opcode==0x72){ if(ext==6)d->op=VPSLLD; else if(ext==2)d->op=VPSRLD; else if(ext==4)d->op=VPSRAD; else return 0; }
            else { if(ext==6)d->op=VPSLLQ; else if(ext==2)d->op=VPSRLQ; else if(ext==7){d->op=VPSLLDQ;d->shift_imm=0;} else if(ext==3){d->op=VPSRLDQ;d->shift_imm=0;} else return 0; }
            break;
        }
        default: break;
        }
    } else if (mapsel == 1 && pp == 3 && opcode == 0x70) { VVI(VPSHUFLW); handled=1; }
    else if (mapsel == 1 && pp == 2 && opcode == 0x70) { VVI(VPSHUFHW); handled=1; }
    else if (mapsel == 2 && pp == 1) {       /* 66 0F38 */
        switch (opcode) {
        case 0x00: VVV(VPSHUFB); break; case 0x02: VVV(VPHADDD); break;
        case 0x04: VVV(VPMADDUBSW); break;
        case 0x08: VVV(VPSIGNB); break; case 0x09: VVV(VPSIGNW); break;
        case 0x0A: VVV(VPSIGND); break; case 0x0B: VVV(VPMULHRSW); break;
        case 0x1C: UNA(VPABSB); break; case 0x1D: UNA(VPABSW); break; case 0x1E: UNA(VPABSD); break;
        case 0x28: VVV(VPMULDQ); break; case 0x29: VVV(VPCMPEQQ); break; case 0x2B: VVV(VPACKUSDW); break;
        case 0x37: VVV(VPCMPGTQ); break;
        case 0x38: VVV(VPMINSB); break; case 0x39: VVV(VPMINSD); break;
        case 0x3A: VVV(VPMINUW); break; case 0x3B: VVV(VPMINUD); break;
        case 0x3C: VVV(VPMAXSB); break; case 0x3D: VVV(VPMAXSD); break;
        case 0x3E: VVV(VPMAXUW); break; case 0x3F: VVV(VPMAXUD); break;
        case 0x40: VVV(VPMULLD); break;
        case 0x16: VVV(VPERMPS); break; case 0x36: VVV(VPERMD); break;
        case 0x45: VVV(W?VPSRLVQ:VPSRLVD); break;
        case 0x47: VVV(W?VPSLLVQ:VPSLLVD); break;
        case 0x46: VVV(VPSRAVD); break;
        case 0x78: UNB(VPBROADCASTB); d->mem_bytes=1; break;
        case 0x79: UNB(VPBROADCASTW); d->mem_bytes=2; break;
        case 0x58: UNB(VPBROADCASTD); d->mem_bytes=4; break;
        case 0x59: UNB(VPBROADCASTQ); d->mem_bytes=8; break;
        case 0x5A: UNB(VBROADCASTI128); d->mem_bytes=16; break;
        case 0x20: UNB(VPMOVSXBW); d->mem_bytes=16; break; case 0x21: UNB(VPMOVSXBD); d->mem_bytes=8; break;
        case 0x22: UNB(VPMOVSXBQ); d->mem_bytes=4; break;  case 0x23: UNB(VPMOVSXWD); d->mem_bytes=16; break;
        case 0x24: UNB(VPMOVSXWQ); d->mem_bytes=8; break;  case 0x25: UNB(VPMOVSXDQ); d->mem_bytes=16; break;
        case 0x30: UNB(VPMOVZXBW); d->mem_bytes=16; break; case 0x31: UNB(VPMOVZXBD); d->mem_bytes=8; break;
        case 0x32: UNB(VPMOVZXBQ); d->mem_bytes=4; break;  case 0x33: UNB(VPMOVZXWD); d->mem_bytes=16; break;
        case 0x34: UNB(VPMOVZXWQ); d->mem_bytes=8; break;  case 0x35: UNB(VPMOVZXDQ); d->mem_bytes=16; break;
        case 0x13: UNB(VCVTPH2PS); d->mem_bytes=16; break;
        /* AVX2 masked move: 8C load (reg<-mask,mem), 8E store (mem<-mask,reg). W: D/Q. */
        case 0x8C: d->op = W?VPMASKMOVQ:VPMASKMOVD; d->dst=reg; d->dst_kind=DST_YMM;
                   d->a_src=vvvv; d->b_src=(rm<0)?OPND_MEM:rm; d->mem_bytes=L?32:16; handled=1; break;
        case 0x8E: d->op = W?VPMASKMOVQ:VPMASKMOVD; d->dst=OPND_MEM; d->dst_kind=DST_MEM;
                   d->a_src=vvvv; d->b_src=reg; d->mem_bytes=L?32:16; handled=1; break;
        default:
            /* FMA: 96-9F / A6-AF / B6-BF */
            if ((opcode>=0x96&&opcode<=0x9F)||(opcode>=0xA6&&opcode<=0xAF)||(opcode>=0xB6&&opcode<=0xBF)) {
                int order = (opcode&0xF0)==0x90?0:((opcode&0xF0)==0xA0?1:2);
                int low = opcode & 0x0F; int scalar = low & 1;
                int var = (low>=0x8&&low<=0x9)?0:(low<=0xB?1:(low<=0xD?2:3)); /* add/sub/nmadd/nmsub */
                static const vex_op tab[4][3] = {
                    {VFMADD132,VFMADD213,VFMADD231},{VFMSUB132,VFMSUB213,VFMSUB231},
                    {VFNMADD132,VFNMADD213,VFNMADD231},{VFNMSUB132,VFNMSUB213,VFNMSUB231}};
                if (low>=8) {
                    d->op = tab[var][order];
                    d->type = scalar ? (W?FT_SD:FT_SS) : (W?FT_PD:FT_PS);
                    d->dst=reg; d->dst_kind=DST_YMM; d->a_src=vvvv;
                    d->b_src=(rm<0)?OPND_MEM:rm; d->c_src=reg; handled=1;
                    d->mem_bytes = scalar ? (W?8:4) : (L?32:16);
                }
            }
            /* BMI2 in 0F38 with pp!=66 are handled below */
            break;
        }
    }
    /* BMI2 (0F38, pp = none/66/F2/F3) */
    if (!handled && !d->op && mapsel == 2) {
        d->is_bmi = 1; d->opsize = W?64:32; d->mem_bytes = d->opsize/8;  /* GPR-width mem operand (4 or 8), never 16/32 */
        int b_rm = (rm<0)?OPND_MEM:rm;
        switch (opcode) {
        case 0xF2: if(pp==0){ d->op=BMI_ANDN; d->dst=reg; d->dst_kind=DST_GPR; d->a_src=vvvv; d->b_src=b_rm; handled=1; } break;
        case 0xF3: if(pp==0){ int ext=(q[1]>>3)&7; d->dst=vvvv; d->dst_kind=DST_GPR; d->a_src=b_rm;
                       if(ext==1)d->op=BMI_BLSR; else if(ext==2)d->op=BMI_BLSMSK; else if(ext==3)d->op=BMI_BLSI; else {d->is_bmi=0;}
                       if(d->op)handled=1; } break;
        /* VEX pp: 1=66, 2=F3, 3=F2 */
        case 0xF5: d->dst=reg; d->dst_kind=DST_GPR;
                   if(pp==0){d->op=BMI_BZHI; d->a_src=b_rm; d->b_src=vvvv;}        /* val=rm,idx=vvvv */
                   else if(pp==3){d->op=BMI_PDEP; d->a_src=vvvv; d->b_src=b_rm;}   /* F2: src=vvvv,mask=rm */
                   else if(pp==2){d->op=BMI_PEXT; d->a_src=vvvv; d->b_src=b_rm;}   /* F3 */
                   if(d->op)handled=1; break;
        case 0xF6: if(pp==3){ d->op=BMI_MULX;          /* F2: lo->vvvv, hi->reg, src1 = implicit rdx */
                       d->dst=vvvv; d->bmi_dst2=reg; d->dst_kind=DST_GPR; d->bmi_s1_rdx=1; d->b_src=b_rm; handled=1; } break;
        case 0xF7: d->dst=reg; d->dst_kind=DST_GPR;
                   if(pp==0){d->op=BMI_BEXTR; d->a_src=b_rm; d->b_src=vvvv;}
                   else if(pp==1){d->op=BMI_SHLX; d->a_src=b_rm; d->b_src=vvvv;}   /* 66 */
                   else if(pp==2){d->op=BMI_SARX; d->a_src=b_rm; d->b_src=vvvv;}   /* F3 */
                   else if(pp==3){d->op=BMI_SHRX; d->a_src=b_rm; d->b_src=vvvv;}   /* F2 */
                   if(d->op)handled=1; break;
        default: d->is_bmi=0; break;
        }
        if(!handled) d->is_bmi=0;
    }
    else if (mapsel == 3 && pp == 1) {       /* 66 0F3A */
        switch (opcode) {
        case 0x0F: VVVI(VPALIGNR); break;
        case 0x0E: VVVI(VPBLENDW); break;
        case 0x02: VVVI(VPBLENDD); break;
        case 0x46: VVVI(VPERM2I128); break;
        case 0x00: VVI(VPERMQ); break;     /* W1 */
        case 0x01: VVI(VPERMPD); break;    /* W1 */
        case 0x4C: /* vpblendvb: is4 imm selects mask reg */
            d->op=VPBLENDVB; d->dst=reg; d->dst_kind=DST_YMM; d->a_src=vvvv;
            d->b_src=(rm<0)?OPND_MEM:rm; imm_bytes=1; break;
        case 0x38: /* vinserti128 dst=reg, a=vvvv, b=rm(128) */
            d->op=VINSERTI128; d->dst=reg; d->dst_kind=DST_YMM; d->a_src=vvvv;
            d->b_src=(rm<0)?OPND_MEM:rm; d->mem_bytes=16; imm_bytes=1; break;
        case 0x39: /* vextracti128 dst=rm(128, reg or mem), src=reg(ymm) */
            d->op=VEXTRACTI128; d->b_src=reg; imm_bytes=1; d->mem_bytes=16;
            if(rm<0){ d->dst=OPND_MEM; d->dst_kind=DST_MEM; } else { d->dst=rm; d->dst_kind=DST_YMM; d->wide=0; }
            break;
        default: break;
        }
    }
    else if (mapsel == 3 && pp == 3 && opcode == 0xF0) {   /* rorx: F2 0F3A F0 */
        d->op=BMI_RORX; d->is_bmi=1; d->opsize=W?64:32; d->mem_bytes=d->opsize/8; d->dst=reg; d->dst_kind=DST_GPR;
        d->a_src=(rm<0)?OPND_MEM:rm; imm_bytes=1; handled=1;
    }

    if (d->op == VEX_INVALID) return 0;

    /* immediate */
    if (imm_bytes) { d->imm = *after; d->has_imm = 1; }
    /* for blendvb the is4 high nibble selects the mask register (c) */
    if (d->op == VPBLENDVB) { d->c_src = (d->imm >> 4) & 0xF; d->has_imm = 0; }
    /* default memory size if not set */
    if (d->has_mem && d->mem_bytes == 0) d->mem_bytes = d->wide ? 32 : 16;
    /* shift-imm count carried in imm */
    if (d->shift_imm) { /* a holds data; handler builds count from imm */ }

    d->len = (uint8_t)((after - p) + imm_bytes);
    (void)handled;
    return d->len;
}
