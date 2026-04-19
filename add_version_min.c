/*
 * Append an LC_VERSION_MIN_MACOSX load command targeting 10.9. patch_macho
 * strips LC_BUILD_VERSION and leaves no platform declaration; 10.9's dyld uses
 * that signal for some behaviors (including, possibly, TLV handling).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: %s binary\n", argv[0]); return 1; }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct stat st; fstat(fd, &st);
    uint8_t *buf = malloc(st.st_size);
    if (read(fd, buf, st.st_size) != (ssize_t)st.st_size) { perror("read"); return 1; }

    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) { fprintf(stderr, "not 64-bit mach-o\n"); return 1; }

    /* Find first section offset (upper bound of header pad). */
    uint32_t first_sect_off = UINT32_MAX;
    uint8_t *lcp = buf + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++)
                if (sect[j].offset && sect[j].offset < first_sect_off)
                    first_sect_off = sect[j].offset;
        } else if (lc->cmd == LC_VERSION_MIN_MACOSX) {
            printf("LC_VERSION_MIN_MACOSX already present; nothing to do.\n");
            return 0;
        }
        lcp += lc->cmdsize;
    }

    uint32_t lc_end = sizeof(*hdr) + hdr->sizeofcmds;
    if (lc_end + sizeof(struct version_min_command) > first_sect_off) {
        fprintf(stderr, "no room for LC_VERSION_MIN_MACOSX\n");
        return 1;
    }

    struct version_min_command *vm = (struct version_min_command *)(buf + lc_end);
    memset(vm, 0, sizeof(*vm));
    vm->cmd = LC_VERSION_MIN_MACOSX;
    vm->cmdsize = sizeof(*vm);
    vm->version = (10 << 16) | (9 << 8);   /* 10.9.0 */
    vm->sdk     = (10 << 16) | (9 << 8);
    hdr->ncmds++;
    hdr->sizeofcmds += sizeof(*vm);

    lseek(fd, 0, SEEK_SET);
    if (write(fd, buf, st.st_size) != (ssize_t)st.st_size) { perror("write"); return 1; }
    close(fd);
    printf("Added LC_VERSION_MIN_MACOSX 10.9 (ncmds=%u, sizeofcmds=%u)\n",
           hdr->ncmds, hdr->sizeofcmds);
    return 0;
}
