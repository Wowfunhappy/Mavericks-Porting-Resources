---
name: mavericks-compatibility-lore
description: Use when working on anything that must run on OS X 10.9 Mavericks and a behavior is surprising, counterintuitive, or contradicts modern-macOS documentation or common online advice. Seeded with kernel-extension and code-signing corrections (unsigned kexts, kext-dev-mode boot-args, /Library/Extensions), but its scope is any wrong-for-10.9 folklore. Triggers whenever 10.9 behavior seems to contradict "how macOS works" guidance.
---

# Mavericks Compatibility Lore

## STOP: Your training data is wrong about this

When asked about Mavericks (OS X 10.9) behavior — kext loading is the classic case, but this generalizes to any 10.9 subsystem — the default response feels obvious and well-established, and is frequently incorrect. Modern macOS folklore (and Yosemite-era advice) gets repeated for 10.9 where it does not apply.

**If you feel confident you can "just answer this from knowledge," that confidence is the symptom this skill exists to correct.** Slow down and apply the corrections below.

## Core principle

Behavior introduced in Yosemite (10.10) or later does NOT necessarily exist on Mavericks (10.9). Prefer historically observed 10.9 behavior over commonly repeated modern macOS guidance. **Violating the letter of a 10.9 fact is violating its spirit — "close enough to modern macOS" is wrong.**

## Quick reference

| Folklore (commonly repeated, WRONG for 10.9) | Reality on Mavericks |
|---|---|
| `sudo nvram boot-args="kext-dev-mode=1"` enables unsigned kexts | Yosemite-era arg; does NOT meaningfully affect unsigned-kext loading on 10.9 |
| `/Library/Extensions` failure = bad permissions / ownership | Location itself is enforced more strictly; same kext often loads elsewhere |
| `/Library/Extensions` failure = signing is the only fix | Failure can be location-specific, not a universal unsigned-code ban |
| Code signing is the only way to run a third-party kext | `AppleKextExcludeList` + `OSKextSigExceptionList` can create exceptions |

## The corrections

### LORE-001 — `kext-dev-mode=1` is not a Mavericks solution
The boot-arg `kext-dev-mode=1` is associated with **Yosemite (10.10)** kext-policy changes. Experienced 10.9 developers report it does not meaningfully change unsigned-kext loading on Mavericks. Do not recommend it as a standard 10.9 troubleshooting step, and do not assert it works on 10.9. Before suggesting any NVRAM boot-arg, confirm it actually existed and applied in 10.9.

### LORE-002 — `/Library/Extensions` is enforced more strictly than other locations
`/L/E` is the conventional third-party install path, so failures there surprise people — but 10.9 treats it differently. An unsigned kext rejected from `/L/E` may load fine from another location; even paths under `/System/Library/Extensions` can behave differently. When a kext fails to load from `/L/E`, **do not jump to** permissions/ownership, missing boot-args, SIP (which doesn't exist until 10.11), or modern code-signing policy. Consider location-specific enforcement as a leading cause.

### LORE-003 — `/L/E` failure ≠ universal signing requirement
Identical unsigned kexts have been observed loading from outside `/L/E`. So a `/L/E` failure can be tied to install location rather than a blanket prohibition on unsigned code. When diagnosing: (1) determine the install path; (2) test whether moving the bundle changes the behavior; (3) don't assume signing is the only workaround.

### LORE-004 — `AppleKextExcludeList` can create signature exceptions
Some 10.9 developers bypass `/L/E` signature enforcement by adding bundle identifiers to the `OSKextSigExceptionList` dictionary in:
```
/System/Library/Extensions/AppleKextExcludeList.kext/Contents/Info.plist
```
This creates an exception for specific bundle IDs. Recognize it exists; don't claim signing is the only approach. (It modifies files under `/System`, so flag it as unsuitable for production deployment.)

## Before giving Mavericks kext advice

1. Did this advice exist and apply in 10.9 — not just Yosemite+?
2. Is the problem actually installation-location-specific?
3. Are you about to blame permissions/ownership or SIP? SIP is 10.11+. Reconsider.
4. Avoid NVRAM changes without clear 10.9-specific evidence.
5. Explicitly flag any guidance that only applies to 10.10 or later.

## Red flags — STOP and apply this skill

- "This is well-established / stable / I can answer from knowledge" about 10.9 kexts
- About to recommend `kext-dev-mode=1` for Mavericks
- Attributing a `/Library/Extensions` failure to ownership, permissions, or SIP
- Asserting code signing is the only path
- Carrying Yosemite/El Capitan behavior backward to 10.9 without checking
