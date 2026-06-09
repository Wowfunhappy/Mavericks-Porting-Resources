/*
 * lde_validate.c — check the length decoder against otool over the whole binary.
 *
 * Reads otool's instruction addresses (hex, one per line) on stdin, walks the
 * same __text bytes with x86_len, and asserts every boundary matches. Any
 * divergence is a length-decode bug, printed with context.
 *
 *   otool -tV <bin> | awk '/^[0-9a-f]+\t/{print $1}' | \
 *       lde_validate <bin> <text_vmaddr_hex> <text_fileoff>
 */
#include "lde.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s bin vmaddr fileoff < addrs\n", argv[0]); return 2; }
    uint64_t vmaddr = strtoull(argv[2], 0, 16);
    long fileoff = atol(argv[3]);
    FILE *bf = fopen(argv[1], "rb"); if (!bf) { perror("bin"); return 2; }
    fseek(bf, 0, SEEK_END); long fsz = ftell(bf); fseek(bf, 0, SEEK_SET);
    uint8_t *buf = malloc(fsz); if (fread(buf, 1, fsz, bf) != (size_t)fsz) return 2; fclose(bf);

    /* read otool addresses */
    size_t cap = 1 << 20, na = 0; uint64_t *A = malloc(cap * sizeof *A);
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        uint64_t a; if (sscanf(line, "%llx", (unsigned long long *)&a) != 1) continue;
        if (na == cap) { cap *= 2; A = realloc(A, cap * sizeof *A); }
        A[na++] = a;
    }
    if (!na) { fprintf(stderr, "no addresses\n"); return 2; }

    /* walk: my instruction i must start at A[i] */
    uint64_t va = A[0];
    long i = 0, bad = 0, zcnt = 0;
    while (i < (long)na) {
        if (va != A[i]) {
            if (bad < 8) {
                long fo = (long)(A[i-1] - vmaddr) + fileoff;
                fprintf(stderr, "DESYNC at otool[%ld]=0x%llx (my=0x%llx); prev otool=0x%llx bytes=",
                        i, (unsigned long long)A[i], (unsigned long long)va, (unsigned long long)A[i-1]);
                for (int k = 0; k < 10; k++) fprintf(stderr, "%02x ", buf[fo+k]);
                fprintf(stderr, "\n");
            }
            bad++;
            /* resync to otool to keep finding further issues */
            va = A[i];
        }
        long fo = (long)(va - vmaddr) + fileoff;
        int zk, off;
        int len = x86_len(buf + fo, buf + fsz, &zk, &off);
        if (len <= 0) { fprintf(stderr, "decode error at 0x%llx\n", (unsigned long long)va); bad++; break; }
        if (zk) zcnt++;
        va += len;
        i++;
    }
    printf("instructions: %ld   boundary mismatches: %ld   lzcnt/tzcnt found: %ld\n", na, bad, zcnt);
    return bad ? 1 : 0;
}
