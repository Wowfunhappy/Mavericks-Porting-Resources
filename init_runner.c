/*
 * Run the main executable's __TEXT,__init_offsets initializers.
 * dyld in macOS 10.9 doesn't know about __init_offsets (a dyld4-era
 * format that replaced __mod_init_func with 4-byte offsets from the
 * image base instead of full pointers). C++ static constructors live
 * there in modern binaries; without them, globals like .NET's
 * CObjectType otThread(...) get zero-initialized and PAL setup
 * dereferences a NULL m_pProcessLocalDataSize-driven pointer.
 *
 * This constructor in libSystemWrapper runs before main, finds the
 * main image, locates its __init_offsets, and invokes each entry.
 */
#include <stdint.h>
#include <string.h>
#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <objc/objc.h>
#include <objc/runtime.h>

__attribute__((constructor(101)))
static void run_init_offsets(void) {
    /* Image 0 is the main executable. */
    const struct mach_header *mh = _dyld_get_image_header(0);
    if (!mh || (mh->magic != MH_MAGIC_64 && mh->magic != MH_MAGIC)) return;

    intptr_t slide = _dyld_get_image_vmaddr_slide(0);

    if (mh->magic != MH_MAGIC_64) return;
    const struct mach_header_64 *hdr = (const struct mach_header_64 *)mh;

    const uint8_t *lcp = (const uint8_t *)(hdr + 1);
    uint64_t text_vmaddr = 0;
    int found_text = 0;

    /* First pass: find __TEXT vmaddr. */
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)lc;
            if (strcmp(seg->segname, "__TEXT") == 0) {
                text_vmaddr = seg->vmaddr;
                found_text = 1;
                break;
            }
        }
        lcp += lc->cmdsize;
    }
    if (!found_text) return;

    /* Second pass: find __init_offsets and __objc_selrefs.
     *
     * 10.9's libobjc doesn't pre-process modern (dyld4-era) binaries it
     * sees mapped — the binary's __objc_selrefs entries stay as raw
     * c-string pointers instead of being rewritten to runtime SELs, so
     * objc_msgSend ends up dispatching with a string ptr and the runtime
     * complains about a "selector does not match" mismatch.
     *
     * We walk __objc_selrefs ourselves and replace each entry with
     * sel_registerName(string). */
    lcp = (const uint8_t *)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)lcp;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)lc;
            const struct section_64 *sect =
                (const struct section_64 *)((const uint8_t *)seg + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if (strcmp(sect[j].sectname, "__objc_selrefs") == 0) {
                    SEL *selrefs = (SEL *)(sect[j].addr + slide);
                    uint64_t count = sect[j].size / sizeof(SEL);
                    for (uint64_t k = 0; k < count; k++) {
                        const char *name = (const char *)selrefs[k];
                        if (name) selrefs[k] = sel_registerName(name);
                    }
                } else if (strcmp(sect[j].sectname, "__init_offsets") == 0) {
                    const uint32_t *offsets =
                        (const uint32_t *)(sect[j].addr + slide);
                    uint64_t count = sect[j].size / 4;
                    for (uint64_t k = 0; k < count; k++) {
                        void (*fn)(void) =
                            (void (*)(void))(text_vmaddr + offsets[k] + slide);
                        fn();
                    }
                }
            }
        }
        lcp += lc->cmdsize;
    }
}
