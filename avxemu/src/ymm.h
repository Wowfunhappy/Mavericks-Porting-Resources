#ifndef AVXEMU_YMM_H
#define AVXEMU_YMM_H

#include <stdint.h>

/*
 * A 256-bit AVX register value, stored as raw bytes.
 *
 * Deliberately ABI-free: no __m128i/__m256i members. The emulator core is
 * compiled SSE-only (it must run on the *target* CPU, which lacks AVX2) while
 * the test oracle is compiled with -mavx2. Sharing a vector-typed struct
 * between those two translation units would couple their codegen/alignment
 * assumptions; raw bytes keep the boundary clean. Each side loads the halves
 * it needs with the appropriate (unaligned) intrinsic.
 */
typedef struct { uint8_t b[32]; } ymm256;

#endif /* AVXEMU_YMM_H */
