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
