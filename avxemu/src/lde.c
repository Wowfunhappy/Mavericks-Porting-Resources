/*
 * lde.c — x86-64 length decoder (see lde.h).
 *
 * Length only: prefixes -> REX/VEX -> opcode -> ModRM(+SIB+disp) -> immediate.
 * The per-opcode "has ModRM" / immediate tables cover the encodings a compiler
 * emits; the whole thing is checked against otool's instruction boundaries over
 * the entire binary, so any gap shows up as a desync and is fixed.
 */

#include "lde.h"
#include <stdlib.h>
#include <string.h>

/* imm codes: 0 none, 1 imm8, 2 imm16, 3 immZ(2/4), 4 immV(2/4/8), 5 moffs(8), 6 enter(imm16+imm8) */

static int modrm1(uint8_t op) {
    switch (op) {
    case 0x00:case 0x01:case 0x02:case 0x03: case 0x08:case 0x09:case 0x0A:case 0x0B:
    case 0x10:case 0x11:case 0x12:case 0x13: case 0x18:case 0x19:case 0x1A:case 0x1B:
    case 0x20:case 0x21:case 0x22:case 0x23: case 0x28:case 0x29:case 0x2A:case 0x2B:
    case 0x30:case 0x31:case 0x32:case 0x33: case 0x38:case 0x39:case 0x3A:case 0x3B:
    case 0x63: case 0x69:case 0x6B:
    case 0x80:case 0x81:case 0x83: case 0x84:case 0x85:case 0x86:case 0x87:
    case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F:
    case 0xC0:case 0xC1:case 0xC6:case 0xC7:
    case 0xD0:case 0xD1:case 0xD2:case 0xD3:
    case 0xD8:case 0xD9:case 0xDA:case 0xDB:case 0xDC:case 0xDD:case 0xDE:case 0xDF:
    case 0xF6:case 0xF7:case 0xFE:case 0xFF:
        return 1;
    default: return 0;
    }
}
static int imm1(uint8_t op) {
    switch (op) {
    case 0x04:case 0x0C:case 0x14:case 0x1C:case 0x24:case 0x2C:case 0x34:case 0x3C: return 1;
    case 0x05:case 0x0D:case 0x15:case 0x1D:case 0x25:case 0x2D:case 0x35:case 0x3D: return 3;
    case 0x68: return 3; case 0x6A: return 1; case 0x69: return 3; case 0x6B: return 1;
    case 0x70:case 0x71:case 0x72:case 0x73:case 0x74:case 0x75:case 0x76:case 0x77:
    case 0x78:case 0x79:case 0x7A:case 0x7B:case 0x7C:case 0x7D:case 0x7E:case 0x7F: return 1;
    case 0x80: return 1; case 0x81: return 3; case 0x83: return 1;
    case 0xA0:case 0xA1:case 0xA2:case 0xA3: return 5;
    case 0xA8: return 1; case 0xA9: return 3;
    case 0xB0:case 0xB1:case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:case 0xB7: return 1;
    case 0xB8:case 0xB9:case 0xBA:case 0xBB:case 0xBC:case 0xBD:case 0xBE:case 0xBF: return 4;
    case 0xC0:case 0xC1:case 0xC6: return 1; case 0xC7: return 3;
    case 0xC2:case 0xCA: return 2; case 0xC8: return 6; case 0xCD: return 1;
    case 0xE0:case 0xE1:case 0xE2:case 0xE3:case 0xE4:case 0xE5:case 0xE6:case 0xE7: return 1;
    case 0xE8:case 0xE9: return 3; case 0xEB: return 1;
    default: return 0;
    }
}
static int modrm0F(uint8_t op) {
    switch (op) {
    case 0x05:case 0x06:case 0x07:case 0x08:case 0x09:case 0x0B:case 0x0E:
    case 0x30:case 0x31:case 0x32:case 0x33:case 0x34:case 0x35:case 0x37:case 0x77:
    case 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:case 0x87:
    case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F:
    case 0xA0:case 0xA1:case 0xA2:case 0xA8:case 0xA9:case 0xAA:
    case 0xC8:case 0xC9:case 0xCA:case 0xCB:case 0xCC:case 0xCD:case 0xCE:case 0xCF:
        return 0;
    default: return 1;
    }
}
static int imm0F(uint8_t op) {
    switch (op) {
    case 0x70:case 0x71:case 0x72:case 0x73: return 1;
    case 0xA4:case 0xAC:case 0xBA: return 1;
    case 0xC2:case 0xC4:case 0xC5:case 0xC6: return 1;
    case 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:case 0x87:
    case 0x88:case 0x89:case 0x8A:case 0x8B:case 0x8C:case 0x8D:case 0x8E:case 0x8F: return 3;
    default: return 0;
    }
}

static const uint8_t *dmod(const uint8_t *p, const uint8_t *end) {
    if (p >= end) return 0;
    uint8_t m = *p++; int mod = m >> 6, rm = m & 7;
    if (mod == 3) return p;
    if (rm == 4) { if (p >= end) return 0; uint8_t sib = *p++; if (mod == 0 && (sib & 7) == 5) p += 4; }
    else if (mod == 0 && rm == 5) p += 4;       /* rip-relative / disp32 */
    if (mod == 1) p += 1; else if (mod == 2) p += 4;
    return p;
}
static const uint8_t *addimm(const uint8_t *p, const uint8_t *end, int code, int opsz, int rexw) {
    int n = 0;
    switch (code) {
    case 1: n = 1; break; case 2: n = 2; break;
    case 3: n = opsz ? 2 : 4; break;
    case 4: n = rexw ? 8 : (opsz ? 2 : 4); break;
    case 5: n = 8; break; case 6: n = 3; break; default: n = 0;
    }
    if (p + n > end) return 0;
    return p + n;
}

int x86_len(const uint8_t *p, const uint8_t *end, int *zk, int *pfx_off) {
    const uint8_t *s = p;
    int opsz = 0, rexw = 0, f3 = 0, f3_pos = -1;
    *zk = 0; *pfx_off = -1;

    for (;;) {
        if (p >= end) return 0;
        uint8_t c = *p;
        if (c == 0x66) { opsz = 1; p++; }
        else if (c == 0x67) { p++; }
        else if (c == 0xF0 || c == 0xF2) { p++; }
        else if (c == 0xF3) { f3 = 1; f3_pos = (int)(p - s); p++; }
        else if (c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65) { p++; }
        else break;
    }
    if (p < end && (*p & 0xF0) == 0x40) { rexw = (*p >> 3) & 1; p++; }
    if (p >= end) return 0;
    uint8_t op = *p++;

    if (op == 0xC5) {                                  /* 2-byte VEX */
        if (p >= end) return 0; p++;                   /* vex byte */
        if (p >= end) return 0; uint8_t vop = *p++;
        if (vop != 0x77) { p = dmod(p, end); if (!p) return 0; }  /* 0F77 = vzeroupper/all: no ModRM */
        if (imm0F(vop) == 1) { if (p >= end) return 0; p++; }
    } else if (op == 0xC4) {                            /* 3-byte VEX */
        if (p + 1 >= end) return 0; int map = p[0] & 0x1f; p += 2;
        if (p >= end) return 0; uint8_t vop = *p++;
        if (!(map == 1 && vop == 0x77)) { p = dmod(p, end); if (!p) return 0; }
        int imm = (map == 3) ? 1 : ((map == 1 && imm0F(vop) == 1) ? 1 : 0);
        if (imm) { if (p >= end) return 0; p++; }
    } else if (op == 0x62) {                            /* EVEX (AVX-512); 4-byte prefix */
        if (p + 2 >= end) return 0; int map = p[0] & 0x03; p += 3;
        if (p >= end) return 0; uint8_t vop = *p++;
        p = dmod(p, end); if (!p) return 0;             /* disp8 is 1 byte (compressed) — same length */
        int imm = (map == 3) ? 1 : ((map == 1 && imm0F(vop) == 1) ? 1 : 0);
        if (imm) { if (p >= end) return 0; p++; }
    } else if (op == 0x0F) {
        if (p >= end) return 0; uint8_t o2 = *p++;
        if (o2 == 0x38) {
            if (p >= end) return 0; p++;                /* third opcode byte */
            p = dmod(p, end); if (!p) return 0;         /* 0F38: no imm */
        } else if (o2 == 0x3A) {
            if (p >= end) return 0; p++;
            p = dmod(p, end); if (!p) return 0;
            if (p >= end) return 0; p++;                /* imm8 */
        } else if (o2 == 0x0F) {                        /* 3DNow!: modrm + imm8 suffix */
            p = dmod(p, end); if (!p) return 0;
            if (p >= end) return 0; p++;
        } else {
            if (f3 && o2 == 0xBD) { *zk = 1; *pfx_off = f3_pos; }   /* lzcnt */
            if (f3 && o2 == 0xBC) { *zk = 2; *pfx_off = f3_pos; }   /* tzcnt */
            if (modrm0F(o2)) { p = dmod(p, end); if (!p) return 0; }
            p = addimm(p, end, imm0F(o2), opsz, rexw); if (!p) return 0;
        }
    } else {
        if (modrm1(op)) {
            if (p >= end) return 0;
            uint8_t modrm = *p;
            p = dmod(p, end); if (!p) return 0;
            int ic = imm1(op);
            if (op == 0xF6) ic = (((modrm >> 3) & 7) < 2) ? 1 : 0;
            else if (op == 0xF7) ic = (((modrm >> 3) & 7) < 2) ? 3 : 0;
            p = addimm(p, end, ic, opsz, rexw); if (!p) return 0;
        } else {
            p = addimm(p, end, imm1(op), opsz, rexw); if (!p) return 0;
        }
    }
    if (p > end) return 0;
    return (int)(p - s);
}

/* classify control flow of the instruction at p (length len, offset insn_off):
 * *term = 1 if it has no fallthrough; *tgt = branch target offset, or -1. */
static void cflow(const uint8_t *p, const uint8_t *end, int len, long insn_off,
                  int *term, long *tgt) {
    *term = 0; *tgt = -1;
    const uint8_t *q = p;
    while (q < end) { uint8_t c = *q;
        if (c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65) q++;
        else break; }
    if (q < end && (*q & 0xF0) == 0x40) q++;
    if (q >= end) return;
    uint8_t op = *q;
    const uint8_t *nx = p + len;                 /* next instruction */
    if (op == 0xEB) { *term = 1; *tgt = insn_off + len + (int8_t)nx[-1]; }
    else if (op == 0xE9) { int32_t r; memcpy(&r, nx-4, 4); *term = 1; *tgt = insn_off + len + r; }
    else if (op >= 0x70 && op <= 0x7F) { *tgt = insn_off + len + (int8_t)nx[-1]; }
    else if (op == 0x0F && q+1 < end && q[1] >= 0x80 && q[1] <= 0x8F) { int32_t r; memcpy(&r, nx-4, 4); *tgt = insn_off + len + r; }
    else if (op == 0xC3 || op == 0xC2 || op == 0xCB || op == 0xCA) *term = 1;     /* ret */
    else if (op == 0xF4) *term = 1;                                              /* hlt */
    else if (op == 0x0F && q+1 < end && q[1] == 0x0B) *term = 1;                 /* ud2 */
    /* note: indirect jmp (ff /4,/5) is NOT treated as terminating — clang emits
     * switch-case code immediately after the dispatch, and that code is real and
     * reachable. The jump table itself lives elsewhere (rip-relative), so falling
     * through doesn't decode table data. */
}

/* is the instruction at p a `leaq disp(%rip), reg`? if so return its rip-rel
 * disp (and the opcode-relative position is handled by caller via length). */
static int is_lea_rip(const uint8_t *p, const uint8_t *end, int32_t *disp) {
    const uint8_t *q = p;
    while (q < end) { uint8_t c=*q; if(c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65)q++; else break; }
    if (q >= end || (*q & 0xF8) != 0x48) return 0;          /* REX.W (48-4f with W) */
    if (!(*q & 0x08)) return 0; q++;
    if (q+1 >= end || *q != 0x8D) return 0;                 /* lea */
    uint8_t m = q[1];
    if ((m >> 6) != 0 || (m & 7) != 5) return 0;            /* mod=00 rm=101 => rip-rel */
    memcpy(disp, q + 2, 4);
    return 1;
}
/* is it an indirect jmp `jmpq *reg` (ff /4, mod=11) — a switch dispatch? */
static int is_jmp_indirect(const uint8_t *p, const uint8_t *end) {
    const uint8_t *q = p;
    while (q < end) { uint8_t c=*q; if(c==0x66||c==0x67||c==0xF0||c==0xF2||c==0xF3||c==0x2E||c==0x36||c==0x3E||c==0x26||c==0x64||c==0x65)q++; else break; }
    if (q < end && (*q & 0xF0) == 0x40) q++;
    return q+1 < end && *q == 0xFF && ((q[1] >> 3) & 7) == 4;
}

/* Public wrapper for the trampoline scanner: classify the instruction at p.
 * *term=1 if no fallthrough; *tgt = direct-branch target offset or -1;
 * *indirect = 1 if an indirect jmp (ff /4). */
void lde_cflow(const uint8_t *p, const uint8_t *end, int len, long insn_off,
               int *term, long *tgt, int *indirect) {
    cflow(p, end, len, insn_off, term, tgt);
    *indirect = is_jmp_indirect(p, end);
}

long lde_rd_zcnt(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
                 size_t *out, int maxout) {
    size_t n = fend - fstart;
    uint8_t *vis = calloc(n, 1); size_t *stk = malloc(n * sizeof(size_t));
    if (!vis || !stk) { free(vis); free(stk); return 0; }
    size_t sp = 0; long cnt = 0; long last_lea = -1;   /* table base offset from most recent leaq-rip */
    stk[sp++] = fstart;
    while (sp) {
        size_t off = stk[--sp];
        if (off < fstart || off >= fend || vis[off - fstart]) continue;
        int zk, o2; int len = x86_len(text + off, text + fend, &zk, &o2);
        if (len <= 0) continue;
        vis[off - fstart] = 1;
        if (zk && o2 >= 0 && text[off + o2] == 0xF3 && cnt < maxout) out[cnt++] = off;

        int32_t disp;
        if (is_lea_rip(text + off, text + fend, &disp))
            last_lea = (long)(off + len) + disp;           /* table base (offset space) */
        else if (is_jmp_indirect(text + off, text + fend) && last_lea >= 0) {
            /* resolve switch table: 4-byte entries, target = base + entry */
            for (int i = 0; i < 4096; i++) {
                size_t e = (size_t)last_lea + 4 * i;
                if (e + 4 > readable) break;
                int32_t entry; memcpy(&entry, text + e, 4);
                long tgt = last_lea + entry;
                if (tgt < (long)fstart || tgt >= (long)fend) break;   /* past table */
                if (!vis[tgt - fstart]) stk[sp++] = (size_t)tgt;
            }
        }
        int term; long tgt;
        cflow(text + off, text + fend, len, (long)off, &term, &tgt);
        if (tgt >= 0 && (size_t)tgt >= fstart && (size_t)tgt < fend && !vis[tgt - fstart]) stk[sp++] = (size_t)tgt;
        if (!term) { size_t fa = off + len; if (fa < fend && !vis[fa - fstart]) stk[sp++] = fa; }
    }
    free(vis); free(stk);
    return cnt;
}

int lde_rd_map(const uint8_t *text, size_t fstart, size_t fend, size_t readable,
               uint8_t *code, uint8_t *tgt, int *has_indirect) {
    size_t n = fend - fstart;
    size_t *stk = malloc(n * sizeof(size_t));
    if (!stk) return 0;
    size_t sp = 0; long last_lea = -1; *has_indirect = 0;
    #define RD_PUSH(X) do { if (sp < n) stk[sp++] = (size_t)(X); } while (0)
    stk[sp++] = fstart;
    while (sp) {
        size_t off = stk[--sp];
        if (off < fstart || off >= fend || code[off - fstart]) continue;
        int zk, o2; int len = x86_len(text + off, text + fend, &zk, &o2);
        if (len <= 0) continue;
        code[off - fstart] = 1;

        int32_t disp;
        if (is_lea_rip(text + off, text + fend, &disp)) {
            last_lea = (long)(off + len) + disp;           /* switch-table base (offset space) */
        } else if (is_jmp_indirect(text + off, text + fend)) {
            int resolved = 0;
            if (last_lea >= 0)
                for (int i = 0; i < 4096; i++) {           /* resolve 4-byte switch entries */
                    size_t e = (size_t)last_lea + 4 * i;
                    if (e + 4 > readable) break;
                    int32_t entry; memcpy(&entry, text + e, 4);
                    long t = last_lea + entry;
                    if (t < (long)fstart || t >= (long)fend) break;   /* past the table */
                    resolved = 1;
                    tgt[t - fstart] = 1;
                    if (!code[t - fstart]) RD_PUSH(t);
                }
            if (!resolved) *has_indirect = 1;              /* unknown targets remain */
        }
        int term; long t;
        cflow(text + off, text + fend, len, (long)off, &term, &t);
        if (t >= (long)fstart && (size_t)t < fend) {
            tgt[t - fstart] = 1;
            if (!code[t - fstart]) RD_PUSH(t);
        }
        if (!term) { size_t fa = off + len; if (fa < fend && !code[fa - fstart]) RD_PUSH(fa); }
    }
    #undef RD_PUSH
    free(stk);
    return 1;
}

long lde_scan_func(uint8_t *text, size_t fstart, size_t fend, size_t readable,
                   int patch, size_t *out, int maxout) {
    /* fast path: linear decode that reaches the boundary (padding-tolerant) */
    size_t q = fstart; int nt = 0, ok = 1;
    while (q < fend) {
        if ((size_t)(fend - q) <= 16) {
            int pad = 1;
            for (size_t r = q; r < fend; r++) if (text[r]!=0 && text[r]!=0xCC && text[r]!=0x90) { pad = 0; break; }
            if (pad) { q = fend; break; }
        }
        int zk, o2; int len = x86_len(text + q, text + fend, &zk, &o2);
        if (len <= 0) { ok = 0; break; }
        if (zk && o2 >= 0 && text[q + o2] == 0xF3 && nt < maxout) out[nt++] = q;
        q += len;
    }
    if (!(ok && q == fend))                              /* dirty: recursive descent */
        nt = (int)lde_rd_zcnt(text, fstart, fend, readable, out, maxout);

    if (patch)
        for (int i = 0; i < nt; i++) {
            int zk, o2; x86_len(text + out[i], text + fend, &zk, &o2);
            if (o2 >= 0 && text[out[i] + o2] == 0xF3) text[out[i] + o2] = 0xF0;
        }
    return nt;
}

long lde_scan_zcnt(uint8_t *text, size_t n, int patch) {
    uint8_t *end = text + n, *p = text;
    long count = 0;
    while (p < end) {
        int zk, off;
        int len = x86_len(p, end, &zk, &off);
        if (len <= 0) return -1;                       /* desync */
        if (zk && off >= 0 && p[off] == 0xF3) {
            count++;
            if (patch) p[off] = 0xF0;                  /* F3 -> F0 (lock) => #UD */
        }
        p += len;
    }
    return count;
}
