/*
 * Convert a Mach-O dylib from chained fixups format (macOS 12+)
 * to traditional LC_DYLD_INFO_ONLY format (macOS 10.6+).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#define LC_DYLD_EXPORTS_TRIE    0x80000033
#define LC_DYLD_CHAINED_FIXUPS  0x80000034
#define LC_BUILD_VERSION_CMD    0x00000032

/* Chained fixups structures (not in 10.9 headers) */
struct cf_header {
    uint32_t fixups_version;
    uint32_t starts_offset;
    uint32_t imports_offset;
    uint32_t symbols_offset;
    uint32_t imports_count;
    uint32_t imports_format;
    uint32_t symbols_format;
};

struct cf_starts_image {
    uint32_t seg_count;
    uint32_t seg_info_offset[];
};

struct cf_starts_seg {
    uint32_t size;
    uint16_t page_size;
    uint16_t pointer_format;
    uint64_t segment_offset;
    uint32_t max_valid_pointer;
    uint16_t page_count;
    uint16_t page_start[];
};

struct cf_import {
    uint32_t bits; /* lib_ordinal:8, weak_import:1, name_offset:23 */
};

#define CF_PTR_64        2
#define CF_PTR_64_OFFSET 6
#define CF_START_NONE    0xFFFF

/* Dynamic buffer for opcodes */
struct opbuf {
    uint8_t *data;
    size_t len, cap;
};

static void ob_init(struct opbuf *b) { b->data = malloc(1024*1024); b->len = 0; b->cap = 1024*1024; }
static void ob_byte(struct opbuf *b, uint8_t v) { b->data[b->len++] = v; }
static void ob_uleb(struct opbuf *b, uint64_t v) {
    do { uint8_t byte = v & 0x7F; v >>= 7; if (v) byte |= 0x80; ob_byte(b, byte); } while (v);
}
static void ob_str(struct opbuf *b, const char *s) {
    while (*s) ob_byte(b, *s++);
    ob_byte(b, 0);
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "Usage: %s input output\n", argv[0]); return 1; }

    /* Read file */
    int fd = open(argv[1], O_RDONLY);
    struct stat st; fstat(fd, &st);
    size_t fsize = st.st_size;
    uint8_t *buf = malloc(fsize + 2*1024*1024);
    if (read(fd, buf, fsize) != (ssize_t)fsize) { perror("read"); return 1; }
    close(fd);

    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) { fprintf(stderr, "Not 64-bit Mach-O (magic=0x%x)\n", hdr->magic); return 1; }

    /* Collect segments and find special load commands */
    struct segment_command_64 *segs[32] = {0};
    int nsegs = 0;
    uint64_t image_base_vmaddr = 0; /* __TEXT's vmaddr: the pre-slide base. */
    uint32_t exports_off = 0, exports_size = 0;
    uint32_t fixups_off = 0, fixups_size = 0;
    int has_dyld_info_only = 0;

    /* Track positions and sizes of commands to remove */
    struct { uint8_t *pos; uint32_t size; } to_remove[4];
    int n_remove = 0;

    uint8_t *lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg = (struct segment_command_64 *)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) image_base_vmaddr = seg->vmaddr;
            segs[nsegs++] = seg;
        } else if (lc->cmd == LC_DYLD_EXPORTS_TRIE) {
            uint32_t *d = (uint32_t *)lc;
            exports_off = d[2]; exports_size = d[3];
            to_remove[n_remove++] = (typeof(to_remove[0])){lcp, lc->cmdsize};
            printf("Exports trie: off=%u size=%u\n", exports_off, exports_size);
        } else if (lc->cmd == LC_DYLD_CHAINED_FIXUPS) {
            uint32_t *d = (uint32_t *)lc;
            fixups_off = d[2]; fixups_size = d[3];
            to_remove[n_remove++] = (typeof(to_remove[0])){lcp, lc->cmdsize};
            printf("Chained fixups: off=%u size=%u\n", fixups_off, fixups_size);
        } else if (lc->cmd == LC_DYLD_INFO_ONLY) {
            has_dyld_info_only = 1;
        } else if (lc->cmd == LC_BUILD_VERSION_CMD) {
            to_remove[n_remove++] = (typeof(to_remove[0])){lcp, lc->cmdsize};
        }
        lcp += lc->cmdsize;
    }

    /* Idempotency: a binary that already has LC_DYLD_INFO_ONLY and no chained
     * fixups has been through this tool before (or never needed patching),
     * and re-running the conversion would error out on the missing fixups.
     * Pass the input through unchanged so driver scripts can invoke this
     * tool safely on an already-converted binary. */
    if (!fixups_off && has_dyld_info_only) {
        printf("Already patched (LC_DYLD_INFO_ONLY present, no chained fixups) — passing through.\n");
        fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (fd < 0) { perror("create output"); return 1; }
        if (write(fd, buf, fsize) != (ssize_t)fsize) { perror("write"); close(fd); return 1; }
        close(fd);
        free(buf);
        return 0;
    }
    if (!fixups_off) { fprintf(stderr, "No chained fixups found\n"); return 1; }
    printf("Found %d segments\n", nsegs);

    /* Parse chained fixups */
    struct cf_header *cfh = (struct cf_header *)(buf + fixups_off);
    struct cf_starts_image *csi = (struct cf_starts_image *)(buf + fixups_off + cfh->starts_offset);
    struct cf_import *imports = (struct cf_import *)(buf + fixups_off + cfh->imports_offset);
    char *sympool = (char *)(buf + fixups_off + cfh->symbols_offset);

    printf("Fixups v%u: %u imports, %u segs in starts\n",
           cfh->fixups_version, cfh->imports_count, csi->seg_count);

    /* Generate rebase and bind opcodes */
    struct opbuf rebase, bind;
    ob_init(&rebase); ob_init(&bind);
    ob_byte(&rebase, REBASE_OPCODE_SET_TYPE_IMM | REBASE_TYPE_POINTER);
    ob_byte(&bind, BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);

    int total_rebases = 0, total_binds = 0;

    for (uint32_t si = 0; si < csi->seg_count && si < (uint32_t)nsegs; si++) {
        if (csi->seg_info_offset[si] == 0) continue;
        struct cf_starts_seg *ss = (struct cf_starts_seg *)((uint8_t *)csi + csi->seg_info_offset[si]);
        printf("  Seg %u (%s): fmt=%u pages=%u segoff=0x%llx\n",
               si, segs[si]->segname, ss->pointer_format, ss->page_count, ss->segment_offset);

        for (uint16_t pg = 0; pg < ss->page_count; pg++) {
            uint16_t ps = ss->page_start[pg];
            if (ps == CF_START_NONE) continue;

            uint64_t off_in_seg = (uint64_t)pg * ss->page_size + ps;

            while (1) {
                uint64_t file_pos = segs[si]->fileoff + off_in_seg;
                if (file_pos + 8 > fsize) {
                    fprintf(stderr, "Fixup out of bounds at seg %u off 0x%llx\n", si, off_in_seg);
                    break;
                }
                uint64_t *ptr = (uint64_t *)(buf + file_pos);
                uint64_t raw = *ptr;
                int is_bind = (raw >> 63) & 1;
                uint32_t next;

                if (ss->pointer_format == CF_PTR_64 || ss->pointer_format == CF_PTR_64_OFFSET) {
                    /* Both formats: next is 12 bits at position 51, stride 4 */
                    next = ((raw >> 51) & 0xFFF) * 4;
                } else {
                    fprintf(stderr, "Unknown ptr format %u\n", ss->pointer_format);
                    return 1;
                }

                if (is_bind) {
                    uint32_t ordinal = raw & 0xFFFFFF;
                    if (ordinal >= cfh->imports_count) {
                        fprintf(stderr, "Bad ordinal %u\n", ordinal);
                        break;
                    }
                    uint32_t bits = imports[ordinal].bits;
                    int lib_ord = bits & 0xFF;
                    /* Handle signed 8-bit lib ordinal */
                    if (lib_ord > 127) lib_ord -= 256;
                    int weak = (bits >> 8) & 1;
                    uint32_t name_off = bits >> 9;
                    char *sym = sympool + name_off;

                    /* Emit bind opcodes. dyld in macOS 10.9 only understands
                     * special ordinals 0 (self), -1 (main exec), -2 (flat).
                     * -3 (weak lookup) is rejected with "bad special ordinal",
                     * so remap to flat lookup + weak-import flag. */
                    int effective_weak = weak;
                    int effective_ord = lib_ord;
                    if (lib_ord == -3) {
                        effective_ord = -2;
                        effective_weak = 1;
                    }
                    if (effective_ord < 0) {
                        ob_byte(&bind, BIND_OPCODE_SET_DYLIB_SPECIAL_IMM | (effective_ord & 0x0F));
                    } else if (effective_ord < 16) {
                        ob_byte(&bind, BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | effective_ord);
                    } else {
                        ob_byte(&bind, BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB);
                        ob_uleb(&bind, (uint64_t)effective_ord);
                    }
                    ob_byte(&bind, BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM | (effective_weak ? BIND_SYMBOL_FLAGS_WEAK_IMPORT : 0));
                    ob_str(&bind, sym);
                    ob_byte(&bind, BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | si);
                    ob_uleb(&bind, off_in_seg);
                    ob_byte(&bind, BIND_OPCODE_DO_BIND);

                    *ptr = 0; /* dyld will fill in */
                    total_binds++;
                } else {
                    /* Rebase. CF_PTR_64 stores a full VM address (with 8 high
                     * bits packed in the chain). CF_PTR_64_OFFSET stores an
                     * offset from image base — we have to add image_base so the
                     * classic REBASE (which adds slide, not slide+image_base)
                     * ends up at the right place. */
                    uint64_t target;
                    if (ss->pointer_format == CF_PTR_64) {
                        target = raw & 0x7FFFFFFFFFF; /* bits [42:0] */
                        uint8_t high8 = (raw >> 43) & 0xFF;
                        target |= (uint64_t)high8 << 56;
                    } else { /* CF_PTR_64_OFFSET */
                        target = (raw & 0xFFFFFFFFF) + image_base_vmaddr;
                    }

                    ob_byte(&rebase, REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | si);
                    ob_uleb(&rebase, off_in_seg);
                    ob_byte(&rebase, REBASE_OPCODE_DO_REBASE_IMM_TIMES | 1);

                    *ptr = target;
                    total_rebases++;
                }

                if (next == 0) break;
                off_in_seg += next;
            }
        }
    }

    ob_byte(&rebase, REBASE_OPCODE_DONE);
    ob_byte(&bind, BIND_OPCODE_DONE);
    printf("Processed %d rebases, %d binds\n", total_rebases, total_binds);

    /* Remove old commands from the header (process in reverse order to maintain positions) */
    /* Sort by position descending */
    for (int i = 0; i < n_remove - 1; i++) {
        for (int j = i + 1; j < n_remove; j++) {
            if (to_remove[j].pos > to_remove[i].pos) {
                typeof(to_remove[0]) tmp = to_remove[i];
                to_remove[i] = to_remove[j];
                to_remove[j] = tmp;
            }
        }
    }

    uint8_t *lcmds_end = buf + sizeof(struct mach_header_64) + hdr->sizeofcmds;
    for (int i = 0; i < n_remove; i++) {
        uint8_t *pos = to_remove[i].pos;
        uint32_t sz = to_remove[i].size;
        size_t tail = lcmds_end - (pos + sz);
        memmove(pos, pos + sz, tail);
        lcmds_end -= sz;
        hdr->ncmds--;
        hdr->sizeofcmds -= sz;
        printf("Removed cmd at %ld (size %u)\n", pos - buf, sz);
    }

    /* Add LC_DYLD_INFO_ONLY */
    /* Append data at end of file (aligned) */
    size_t new_end = (fsize + 7) & ~7UL;
    uint32_t rebase_foff = (uint32_t)new_end;
    memcpy(buf + new_end, rebase.data, rebase.len);
    new_end += rebase.len;
    new_end = (new_end + 7) & ~7UL;
    uint32_t bind_foff = (uint32_t)new_end;
    memcpy(buf + new_end, bind.data, bind.len);
    new_end += bind.len;

    /* Check space for new load command.
     * __TEXT segment starts at fileoff=0 (includes header), but actual section
     * data starts much later. Find first section offset. */
    uint32_t first_sect_off = 0;
    for (int i = 0; i < nsegs; i++) {
        struct section_64 *sect = (struct section_64 *)((uint8_t *)segs[i] + sizeof(struct segment_command_64));
        for (uint32_t j = 0; j < segs[i]->nsects; j++) {
            if (sect[j].offset > 0 && (first_sect_off == 0 || sect[j].offset < first_sect_off))
                first_sect_off = sect[j].offset;
        }
    }
    if (first_sect_off == 0) first_sect_off = 4096; /* fallback */
    uint8_t *first_data = buf + first_sect_off;
    if (lcmds_end + 48 > first_data) {
        fprintf(stderr, "ERROR: No room for LC_DYLD_INFO_ONLY (need 48 bytes, have %ld)\n",
                first_data - lcmds_end);
        return 1;
    }

    struct dyld_info_command *di = (struct dyld_info_command *)lcmds_end;
    memset(di, 0, 48);
    di->cmd = LC_DYLD_INFO_ONLY;
    di->cmdsize = 48;
    di->rebase_off = rebase_foff;
    di->rebase_size = (uint32_t)rebase.len;
    di->bind_off = bind_foff;
    di->bind_size = (uint32_t)bind.len;
    di->export_off = exports_off;
    di->export_size = exports_size;
    hdr->ncmds++;
    hdr->sizeofcmds += 48;

    printf("Added LC_DYLD_INFO_ONLY: rebase=%u+%zu bind=%u+%zu exports=%u+%u\n",
           rebase_foff, rebase.len, bind_foff, bind.len, exports_off, exports_size);

    /* Extend __LINKEDIT segment to cover the appended opcode data — dyld only
     * accesses file ranges declared by some segment, and our new data lives
     * past the old __LINKEDIT end. */
    struct segment_command_64 *linkedit = NULL;
    for (int i = 0; i < nsegs; i++) {
        if (strcmp(segs[i]->segname, "__LINKEDIT") == 0) { linkedit = segs[i]; break; }
    }
    if (!linkedit) { fprintf(stderr, "No __LINKEDIT segment found\n"); return 1; }
    uint64_t needed_end = new_end;
    uint64_t new_filesize = needed_end - linkedit->fileoff;
    uint64_t new_vmsize = (new_filesize + 0xFFF) & ~0xFFFUL;
    if (new_filesize > linkedit->filesize) {
        printf("Extending __LINKEDIT: filesize %llu -> %llu, vmsize %llu -> %llu\n",
               linkedit->filesize, new_filesize, linkedit->vmsize, new_vmsize);
        linkedit->filesize = new_filesize;
        linkedit->vmsize = new_vmsize;
    }

    /* Write output */
    fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) { perror("create output"); return 1; }
    write(fd, buf, new_end);
    close(fd);
    printf("Wrote %s (%zu bytes)\n", argv[2], new_end);

    free(rebase.data); free(bind.data); free(buf);
    return 0;
}
