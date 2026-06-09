// Decoder-test blob: a contiguous run of target instructions whose byte ranges
// the decoder test reads (never executed). The e2e_* fault-injection stubs live
// in ../src/selftest.s (shared with the in-dylib self-test).

.text
.align 4
.globl _DI0
_DI0:  vpaddd %ymm2,%ymm1,%ymm0
_DI1:  vpand %ymm2,%ymm1,%ymm0
_DI2:  vpsubq %ymm2,%ymm1,%ymm0
_DI3:  vpcmpeqb %ymm2,%ymm1,%ymm0
_DI4:  vpshufb %ymm2,%ymm1,%ymm0
_DI5:  vpmaxsd %ymm2,%ymm1,%ymm0
_DI6:  vpshufd $0x1b,%ymm1,%ymm0
_DI7:  vpermq $0x1b,%ymm1,%ymm0
_DI8:  vpslld $5,%ymm1,%ymm0
_DI9:  vpslld %xmm2,%ymm1,%ymm0
_DI10: vpsllvd %ymm2,%ymm1,%ymm0
_DI11: vpbroadcastb %xmm1,%ymm0
_DI12: vpmovzxbw %xmm1,%ymm0
_DI13: vextracti128 $1,%ymm1,%xmm0
_DI14: vinserti128 $1,%xmm2,%ymm1,%ymm0
_DI15: vperm2i128 $0x20,%ymm2,%ymm1,%ymm0
_DI16: vpblendd $0xc,%ymm2,%ymm1,%ymm0
_DI17: vpalignr $5,%ymm2,%ymm1,%ymm0
_DI18: vpblendvb %ymm3,%ymm2,%ymm1,%ymm0
_DI19: vpmovmskb %ymm1,%eax
_DI20: vfmadd213pd %ymm2,%ymm1,%ymm0
_DI21: vfmadd231sd %xmm2,%xmm1,%xmm0
_DI22: vcvtph2ps %xmm1,%ymm0
_DI23: andnq %r8,%rdi,%rax
_DI24: mulxq %r8,%rax,%r9
_DI25: shlxq %rcx,%r8,%rax
_DI26: rorxq $13,%r8,%rax
_DI27: tzcntq %r8,%rax
_DI28: pdepq %r8,%rdi,%rax
_DI29: vpaddd (%rsi),%ymm1,%ymm0
_DIEND:

.data
.align 4
.globl _dec_labels
_dec_labels:
    .quad _DI0,_DI1,_DI2,_DI3,_DI4,_DI5,_DI6,_DI7,_DI8,_DI9
    .quad _DI10,_DI11,_DI12,_DI13,_DI14,_DI15,_DI16,_DI17,_DI18,_DI19
    .quad _DI20,_DI21,_DI22,_DI23,_DI24,_DI25,_DI26,_DI27,_DI28,_DI29
    .quad _DIEND
.globl _dec_n
_dec_n: .quad 30
