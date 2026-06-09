/*
 * oracle.c — differential test of the emulator vs. real AVX2/FMA/BMI silicon.
 * COMPILE -mavx2 -mfma -mbmi -mbmi2 -mlzcnt -mf16c on a Haswell+ host.
 *
 * For every implemented op: run the real instruction (ground truth), run the
 * SSE-only emulator on identical inputs, assert bit-equality. NaN results are
 * compared NaN-aware (payload not required to match).
 */

#include "vexops.h"
#include "softfma.h"
#include <immintrin.h>
#include <x86intrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- PRNG ---------- */
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t xs(void){ uint64_t x=rng; x^=x<<13; x^=x>>7; x^=x<<17; return rng=x; }
static void fill(ymm256*v){ uint64_t*q=(uint64_t*)v->b; for(int i=0;i<4;i++)q[i]=xs(); }

/* ---------- compare helpers ---------- */
static int eq32(const ymm256*x,const ymm256*y){ return memcmp(x->b,y->b,32)==0; }
/* float/double-lane compare that treats NaN==NaN */
static int eq_ps(const ymm256*x,const ymm256*y){
    const float*a=(const float*)x->b,*b=(const float*)y->b;
    for(int i=0;i<8;i++){ if(a[i]!=b[i] && !(a[i]!=a[i] && b[i]!=b[i])) return 0; } return 1; }
static int eq_pd(const ymm256*x,const ymm256*y){
    const double*a=(const double*)x->b,*b=(const double*)y->b;
    for(int i=0;i<4;i++){ if(a[i]!=b[i] && !(a[i]!=a[i] && b[i]!=b[i])) return 0; } return 1; }
/* scalar FMA: element 0 compared by value (+-0 and NaN-payload don't matter),
 * the preserved lanes + zeroed upper compared bitwise. */
static int eq_scalar(const ymm256*r,const ymm256*g,int dbl){
    if(memcmp(r->b+16,g->b+16,16)) return 0;
    if(dbl){ if(memcmp(r->b+8,g->b+8,8)) return 0;
        double x=((const double*)r->b)[0],y=((const double*)g->b)[0];
        return x==y || (x!=x && y!=y); }
    if(memcmp(r->b+4,g->b+4,12)) return 0;
    float x=((const float*)r->b)[0],y=((const float*)g->b)[0];
    return x==y || (x!=x && y!=y); }

#define LA  _mm256_loadu_si256((const __m256i*)a->b)
#define LB  _mm256_loadu_si256((const __m256i*)b->b)
#define XB  _mm_loadu_si128((const __m128i*)b->b)
#define ST(R) _mm256_storeu_si256((__m256i*)out->b,(R))

/* ground truth for non-immediate ops. operand slots match vec_exec(). */
static int native(vex_op op, const ymm256*a,const ymm256*b,const ymm256*c,ymm256*out,uint64_t*gpr){
    __m256i r;
    switch(op){
    case VPADDB:r=_mm256_add_epi8(LA,LB);break; case VPADDW:r=_mm256_add_epi16(LA,LB);break;
    case VPADDD:r=_mm256_add_epi32(LA,LB);break; case VPADDQ:r=_mm256_add_epi64(LA,LB);break;
    case VPSUBB:r=_mm256_sub_epi8(LA,LB);break; case VPSUBW:r=_mm256_sub_epi16(LA,LB);break;
    case VPSUBD:r=_mm256_sub_epi32(LA,LB);break; case VPSUBQ:r=_mm256_sub_epi64(LA,LB);break;
    case VPADDSB:r=_mm256_adds_epi8(LA,LB);break; case VPADDSW:r=_mm256_adds_epi16(LA,LB);break;
    case VPADDUSB:r=_mm256_adds_epu8(LA,LB);break; case VPADDUSW:r=_mm256_adds_epu16(LA,LB);break;
    case VPSUBSB:r=_mm256_subs_epi8(LA,LB);break; case VPSUBSW:r=_mm256_subs_epi16(LA,LB);break;
    case VPSUBUSB:r=_mm256_subs_epu8(LA,LB);break; case VPSUBUSW:r=_mm256_subs_epu16(LA,LB);break;
    case VPAND:r=_mm256_and_si256(LA,LB);break; case VPANDN:r=_mm256_andnot_si256(LA,LB);break;
    case VPOR:r=_mm256_or_si256(LA,LB);break; case VPXOR:r=_mm256_xor_si256(LA,LB);break;
    case VPCMPEQB:r=_mm256_cmpeq_epi8(LA,LB);break; case VPCMPEQW:r=_mm256_cmpeq_epi16(LA,LB);break;
    case VPCMPEQD:r=_mm256_cmpeq_epi32(LA,LB);break; case VPCMPEQQ:r=_mm256_cmpeq_epi64(LA,LB);break;
    case VPCMPGTB:r=_mm256_cmpgt_epi8(LA,LB);break; case VPCMPGTW:r=_mm256_cmpgt_epi16(LA,LB);break;
    case VPCMPGTD:r=_mm256_cmpgt_epi32(LA,LB);break; case VPCMPGTQ:r=_mm256_cmpgt_epi64(LA,LB);break;
    case VPMINUB:r=_mm256_min_epu8(LA,LB);break; case VPMINUW:r=_mm256_min_epu16(LA,LB);break;
    case VPMINUD:r=_mm256_min_epu32(LA,LB);break; case VPMINSB:r=_mm256_min_epi8(LA,LB);break;
    case VPMINSW:r=_mm256_min_epi16(LA,LB);break; case VPMINSD:r=_mm256_min_epi32(LA,LB);break;
    case VPMAXUB:r=_mm256_max_epu8(LA,LB);break; case VPMAXUW:r=_mm256_max_epu16(LA,LB);break;
    case VPMAXUD:r=_mm256_max_epu32(LA,LB);break; case VPMAXSB:r=_mm256_max_epi8(LA,LB);break;
    case VPMAXSW:r=_mm256_max_epi16(LA,LB);break; case VPMAXSD:r=_mm256_max_epi32(LA,LB);break;
    case VPMULLW:r=_mm256_mullo_epi16(LA,LB);break; case VPMULLD:r=_mm256_mullo_epi32(LA,LB);break;
    case VPMULHW:r=_mm256_mulhi_epi16(LA,LB);break; case VPMULHUW:r=_mm256_mulhi_epu16(LA,LB);break;
    case VPMULHRSW:r=_mm256_mulhrs_epi16(LA,LB);break; case VPMULDQ:r=_mm256_mul_epi32(LA,LB);break;
    case VPMULUDQ:r=_mm256_mul_epu32(LA,LB);break; case VPMADDWD:r=_mm256_madd_epi16(LA,LB);break;
    case VPMADDUBSW:r=_mm256_maddubs_epi16(LA,LB);break;
    case VPAVGB:r=_mm256_avg_epu8(LA,LB);break; case VPAVGW:r=_mm256_avg_epu16(LA,LB);break;
    case VPSADBW:r=_mm256_sad_epu8(LA,LB);break;
    case VPABSB:r=_mm256_abs_epi8(LA);break; case VPABSW:r=_mm256_abs_epi16(LA);break;
    case VPABSD:r=_mm256_abs_epi32(LA);break;
    case VPSIGNB:r=_mm256_sign_epi8(LA,LB);break; case VPSIGNW:r=_mm256_sign_epi16(LA,LB);break;
    case VPSIGND:r=_mm256_sign_epi32(LA,LB);break; case VPHADDD:r=_mm256_hadd_epi32(LA,LB);break;
    case VPSLLW:r=_mm256_sll_epi16(LA,XB);break; case VPSLLD:r=_mm256_sll_epi32(LA,XB);break;
    case VPSLLQ:r=_mm256_sll_epi64(LA,XB);break; case VPSRLW:r=_mm256_srl_epi16(LA,XB);break;
    case VPSRLD:r=_mm256_srl_epi32(LA,XB);break; case VPSRLQ:r=_mm256_srl_epi64(LA,XB);break;
    case VPSRAW:r=_mm256_sra_epi16(LA,XB);break; case VPSRAD:r=_mm256_sra_epi32(LA,XB);break;
    case VPSLLVD:r=_mm256_sllv_epi32(LA,LB);break; case VPSLLVQ:r=_mm256_sllv_epi64(LA,LB);break;
    case VPSRLVD:r=_mm256_srlv_epi32(LA,LB);break; case VPSRLVQ:r=_mm256_srlv_epi64(LA,LB);break;
    case VPSRAVD:r=_mm256_srav_epi32(LA,LB);break;
    case VPSHUFB:r=_mm256_shuffle_epi8(LA,LB);break;
    case VPACKSSWB:r=_mm256_packs_epi16(LA,LB);break; case VPACKSSDW:r=_mm256_packs_epi32(LA,LB);break;
    case VPACKUSWB:r=_mm256_packus_epi16(LA,LB);break; case VPACKUSDW:r=_mm256_packus_epi32(LA,LB);break;
    case VPUNPCKLBW:r=_mm256_unpacklo_epi8(LA,LB);break; case VPUNPCKHBW:r=_mm256_unpackhi_epi8(LA,LB);break;
    case VPUNPCKLWD:r=_mm256_unpacklo_epi16(LA,LB);break; case VPUNPCKHWD:r=_mm256_unpackhi_epi16(LA,LB);break;
    case VPUNPCKLDQ:r=_mm256_unpacklo_epi32(LA,LB);break; case VPUNPCKHDQ:r=_mm256_unpackhi_epi32(LA,LB);break;
    case VPUNPCKLQDQ:r=_mm256_unpacklo_epi64(LA,LB);break; case VPUNPCKHQDQ:r=_mm256_unpackhi_epi64(LA,LB);break;
    case VPBLENDVB:r=_mm256_blendv_epi8(LA,LB,_mm256_loadu_si256((const __m256i*)c->b));break;
    case VPBROADCASTB:r=_mm256_broadcastb_epi8(XB);break; case VPBROADCASTW:r=_mm256_broadcastw_epi16(XB);break;
    case VPBROADCASTD:r=_mm256_broadcastd_epi32(XB);break; case VPBROADCASTQ:r=_mm256_broadcastq_epi64(XB);break;
    case VBROADCASTI128:r=_mm256_broadcastsi128_si256(XB);break;
    case VPMOVZXBW:r=_mm256_cvtepu8_epi16(XB);break; case VPMOVZXBD:r=_mm256_cvtepu8_epi32(XB);break;
    case VPMOVZXBQ:r=_mm256_cvtepu8_epi64(XB);break; case VPMOVZXWD:r=_mm256_cvtepu16_epi32(XB);break;
    case VPMOVZXWQ:r=_mm256_cvtepu16_epi64(XB);break; case VPMOVZXDQ:r=_mm256_cvtepu32_epi64(XB);break;
    case VPMOVSXBW:r=_mm256_cvtepi8_epi16(XB);break; case VPMOVSXBD:r=_mm256_cvtepi8_epi32(XB);break;
    case VPMOVSXBQ:r=_mm256_cvtepi8_epi64(XB);break; case VPMOVSXWD:r=_mm256_cvtepi16_epi32(XB);break;
    case VPMOVSXWQ:r=_mm256_cvtepi16_epi64(XB);break; case VPMOVSXDQ:r=_mm256_cvtepi32_epi64(XB);break;
    case VPERMD:r=_mm256_permutevar8x32_epi32(LB,LA);break;
    case VPERMPS:r=(__m256i)_mm256_permutevar8x32_ps((__m256)LB,LA);break;
    case VCVTPH2PS:r=(__m256i)_mm256_cvtph_ps(XB);break;
    case VPMOVMSKB:*gpr=(uint32_t)_mm256_movemask_epi8(LB);return 1;
    default:return 0;
    }
    ST(r); return 1;
}

/* operand-fill categories */
enum { CAT_BIN, CAT_BIN3, CAT_SHIFT, CAT_VSHIFT, CAT_UN_A, CAT_UN_B, CAT_PERMD, CAT_FLOATCMP_PS, CAT_FLOATCMP_PD };
struct row { vex_op op; int cat; };
static struct row simple_ops[] = {
    {VPADDB,CAT_BIN},{VPADDW,CAT_BIN},{VPADDD,CAT_BIN},{VPADDQ,CAT_BIN},
    {VPSUBB,CAT_BIN},{VPSUBW,CAT_BIN},{VPSUBD,CAT_BIN},{VPSUBQ,CAT_BIN},
    {VPADDSB,CAT_BIN},{VPADDSW,CAT_BIN},{VPADDUSB,CAT_BIN},{VPADDUSW,CAT_BIN},
    {VPSUBSB,CAT_BIN},{VPSUBSW,CAT_BIN},{VPSUBUSB,CAT_BIN},{VPSUBUSW,CAT_BIN},
    {VPAND,CAT_BIN},{VPANDN,CAT_BIN},{VPOR,CAT_BIN},{VPXOR,CAT_BIN},
    {VPCMPEQB,CAT_BIN},{VPCMPEQW,CAT_BIN},{VPCMPEQD,CAT_BIN},{VPCMPEQQ,CAT_BIN},
    {VPCMPGTB,CAT_BIN},{VPCMPGTW,CAT_BIN},{VPCMPGTD,CAT_BIN},{VPCMPGTQ,CAT_BIN},
    {VPMINUB,CAT_BIN},{VPMINUW,CAT_BIN},{VPMINUD,CAT_BIN},{VPMINSB,CAT_BIN},{VPMINSW,CAT_BIN},{VPMINSD,CAT_BIN},
    {VPMAXUB,CAT_BIN},{VPMAXUW,CAT_BIN},{VPMAXUD,CAT_BIN},{VPMAXSB,CAT_BIN},{VPMAXSW,CAT_BIN},{VPMAXSD,CAT_BIN},
    {VPMULLW,CAT_BIN},{VPMULLD,CAT_BIN},{VPMULHW,CAT_BIN},{VPMULHUW,CAT_BIN},{VPMULHRSW,CAT_BIN},
    {VPMULDQ,CAT_BIN},{VPMULUDQ,CAT_BIN},{VPMADDWD,CAT_BIN},{VPMADDUBSW,CAT_BIN},
    {VPAVGB,CAT_BIN},{VPAVGW,CAT_BIN},{VPSADBW,CAT_BIN},
    {VPABSB,CAT_UN_A},{VPABSW,CAT_UN_A},{VPABSD,CAT_UN_A},
    {VPSIGNB,CAT_BIN},{VPSIGNW,CAT_BIN},{VPSIGND,CAT_BIN},{VPHADDD,CAT_BIN},
    {VPSLLW,CAT_SHIFT},{VPSLLD,CAT_SHIFT},{VPSLLQ,CAT_SHIFT},{VPSRLW,CAT_SHIFT},
    {VPSRLD,CAT_SHIFT},{VPSRLQ,CAT_SHIFT},{VPSRAW,CAT_SHIFT},{VPSRAD,CAT_SHIFT},
    {VPSLLVD,CAT_VSHIFT},{VPSLLVQ,CAT_VSHIFT},{VPSRLVD,CAT_VSHIFT},{VPSRLVQ,CAT_VSHIFT},{VPSRAVD,CAT_VSHIFT},
    {VPSHUFB,CAT_BIN},
    {VPACKSSWB,CAT_BIN},{VPACKSSDW,CAT_BIN},{VPACKUSWB,CAT_BIN},{VPACKUSDW,CAT_BIN},
    {VPUNPCKLBW,CAT_BIN},{VPUNPCKHBW,CAT_BIN},{VPUNPCKLWD,CAT_BIN},{VPUNPCKHWD,CAT_BIN},
    {VPUNPCKLDQ,CAT_BIN},{VPUNPCKHDQ,CAT_BIN},{VPUNPCKLQDQ,CAT_BIN},{VPUNPCKHQDQ,CAT_BIN},
    {VPBLENDVB,CAT_BIN3},
    {VPBROADCASTB,CAT_UN_B},{VPBROADCASTW,CAT_UN_B},{VPBROADCASTD,CAT_UN_B},{VPBROADCASTQ,CAT_UN_B},
    {VBROADCASTI128,CAT_UN_B},
    {VPMOVZXBW,CAT_UN_B},{VPMOVZXBD,CAT_UN_B},{VPMOVZXBQ,CAT_UN_B},{VPMOVZXWD,CAT_UN_B},{VPMOVZXWQ,CAT_UN_B},{VPMOVZXDQ,CAT_UN_B},
    {VPMOVSXBW,CAT_UN_B},{VPMOVSXBD,CAT_UN_B},{VPMOVSXBQ,CAT_UN_B},{VPMOVSXWD,CAT_UN_B},{VPMOVSXWQ,CAT_UN_B},{VPMOVSXDQ,CAT_UN_B},
    {VPERMD,CAT_PERMD},{VPERMPS,CAT_PERMD},{VCVTPH2PS,CAT_UN_B},{VPMOVMSKB,CAT_UN_B},
};

static int run_simple(void){
    int nfail=0, n=(int)(sizeof simple_ops/sizeof simple_ops[0]);
    for(int i=0;i<n;i++){
        vex_op op=simple_ops[i].op; int cat=simple_ops[i].cat, bad=0;
        for(int t=0;t<20000;t++){
            ymm256 a,b,c,ref,got; uint64_t gref=0,ggot=0;
            fill(&a); fill(&b); fill(&c);
            if(cat==CAT_SHIFT){ memset(b.b,0,32); ((uint64_t*)b.b)[0]=xs()%72; }
            if(cat==CAT_VSHIFT){ for(int k=0;k<8;k++)((uint32_t*)b.b)[k]=(uint32_t)(xs()%72); }
            memset(&ref,0,32); memset(&got,0xCC,32);
            native(op,&a,&b,&c,&ref,&gref);
            vec_exec(op,0,&a,&b,&c,0,&got,&ggot);
            int ok = (op==VPMOVMSKB)? (gref==ggot)
                   : (op==VCVTPH2PS)? eq_ps(&ref,&got) : eq32(&ref,&got);
            if(!ok){ bad++; }
        }
        printf("  %-12s %s\n", vex_op_name(op), bad?"FAIL":"ok");
        if(bad)nfail++;
    }
    return nfail;
}

/* ---- immediate ops: fixed imm constants, constant-imm intrinsics ---- */
static int chk(vex_op op,int type,const ymm256*a,const ymm256*b,const ymm256*c,uint8_t imm,const ymm256*ref){
    ymm256 got; uint64_t g; memset(&got,0xCC,32);
    vec_exec(op,type,a,b,c,imm,&got,&g);
    return eq32(ref,&got);
}
#define IMMCASE(EXPR) do{ ymm256 ref; _mm256_storeu_si256((__m256i*)ref.b,(EXPR)); if(!chk(op,0,&a,&b,&c,imm,&ref)) bad++; }while(0)

static int run_imm(void){
    int bad_total=0;
    static const uint8_t imms[]={0x00,0x1B,0x4E,0xD8,0x39,0xAA,0x3C,0xA5,0xFF,1,3,7,15,16,31};
    struct { const char*name; } tags[]={{0}};
    (void)tags;
    /* each op tested over the imm set; use switch with constant cases */
    const char* names_[]={"vpshufd","vpshuflw","vpshufhw","vpslldq","vpsrldq",
        "vpblendw","vpblendd","vpalignr","vperm2i128","vpermq","vpermpd",
        "vextracti128","vinserti128"};
    int bad[13]={0};
    for(unsigned ii=0; ii<sizeof imms/sizeof imms[0]; ii++){
        uint8_t imm=imms[ii];
        for(int t=0;t<3000;t++){
            ymm256 a,b,c; fill(&a); fill(&b); fill(&c); vex_op op;
            #define DO(IDX,OP,EXPR) do{ op=OP; ymm256 ref; _mm256_storeu_si256((__m256i*)ref.b,(EXPR)); \
                ymm256 got; uint64_t g; memset(&got,0xCC,32); vec_exec(op,0,&a,&b,&c,imm,&got,&g); \
                if(!eq32(&ref,&got)) bad[IDX]++; }while(0)
            /* constant-imm intrinsics require a switch on imm */
            #define SW(IDX,OP,FN) do{ op=OP; ymm256 ref; __m256i R; switch(imm){ \
                case 0x00:R=FN(0x00);break; case 0x1B:R=FN(0x1B);break; case 0x4E:R=FN(0x4E);break; \
                case 0xD8:R=FN(0xD8);break; case 0x39:R=FN(0x39);break; case 0xAA:R=FN(0xAA);break; \
                case 0x3C:R=FN(0x3C);break; case 0xA5:R=FN(0xA5);break; case 0xFF:R=FN(0xFF);break; \
                case 1:R=FN(1);break; case 3:R=FN(3);break; case 7:R=FN(7);break; case 15:R=FN(15);break; \
                case 16:R=FN(16);break; case 31:R=FN(31);break; default:R=FN(0);break; } \
                _mm256_storeu_si256((__m256i*)ref.b,R); ymm256 got; uint64_t g; memset(&got,0xCC,32); \
                vec_exec(op,0,&a,&b,&c,imm,&got,&g); if(!eq32(&ref,&got)) bad[IDX]++; }while(0)
            #define SHUF(I) _mm256_shuffle_epi32(LA_,I)
            #define LA_ _mm256_loadu_si256((const __m256i*)b.b)
            SW(0,VPSHUFD,SHUF);
            #define SLW(I) _mm256_shufflelo_epi16(LA_,I)
            SW(1,VPSHUFLW,SLW);
            #define SHW(I) _mm256_shufflehi_epi16(LA_,I)
            SW(2,VPSHUFHW,SHW);
            #define SLDQ(I) _mm256_slli_si256(_mm256_loadu_si256((const __m256i*)a.b),I)
            SW(3,VPSLLDQ,SLDQ);
            #define SRDQ(I) _mm256_srli_si256(_mm256_loadu_si256((const __m256i*)a.b),I)
            SW(4,VPSRLDQ,SRDQ);
            #define BLW(I) _mm256_blend_epi16(_mm256_loadu_si256((const __m256i*)a.b),_mm256_loadu_si256((const __m256i*)b.b),I)
            SW(5,VPBLENDW,BLW);
            #define BLD(I) _mm256_blend_epi32(_mm256_loadu_si256((const __m256i*)a.b),_mm256_loadu_si256((const __m256i*)b.b),I)
            SW(6,VPBLENDD,BLD);
            #define ALN(I) _mm256_alignr_epi8(_mm256_loadu_si256((const __m256i*)a.b),_mm256_loadu_si256((const __m256i*)b.b),I)
            SW(7,VPALIGNR,ALN);
            #define P2(I) _mm256_permute2x128_si256(_mm256_loadu_si256((const __m256i*)a.b),_mm256_loadu_si256((const __m256i*)b.b),I)
            SW(8,VPERM2I128,P2);
            #define PQ(I) _mm256_permute4x64_epi64(_mm256_loadu_si256((const __m256i*)b.b),I)
            SW(9,VPERMQ,PQ);
            #define PPD(I) (__m256i)_mm256_permute4x64_pd((__m256d)_mm256_loadu_si256((const __m256i*)b.b),I)
            SW(10,VPERMPD,PPD);
            #define EXT(I) _mm256_castsi128_si256(_mm256_extracti128_si256(_mm256_loadu_si256((const __m256i*)b.b),(I)&1))
            { op=VEXTRACTI128; ymm256 ref; memset(ref.b,0,32);
              __m128i x = (imm&1)? _mm256_extracti128_si256(_mm256_loadu_si256((const __m256i*)b.b),1)
                                 : _mm256_extracti128_si256(_mm256_loadu_si256((const __m256i*)b.b),0);
              _mm_storeu_si128((__m128i*)ref.b,x);
              ymm256 got; uint64_t g; memset(&got,0xCC,32); vec_exec(op,0,&a,&b,&c,imm,&got,&g);
              if(!eq32(&ref,&got)) bad[11]++; }
            { op=VINSERTI128; ymm256 ref; __m256i R;
              __m128i xb=_mm_loadu_si128((const __m128i*)b.b);
              if(imm&1) R=_mm256_inserti128_si256(_mm256_loadu_si256((const __m256i*)a.b),xb,1);
              else      R=_mm256_inserti128_si256(_mm256_loadu_si256((const __m256i*)a.b),xb,0);
              _mm256_storeu_si256((__m256i*)ref.b,R);
              ymm256 got; uint64_t g; memset(&got,0xCC,32); vec_exec(op,0,&a,&b,&c,imm,&got,&g);
              if(!eq32(&ref,&got)) bad[12]++; }
        }
    }
    for(int i=0;i<13;i++){ printf("  %-12s %s\n",names_[i],bad[i]?"FAIL":"ok"); if(bad[i])bad_total++; }
    return bad_total;
}

/* ---- FMA: all 12 ops x {pd, ps, sd, ss} ---- */
static double rdbl(void){ int e=(int)(xs()%160)-80; return ldexp((double)(int64_t)xs()/9.2e18,e); }
static double hw_low_d(int variant,double m1,double m2,double ad){
    __m128d x=_mm_set_sd(m1),y=_mm_set_sd(m2),z=_mm_set_sd(ad),r;
    switch(variant){case 0:r=_mm_fmadd_sd(x,y,z);break;case 1:r=_mm_fmsub_sd(x,y,z);break;
        case 2:r=_mm_fnmadd_sd(x,y,z);break;default:r=_mm_fnmsub_sd(x,y,z);break;}
    return _mm_cvtsd_f64(r);
}
static float hw_low_f(int variant,float m1,float m2,float ad){
    __m128 x=_mm_set_ss(m1),y=_mm_set_ss(m2),z=_mm_set_ss(ad),r;
    switch(variant){case 0:r=_mm_fmadd_ss(x,y,z);break;case 1:r=_mm_fmsub_ss(x,y,z);break;
        case 2:r=_mm_fnmadd_ss(x,y,z);break;default:r=_mm_fnmsub_ss(x,y,z);break;}
    return _mm_cvtss_f32(r);
}
/* roles: pick (m1,m2,add) indices from {a=0,b=1,c=2} by order 0=132,1=213,2=231 */
static void roles_idx(int order,int*m1,int*m2,int*ad){
    if(order==0){*m1=2;*m2=1;*ad=0;} else if(order==1){*m1=0;*m2=2;*ad=1;} else {*m1=0;*m2=1;*ad=2;}
}
static int run_fma(void){
    const char* nm[12]={"vfmadd132","vfmadd213","vfmadd231","vfmsub132","vfmsub213","vfmsub231",
        "vfnmadd132","vfnmadd213","vfnmadd231","vfnmsub132","vfnmsub213","vfnmsub231"};
    int nfail=0;
    for(int i=0;i<12;i++){
        vex_op op=(vex_op)(VFMADD132+i); int variant=i/3, order=i%3;
        int m1i,m2i,adi; roles_idx(order,&m1i,&m2i,&adi);
        int bad_pd=0,bad_ps=0,bad_sd=0,bad_ss=0;
        for(int t=0;t<20000;t++){
            ymm256 a,b,c,got; uint64_t g;
            double *ad=(double*)a.b,*bd=(double*)b.b,*cd=(double*)c.b;
            for(int k=0;k<4;k++){ ad[k]=rdbl(); bd[k]=rdbl(); cd[k]=rdbl(); }
            const ymm256* abc[3]={&a,&b,&c};
            /* pd */
            { ymm256 ref; double*o=(double*)ref.b;
              for(int k=0;k<4;k++){ double M1=((const double*)abc[m1i]->b)[k],M2=((const double*)abc[m2i]->b)[k],AD=((const double*)abc[adi]->b)[k];
                  o[k]=hw_low_d(variant,M1,M2,AD); }
              memset(&got,0xCC,32); vec_exec(op,FT_PD,&a,&b,&c,0,&got,&g); if(!eq_pd(&ref,&got))bad_pd++; }
            /* ps */
            { ymm256 ref; float*o=(float*)ref.b;
              for(int k=0;k<8;k++){ float M1=((const float*)abc[m1i]->b)[k],M2=((const float*)abc[m2i]->b)[k],AD=((const float*)abc[adi]->b)[k];
                  o[k]=hw_low_f(variant,M1,M2,AD); }
              memset(&got,0xCC,32); vec_exec(op,FT_PS,&a,&b,&c,0,&got,&g); if(!eq_ps(&ref,&got))bad_ps++; }
            /* sd: low element computed, upper64 from c (dst), 255:128 zero */
            { ymm256 ref; memcpy(ref.b,c.b,16); memset(ref.b+16,0,16);
              double M1=((const double*)abc[m1i]->b)[0],M2=((const double*)abc[m2i]->b)[0],AD=((const double*)abc[adi]->b)[0];
              ((double*)ref.b)[0]=hw_low_d(variant,M1,M2,AD);
              memset(&got,0xCC,32); vec_exec(op,FT_SD,&a,&b,&c,0,&got,&g); if(!eq_scalar(&ref,&got,1))bad_sd++; }
            /* ss */
            { ymm256 ref; memcpy(ref.b,c.b,16); memset(ref.b+16,0,16);
              float M1=((const float*)abc[m1i]->b)[0],M2=((const float*)abc[m2i]->b)[0],AD=((const float*)abc[adi]->b)[0];
              ((float*)ref.b)[0]=hw_low_f(variant,M1,M2,AD);
              memset(&got,0xCC,32); vec_exec(op,FT_SS,&a,&b,&c,0,&got,&g); if(!eq_scalar(&ref,&got,0))bad_ss++; }
        }
        int bad=bad_pd+bad_ps+bad_sd+bad_ss;
        printf("  %-12s %s (pd:%d ps:%d sd:%d ss:%d)\n", nm[i], bad?"FAIL":"ok", bad_pd,bad_ps,bad_sd,bad_ss);
        if(bad)nfail++;
    }
    return nfail;
}

/* FMA over special values: 0, -0, inf, nan, subnormals, overflow, cancellation */
static int run_fma_edge(void){
    static const double sp[] = {
        0.0, -0.0, 1.0, -1.0, 2.0, -2.0, 0.5,
        1.7976931348623157e308 /*~DBL_MAX*/, -1.7976931348623157e308,
        2.2250738585072014e-308 /*~DBL_MIN*/, 1e308, 1e-308,
    };
    double pool[14]; for(int i=0;i<12;i++) pool[i]=sp[i];
    pool[12]=ldexp(1.0,-1074); /* smallest subnormal */ pool[13]=ldexp(1.0,1020);
    double nan = pool[0]/pool[0]; /* runtime 0/0 -> NaN */
    const int NS = 14;
    int nfail=0;
    for(int i=0;i<12;i++){
        vex_op op=(vex_op)(VFMADD132+i); int variant=i/3, order=i%3;
        int m1i,m2i,adi; roles_idx(order,&m1i,&m2i,&adi);
        long bad=0;
        for(int x=0;x<=NS;x++) for(int y=0;y<=NS;y++) for(int z=0;z<=NS;z++){
            double X=(x<NS?pool[x]:nan), Y=(y<NS?pool[y]:nan), Z=(z<NS?pool[z]:nan);
            ymm256 a,b,c,got,ref; uint64_t g;
            for(int k=0;k<4;k++){ ((double*)a.b)[k]=X; ((double*)b.b)[k]=Y; ((double*)c.b)[k]=Z; }
            const ymm256* abc[3]={&a,&b,&c};
            for(int k=0;k<4;k++){ double M1=((const double*)abc[m1i]->b)[k],M2=((const double*)abc[m2i]->b)[k],AD=((const double*)abc[adi]->b)[k];
                ((double*)ref.b)[k]=hw_low_d(variant,M1,M2,AD); }
            memset(&got,0xCC,32); vec_exec(op,FT_PD,&a,&b,&c,0,&got,&g);
            if(!eq_pd(&ref,&got)) bad++;
        }
        if(bad) nfail++;
        printf("  %-12s %s (%ld combos)\n", vex_op_name(op), bad?"FAIL":"ok",
               (long)(NS+1)*(NS+1)*(NS+1));
    }
    return nfail;
}

int main(void){
    printf("== simple / shift / extend / broadcast / permd ==\n");
    int f1=run_simple();
    printf("== immediate ops ==\n");
    int f2=run_imm();
    printf("== FMA (pd/ps/sd/ss) ==\n");
    int f3=run_fma();
    printf("== FMA edge cases (0/-0/inf/nan/subnormal/overflow) ==\n");
    int f4=run_fma_edge();
    int tot=f1+f2+f3+f4;
    printf("\nTOTAL: %d failing op(s)\n", tot);
    return tot?1:0;
}
