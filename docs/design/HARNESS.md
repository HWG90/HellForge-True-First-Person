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
`up NAME`, `look DX DY`. Names: letters, digits, `shift ctrl alt space tab esc f9 f10 f11`.

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
  another application's window. Not kept. The NEST panel's rows, and stratagem codes, stay checks for a person; do not
  drive the desktop pointer blind.
- 2026-11-23: the NEST panel CAN be driven from inside the game's process: the host takes the pointer position from
  the click message itself (overlay-host `src/host.cpp`), so `press X Y` in a `drive.py` script posts
  WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_LBUTTONUP at a client pixel. A window capture is 1430x871 for a 1280x720 client:
  the client's corner is at (75, 71) of the capture (shadow margin, border and title bar). With the panel at its
  default place: tabs at client y 151 (First person 430, Bob and sway 513, Locked view 595); on the Locked view tab
  the checkboxes are at x 402: Camera lock 209, Field of view lock 234, Zoom when aiming 284, Animated arms 384,
  Weapon put-away 452, Smooth eye 477.
