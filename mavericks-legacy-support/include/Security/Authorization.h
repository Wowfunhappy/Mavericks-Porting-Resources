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

/* Include the primary system Security/Authorization.h (a framework header). */
#include_next <Security/Authorization.h>

/*
  Match the modern macOS SDK's transitive behavior.  On 10.9, whatever pulls in
  <Security/Authorization.h> stops there; newer SDKs also surface the sibling
  <Security/AuthSession.h> (the Security *session* API: SessionGetInfo,
  callerSecuritySession, sessionHasGraphicAccess, ...).  Code that relies on the
  modern SDK (e.g. lldb's Host.mm, which uses those session calls without an
  explicit include) therefore compiles there but not on 10.9.  Both headers
  exist on 10.9 -- AuthSession.h #includes Authorization.h -- so pulling it in
  here reproduces the modern SDK contract.  Header guards make this idempotent
  and the AuthSession.h -> Authorization.h back-edge harmless.
*/
#include <Security/AuthSession.h>
