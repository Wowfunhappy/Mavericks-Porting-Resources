/*
 * fix_macho - modify dylib paths and strip LC_BUILD_VERSION in Mach-O files.
 * Works with both thin and fat (universal) binaries.
 * Usage: fix_macho <file> [operations...]
 *   -change <old> <new>    Change a dylib path
 *   -strip_build_version   Remove LC_BUILD_VERSION commands
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>

#define LC_BUILD_VERSION_CMD 0x00000032

struct change_entry {
    const char *old_path;
    const char *new_path;
};

struct rename_seg_entry {
    const char *old_name;
    const char *new_name;
};

static int process_macho(uint8_t *buf, size_t size, struct change_entry *changes,
                         int nchanges, int strip_bv,
                         struct rename_seg_entry *renames, int nrenames) {
    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) {
        fprintf(stderr, "  Not 64-bit Mach-O (magic=0x%x)\n", hdr->magic);
        return -1;
    }

    uint8_t *lcp = buf + sizeof(struct mach_header_64);
    uint8_t *lcend = lcp + hdr->sizeofcmds;
    int modified = 0;

    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;

        if (strip_bv && lc->cmd == LC_BUILD_VERSION_CMD) {
            /* Remove by converting to padding (set to zero-filled) */
            uint32_t sz = lc->cmdsize;
            size_t tail = lcend - (lcp + sz);
            memmove(lcp, lcp + sz, tail);
            memset(lcend - sz, 0, sz);
            lcend -= sz;
            hdr->ncmds--;
            hdr->sizeofcmds -= sz;
            modified = 1;
            printf("  Removed LC_BUILD_VERSION (%u bytes)\n", sz);
            continue; /* Don't advance lcp, re-check at same position */
        }

        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            for (int r = 0; r < nrenames; r++) {
                if (strncmp(seg->segname, renames[r].old_name, 16) == 0) {
                    printf("  Renamed segment '%s' -> '%s'\n", renames[r].old_name, renames[r].new_name);
                    memset(seg->segname, 0, 16);
                    strncpy(seg->segname, renames[r].new_name, 16);
                    /* Also rename sections within this segment */
                    struct section_64 *sect = (struct section_64 *)(lcp + sizeof(struct segment_command_64));
                    for (uint32_t s = 0; s < seg->nsects; s++) {
                        memset(sect[s].segname, 0, 16);
                        strncpy(sect[s].segname, renames[r].new_name, 16);
                    }
                    modified = 1;
                    break;
                }
            }
        }

        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB ||
            lc->cmd == LC_ID_DYLIB || lc->cmd == LC_REEXPORT_DYLIB) {
            struct dylib_command *dc = (struct dylib_command *)lcp;
            char *name = (char *)lcp + dc->dylib.name.offset;

            for (int c = 0; c < nchanges; c++) {
                if (strcmp(name, changes[c].old_path) == 0) {
                    size_t old_len = strlen(changes[c].old_path);
                    size_t new_len = strlen(changes[c].new_path);
                    /* Check there's room in the existing command */
                    size_t name_off = dc->dylib.name.offset;
                    size_t available = dc->cmdsize - name_off;
                    if (new_len + 1 > available) {
                        fprintf(stderr, "  ERROR: new path '%s' too long (%zu > %zu)\n",
                                changes[c].new_path, new_len + 1, available);
                        return -1;
                    }
                    memset(name, 0, available);
                    memcpy(name, changes[c].new_path, new_len);
                    printf("  Changed: %s -> %s\n", changes[c].old_path, changes[c].new_path);
                    modified = 1;
                    break;
                }
            }
        }

        lcp += lc->cmdsize;
    }
    return modified;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <file> [-change old new] [-strip_build_version]\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];

    /* Parse operations */
    struct change_entry changes[32];
    int nchanges = 0;
    int strip_bv = 0;
    struct rename_seg_entry renames[16];
    int nrenames = 0;

    for (int i = 2; i < argc; ) {
        if (strcmp(argv[i], "-change") == 0 && i + 2 < argc) {
            changes[nchanges].old_path = argv[i+1];
            changes[nchanges].new_path = argv[i+2];
            nchanges++;
            i += 3;
        } else if (strcmp(argv[i], "-strip_build_version") == 0) {
            strip_bv = 1;
            i++;
        } else if (strcmp(argv[i], "-rename_seg") == 0 && i + 2 < argc) {
            renames[nrenames].old_name = argv[i+1];
            renames[nrenames].new_name = argv[i+2];
            nrenames++;
            i += 3;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    /* Read file */
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct stat st;
    fstat(fd, &st);
    size_t fsize = st.st_size;

    uint8_t *buf = malloc(fsize);
    if (read(fd, buf, fsize) != (ssize_t)fsize) { perror("read"); return 1; }

    int modified = 0;
    uint32_t magic = *(uint32_t *)buf;

    if (magic == FAT_CIGAM || magic == FAT_MAGIC) {
        /* Fat binary - process each slice */
        struct fat_header *fh = (struct fat_header *)buf;
        uint32_t narch = (magic == FAT_CIGAM) ? OSSwapInt32(fh->nfat_arch) : fh->nfat_arch;
        struct fat_arch *archs = (struct fat_arch *)(buf + sizeof(struct fat_header));

        for (uint32_t i = 0; i < narch; i++) {
            uint32_t offset = (magic == FAT_CIGAM) ? OSSwapInt32(archs[i].offset) : archs[i].offset;
            uint32_t asize = (magic == FAT_CIGAM) ? OSSwapInt32(archs[i].size) : archs[i].size;
            printf("Processing arch %u at offset %u:\n", i, offset);
            int r = process_macho(buf + offset, asize, changes, nchanges, strip_bv, renames, nrenames);
            if (r > 0) modified = 1;
            else if (r < 0) { fprintf(stderr, "  Skipping arch %u\n", i); }
        }
    } else if (magic == MH_MAGIC_64) {
        printf("Processing thin Mach-O:\n");
        int r = process_macho(buf, fsize, changes, nchanges, strip_bv, renames, nrenames);
        if (r > 0) modified = 1;
    } else {
        fprintf(stderr, "Not a Mach-O file (magic=0x%x)\n", magic);
        close(fd);
        return 1;
    }

    if (modified) {
        lseek(fd, 0, SEEK_SET);
        if (write(fd, buf, fsize) != (ssize_t)fsize) { perror("write"); close(fd); return 1; }
        printf("File updated: %s\n", path);
    } else {
        printf("No changes needed: %s\n", path);
    }

    close(fd);
    free(buf);
    return 0;
}
