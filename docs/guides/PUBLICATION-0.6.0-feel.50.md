# Publication record: 0.6.0-feel.50 test build

- Published 2026-11-24 as a GitHub pre-release:
  https://github.com/HWG90/HellForge-True-First-Person/releases/tag/v0.6.0-feel.50
- `main` pushed to origin first (`b9fed78..9fff0b9`, 282 commits, all authored "Goose" with the noreply address).
- Tag `v0.6.0-feel.50` (annotated) on `main` at `9fff0b9`. The manifest (`manifests/feel/hellforge-mod.json`) and
  `src/mod_view.c` both say 0.6.0-feel.50 there.
- The DLL in both ZIPs, `dist/true-first-person-feel/goose_true_first_person.dll` from a fresh
  `TMPDIR=$HOME/.cache bash build.sh --feel` at `9fff0b9`, and the copy installed and played on the test machine are
  byte-identical: sha256 `0f08d0781fb6f82d9e07e2f1768beaf563fe788d4f4ee91aad1637e6b98dd069`.
  - The build is reproducible only in the same folder: a build of the same commit in another folder differs in 20
    bytes (the PE build hash, the debug-directory stamps and the PDB GUID, which LLD derives from the PDB and so from
    the build path). The code is the same. The check above was built in the repository's own checkout.
- Assets (both downloaded back and compared):
  - `True-First-Person-0.6.0-feel.50-HellForge-25480438-complete.zip`, sha256 `71d2b84eccd971c705e11cc304bd73d081be7907a719b8f60b2945a296ff8b2c`
  - `True-First-Person-0.6.0-feel.50-HellForge-25480438-test.zip`, sha256 `fd476152164e75d1a70972bc2c445f2bd991d885173b3ed7681b5f91edc1cecd`
- The complete ZIP holds the mod and NEST 0.8.3-tfp.1 (`overlay-host`), byte for byte the copy installed beside the
  mod on the test machine and the one in the feel.19 complete ZIP. It does not hold DBF-MCM, unlike feel.19's: feel.19
  promised DBF-MCM because it was the mod's settings menu, and this build has its own panel (F10); the release text
  says DBF-MCM is no longer needed. It was not installed into an empty HellForge.
- Known stale text: the built manifest's description still says "Settings are in DBF-MCM". Left as built, since it is
  the build that was played; to correct in the next build.
- Release text: `docs/guides/TEST-BUILD.md` as it was at the tag (also `README-TEST.md` in both ZIPs, identical).
- Playtest: pinned issue "Playtest 1: 0.6.0-feel.50", label `playtest`:
  https://github.com/HWG90/HellForge-True-First-Person/issues/1
  It names the known issues of this build, including a slight field-of-view pulse while aiming that is not in
  TEST-BUILD.md and is fixed for the next build.
  Edited the same day to add a "Weapon positions" section (not every weapon's position is set yet; the placement
  editor's controls; testers asked to send the values of weapons they place) and a checklist line for the editor.
- The 0.6.0-feel.18 and feel.19 pre-releases stay up beside it.
- Discord, at the user's request (2026-11-24): a post "True First Person" in HellForge Official's `mod-showcase`
  forum, tags Mod, Work in Progress and Available, linking the pre-release and the playtest issue:
  https://discord.com/channels/1555635878775361606/1555642510473429093/threads/1574781605258469456
  The user added an in-game screenshot. Edited the same day to say it is not a third-person camera moved into the
  head but a true first-person rig (the game's own first-person view, kept on); edited again to say not every
  weapon's position is set yet, with the placement editor's controls (Page Up; left drag, wheel, right and middle
  drag, Shift, < >, Save weapon / Save all / Reset, Enter, Esc). Nothing posted elsewhere (no Nexus
  or HellForge listing).
