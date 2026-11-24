# Publication record: 0.6.0-feel.51 test build

- Published 2026-11-24 as a GitHub pre-release, beside feel.50 (left up, as feel.18 and feel.19 are):
  https://github.com/HWG90/HellForge-True-First-Person/releases/tag/v0.6.0-feel.51
- Asked for by the user in the feature session ("upload all changes to github immediately, and update the
  pre-release as well, then update the post on discord", after the reviewed fixes were in); confirmed by the user in
  the release session before anything was published, with DBF-MCM chosen for the complete ZIP.
- `main` pushed (`7744f1a..1a1659e`): the feel.51 commit `cc0a95f`, its test notes `383ddb1`, and `1a1659e`, which
  changes only `docs/guides/TEST-BUILD.md` (Install and Use: the complete ZIP carries DBF-MCM again; F10 opens its
  page, or the own panel without it). All commits authored "Goose" with the noreply address.
- Tag `v0.6.0-feel.51` (annotated) on `1a1659e`. The manifest and `src/mod_view.c` say 0.6.0-feel.51.
- The DLL in both ZIPs, a fresh `TMPDIR=$HOME/.cache bash build.sh --feel` in the repository's checkout at
  `383ddb1` and again at `1a1659e`, and the copy installed and tested on the test machine are byte-identical: sha256
  `8c75b8d02ca459f2d4579f87b234709114b01ac6c0f57b23a11799e7f058ad5e`. The installed `hellforge-mod.json` and
  `settings.json` are identical to `dist/` too.
- Assets (both downloaded back and compared):
  - `True-First-Person-0.6.0-feel.51-HellForge-25480438-complete.zip`, sha256 `c5ceeb8b24f34527dc9bb38c9657bd31fecd3bca81c92bffe4dfac1fe8ac9afd`
  - `True-First-Person-0.6.0-feel.51-HellForge-25480438-test.zip`, sha256 `921b125c5736a69d458d56af61edcde514ccab025e0f3c008843cbd1913d96e3`
- Layout as feel.50: the test ZIP holds the mod's six files at its root; the complete ZIP has one folder per mod.
  The complete ZIP holds `true-first-person`, `overlay-host` (NEST 0.8.3-tfp.1, byte for byte the feel.50 package's
  copy and the one installed, apart from the user's own `imgui.ini`) and `mcm` (DBF-MCM 0.1.74-hellforge.3, file for
  file the feel.19 package's copy and the one installed beside the mod when it was tested; `MCM_API_VERSION` 1 on both
  sides). DBF-MCM is back because feel.51 puts its full menu in DBF-MCM when it is loaded. It was not installed into an
  empty HellForge.
- No personal details in any packaged file (checked for the user's name, email and home paths).
- Release text: `docs/guides/TEST-BUILD.md` as at the tag (also `README-TEST.md` in both ZIPs, identical).
- Evidence of the game runs, from the feature session (on the test machine):
  - Stable soak, seed 15: `~/.cache/tfp-feel51s/soak-stable-15.out`: 337 actions, 23,554 frames held, none missed,
    35 flagged, largest step 0.442 m, no crash.
  - The 35-phase matrix in Stable: `~/.cache/tfp-feel51s/matrix.out`: 31 of 35 passed (failed: prone, dive,
    grenade-held, stratagem-menu).
  - Bodycam soak `~/.cache/tfp-feel51/soak-bodycam-14.out`; screenshots `~/.cache/tfp-feel51/*.png`.
  - A comparison with feel.50 on the same seed found no regression from the merged code; verdict "publish with known
    issues".
- Playtest: the pinned issue is renamed "Playtest 1: 0.6.0-feel.51"; its text now names feel.51, the three-folder
  complete ZIP, DBF-MCM or the own panel, wheel-only depth in the editor and feel.51's known issues, and a comment
  says what changed: https://github.com/HWG90/HellForge-True-First-Person/issues/1
- Discord: the "True First Person - Early Preview" post in HellForge Official's `mod-showcase` (the user renamed it)
  is edited to feel.51 (version, F10 with DBF-MCM or the own panel, what is new, the three known issues, wheel-only
  depth, remove the older test mod's MCM page, the complete ZIP's contents, feel.51 link):
  https://discord.com/channels/1555635878775361606/1574781605258469456
  Nothing posted elsewhere (no Nexus or HellForge listing).
- Later on 2026-11-24 the user asked for "only one zip" after a player in the Discord thread asked which of the
  two was the right one. The `-test.zip` asset was deleted from the release; only the complete ZIP (sha256 above,
  unchanged) is left. The release text, the playtest issue and `TEST-BUILD.md` no longer mention a second ZIP. The
  `README-TEST.md` inside the complete ZIP still names the `-test.zip` once (the ZIP was left byte for byte as
  published). From now on a release has one ZIP.
- Also on 2026-11-24, at the user's request: a player had put the complete ZIP itself into `mods`, and the launcher
  refused it ("no hellforge-mod.json ... in its one top-level folder"). HellForge takes each archive in `mods` as one
  mod and has no form for several mods in one archive. The release text, the playtest issue and `TEST-BUILD.md` now say
  to extract the ZIP and not put the ZIP itself in `mods`.
