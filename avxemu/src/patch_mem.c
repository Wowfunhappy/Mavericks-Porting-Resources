/*
 * patch_mem.c — in-memory lzcnt/tzcnt patcher.
 *
 * lzcnt/tzcnt don't fault on pre-Haswell CPUs — they silently run as bsr/bsf
 * (wrong results). They can't be trap-and-emulated like AVX2, so at load we
 * rewrite each one's F3 prefix to F0 (lock) *in the running process's memory*,
 * making it `lock bsr/bsf` (#UD) so the SIGILL handler emulates the correct
 * lzcnt/tzcnt. The on-disk binary is never modified; this only touches mapped
 * code pages, only on CPUs that lack LZCNT, only where the emulator is present.
 *
 * Instructions are located with a length-aware, function-start-guided,
 * recursive-descent scan (lde.c) so embedded jump-table data is never patched.
 */

#include "lde.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <cpuid.h>

static void emit(const char *s){ (void)write(2, s, strlen(s)); }

static int cpu_has_lzcnt(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(0x80000001u, &a, &b, &c, &d)) return 0;
    return (c >> 5) & 1;                          /* ECX[5] = LZCNT/ABM */
}

static uint64_t uleb(const uint8_t **p, const uint8_t *e) {
    uint64_t r = 0; int s = 0; uint8_t x;
    do { if (*p >= e) return r; x = *(*p)++; r |= (uint64_t)(x & 0x7f) << s; s += 7; } while (x & 0x80);
    return r;
}

/* Patch all lzcnt/tzcnt in the main executable. Returns the number patched. */
long avxemu_patch_lzcnt(void) {
    /* Native lzcnt works -> nothing to do. AVXEMU_FORCEPATCH overrides this so
     * the full parse/patch path can be exercised on an AVX2 dev host (every
     * patched site then traps and is emulated, proving the whole chain). */
    if (cpu_has_lzcnt() && !getenv("AVXEMU_FORCEPATCH")) return 0;

    const struct mach_header_64 *mh = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!mh || mh->magic != MH_MAGIC_64) return 0;
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);

    uint64_t textseg_vm = 0, textseg_sz = 0, text_addr = 0, text_size = 0, le_vm = 0, le_off = 0;
    uint32_t fs_off = 0, fs_size = 0;
    const struct load_command *lc = (const struct load_command *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (!strcmp(sg->segname, "__TEXT")) {
                textseg_vm = sg->vmaddr; textseg_sz = sg->vmsize;
                const struct section_64 *s = (const struct section_64 *)(sg + 1);
                for (uint32_t j = 0; j < sg->nsects; j++)
                    if (!strcmp(s[j].sectname, "__text")) { text_addr = s[j].addr; text_size = s[j].size; }
            } else if (!strcmp(sg->segname, "__LINKEDIT")) { le_vm = sg->vmaddr; le_off = sg->fileoff; }
        } else if (lc->cmd == LC_FUNCTION_STARTS) {
            const struct linkedit_data_command *ld = (const struct linkedit_data_command *)lc;
            fs_off = ld->dataoff; fs_size = ld->datasize;
        }
        lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
    }
    if (!text_addr || !fs_off) return 0;

    uint8_t *text = (uint8_t *)(text_addr + slide);
    size_t readable = (size_t)((textseg_vm + textseg_sz) - text_addr);   /* __text + following __TEXT data */

    /* Make __text writable. A Mach-O __TEXT segment's *maximum* protection is
     * r-x (write is forbidden even as a ceiling), so plain mprotect(PROT_WRITE)
     * is rejected with EACCES. VM_PROT_COPY forces a private copy-on-write of
     * the region and grants write to that copy, the standard way to patch code
     * on macOS. We then restore r-x; execution runs from the patched copy. */
    mach_port_t task = mach_task_self();
    uintptr_t lo = (uintptr_t)text & ~(uintptr_t)0xfff;
    uintptr_t hi = ((uintptr_t)text + text_size + 0xfff) & ~(uintptr_t)0xfff;
    kern_return_t kr = vm_protect(task, (vm_address_t)lo, (vm_size_t)(hi - lo), FALSE,
                                  VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS) {
        emit("avxemu: vm_protect(__text, COPY) failed — lzcnt/tzcnt NOT patched\n");
        return 0;
    }

    const uint8_t *fp = (const uint8_t *)(le_vm + slide + (fs_off - le_off));
    const uint8_t *fe = fp + fs_size;
    uint64_t addr = textseg_vm; long patched = 0; size_t res[256];
    uint64_t prev = 0;
    while (fp < fe) {
        uint64_t delta = uleb(&fp, fe); if (!delta) break;
        addr += delta;
        if (prev) {
            uint64_t fstart = prev, fend = addr;
            if (fstart >= text_addr && fstart < text_addr + text_size) {
                if (fend > text_addr + text_size) fend = text_addr + text_size;
                patched += lde_scan_func(text, fstart - text_addr, fend - text_addr,
                                         readable, 1, res, 256);
            }
        }
        prev = addr;
    }
    if (prev && prev >= text_addr && prev < text_addr + text_size)        /* last function */
        patched += lde_scan_func(text, prev - text_addr, text_size, readable, 1, res, 256);

    /* Linear mop-up: the function-start recursive-descent scan never enters
     * blocks unreachable from a function start (cold paths, tail regions the
     * exports table doesn't bound). A final linear sweep catches the stragglers;
     * sites already patched are now F0-prefixed and are skipped. */
    { long lin = lde_scan_zcnt(text, (size_t)text_size, 1); if (lin > 0) patched += lin; }

    vm_protect(task, (vm_address_t)lo, (vm_size_t)(hi - lo), FALSE,
               VM_PROT_READ | VM_PROT_EXECUTE);
    return patched;
}
