# Publication record: 0.6.0-feel.56 test build

- Published 2026-11-25 as a GitHub pre-release, beside feel.52 (left up):
  https://github.com/HWG90/HellForge-True-First-Person/releases/tag/v0.6.0-feel.56
- The user asked in the feature session ("Let's at least update the discord / git with the latest builds"). The feature
  session relayed it, and the user confirmed in the release session ("Publish all") before anything was published.
- `main` pushed (`fdfb2e1..fdba78f`, 105 commits, all authored "Goose" with the noreply address). These are rounds 25 to 37
  (37 reverted), feel.53 to feel.56, and `fdba78f`, which changes only `docs/guides/TEST-BUILD.md`. That commit retitles the
  notes to feel.56, adds "Changed and fixed in 0.6.0-feel.56" in place of "Changed in the next build", says "For
  HellForge 0.4 and 0.5", and keeps the extract step, which now names HellForge 0.4 as the version that refuses the ZIP whole.
- Tag `v0.6.0-feel.56` (annotated) on `fdba78f`. The manifest and `src/mod_view.c` say 0.6.0-feel.56.
- The DLL in the ZIP is the fresh `TMPDIR=$HOME/.cache bash build.sh --feel` build in the repository's checkout. It matches
  the feature session's tested build: sha256 `32fcd65567d402008ddbb0c975eeb670f50c9607388318683c9e9895a1c12aa1`.
  The installed copy on the test machine is still feel.55 (08f12b29…). feel.56 as a whole has not been played, and the
  notes say so, marking the unplayed items.
- One asset (downloaded back and compared):
  `True-First-Person-0.6.0-feel.56-HellForge-25480438-complete.zip`, sha256
  `2d6aec4e60cc3ae8a68e862ee458683f240a7dc388257cff344dc1f540e8592f`, 54 files, one folder per mod:
  - `true-first-person`: the DLL, manifest, settings, `README-TEST.md` (the same as `TEST-BUILD.md` at the tag),
    LICENSE and NOTICE.
  - `overlay-host`: NEST 0.8.3-tfp.1, as in feel.52.
  - `mcm`: DBF-MCM 0.1.74-hellforge.4, as in feel.52. Both are identical to the installed copies.
- No personal details in any packaged file (checked for the user's name, email and home paths).
- Checks, from the feature session: 69/69 tests/run-*.sh, 172 harness tests, a clean feel build, each round reviewed
  ("merge"). feel.55 was played by the user on HellForge 0.5.
- The HellForge 0.5 multi-mod ZIP (dropped whole into `mods`) is still not confirmed on the user's install, so the
  notes keep "extract".
- Playtest: the pinned issue is renamed "Playtest 1: 0.6.0-feel.56". Its body names feel.56, HellForge 0.4/0.5, U, the
  exosuit and Scope of Authority issues, and adds U, foregrip and HellForge-version items to the checklist. A comment
  says what changed: https://github.com/HWG90/HellForge-True-First-Person/issues/1
- Discord: the "True First Person - Early Preview" post in HellForge Official's `mod-showcase` is edited to feel.56.
  Nothing posted elsewhere.
- Later on 2026-11-25, at the user's request ("Add the version number in the post name so people know when its
  updated"), the Discord post was renamed "True First Person - Early Preview (0.6.0-feel.56)". Each release from now on
  renames it to its own version.
