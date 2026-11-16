# True First Person for HellForge: C rewrite of retail smooth.66 (design)

Date: 2026-11-16. Target: HellForge 0.4, game build 25480438, SDK 16. Language: C11. This repository.

## Intent

The user plays True First Person on the retail game (Goose Gadget Kit, Lua, build 26568081, version 0.5.41-smooth.66) and wants the same mode on HellForge: "bring over the current state of things, features and all". Their words for the bar: "The first person model should feel like it was already in the game. It should feel stable, polished, clean and fluid. There should be NO jarring transitions." They chose a rewrite in C over running the retail Lua (asked 2026-11-16).

Two earlier C attempts on this build were judged by the user in game and rejected: the port of kit 0.5.34 (cancelled 2026-11-10: weapons slow to reappear after a swap, scope masking) and the First Person Controller (2026-11-16: "SIGNIFICANTLY more jank than my live retail lua version, with old bugs regarding view problems"). Both behaved differently from retail. This design exists to prevent a third drift.

## Goals

1. Every behaviour of the live retail mode at smooth.66 exists here and behaves the same. The acceptance list is section 7 of `docs/RETAIL-SMOOTH66-INVENTORY.md` (172 numbered behaviours).
2. The view the player sees is the game's own camera, driven as retail drives it. No substitute camera at draw time. (Measured 2026-11-16 on the Controller: its shown camera differed from the game's by up to 64 degrees of field of view and 2.8 m, which is what made foliage update strangely.)
3. Same numbers as retail: every timer, distance, blend and threshold listed in the inventory is carried over unchanged unless a difference of this game build forces a change, and each such change is written down with its reason.
4. Each milestone can be checked by the user in game and by a session log.

## Non-goals

- The 26 parked retail files (Fird Person, ready pose, alignment, held fire, zoom, skinned placement, support hand) and the two dead menu controls ("Free aim", "Compare native ADS camera").
- The rest of Goose Gadget Kit (unlocks, spawner, loadouts).
- Fixing the First Person Controller. It stays as it is; it is switched off when this mod is installed.
- Hot reload of the mod's code. One DLL; a new build needs a game restart.
- Releases, pushes, publishing.

## Anti-goals

- Any transition the retail mode does not have: a snap, a frame of third person, a weapon that jumps to the screen centre, arms left floating after a dropped weapon.
- A weapon or arms invisible after a swap (the 0.5.34 port's failure on this build).
- "Improvements" to retail behaviour made while translating. A translation that reads better but answers differently is a defect.
- A crash or hang: every game read is guarded, every hook is checked against pinned bytes and fails closed.

## Constraints

- Retail source is read only: `~/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit/` at 1d0d453. Another session works there.
- HellForge mod UI is Dear ImGui through the Goose Overlay Host and follows `~/Projects/HD2-UI-Style/DESIGN.md`, colours from `ov->theme()`. Retail's menu and F8 editor live in DBF-MCM, which is not used here; this is the one deliberate difference in form (same settings, same defaults, same keys F11 and F8).
- Build 25480438 data comes from this build's image (`native-work/evidence/game-pre1020.dll`, file offset = RVA) and is pinned by bytes. Known so far: 11 of retail's 14 code pins match at one place each (enter wrapper 0x127ddb0, constructor 0x127de20, exit 0x127e9d0, evaluator 0x1286330, aim controller enter/exit/update 0x12b29b0 / 0x12b2ae0 / 0x12b26a0, controller aim gate 0x12b28a3, input desired gate 0xa41c0b, allowed 0x12b2d20, allow-first-person field 0x12b2dfb). The three 4-byte call sites and every structure offset on the Lua side still have to be found.
- No install and no Wine or game run that opens a window without the user's word. No attribution lines. No push.

## Approaches considered

A. **Translate retail, unit for unit, on the infrastructure of the 0.5.34 C port (chosen).** The earlier port (`~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge-dev5`) already has, for this build: guarded reads and snapshots, the API-table slot leases retail's helpers use, a bridge to the engine's Lua-registered SDK functions, a 65-witness profile, settings, an Overlay Host panel, a build script and tests. Its behaviour code (camera, pose, lens, lifecycle policies) is retail as of 0.5.34 and is replaced, file by file, by translations of the smooth.66 sources. What is kept is plumbing, and each kept piece is re-read against the retail helper it serves before it is trusted.

B. **Translate retail from an empty folder.** Cleanest lineage, but re-solves the engine bridge, slot leases and this build's profile, which took the earlier port days and are not where it went wrong.

C. **Extend the First Person Controller.** Rejected by the user's verdict and by the camera measurement.

The user decided: C, retail's current state, features and all. A is my call for how.

## Design

**One mod, one DLL**, `goose.true-first-person` (the id the 0.5.34 port used; that install is replaced). Units mirror retail one to one so a behaviour can be traced to its source:

| Retail (live) | Here |
|---|---|
| `profile.lua` | `profile.h`: build 25480438 addresses, offsets, pins |
| `fp/backend.lua` (live half), `action_state.lua`, `locomotion.lua` | `backend.c`, `action_state.c`, `locomotion.c`: guarded reads, the snapshot, enter / exit / allowed, keep-aim-view and seated one-shots |
| `fp/controller.lua`, `recovery.lua`, `transition.lua` | `controller.c`, `recovery.c`, `transition.c`: session and lease state machine, retry ledger, the shared 0.18 s clock |
| `fp/runtime.lua` | `runtime.c`: the per-update order of inventory section 2.1, exactly |
| camera helper (`native_ads_camera.cpp` + `camera.lua`) | `camera_policy.c`: CameraAPI position slot; head camera, eye hold with per-weapon memory, exit hold, correction, aim anchor, seated view |
| editor/pose helper (`native_ads_editor.cpp` + `pose.lua`, `motion*.lua`, `action_placement.lua`) | `pose_policy.c`, `motion.c`, `motion_runtime.c`, `action_placement.c`: placement, bob and sway, ADS placement, draw animation, entry rotation guard, remembered entry position, zoom depth, look-gain normalisation |
| fov, near, blur helpers and their adapters | `fov_policy.c`, `near_policy.c`, `blur_policy.c` |
| `head_visibility.lua`, `model_shadow_source.lua` | `head_visibility.c`, `model_shadow.c` |
| `arm_pieces.lua`, `grip_hand.lua` | `arm_pieces.c`, `grip_hand.c` |
| `dof.lua`, `vignette.lua`, `aim_markers.lua`, `first_person_movement.lua`, `native_ads_option.lua`, `camera_compat.lua` | same names |
| `menu.lua`, `placement_editor.lua`, `ui_style.lua` | `ui.c`, `placement_editor.c` on the Overlay Host |
| `diagnostics.lua` | `diagnostics.c`, plus the smoothness meter and request recorder written on 2026-11-16 (`smooth_meter`, `request_log`, `tools/smooth_report.py`) |

**Hooks** are what retail's helpers use: function-pointer swaps in the engine's API tables (CameraAPI position, field of view, near; five Unit API slots for the rig; the weapon parameter setter for blur), each gated on the caller's return address and on pinned bytes. No code patching. The aim-release keeper is retail's one-shot write of the aim controller's active byte on the button edge, not the Controller's hook.

**Calling the engine.** Retail calls SDK functions from Lua (`Unit.set_local_pose`, `World.link_unit`, mesh visibility, renderer settings). Here they are called through the engine bridge of the earlier port; every function used is named in a table with its pinned address, and a missing one disables the feature that needs it with one log line.

**Keeping the answers the same.**
- Helper sources are translated from C++ with their own fixtures (`camera_fixture.cpp`, `editor_fixture.cpp`, `fov_fixture.cpp`, `near_fixture.cpp`, `blur_fixture.cpp`) translated alongside, so the same cases pass.
- Pure Lua logic (inventory section 6e: motion, transition, action placement, the controller state machine, the grip solve, sleeve placement, visibility pass bits) is run from the retail files under LuaJIT on scripted inputs to produce reference vectors; the C tests must reproduce them to 1e-5.
- Wiring that has no retail test (the update order, arm module gating, the one-shots) gets tests written from inventory section 2.
- Every number is a named constant with the retail file and line beside it.

**What this build needs that retail does not** (known so far): weapons can stay hidden after a swap because the game hides the whole unit; the earlier port's hide filter and redraw took about 1.5 s on one weapon. This is solved inside milestone 1 with a stated limit: the weapon is visible on the first frame the arms are shown. Anything else found is added to `docs/BUILD-DIFFERENCES.md` with evidence.

## Milestones

The user judges against retail's feel. Retail's entry arms the pose helper before the game's own entry call, and its arms are corrected from the first frame, so a build without placement, the draw animation, the entry guards, sleeves and grips would fail that comparison by construction, as the two earlier attempts did. The first in-game session therefore comes after milestones 1 to 3. Until then each milestone is shown by its offline tests and a short report. (My call; the user may ask for an earlier look, knowing what is missing.)

1. **The view.** Profile and pins; backend snapshot; controller, recovery, transition; camera, fov, near, blur policies; head visibility, shadows, depth of field, vignette, aim markers; F11; diagnostics log.
2. **Placement and motion on the entry path.** Pose policy, motion, action placement, draw animation, entry rotation guard, remembered entry position, ADS placement, zoom depth, look-gain normalisation.
3. **Arms.** Sleeves and hand grips, finger shapes, wrist-screen mesh, revolver spin, swap lookup.
   **First session with the user:** F11 gives the game's first-person arms at the hip through the game's own camera; aiming in and out, swaps, stims, throws, dives and seats behave as retail; no invisible weapon; no stretched sleeves; hands on the weapon.
4. **Editor, settings and the rest.** F8 placement editor; Overlay Host menu with every retail setting and default; movement policy; native ADS option; Photo Mode camera sharing (what `goose.photo-mode` exposes for camera ownership is looked up then; if it exposes nothing, that mod needs a change, which is a separate piece of work to raise with the user).

## Decisions made after review (my calls unless marked)

- **Retail is frozen at 1d0d453 (smooth.66) for this project.** Reference vectors and the 172-item list are cut from that commit. Later retail commits are folded in one at a time by reading their diff, on the user's word.
- **The user's own retail values come across.** Per-weapon placements and the mode's settings are imported once from the retail install's stores, read only, when the weapon keys match between builds; otherwise retail's shipped defaults apply and the log says which weapons were not matched. Locating those stores and checking the keys is a task in milestone 2.
- **The 0.5.34 port's `settings.ini` is not read.** This mod writes a new file; the old one is left alone.
- **Weapon visibility after a swap:** the earlier port's hide filter and per-tick redraw did run in the game (2026-11-09: one weapon stayed hidden about 1.5 s after each swap) and is not the starting point. The First Person Controller showed weapons through every swap in the user's sessions of 2026-11-16; its handling (`src/native_body.h`, `src/native_camera_adapter.c` mesh passes, and the absence of a whole-unit hide filter) is read first and the cause of the earlier port's delay is established before anything is written.
- **Other mods on the same API slots:** before milestone 1's hooks are written, the session folder's `hooks.tsv` and the installed mods are checked for anything that swaps the CameraAPI or Unit API slots this mod leases; the First Person Controller is switched off when this mod is installed. A slot already swapped by another mod is reported in the log by name, not refused silently.
- **Overlay Host version:** the installed host's API is checked when the first panel is written and the oldest header that has what the panel needs is vendored.

## Size, said plainly

About 11,000 lines of live Lua and 4,600 lines of helper C++ to translate, plus the address work for this build. This is several long working sessions, not one. The earlier port's plumbing shortens milestone 1; nothing shortens the translation of the arms and the editor.

## Left to the builder

File-internal structure, test harness layout, the order of translation inside a milestone, and how the reference vectors are generated (LuaJIT binary or lupa), provided the retail files are run unmodified.

## Open, to settle with evidence as the work reaches it

- Which thread runs each hook on this build, and whether the camera evaluator runs before or after animation (retail's source does not say either).
- Whether this build's packed code pages need warming before entry, as retail's four cold pins do.
- The aim-release keeper is bound to the right and middle mouse buttons in retail. Ported as is; a controller or a rebound aim key will not trigger it, the same as retail.
- The Lua-side structure offsets for this build: the First Person Controller's `src/native_view.c` reads the same structures and is the reference.
