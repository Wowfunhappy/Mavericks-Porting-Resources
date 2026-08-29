/*
 * Wrapper for /usr/lib/libc++.1.dylib on OS X 10.9.
 *
 * 10.9's libc++ predates C++17, so it has no std::filesystem, no
 * shared_timed_mutex, and no bad_optional_access / bad_variant_access. Rather
 * than reimplement those, the build pulls exactly the needed objects out of a
 * modern libc++ static archive that was itself built for a 10.9 deployment
 * target, and re-exports the system library for everything else.
 *
 * Taking only the missing objects, rather than shipping the whole modern
 * dylib, is deliberate: two libc++ images in one process means two copies of
 * the type_info for every standard type, and exceptions stop matching their
 * catch clauses across the boundary.
 */

#include <cstddef>
#include <new>

/*
 * Sized deallocation (C++14). The modern objects call these; 10.9's libc++abi
 * exports only the unsized forms. Forwarding is exactly what the standard
 * permits -- the size is an optimisation hint, and ignoring it is conformant.
 * Without them the filesystem objects will not link at all.
 */
void operator delete(void *p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void *p, std::size_t) noexcept { ::operator delete[](p); }

/*
 * __cxa_uncaught_exceptions (plural, C++17) returns how many exceptions are
 * currently in flight. 10.9's libc++abi has only the singular predicate, but
 * it does export __cxa_get_globals, and the count is a field of that
 * structure -- so this is the exact value, not an approximation of it.
 *
 * The layout below is libc++abi's own and has not changed: the caught-
 * exception chain followed by the uncaught count.
 */
namespace __cxxabiv1 {
struct __cxa_eh_globals {
    void *caughtExceptions;
    unsigned int uncaughtExceptions;
};
extern "C" __cxa_eh_globals *__cxa_get_globals();
}

extern "C" unsigned int __cxa_uncaught_exceptions() {
    __cxxabiv1::__cxa_eh_globals *g = __cxxabiv1::__cxa_get_globals();
    return g ? g->uncaughtExceptions : 0;
}

/*
 * std::uncaught_exceptions() is NOT defined here.
 *
 * It comes from the archive's exception.cpp, which gets linked in anyway:
 * asking for std::filesystem pulls string.cpp, system_error.cpp, memory.cpp
 * and their dependencies, and exception.cpp arrives with them. Defining it
 * here as well is a duplicate-symbol link error.
 *
 * That transitive pull is also why this library exports an explicit symbol
 * list (see "restrict_exports" in frameworks.json). Without it the wrapper
 * would export its own std::string, std::system_error and friends, and every
 * consumer binding through it would get those instead of the system libc++'s
 * -- two implementations of the same standard types in one process, which is
 * exactly what this wrapper exists to avoid. The list keeps the additions
 * visible and everything else private.
 */

/*
 * __cxa_init_primary_exception is how a modern libc++abi builds an
 * exception_ptr without throwing. 10.9's libc++abi predates it, and pulling
 * future.cpp out of the modern archive (for std::__assoc_sub_state) drags in a
 * reference to it from std::promise's destructor.
 *
 * It aborts rather than being implemented. Building a __cxa_exception by hand
 * means reproducing that header's layout exactly, and getting it subtly wrong
 * would corrupt exception handling everywhere -- a far worse failure than
 * stopping at the one call site that needs it. Nothing reaches it here: no
 * binary in the app imports this symbol, and the only path to it is the
 * broken-promise case inside the copy of future.cpp pulled in for an unrelated
 * symbol.
 *
 * If a consumer ever does reach this, the fix is a real implementation, not a
 * quieter stub.
 */
#include <cstdio>
#include <cstdlib>

namespace std { class type_info; }

extern "C" void *__cxa_init_primary_exception(void *object,
                                              std::type_info *tinfo,
                                              void (*dest)(void *)) {
    (void)object; (void)tinfo; (void)dest;
    std::fprintf(stderr,
        "__cxa_init_primary_exception is not implemented on this OS; "
        "a modern libc++ exception_ptr was constructed without throwing.\n");
    std::abort();
}
