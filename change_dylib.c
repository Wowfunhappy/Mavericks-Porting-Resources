/*
 * Rewrite LC_LOAD_DYLIB paths.
 *
 * By default the new load commands must fit in the header padding between the
 * last load command and the first section's file data; if they don't, the tool
 * fails (unchanged behavior). Pass -grow to opt in to enlarging that padding
 * first (see macho_grow.h) — that resize only works on a PIE executable and is
 * rejected otherwise.
 *
 * Usage: change_dylib input [-grow] [-change old new] [-delete path]
 *                     [-reexport path] [-add path] [-strip-lc name]...
 *
 * -strip-lc drops a whole load command by kind (uuid, codesig, source-version,
 * build-version, code-sign-drs). It reclaims header padding without moving any
 * file data, so unlike -grow it never disturbs image-base-relative structures.
 * Prefer it when a longer path needs a few more bytes.
 *
 * -add appends a brand-new LC_LOAD_DYLIB naming `path` (combine with -grow to
 * guarantee header room). Used to bake a dependency — e.g. libavxemu.dylib —
 * into the binary itself, so only that binary loads it (not children inheriting
 * DYLD_INSERT_LIBRARIES). See HEADER_PAD_GROWTH.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mach-o/loader.h>

#include "macho_grow.h"

/* Load commands safe to drop: purely informational, or invalidated the moment
 * the binary is rewritten. Deliberately excludes LC_FUNCTION_STARTS (avxemu
 * reads it for patch-safety bounds) and LC_DATA_IN_CODE. */
#ifndef LC_SOURCE_VERSION
#define LC_SOURCE_VERSION 0x2A
#endif
#ifndef LC_BUILD_VERSION
#define LC_BUILD_VERSION 0x32
#endif
#ifndef LC_DYLIB_CODE_SIGN_DRS
#define LC_DYLIB_CODE_SIGN_DRS 0x2B
#endif
static const struct { const char *name; uint32_t cmd; } strippable[] = {
    { "uuid",           LC_UUID                },
    { "codesig",        LC_CODE_SIGNATURE      },
    { "source-version", LC_SOURCE_VERSION      },
    { "build-version",  LC_BUILD_VERSION       },
    { "code-sign-drs",  LC_DYLIB_CODE_SIGN_DRS },
};

struct change {
    const char *old_path;
    const char *new_path;   /* NULL = delete; "" = in-place, no path change */
    int reexport;           /* 1 = promote LC_LOAD_DYLIB -> LC_REEXPORT_DYLIB */
};

/*
 * Build the new load-command table into `new_lcs` from the current header in
 * `buf`. Returns the new total size (sizeofcmds) via *out_off, the new command
 * count via *out_ncmds, and how many changes applied via *out_mods. Does NOT
 * mutate the header, so it is safe to call more than once (e.g. again after the
 * header pad has been grown). `verbose` prints the per-change diagnostics once.
 */
static void build_lcs(const uint8_t *buf, const struct change *changes, int nchanges,
                      const char *const *adds, int nadds,
                      const uint32_t *strip, int nstrip,
                      uint8_t *new_lcs, uint32_t *out_off, uint32_t *out_ncmds,
                      int *out_mods, int verbose) {
    const struct mach_header_64 *hdr = (const struct mach_header_64 *)buf;
    uint32_t new_off = 0, ncmds = hdr->ncmds;
    int mods = 0;

    const uint8_t *lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)lcp;
        uint32_t cmdsize = lc->cmdsize;
        uint32_t write_size = cmdsize;
        int matched = -1;

        /* Dropping a command reclaims its bytes for the rest of the table.
         * Any __LINKEDIT payload it referenced simply stops being reachable;
         * nothing moves, so no offset anywhere needs fixing up. */
        int stripped = 0;
        for (int s = 0; s < nstrip; s++)
            if (lc->cmd == strip[s]) { stripped = 1; break; }
        if (stripped) {
            if (verbose) printf("  Strip [%u bytes]: load command 0x%x\n", cmdsize, lc->cmd);
            ncmds--;
            mods++;
            lcp += cmdsize;
            continue;
        }

        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB ||
            lc->cmd == LC_ID_DYLIB || lc->cmd == LC_REEXPORT_DYLIB) {
            const struct dylib_command *dc = (const struct dylib_command *)lcp;
            const char *name = (const char *)lcp + dc->dylib.name.offset;
            if (lc->cmd != LC_ID_DYLIB) {  /* never rewrite this dylib's own identity */
                for (int c = 0; c < nchanges; c++)
                    if (strcmp(name, changes[c].old_path) == 0) { matched = c; break; }
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
            if (verbose) printf("  Delete [%u bytes]: %s\n", cmdsize, changes[matched].old_path);
            ncmds--;
            mods++;
        } else {
            memcpy(new_lcs + new_off, lcp, cmdsize);
            if (matched >= 0) {
                struct dylib_command *ndc = (struct dylib_command *)(new_lcs + new_off);
                ndc->cmdsize = write_size;
                if (changes[matched].reexport) {
                    ndc->cmd = LC_REEXPORT_DYLIB;
                    if (verbose) printf("  Reexport: %s\n", changes[matched].old_path);
                }
                if (changes[matched].new_path[0] != '\0') {
                    size_t base = ndc->dylib.name.offset;
                    memset(new_lcs + new_off + base, 0, write_size - base);
                    strcpy((char *)(new_lcs + new_off + base), changes[matched].new_path);
                    if (verbose)
                        printf("  Change [%u->%u bytes]: %s -> %s\n", cmdsize, write_size,
                               changes[matched].old_path, changes[matched].new_path);
                }
                mods++;
            }
            new_off += write_size;
        }
        lcp += cmdsize;
    }

    /* Append brand-new LC_LOAD_DYLIB commands (-add). Each is a dylib_command
     * (name lc_str, timestamp, versions) followed by the NUL-terminated path,
     * the whole thing padded to 8 bytes. */
    for (int a = 0; a < nadds; a++) {
        size_t plen = strlen(adds[a]) + 1;
        uint32_t cs = (uint32_t)((sizeof(struct dylib_command) + plen + 7) & ~7UL);
        struct dylib_command *ndc = (struct dylib_command *)(new_lcs + new_off);
        memset(ndc, 0, cs);
        ndc->cmd = LC_LOAD_DYLIB;
        ndc->cmdsize = cs;
        ndc->dylib.name.offset = sizeof(struct dylib_command);
        ndc->dylib.timestamp = 2;            /* conventional (matches install_name_tool) */
        ndc->dylib.current_version = 0;
        ndc->dylib.compatibility_version = 0;
        strcpy((char *)ndc + sizeof(struct dylib_command), adds[a]);
        new_off += cs;
        ncmds++;
        mods++;
        if (verbose) printf("  Add [%u bytes]: LC_LOAD_DYLIB %s\n", cs, adds[a]);
    }

    *out_off = new_off;
    *out_ncmds = ncmds;
    *out_mods = mods;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s input [-grow] [-change old new] [-delete path] "
                        "[-reexport path] [-add path] [-strip-lc name] ...\n", argv[0]);
        fprintf(stderr, "  -strip-lc kinds:");
        for (size_t k = 0; k < sizeof(strippable)/sizeof(strippable[0]); k++)
            fprintf(stderr, " %s", strippable[k].name);
        fprintf(stderr, "\n");
        return 1;
    }
    const char *path = argv[1];

    struct change changes[32];
    int nchanges = 0;
    const char *adds[32];
    int nadds = 0;
    int allow_grow = 0;
    uint32_t strip[16];
    int nstrip = 0;
    for (int i = 2; i < argc; ) {
        if (strcmp(argv[i], "-grow") == 0) {
            allow_grow = 1;
            i += 1;
        } else if (strcmp(argv[i], "-strip-lc") == 0 && i + 1 < argc) {
            size_t k, nk = sizeof(strippable)/sizeof(strippable[0]);
            for (k = 0; k < nk; k++)
                if (strcmp(argv[i+1], strippable[k].name) == 0) break;
            if (k == nk) { fprintf(stderr, "unknown -strip-lc kind: %s\n", argv[i+1]); return 1; }
            if (nstrip == 16) { fprintf(stderr, "too many -strip-lc\n"); return 1; }
            strip[nstrip++] = strippable[k].cmd;
            i += 2;
        } else if (strcmp(argv[i], "-add") == 0 && i + 1 < argc) {
            adds[nadds++] = argv[i+1];
            i += 2;
        } else if (strcmp(argv[i], "-change") == 0 && i + 2 < argc) {
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

    uint32_t first_sect_off = mg_first_sect_off(buf);
    uint32_t cur_lc_end = sizeof(struct mach_header_64) + hdr->sizeofcmds;
    uint32_t pad_avail = first_sect_off > cur_lc_end ? first_sect_off - cur_lc_end : 0;
    printf("Header pad: %u bytes available (LC end=%u, first sect=%u)\n",
           pad_avail, cur_lc_end, first_sect_off);

    /* Upper bound on bytes the -add commands contribute, so the scratch buffer
     * can hold the full new table even before the header pad is grown. */
    uint32_t add_bytes = 0;
    for (int a = 0; a < nadds; a++)
        add_bytes += (uint32_t)((sizeof(struct dylib_command) + strlen(adds[a]) + 1 + 7) & ~7UL);

    /* Build the new table once to learn its size (and print diagnostics). */
    uint8_t *new_lcs = calloc(1, first_sect_off + add_bytes + 64);
    uint32_t new_off, new_ncmds; int modifications;
    build_lcs(buf, changes, nchanges, adds, nadds, strip, nstrip, new_lcs, &new_off, &new_ncmds, &modifications, 1);

    if (modifications == 0) { printf("Nothing to change.\n"); return 0; }

    /* The new table must fit before the first section's data. The boundary is
     * sizeof(mach_header_64) + sizeofcmds; using new_off alone would understate
     * it by the 32-byte header and allow a 16-byte overlap into the section. */
    uint32_t need_end = (uint32_t)sizeof(struct mach_header_64) + new_off;
    if (need_end > first_sect_off) {
        if (!allow_grow) {
            /* Default, unchanged behavior: refuse rather than resize. */
            fprintf(stderr, "ERROR: new LCs (%u bytes) don't fit in header pad (%u avail); "
                            "pass -grow to enlarge it\n", new_off, pad_avail);
            return 1;
        }
        uint32_t grow_req = need_end - first_sect_off;
        printf("Load commands need %u more bytes than the %u-byte pad; growing header...\n",
               grow_req, pad_avail);
        if (mg_grow_header(&buf, &fsize, grow_req) != 0) {
            fprintf(stderr, "ERROR: new LCs (%u bytes) don't fit and header could not be grown\n",
                    new_off);
            return 1;
        }
        hdr = (struct mach_header_64 *)buf;
        first_sect_off = mg_first_sect_off(buf);
        printf("Grew header pad: first sect now at %u (%u bytes available)\n",
               first_sect_off, first_sect_off - cur_lc_end);
        /* Rebuild against the relocated header so segment/linkedit offsets in
         * the copied load commands reflect the shift. */
        free(new_lcs);
        new_lcs = calloc(1, first_sect_off + add_bytes + 64);
        build_lcs(buf, changes, nchanges, adds, nadds, strip, nstrip, new_lcs, &new_off, &new_ncmds, &modifications, 0);
    }

    /* Commit: zero the whole LC area, write the new table, fix up the header. */
    memset(buf + sizeof(struct mach_header_64), 0, first_sect_off - sizeof(struct mach_header_64));
    memcpy(buf + sizeof(struct mach_header_64), new_lcs, new_off);
    hdr->ncmds = new_ncmds;
    hdr->sizeofcmds = new_off;

    if (ftruncate(fd, fsize) != 0) { perror("ftruncate"); return 1; }
    lseek(fd, 0, SEEK_SET);
    if (write(fd, buf, fsize) != (ssize_t)fsize) { perror("write"); return 1; }
    close(fd);
    printf("Updated %s (sizeofcmds=%u, %zu bytes)\n", path, new_off, fsize);
    return 0;
}
