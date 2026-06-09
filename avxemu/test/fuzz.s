// Harness for the native-vs-emulated fuzzer.
//
// _fuzz(state*): load ymm0..15 from state[0..512), execute the patched slot,
// store ymm0..15 to state[512..1024). State pointer kept in r15 (vector ops
// never touch GPRs), so the prologue/epilogue are position-independent and the
// whole template can be copied into an RWX buffer with an instruction patched
// into the slot. The slot is empty here; the fuzzer fills it at runtime.

.text
.globl _fuzz_start
_fuzz_start:
    pushq %r15
    movq  %rdi, %r15
    vmovdqu   0(%r15), %ymm0
    vmovdqu  32(%r15), %ymm1
    vmovdqu  64(%r15), %ymm2
    vmovdqu  96(%r15), %ymm3
    vmovdqu 128(%r15), %ymm4
    vmovdqu 160(%r15), %ymm5
    vmovdqu 192(%r15), %ymm6
    vmovdqu 224(%r15), %ymm7
    vmovdqu 256(%r15), %ymm8
    vmovdqu 288(%r15), %ymm9
    vmovdqu 320(%r15), %ymm10
    vmovdqu 352(%r15), %ymm11
    vmovdqu 384(%r15), %ymm12
    vmovdqu 416(%r15), %ymm13
    vmovdqu 448(%r15), %ymm14
    vmovdqu 480(%r15), %ymm15
.globl _fuzz_slot
_fuzz_slot:
    // runtime patches [ud2?]+instruction+nop padding here (fixed-size slot)
.globl _fuzz_epilogue
_fuzz_epilogue:
    vmovdqu %ymm0,  512(%r15)
    vmovdqu %ymm1,  544(%r15)
    vmovdqu %ymm2,  576(%r15)
    vmovdqu %ymm3,  608(%r15)
    vmovdqu %ymm4,  640(%r15)
    vmovdqu %ymm5,  672(%r15)
    vmovdqu %ymm6,  704(%r15)
    vmovdqu %ymm7,  736(%r15)
    vmovdqu %ymm8,  768(%r15)
    vmovdqu %ymm9,  800(%r15)
    vmovdqu %ymm10, 832(%r15)
    vmovdqu %ymm11, 864(%r15)
    vmovdqu %ymm12, 896(%r15)
    vmovdqu %ymm13, 928(%r15)
    vmovdqu %ymm14, 960(%r15)
    vmovdqu %ymm15, 992(%r15)
    popq %r15
    vzeroupper
    retq
.globl _fuzz_end
_fuzz_end:
