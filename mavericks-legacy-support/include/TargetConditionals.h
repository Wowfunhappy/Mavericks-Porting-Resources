/*
 * Copyright (c) 2025
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Include the primary system TargetConditionals.h */
#include_next <TargetConditionals.h>

/*
  The 10.9 SDK defines only the original TARGET_OS_* family (TARGET_OS_MAC,
  TARGET_OS_IPHONE, TARGET_OS_EMBEDDED, ...).  The TARGET_OS_OSX spelling and
  the per-platform flags below were added later (10.12+) and are tested
  directly by modern code (e.g. lldb's Host layer), which trips clang's
  -Wundef-prefix.  Derive them from the 10.9 values; everything that is not
  macOS evaluates to 0 on this single-target SDK. */

#ifndef TARGET_OS_OSX
#  if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#    define TARGET_OS_OSX 0
#  else
#    define TARGET_OS_OSX 1
#  endif
#endif

#ifndef TARGET_OS_IOS
#define TARGET_OS_IOS 0
#endif
#ifndef TARGET_OS_WATCH
#define TARGET_OS_WATCH 0
#endif
#ifndef TARGET_OS_TV
#define TARGET_OS_TV 0
#endif
#ifndef TARGET_OS_BRIDGE
#define TARGET_OS_BRIDGE 0
#endif
#ifndef TARGET_OS_MACCATALYST
#define TARGET_OS_MACCATALYST 0
#endif
#ifndef TARGET_OS_SIMULATOR
#define TARGET_OS_SIMULATOR 0
#endif
#ifndef TARGET_OS_VISION
#define TARGET_OS_VISION 0
#endif
#ifndef TARGET_OS_XR
#define TARGET_OS_XR 0
#endif
