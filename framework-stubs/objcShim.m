// Hand-written wrapper for /usr/lib/libobjc.A.dylib on OS X 10.9.
//
// The Objective-C runtime helpers this OS's copy predates -- objc_alloc_init,
// objc_opt_new, objc_opt_self, objc_opt_class, objc_opt_isKindOfClass,
// objc_opt_respondsToSelector, objc_unsafeClaimAutoreleasedReturnValue,
// objc_loadClassref -- are all implemented in mavericks-legacy-support
// (src/objc_runtime.c) and linked in from there, so this file only gives the
// wrapper a translation unit.
//
// They used to be implemented here as well. Two definitions of a runtime
// helper is one too many: whichever the linker picked would depend on link
// order, so the polyfill library is the single source of truth and this file
// defines nothing.
