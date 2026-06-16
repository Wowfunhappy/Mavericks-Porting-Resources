/*
 * LSCopyDefaultApplicationURLForURL (CoreServices, 10.10+) --
 * custom polyfill (not from macports-legacy-support).
 *
 * Stub returning NULL ("no default application"); callers such as the Rust
 * webbrowser crate fall back to /usr/bin/open.  Left untyped so we don't pull
 * in SDK-specific CoreServices declarations.
 */

void *LSCopyDefaultApplicationURLForURL(void *inURL, unsigned int inRoleMask, void *outError) {
	(void)inURL; (void)inRoleMask;
	if (outError) *(void**)outError = 0;
	return 0;
}
