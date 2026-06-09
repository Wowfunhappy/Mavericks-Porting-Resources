// Patched-form lzcnt/tzcnt stubs. The bytes are exactly what the in-memory
// patcher produces: the F3 prefix of a real lzcnt/tzcnt rewritten to F0 (lock),
// which #UDs on every x86 (lock on a register-dest bsr/bsf is illegal). On the
// AVX2 dev host that #UD traps into the production handler, which decodes the
// F0 form (decode.c) and emulates it. Arg in %rdi, result returned in %rax.

.text
.align 4

.globl _e2e_plzcnt64
_e2e_plzcnt64:
    .byte 0xF0,0x48,0x0F,0xBD,0xC7   // (lock) lzcnt %rdi,%rax  [patched F3->F0]
    ret

.globl _e2e_ptzcnt64
_e2e_ptzcnt64:
    .byte 0xF0,0x48,0x0F,0xBC,0xC7   // (lock) tzcnt %rdi,%rax
    ret

.globl _e2e_plzcnt32
_e2e_plzcnt32:
    .byte 0xF0,0x0F,0xBD,0xC7        // (lock) lzcnt %edi,%eax
    ret

.globl _e2e_ptzcnt32
_e2e_ptzcnt32:
    .byte 0xF0,0x0F,0xBC,0xC7        // (lock) tzcnt %edi,%eax
    ret

.globl _e2e_plzcnt_mem
_e2e_plzcnt_mem:
    .byte 0xF0,0x48,0x0F,0xBD,0x07   // (lock) lzcnt (%rdi),%rax
    ret
