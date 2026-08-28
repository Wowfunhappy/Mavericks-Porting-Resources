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
 * std::uncaught_exceptions() is defined here rather than taken from the modern
 * archive on purpose. In that archive it lives in the same object file as
 * exception_ptr's copy constructor, assignment and destructor -- so pulling it
 * would drag those in too, and they would override the system libc++'s. Two
 * implementations of exception_ptr refcounting in one process is not a risk
 * worth taking for one function that is three lines long.
 */
extern "C" unsigned int __cxa_uncaught_exceptions();

namespace std {
int uncaught_exceptions() noexcept {
    return static_cast<int>(::__cxa_uncaught_exceptions());
}
}
