/*
 * lde_find2.c — function-guided lzcnt/tzcnt finder.
 *
 * Parses LC_FUNCTION_STARTS and decodes each function from its true start, so a
 * desync in one function's trailing junk/jump-table can't propagate into the
 * next function's real code. Prints lzcnt/tzcnt vmaddrs. Validate vs otool.
 */
#include "lde.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t uleb(const uint8_t **p, const uint8_t *end) {
    uint64_t r = 0; int s = 0; uint8_t b;
    do { if (*p >= end) return r; b = *(*p)++; r |= (uint64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80);
    return r;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s bin\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("bin"); return 2; }
    fseek(f, 0, SEEK_END); long fsz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(fsz); if (fread(buf, 1, fsz, f) != (size_t)fsz) return 2; fclose(f);

    /* parse Mach-O 64 */
    uint32_t ncmds = *(uint32_t *)(buf + 16);
    uint64_t textseg_vm = 0, text_vm = 0; long text_off = 0; uint64_t text_size = 0;
    uint32_t fs_off = 0, fs_size = 0;
    const uint8_t *lc = buf + 32;
    for (uint32_t i = 0; i < ncmds; i++) {
        uint32_t cmd = *(uint32_t *)lc, sz = *(uint32_t *)(lc + 4);
        if (cmd == 0x19) {                                   /* LC_SEGMENT_64 */
            if (!memcmp(lc + 8, "__TEXT", 7)) {
                textseg_vm = *(uint64_t *)(lc + 24);
                uint32_t nsect = *(uint32_t *)(lc + 64);
                const uint8_t *s = lc + 72;
                for (uint32_t j = 0; j < nsect; j++, s += 80)
                    if (!memcmp(s, "__text", 7)) {
                        text_vm = *(uint64_t *)(s + 32);
                        text_size = *(uint64_t *)(s + 40);
                        text_off = *(uint32_t *)(s + 48);
                    }
            }
        } else if (cmd == 0x26) {                            /* LC_FUNCTION_STARTS */
            fs_off = *(uint32_t *)(lc + 8); fs_size = *(uint32_t *)(lc + 12);
        }
        lc += sz;
    }
    if (!text_vm || !fs_off) { fprintf(stderr, "missing __text or function starts\n"); return 2; }

    /* decode function-start addresses */
    const uint8_t *p = buf + fs_off, *fe = p + fs_size;
    uint64_t addr = textseg_vm;
    uint64_t *fn = malloc(((fs_size) + 1) * sizeof(uint64_t)); size_t nf = 0;
    while (p < fe) { uint64_t d = uleb(&p, fe); if (!d) break; addr += d; fn[nf++] = addr; }
    fn[nf] = text_vm + text_size;                            /* sentinel end */

    long total = 0;
    for (size_t i = 0; i < nf; i++) {
        uint64_t fstart = fn[i], fend = fn[i + 1];
        if (fstart < text_vm || fstart >= text_vm + text_size) continue;
        if (fend > text_vm + text_size) fend = text_vm + text_size;
        size_t res[256];
        long rn = lde_scan_func(buf + text_off, fstart - text_vm, fend - text_vm,
                                (size_t)(fsz - text_off), 0, res, 256);
        for (long k = 0; k < rn; k++) printf("%llx\n", (unsigned long long)(text_vm + res[k]));
        total += rn;
    }
    fprintf(stderr, "total lzcnt/tzcnt found: %ld\n", total);
    return 0;
}
