# Publication record: 0.6.0-feel.59 test build

- Published 2026-11-25 as a GitHub pre-release, beside feel.56 (left up):
  https://github.com/HWG90/HellForge-True-First-Person/releases/tag/v0.6.0-feel.59
- The user asked in the feature session ("Make sure we get the latest version out before we run out of usage"). The
  feature session relayed it, and the user confirmed in the release session ("Publish all") before anything was published.
- Built from commit `75e9ba8` (local tag `feel.59-build`), not main's head: local main had moved on with round 40.
  The release commit `6d1b89c` sits on top of `75e9ba8` and changes only `docs/guides/TEST-BUILD.md`, which retitles the
  notes to feel.59 and adds "Changed and fixed in 0.6.0-feel.59" with the unplayed items and the known issues.
- It was made in a separate worktree (branch `release-feel59`). Origin's `main` was fast-forwarded `8b9c299..6d1b89c`
  (33 commits, all authored "Goose" with the noreply address) and then to this record. The local `main` (round 40,
  33d1d08) must merge origin/main.
- Tag `v0.6.0-feel.59` (annotated) on `6d1b89c`.
- DLL: the feature session's tested build from `~/.cache/tfp-release-feel59/`, sha256
  `5890d56fd37d10a92f3aeda54e468517043919d22c2b360f2a55567f24b592e2` (75 test scripts and the harness tests passed).
  Its manifest is identical to `manifests/feel/hellforge-mod.json` at `75e9ba8` and says 0.6.0-feel.59.
- One asset (downloaded back and compared): `True-First-Person-0.6.0-feel.59-HellForge-25480438-complete.zip`, sha256
  `6cefb53ee0966ea8765ee1a35dd71c418425c0f379ffffa21d4288a0ff460f72`, 54 files. It holds true-first-person (the DLL,
  manifest, settings, README-TEST.md (= TEST-BUILD.md at the tag), LICENSE and NOTICE), and NEST 0.8.3-tfp.1 and DBF-MCM
  0.1.74-hellforge.4 as in feel.56. No personal details in any packaged file.
- Known issues added beyond the hand-over, from the release session's diagnoses: belt gear in the exosuit, and the
  camera behind the head at throws. Neither is fixed between 8b9c299 and 75e9ba8.
- Playtest 1 renamed "Playtest 1: 0.6.0-feel.59". Its body names feel.59, the new known issues, and an exosuit item to
  try and tick. A comment lists what changed: https://github.com/HWG90/HellForge-True-First-Person/issues/1
- Discord: the post is edited to feel.59 and renamed "True First Person - Early Preview (0.6.0-feel.59)".
