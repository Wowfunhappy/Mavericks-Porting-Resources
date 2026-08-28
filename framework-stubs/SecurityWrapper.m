// Hand-written wrapper for Security.framework on OS X 10.9.
//
// Every symbol it adds is implemented in mavericks-legacy-support (src/
// security.c) and linked in from there, so this file only exists to give the
// wrapper a translation unit. Adding a Security polyfill means editing that
// file, not this one -- keeping trust-evaluation code in one place matters
// more here than in most of these wrappers.
