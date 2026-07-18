/*
 * dlopen_interpose.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── dlopen interposer ──────────────────────────────────────────────────────
 *
 * Claude Code embeds native addons (image-processor.node, audio-capture.node,
 * etc.) inside its bunfs virtual filesystem. At process.dlopen time Bun
 * extracts one to a /var/folders/...T/.<hash>-<idx>.node temp file and calls
 * dlopen() on it. Those addons were built against a 10.14+ SDK and two-level-
 * bind symbols like _CCRandomGenerateBytes directly to /usr/lib/libSystem.B
 * .dylib, which on 10.9 does not export them. When dyld's first lazy-bind
 * thunk fires it can't resolve the symbol, calls dyld::halt(), and the whole
 * process dies with SIGILL — reported from Bun as a segfault in
 * Process_functionDlopen:520 (the dlopen call site).
 *
 * Fix: before handing a .node path to the real dlopen, rewrite every
 * LC_LOAD_DYLIB / LC_LOAD_WEAK_DYLIB that targets /usr/lib/libSystem.B.dylib
 * to point at this wrapper instead. The wrapper re-exports libSystem, so all
 * the addon's normal libc bindings still resolve; and our locally-defined
 * stubs (CCRandomGenerateBytes etc.) now satisfy the binds that libSystem
 * alone couldn't.
 *
 * We do the rewrite on a private copy in /tmp, dlopen that, then unlink —
 * dyld has already mapped the image by the time dlopen returns, so the file
 * can go away immediately.
 *
 * This wrapper exports its own strong `dlopen` symbol which shadows the one
 * re-exported from /usr/lib/libSystem.B.dylib; when the re-written main
 * claude binary two-level-binds `_dlopen` against @loader_path/libSystem-
 * Wrapper.dylib it resolves to ours. We reach the system dlopen via a
 * function pointer obtained from libdyld's image at constructor time with
 * NSLookupSymbolInImage (stable 10.4+ API), so calling "the real dlopen"
 * from inside our shim never recurses.
 *
 * __DATA,__interpose would be cleaner but on 10.9's dyld-239 it's only
 * applied for DYLD_INSERT_LIBRARIES images — not regular LC_LOAD_DYLIB
 * dependencies like this wrapper — so an interpose tuple here is a no-op.
 */

static char  g_wrapper_path[PATH_MAX] = {0};
static size_t g_wrapper_path_len      = 0;
static void *(*g_real_dlopen)(const char *, int) = NULL;

static void mav_dlopen_init_real(void) {
    uint32_t n = _dyld_image_count();
    for (uint32_t i = 0; i < n; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name) continue;
        if (!strstr(name, "/libdyld.dylib")) continue;
        const struct mach_header *mh = _dyld_get_image_header(i);
        NSSymbol sym = NSLookupSymbolInImage(mh, "_dlopen",
            NSLOOKUPSYMBOLINIMAGE_OPTION_BIND);
        if (sym) g_real_dlopen = (void *(*)(const char *, int))NSAddressOfSymbol(sym);
        return;
    }
}

__attribute__((constructor))
static void mav_dlopen_init(void) {
    Dl_info info;
    if (dladdr((void *)&mav_dlopen_init, &info) && info.dli_fname) {
        char resolved[PATH_MAX];
        if (realpath(info.dli_fname, resolved)) {
            size_t L = strlen(resolved);
            if (L < sizeof(g_wrapper_path)) {
                memcpy(g_wrapper_path, resolved, L + 1);
                g_wrapper_path_len = L;
            }
        }
    }
    mav_dlopen_init_real();
    DBG("dlopen_init: wrapper=%s real_dlopen=%p",
        g_wrapper_path_len ? g_wrapper_path : "(unknown)", (void *)g_real_dlopen);
}

/* Copy src into a fresh /tmp/claude-mav-...-XXXXXX.node, with every
 * LC_LOAD_DYLIB/LC_LOAD_WEAK_DYLIB/LC_REEXPORT_DYLIB that matches
 * /usr/lib/libSystem.B.dylib rewritten to point at g_wrapper_path. Grows
 * individual LCs into the header pad (space between the last LC and the
 * first section's file data) when the new path is longer than the old.
 *
 * Return: 0 = rewritten temp file written (path in dst), caller dlopens it
 *         1 = no libSystem LC_LOAD_DYLIB found (src is fine as-is)
 *        -1 = error (e.g. not a 64-bit Mach-O, or not enough header pad) */
static int mav_rewrite_node_libsystem(const char *src, char *dst, size_t dstsz) {
    int      rc       = -1;
    int      in_fd    = -1;
    int      out_fd   = -1;
    uint8_t *buf      = NULL;
    uint8_t *new_lcs  = NULL;
    dst[0] = '\0';

    static const char OLD_TARGET[] = "/usr/lib/libSystem.B.dylib";

    in_fd = open(src, O_RDONLY);
    if (in_fd < 0) { DBG("dlopen rewrite: open(%s): %s", src, strerror(errno)); goto done; }
    struct stat st;
    if (fstat(in_fd, &st) < 0) goto done;
    size_t fsize = (size_t)st.st_size;
    if (fsize < sizeof(struct mach_header_64)) goto done;

    buf = malloc(fsize);
    if (!buf) goto done;
    if (read(in_fd, buf, fsize) != (ssize_t)fsize) goto done;
    close(in_fd); in_fd = -1;

    struct mach_header_64 *hdr = (struct mach_header_64 *)buf;
    if (hdr->magic != MH_MAGIC_64) { DBG("dlopen rewrite: %s not MH_MAGIC_64", src); goto done; }

    /* First section offset bounds the header pad. */
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

    new_lcs = calloc(1, first_sect_off);
    if (!new_lcs) goto done;

    uint32_t new_off   = 0;
    int      modified  = 0;
    size_t   new_len   = g_wrapper_path_len + 1;  /* include NUL */

    lcp = buf + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        struct load_command *lc = (struct load_command *)lcp;
        uint32_t cmdsize   = lc->cmdsize;
        uint32_t write_size = cmdsize;
        int      match      = 0;

        if (lc->cmd == LC_LOAD_DYLIB ||
            lc->cmd == LC_LOAD_WEAK_DYLIB ||
            lc->cmd == LC_REEXPORT_DYLIB) {
            struct dylib_command *dc = (struct dylib_command *)lcp;
            const char *name = (const char *)lcp + dc->dylib.name.offset;
            if (strcmp(name, OLD_TARGET) == 0) {
                match = 1;
                size_t base = dc->dylib.name.offset;
                uint32_t needed = (uint32_t)((base + new_len + 7) & ~7UL);
                if (needed < cmdsize) needed = cmdsize;
                write_size = needed;
            }
        }

        if (new_off + write_size > first_sect_off) {
            DBG("dlopen rewrite: %s LCs overflow pad (need %u, have %u)",
                src, new_off + write_size, first_sect_off);
            goto done;
        }

        memcpy(new_lcs + new_off, lcp, cmdsize);
        if (match) {
            struct dylib_command *ndc = (struct dylib_command *)(new_lcs + new_off);
            ndc->cmdsize = write_size;
            size_t base = ndc->dylib.name.offset;
            memset(new_lcs + new_off + base, 0, write_size - base);
            memcpy(new_lcs + new_off + base, g_wrapper_path, g_wrapper_path_len);
            modified++;
        }
        new_off += write_size;
        lcp += cmdsize;
    }

    if (!modified) { rc = 1; goto done; }

    /* Splice the rebuilt LC block back into buf. */
    memset(buf + sizeof(struct mach_header_64), 0,
           first_sect_off - sizeof(struct mach_header_64));
    memcpy(buf + sizeof(struct mach_header_64), new_lcs, new_off);
    hdr->sizeofcmds = new_off;

    snprintf(dst, dstsz, "/tmp/claude-mav-dlopen-%d-XXXXXX.node", (int)getpid());
    out_fd = mkstemps(dst, 5);
    if (out_fd < 0) { DBG("dlopen rewrite: mkstemps: %s", strerror(errno)); goto done; }
    if (write(out_fd, buf, fsize) != (ssize_t)fsize) goto done;
    close(out_fd); out_fd = -1;

    DBG("dlopen rewrite: %s → %s (%d LCs repointed to %s)",
        src, dst, modified, g_wrapper_path);
    rc = 0;
done:
    if (in_fd  >= 0) close(in_fd);
    if (out_fd >= 0) close(out_fd);
    free(buf);
    free(new_lcs);
    if (rc == -1 && dst[0]) { unlink(dst); dst[0] = '\0'; }
    return rc;
}

void *dlopen(const char *path, int mode) {
    /* Lazily cache real_dlopen if the constructor hasn't run yet (some early
     * dlopens happen during libSystem bootstrap before our ctor fires). */
    if (!g_real_dlopen) mav_dlopen_init_real();
    if (!g_real_dlopen) return NULL;

    DBG("dlopen(%s, 0x%x)", path ? path : "(null)", mode);
    if (!path) return g_real_dlopen(path, mode);

    size_t pl = strlen(path);
    if (pl < 5 || memcmp(path + pl - 5, ".node", 5) != 0)
        return g_real_dlopen(path, mode);

    if (g_wrapper_path_len == 0) {
        DBG("dlopen: wrapper path unknown, passing %s through", path);
        return g_real_dlopen(path, mode);
    }

    char tmp[PATH_MAX];
    int rc = mav_rewrite_node_libsystem(path, tmp, sizeof(tmp));
    if (rc == 1) return g_real_dlopen(path, mode);
    if (rc != 0) {
        /* Rewrite failed — best effort, pass original through. dyld may halt,
         * but we've already done all we usefully can at this layer. */
        return g_real_dlopen(path, mode);
    }

    void *h = g_real_dlopen(tmp, mode);
    unlink(tmp);
    return h;
}

