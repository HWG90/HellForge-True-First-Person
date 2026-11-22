# True First Person for HellForge

A first-person view for HELLDIVERS 2 running in [HellForge](https://github.com/HWG90/HellForge) (offline, game build 25480438). Press **F11** in a mission and the game's own first-person camera takes over, with your weapon and arms placed, moving and aiming as they do in the retail mod this is rewritten from.

This is a C rewrite of the True First Person mode of Goose Gadget Kit (a Lua mod for the current retail game, version 0.5.41-smooth.66). The retail game and HellForge's pinned build differ, so every address, offset and code check was established again for build 25480438.

**Status: in development.** It runs in the game and is being tested session by session. Expect rough edges and refused features; the session log says what refused and why.

## What works

| Feature | State |
|---|---|
| First-person camera, field of view, near clip, scope blur | In game |
| Weapon and arm placement, per weapon and per action (idle, aim, sprint, crouch, prone, reload) | In game, with placements imported from the retail mod |
| Bob, sway and movement feel | In game |
| Aiming down sights through the view | In game |
| Hiding your own head, torso and shoulders | In game; a fix for backpacks is in 0.6.0-feel.4, not yet confirmed |
| Hip sensitivity that does not change with zoom level | In game since 0.6.0-feel.2 |
| Depth of field and vignette handling, aim markers | In game |
| Locked view: first person through every action (swap, grenade, stim, emote, vault, dive, prone) | In game since 0.6.0-feel.20; all 35 scripted actions pass with no frame out of first person (0.6.0-feel.34), stratagem list and throw included |
| Animated arms: the first-person arms follow the character's own animation; pistol in one hand at the hip, two when aiming | In game since 0.6.0-feel.21 |
| Weapon put-away shown before a swap, grenade or stim | In game since 0.6.0-feel.22 |
| Smooth eye: no camera pops when the game changes its camera | In game since 0.6.0-feel.23 |
| Field-of-view lock (one configurable field of view), view kicks from body movement | In game since 0.6.0-feel.20; the field-of-view lock works with the camera lock off since feel.34 |
| Dying in first person (the view stays at the head of the body) | In game: seen standing, aiming, sprinting and mid-swap (0.6.0-feel.28) |
| Own settings panel (F10, through NEST; DBF-MCM no longer needed) | In game since 0.6.0-feel.20; options switch live since feel.24; tabs, toggles, sliders and key rows used in the game (feel.34) |
| Saving settings (`settings.json` for placements and menu, `locked_view.json` for the locked view) | In game; files written by feel.20 to feel.24 are converted on first start (0.6.0-feel.25) |
| Sleeves and hand grips (retail's bone offsets) | Held back since 0.6.0-feel.19; superseded by Animated arms |
| Replacing the mod in a running game | Built in 0.6.0-feel.5, never run |
| Placement editor ("poser") | Built in 0.6.0-feel.7: Page Up, or the panel's button; needs NEST. The eye-camera editor is not built |
| Sharing the camera with Photo Mode | Not built: do not open Photo Mode with the view on |

Tester notes for the current build: [docs/guides/TEST-BUILD.md](docs/guides/TEST-BUILD.md). How it was tested in the
game: [docs/design/HARNESS.md](docs/design/HARNESS.md) and the results sections of
[docs/design/specs/2026-11-22-locked-view-design.md](docs/design/specs/2026-11-22-locked-view-design.md).

## Install

1. Build it (below), or take a build from Releases when there is one.
2. With HellForge and the game closed, copy `goose_true_first_person.dll`, `hellforge-mod.json` and `settings.json` into `HellForge/mods/true-first-person/`.
3. Switch the mod on on HellForge's Mods page. Switch any other first-person mod off.
4. For the settings menu, also have DBF-MCM for HellForge (`goose.mcm`) switched on; it opens with DEL. Without it the mod runs on its saved settings.

Keys: **F11** toggles the view (changeable in the menu). What the menu and the mod change is saved to `%LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson\settings.json`; see [docs/guides/POSER-AND-MENU.md](docs/guides/POSER-AND-MENU.md).

## Build

Needs a HellForge 0.4 installation (it supplies the SDK header and the zig compiler) and Python 3.

```bash
bash build.sh --feel
```

The result is in `dist/true-first-person-feel/`. `HF=<HellForge folder>` points the script at an installation elsewhere. `bash build.sh` lists the other targets.

## Tests

```bash
bash tests/run-native.sh
```

It runs on Linux without the game. Some of it needs things this repository does not contain and says "skipped" without them:

- the retail mod's source (`TFP_RETAIL`), for the differential tests;
- LuaJIT, to regenerate reference vectors;
- images of the two game builds, for the address tools.

## How it is kept honest

The retail mod is the reference. Each retail Lua or C++ unit runs unmodified against scripted stand-ins for the game, and its answers, its ordered calls into the game and its log texts are recorded as vectors (`tests/vectors/`). The C translation of that unit must answer the same, doubles compared bit for bit. Where both can be built side by side they are compared call by call instead (`tests/run-diff-*.sh`).

Every address is pinned: before the mod writes anything it checks, in the running game, that the code it depends on is byte for byte what was analysed. A check that fails switches that one feature off and says so in the log.

## Layout

| Folder | What is in it |
|---|---|
| `src/` | The mod. Translated retail units, the binders that join them (`wire_*.c`), the Windows side (`wire_win*.c`, `mod_view.c`), and the older 0.5.34 port this started from |
| `tests/` | Native tests, reference vectors, the scripts that run them |
| `tools/` | Address finders and checkers, settings converter, vector generators (`tools/retail_ref/`) |
| `manifests/` | One `hellforge-mod.json` per build target |
| `import/` | Weapon placements exported from the retail mod, converted at build time |
| `docs/` | Translation notes, build data, wiring, guides and history: start at [docs/README.md](docs/README.md) |

## Licence

MIT, see [LICENSE](LICENSE). No game files are included or distributed. See [NOTICE.md](NOTICE.md) for where the code came from.
