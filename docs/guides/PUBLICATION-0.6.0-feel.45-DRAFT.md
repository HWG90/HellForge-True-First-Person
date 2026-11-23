# Publication draft: 0.6.0-feel.45 test build (NOT published)

Nothing here has been pushed or uploaded. GitHub still has 0.6.0-feel.19 as the newest pre-release. This file lists
what a publication of feel.45 would consist of, so it can be done in one go once it is wanted.

- Branch `main`, local only; the commits since `v0.6.0-feel.19` are not on `origin`.
- Tag to create: `v0.6.0-feel.45` on the commit that holds this file's final form.
- Assets, in `work/release/` (not in git):
  - `True-First-Person-0.6.0-feel.45-HellForge-25480438-complete.zip`, sha256 `47aee36fad3cfdaab9f539ccd41c547867f20e1d281a0c2cac487fc83f7a5625`
  - `True-First-Person-0.6.0-feel.45-HellForge-25480438-test.zip`, sha256 `835f9092bee1a34245c2985825d3209583d2a607b477d2bf2df98f7d8af7b5e2`
- The complete ZIP holds the mod and NEST 0.8.3-tfp.1 (the `overlay-host` folder, byte for byte the one in the
  feel.31 complete ZIP). DBF-MCM is no longer in it: the mod has its own panel.
- Release text: `docs/guides/TEST-BUILD.md` (it is `README-TEST.md` inside the ZIPs).
- Pre-release, like feel.18 and feel.19, which stay up.

## What to say first

First person now stays on through every action, the arms are animated, the field of view is one value, and the mod
has its own settings panel (F10). Tested in solo missions on game build 25480438; not tested in multiplayer, in
vehicles, at terminals or on ladders.

## Before publishing

- feel.34 was played by hand on 2026-11-22; what that found is fixed in feel.45, which itself was checked by scripted input and captures only.
- Decide whether the hashes above are still the files to upload (they change with any rebuild).
