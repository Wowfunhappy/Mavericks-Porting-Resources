/*
 * Rename a Mach-O segment, and the segname recorded in each of its sections.
 *
 * The reason this exists: 10.9's Objective-C runtime finds an image's metadata
 * by asking for named sections of the __DATA segment -- __objc_imageinfo,
 * __objc_classlist, __objc_catlist, __objc_protolist and the rest. Linkers
 * from Xcode 10 onward place those in __DATA_CONST instead (a segment that
 * later dyld versions re-protect read-only once binding is done). 10.9's
 * libobjc does not look there, so it concludes the image contains no
 * Objective-C at all and skips it: classes go unregistered and, more subtly,
 * the __objc_selrefs entries are never fixed up. Each selref then still holds
 * a pointer to its method-name string rather than a registered SEL, and the
 * first message sent through one dies with
 *
 *   NSForwarding: warning: selector (0x...) for message 'foo:' does not match
 *   selector known to Objective C runtime
 *
 * Renaming __DATA_CONST to __DATA puts the sections where the runtime looks.
 * It is safe because the two segments carry the same protections (initprot
 * read+write); __DATA_CONST differs only in that a newer dyld hardens it after
 * fixups, which 10.9's dyld never does either way. The resulting image has two
 * segments named __DATA, which is legal -- section lookup is by the
 * (segment, section) name pair, and no section name appears in both.
 *
 * Usage: rename_segment binary OLDNAME NEWNAME
 *
 * The new name must be no longer than the 16 bytes a segname field holds.
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
    if (argc != 4) {
        fprintf(stderr, "Usage: %s binary OLDNAME NEWNAME\n", argv[0]);
        return 1;
    }
    const char *path = argv[1], *oldname = argv[2], *newname = argv[3];
    if (strlen(newname) > 16) {
        fprintf(stderr, "new segment name longer than 16 bytes\n");
        return 1;
    }

    int fd = open(path, O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct stat st;
    if (fstat(fd, &st) != 0) { perror("fstat"); close(fd); return 1; }
    uint8_t *buf = malloc(st.st_size);
    if (!buf) { close(fd); return 1; }
    if (read(fd, buf, st.st_size) != (ssize_t)st.st_size) {
        perror("read"); free(buf); close(fd); return 1;
    }

    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) {
        fprintf(stderr, "%s: not a 64-bit Mach-O\n", path);
        free(buf); close(fd); return 1;
    }

    int renamed = 0;
    uint8_t *lcp = buf + sizeof(*hdr);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lcp;
            if (strncmp(seg->segname, oldname, 16) == 0) {
                memset(seg->segname, 0, 16);
                strncpy(seg->segname, newname, 16);
                /* Each section repeats its segment's name; getsectiondata
                 * matches on the section's copy, so it has to change too. */
                struct section_64 *sects = (struct section_64 *)(lcp + sizeof(*seg));
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    memset(sects[s].segname, 0, 16);
                    strncpy(sects[s].segname, newname, 16);
                }
                renamed++;
            }
        }
        lcp += lc->cmdsize;
    }

    if (!renamed) { free(buf); close(fd); return 2; }   /* nothing to do */

    if (lseek(fd, 0, SEEK_SET) != 0 ||
        write(fd, buf, st.st_size) != (ssize_t)st.st_size) {
        perror("write"); free(buf); close(fd); return 1;
    }
    free(buf); close(fd);
    printf("%s: renamed %d segment(s) %s -> %s\n", path, renamed, oldname, newname);
    return 0;
}
