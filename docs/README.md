# Documents

## Guides

How to do things with the mod in a running game.

- [The read-only probe (0.6.0-probe.2)](guides/LIVE-PROBE.md)
- [Replacing the mod while the game runs (0.6.0-feel.5)](guides/LIVE-REPLACE.md)

## Wiring

Which translated unit is bound to what in each build, what is off, and the in-game checklists.

- [The feel build (0.6.0-feel.1): what is wired to what](wiring/WIRING-FEEL.md)
- [Wiring inventory: binding every host pointer to HellForge (build 25480438)](wiring/WIRING-INVENTORY.md)
- [The view build (0.6.0-view.1): what is wired to what](wiring/WIRING-VIEW.md)

## Build 25480438

Addresses, offsets and code checks for HellForge's game build, and how each was established.

- [Build data: 25480438 (HellForge) against 26568081 (retail)](build-25480438/BUILD-DATA-25480438.md)
- [What differs on build 25480438](build-25480438/BUILD-DIFFERENCES.md)
- [Look-gain constants on build 25480438](build-25480438/LOOK-GAIN-CONSTANTS-25480438.md)

## Translation notes

One note per translated retail unit: what was translated, how it is tested, what is open.

- [Arms: arm_pieces.lua and grip_hand.lua in C](translation/ARMS-TRANSLATION.md)
- [Backend operations: translation notes](translation/BACKEND-OPS-TRANSLATION.md)
- [Blur helper: the C translation](translation/BLUR-HELPER-TRANSLATION.md)
- [Camera adapter: translation notes](translation/CAMERA-ADAPTER-TRANSLATION.md)
- [Camera helper: the C translation](translation/CAMERA-HELPER-TRANSLATION.md)
- [Effects modules: seven small retail Lua files in C](translation/EFFECTS-MODULES-TRANSLATION.md)
- [FOV helper: the C translation](translation/FOV-HELPER-TRANSLATION.md)
- [Lens adapters: fov.lua, near_clip.lua, blur.lua in C](translation/LENS-ADAPTERS-TRANSLATION.md)
- [Motion modules: translation notes](translation/MOTION-MODULES-TRANSLATION.md)
- [Motion runtime: motion_runtime.lua in C](translation/MOTION-RUNTIME-TRANSLATION.md)
- [Near-clip helper: the C translation](translation/NEAR-HELPER-TRANSLATION.md)
- [Pose adapter: pose.lua in C](translation/POSE-ADAPTER-TRANSLATION.md)
- [Editor/pose helper: the C translation](translation/POSE-HELPER-TRANSLATION.md)
- [True First Person (F11), Goose Gadget Kit 0.5.41-smooth.66: live inventory](translation/RETAIL-SMOOTH66-INVENTORY.md)
- [Runtime order: runtime.lua in C](translation/RUNTIME-TRANSLATION.md)
- [Head visibility and model shadow source in C](translation/VISIBILITY-TRANSLATION.md)

## Design

The design the rewrite follows and the plans it was built from.

- [Milestone 1, Part A: Foundation (build data, snapshot, session state machine) Implementation Plan](design/plans/2026-11-16-m1a-foundation.md)
- [Milestone 1, Part B: The camera policy Implementation Plan](design/plans/2026-11-16-m1b-camera.md)
- [True First Person for HellForge: C rewrite of retail smooth.66 (design)](design/specs/2026-11-16-c-rewrite-design.md)

## History

Where the starting files came from, and research done for the earlier 0.5.34 port.

- [Provenance](history/NOTICE-0.5.34.md)
- [Where the starting files came from](history/PROVENANCE.md)
- [research-0.5.34/](history/research-0.5.34): evidence files and reports from the 0.5.34 port
