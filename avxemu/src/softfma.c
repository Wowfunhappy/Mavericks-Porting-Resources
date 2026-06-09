/*
 * softfma.c — software FMA + F16C, SSE-only (compile -mno-avx -mno-fma).
 *
 * fma()/fmaf() resolve to libm's software fused-multiply-add (verified to be a
 * libcall, not a hardware vfmadd, and bit-exact vs AVX2 silicon), so this is
 * safe on the target CPU and correctly rounded.
 */

#include "softfma.h"
#include <math.h>
#include <string.h>

float half_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t man  = h & 0x3FF;
    uint32_t f;
    if (exp == 0) {
        if (man == 0) {
            f = sign;                                /* +-0 */
        } else {
            exp = 1;                                 /* subnormal half -> normal float */
            while (!(man & 0x400)) { man <<= 1; exp--; }
            man &= 0x3FF;
            f = sign | ((exp + (127 - 15)) << 23) | (man << 13);
        }
    } else if (exp == 0x1F) {
        f = sign | 0x7F800000u | (man << 13);        /* inf / nan */
    } else {
        f = sign | ((exp + (127 - 15)) << 23) | (man << 13);
    }
    float out;
    memcpy(&out, &f, 4);
    return out;
}

/* order: 0=132, 1=213, 2=231.  Returns the three roles (m1,m2,addend) for a
 * given element, selected from a(src1), b(src2), c(old dst). */
static void roles(int order, double a, double b, double c,
                  double *m1, double *m2, double *ad) {
    switch (order) {
    case 0: *m1 = c; *m2 = b; *ad = a; break; /* 132: dst*src2 + src1 */
    case 1: *m1 = a; *m2 = c; *ad = b; break; /* 213: src1*dst + src2 */
    default:*m1 = a; *m2 = b; *ad = c; break; /* 231: src1*src2 + dst */
    }
}
static void rolesf(int order, float a, float b, float c,
                   float *m1, float *m2, float *ad) {
    switch (order) {
    case 0: *m1 = c; *m2 = b; *ad = a; break;
    case 1: *m1 = a; *m2 = c; *ad = b; break;
    default:*m1 = a; *m2 = b; *ad = c; break;
    }
}

int fma_exec(vex_op op, int type,
             const ymm256 *a, const ymm256 *b, const ymm256 *c, ymm256 *out) {
    int order, negp, negc;
    switch (op) {
    case VFMADD132: order=0; negp=0; negc=0; break;
    case VFMADD213: order=1; negp=0; negc=0; break;
    case VFMADD231: order=2; negp=0; negc=0; break;
    case VFMSUB132: order=0; negp=0; negc=1; break;
    case VFMSUB213: order=1; negp=0; negc=1; break;
    case VFMSUB231: order=2; negp=0; negc=1; break;
    case VFNMADD132:order=0; negp=1; negc=0; break;
    case VFNMADD213:order=1; negp=1; negc=0; break;
    case VFNMADD231:order=2; negp=1; negc=0; break;
    case VFNMSUB132:order=0; negp=1; negc=1; break;
    case VFNMSUB213:order=1; negp=1; negc=1; break;
    case VFNMSUB231:order=2; negp=1; negc=1; break;
    default: return 0;
    }

    if (type == FT_SD || type == FT_SS) {
        memcpy(out->b, c->b, 16);          /* preserve dst upper bits */
        memset(out->b + 16, 0, 16);        /* zero 255:128 */
        if (type == FT_SD) {
            double av=((const double*)a->b)[0], bv=((const double*)b->b)[0], cv=((const double*)c->b)[0];
            double m1,m2,ad; roles(order, av,bv,cv, &m1,&m2,&ad);
            double r = fma(negp?-m1:m1, m2, negc?-ad:ad);
            ((double*)out->b)[0] = r;
        } else {
            float av=((const float*)a->b)[0], bv=((const float*)b->b)[0], cv=((const float*)c->b)[0];
            float m1,m2,ad; rolesf(order, av,bv,cv, &m1,&m2,&ad);
            float r = fmaf(negp?-m1:m1, m2, negc?-ad:ad);
            ((float*)out->b)[0] = r;
        }
        return 1;
    }

    if (type == FT_PD) {
        const double *av=(const double*)a->b,*bv=(const double*)b->b,*cv=(const double*)c->b;
        double *o=(double*)out->b;
        for (int i=0;i<4;i++){ double m1,m2,ad; roles(order,av[i],bv[i],cv[i],&m1,&m2,&ad);
            o[i]=fma(negp?-m1:m1,m2,negc?-ad:ad); }
        return 1;
    }
    /* FT_PS */
    {
        const float *av=(const float*)a->b,*bv=(const float*)b->b,*cv=(const float*)c->b;
        float *o=(float*)out->b;
        for (int i=0;i<8;i++){ float m1,m2,ad; rolesf(order,av[i],bv[i],cv[i],&m1,&m2,&ad);
            o[i]=fmaf(negp?-m1:m1,m2,negc?-ad:ad); }
        return 1;
    }
}
