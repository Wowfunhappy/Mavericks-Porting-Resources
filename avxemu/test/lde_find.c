/* lde_find.c — linear sweep of __text, print every lzcnt/tzcnt vmaddr (hex). */
#include "lde.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: %s bin vmaddr fileoff size\n", argv[0]); return 2; }
    uint64_t vmaddr = strtoull(argv[2], 0, 16);
    long fileoff = atol(argv[3]);
    uint64_t size = strtoull(argv[4], 0, 16);
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("bin"); return 2; }
    fseek(f, fileoff, SEEK_SET);
    uint8_t *t = malloc(size); if (fread(t, 1, size, f) != size) return 2; fclose(f);

    uint8_t *p = t, *end = t + size; long desync = 0, found = 0;
    while (p < end) {
        int zk, off;
        int len = x86_len(p, end, &zk, &off);
        if (len <= 0) { desync++; p++; continue; }      /* skip a byte and retry */
        if (zk && off >= 0 && p[off] == 0xF3) {
            printf("%llx\n", (unsigned long long)(vmaddr + (uint64_t)(p - t)));
            found++;
        }
        p += len;
    }
    fprintf(stderr, "found=%ld desync_bytes=%ld\n", found, desync);
    return 0;
}
