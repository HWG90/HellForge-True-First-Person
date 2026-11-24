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
