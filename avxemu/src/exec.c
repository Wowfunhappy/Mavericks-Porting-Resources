/*
 * exec.c — SSE-only emulation of the AVX2/F16C vector trap set.
 *
 * COMPILE SSE4.2-ONLY (-msse4.2 -mno-avx). Verified to emit no VEX, so it is
 * safe to run on the target CPU (Sandy/Ivy Bridge: AVX1 + SSE4.2, no AVX2).
 *
 * Three strategies:
 *   - per-128-bit-lane ops  -> apply the SSE equivalent to low & high halves
 *   - runtime-imm / cross-lane ops -> explicit scalar code
 *   - FMA -> software correctly-rounded fused multiply-add (softfma.c)
 *
 * Every mapping is checked bit-for-bit against the real instruction by the
 * differential oracle on AVX2 hardware.
 */

#include "vexops.h"
#include "softfma.h"

#include <string.h>
#include <emmintrin.h> /* SSE2  */
#include <tmmintrin.h> /* SSSE3 */
#include <smmintrin.h> /* SSE4.1 */
#include <nmmintrin.h> /* SSE4.2 */

/* ---- 128-bit half access ---- */
static inline __m128i lo(const ymm256 *v){ return _mm_loadu_si128((const __m128i*)(v->b)); }
static inline __m128i hi(const ymm256 *v){ return _mm_loadu_si128((const __m128i*)(v->b+16)); }
static inline void put(ymm256 *o, __m128i l, __m128i h){
    _mm_storeu_si128((__m128i*)(o->b), l);
    _mm_storeu_si128((__m128i*)(o->b+16), h);
}

#define HALFOP(SSE)  do{ put(out, SSE(lo(a),lo(b)), SSE(hi(a),hi(b))); }while(0)
#define HALFOP1(SSE) do{ put(out, SSE(lo(a)),       SSE(hi(a)));       }while(0)
#define SHIFT(SSE)   do{ __m128i c_=_mm_loadl_epi64((const __m128i*)b->b); \
                         put(out, SSE(lo(a),c_), SSE(hi(a),c_)); }while(0)

/* per-lane (16-byte) helpers for scalar code */
static inline const uint8_t *lane(const ymm256 *v, int l){ return v->b + 16*l; }

int vec_exec(vex_op op, int type,
             const ymm256 *a, const ymm256 *b, const ymm256 *c,
             uint8_t imm, ymm256 *out, uint64_t *gpr_out)
{
    switch (op) {
    /* ---- add / sub (wrap) ---- */
    case VPADDB: HALFOP(_mm_add_epi8);  return 1;
    case VPADDW: HALFOP(_mm_add_epi16); return 1;
    case VPADDD: HALFOP(_mm_add_epi32); return 1;
    case VPADDQ: HALFOP(_mm_add_epi64); return 1;
    case VPSUBB: HALFOP(_mm_sub_epi8);  return 1;
    case VPSUBW: HALFOP(_mm_sub_epi16); return 1;
    case VPSUBD: HALFOP(_mm_sub_epi32); return 1;
    case VPSUBQ: HALFOP(_mm_sub_epi64); return 1;
    /* saturating */
    case VPADDSB:  HALFOP(_mm_adds_epi8);  return 1;
    case VPADDSW:  HALFOP(_mm_adds_epi16); return 1;
    case VPADDUSB: HALFOP(_mm_adds_epu8);  return 1;
    case VPADDUSW: HALFOP(_mm_adds_epu16); return 1;
    case VPSUBSB:  HALFOP(_mm_subs_epi8);  return 1;
    case VPSUBSW:  HALFOP(_mm_subs_epi16); return 1;
    case VPSUBUSB: HALFOP(_mm_subs_epu8);  return 1;
    case VPSUBUSW: HALFOP(_mm_subs_epu16); return 1;

    /* ---- logic.  andnot(x,y)=~x&y matches VPANDN dst=~src1&src2 ---- */
    case VPAND:  HALFOP(_mm_and_si128);    return 1;
    case VPANDN: HALFOP(_mm_andnot_si128); return 1;
    case VPOR:   HALFOP(_mm_or_si128);     return 1;
    case VPXOR:  HALFOP(_mm_xor_si128);    return 1;

    /* ---- compare ---- */
    case VPCMPEQB: HALFOP(_mm_cmpeq_epi8);  return 1;
    case VPCMPEQW: HALFOP(_mm_cmpeq_epi16); return 1;
    case VPCMPEQD: HALFOP(_mm_cmpeq_epi32); return 1;
    case VPCMPEQQ: HALFOP(_mm_cmpeq_epi64); return 1;
    case VPCMPGTB: HALFOP(_mm_cmpgt_epi8);  return 1;
    case VPCMPGTW: HALFOP(_mm_cmpgt_epi16); return 1;
    case VPCMPGTD: HALFOP(_mm_cmpgt_epi32); return 1;
    case VPCMPGTQ: HALFOP(_mm_cmpgt_epi64); return 1;

    /* ---- min / max ---- */
    case VPMINUB: HALFOP(_mm_min_epu8);  return 1;
    case VPMINUW: HALFOP(_mm_min_epu16); return 1;
    case VPMINUD: HALFOP(_mm_min_epu32); return 1;
    case VPMINSB: HALFOP(_mm_min_epi8);  return 1;
    case VPMINSW: HALFOP(_mm_min_epi16); return 1;
    case VPMINSD: HALFOP(_mm_min_epi32); return 1;
    case VPMAXUB: HALFOP(_mm_max_epu8);  return 1;
    case VPMAXUW: HALFOP(_mm_max_epu16); return 1;
    case VPMAXUD: HALFOP(_mm_max_epu32); return 1;
    case VPMAXSB: HALFOP(_mm_max_epi8);  return 1;
    case VPMAXSW: HALFOP(_mm_max_epi16); return 1;
    case VPMAXSD: HALFOP(_mm_max_epi32); return 1;

    /* ---- multiply / madd ---- */
    case VPMULLW:   HALFOP(_mm_mullo_epi16);  return 1;
    case VPMULLD:   HALFOP(_mm_mullo_epi32);  return 1;
    case VPMULHW:   HALFOP(_mm_mulhi_epi16);  return 1;
    case VPMULHUW:  HALFOP(_mm_mulhi_epu16);  return 1;
    case VPMULHRSW: HALFOP(_mm_mulhrs_epi16); return 1;
    case VPMULDQ:   HALFOP(_mm_mul_epi32);    return 1;
    case VPMULUDQ:  HALFOP(_mm_mul_epu32);    return 1;
    case VPMADDWD:  HALFOP(_mm_madd_epi16);   return 1;
    case VPMADDUBSW:HALFOP(_mm_maddubs_epi16);return 1;

    /* ---- avg / sad / abs / sign / hadd ---- */
    case VPAVGB:  HALFOP(_mm_avg_epu8);  return 1;
    case VPAVGW:  HALFOP(_mm_avg_epu16); return 1;
    case VPSADBW: HALFOP(_mm_sad_epu8);  return 1;
    case VPABSB:  HALFOP1(_mm_abs_epi8);  return 1;
    case VPABSW:  HALFOP1(_mm_abs_epi16); return 1;
    case VPABSD:  HALFOP1(_mm_abs_epi32); return 1;
    case VPSIGNB: HALFOP(_mm_sign_epi8);  return 1;
    case VPSIGNW: HALFOP(_mm_sign_epi16); return 1;
    case VPSIGND: HALFOP(_mm_sign_epi32); return 1;
    case VPHADDD: HALFOP(_mm_hadd_epi32); return 1;

    /* ---- scalar-count shifts (count = low 64 bits of b) ---- */
    case VPSLLW: SHIFT(_mm_sll_epi16); return 1;
    case VPSLLD: SHIFT(_mm_sll_epi32); return 1;
    case VPSLLQ: SHIFT(_mm_sll_epi64); return 1;
    case VPSRLW: SHIFT(_mm_srl_epi16); return 1;
    case VPSRLD: SHIFT(_mm_srl_epi32); return 1;
    case VPSRLQ: SHIFT(_mm_srl_epi64); return 1;
    case VPSRAW: SHIFT(_mm_sra_epi16); return 1;
    case VPSRAD: SHIFT(_mm_sra_epi32); return 1;

    /* ---- variable per-element shifts ---- */
    case VPSLLVD: case VPSRLVD: case VPSRAVD: {
        const uint32_t *av=(const uint32_t*)a->b, *bv=(const uint32_t*)b->b;
        uint32_t *ov=(uint32_t*)out->b;
        for (int i=0;i<8;i++){ uint32_t s=bv[i];
            if (op==VPSLLVD) ov[i] = s>31?0:(av[i]<<s);
            else if (op==VPSRLVD) ov[i] = s>31?0:(av[i]>>s);
            else { int32_t x=(int32_t)av[i]; ov[i]=(uint32_t)(x>>(s>31?31:s)); }
        } return 1;
    }
    case VPSLLVQ: case VPSRLVQ: {
        const uint64_t *av=(const uint64_t*)a->b, *bv=(const uint64_t*)b->b;
        uint64_t *ov=(uint64_t*)out->b;
        for (int i=0;i<4;i++){ uint64_t s=bv[i];
            ov[i] = s>63?0:(op==VPSLLVQ?(av[i]<<s):(av[i]>>s));
        } return 1;
    }

    /* ---- whole-lane byte shift by imm (per 128-bit lane) ---- */
    case VPSLLDQ: case VPSRLDQ: {
        int n = imm>15?16:imm;
        for (int l=0;l<2;l++){ const uint8_t *in=lane(a,l); uint8_t *o=out->b+16*l;
            for (int j=0;j<16;j++){
                if (op==VPSLLDQ) o[j] = (j>=n)? in[j-n] : 0;
                else             o[j] = (j+n<16)? in[j+n] : 0;
            }
        } return 1;
    }

    /* ---- in-lane shuffles ---- */
    case VPSHUFB: HALFOP(_mm_shuffle_epi8); return 1;
    case VPSHUFD: {
        const uint32_t *in; uint32_t *o;
        for (int l=0;l<2;l++){ in=(const uint32_t*)lane(b,l); o=(uint32_t*)(out->b+16*l);
            for (int i=0;i<4;i++) o[i]=in[(imm>>(2*i))&3];
        } return 1;
    }
    case VPSHUFLW: case VPSHUFHW: {
        for (int l=0;l<2;l++){ const uint16_t *in=(const uint16_t*)lane(b,l);
            uint16_t *o=(uint16_t*)(out->b+16*l);
            for (int i=0;i<8;i++) o[i]=in[i];           /* copy through */
            int base = (op==VPSHUFHW)?4:0;
            for (int i=0;i<4;i++) o[base+i]=in[base+((imm>>(2*i))&3)];
        } return 1;
    }

    /* ---- pack / unpack (per-lane) ---- */
    case VPACKSSWB: HALFOP(_mm_packs_epi16);  return 1;
    case VPACKSSDW: HALFOP(_mm_packs_epi32);  return 1;
    case VPACKUSWB: HALFOP(_mm_packus_epi16); return 1;
    case VPACKUSDW: HALFOP(_mm_packus_epi32); return 1;
    case VPUNPCKLBW: HALFOP(_mm_unpacklo_epi8);  return 1;
    case VPUNPCKHBW: HALFOP(_mm_unpackhi_epi8);  return 1;
    case VPUNPCKLWD: HALFOP(_mm_unpacklo_epi16); return 1;
    case VPUNPCKHWD: HALFOP(_mm_unpackhi_epi16); return 1;
    case VPUNPCKLDQ: HALFOP(_mm_unpacklo_epi32); return 1;
    case VPUNPCKHDQ: HALFOP(_mm_unpackhi_epi32); return 1;
    case VPUNPCKLQDQ:HALFOP(_mm_unpacklo_epi64); return 1;
    case VPUNPCKHQDQ:HALFOP(_mm_unpackhi_epi64); return 1;

    /* ---- align / blend / movemask ---- */
    case VPALIGNR: {
        for (int l=0;l<2;l++){ const uint8_t *al=lane(a,l),*bl=lane(b,l);
            uint8_t cc[32]; memcpy(cc,bl,16); memcpy(cc+16,al,16);
            uint8_t *o=out->b+16*l;
            for (int j=0;j<16;j++){ int k=j+imm; o[j]=(k<32)?cc[k]:0; }
        } return 1;
    }
    case VPBLENDW: {
        const uint16_t *av=(const uint16_t*)a->b,*bv=(const uint16_t*)b->b;
        uint16_t *ov=(uint16_t*)out->b;
        for (int i=0;i<16;i++) ov[i] = ((imm>>(i&7))&1)? bv[i] : av[i];
        return 1;
    }
    case VPBLENDD: {
        const uint32_t *av=(const uint32_t*)a->b,*bv=(const uint32_t*)b->b;
        uint32_t *ov=(uint32_t*)out->b;
        for (int i=0;i<8;i++) ov[i] = ((imm>>i)&1)? bv[i] : av[i];
        return 1;
    }
    case VPBLENDVB:
        put(out, _mm_blendv_epi8(lo(a),lo(b),lo(c)),
                 _mm_blendv_epi8(hi(a),hi(b),hi(c)));
        return 1;
    case VPMOVMSKB:
        if (gpr_out) *gpr_out = (uint32_t)((uint16_t)_mm_movemask_epi8(lo(b)))
                              | ((uint32_t)((uint16_t)_mm_movemask_epi8(hi(b)))<<16);
        return 1;

    /* ---- broadcast ---- */
    case VPBROADCASTB: { uint8_t v=b->b[0];  memset(out->b,v,32); return 1; }
    case VPBROADCASTW: { uint16_t v=((const uint16_t*)b->b)[0]; uint16_t*o=(uint16_t*)out->b; for(int i=0;i<16;i++)o[i]=v; return 1; }
    case VPBROADCASTD: { uint32_t v=((const uint32_t*)b->b)[0]; uint32_t*o=(uint32_t*)out->b; for(int i=0;i<8;i++)o[i]=v;  return 1; }
    case VPBROADCASTQ: { uint64_t v=((const uint64_t*)b->b)[0]; uint64_t*o=(uint64_t*)out->b; for(int i=0;i<4;i++)o[i]=v;  return 1; }
    case VBROADCASTI128: memcpy(out->b, b->b, 16); memcpy(out->b+16, b->b, 16); return 1;

    /* ---- zero / sign extend (src = low bytes of b) ---- */
    case VPMOVZXBW: case VPMOVSXBW: { const uint8_t*s=b->b; for(int i=0;i<16;i++){ int v=(op==VPMOVSXBW)?(int)(int8_t)s[i]:s[i]; ((uint16_t*)out->b)[i]=(uint16_t)v; } return 1; }
    case VPMOVZXBD: case VPMOVSXBD: { const uint8_t*s=b->b; for(int i=0;i<8;i++){ int v=(op==VPMOVSXBD)?(int)(int8_t)s[i]:s[i]; ((uint32_t*)out->b)[i]=(uint32_t)v; } return 1; }
    case VPMOVZXBQ: case VPMOVSXBQ: { const uint8_t*s=b->b; for(int i=0;i<4;i++){ int64_t v=(op==VPMOVSXBQ)?(int64_t)(int8_t)s[i]:(int64_t)s[i]; ((uint64_t*)out->b)[i]=(uint64_t)v; } return 1; }
    case VPMOVZXWD: case VPMOVSXWD: { const uint16_t*s=(const uint16_t*)b->b; for(int i=0;i<8;i++){ int v=(op==VPMOVSXWD)?(int)(int16_t)s[i]:s[i]; ((uint32_t*)out->b)[i]=(uint32_t)v; } return 1; }
    case VPMOVZXWQ: case VPMOVSXWQ: { const uint16_t*s=(const uint16_t*)b->b; for(int i=0;i<4;i++){ int64_t v=(op==VPMOVSXWQ)?(int64_t)(int16_t)s[i]:(int64_t)s[i]; ((uint64_t*)out->b)[i]=(uint64_t)v; } return 1; }
    case VPMOVZXDQ: case VPMOVSXDQ: { const uint32_t*s=(const uint32_t*)b->b; for(int i=0;i<4;i++){ int64_t v=(op==VPMOVSXDQ)?(int64_t)(int32_t)s[i]:(int64_t)s[i]; ((uint64_t*)out->b)[i]=(uint64_t)v; } return 1; }

    /* ---- lane extract / insert / permute ---- */
    case VEXTRACTI128:
        memcpy(out->b, (imm&1)? b->b+16 : b->b, 16);
        memset(out->b+16, 0, 16);             /* xmm dest zeroes upper */
        return 1;
    case VINSERTI128:
        memcpy(out->b, a->b, 32);
        memcpy(out->b + ((imm&1)?16:0), b->b, 16);
        return 1;
    case VPERM2I128: {
        for (int d=0; d<2; d++){
            int sel=(imm>>(d*4))&0xF; uint8_t *o=out->b+16*d;
            if (sel&0x8){ memset(o,0,16); continue; }
            const ymm256 *src=(sel&0x2)?b:a;
            memcpy(o, (sel&0x1)? src->b+16 : src->b, 16);
        } return 1;
    }
    case VPERMQ: case VPERMPD: {           /* qword permute of b by imm */
        const uint64_t *s=(const uint64_t*)b->b; uint64_t *o=(uint64_t*)out->b;
        for (int i=0;i<4;i++) o[i]=s[(imm>>(2*i))&3];
        return 1;
    }
    case VPERMD: case VPERMPS: {           /* dword permute: dst[i]=b[a[i]&7] */
        const uint32_t *idx=(const uint32_t*)a->b, *s=(const uint32_t*)b->b;
        uint32_t *o=(uint32_t*)out->b;
        for (int i=0;i<8;i++) o[i]=s[idx[i]&7];
        return 1;
    }

    /* ---- F16C: 8 halfs (low 128 of b) -> 8 floats ---- */
    case VCVTPH2PS: {
        const uint16_t *s=(const uint16_t*)b->b; float *o=(float*)out->b;
        for (int i=0;i<8;i++) o[i]=half_to_float(s[i]);
        return 1;
    }

    /* ---- FMA ---- */
    case VFMADD132: case VFMADD213: case VFMADD231:
    case VFMSUB132: case VFMSUB213: case VFMSUB231:
    case VFNMADD132:case VFNMADD213:case VFNMADD231:
    case VFNMSUB132:case VFNMSUB213:case VFNMSUB231:
        return fma_exec(op, type, a, b, c, out);

    default:
        return 0;   /* not a vector op (BMI handled elsewhere) or unimplemented */
    }
}
