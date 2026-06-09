// tramp_harness.s — drive a thunk with a full, known machine state and capture
// the state it produces. State layout matches the test's struct:
//   [0..511] ymm[16][32]   [512..639] gpr[16]   [640] rflags
// rsp (gpr[4]) is intentionally left as the harness's own; the thunk preserves
// it, so it is not loaded from `in` nor meaningfully captured.

.data
.align 8
g_out:     .quad 0
g_thunk:   .quad 0
g_scratch: .quad 0

.text
.align 4
.globl _tramp_test_invoke
_tramp_test_invoke:                 // rdi=thunk, rsi=in, rdx=out
    pushq %rbx; pushq %rbp; pushq %r12; pushq %r13; pushq %r14; pushq %r15
    movq  %rdx, g_out(%rip)
    movq  %rdi, g_thunk(%rip)

    vmovdqu 0*32(%rsi),  %ymm0
    vmovdqu 1*32(%rsi),  %ymm1
    vmovdqu 2*32(%rsi),  %ymm2
    vmovdqu 3*32(%rsi),  %ymm3
    vmovdqu 4*32(%rsi),  %ymm4
    vmovdqu 5*32(%rsi),  %ymm5
    vmovdqu 6*32(%rsi),  %ymm6
    vmovdqu 7*32(%rsi),  %ymm7
    vmovdqu 8*32(%rsi),  %ymm8
    vmovdqu 9*32(%rsi),  %ymm9
    vmovdqu 10*32(%rsi), %ymm10
    vmovdqu 11*32(%rsi), %ymm11
    vmovdqu 12*32(%rsi), %ymm12
    vmovdqu 13*32(%rsi), %ymm13
    vmovdqu 14*32(%rsi), %ymm14
    vmovdqu 15*32(%rsi), %ymm15

    pushq 640(%rsi)
    popfq

    movq 512+0*8(%rsi),  %rax
    movq 512+1*8(%rsi),  %rcx
    movq 512+2*8(%rsi),  %rdx
    movq 512+3*8(%rsi),  %rbx
    movq 512+5*8(%rsi),  %rbp
    movq 512+7*8(%rsi),  %rdi
    movq 512+8*8(%rsi),  %r8
    movq 512+9*8(%rsi),  %r9
    movq 512+10*8(%rsi), %r10
    movq 512+11*8(%rsi), %r11
    movq 512+12*8(%rsi), %r12
    movq 512+13*8(%rsi), %r13
    movq 512+14*8(%rsi), %r14
    movq 512+15*8(%rsi), %r15
    movq 512+6*8(%rsi),  %rsi        // rsi last
    jmp  *g_thunk(%rip)

.globl _tramp_test_capture
_tramp_test_capture:                // thunk resumes here with the post-run state
    movq %rax, g_scratch(%rip)
    movq g_out(%rip), %rax
    movq %rcx, 512+1*8(%rax)
    movq %rdx, 512+2*8(%rax)
    movq %rbx, 512+3*8(%rax)
    movq %rbp, 512+5*8(%rax)
    movq %rsi, 512+6*8(%rax)
    movq %rdi, 512+7*8(%rax)
    movq %r8,  512+8*8(%rax)
    movq %r9,  512+9*8(%rax)
    movq %r10, 512+10*8(%rax)
    movq %r11, 512+11*8(%rax)
    movq %r12, 512+12*8(%rax)
    movq %r13, 512+13*8(%rax)
    movq %r14, 512+14*8(%rax)
    movq %r15, 512+15*8(%rax)
    movq g_scratch(%rip), %rcx
    movq %rcx, 512+0*8(%rax)
    pushfq
    popq %rcx
    movq %rcx, 640(%rax)
    vmovdqu %ymm0,  0*32(%rax)
    vmovdqu %ymm1,  1*32(%rax)
    vmovdqu %ymm2,  2*32(%rax)
    vmovdqu %ymm3,  3*32(%rax)
    vmovdqu %ymm4,  4*32(%rax)
    vmovdqu %ymm5,  5*32(%rax)
    vmovdqu %ymm6,  6*32(%rax)
    vmovdqu %ymm7,  7*32(%rax)
    vmovdqu %ymm8,  8*32(%rax)
    vmovdqu %ymm9,  9*32(%rax)
    vmovdqu %ymm10, 10*32(%rax)
    vmovdqu %ymm11, 11*32(%rax)
    vmovdqu %ymm12, 12*32(%rax)
    vmovdqu %ymm13, 13*32(%rax)
    vmovdqu %ymm14, 14*32(%rax)
    vmovdqu %ymm15, 15*32(%rax)
    popq %r15; popq %r14; popq %r13; popq %r12; popq %rbp; popq %rbx
    ret

// Sample instructions (never executed) so the test decodes assembler-correct
// bytes instead of hand-encoded ones. Read via the _ti_labels array.
.text
.align 4
_ti0: vpaddd %ymm2,%ymm1,%ymm0      // ymm0 = ymm1 + ymm2
_ti1: vpxor  %ymm4,%ymm3,%ymm5      // ymm5 = ymm3 ^ ymm4
_ti2: shlxq  %rcx,%r8,%rax          // rax  = r8 << rcx
_ti3: andnq  %r9,%r10,%rax          // rax  = ~r10 & r9
_ti4:

.data
.align 8
.globl _ti_labels
_ti_labels: .quad _ti0, _ti1, _ti2, _ti3, _ti4
