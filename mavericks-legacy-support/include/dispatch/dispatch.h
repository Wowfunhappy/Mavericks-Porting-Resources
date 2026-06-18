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

/* Include the primary system dispatch/dispatch.h */
#include_next <dispatch/dispatch.h>

/*
  dispatch_activate() was added in macOS 10.12.  Before it, a suspended-on-
  creation dispatch source/object was started with a single dispatch_resume().
  For the one-shot activation that callers (e.g. lldb's MemoryMonitorMacOSX)
  perform, dispatch_resume() is the correct 10.9 equivalent. */

#ifndef dispatch_activate
#define dispatch_activate(object) dispatch_resume(object)
#endif
