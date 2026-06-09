#ifndef AVXEMU_SOFTFMA_H
#define AVXEMU_SOFTFMA_H

#include <stdint.h>
#include "vexops.h"

/* IEEE half -> float (exact; no rounding needed). For F16C VCVTPH2PS. */
float half_to_float(uint16_t h);

/*
 * FMA executor: out = fma-variant(a, b, c) per element.
 * `type` is FT_SS/FT_SD/FT_PS/FT_PD. For scalar types the upper lanes follow
 * Intel's rule (preserve c's upper bits, zero bits 255:128). Uses libm fma()/
 * fmaf(), which on this OS is correctly-rounded software (no hardware FMA),
 * so it runs on the target and matches AVX2 silicon bit-for-bit.
 */
int fma_exec(vex_op op, int type,
             const ymm256 *a, const ymm256 *b, const ymm256 *c, ymm256 *out);

#endif /* AVXEMU_SOFTFMA_H */
