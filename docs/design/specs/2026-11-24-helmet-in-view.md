# The helmet in the view during a weapon swap (2026-11-24, round 13)

Seen in the game on 0.6.0-feel.49, in two sessions with the same script: view on (F11), look down 250 px, wait 0.7 s,
look up 250 px and at once key 2 (to the sidearm), a capture 0.35 s after the key. Both captures show the player's own
helmet from behind, the left shoulder and the back, filling the lower half of the screen:
`~/.cache/tfp-feel49b/03-swap-mid.png` (log `~/.cache/tfp-shots-baseline.modlog`) and
`~/.cache/tfp-feel49r/03-swap-mid.png` (log `~/.cache/tfp-shots-putback.modlog`).

**This round changes nothing the player sees.** The cause is established in one half and not in the other, so nothing
was mended. The log now says what was missing to settle the other half in one game run. Nothing here was run in the
game.

## What the logs say

Times are the log's (the captures' file times are five hours behind: 06:26:53.965 is 11:26:53.965).

The first swap of the baseline session, tick by tick (`Hold:`, `State:` and `Trace:` lines; the eye is given in the body
root's frame: sideways, forward, up):

| Time | Camera | What happens | Eye shown |
|---|---|---|---|
| 11:26:48.713 | 6 | View on, LAS-5 Scythe. `Feel:` a second later: `head=active:59` | (0.000 -0.123 1.720) |
| 53.521 | 6 | Mask 2: the swap has begun, the hold keeps the native view (`holding=1`) | the same, 0.05 m of the look's own |
| 53.721 | 6 | `Feel:` `head=active:3` | (0.000 -0.123 1.702) |
| 53.855 | 4 | "hold ended: the gun has left the hands", the exit is acknowledged, "left the native view" | (0.000 -0.123 1.702) |
| 53.889 | 4 | "swap (weapon identity changed)", wielded 493 to 495 | (-0.001 -0.124 1.703) |
| 53.923 | 4 | | (-0.001 -0.125 1.703) |
| 53.955 | 4 | The glide to the sidearm's eye begins, 0.42 m: retail's (over the 0.2625 m gate), `taken=jump` three times | (-0.001 -0.125 1.703) |
| 53.989 to 54.154 | 4 | The filter carries up to 0.295 m and lets it out | -0.114, -0.087, -0.012, 0.051, 0.111, 0.164 forward |
| 54.189 | 6 | Native view again, LAS-58 Talon; its eye is (0.000 0.214 1.665) | (0.038 0.210 1.624) |

The capture was asked for 0.35 s after the key (about 53.86) and its file was closed at 53.965, so it was taken between
53.86 and about 53.93: one of the first three head-camera frames. The second session is the same to the frame (left the
native view 11:23:59.817, file closed 59.913, eye (-0.001 -0.123 1.702) until 59.916).

### Where the eye was: proven

- In those frames the eye shown is the Scythe's own eye, where the native view had it: 0.123 m behind the body's root
  and 1.70 m up. That is by design. Through a swap the helper holds the native view's eye and glides it to the incoming
  weapon's (`src/camera_helper.c`, `eye_delta`; `docs/design/specs/2026-11-23-swap-glide.md`), and the first three
  head-camera frames had nothing to glide to yet.
- The head camera's own eye, standing, is (0.03 0.28 1.61) in the same frame (11:27:10.347 and 10.381, two head-camera
  frames with no eye held). It is the head node plus 0.18 m forward and 0.04 m up, so the head node is about
  (0.03 0.10 1.57).
- So the eye was 0.22 m behind the head node and 0.13 m above it, looking level. A helmet seen from there is what the
  captures show (its top at the middle of the screen, about 0.3 m away by its width on the screen).
- The near plane was 0.100 m (`View:` `near=0.100`). It is not the near plane: the helmet is beyond it and would be
  drawn at any value the mod uses.
- The filter and the glide did not put the eye there. The eye did not move at all in those frames (steps of 0.0004 to
  0.0013 m); what the filter carried later (0.295 m at most) kept it behind the head for four more frames.

The Scythe's eye is behind the head node whenever that weapon is up; that is the game's own first-person rig, and it
only shows when the body is drawn. The Talon's eye is 0.11 m ahead of the head node: a swap away from the sidearm
starts with the helmet behind the camera.

### Whether the head was hidden: not proven

The body was drawn in that frame: the captures show the helmet, a shoulder and the back, which are the three things
head visibility takes out of the camera's sight. Why it was drawn is what the log of feel.49 cannot say. It carries
one number of head visibility a second (`Feel:` `head=active:N`, all rows the module owns, shadow rows included) and
nothing at the tick.

What that number does say, in both sessions and in the four soaks:

- Head visibility never stopped with a reason and never went unavailable: no `FPV head visibility:` line in either
  capture session. (Soak 24 has one, at 08:47:07.999, "Head SDK world changed", at the first native entry after a
  world change; there the module owned 34 rows within the first second, so a new body is not always like the first
  F11.) Its in-game checks passed at 11:26:48.747, five seconds before the swap. It is not that the module was
  not ready yet.
- With the Scythe up it owns 59 rows until the first time the view leaves the native view, and 90 every time after.
  With the Talon 71. With no gun on the rig 3 before that first time (11:26:53.721, 11:23:59.682, soak 26 at
  11:08:35.453, soak 24 at 08:27:30.479 after the view was switched on a second time) and 34 every time after, in
  either camera.
- 59 is 3 and 56, 90 is 34 and 56, 71 is 34 and 37. So there are 31 rows of the body that the module does not own from
  F11 until the view first leaves the native view, and owns from then on. The first leave of a session (and of a new
  body) is not like the later ones.
- The captures before the swap (`01-hip.png`, `02-look-down.png`) show no helmet, with the eye 0.22 m behind the head.
  So through the first native view the head was out of the camera's sight all the same.

What follows from the code, not from the log (`src/head_visibility.c` `apply`): the module takes a row only when it
finds the pass on and switches it off itself. A mesh that is hidden already when the module first looks is not taken.
31 rows not taken and not drawn means they were hidden by something else when the view came on: the game's own doing
in its first-person view, as it seems. When the game switches them back on, the module finds meshes it last saw hidden
now drawn. Inside the first 0.25 s of a new view, weapon or aim state it hides them at once, on its next tick (once
for each pass in that state); outside it leaves them drawn until the state changes (retail's rule, item 58 of
`docs/translation/RETAIL-SMOOTH66-INVENTORY.md`).

Three readings fit everything above, and the log cannot tell them apart:

1. **One frame, every leave.** The game switches the body back on when the native view ends, after the mod's tick of
   that frame, and the module hides it on the next tick. One frame is drawn with the body in it. It would happen on
   every swap, grenade and stim, and show whenever the outgoing weapon's eye is behind the head.
2. **The first leave only.** The same, but only the first time, when the 31 rows are not the module's yet; later the
   game's switching on finds them owned and something about that goes differently.
3. **Longer than a frame.** The module leaves the body drawn for part of the head-camera stretch (the game switches it
   on a second time in the same state, or the part count is not what is assumed here).

The two later captures of the baseline session do not help. `13-swap-200.png` was taken while the native view was
still held. `14-swap-500.png` was taken 0.17 to 0.25 s after the view left, in a swap away from the sidearm: the eye
was still at the Talon's, ahead of the head, so a drawn helmet would have been behind the camera.

## How often the conditions hold

What can be counted in the logs is the eye, not the head. `Trace:` rows in the head camera after "left the native
view", body upright (eye more than 1.5 m up the root's own axis), eye shown behind the body's root along the level
view direction, which is 0.10 m or more behind the standing head node. That estimate takes the standing head node for
every upright frame.

| Log | Leaves of the native view | Eye behind the head in the head camera | Swaps | Of them | Frames each | Deepest |
|---|---|---|---|---|---|---|
| soak 24 (holds soak 23) | 192 | 45 | 108 | 34 | 6 (median), 16 at most | 0.22 m behind the root |
| soak 25 | 147 | 49 | 91 | 38 | 7, 16 | 0.32 m |
| soak 26 | 56 | 12 | 30 | 9 | 6, 16 | 0.17 m |
| baseline captures | 7 | 4 | 4 | 4 | 6, 7 | 0.13 m |
| putback captures | 5 | 2 | 2 | 2 | 7, 7 | 0.12 m |

About a third of all swaps in the soaks, and every swap away from the Scythe when standing still, have the eye behind
the head for six or seven frames (0.2 s) of the head camera. Whether the helmet shows in them is the open half:

- reading 1: the first of those frames, in each of them (about a third of the swaps, one frame each);
- reading 2: once each time the view is switched on, and once a life (the first leave after F11 had the eye behind
  in soaks 24, 25 and 26 and in both capture sessions, for 8, 7, 16, 7 and 7 frames; soak 24's second F11 did not);
- reading 3: up to all six or seven frames, in each of them.

## What was added

No rule of head visibility, of the camera or of the filter was touched. Three things, all counting what the code
already reads and writes:

- `src/head_visibility.c`, `tfp_head_watch` (beyond retail): for the last update, of the meshes the module wants out
  of the camera's sight (head, torso, shoulders, backpack, the rig's shoulder pads): how many it found drawn that it
  had never seen, how many it found drawn that it last saw hidden (and how many of those are the head's), how many it
  found drawn that it had left drawn before, how many camera passes it switched off, how many it left drawn, and how
  many rows it gave back in a stop. No read, no write and no engine call is made for it.
  `tests/head_visibility_test.c` checks it on all 32154 operations of retail's vectors, twice: every camera pass
  switched off is one `set ... F default` of the scripted world, and every mesh found drawn was either switched off
  or left drawn.
- `src/head_log.h`: the "Head:" line, the arithmetic and the words alone, as `src/gate_log.h` is
  (`tests/head_log_test.c`, `tests/run-head-log.sh`: the 52nd script).
- `src/wire_engine.c` `tfp_wire_engine_head_tick` and `src/mod_view.c` `head_note`: the line is written in the feel
  build, each tick after the update, when one of these holds: what the module owns of head, torso or shoulders
  changed; it found meshes drawn again; the number it leaves drawn changed; it stopped or started while the view was
  on; it gave rows back.

```
Head: kind=4 active: owns head 9, torso 14, shoulders 8 (34 rows; was head 0, torso 0, shoulders 2); found drawn 0 new, 29 again (9 of the head), 0 still; switched off 29, left drawn 0, gave back 0; so far drawn again on 1 ticks (29 meshes), view on without hiding on 0 ticks
```

- `kind`: the camera kind of the snapshot the module took in that update (0: it stopped before taking one).
- `owns`: rows it owns now by part, and all rows (shadow rows included, the number of `Feel:` `head=active:N`).
- `found drawn`: new (never seen: the first update on a body), again (hidden when it last looked), still (left drawn
  before too).
- `so far`: ticks with meshes drawn again and those meshes since the mod started; ticks with the view on and the
  module not active. These are the counts to compare between sessions.

The numbers in the example are made up; the split of the 34 rows is one of the things the line is there to tell.

About a line or two for each leave and each return of the native view (the rig's shoulder pads come and go with the
rig), more when something is wrong.

## To look at in the game

One session with the capture script as it is (the helmet came in two sessions of two), then the same swap away from
the primary three more times in the same life, each with a burst of captures from 0.30 to 0.55 s after the key
(`tools/harness/burst.py` takes about three a second; several runs at shifted delays cover the 0.2 s). Then read the
`Head:` lines between "left the native view" and the next "entered the native view":

1. **After F11**, the first `Head:` line: `owns head 0, torso 0` with the Scythe up says the body was hidden by the
   game and not by the mod (the 31 rows). `owns head N` with N above 0 says the arithmetic above is wrong, and the 31
   are something else.
2. **`N again` on a `kind=4` line** at the tick after "left the native view", `switched off` the same number,
   `left drawn 0`: the game switched the body on and the module hid it a tick later. That tick's frame before it had
   the body in it. On the first leave only: reading 2. On every leave: reading 1. `so far drawn again on N ticks`
   against the number of leaves says which without reading every line.
3. **`left drawn N` above 0 on a `kind=4` line**, and how many ticks until a line says 0 again: reading 3. `(M of the
   head)` on the line before says whether the helmet is among them.
4. **`kind=4 idle` or `unavailable` with `gave back N`** while the view is on, and `view on without hiding on N
   ticks` rising: the module stopped in the head camera (its own conditions: not on foot by the action state, the
   camera helper not armed, the snapshot neither safe nor a swap). None of the old logs has a sign of this; the line
   would show it.
5. **No `again`, nothing left drawn, the module active with `owns head N`, and the helmet in a capture all the
   same**: the pass bits are not what decides the frame (the bit is read when the frame is put together, before the
   module's write; or the unit is drawn another way). Then the mend is not in head visibility at all.

What each reading would ask for, none of it built:

- readings 1 and 2: hide before the frame is put together and not a tick later. The module's own update a second
  time from the render hook is the existing checked path, but an engine call from there is new and must be shown
  safe first; or the near plane taken beyond the helmet (0.45 m) for the head-camera frames in which the eye is
  behind the head, which hides nothing the head camera shows of the arms only if they are further out, to be looked
  at; or the eye taken to the head camera's own point for those frames, which is a jump of 0.4 m the glide was built
  to avoid.
- reading 3: the 0.25 s and once-per-state rule of the module (retail's) for a view the lock holds.
- reading 5: the camera, not the body.

## Not done

- Nothing was mended, for the reason above.
- The head node's place is not in the log. The count of "eye behind the head" takes the standing head for every
  upright frame; crouched and prone frames are left out. A `Trace:` field for the head camera's own eye would make
  the count exact; the row is read by the harness and was left as it is.
- The "Head:" line is written by the tick. It cannot say whether a frame was drawn between the game's switching on
  and the module's switching off; the capture says that.
- Nothing was looked at in the game.


## Reading the `Head:` lines (added after the review)

Read them from "holding the native view through an action" on, not only from "left the native view": the swap
begins about 0.37 s before the leave, and a body drawn during that hold is a fourth reading the capture cannot rule
out.

## Round 14: what soak 27 settled, and the veil

### What the log of feel.50 says

One game session (`~/.cache/tfp-soak-27.modlog`, `--logged b70333e`): ten captures from 12:41:45 to 12:42:14
(`~/.cache/tfp-feel50/`), a Stable soak (seed 15, to 12:56:28) and a Bodycam soak (seed 14). The captures' file times
are five hours behind the log's.

- 138 leaves of the native view (6 in the captures, 61 in the first soak, 71 in the second). 147 `Head:` lines.
- **At 137 of them the game switched the body back on, every time the same 31 meshes, 5 of them the head's**, and the
  module found them on its next tick and switched all 31 off: the `Head:` line of the tick after "left the native view",
  0.027 to 0.035 s later (one tick; `View:` says 30 frames a second in 1707 of 1728 seconds, so one tick is one frame).
  The one leave without it (12:45:24.412, "Native camera request or firearm changed") was back in the native view on
  the next tick.
- It is every leave, not the first one only: "so far drawn again" counts 146 ticks and 4265 meshes at the end
  (137 × 31, and 9 ticks of 2 meshes that are not the head's: 8 of them at the start of a hold in the native view).
- `still` and `left drawn` are 0 on all 147 lines, and `gave back` too. Retail's 0.25 s rule never came into it: a
  leave changes the camera kind, so the module's next update is inside the 0.25 s of a new state and hides at once.
- No tick re-showed the body a second time inside one head-camera stretch: after the hide, nothing until the next
  leave. A grenade is like a swap.
- The order inside the leave tick: the controller's update asks the game for the native camera's exit ("Owned native
  camera exit acknowledged"), and head visibility's update, later in the same tick, still finds all 31 hidden. The
  game switches them on in its own update after the tick (HellForge's `on_game_thread` runs "before the game's
  update", `hellforge_mod.h`). The frame of that update is drawn with the body; the next tick hides it.
- Where the eye was in that frame (the `Trace:` row of the leave tick, in the body root's frame): 37 of the 146 upright
  and behind the root (as far as 0.39 m), which is 0.10 m or more behind the standing head; 2 just in front of the
  root, inside the head; 60 more than 0.10 m in front of the root (the helmet behind the camera or around it); 40 with
  the eye lower than 1.5 m (crouched, prone, diving), where the standing head says nothing; 7 without a row.

**Reading 1 holds: one frame at every leave.** The helmet shows in it when the eye the swap holds is behind the head:
every leave with the LAS-5 Scythe up while standing.

Capture 10 (`10-grenade.png`, the helmet from behind, the rifle at the lower left) is that frame. The grenade key went
at about 12:42:13.67 ("holding the native view through an action"); the gun left the hands and the view left the native
view at 14.042; the capture was asked for 0.4 s after the key and its file closed at 14.134; the `Head:` line of 14.076
found 31 meshes (5 of the head) drawn again and switched them off. The eye of the 14.042 frame was the Scythe's
(0.003, -0.121, 1.720): 0.22 m behind the head and 0.13 m above it. The four swap captures missed the frame by timing:
02 was taken while the native view still held (file closed 59 ms after the leave), 03 and 04 well after it (146 and
148 ms), 05 at the leave of a swap away from the sidearm, whose eye is in front of the head.

### Why the body is not hidden in that frame instead

The mend asked for first is to switch the body off in the frame the game switches it on. The game does it in its update,
after this mod's tick; between it and the drawing of the frame there is one place this mod runs on the game thread: the
primary draw (`src/render_slot.c`), where the arm drive writes the rig's joints. Head visibility's writes go through
the engine's Lua functions (`Unit.set_mesh_visibility` through `src/engine.c`), and `src/engine.h` says of every such
call: from the game thread "while the engine's own script is not running (HellForge's on_game_thread tick)". Nothing
proves that of the primary draw; the arm drive there calls native functions and no Lua. Calling the pass-mask setter
directly (the retired controller did, from the same hook) is not head visibility's checked path. So it is not done.
Re-applying on every tick does not reach the frame either: the hide already comes on the very next tick.

### The veil (`src/camera_helper.c`, beyond retail)

The frame cannot be kept from having the body in it, so its eye is kept from being behind the head. From the first
frame of the lock that is not the native view after one that was, until the tick whose head visibility update found the
body drawn again and switched all of it off (at most two ticks; at once when head visibility stops), a head-camera frame
of a body on foot whose eye is less than 0.10 m (`TFP_CAMERA_VEIL_CLEAR`) in front of the head node, along the level
view direction, is handed to the game with the eye moved forward to 0.10 m. Height, side and rotation stay. With the
Scythe that is 0.32 m forward for two frames, then the swap's held eye again: everything far away moves by a few
per cent of its size for about 66 ms instead of the helmet covering half the screen. Eyes already in front (the Talon's)
are not moved.

Two frames, not one (the review of round 14): in all 131 leaves of soak 27 with a `Trace:` row, the first head-camera
frame comes before the tick that logs "left the native view". That frame is drawn with the body still hidden; the
tick finds nothing drawn again and the veil stays open; the game switches the body on in its update after that tick,
the next frame is the one with the body, and the tick after it closes the veil. So the first veiled frame does not
need the move. It is moved all the same: from the camera hook it looks like the first frame of a leave whose tick came
first (the other order, `tests/camera_veil_helper_test.cpp` test 3), and there that frame is the one with the body.
Nothing the hook reads tells the two apart, and guessing wrong brings the helmet back.

Only the position handed to the game in that frame changes. The eye the swap holds and glides (`eye_delta`, the glide),
the continuity, the damping and the trace row are the eye's before the veil, so the next frame goes on from them as
it did; `tests/run-camera-glide.sh` and the other camera scripts pass unchanged. The trace rows therefore show the
held eye in the veiled frame; the `Head:` line counts what the veil did.

Why it is safe: no engine call is added, nothing is written to any mesh, unit or game memory. The camera helper already
replaces this argument of the game's camera setter in every frame the lock holds; the veil changes three floats of it
before the same call, inside the same policy lock. The hook and the tick are both the game thread ("Hook: camera first
fired on thread 660 ... (the tick is thread 660)"). It needs the lock (no veil in the game's own view), head visibility
active (armed by the tick, whatever rows of the head it owns: until the first leave of a session it owns none, and that
leave drew the head all the same: 12:41:45.163 "owns head 0", 12:41:50.071 31 meshes, 5 of the head), and a finite eye
no more than a metre from where it was.

The tick (`src/mod_view.c` `head_note`): after the runtime's update, `tfp_camera_veil_from_head` with the tick, which
calls `tfp_camera_veil_tick` (closes it when the update found meshes drawn again and left none drawn) and
`tfp_camera_set_veil` (armed while the view is on and the module is active; a tick that cannot be read switches it off).
The first build armed it only while the module owned rows of the head, which left out the first leave of every session
(the review of round 14). Tests: `tests/camera_veil_helper_test.cpp` (`tests/run-camera-veil.sh`, the 53rd script) on
the fake game memory of `tests/camera_smooth_test.cpp`: not armed, nothing changes; the Scythe's eye moved to 0.10 m in
front for both frames of a leave in the order soak 27 has (the first frame before its tick) and back to the held eye
after the closing tick; the other order (one frame); no re-show (closed after two ticks); an eye in front already;
switched off while open; a native frame while open (counted among the frames, not behind); no lock, no veil; what the
tick hands it (armed on a kind-6 tick that owns no head row, closed by the tick that found the body and left none of it
drawn, not by one that left some drawn, not armed with the view off, the module not active or no tick). Off foot at a leave the eye is not moved and the frame counts as left behind
the head; a new world (the helper ended and begun again) does not open a veil from the old world's last native frame
(the first build kept that kind across worlds and could open a spurious veil of up to two frames).

### The `Head:` line now ends with the veil's counts

```
...; view on without hiding on 0 ticks; veil: 3 leaves, 4 frames, eye moved clear on 2, left behind the head on 0
```

- `leaves`: leaves of the native view the veil opened at; `frames`: frames drawn while it was open (two a leave: the
  first head-camera frame, before the tick that sees the leave, and the frame with the body); `eye moved clear`: of
  them, frames whose eye it moved.
- `left behind the head`: frames drawn while it was open, other than the native view, whose eye was not proven 0.10 m
  in front of the head (a body off foot, a frame the helper did not present). On foot it reads 0. A rise of it is a
  line by itself. Native frames are left out (the review of round 14): a leave that goes straight back to the native
  view within a tick draws native frames while the veil is open (soak 27: 12:42:10.736, 12:42:36.644, 12:45:24.412),
  and those are the game's own camera, which the veil never moves; they are counted in `frames` only.

### What only the game can show

- That a game session's `Head:` lines read `left behind the head on 0` (on foot) with `leaves` close to the number of
  "left the native view" lines, and `frames` about two a leave (the first head-camera frame comes before the tick
  that sees the leave; one where it does not; more where a leave goes straight back to the native view).
- Whether the body shows in a native frame drawn while the veil is open (a leave straight back within a tick): the
  veil does not move those frames.
- That the helmet is gone from the frame of a leave: the capture script of feel.50 again, the grenade (shot 10) at
  0.40 s, and swaps away from the Scythe with a burst of captures around the leave (`tools/harness/burst.py`).
- How the two-frame step forward looks and feels next to the helmet it replaces. If it shows, the distance (0.10 m) is
  one constant; the head camera's own eye is 0.18 m in front of the head.
- Whether looking steeply down in that frame shows the chest (the veil only moves the eye forward of the head; the
  body is drawn below it all the same). The old frame showed the helmet there too.
- Crouched and prone leaves (40 of 146): the head node is the body's own, so the rule is the same, but no capture
  has looked at one.

## Round 19: what the veil shows is logged

Round 18 compared feel.50 and feel.51 (`~/.cache/tfp-soak-27.modlog` against `-28`, `-29` and `-30`). The veil moves the
eye handed to the game in a frame or two at most leaves of the native view (soak 30, Stable: 66 leaves, 73 frames
moved; soaks 28 and 29, Bodycam: 90 and 88), by `TFP_CAMERA_VEIL_CLEAR` less where the held eye was: 0.32 m with the
LAS-5 Scythe's eye 0.22 m behind the head. No log and no judge saw it:

- `veil_apply` (`src/camera_helper.c`) runs after `smooth_present` has written the frame's Trace row and after
  `last_head_world` is kept, so every row holds the eye before the veil, and the judges measure that eye.
- The `Head:` line counted the moved frames but did not print `largest_m`.

So a step of up to 0.32 m forward and back again on screen was judged as no step. Nothing about what is shown changed
in round 19; the log now says it:

- **`veil=` on the Trace row**, the last field: the move as a vector in the world, `veil=(x y z)`, or `veil=0` where the
  veil moved nothing. The setter writes it into the frame's own row (`trace.frames[(trace.head-1) % TRACE_RING]`, only
  where that row's tick is this frame's) right after `veil_apply`. `eye_world` stays the eye before the veil, which is
  what the continuity was given; the eye shown is `eye_world + veil`.
- **The `Head:` line** ends `..., left behind the head on N, largest move 0.321 m, opens at the first head-camera
  frame` (or `opens at the tick that sees the leave (veil_second.txt)`). Neither field makes a line by itself.
- **The judges** (`tools/harness/matrix.py measure`, so `soak.py` and `matrix.py` alike) measure the shown eye. A frame
  over the limit with a veiled frame among its three, whose eye before the veil is under it, is **the veil's**: listed
  as such with `veil 0.320` on its line, and counted apart. Where the eye before the veil is over the limit as well, it
  stays the eye's own or the filter's, judged on that eye. The veil's frames do not set "largest", fail a matrix phase
  or flag a soak action: `largest_jump_m` is the eye's own (measured without the veil's move) and `largest_veil_m` the
  veil's, printed beside it (`jump 0.012 m ... (veil 0.320 not counted)`; soak: `...; 0 the filter's), largest
  0.041 m; 3 the veil's, largest 0.320 m, not counted; ...`). A soak has some 70 leaves, and each would otherwise have
  marked its window bad and read 0.32 m as the run's largest. Rows without `veil=` (every log before round 19) and rows
  with `veil=0` are judged exactly as before (the same pass, largest and whose).
- `tools/harness/replay.py` reads `veil=` into `row["veil"]` and uses nothing of it: it replays the filter, which
  never saw the veil.

### A test switch: `veil_second.txt` (off by default)

The analyst of round 18 proposed opening the veil at the tick that sees the leave instead of at the first
head-camera frame. In the order soak 27 traced in all 131 leaves (the first head-camera frame drawn before that tick
with the body still hidden, the frame with the body after it) that moves only the frame that draws the body: one
frame of 0.32 m forward and back instead of two. With the file `veil_second.txt` beside the DLL
(`tfp_camera_set_veil_second`):

- the hook only notes the leave; the next tick opens the veil if it did not itself find the body drawn again and
  switch it off; the tick after that closes it whatever it found, unless it saw another leave. A leave is taken by
  one tick; switching the veil off and the helper's begin and end drop a noted one.
- In the other order (the tick before the first head-camera frame; not seen in soak 27) that frame is the one with the
  body, and it is not moved.
- The log says it once at the start (`Test: veil_second.txt: ...`) and on every `Head:` line.

### A second leave while the veil is open

A leave that goes straight back to the native view within a tick and leaves again (soak 27 had three) is a second
leave while the veil is open. The tick that sees it keeps the veil open, counted as the first of two ticks again
(unless it found the body drawn again and switched it off), so the frame the game draws that leave's body in is veiled
too, and the veil closes at the tick after; the leave is counted in `leaves`. Both ways of opening take it the same way.
Before, with `veil_second.txt`, the open veil closed at that tick and left the leave noted, so the next tick opened it
again a tick late and moved the frame after it 0.31 m for a whole tick; and by default a second leave did not extend
the veil, so its body frame was drawn unveiled (on main as well).

### What only the game can show

- How much of a session's flagged frames the veil is: a soak on this build, judged with `soak.py --log`, now counts
  them; the size per leave is on the Trace rows.
- Whether one veiled frame (`veil_second.txt`) still hides the helmet at every leave: that needs the order of soak
  27 to hold in the game, and a burst of captures around the leave (`tools/harness/burst.py`).
- Whether the step forward and back reads as a jolt at all next to the helmet it replaces, with one frame or two.

## Round 26: a swap in the middle of a dive

feel.52's Stable soak (`~/.cache/tfp-soak-34.modlog`) has 117 veil moves: 71 of 0.1 to 0.2 m, 7 of 0.3 to 0.4, one of
0.495 and three over 0.5 (0.548, 0.534, 0.527). All four over 0.4 m are the two frames of two leaves, both a weapon
swap (BR-14 Adjudicator to P/40-K Bolt Pistol) begun in the middle of a dive or lunge, the view leaving because the gun
left the hands (mask 0x02): actions 118 (02:16:09.838) and 173 (02:18:33.429).

### What the rows say

- **The eye is not held still; the head goes on.** The swap holds the native eye as a difference from the head
  camera's eye in the body root's frame (`eye_delta`), so the held eye moves with the root (0.14 to 0.15 m a frame
  after the leave of 118, the root's own). In the dive the game's own camera stays high above the root (1.065 m above
  it, 0.289 m in front at the leave) while the body pitches forward under it: the head node is 0.737 m in front of the
  root, 0.446 m in front of the eye (0.548 - 0.10). In 173 the head is 0.458 m in front of the root, 0.433 in front of the
  eye, which is 0.025 m in front of the root and 1.076 m above it. Neither the analyst's "held still while the body
  carries the head on" nor the sceptic's "the native eye fell behind (sock -0.64)" is quite it: `sock` is the weapon
  socket against the game's (`src/arm_drive.c` stats.draw.sock_m), not the eye.
- **The old veil carries the eye the whole way.** `veil_apply` moved it to 0.10 m in front of the head along the level
  view whatever the height: 0.548 and 0.527 m in 118, 0.534 and 0.495 m in 173, and back in one frame after the second
  tick. The judge (`tools/harness/matrix.py measure` on the rows) sees 0.548, 0.633, 0.494 and 0.535 m of change of
  step in 118 and 0.537, 0.570, 0.456, 0.496 in 173; two of them (0.633 and 0.494 at .872 and .905) were even counted
  as the filter's, the eye before the veil changing by 0.06 to 0.08 m there.
- **Where the head is in height is not logged.** From the head camera's eye in the dives of soak 25 (10:17:37: 0.74 to
  0.78 m in front of the root, 0.53 to 0.56 m above it, less c.forward 0.18 and c.up 0.04) the head in 118 is about 0.51
  m above the root: 0.55 m below the held eye. In 173 the wanted eye rose 0.52 m and went 0.24 m back in the 330 ms
  after the leave with the held difference unchanged, so the head did: standing again by then, it was about 1.0 m
  above the root at the leave, level with the eye. 118 is a dive with the head under the eye; 173 a lunge with the head
  in front of it.
- **The 0.4 s after.** At .938 the incoming weapon's eye is known and the hold glides to it: the wanted eye's part
  in front of the root goes 0.304, 0.536, 0.683, 0.770, 0.824, 0.857 m, each step 0.62 to 0.65 of the last, which is
  retail's 70 ms glide (src/camera_glide.h: a way over TFP_EYE_GLIDE_FAR_M, here about 0.6 m, is glided on retail's
  rate under the lock as well), and the continuity takes its first step up as a jump and lets it out (carried 0.31 to
  0.65 m, the soak's float 0.295 and 0.334 m). It is smooth: after .938 the judge finds no frame of 0.06 m or more in
  either window (the largest 0.050 m at 10.272). It is the round 19 path doing what its spec says for a long way, from an
  eye the game put high above a diving body. **Not changed**: nothing in the rows calls for it, and the glide's gate was
  measured against 96 made-up swaps.

### What changed (`src/camera_helper.c` `veil_apply`, `src/camera_helper.h`)

Root speed was not used as the sign: the lunge's root moves 2.1 m/s, a sprinting standing swap's as fast, and what
makes the move large is where the head is against the eye, which the veil has in hand.

1. **The head below the view.** The head is taken as a box from 0.15 m (`TFP_CAMERA_VEIL_HEAD_M`) behind its node to
   `TFP_CAMERA_VEIL_CLEAR` in front of it, 0.15 m up, down and to either side. An eye with all of that box below the
   bottom edge of the view has no head in its frame: the bottom edge is a plane through the eye holding the camera's
   right axis, at the view's pitch less half the vertical field of view and 5 degrees (`TFP_CAMERA_VEIL_FOV_SPARE`).
   The veil moves the eye forward only as far as that takes (5 mm past it), where that is less than the whole way.
   The field of view is the wider of the game's and the one shown (`tfp_fov_lock_stats`), the widest of the last two
   seconds, handed over each tick in `head_note` (`tfp_camera_set_veil_fov`); not known, 120 degrees, the most the
   settings allow. Looking down steeply (the edge within a degree of straight down) nothing is below the view and the
   rule is the old one.
2. **Too far: no move.** A move over 0.40 m (`TFP_CAMERA_VEIL_MOST`) is not made. That is no weapon's eye behind the
   head (the LAS-5 Scythe's needs 0.32 m, the prone swaps of soak 34 0.33 to 0.34) but a body that has left the camera,
   and the move itself is the lurch. The frame keeps the held eye and counts as left behind the head (so the `Head:`
   line comes) and as `far`. Accepted: a frame of the helmet from 0.30 m or more behind it, in this case only. A part
   of the way was not taken: for a head level with the eye (173) every point between the held eye and 0.10 m in front
   of the head has the helmet larger in the frame than the held eye has it, and from 0.08 m behind the node the eye is
   inside it; and moving up and forward together takes about as far (0.53 m).
3. **Let out, not taken back.** After the veil closes, its last move is let out over 300 ms
   (`TFP_CAMERA_VEIL_LET_OUT_MS`, `tfp_camera_veil_let_out`: from rest to rest, at most 6 (dt/T)^2 of the move a frame,
   0.058 m for 0.40 m at 50 ms a frame, under the judge's 0.06) instead of in one frame: the body is hidden by then, so
   the eye need not be clear, and the step back was the lurch's other half. Only head-camera frames of the lock on
   foot are let out into; any other frame, and a new leave, ends it. The Trace row's `veil=` carries the let-out too, so
   `eye_world + veil` stays the eye shown and the judges see it.

**How "never inside the head" is checked.** In the code: a move is either the whole way (the eye `TFP_CAMERA_VEIL_CLEAR`
in front of the node, outside the box) or one after which the whole box is below a plane through the eye, which the eye
cannot be inside; a frame that gets neither keeps the held eye, which is then more than 0.30 m behind the node. In the
tests (`tests/camera_veil_helper_test.cpp` 17 to 20), apart from the helper's arithmetic: in every veiled frame the shown
eye is checked against the box (`inside_head`: behind the front, in front of the back, within 0.15 m sideways and in
height) and for being clear (`clear_of_head`: 0.10 m in front, or the angle of each of the box's eight corners from the
view axis, in the plane of the view's forward and up, below minus half the field of view, the spare left out).

### Numbers (the scenes of the tests: the two leaves rebuilt from their rows; field of view 90 degrees as the user's
`Fov:` lines, or not known)

| Scene | Before: moves, judge's measure | After, 90 degrees | After, not known |
|---|---|---|---|
| 118 dive, 8 / 16.7 / 33.3 / 50 ms | 0.553 to 0.582 m; 0.562 to 0.637 m | moves 0.099 / 0.088 / 0.067 / 0.046 m; measure 0.110 to 0.112 m | moves 0.290 to 0.269 m; measure 0.300 to 0.333 m |
| 173 lunge, same | 0.501 to 0.531 m; 0.537 to 0.557 m | no move (far, 2 frames); measure 0.010 to 0.052 m, the bare eye's own | the same |

The measure "bare" (the same scene without the veil) is 0.011 / 0.023 / 0.045 / 0.066 m in 118, so what the veil adds
falls from about 0.55 m to 0.05 to 0.10 m at 90 degrees. Made-up dives (the head 0.30 to 0.70 m below the held eye and
0.30 to 0.55 m in front of it, fields of view of 80, 90 and 120 degrees, every frame time: 144 scenes): 116 moved less
than the whole way, 19 kept the held eye, the largest move 0.396 m; never inside the head, always clear or counted far.
The standing Scythe swap (test 20) moves the whole 0.32 m as before, and lets it out after. The soak's `largest_veil_m`
(0.570 m on feel.52) should fall to the largest kept move: at most 0.40 m and the eye's own change, about 0.34 m for the
prone swaps of soak 34 unless their head is below the view too.

### Logged

- **Trace rows**: `veil_head=(ahead up)` at the end, in a frame the veil was open for: the head node from the eye before
  the veil, along the level view and up (`0` otherwise). That settles the head's height that this round had to estimate.
- **`Head:` line**: `...; veil kept short: head below the view on N, too far to move on M`, only where either happened.

### What only the game can show

- Where the head really is in a dive and a lunge (`veil_head=`): the 0.55 m below the eye in 118 and level in 173 are
  estimates. If the dive's head is higher, more of those leaves fall under "too far" and show a frame of helmet.
- Whether the bottom-edge rule is right on screen: a burst of captures (`tools/harness/burst.py`) around a swap begun
  in a dive, at the user's field of view; the 5-degree spare and the box's size (0.15 m) are judgement.
- Whether the frame of helmet from 0.43 m behind it in a lunge (173, "too far") shows, and whether it is worse than the
  0.53 m lurch it replaces. If it is, `TFP_CAMERA_VEIL_MOST` is the one number to raise.
- How the 300 ms let-out reads after a standing swap (0.32 m eased back where it used to step back in a frame).

## Round 30: a lone head-camera frame at the release of full aim

feel.53's soak (`~/.cache/tfp-soak-36.modlog`, the first to reach full aim) has six leaves of the native view that are
one head-camera frame long. Each comes 0.22 to 0.35 ms after a native frame of full aim (`quiet=1`), in the same game
frame, at "State: entered the native view" and "State: aim out", with control mask 0 (`Return: ... mask=00000000`),
and the next frame is the native view again. The veil opened at each and moved four of them 0.20 to 0.38 m forward
(`veil_head` 0.10 to 0.28 m in front of the held eye, the head level with it); the native frame after took it back. The
judge read 0.745, 0.506, 0.399 and 0.390 m: the top four veil frames of the soak. The body is never drawn there: the
native view never left.

Neither rule proposed after the soak tells them apart on its own:

- **"Under 2 ms after a native frame."** 1,916 of the 1,940 leaves of soaks 8 to 36 have their first head-camera frame
  that close to the native frame before it (the game evaluates the camera twice in the leave's frame). Keeping all of
  them pending is `veil_second.txt` for every leave.
- **"Out of full aim."** Soak 36 also has five ordinary leaves out of full aim, 16 to 22 frames long: a swap made in full
  aim (07:02:36.769, 07:04:23.133, 07:04:50.374, 07:07:05.517, 07:15:28.508). They differ in the control mask, 2 (the
  gun leaving the hands) at that same tick (`Hold: mask=00000002 kind=4`, `Drop: ... mask=00000002`).

(The Trace rows' `lease=` is the binder's lease when the window is written, not the frame's, so it tells nothing here.)

### What changed (`src/camera_helper.c` `veil_same_frame_leave`, `src/camera_helper.h` `TFP_CAMERA_VEIL_SAME_FRAME_MS`)

A leave noted in a head-camera frame within 2 ms of a native frame of full aim (`native_full_ads`) is decided once that
frame's control mask is read (`owner_status`, a few lines later in the hook). Mask 0: the leave is kept pending
(`veil_pending`), so the frame is not moved, and the tick that sees it opens the veil for the head-camera frames after
it, which moves nothing when the view went straight back. Any other mask: opened at once, as every other leave. Counted
in `TfpCameraVeilStats.held_back` (not on the `Head:` line: that is `src/mod_view.c`).

### Numbers (soak 36 replayed: `replay.py`'s continuity on the logged calls, the veil taken off the frames the rule keeps,
judged with `matrix.measure`; the rule's mask from the last Hold/Return/Drop line written at or before the frame's stamp)

| Frame (the native frame after the lone one) | Before | After |
|---|---|---|
| 07:03:36.728 | 0.745 m, the veil's | 0.012 m |
| 07:07:06.652 | 0.506 | 0.012 |
| 07:04:24.267 | 0.399 | 0.012 |
| 07:02:37.937 | 0.390 | 0.012 |
| 07:15:29.643 | 0.340 | 0.012 |

0.012 m is the continuity letting go of the edge it took (correction 0.25 to 0.44 m held at the native eye). The rule
keeps six frames (the sixth, 07:04:51.476, had no move: its head was behind the eye); the five swaps in full aim and
every leave of soaks 8 to 35 are untouched (no frame of theirs changes). The soak's largest veil frame goes from 0.745
to 0.339 m (an ordinary leave). Tests: `tests/camera_veil_helper_test.cpp` 21 (the release kept, with the view going
back and staying away; a swap in full aim, a same-frame leave out of the hip and a lone frame a whole frame later
veiled at once).

### What only the game can show

- Whether that second evaluation is drawn at all. If it is, it now shows the held eye (the native one) for a frame, 0.25
  to 0.44 m behind the head camera's eye; the body is hidden there, so nothing of the helmet should show.
- A release of full aim with an action owning the view (a mask other than 0 or 2) is veiled as before.
