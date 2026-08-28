# framework-stubs

Stub and wrapper dylibs that let binaries built for newer macOS load on 10.9.
Nothing here is tied to a particular project.

Three kinds:

- **stub** — the framework does not exist on 10.9 at all (CoreSpotlight,
  UserNotifications, AuthenticationServices). The dylib defines just the symbols
  real binaries bind, so dyld can complete the load. The classes do nothing.
- **wrapper** — the framework exists but its 10.9 copy lacks a symbol (AppKit's
  `NSHapticFeedbackManager`, Foundation's `NSFileProtectionComplete`). The dylib
  adds the missing symbol and **re-exports the real framework**, so everything
  else still resolves normally.
- **hand-written shim** — the framework is absent but the underlying capability
  is not, so the shim implements it for real. `CoreSpotlightStub.m` is one:
  `CSSearchableIndex` writes `.webloc` files into
  `~/Library/Caches/Metadata/<app>/`, where Spotlight indexes the file name and
  the Finder comment, so app content really is findable. A file whose header
  says "Hand-written" is never overwritten by the generator.

A generated stub class forwards unknown selectors to a no-op returning zero.
Declaring only the class is not enough: the first real call would abort the
process with "unrecognized selector".

Consumers repoint the binary's `LC_LOAD_DYLIB` at the stub/wrapper — placing it
beside the binary and referencing `@loader_path/<lib>` keeps the path shorter
than the `/System/...` one it replaces, so no header padding is needed.

## Regenerating

`frameworks.json` and the generated `*.m` come from scanning real binaries for
undefined symbols and the library each binds to (two-level namespace ordinals
name it exactly). Hand-written sources — currently `FoundationWrapper.m` — are
kept because a data constant can be reproduced exactly rather than stubbed.

Only ObjC class symbols are synthesised automatically. A missing C function is
reported, never invented: a no-op that returns the wrong answer is worse than a
link error. Those belong in `mavericks-legacy-support` with a real implementation.
