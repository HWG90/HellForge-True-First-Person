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
| Sleeves and hand grips | Refused at start-up (a maths check is 0.004 off its 0.0001 limit) |
| Replacing the mod in a running game | Built in 0.6.0-feel.5, never run |
| Placement editor, settings menu, saving settings | Not built |
| Sharing the camera with Photo Mode | Not built: do not open Photo Mode with the view on |

## Install

1. Build it (below), or take a build from Releases when there is one.
2. With HellForge and the game closed, copy `goose_true_first_person.dll`, `hellforge-mod.json` and `settings.json` into `HellForge/mods/true-first-person/`.
3. Switch the mod on on HellForge's Mods page. Switch any other first-person mod off.

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
