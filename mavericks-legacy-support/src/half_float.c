/*
 * IEEE 754 half-precision conversions.
 *
 * The compiler emits calls to these for __fp16 / _Float16 values. They live in
 * compiler-rt, which 10.9's libSystem predates. Both are exact: every half has
 * an exact float representation, and the narrowing direction rounds to nearest,
 * ties to even, as the hardware instruction does.
 */

#include <stdint.h>
#include <string.h>

static float bits_to_float(uint32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static uint32_t float_to_bits(float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return bits;
}

float __extendhfsf2(uint16_t half) {
    uint32_t sign = (uint32_t)(half & 0x8000u) << 16;
    uint32_t exp = (half >> 10) & 0x1Fu;
    uint32_t mant = half & 0x3FFu;

    if (exp == 0) {
        if (mant == 0) return bits_to_float(sign);          /* +/-0 */
        /* Subnormal half: normalise into a float exponent. */
        exp = 127 - 15 + 1;
        while (!(mant & 0x400u)) { mant <<= 1; exp--; }
        mant &= 0x3FFu;
        return bits_to_float(sign | (exp << 23) | (mant << 13));
    }
    if (exp == 0x1Fu)                                        /* inf / NaN */
        return bits_to_float(sign | 0x7F800000u | (mant << 13));
    return bits_to_float(sign | ((exp - 15 + 127) << 23) | (mant << 13));
}

uint16_t __truncsfhf2(float value) {
    uint32_t bits = float_to_bits(value);
    uint16_t sign = (uint16_t)((bits >> 16) & 0x8000u);
    int32_t exp = (int32_t)((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = bits & 0x7FFFFFu;

    if (((bits >> 23) & 0xFFu) == 0xFFu)                     /* inf / NaN */
        return (uint16_t)(sign | 0x7C00u | (mant ? (mant >> 13) | 1u : 0u));
    if (exp >= 0x1F) return (uint16_t)(sign | 0x7C00u);      /* overflow -> inf */
    if (exp <= 0) {
        if (exp < -10) return sign;                          /* underflow -> 0 */
        /* Subnormal half: shift the implicit 1 back in, then round. */
        mant |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half_mant = mant >> shift;
        uint32_t remainder = mant & ((1u << shift) - 1);
        uint32_t halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (half_mant & 1)))
            half_mant++;
        return (uint16_t)(sign | half_mant);
    }
    /* Round to nearest, ties to even. */
    uint16_t half_mant = (uint16_t)(mant >> 13);
    uint32_t remainder = mant & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (half_mant & 1))) {
        half_mant++;
        if (half_mant == 0x400u) { half_mant = 0; exp++; }
        if (exp >= 0x1F) return (uint16_t)(sign | 0x7C00u);
    }
    return (uint16_t)(sign | ((uint16_t)exp << 10) | half_mant);
}
