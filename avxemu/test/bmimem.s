// BMI ops with a MEMORY operand — the path the reg-only fuzzer never reaches.
// Each op has an _e2e_ form (ud2-injected -> traps into the emulator) and a
// _nat_ form (runs natively on a BMI host). The driver compares them; if the
// emulator mis-sizes the memory load (the __memcpy_chk overflow bug) or reads
// the wrong source, the results diverge or the test aborts.
//
// Convention: rdi = &mem (8-byte operand), rsi = scalar 2nd operand -> rax.

.text
.align 4

// --- memory as the a_src (read into s1) ---
.globl _e2e_shlx_mem
_e2e_shlx_mem:  movq %rsi,%rcx; ud2; shlxq %rcx,(%rdi),%rax; ret
.globl _nat_shlx_mem
_nat_shlx_mem:  movq %rsi,%rcx; shlxq %rcx,(%rdi),%rax; ret

.globl _e2e_sarx_mem
_e2e_sarx_mem:  movq %rsi,%rcx; ud2; sarxq %rcx,(%rdi),%rax; ret
.globl _nat_sarx_mem
_nat_sarx_mem:  movq %rsi,%rcx; sarxq %rcx,(%rdi),%rax; ret

.globl _e2e_shrx_mem
_e2e_shrx_mem:  movq %rsi,%rcx; ud2; shrxq %rcx,(%rdi),%rax; ret
.globl _nat_shrx_mem
_nat_shrx_mem:  movq %rsi,%rcx; shrxq %rcx,(%rdi),%rax; ret

.globl _e2e_bextr_mem
_e2e_bextr_mem: movq %rsi,%rcx; ud2; bextrq %rcx,(%rdi),%rax; ret
.globl _nat_bextr_mem
_nat_bextr_mem: movq %rsi,%rcx; bextrq %rcx,(%rdi),%rax; ret

.globl _e2e_bzhi_mem
_e2e_bzhi_mem:  movq %rsi,%rcx; ud2; bzhiq %rcx,(%rdi),%rax; ret
.globl _nat_bzhi_mem
_nat_bzhi_mem:  movq %rsi,%rcx; bzhiq %rcx,(%rdi),%rax; ret

.globl _e2e_blsr_mem
_e2e_blsr_mem:  ud2; blsrq (%rdi),%rax; ret
.globl _nat_blsr_mem
_nat_blsr_mem:  blsrq (%rdi),%rax; ret

.globl _e2e_rorx_mem
_e2e_rorx_mem:  ud2; rorxq $13,(%rdi),%rax; ret
.globl _nat_rorx_mem
_nat_rorx_mem:  rorxq $13,(%rdi),%rax; ret

// 32-bit memory form (mem_bytes must be 4, not 16/32)
.globl _e2e_shlx_mem32
_e2e_shlx_mem32: movq %rsi,%rcx; ud2; shlxl %ecx,(%rdi),%eax; ret
.globl _nat_shlx_mem32
_nat_shlx_mem32: movq %rsi,%rcx; shlxl %ecx,(%rdi),%eax; ret

// --- memory as the b_src (read into s2) ---
.globl _e2e_andn_mem
_e2e_andn_mem:  movq %rsi,%r10; ud2; andnq (%rdi),%r10,%rax; ret
.globl _nat_andn_mem
_nat_andn_mem:  movq %rsi,%r10; andnq (%rdi),%r10,%rax; ret

.globl _e2e_mulx_mem
_e2e_mulx_mem:  movq %rsi,%rdx; ud2; mulxq (%rdi),%rax,%r9; ret
.globl _nat_mulx_mem
_nat_mulx_mem:  movq %rsi,%rdx; mulxq (%rdi),%rax,%r9; ret

.globl _e2e_pdep_mem
_e2e_pdep_mem:  movq %rsi,%r10; ud2; pdepq (%rdi),%r10,%rax; ret
.globl _nat_pdep_mem
_nat_pdep_mem:  movq %rsi,%r10; pdepq (%rdi),%r10,%rax; ret

.globl _e2e_pext_mem
_e2e_pext_mem:  movq %rsi,%r10; ud2; pextq (%rdi),%r10,%rax; ret
.globl _nat_pext_mem
_nat_pext_mem:  movq %rsi,%r10; pextq (%rdi),%r10,%rax; ret
