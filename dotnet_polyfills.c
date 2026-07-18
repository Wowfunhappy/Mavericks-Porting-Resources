/*
 * Polyfills required by .NET 9 NativeAOT / SingleFile binaries on macOS 10.9.
 *
 * These belong in mavericks-legacy-support (the single source of truth for
 * 10.9 libSystem polyfills; see its src/*.c) — fold them in there when a .NET
 * target needs them, rather than keeping this as a separate inline shim. Until
 * then they can also be built into libSystemWrapper or a separate dylib
 * reachable via the binary's libSystem-rewritten LC_LOAD_DYLIB.
 *
 * The interposes here are the ones whose presence/behavior on 10.10+ vs 10.9
 * specifically gate .NET startup. They are individually small but failure of
 * any one causes a cascade of opaque crashes deep inside coreclr or the GC.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_statistics.h>

/* sysconf(_SC_PHYS_PAGES = 200) — Linux-only on stock macOS. .NET's
 * GCToOSInterface::Initialize reads it to size the GC heap and aborts on -1.
 * Compute it from hw.memsize / page size. */
typedef long (*sysconf_fn)(int);
long sysconf_wrap(int name) __asm("_sysconf");
long sysconf_wrap(int name) {
    static sysconf_fn real = NULL;
    if (!real) {
        void *h = dlopen("/usr/lib/system/libsystem_c.dylib", RTLD_LAZY);
        if (h) real = (sysconf_fn)dlsym(h, "sysconf");
    }
    if (name == 200 /* _SC_PHYS_PAGES */) {
        uint64_t memsize = 0;
        size_t sz = sizeof(memsize);
        if (sysctlbyname("hw.memsize", &memsize, &sz, NULL, 0) == 0) {
            long page = real ? real(29 /* _SC_PAGESIZE */) : 4096;
            if (page <= 0) page = 4096;
            return (long)(memsize / page);
        }
        return -1;
    }
    return real ? real(name) : -1;
}

/* sysctlnametomib for sysctl names 10.9 doesn't have but .NET probes for
 * (e.g. "kern.memorystatus_level"). Synthesize a sentinel MIB; sysctl_wrap
 * below recognizes it and returns a synthetic reading. */
typedef int (*sysctlnametomib_fn)(const char *, int *, size_t *);
int sysctlnametomib_wrap(const char *name, int *mibp, size_t *sizep)
    __asm("_sysctlnametomib");
int sysctlnametomib_wrap(const char *name, int *mibp, size_t *sizep) {
    static sysctlnametomib_fn real = NULL;
    if (!real) {
        void *h = dlopen("/usr/lib/system/libsystem_c.dylib", RTLD_LAZY);
        if (h) real = (sysctlnametomib_fn)dlsym(h, "sysctlnametomib");
    }
    if (real) {
        int r = real(name, mibp, sizep);
        if (r == 0) return 0;
    }
    if (name && strcmp(name, "kern.memorystatus_level") == 0) {
        if (!sizep) return -1;
        if (mibp == NULL) { *sizep = 4; return 0; }   /* probe call */
        if (*sizep >= 4) {
            mibp[0] = 0x4d4f4d54;  /* 'TMOM' marker */
            mibp[1] = 1; mibp[2] = 2; mibp[3] = 3;
            *sizep = 4;
            return 0;
        }
    }
    return -1;
}

typedef int (*sysctl_fn)(int *, unsigned int, void *, size_t *, void *, size_t);
int sysctl_wrap(int *name, unsigned int namelen, void *oldp, size_t *oldlenp,
                void *newp, size_t newlen) __asm("_sysctl");
int sysctl_wrap(int *name, unsigned int namelen, void *oldp, size_t *oldlenp,
                void *newp, size_t newlen) {
    static sysctl_fn real = NULL;
    if (!real) {
        void *h = dlopen("/usr/lib/system/libsystem_c.dylib", RTLD_LAZY);
        if (h) real = (sysctl_fn)dlsym(h, "sysctl");
    }
    if (namelen >= 1 && name && name[0] == 0x4d4f4d54) {
        /* Synthetic kern.memorystatus_level — report 100 (no pressure). */
        if (oldp && oldlenp && *oldlenp >= sizeof(int)) {
            *(int *)oldp = 100;
            *oldlenp = sizeof(int);
        }
        return 0;
    }
    return real ? real(name, namelen, oldp, oldlenp, newp, newlen) : -1;
}

/* vm_remap on 10.9 rejects VM_FLAGS_RANDOM_ADDR (0x8) with KERN_INVALID_ARGUMENT.
 * .NET's W^X JIT setup passes VM_FLAGS_ANYWHERE | VM_FLAGS_RANDOM_ADDR (0x9);
 * the whole MapRW dual-mapping fails and the runtime hits its fatal error
 * handler. Strip the unsupported flag, then widen max_protection so the
 * caller's follow-up mprotect(RW) succeeds — 10.9 inherits a stricter
 * max_protection than 10.10+ for vm_remap'd regions. */
typedef kern_return_t (*vm_remap_fn)(
    vm_map_t, vm_address_t *, vm_size_t, vm_address_t, int,
    vm_map_t, vm_address_t, boolean_t,
    vm_prot_t *, vm_prot_t *, vm_inherit_t);

kern_return_t vm_remap_wrap(
    vm_map_t target_task, vm_address_t *target_address,
    vm_size_t size, vm_address_t mask, int flags,
    vm_map_t src_task, vm_address_t src_address, boolean_t copy,
    vm_prot_t *cur_protection, vm_prot_t *max_protection,
    vm_inherit_t inheritance) __asm("_vm_remap");
kern_return_t vm_remap_wrap(
    vm_map_t target_task, vm_address_t *target_address,
    vm_size_t size, vm_address_t mask, int flags,
    vm_map_t src_task, vm_address_t src_address, boolean_t copy,
    vm_prot_t *cur_protection, vm_prot_t *max_protection,
    vm_inherit_t inheritance)
{
    static vm_remap_fn real = NULL;
    if (!real) real = (vm_remap_fn)dlsym(RTLD_NEXT, "vm_remap");
    if (!real) return KERN_FAILURE;

    int safe_flags = flags & 0x1;     /* keep VM_FLAGS_ANYWHERE only */
    if (safe_flags & 0x1) *target_address = 0;

    kern_return_t kr = real(target_task, target_address, size, mask,
                             safe_flags, src_task, src_address, copy,
                             cur_protection, max_protection, inheritance);
    if (kr != KERN_SUCCESS) return kr;

    vm_protect(target_task, *target_address, size, TRUE,
               VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE);
    vm_protect(target_task, *target_address, size, FALSE,
               VM_PROT_READ | VM_PROT_WRITE);
    return KERN_SUCCESS;
}

/* preadv/pwritev — added in 10.10. Emulate via pread/pwrite. The $NOCANCEL
 * variants are in mavericks-legacy-support (src/preadv_pwritev_nocancel.c);
 * this covers the plain symbols. */
#include <sys/uio.h>
ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = pread(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (n < 0) return (total > 0) ? total : -1;
        total += n;
        offset += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}
ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        ssize_t n = pwrite(fd, iov[i].iov_base, iov[i].iov_len, offset);
        if (n < 0) return (total > 0) ? total : -1;
        total += n;
        offset += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}
