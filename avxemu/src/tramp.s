// tramp.s — trampoline thunk template.
//
// One copy of this template is emitted per faulting run. A patched site does
// `jmp` here; the thunk spills the live machine state into a register-file on
// its own stack frame, calls avxemu_tramp_dispatch() to emulate the run, reloads
// the (updated) state, and jumps to the run's resume address. No instruction
// ever faults.
//
// The template is position-independent: every internal reference is RIP-relative
// with a FIXED displacement, so the builder just memcpy's [tt_start,tt_end),
// appends the run_record, and writes two pointer slots (dispatch, resume). No
// code bytes are patched.
//
// Frame after `sub rsp,FRAME` (rsp-relative), matching avxemu_regfile:
//   [0   ..511] ymm[16][32]
//   [512 ..639] gpr[16]   (x86 order: rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8..r15)
//   [640]       rflags
//   [648]       rip
// FRAME is large enough that the whole regfile sits BELOW the caller's 128-byte
// red zone, so we never clobber the interrupted function's red-zone temporaries.

.set FRAME,    0x340
.set RF_GPR,   512
.set RF_FLAGS, 640
.set RF_MXCSR, 656         // SSE control/status word (preserved across the C emulation)

.text
.align 4
.globl _avxemu_tt_start
.globl _avxemu_tt_dispatchptr
.globl _avxemu_tt_resumeptr
.globl _avxemu_tt_record
.globl _avxemu_tt_end

_avxemu_tt_start:
    leaq    -FRAME(%rsp), %rsp                 // lea (NOT sub): must not disturb the
                                               // program's flags before we save them
    movq    %r11, RF_GPR+11*8(%rsp)            // save program r11 first (we reuse it)
    leaq    _avxemu_tt_record(%rip), %r11      // r11 = &run_record (this thunk's, fixed disp)

    movq    %rax, RF_GPR+0*8(%rsp)
    movq    %rcx, RF_GPR+1*8(%rsp)
    movq    %rdx, RF_GPR+2*8(%rsp)
    movq    %rbx, RF_GPR+3*8(%rsp)
    leaq    FRAME(%rsp), %rax                  // original rsp
    movq    %rax, RF_GPR+4*8(%rsp)
    movq    %rbp, RF_GPR+5*8(%rsp)
    movq    %rsi, RF_GPR+6*8(%rsp)
    movq    %rdi, RF_GPR+7*8(%rsp)
    movq    %r8,  RF_GPR+8*8(%rsp)
    movq    %r9,  RF_GPR+9*8(%rsp)
    movq    %r10, RF_GPR+10*8(%rsp)
    movq    %r12, RF_GPR+12*8(%rsp)
    movq    %r13, RF_GPR+13*8(%rsp)
    movq    %r14, RF_GPR+14*8(%rsp)
    movq    %r15, RF_GPR+15*8(%rsp)

    pushfq                                      // save rflags (transient, frame red zone)
    popq    %rax
    movq    %rax, RF_FLAGS(%rsp)

    vmovdqu %ymm0,  0*32(%rsp)
    vmovdqu %ymm1,  1*32(%rsp)
    vmovdqu %ymm2,  2*32(%rsp)
    vmovdqu %ymm3,  3*32(%rsp)
    vmovdqu %ymm4,  4*32(%rsp)
    vmovdqu %ymm5,  5*32(%rsp)
    vmovdqu %ymm6,  6*32(%rsp)
    vmovdqu %ymm7,  7*32(%rsp)
    vmovdqu %ymm8,  8*32(%rsp)
    vmovdqu %ymm9,  9*32(%rsp)
    vmovdqu %ymm10, 10*32(%rsp)
    vmovdqu %ymm11, 11*32(%rsp)
    vmovdqu %ymm12, 12*32(%rsp)
    vmovdqu %ymm13, 13*32(%rsp)
    vmovdqu %ymm14, 14*32(%rsp)
    vmovdqu %ymm15, 15*32(%rsp)
    stmxcsr RF_MXCSR(%rsp)                       // save SSE control word

    movq    %rsp, %rbx                          // rbx = regfile base (callee-saved across call)
    movq    %r11, %rdi                          // arg1 = run_record
    movq    %rbx, %rsi                          // arg2 = regfile
    andq    $-16, %rsp                          // 16-align for the C call
    call    *_avxemu_tt_dispatchptr(%rip)
    movq    %rbx, %rsp                          // restore frame base
    ldmxcsr RF_MXCSR(%rsp)                       // restore SSE control word (C emu may have set flags)

    vmovdqu 0*32(%rsp),  %ymm0
    vmovdqu 1*32(%rsp),  %ymm1
    vmovdqu 2*32(%rsp),  %ymm2
    vmovdqu 3*32(%rsp),  %ymm3
    vmovdqu 4*32(%rsp),  %ymm4
    vmovdqu 5*32(%rsp),  %ymm5
    vmovdqu 6*32(%rsp),  %ymm6
    vmovdqu 7*32(%rsp),  %ymm7
    vmovdqu 8*32(%rsp),  %ymm8
    vmovdqu 9*32(%rsp),  %ymm9
    vmovdqu 10*32(%rsp), %ymm10
    vmovdqu 11*32(%rsp), %ymm11
    vmovdqu 12*32(%rsp), %ymm12
    vmovdqu 13*32(%rsp), %ymm13
    vmovdqu 14*32(%rsp), %ymm14
    vmovdqu 15*32(%rsp), %ymm15

    movq    RF_GPR+0*8(%rbx),  %rax
    movq    RF_GPR+1*8(%rbx),  %rcx
    movq    RF_GPR+2*8(%rbx),  %rdx
    movq    RF_GPR+5*8(%rbx),  %rbp
    movq    RF_GPR+6*8(%rbx),  %rsi
    movq    RF_GPR+7*8(%rbx),  %rdi
    movq    RF_GPR+8*8(%rbx),  %r8
    movq    RF_GPR+9*8(%rbx),  %r9
    movq    RF_GPR+10*8(%rbx), %r10
    movq    RF_GPR+11*8(%rbx), %r11
    movq    RF_GPR+12*8(%rbx), %r12
    movq    RF_GPR+13*8(%rbx), %r13
    movq    RF_GPR+14*8(%rbx), %r14
    movq    RF_GPR+15*8(%rbx), %r15

    pushq   RF_FLAGS(%rbx)                      // restore rflags
    popfq
    leaq    FRAME(%rbx), %rsp                   // restore rsp (lea: preserves flags)
    movq    RF_GPR+3*8(%rbx), %rbx              // restore program rbx (last)
    jmp     *_avxemu_tt_resumeptr(%rip)

.align 8
_avxemu_tt_dispatchptr: .quad 0                 // -> avxemu_tramp_dispatch (set by builder)
_avxemu_tt_resumeptr:   .quad 0                 // -> resume address (set per run)
_avxemu_tt_record:                              // run_record bytes appended here per run
_avxemu_tt_end:

// avxemu_run_on_stack(base, sz, fn, a, b): run fn(a,b) on the side stack
// [base, base+sz). Keeps the heavy C emulation off the program stack. If rsp is
// already inside that range (a nested call, e.g. a signal during emulation),
// run fn in place so nested use grows the side stack naturally.
// rdi=base, rsi=sz, rdx=fn, rcx=a, r8=b.
.align 4
.globl _avxemu_run_on_stack
_avxemu_run_on_stack:
    movq    %rsp, %rax
    subq    %rdi, %rax
    cmpq    %rsi, %rax
    jb      1f                                  // (unsigned)(rsp-base) < sz => already on side stack
    leaq    (%rdi,%rsi), %r11                   // r11 = base + sz
    andq    $-16, %r11                          // 16-align the top
    movq    %rsp, -8(%r11)                      // stash caller rsp at [top-8]
    leaq    -16(%r11), %rsp                     // switch to the side stack
    movq    %rcx, %rdi                          // arg1 = a
    movq    %r8,  %rsi                          // arg2 = b
    callq   *%rdx                               // fn(a, b)
    movq    8(%rsp), %rsp                       // restore caller rsp from [top-8]
    ret
1:  movq    %rcx, %rdi
    movq    %r8,  %rsi
    jmpq    *%rdx                               // already on side stack: tail-call fn(a,b)
