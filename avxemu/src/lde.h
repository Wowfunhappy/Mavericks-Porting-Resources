#ifndef AVXEMU_LDE_H
#define AVXEMU_LDE_H

#include <stdint.h>
#include <stddef.h>

/*
 * Minimal x86-64 length disassembler — just enough to walk instruction
 * boundaries and locate lzcnt/tzcnt. We need lengths (not semantics) so we can
 * find `F3 [REX] 0F BD/BC` precisely and never mistake a mid-instruction or
 * data byte for one. Validated against otool over the whole binary.
 */

/* Decode one instruction at p (bounded by end). Returns its length, or 0 on a
 * decode error / truncation. *zk = 1 for lzcnt, 2 for tzcnt, 0 otherwise;
 * *pfx_off = byte offset (within the instruction) of the F3 prefix to patch. */
int x86_len(const uint8_t *p, const uint8_t *end, int *zk, int *pfx_off);

/* Walk [text, text+n) instruction by instruction. For each lzcnt/tzcnt, if
 * patch != 0, rewrite its F3 prefix to F0 (lock) so it faults (#UD). Returns
 * the number of lzcnt/tzcnt seen, or -1 on a decode desync. */
long lde_scan_zcnt(uint8_t *text, size_t n, int patch);

/* Recursive-descent scan of one function [fstart, fend) (offsets into text):
 * follows control flow so embedded jump-table data is never decoded as code.
 * Records the offsets of lzcnt/tzcnt in reachable code into out[] (up to
 * maxout). Returns the count. Used for functions that don't decode cleanly
 * linearly. */
long lde_rd_zcnt(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
                 size_t *out, int maxout);

/*
 * Scan one function [fstart, fend) for lzcnt/tzcnt. If the function decodes
 * cleanly to its boundary, uses fast linear scan; otherwise falls back to
 * recursive descent (skips embedded jump-table data, resolves switch tables).
 * Records instruction-start offsets in out[]. If patch != 0, also rewrites each
 * one's F3 prefix to F0 (lock) so it faults. `readable` bounds table reads.
 */
long lde_scan_func(uint8_t *text, size_t fstart, size_t fend, size_t readable,
                   int patch, size_t *out, int maxout);

/* Classify the instruction at p for the trampoline scanner (see lde.c). */
void lde_cflow(const uint8_t *p, const uint8_t *end, int len, long insn_off,
               int *term, long *tgt, int *indirect);

/*
 * Recursive-descent reachability map of one function [fstart, fend) for the
 * trampoline scanner. Follows control flow (and resolves switch tables) so
 * embedded jump-table data is never treated as code. Marks every reachable
 * instruction-start offset (relative to fstart) in code[] and every direct or
 * resolved-switch branch-target offset in tgt[] (both caller-allocated, length
 * fend-fstart). Sets *has_indirect if an indirect jmp had no resolvable table,
 * so unknown targets may remain and the caller must stay conservative.
 * `readable` bounds table reads. Returns 1, or 0 on allocation failure.
 */
int lde_rd_map(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
               uint8_t *code, uint8_t *tgt, int *has_indirect);

#endif /* AVXEMU_LDE_H */
