/*
 * tramptest.c — validate the trampoline thunk in isolation.
 *
 * Builds a thunk for a run of instructions, drives it through a full known
 * machine state (tramp_harness.s), and checks: (1) the destination register(s)
 * get the emulated result, and (2) EVERY other register, all of ymm, and rflags
 * are preserved bit-for-bit. This exercises the save/spill/dispatch/reload/jump
 * path that the eager patcher will rely on.
 */
#include "decode.h"
#include "regfile.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t ymm[16][32]; uint64_t gpr[16]; uint64_t rflags; } State;
typedef struct { uint64_t addr; decoded dec; } tramp_insn;

extern int   avxemu_pool_init(void *hint, size_t cap);
extern void *avxemu_build_thunk(const tramp_insn *insns, int n, uint64_t resume);
extern void  tramp_test_invoke(void *thunk, const State *in, State *out);
extern void  tramp_test_capture(void);
extern uint64_t ti_labels[];      /* [_ti0.._ti4] addresses of sample instructions */

static int g_fail;

/* fill a State with distinctive, non-trivial contents */
static void seed(State *s) {
    for (int r = 0; r < 16; r++)
        for (int b = 0; b < 32; b++) s->ymm[r][b] = (uint8_t)(r * 37 + b * 5 + 1);
    for (int i = 0; i < 16; i++) s->gpr[i] = 0x1111111100000000ull * (i + 1) + 0xABCD + i;
    /* IF + reserved + EVERY arithmetic flag (CF/PF/AF/ZF/SF/OF) set. A thunk that
     * disturbs flags before saving them (e.g. `sub` instead of `lea` for the frame)
     * computes its own CF/PF/... here and is caught — 0x202 could match by luck. */
    s->rflags = 0xAD7;
}

/* verify every reg/flag in `out` matches `in` except the listed exceptions */
static void check_preserved(const State *in, const State *out, const char *name,
                            int xgpr0, int xgpr1, int xymm) {
    for (int r = 0; r < 16; r++) {
        if (r == xymm) continue;
        if (memcmp(in->ymm[r], out->ymm[r], 32)) { printf("  [%s] ymm%d CLOBBERED\n", name, r); g_fail++; }
    }
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == xgpr0 || i == xgpr1) continue;   /* 4=rsp (harness-owned) */
        if (in->gpr[i] != out->gpr[i]) { printf("  [%s] gpr%d CLOBBERED %016llx->%016llx\n",
            name, i, (unsigned long long)in->gpr[i], (unsigned long long)out->gpr[i]); g_fail++; }
    }
    if (in->rflags != out->rflags) { printf("  [%s] rflags CHANGED %llx->%llx\n",
        name, (unsigned long long)in->rflags, (unsigned long long)out->rflags); g_fail++; }
}

static decoded dec_at(int idx) {
    decoded d; int n = decode((const uint8_t *)ti_labels[idx], &d);
    int asmlen = (int)(ti_labels[idx + 1] - ti_labels[idx]);
    if (n != asmlen) { printf("  decode len mismatch idx %d: %d vs %d\n", idx, n, asmlen); g_fail++; }
    return d;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!avxemu_pool_init(0, 1 << 20)) { printf("pool_init failed\n"); return 1; }
    uint64_t resume = (uint64_t)(void *)tramp_test_capture;
    printf("== trampoline thunk: emulate + full state preservation ==\n");

    /* (1) single vector op: ymm0 = ymm1 + ymm2 */
    {
        tramp_insn ti = { ti_labels[0], dec_at(0) };
        void *thunk = avxemu_build_thunk(&ti, 1, resume);
        State in, out; seed(&in); memset(&out, 0xEE, sizeof out);
        tramp_test_invoke(thunk, &in, &out);
        int bad = 0;
        for (int l = 0; l < 8; l++) {
            uint32_t a, b, o;
            memcpy(&a, in.ymm[1] + l*4, 4); memcpy(&b, in.ymm[2] + l*4, 4); memcpy(&o, out.ymm[0] + l*4, 4);
            if (o != (uint32_t)(a + b)) bad = 1;
        }
        printf("  vpaddd result: %s\n", bad ? "FAIL" : "ok"); g_fail += bad;
        check_preserved(&in, &out, "vpaddd", -1, -1, 0 /*ymm0 is dst*/);
    }

    /* (2) BMI gpr op: rax = r8 << rcx (shlx) */
    {
        tramp_insn ti = { ti_labels[2], dec_at(2) };
        void *thunk = avxemu_build_thunk(&ti, 1, resume);
        State in, out; seed(&in); in.gpr[8] = 0x00000000DEADBEEFull; in.gpr[1] = 12;
        memset(&out, 0xEE, sizeof out);
        tramp_test_invoke(thunk, &in, &out);
        uint64_t exp = in.gpr[8] << (in.gpr[1] & 63);
        printf("  shlx result:   %s\n", out.gpr[0] == exp ? "ok" : "FAIL"); g_fail += (out.gpr[0] != exp);
        check_preserved(&in, &out, "shlx", 0 /*rax dst*/, -1, -1);
    }

    /* (3) a RUN of two ops with different dests: ymm0=ymm1+ymm2 ; ymm5=ymm3^ymm4 */
    {
        tramp_insn ti[2] = { { ti_labels[0], dec_at(0) }, { ti_labels[1], dec_at(1) } };
        void *thunk = avxemu_build_thunk(ti, 2, resume);
        State in, out; seed(&in); memset(&out, 0xEE, sizeof out);
        tramp_test_invoke(thunk, &in, &out);
        int bad = 0;
        for (int l = 0; l < 8; l++) {
            uint32_t a,b,o; memcpy(&a,in.ymm[1]+l*4,4); memcpy(&b,in.ymm[2]+l*4,4); memcpy(&o,out.ymm[0]+l*4,4);
            if (o != (uint32_t)(a+b)) bad = 1;
        }
        for (int b32 = 0; b32 < 32; b32++) if (out.ymm[5][b32] != (uint8_t)(in.ymm[3][b32] ^ in.ymm[4][b32])) bad = 1;
        printf("  run[vpaddd,vpxor]: %s\n", bad ? "FAIL" : "ok"); g_fail += bad;
        /* both ymm0 and ymm5 are dests */
        for (int r = 0; r < 16; r++) {
            if (r == 0 || r == 5) continue;
            if (memcmp(in.ymm[r], out.ymm[r], 32)) { printf("  [run] ymm%d CLOBBERED\n", r); g_fail++; }
        }
        for (int i = 0; i < 16; i++) if (i != 4 && in.gpr[i] != out.gpr[i]) { printf("  [run] gpr%d CLOBBERED\n", i); g_fail++; }
        if (in.rflags != out.rflags) { printf("  [run] rflags CHANGED\n"); g_fail++; }
    }

    printf("\nTRAMPTEST TOTAL: %d failure(s)\n", g_fail);
    return g_fail ? 1 : 0;
}
