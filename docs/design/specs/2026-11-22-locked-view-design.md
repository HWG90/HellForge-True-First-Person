# The locked view: design

Written 2026-11-22 for the request:

> Test every possible animation the camera can encounter, and make sure the system passes. Port third person
> animations for certain events into first person compatible counterparts (grenade throw, stim inject, clamber,
> climb, ammo check). When switching weapons or doing an action that requires the weapon be out of our hands,
> create a smooth animated transition for putting away the weapon. For handguns in hip-fire, utilize only one hand.
> When point shooting or ADS, use two hands. Camera & FOV lock: field of view is persistent (and configurable)
> across ALL weapons, actions, transitions. Until deactivated, never leave first person, even on death, follow the
> body to the ground. Smooth pitch and yaw kicks to match body movements / actions like diving, getting ragdolled.
> Standalone menu, no reliance on MCM.

Everything here goes beyond retail (Goose Gadget Kit 0.5.41-smooth.66). Each part is a switch whose off position is
retail's behaviour, so the comparisons with retail's source keep holding.

## What exists (0.6.0-feel.19)

- Two cameras: the game's own first-person camera (request kind 6, on the first-person arm rig) and the head camera
  (kind 4 with the position replaced by a point at the head). [ACTION-CAMERA.md](../ACTION-CAMERA.md).
- The first-person arm rig never animates beyond a pose per weapon and recoil; the third-person body's arms, with the
  same joint names, animate through everything. [HARNESS.md](../HARNESS.md), probe of 2026-11-20.
- The head camera refuses most action masks, every off-foot state and every camera kind but 4. Death, ragdoll and
  several actions therefore show the game's third-person camera.

## Parts

### 1. Camera lock (`src/camera_lock.h`, `src/camera_helper.c` `tfp_camera_set_lock`)

While the view is on, the head camera is kept for every frame of the player's own avatar: any action mask, off foot,
any camera kind. The eye is the head, so it follows the body to the ground. In a camera that is not kind 4 or 6, and
in kind 4 on a body that physics has (thrown or dead), the direction is the last first-person direction. No glide in
from the game's camera: the lock never shows the way from third person. A camera whose record is not the player's
own avatar is left alone: a spectator camera, and the reinforcement cameras (kinds 11 and 10) that follow a death.

Death, as the game does it on this build (session `b08c8327`, 2026-11-22): the camera on the corpse is the ordinary
request, kind 4, still the avatar's, for about five seconds; the body unit, its node graph and the head node stay
the same; the unit root stays where the character died and the skeleton leaves it. Then kinds 11 and 10 with another
avatar's record for about six seconds, in which the game does not call the hooked camera setter at all; then a new
avatar and body, on which the controller arms the helper again by itself. See "Death under the lock" below.

### 2. Kicks (same files)

The head's rotation relative to the view, high-passed: `kick = relative * rest⁻¹`, `rest` following `relative` with a
300 ms time constant. Turning the view with the mouse moves the head with it and gives nothing; a dive, a landing or
a hit moves the head on its own and tilts the view by a share of it (35 % up to 8° on foot; 70 % up to 35° when
thrown or dead). Applied in the view's own frame, so the same code serves both cameras.

### 3. Field of view lock

One configurable field of view, applied on every frame the view is on, whatever the weapon, the action or the camera
kind; aiming keeps its own zoom only when the user leaves "zoom when aiming" on. Built on the existing FOV helper
(`src/fov_helper.c`), which already owns the game's FOV write; the lock removes the per-weapon and per-camera cases.

### 4. Arms that move: the body's arm animation on the first-person rig

The first-person rig's arm joints are driven from the body's arm joints while an action plays, joint by joint
(shoulder, elbow, wrist, fingers; same names on both), in the chest's frame, blended in and out over 120 ms. This is
a copy of the game's own animation, not a hand-made one. Events: grenade, stim, clamber/vault, climb, ammo check,
reload, weapon swap, pick-up, melee. The trigger is the body's animation state (the layers already read for the
action log), not a key.

Open: whether the rig is drawn while the game's request is not kind 6 (it sat at the world origin in the probe), and
whether writing local joint poses each frame survives the rig's own update. Both are measured before this is built;
if the rig cannot be kept, the fallback is the body's own arms shown from the locked eye, with the near clip and the
hidden passes tuned per action.

### 5. Putting the weapon away

A swap or an empty-hands action starts with the weapon lowering out of the bottom of the view (placement offset
eased over 180 ms, down and slightly back), and ends with the incoming weapon rising the same way. It runs on the
existing placement layer (`src/pose_policy.c`), driven by the action mask's edges.

### 6. Handgun hands

In hip fire with a one-handed weapon class the left arm is lowered out of view (the body's left-arm pose is used, or
the rig's left arm is blended to its rest pose); point shooting and ADS use both hands. Needs part 4's joint writes.

### 7. Own menu

A settings panel of the mod's own, drawn through NEST (Goose Overlay Host), opened with a key of its own. DBF-MCM is
no longer needed and no longer registered with. NEST stays: it is the one shared in-game UI layer for HellForge code
mods, and the placement editor already draws through it. The panel takes its colours from `ov->theme()`.

### 8. The test matrix (`tools/harness/matrix.py`)

One harness run per build that plays every action it can produce and judges each from the mod's log and the helper's
counters: idle, walk, sprint, strafe, crouch, prone, dive, vault, climb, fire, aim, aim-fire, reload, ammo check,
swap to each slot, grenade, stim, melee, pick-up, stratagem input and throw, emote, ragdoll (explosion), death and
respawn, terminal, mission end. A phase passes when no frame showed a camera that was not first person
(`forwarded` did not rise without `replaced` or the native count), the largest eye jump is under 5 cm, and the mod
logged no refusal. The matrix result is written beside the captures.

## Order

1, 2 and 8 first (the lock is what every other part is seen through, and the matrix is how each is judged); then 3;
then 7; then 4, 5, 6, which depend on the rig measurements.

## Results in the game, 2026-11-22 (0.6.0-feel.20, solo missions, `tools/harness/matrix.py`)

Three sessions. In the last, 32 phases:

- **No frame left first person** in any phase but one: one frame at the start of a sprint (the camera still linked
  to the rig on its way out; the last native view is repeated there, which the count now treats as first person).
- **Passed (26):** idle, walk, back, look, crouch, prone, dive, dive back, stand up, fire, aim, aim and fire, aim and
  walk, reload, ammo check, swap to secondary, pistol fire / aim / reload, swap to support, swap to primary, quick
  swaps, grenade, held grenade, melee, emote.
- **Camera jumps left:** stim 0.25 m (on leaving the native view; the stim keeps kind 6 for a moment with empty
  hands), vault 0.12 m, and 0.52 m once when the game changed its camera request for a single frame at the start of
  a strafe/sprint (the controller takes a new lease and the placement blends in again).
- **The eased entry works on this build** (retail had it off): entering the native view after a swap or a grenade
  went from 0.14-0.17 m to 0.01-0.03 m.
- **Field of view:** the game asked for 60 degrees in the native view and 90 in the head camera; the screen showed
  the locked 80 throughout (`Fov:` lines).
- **Kicks:** up to the 8 degree limit in dives, sprints and stims; 1 degree in a pure mouse look and under 3 when
  firing, after the view's own turning was excluded.
- **Own panel:** F10 opened and closed it in the game; no option was changed there yet.
- **Death and ragdoll are not tested.** The character survived its own grenades in every attempt (thrown at the
  feet, and held for 7 s), so no death camera was ever produced. The code path for other camera kinds has not run.
- **Arms:** copying the body's 32 arm joints onto the first-person rig from the script update has no visible effect:
  94 % of the writes were gone by the next frame (the rig is re-posed before drawing). The names match on both rigs.
  Joint writes have to happen where the pose helper already works (the game's own setter calls for the rig), or in
  a hook after the rig's animation; that hook is not found yet. Parts 4 to 6 wait on it.

## Arms in the game, 2026-11-22 evening (0.6.0-feel.21, `arms_live.txt` beside the DLL)

- **Where writes are drawn.** Not from the script: a joint bent at the Lua `render` or `update` callback, with or
  without refreshing the glove and sleeve units, never showed (`tools/harness/arm_bend.lua`). At the game's primary
  world-render call it does: with `arm_bend.txt` the left arm left the view and the pistol was held in one hand.
- **The arm drive** (`src/arm_drive.c`, `src/arm_graft.h`, `src/render_slot.c`) is the retired First Person
  Controller's 0.3.36/0.3.37 method, lifted: 42 rig joints from the body's world matrices at the primary draw, the
  rig, its sleeve and glove units and the weapon tree refreshed, the game's own values recovered the next frame.
  Reviewed by three readers against the retired source before it ran.
- **Recorded** (`probe_rig.py`): the first-person rig's joints now change in step with the body's in idle, walk,
  sprint, fire and aim (they never changed before). 42 joints a frame, 10 units refreshed, two refusals in a session.
- **Seen in captures** (`work/captures/feel21-arms-2026-11-22.jpg`): both forearms and hands on the rifle in sprint,
  reload and ammo check; the pistol in one hand at the hip and in two hands when aiming, as the body holds it.
- **Matrix with the arms on:** 24 of 27; swap to primary 0.051 m and vault 0.057 m are just over the 0.05 m limit,
  stim 0.21 m as before.
- **Not there:** arms during swap, grenade and stim (the game leaves its first-person camera there, so the rig is
  not the one drawn); the put-away transition; a menu option for the arm drive (it is a test file); death.

## The action hold in the game, 2026-11-22 night (0.6.0-feel.22, `hold_lease.txt` beside the DLL)

- **Who drops the native view.** On a swap and a grenade it was the mod (retail's rule: leave when the snapshot is
  not safe). With the hold the mod stays while the lease's own kind-6 request is on top, and the arm drive shows the
  body's put-away on the first-person arms: 0.27 to 0.40 s from the action's first frame until the gun leaves the rig
  (captures `work/captures/feel22-hold-grenade-putaway-2026-11-22.jpg`).
- **The hold ends when the gun leaves the rig.** Held longer, a stim (mask 0x40 here, as in retail; the gun goes to the
  body 30 ms in) showed the helmet from behind in the native view. After the change the stim shows the hands from the
  head (`feel22-hold-stim-swap-2026-11-22.jpg`).
- **A grenade** is the game's own exit: kind 6 ends when the grenade is in the hand.
- **A stim** keeps kind 6 for about 0.85 s by the game's own doing, hold or no hold; the mod's exit is only
  acknowledged afterwards. Its 0.21 to 0.28 m jump is unchanged.
- **Cost.** With the hold on the matrix was 21 of 25: swap to secondary 0.31 m, swap to primary 0.15 m (0.01 to 0.05
  without the hold), stim 0.28 m, and 1.1 m once on a one-frame change of the game's request after a support-weapon
  swap (the same event measured 0.52 m without the hold). Whether the larger numbers are a visible snap or the eased
  entry covering more distance is not known: it has to be seen in motion.
- **Not built:** the mirrored lowering term in the pose helper, arms through the empty-handed part of a grenade or
  stim on the rig (there the body's own arms are shown from the head), a menu option for the arm drive and the hold.

## The smooth return, 2026-11-22 late (0.6.0-feel.23, branch `smooth-return`; offline, not yet run in the game)

- **Why the hold cost 0.31 / 0.15 m:** its exit waited four polls after the gun had left (the game's camera went back
  0.6 m in them), and that eye was remembered for the weapon, so the next return had 0.66 m to cover. With an exit
  acknowledged at once the hold measured 0.008 to 0.048 m in the same session.
- **Built:** the exit on the tick the gun leaves the rig; under the camera lock, continuity of the presented eye
  across every change of camera and every one-frame move of the rig (a correction of at most 1 m, eased out over 100
  to 400 ms, nothing in full aim, restart beyond 1.5 m); a per-frame `Trace:` for 0.5 s after each State edge; the
  `Entry:` line in one space. Switches `smooth_off.txt`, `trace_off.txt`. [ACTION-CAMERA.md](../ACTION-CAMERA.md)
  section 7 has the mechanism, the reasons and the list for the first run.
- **Offline:** `tests/run-camera-continuity.sh` (the arithmetic), `tests/run-camera-smooth.sh` (through the camera
  helper on the fake game memory: the rig steps of the feel.22 session, a stim's mask, the one-frame request, a
  teleport, full aim, the trace, and no difference without the lock), `tests/run-vectors-backend-ops.sh` (the exit at
  once; retail's four polls with the switch off).

## Smooth return in the game, 2026-11-23 early (0.6.0-feel.23, hold on, arms on)

Matrix phases with the put-away hold on (`tools/harness/matrix.py`), by the mod's own Cut measure:

| Phase | feel.22 | feel.23 |
|---|---|---|
| swap to secondary | 0.31 m | 0.033 m |
| swap to primary | 0.15 m | 0.039 m, 0.000 m |
| quick swaps | 0.043 m | 0.040 m |
| grenade | 0.04 m | 0.037 m |
| stim | 0.21 to 0.28 m | 0.059 m |
| vault | 0.057 m | 0.000 m |

Frame by frame (`tools/harness/trace.py` over the `Trace:` lines, 30 frames a second in the harness): the eye's step
never changed by more than 0.040 m from one frame to the next in any of 19 edge windows; corrections of up to 0.49 m
were carried and eased out. The first run of this build still showed 0.15 m in one frame inside a swap (the held eye
letting go in the head camera); jumps are now looked for in head-camera frames too.

A death happened in that first run (enemies, by chance): the head camera refused with error 16 (the body's graph no
longer matched) and the game's death camera was shown for about five seconds. Session `b08c8327`. Being worked on.

## Death under the lock, 2026-11-23 (0.6.0-feel.24, branch `death-view`; offline, not yet run in the game)

What the death in session `b08c8327` showed (10:10:50 to 10:11:07 in the mod's log):

- **Error 16 was not the graph.** It was the last clause of the head proof: the head must be 0.05 to 3 m from the
  unit root (`points()`), and the corpse's head was 4.28 m from it. Everything before that clause passed on those
  frames (the distance is only computed after the graph, the tables and both matrices were proven), and the owner
  proof passed on every frame for the five seconds. The first dead frame failed differently, with error 7 and the
  head at 1.70 m: the final proof compared the animation chain with a sample that is only taken on foot.
- **The view came back by itself**: new helper generation at 10:11:01.4 on the new body, the head camera on every
  frame from there, the native view at 10:11:07. F11 was never involved. The later part of that run was first
  person throughout (the `Lock:` lines kept printing the last missed frame's kind and error beside `+0 missed`,
  which read like a failure); at 10:12:05 the avatar was gone (`Empty entity`, no camera calls for seven seconds), and
  from 10:12:13 the head camera held every frame, with empty hands, until the session closed.

Built, all of it only in frames the lock holds (`src/camera_helper.c`; lock off: retail's reads and answers):

- A head beyond 3 m from the root is taken when it is 0.005 to 0.5 m from its neck, the parent node whose link and
  name the graph proof has just checked, in the same world array (the retired controller's rule,
  `native_view.c:803-814`). Also in the begin proof while the lock is on.
- Off foot, the animation chain the final proof compares is read in the same frame; a chain that cannot be read is
  not compared. The body, graph, request and camera-parent clauses stay.
- The body is **down** when the head is beyond 3 m from the root, or it is off foot with the avatar's ragdoll flag
  (`avatar_owner + 0x547844 + index * 0x78`, bit 0x10). Down: the eye is placed along, and the view holds, the last
  first-person direction, with the thrown kick (70 % up to 35 degrees) on it; the head's own motion against the
  root is not read as a camera jump. Up again (a ragdoll that was survived): the direction eases back to the
  game's in about 0.3 s. Nothing but that direction is kept between frames.
- Head hiding (`src/wire_engine.c`): while the helper holds the head camera on the snapshot's body off foot, the
  on-foot answer is yes, and an action state that cannot be read is replaced by the snapshot's own owner. The
  helmet, torso and shoulders stay hidden around the eye.
- The held direction is kept apart from the view last shown (`lock_first`): it is taken only from native (kind 6)
  frames and ordinary (kind 4) frames on foot. Off foot with the head in walking range and no ragdoll flag (a dive,
  or the start of a death that does not set the flag) the game's direction is shown but not remembered; when the
  body then counts as down the view turns to the remembered direction over 120 ms, the same ease as the turn back
  after a survived ragdoll.
- Log: `Dead: kind=4 head=(x y z) frames=+N refused=+M (reason)` once a second while the body is down;
  `Body:` (off foot, ragdoll flag, head distance) while the lock holds an off-foot body; `Missed:` with the proof
  that refused (`tfp_camera_why_text`) in any second with a missed frame; `Lock:` shows kind 0, error 0 with
  `+0 missed`.
- Not changed: kinds 11 and 10 after the corpse. The record is another avatar's and the setter is not called from
  the hooked site; there is no body of the player's to put an eye on. About six seconds until the new body.

Offline: `tests/run-camera-death.sh` (through the helper on the fake game memory: a death thrown 4.3 m, held
direction, at-rest eye, the same without the lock, a head away from its neck, off foot in walking range, an
unreadable animation instance, a survived ragdoll, the thrown kick, arming on a thrown body, a death without the
ragdoll flag whose head leaves walking range only later while the game's camera circles).

**The first death in the game must check:**

1. `Dead:` lines from the first second: `frames=+30 refused=+0`. If there is no `Dead:` line but `Body: off foot 1,
   ragdoll flag 0`, death does not set the ragdoll flag and the head stayed within 3 m: the eye is at the head but
   the direction is the game's corpse camera; say so and the sign for "down" has to be another one.
   `Dead:` lines alone do not show the direction. If the `Body:` lines before the first `Dead:` line say
   `ragdoll flag 0` (the body was off foot for a stretch before it counted as down: the first log had the head at
   1.70 m at 10:10:50.276 and at 4.28 m only later), compare what is on screen once the body lies with the
   direction before death: it must be that one, reached by a short turn (120 ms) from wherever the game's corpse
   camera was, not the corpse camera's.
2. Any `Missed:` line during the death, and its reason. "the animation state changed during the frame" would mean
   the chain moves between sampling and the final proof; "not at its neck" a head that left the body.
3. What is on screen: the helmet, torso or shoulders around the eye (`Feel:` `head=active:N` or `head=idle:0`);
   the ground or the body clipping the near plane when lying; whether the held direction with the kick reads as
   falling, or whether the game's own direction (mouse) would be better there.
4. The first 0.3 s: `Cut:` after leaving the native view and the `Trace:` rows (`source=head`, `correction`).
5. A ragdoll that is survived (an explosion nearby): the `Dead:` lines, and the turn back to the mouse direction.
6. Respawn: `Lock:` holding from the first second on the new body, head hiding back, no key pressed.
7. A death by explosion or a head shot, where the head may leave the neck: `refused` with the neck reason.


## The stim's orphaned frames, 2026-11-23 (0.6.0-feel.26, branch `stim-view`; offline, not yet run in the game)

Seen in the game on 0.6.0-feel.25 (session `8d992b8d`, 12:17:38.895 to 12:17:39.830, eight captured frames): for
about half a second of every stim the screen showed the player's own helmet from behind, the rifle on the back.

What the log says, tick by tick:

- 38.895: mask 0 to 0x40, kind 6, the lease's record, gun on the rig; the hold begins. By the code head hiding
  stops on this tick: its snapshot is not safe, and only mask 2 (a swap) or the head camera's masks keep it
  (`head_visibility.c` `available()`); `Feel:` says `head=idle:0` at 38.961 and `head=active:90` again at 39.964.
- 38.928: gun on the body, placement finished, "hold ended: the gun has left the hands", and the release is refused
  ("the action does not allow the exit yet", `backend_ops.c` `release_held`, mask 64). The controller waits in
  cleanup pending with the lease until 39.795 (0.87 s, 26 frames), and the game presents kind 6 all that time.
- 38.961 and 38.995: the rig's eye moves 0.28 m and 0.22 m, to (-2.861 1.137 5.635), and stays. The one head-camera
  frame of the stim (39.796) wanted (-2.849 1.344 5.635): the rig's eye sat 0.21 m behind the head camera's eye at
  the same height, which is 0.03 m behind the head node: inside the helmet. The weapon's eye before the stim was
  0.68 m behind that point and 0.13 m above it.
- Since 0.6.0-feel.23 the continuity follows the rig's eye there in 0.4 s (steps of 0.03 to 0.06 m) and it stays
  there for another 0.43 s. Two things together made the picture: the eye went into the head, and the head was drawn.
  A grenade has neither: mask 2 keeps the head hidden, and its exit is acknowledged on the tick the gun leaves.
- By the log the stim has one head-camera frame; the "hands seen from the head" of capture frames 3 and 4 are the
  native view after 39.830 (a new lease, the weapon coming up).

Built, all of it only while the camera lock is on:

- **The eye** (`src/camera_helper.c` `orphan_head`, `tfp_camera_set_orphan`). The binder says "orphaned" while the
  controller waits for the exit of its lease, the game's top request is still that lease's kind 6, and the lease's gun
  is not on the first-person rig (`src/wire_action.h` `tfp_orphan_step`, `src/wire_rt.c` `tfp_wire_orphan`; not
  seated, not full aim, no ragdoll, no other camera owner; kept through a tick that cannot be read; ends when the gun
  is on the rig again). In those native frames the helper presents the head camera's own point (the head, 0.18 m
  along the level view direction, 0.04 m up) as the head source of the continuity, converted to the rig eye's frame
  for the game's parent-local argument. The change from the weapon's eye is a change of camera for the continuity:
  taken up whole and eased out (0.68 m in 0.4 s here, no step changing by more than 0.035 m at 30 frames a second).
  The game's exit then brings the head camera, which wants the same point: nothing to take up. The native view's
  return is a change of camera as before. A frame whose head or rig eye is not proven is presented as before
  (following the rig) and counted.
- **Head hiding** (`src/wire_engine.c` `io_snapshot_for`, `src/wire_action.h` `tfp_action_own_native`). While the
  game presents the lease's own first-person request (kind 6, the lease's record, not seated), the head module's
  snapshot carries the flag it already takes for a vault and a pick-up, so it never lets go: from the stim's first
  tick, through the wait, until the exit, where the ordinary camera's own rules take over. The module itself is not
  changed. This also covers any other action flag in the lease's native view.
- **Not changed:** the put-away (the gun on the rig: the game's own view, byte for byte in
  `tests/run-camera-smooth.sh`), the native view, full aim, everything with the lock off (same forwarded bytes and
  the same number of memory reads with the binder's word on or off).
- Log, one line when such a window ends:
  `Orphan: kind=6 frames=+N eye held at the head (rig eye R m behind), head hidden=<0/1>, why=<...>`.
  N: camera frames presented at the head's eye. R: the most the game's own eye was behind that point along the level
  view direction. head hidden: head visibility was hiding rows on every tick of the window. why: what the exit waited
  for, then `+M frames followed the rig's eye` when frames were refused, and a note when head hiding was not on
  throughout. The window's edges open a `Trace:` window; its rows say `kind=6 ... source=head`.

Offline: `tests/run-camera-smooth.sh` section 12 (the logged stim on the fake game memory, with and without the
binder's word; full aim; a head that is not proven; Smooth eye off; the lock off), `tests/wire_action_test.c` (the
window tick by tick as logged, each way out of it, the head rule).

**The first stim in the game must check:**

1. The screen: no helmet, no torso, no rifle on the back at any moment of the stim. Burst captures as on 2026-11-23.
2. One `Orphan:` line per stim: `frames=+24` to `+27`, `rig eye 0.2` m behind, `head hidden=1`,
   `why=the action does not allow the exit yet`. No `Orphan:` line after a grenade or a swap.
3. `Feel:` during the stim: `head=active:N` with N above 0 (34 was seen with empty hands in the head camera). If it
   says `head=unavailable`, head visibility's own proofs refuse the state with the gun on the body under mask 0x40,
   and that module needs the next look.
4. `Trace:` rows of the window: `source=head` from the frame after "hold ended", `step` never changing by more than
   0.035 m, `correction` from about 0.68 to 0 in 0.4 s, and the kind 4 row at the exit with a correction near 0
   (it was 0.2078).
5. What the arms are. Not known offline: whether the first-person rig's arms are drawn in these frames (the arm
   drive refuses them: "player or weapon not proven") and how they look from 0.21 m in front of the rig's eye, or
   whether the body's own arms do the injection as they do for a grenade. Arms too close, doubled or cut by the near
   plane are the next thing to fix (hide the rig, or drive it).
6. The direction of the view in the window: it is still the rig eye's rotation. A turn of the view at the start of
   the window or at the exit would mean the rig's eye is rotated when it is re-seated.
7. The glide itself: 0.68 m forward in 0.4 s at the stim's start, and 0.4 m back when the weapon returns. Whether
   that reads as fluid or as a push-in. If the weapon's eye really is that far behind the head (the distances rest on
   one head sample), the same glide already happens on every grenade.
8. With Weapon put-away off: the window should begin one tick later (the exit is asked on the mask's first tick, the
   gun leaves on the next); the same checks.

## What pictures showed that numbers did not, 2026-11-23 (0.6.0-feel.25 to feel.27)

Each action was filmed as a strip of captures (`tools/harness/burst.py`). Three flaws had passed every numeric check:

- **Stim: the helmet from behind for about half a second** (`work/captures/feel25-stim-helmet-2026-11-23.jpg`). Head
  hiding stopped on the stim's first tick and the game's own camera, kept for 26 frames with the gun on the body, sat
  0.41 to 0.43 m behind the head camera's eye. feel.26: those frames present the head camera's eye and the head stays
  hidden. Seen fixed (`feel26-stim-2026-11-23.jpg`; `Orphan:` lines).
- **Two resting places for the rifle** (`feel25-burst-swap-back.jpg`, `feel25-burst-reload.jpg`): after a swap back
  the arms' reference was taken inside the draw. feel.27: measured only at rest, kept per weapon and stance, eased.
  In the game it moved 0.247 m / 8.9 degrees back to the placement after the first swap and was reused afterwards
  (`Arms: aligned` lines; `feel27-rest-pose-2026-11-23.jpg`).
- **Aiming sank the rifle to the bottom of the view** with the arms copied from the body. feel.27: while the action
  is aim or ADS the copy fades out over 120 ms and the game's own aim pose shows; seen with the rifle
  (`feel27-aim-rifle-pistol-2026-11-23.jpg`). The pistol in the same strip sits low at the hip and sweeps on release:
  not judged.

The matrix is now judged by the presented eye's own trace (the step may change by 0.06 m between two frames at 30 a
second): 0.6.0-feel.27, 31 of 31 phases, no frame out of first person.

Still not seen: a death under the relaxed head rule; stratagem input and the panel's rows (the harness cannot enter
either); the empty-handed part of a stim shows no hands (they work below the view).

## The panel in the game, 2026-11-23 (0.6.0-feel.27)

Driven by click messages from inside the game's process (HARNESS.md). All three tabs draw
(`work/captures/feel27-panel-locked-view-2026-11-23.jpg`, `feel27-panel-bob-sway-2026-11-23.jpg`). On the Locked view
tab: Animated arms off took the render hook out and on put it back; Weapon put-away and Smooth eye switched off and
on; Camera lock off stopped every `Lock:`, `Fov:` and `Smooth:` line within the second and on brought them back. Each
change wrote `locked_view.json` once (7 ms). Sliders and the key-capture rows were not exercised.

Found on the way: `settings.json` is rewritten on every weapon swap, because the menu's per-weapon value
`menu/weapon_near_hip_cm` (10 for one weapon, 5 for the other) is stored as it is shown. One small file write per
swap; not fixed yet.

## Death in first person, seen: 2026-11-23 (0.6.0-feel.27)

A death can be produced on demand: `tools/harness/hurt.py kill` writes health 0 and life 1 (downed) on the local
player's own record in the game's health table (the avatar id comes from the mod's `Ids:` line, new in feel.28); the
game then runs its own death. Enemies never do it here: the installed No-Aggro mod keeps them off, and own grenades
did no damage in any attempt. Writing life 2 directly also kills, but without the fall.

What the strip shows (`work/captures/feel27-death-first-person-2026-11-23.jpg`, 0.35 s apart): ELIMINATED comes up,
the view tilts and falls with the body, then lies at the head for about five seconds with the body's own leg and
pack in view and the head hidden, until the game's replacement sequence (a camera that is not the avatar's) takes
over. The log: `Body: off foot 1, ragdoll flag 0, head 1.62 m from its root`, then 1.57, 1.11, 0.86, 0.86; one frame
missed, at the handover (kind 65535). The view came back by itself on the new character.

Not seen: a death that tears the body (the relaxed head rule of feel.24), and a death while aiming or in a hold.

Three more on 0.6.0-feel.28 (`work/captures/feel28-deaths-aim-sprint-swap-2026-11-23.jpg`): a death while aiming, while
sprinting and 0.25 s into a weapon swap. Each stayed at the head as the body fell (`Body:` head 1.24 to 0.72 m, 1.39 to
1.09 m, 1.66 to 0.98 m), with the one handover frame missed. The eye's step changed by 0.02 m, 0.19 m and 0.04 m at
most between two frames; the 0.19 m is the sprint's body going down. On the ground the lens shows what lies in front
of the face: the dropped rifle after the aiming death, the character's arm after the sprint, grass after the swap.

0.6.0-feel.29: the menu's per-weapon drafts following the weapon are no longer saved as a change (four swaps in the
game: no write of `settings.json`). The panel's sliders were exercised by click: field of view to 69 (the screen
showed 69.0 by the `Fov:` line) and back to 80; view kick strength to 30 and back to 35 (5 % steps).

Key row (0.6.0-feel.29): "Change" beside the toggle key, then F9: `menu/toggle_key` went 122 to 120 in `settings.json`,
F9 switched the view, and the same steps with F11 put it back. The log still calls the request "(F11)" whatever the
key is: a name in the log, left as it is.

Stratagems: the game binds the menu to left Ctrl and the directions to the arrow keys
(`userdata/profiles/<name>/cloud/input_settings.config`). The harness now sends the arrows as extended keys and has a
left-Ctrl name, but the stratagem menu does not open from its input, with the view on or off (plain third person):
a limit of the test input, not of the mod. Stratagem throws remain a check for a person.

## What the mod costs, 2026-11-23 (0.6.0-feel.30 `Cost:` line, 54 seconds of play at 30 frames a second in the harness)

- The arm drive inside the primary draw (42 joints, the rig, about ten sleeve, glove and weapon units): 62 us a frame
  on average, 72 us in the worst second, 111 us for the slowest single frame.
- The mod's tick on the game thread: 331 us on average, 406 us in the worst second. Single ticks of 1.3 to 1.8 ms
  when the native view is entered (a new lease: the helpers' proofs), and 8.1 ms once, in the first second after the
  first F11 of the session (the one-time checks and the first alignment).
- The log grew by 189 KB and 1027 lines in a session of about four minutes.

The older `--view` build variant (the milestone before the feel build) has not compiled since 0.6.0-feel.20: the
lock's code in `src/mod_view.c` sits in the feel build's part. Nothing ships from it.

## Second review, 2026-11-23 (0.6.0-feel.31)

Four reviewers over the changes made by hand since feel.24, each finding verified by a second: five confirmed, all
fixed with a test that failed first, then two reviewers over the fixes.

1. The arm copy's aim fade followed only an active motion runtime: with no running lease, or on a refused sample, it
   stayed on while aiming. Now the player's own aim flags decide there, and the last answer stands when unread.
2. A binding begun while aiming took its provisional alignment from the aim pose. It is taken again on the first
   frame the arms are fully shown.
3. Jump detection in head-camera frames took a thrown body's first frames for a re-seated rig (the eye left behind
   the head for 0.1 to 0.5 s). No detection in head-camera frames of a body that is off foot.
4. A refused `settings.json` was replaced by defaults in the first tick: the bind's own rewrite of the editor key
   counted as a change. Binding a menu is no change to save.
5. A second conversion after a session on feel.20 to feel.24 dropped the options set there. The member in
   `settings.json` is the newer one and wins; the file as found goes to a numbered copy.

In the game on feel.31: matrix 30 of 31 (vault 0.063 m against the 0.06 m limit), a forced death held at the head,
aim, sprint into aim and swap back filmed and right, placements identical to the backup of 2026-11-23.

### Stratagems on feel.31 (2026-11-22)

The earlier "the menu does not open from test input" was the test's own fault, twice over: the menu opens on a
tap of Ctrl (the binding triggers on press; holding does not open it), and the harness words `left` and `right`
are the mouse buttons, so every code's "right" pressed aim and closed the list. The arrow keys now have their own
names (`arrowup`, `arrowdown`, `arrowleft`, `arrowright`).

Seen in the game, view on:

- Opening the list: the put-away is shown (0.37 s in the native view), then the view is at the eyes with empty
  hands. Closing it returns to the weapon. No frame left first person, largest camera step 0.012 to 0.037 m.
- SOS Beacon entered (up, down, right, up) and thrown with the left button: the beacon lands ahead with its beam,
  the weapon returns. 361 frames, 0 missed, largest step 0.037 m. Capture `work/captures/feel31-stratagem-throw.jpg`.
- While the list is open and while the beacon is in hand, no hand and no beacon are in view: the same limit as a
  grenade with empty hands. The left wrist display is seen only in the first frames of the put-away.

Not seen: Resupply and Reinforce (both unavailable in this solo session), a support weapon or sentry call-in.

Second look, same day: with the list open the left forearm with the wrist pad is raised and the rifle hangs low in
the right hand, as in third person. At a level view the pad sits at the lower edge of the frame; it is in full view
when looking down or walking. Nothing is hidden that should show. A glance down at the pad on opening the list was
considered and left out: it would move the view against the player's hand.


### feel.32 and feel.33 (2026-11-22)

A third review, of the feel.31 fixes themselves (four areas, each finding checked twice): fixed in feel.32 were a
binding begun again inside a hold never arming the retake of its mid-action alignment, identical numbered settings
copies at every start of a failing write, and a later build's `locked_view.json` written over without a copy (now
`locked_view.json.newer`); four test gaps closed. Left as documented: one start of feel.20 to feel.24 resets the
Locked view options to that build's defaults at the next conversion.

In the game on feel.32: matrix 33 of 35 (ragdoll and death did not run: the stratagem phase left the list open, a
fault of the phase, now a tap to open and a tap to close); stim while aiming 0.037 m.

Found there and fixed in feel.33: with the view pitched down, the return to the weapon after a swap or after closing
the stratagem list stepped 0.09 to 0.13 m over two frames. The game re-seats the rig over three frames (0.08 m each)
about 270 ms after the entry; jumps were looked for during 300 ms only, so the first frame was taken up and the next
two shown. A run of jumps that begins inside the time is now looked at to its end (bounded by the run's 70 ms).
In the game on feel.33: level swap and list 0.027 to 0.038 m, pitched down 0.030 to 0.070 m (ten runs); a forced
death held at the head until the replacement (one frame missed when the avatar is replaced, as before).

