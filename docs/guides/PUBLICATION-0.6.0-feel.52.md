# Publication record: 0.6.0-feel.52 test build

- Published 2026-11-24 as a GitHub pre-release, beside feel.51 (left up):
  https://github.com/HWG90/HellForge-True-First-Person/releases/tag/v0.6.0-feel.52
- Asked for by the feature session under the user's standing request (upload, update the pre-release and the
  Discord post once reviewed fixes are in); confirmed by the user in the release session before anything was
  published ("Publish all"), with DBF-MCM 0.1.74-hellforge.4 chosen for the ZIP.
- `main` pushed (`1007335..1169427`, 101 commits, all authored "Goose" with the noreply address): rounds 17 to 24, the
  feel.52 version commit `4ea830f`, its test notes `f22e50f`, and `1169427`, which changes only
  `docs/guides/TEST-BUILD.md`: DBF-MCM 0.1.74-hellforge.4 in Install; Camera style described as a selector (the
  section still described the old Bodycam checkbox); a known issue for Enter on a choice row in DBF-MCM.
- Tag `v0.6.0-feel.52` (annotated) on `1169427`. The manifest and `src/mod_view.c` say 0.6.0-feel.52.
- The DLL in the ZIP, a fresh `TMPDIR=$HOME/.cache bash build.sh --feel` in the repository's checkout, and the copy
  installed and tested are byte-identical: sha256 `bf6ab599024c3fbab200110269f0140e1232be54bfd6e6083429590d8a918447`.
  The installed `hellforge-mod.json` and `settings.json` are identical to `dist/`.
- One asset (downloaded back and compared):
  `True-First-Person-0.6.0-feel.52-HellForge-25480438-complete.zip`, sha256
  `bf38ad2f0d65c4353b50988d233519c5093cc5e8680825284c2a8c8bfd07fba0`, 54 files, one folder per mod:
  - `true-first-person`: the DLL, manifest, settings, `README-TEST.md` (= `TEST-BUILD.md` at the tag), LICENSE, NOTICE.
  - `overlay-host`: NEST 0.8.3-tfp.1, byte for byte the feel.51 ZIP's copy and the installed one (without `imgui.ini`).
  - `mcm`: DBF-MCM 0.1.74-hellforge.4, the `mcm/` folder of
    `dbf-mcm/.worktrees/megapack-options/hellforge/dist/DBF-MCM-0.1.74-hellforge.4.zip` (branch
    `indev/mcm-megapack-options`, not merged into DBF-MCM's `hellforge` branch). Its DLL, manifest and sources are
    identical to the installed copy (dll sha256 a6f4ea8d…) that feel.52 was tested with. This ZIP is the first public
    copy of .4. It does not hold the round 21 MCM changes (Enter steps a choice; scroll logging; branch
    `indev/round21-choice-and-scroll`).
- No personal details in any packaged file (checked for the user's name, email and home paths).
- Evidence of the game runs, from the feature session (on the test machine):
  - Stable: `~/.cache/tfp-feel52s/`: matrix 34 of 37 (failed prone 0.067 m, dive 0.073 m, jump-vault 0.088 m; jump-vault
    really vaulted this time, which feel.51's run never did), soak seed 15 (315 actions).
  - Bodycam: `~/.cache/tfp-feel52/soak-bodycam-14.out` (329 actions, largest eye jump 0.155 m against feel.51's 0.456 m).
    The rest of `~/.cache/tfp-feel52/` ran with the view off and is invalid.
  - Two analysts under one judge (`~/.cache/tfp-round25/result-*.md`): "publish with known issues".
- Playtest: the pinned issue is renamed "Playtest 1: 0.6.0-feel.52"; its text names feel.52, DBF-MCM .4, Camera style as a
  selector, clambers and pick-ups, the Aim field of view, the editor in aim, `warp_hide.txt` and feel.52's known
  issues; a comment says what changed: https://github.com/HWG90/HellForge-True-First-Person/issues/1
- Discord: the "True First Person - Early Preview" post in HellForge Official's `mod-showcase` is edited to feel.52
  (what is new, what to try: Aim field of view and `warp_hide.txt`, the known issues, the editor in aim, extract the
  ZIP, the feel.52 link). Nothing posted elsewhere.
