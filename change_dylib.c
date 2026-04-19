/*
 * Rewrite LC_LOAD_DYLIB paths, growing the load command when the replacement
 * path doesn't fit. Uses header padding between the last load command and the
 * first section's file data.
 *
 * Usage: change_dylib input [-change old new]...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>

struct change {
    const char *old_path;
    const char *new_path;   /* NULL = delete; "" = in-place, no path change */
    int reexport;           /* 1 = promote LC_LOAD_DYLIB -> LC_REEXPORT_DYLIB */
};

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s input [-change old new] [-delete path] ...\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];

    struct change changes[32];
    int nchanges = 0;
    for (int i = 2; i < argc; ) {
        if (strcmp(argv[i], "-change") == 0 && i + 2 < argc) {
            changes[nchanges].old_path = argv[i+1];
            changes[nchanges].new_path = argv[i+2];
            changes[nchanges].reexport = 0;
            nchanges++;
            i += 3;
        } else if (strcmp(argv[i], "-delete") == 0 && i + 1 < argc) {
            changes[nchanges].old_path = argv[i+1];
            changes[nchanges].new_path = NULL;
            changes[nchanges].reexport = 0;
            nchanges++;
            i += 2;
        } else if (strcmp(argv[i], "-reexport") == 0 && i + 1 < argc) {
            changes[nchanges].old_path = argv[i+1];
            changes[nchanges].new_path = "";
            changes[nchanges].reexport = 1;
            nchanges++;
            i += 2;
        } else { fprintf(stderr, "bad arg: %s\n", argv[i]); return 1; }
    }

    int fd = open(path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct stat st; fstat(fd, &st);
    size_t fsize = st.st_size;
    uint8_t *buf = malloc(fsize);
    if (read(fd, buf, fsize) != (ssize_t)fsize) { perror("read"); return 1; }

    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) { fprintf(stderr, "not 64-bit Mach-O\n"); return 1; }

    /* Find the first section's file offset: that bounds header pad. */
    uint32_t first_sect_off = UINT32_MAX;
    uint8_t *lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            struct section_64 *sect = (struct section_64 *)(lcp + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if (sect[j].offset > 0 && sect[j].offset < first_sect_off)
                    first_sect_off = sect[j].offset;
            }
        }
        lcp += lc->cmdsize;
    }
    if (first_sect_off == UINT32_MAX) first_sect_off = 4096;
    uint32_t cur_lc_end = sizeof(struct mach_header_64) + hdr->sizeofcmds;
    uint32_t pad_avail = first_sect_off - cur_lc_end;
    printf("Header pad: %u bytes available (LC end=%u, first sect=%u)\n",
           pad_avail, cur_lc_end, first_sect_off);

    /* Walk LCs, collecting the new cmdsizes we'll need. */
    uint8_t *new_lcs = calloc(1, first_sect_off); /* max possible */
    uint32_t new_off = 0;
    int modifications = 0;

    lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        uint32_t cmdsize = lc->cmdsize;
        uint32_t write_size = cmdsize;
        int matched = -1;

        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB ||
            lc->cmd == LC_ID_DYLIB || lc->cmd == LC_REEXPORT_DYLIB) {
            struct dylib_command *dc = (struct dylib_command *)lcp;
            const char *name = (const char *)lcp + dc->dylib.name.offset;
            /* LC_ID_DYLIB is this dylib's own identity — don't rewrite or delete it. */
            if (lc->cmd != LC_ID_DYLIB) {
                for (int c = 0; c < nchanges; c++) {
                    if (strcmp(name, changes[c].old_path) == 0) { matched = c; break; }
                }
            }
            if (matched >= 0 && changes[matched].new_path != NULL) {
                size_t base = dc->dylib.name.offset;
                size_t new_len = strlen(changes[matched].new_path) + 1;
                uint32_t needed = (uint32_t)((base + new_len + 7) & ~7UL);
                if (needed < cmdsize) needed = cmdsize;
                write_size = needed;
            }
        }

        if (matched >= 0 && changes[matched].new_path == NULL) {
            /* Delete: don't copy this LC and don't advance new_off. */
            printf("  Delete [%u bytes]: %s\n", cmdsize, changes[matched].old_path);
            hdr->ncmds--;
            modifications++;
        } else {
            memcpy(new_lcs + new_off, lcp, cmdsize);
            if (matched >= 0) {
                struct dylib_command *ndc = (struct dylib_command *)(new_lcs + new_off);
                ndc->cmdsize = write_size;
                if (changes[matched].reexport) {
                    ndc->cmd = LC_REEXPORT_DYLIB;
                    printf("  Reexport: %s\n", changes[matched].old_path);
                }
                if (changes[matched].new_path[0] != '\0') {
                    size_t base = ndc->dylib.name.offset;
                    memset(new_lcs + new_off + base, 0, write_size - base);
                    strcpy((char *)(new_lcs + new_off + base), changes[matched].new_path);
                    printf("  Change [%u->%u bytes]: %s -> %s\n",
                           cmdsize, write_size,
                           changes[matched].old_path, changes[matched].new_path);
                }
                modifications++;
            }
            new_off += write_size;
        }
        lcp += cmdsize;
    }

    if (new_off > first_sect_off) {
        fprintf(stderr, "ERROR: new LCs (%u bytes) don't fit in header pad (%u avail)\n",
                new_off - (uint32_t)sizeof(struct mach_header_64), pad_avail);
        return 1;
    }
    if (modifications == 0) { printf("No matching dylibs found.\n"); return 0; }

    /* Zero the entire LC area, then write new table. */
    memset(buf + sizeof(struct mach_header_64), 0, first_sect_off - sizeof(struct mach_header_64));
    memcpy(buf + sizeof(struct mach_header_64), new_lcs, new_off);
    hdr->sizeofcmds = new_off;

    lseek(fd, 0, SEEK_SET);
    if (write(fd, buf, fsize) != (ssize_t)fsize) { perror("write"); return 1; }
    close(fd);
    printf("Updated %s (sizeofcmds=%u)\n", path, new_off);
    return 0;
}
