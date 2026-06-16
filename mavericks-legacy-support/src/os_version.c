/*
 * @available() support -- custom polyfill (not from macports-legacy-support).
 *
 * __isOSVersionAtLeast / __isPlatformVersionAtLeast are compiler-rt builtins
 * that don't exist on 10.9.  Without them, @available(macOS 10.15, *) calls
 * would crash.  We implement them to report the actual OS version so code
 * correctly skips paths for newer APIs.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/sysctl.h>

static int cached_major = 0, cached_minor = 0, cached_patch = 0;

static void ensure_os_version(void) {
	if (cached_major) return;
	char str[64];
	size_t len = sizeof(str);
	if (sysctlbyname("kern.osproductversion", str, &len, NULL, 0) == 0) {
		sscanf(str, "%d.%d.%d", &cached_major, &cached_minor, &cached_patch);
	}
	if (!cached_major) {
		/* Fallback: we know we're on 10.9 */
		cached_major = 10; cached_minor = 9; cached_patch = 5;
	}
}

int32_t __isOSVersionAtLeast(int32_t major, int32_t minor, int32_t patch) {
	ensure_os_version();
	if (cached_major != major) return cached_major > major;
	if (cached_minor != minor) return cached_minor > minor;
	return cached_patch >= patch;
}

/* Newer Clang uses this variant (platform 1 = macOS) */
int32_t __isPlatformVersionAtLeast(uint32_t platform, uint32_t major,
                                    uint32_t minor, uint32_t patch) {
	(void)platform; /* Assume macOS */
	return __isOSVersionAtLeast((int32_t)major, (int32_t)minor, (int32_t)patch);
}
