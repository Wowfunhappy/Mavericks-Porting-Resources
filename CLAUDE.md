# Working in this repo

This repo banks reusable resources for getting modern-SDK macOS binaries to
run on Mac OS X 10.9 Mavericks (see `Readme.txt`). It is meant to be handed to
Claude as a starting point for porting work.

## Skills

Two skills live under `skills/` (top-level, so not auto-discovered — load them
when relevant):

- `skills/mavericks-porting-toolchain/SKILL.md` — symptom→resource index for
  the Mach-O patchers, wrappers, polyfills, recipes, and AVX emulator here.
  Use it when making a modern binary launch/run on 10.9.
- `skills/mavericks-compatibility-lore/SKILL.md` — corrections to
  widely-repeated-but-wrong 10.9 folklore (kexts, code signing). Use it before
  giving any "this works on macOS" advice that must hold on 10.9.

## Maintenance rules

**Keep the toolchain skill in sync with the tree.** When you add, change,
remove, or rename a tool / recipe / wrapper / source file here, update
`skills/mavericks-porting-toolchain/SKILL.md` in the same change — its symptom
map and inventory are only useful while they match what's actually in the repo.
A toolchain change that leaves the skill stale is incomplete. When you finish
porting work and identify something reusable, save it back here (that's this
repo's whole purpose) and reflect it in the skill.

**Capture surprising Mavericks behavior as lore.** Whenever you discover
something on 10.9 that is unexpected, counterintuitive, or contradicts the
documentation or common online advice, add it to
`skills/mavericks-compatibility-lore/SKILL.md`. The point of that skill is to
stop a future Claude from confidently repeating advice that is wrong for 10.9 —
so every hard-won "actually, on Mavericks it does X" belongs there, with enough
context to recognize when it applies.
