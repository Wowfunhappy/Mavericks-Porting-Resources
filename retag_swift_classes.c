/*
 * Retag Objective-C class records from the stable-ABI is-swift bit to the
 * legacy one, so a Swift runtime built for a pre-10.14.4 deployment target
 * recognises them.
 *
 * A class record's data word carries a tag in its low two bits saying whether
 * the class is a Swift class. Which bit is used depends on the deployment
 * target of whatever produced it:
 *
 *   bit 1 (value 2)  stable ABI  -- emitted when targeting macOS 10.14.4+
 *   bit 0 (value 1)  legacy      -- emitted when targeting anything older
 *
 * The Swift runtime checks whichever bit its *own* deployment target implies.
 * A runtime built for 10.9 therefore tests bit 0, while an application built
 * for 10.15 tags its classes with bit 1. Nothing rejects the mismatch: the
 * runtime simply concludes that none of the application's classes are Swift
 * classes, treats each as a plain Objective-C class, and takes the
 * ObjC-class-wrapper path in swift_getObjCClassMetadata. For a Swift class
 * that overrides an Objective-C initialiser, that turns super.init() into a
 * call to itself, and the process dies of an infinite recursion long before
 * anything is drawn.
 *
 * Objective-C itself is indifferent: objc masks both bits off before using the
 * pointer, and on 10.9 pure Objective-C classes leave them zero, so moving the
 * tag from one bit to the other changes nothing for the Objective-C runtime.
 *
 * Both halves of each class pair are retagged -- the class and its metaclass,
 * reached through the class record's isa field.
 *
 * Usage: retag_swift_classes binary [binary ...]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>

#define IS_SWIFT_STABLE 2
#define IS_SWIFT_LEGACY 1

struct seg { uint64_t vmaddr, vmsize, fileoff; };

/* class_t: isa, superclass, cache, vtable, data -- data is at offset 32. */
#define CLASS_ISA_OFFSET   0
#define CLASS_DATA_OFFSET 32

static int find_section(uint8_t *buf, const char *want_seg, const char *want_sect,
                        uint64_t *out_off, uint64_t *out_size,
                        struct seg *segs, int *nsegs) {
    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    uint8_t *lcp = buf + sizeof(*hdr);
    int found = 0;
    *nsegs = 0;
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *sc = (struct segment_command_64 *)lcp;
            if (*nsegs < 64) {
                segs[*nsegs].vmaddr = sc->vmaddr;
                segs[*nsegs].vmsize = sc->vmsize;
                segs[*nsegs].fileoff = sc->fileoff;
                (*nsegs)++;
            }
            struct section_64 *s = (struct section_64 *)(lcp + sizeof(*sc));
            for (uint32_t k = 0; k < sc->nsects; k++) {
                if (strncmp(s[k].segname, want_seg, 16) == 0 &&
                    strncmp(s[k].sectname, want_sect, 16) == 0) {
                    *out_off = s[k].offset;
                    *out_size = s[k].size;
                    found = 1;
                }
            }
        }
        lcp += lc->cmdsize;
    }
    return found;
}

static int64_t file_off(struct seg *segs, int nsegs, uint64_t va) {
    for (int i = 0; i < nsegs; i++)
        if (va >= segs[i].vmaddr && va < segs[i].vmaddr + segs[i].vmsize)
            return (int64_t)(segs[i].fileoff + (va - segs[i].vmaddr));
    return -1;
}

/* Retag one class record in place; returns 1 if it changed. */
static int retag(uint8_t *buf, struct seg *segs, int nsegs, uint64_t class_va) {
    int64_t co = file_off(segs, nsegs, class_va);
    if (co < 0) return 0;
    uint64_t *data = (uint64_t *)(buf + co + CLASS_DATA_OFFSET);
    if ((*data & 3) != IS_SWIFT_STABLE) return 0;
    *data = (*data & ~(uint64_t)3) | IS_SWIFT_LEGACY;
    return 1;
}

static int process(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); return -1; }
    struct stat st;
    if (fstat(fd, &st) != 0) { perror("fstat"); close(fd); return -1; }
    uint8_t *buf = malloc(st.st_size);
    if (!buf || read(fd, buf, st.st_size) != (ssize_t)st.st_size) {
        perror("read"); free(buf); close(fd); return -1;
    }
    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) { free(buf); close(fd); return 0; }

    struct seg segs[64];
    int nsegs = 0;
    uint64_t listoff = 0, listsize = 0;
    int changed = 0;

    static const char *lists[] = { "__objc_classlist", "__objc_nlclslist" };
    for (size_t li = 0; li < sizeof(lists)/sizeof(lists[0]); li++) {
        /* Modern linkers place the list in __DATA_CONST; a port may already
         * have renamed that segment to __DATA, so accept either. */
        if (!find_section(buf, "__DATA", lists[li], &listoff, &listsize, segs, &nsegs) &&
            !find_section(buf, "__DATA_CONST", lists[li], &listoff, &listsize, segs, &nsegs))
            continue;
        for (uint64_t i = 0; i + 8 <= listsize; i += 8) {
            uint64_t cls_va = *(uint64_t *)(buf + listoff + i);
            if (!cls_va) continue;
            changed += retag(buf, segs, nsegs, cls_va);
            /* The metaclass carries the same tag and is reached via isa. */
            int64_t co = file_off(segs, nsegs, cls_va);
            if (co >= 0) {
                uint64_t meta_va = *(uint64_t *)(buf + co + CLASS_ISA_OFFSET);
                if (meta_va) changed += retag(buf, segs, nsegs, meta_va);
            }
        }
    }

    if (changed) {
        if (lseek(fd, 0, SEEK_SET) != 0 ||
            write(fd, buf, st.st_size) != (ssize_t)st.st_size) {
            perror("write"); free(buf); close(fd); return -1;
        }
    }
    free(buf);
    close(fd);
    return changed;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s binary [binary ...]\n", argv[0]); return 1; }
    int total = 0;
    for (int i = 1; i < argc; i++) {
        int n = process(argv[i]);
        if (n > 0) { printf("%s: retagged %d class record(s)\n", argv[i], n); total += n; }
    }
    printf("total: %d class record(s) retagged\n", total);
    return 0;
}
