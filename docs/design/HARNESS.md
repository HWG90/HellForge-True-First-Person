# Testing in the running game

True First Person is tested in the game itself through the Bingus Development Harness
(`~/hellforge-research/tools/BingusDevelopmentHarness`) plus the three files in `tools/harness/`.
Offline tests prove the C matches retail's Lua; only a run in the game proves a behaviour.

## One run

```bash
cd ~/hellforge-research/tools/BingusDevelopmentHarness
export HD2H_TARGET=hellforge
python3 harness.py recover                 # only after a run that did not stop cleanly
python3 harness.py start --preset light    # launch; the installed mod list decides what loads
python3 harness.py ship
python3 harness.py mission --difficulty 1  # solo mission, player in control (about 3 minutes from start)

cd ~/Projects/HellForge/Backports/true-first-person/tools/harness
python3 drive.py focus                     # bring the game to the front
python3 drive.py run "key f11; wait 1; hold w 1.5; click left 0.5; key r"
python3 drive.py shot /path/out.png
python3 probe_rig.py OUT_DIR idle walk fire reload aim swap

python3 harness.py stop                    # from the harness folder; restores what it changed
```

The mod list is `mods/hellforge-mods.json` in the HellForge folder (`disabled` holds mod ids). Change it
only while the game and launcher are closed, and keep a copy.

## What the pieces do

| File | Purpose |
|---|---|
| `drive.py` | `focus()` through a KWin script, `Input.run(script)`, `shot(path)` through Spectacle, `lua(code)` through the harness |
| `win_input.c` | started inside the game's Wine prefix; plays a timed event list with `SendInput` |
| `rig_motion.lua` | per-frame recorder inside the game: largest joint change per 0.1 s for the first-person rig and the body |
| `probe_rig.py` | runs named phases (input + recording + capture) and prints one strip per rig |
| `session.sh` | `up`: install a build with a backup, set the mod list, launch to a mission; `down`: quit and restore the list |
| `peek.py`, `watch_field.py` | read the running game's memory from Linux by module offset |

Input script words: `wait S`, `key NAME`, `hold NAME S`, `click left|right|middle [S]`, `down NAME`,
`up NAME`, `keep NAME`, `look DX DY`. Names: letters, digits, `shift ctrl alt space tab esc f9 f10 f11`, the arrows
`arrowup arrowdown arrowleft arrowright`. `keep NAME` (round 26) leaves what the script pressed with `down NAME` down
when the helper ends; without it the helper lets go of everything it holds, so nothing stays held from one
`Input.run()` to the next. `aim.py` and `aim-full` in `matrix.py` play full aim (see below).

## Facts that cost time to find

- The game is a native Wayland window (`PROTON_ENABLE_WAYLAND=1`). The engine drops all input while
  `stingray.Window.has_focus()` is false, and only the compositor can capture the window. So a run takes
  the keyboard and mouse: `drive.py` puts the window in front first and refuses to send input otherwise.
- Posted window messages (`PostMessage`) move the player but are invisible to `GetAsyncKeyState`, which is
  how the mod reads F11 and the aim button. `SendInput` from a process in the same prefix reaches both.
- `/dev/uinput` is not usable after a kernel package update until the next reboot (the running kernel's
  modules are gone), so the helper does not depend on it.
- The harness's own `shot` needs X11 and fails here; use `drive.py shot`.
- Commands that touch the prefix (`bwrap`, Wine) must run outside the tool sandbox.
- The first run hung on the way back to the ship (`harness.py stop` from the mission: state `prepare-ship`,
  game thread at 100 %, agent silent) and had to be ended with `stop --kill`. Two things were live that
  the earlier clean stops did not have: the rig recorder still scanning `World.units` every frame, and
  True First Person switched on ("waiting: Empty entity" was its last status). Which one hung the load is
  not known yet. `probe_rig.py` now switches the recorder off when it finishes; test the other by leaving
  a mission with the view on and no recorder.
- Lua node indices start at 1: `Unit.world_position(unit, 0)` answers the origin or NaN.
- The hang on the way back to the ship did not repeat once the recorder was off (three later sessions left
  a mission with the view on): do not leave a per-frame `World.units` scan running across a level change.
- The menu's own store wins: `userdata/environment/Local/DBF/HellForge/MCM/true_first_person.ini` overrides
  the mod's `settings.json` at start-up. Change a menu option for a test there, with the game closed.
- Forcing meshes visible from Lua (`Unit.set_mesh_visibility(unit, i, true)` on every mesh) also shows the
  shadow-only meshes: grey untextured armour in the view for the rest of the session. A probe that does
  this needs a fresh session before any capture counts.
- A trailing `wait` in an input script is honoured now; before 2026-11-20 midday, captures after
  `run()` were taken too early.
- The game often reports a crash on quit that is not the mod's: see [QUIT-CRASH.md](QUIT-CRASH.md).

## Full aim (round 26, 2026-11-24)

The right button alone is the game's **shoulder** aim. Full aim (the mod's `full_ads`, the controller's `controls[4]`,
the log's `State: aim in`, View `ads=1` and the `Aim:` lines) is the game's other aim mode. On PC it is switched with
the **middle** button while aiming (the default "switch aim mode" binding; the mod's aim watch treats a middle press as
the aim-mode button, `src/wire_action.h`). The 26 harness soak logs 8 to 34 have 1,332 `Motion: action=aim` and one
`State: aim in` (soak 22, 05:24:47, cause not known). The user's log 31 has 13. The game's `remember_aim_mode` is 0
in every HellForge profile.

What the harness does about it, as a spec:

- `drive.py`: the script word `keep NAME` and `Input.held`; `Input.close()` lets go of what is held. `win_input.c`:
  `k` lines; an interrupt or a vanished game window lets go of everything, kept or not.
- `aim.py` `full_aim()`: `down right; keep right; wait 0.35`, then `click middle`, then read the log for
  `State: aim in` for 1.5 s; one more `click middle` where it did not come; the phase's action; the hold; `up right`
  always. A result says reached, presses, seconds to full aim, and lost (out before the harness let go).
- `aim.py` `zoom_cycle()`: in full aim, `down r; wait 0.6; key DIRECTION; wait 0.2; up r` for each arrow in turn
  until an `Aim: changed` line has another `fov` (0.5 degrees or more), then that direction once more.
- `matrix.py` `AIMED`: aim, aim-fire, aim-walk, pistol-aim, aim-full, aim-scope-zoom go through `full_aim()` and fail
  without full aim (`aim.verdict()`). `soak.py` plays them so too (not aim-scope-zoom) and counts reached aims.
- Offline proof: `tests/test_harness_aim.py`. It covers the event lines, the helper built against
  `tests/harness_win_stub/windows.h` (a kept button is not let go, an unkept one is), the sequence against a fake game
  that writes the log, the reading of the `Aim:` lines, and the counts above. Not proven offline: that the game goes
  into full aim on the middle press, and which direction of the menu is the scope's range. `docs/guides/AIM-TEST.md`
  "The harness" says what the next run must show.

## First-person rig probe, 2026-11-20

Solo mission, difficulty 1, 0.6.0-feel.10, primary and support weapon. Numbers are in
`probe-2026-11-20/` (`*.json` ordinary camera, `fp-*.json` with the game's first-person camera held).

The game's first-person arm rig (63 joints, node `fp_cha_avatar`) is the same in both cameras:

| Action | First-person rig joints | Third-person body |
|---|---|---|
| idle, walk, sprint, strafe, look, crouch, dive | no change at all | animated every frame |
| aim (hold right button) | no change | animated |
| reload | no change | animated |
| fire | 0.002 to 0.006 for about 0.8 s (recoil) | animated |
| swap, grenade, stim | two single-frame pose changes | animated |

So the game never animates that rig beyond a per-weapon pose and recoil. A private arm rig that copies
it inherits a still pose. Movement, reloads, swaps, throws and stims exist only on the body's arm joints,
which carry the same joint names (`r_hand`, `r_elbow`, `r_shoulder`, fingers, `attach_hand_r/l`).

With True First Person on, the view dropped to the head camera only for swaps, the grenade and the stim
(action mask 2) and returned 0.3 to 1.5 s later; reload, aim, crouch, sprint and dive kept camera kind 6.
The return still logs a camera cut of 0.08 to 0.40 m.
- 2026-11-23: a real pointer through `/dev/uinput` (which works again after a reboot) reaches the compositor, but the
  harness has no reliable way to know where the game window is on a multi-monitor desktop: a first try clicked in
  another application's window. Not kept. The NEST panel's rows are driven with `press X Y`; stratagem codes with a tap of `ctrl` and `arrowup`/`arrowdown`/`arrowleft`/`arrowright` (plain `left`/`right` are the mouse buttons); do not
  drive the desktop pointer blind.
- 2026-11-23: the NEST panel CAN be driven from inside the game's process: the host takes the pointer position from
  the click message itself (overlay-host `src/host.cpp`), so `press X Y` in a `drive.py` script posts
  WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_LBUTTONUP at a client pixel. A window capture is 1430x871 for a 1280x720 client:
  the client's corner is at (75, 71) of the capture (shadow margin, border and title bar). With the panel at its
  default place: tabs at client y 151 (First person 430, Bob and sway 513, Locked view 595); on the Locked view tab
  the checkboxes are at x 402: Camera lock 209, Field of view lock 234, Zoom when aiming 284, Animated arms 384,
  Weapon put-away 452, Smooth eye 477.

## Lua in the game, and walking to a place (2026-11-22)

The Bingus harness runs Lua inside the game (`python harness.py lua "..."`, `HD2H_TARGET=hellforge`). What that gives
on build 25480438, and what it does not:

- `stingray.World.units(stingray.Application.main_world())` lists about 6000 units in a mission. Their names are
  hashes (`#ID[...]`); there is no dictionary for them here.
- **Guard every transform read**: `Unit.alive(u) and Unit.num_scene_graph_items(u) > 0`. An unguarded
  `Unit.world_position` over all units crashed the game (access violation at helldivers2.exe+0x9e27e; the dump of
  12:00 on 2026-11-22 is that probe, not the mod).
- The player's body is the unit with 107 scene-graph nodes (`#ID[4d1c334d294dfa97]` with the armour worn in the
  tests); its node 1 is the root. There is no `Mover` and `Unit.mover` in this build's Lua.
- `Unit.teleport_local_position` on the body does nothing lasting: the game's own character code owns the position.
- `tools/harness/walk.py`: `where` prints the body's position and the rare units within 60 m; `to X Y` walks there by
  closed loop (read the position, turn, walk). Each read is one Lua command through the file mailbox, so a walk of
  20 m takes minutes. It was used once, to a building's wall (the rifle lowers against a wall, as it should), and
  not yet to a terminal or a pick-up: nothing says which hashed unit is one.
- Input is refused when the game window does not have focus (`drive.py`), which is what happens when someone uses
  the desktop meanwhile. That is the wanted behaviour; do not work around it.

## The HD2 runtime in a test session (2026-11-22)

`require('mods/skyeshade/hd2runtime')` loads the shared HD2Runtime (0.50.1) in a harness session; the user pointed
at it and at LLtM as test levers. What was tried on build 25480438, through `tools/harness/game_lua.py`:

- `hd2.local_player()`: `loadout()` names the weapons in hand (`LAS-5 Scythe`, `LAS-58 Talon`, the throwable and its
  count), `health()`, `max_health()`, `is_alive()`, `heal`, `injure`, `add_velocity`.
- **`player:injure(limb, damage)` works** (limbs head, chest, l_hand, r_hand, l_knee, r_knee; from a runtime timer:
  `hd2.after(seconds, function() ... end)`). Three injuries of 40 killed the player: a death from real damage. With
  the view on it stayed at the head until the replacement, and first person came back after the respawn.
- **`player:add_velocity({x=, y=, z=})` is accepted and does nothing visible**, standing or in a dive (the runtime
  calls its effect on ground movement unproven). Actions are refused outside the game update: call them from
  `hd2.after`.
- `hd2.pickups()` and `hd2.entities.list()` are catalogues of definitions, not of things in the world; no call that
  spawns a pick-up or moves the player was found in the runtime on HellForge (`hd2.hellforge.unavailable` lists
  `custom_stratagem` and `enemies`).
- LLtM 0.5.1 is loaded (`LinksLinkToMemory.status`): a memory workbench driven by `commands.txt` in
  `%LOCALAPPDATA%/LinksLinkToMemory`. That folder, and with it the `hd2_*` script kit with the spawner, does not
  exist in the HellForge prefix; the kit's addresses are retail's.

