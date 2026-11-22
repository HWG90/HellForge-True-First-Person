# Publication draft: 0.6.0-feel.34 test build (NOT published)

Nothing here has been pushed or uploaded. GitHub still has 0.6.0-feel.19 as the newest pre-release. This file lists
what a publication of feel.34 would consist of, so it can be done in one go once it is wanted.

- Branch `main`, local only; the commits since `v0.6.0-feel.19` are not on `origin`.
- Tag to create: `v0.6.0-feel.34` on the commit that holds this file's final form.
- Assets, in `work/release/` (not in git):
  - `True-First-Person-0.6.0-feel.34-HellForge-25480438-complete.zip`, sha256 `fe117708dc50cf3b569d23a0188dc9d0b59443dbac0b0959d36c6fd42adb6751`
  - `True-First-Person-0.6.0-feel.34-HellForge-25480438-test.zip`, sha256 `2a16e4ff592e30ce05318924dd122f0638547e77ff2a64c4579e099375835c36`
- The complete ZIP holds the mod and NEST 0.8.3-tfp.1 (the `overlay-host` folder, byte for byte the one in the
  feel.31 complete ZIP). DBF-MCM is no longer in it: the mod has its own panel.
- Release text: `docs/guides/TEST-BUILD.md` (it is `README-TEST.md` inside the ZIPs).
- Pre-release, like feel.18 and feel.19, which stay up.

## What to say first

First person now stays on through every action, the arms are animated, the field of view is one value, and the mod
has its own settings panel (F10). Tested in solo missions on game build 25480438; not tested in multiplayer, in
vehicles, at terminals or on ladders.

## Before publishing

- Play it once by hand. Everything in the notes was checked by scripted input and captures, not by a person playing.
- Decide whether the hashes above are still the files to upload (they change with any rebuild).
