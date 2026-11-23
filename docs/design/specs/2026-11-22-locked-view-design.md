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

The whole matrix on feel.33, fresh session: 35 of 35, nothing missed. Largest steps: swaps 0.035 and 0.036 m, stim
0.041 m, stratagem list 0.017 m, emote 0.058 m and jump 0.060 m. The last two are the body's own motion, read from the
trace: the head camera's eye is the head, which the emote's first frames move 0.28 m in 0.1 s, and a walk at 3.5 m/s
is 0.117 m a frame; no correction was carried in either. Frame strips of nine actions (swap, swap back, stim, aim,
sprint into aim, aim and reload, emote, look down, prone and aim) show nothing out of place; they were filmed with the
view pitched down, because the death phase left it there (now put back).

### feel.34 (2026-11-22)

A fourth review: everything added since feel.19, for crash and thread safety (render hooks and arm drive, state
shared between threads, the camera and field-of-view lock, the panel), each finding checked twice. No crash and no
data race was confirmed. Six behaviour defects were, all fixed:

1. The field-of-view lock followed "the camera lock held a frame", so with Camera lock off it did nothing. It follows
   a count of frames the view is shown (`tfp_camera_view_frames`). The `Fov:` line is written on its own.
2. A key bound in the panel fired its action with the binding press. A key that has just become the bound one counts
   as held until seen up once (`tfp_wire_menu_key_edge`).
3. The kick's "the view is still" test measured the view's turn through acos, whose smallest non-zero answer is 0.04
   degrees: a still view that carries rounding turned 20 degrees a second at 2 ms a frame, and the kick was weaker
   and shorter the higher the frame rate. Measured from the vector part now.
4. The panel connected asking the host for API 8 though it uses API 6 calls: no panel on hosts 0.4.0 and 0.5.0.
5. The placement editor row took F8 (a stored 119 means the default key), showing F8 while Page Up stayed the key.
6. The panel id was new at every start, so the host forgot the window's place; stable now, per-load only when the
   host still holds the first (a build replaced while the game runs).

Disputed and left: no glide out of the field-of-view lock when it is switched off (one frame).

In the game on feel.34: the panel opens and registers (host API 11); binding F9 to the toggle did not switch the
view, the next press did, and back to F11 the same; field of view 80 with Camera lock off, through a swap, off with
its own toggle, both back. Matrix: 33 of 35, then the four phases toggle-fast, dive, ragdoll and death in a fresh
session, all passing (ragdoll 0.045 m, death 0.036 m). The first run's two failures were the quick-toggle phase
losing one of four presses 30 ms apart (the key is polled once a frame), which left the view off; the phase now
leaves 0.11 s between presses.

### feel.35 and feel.36 (2026-11-22): the first session played by hand

The user played feel.34 and reported three things; all are fixed and seen in the game.

1. **"Some of these transforms are a little messy. Notice my arms."** Looking down with the revolver the gun arm was
   stretched to the lower right; the log said the socket was 0.26 to 0.47 m and up to 59 degrees off the rig's own
   while "looking up or down" (0.06 to 0.08 m level). Cause: the frame the body's arms were expressed in was the
   body root's axes at the head (`tfp_graft_anchor`), which does not pitch, while the rig hangs on the view, which
   does: the arms' pitch (and any twist of the body under the view) was counted twice. The root's matrix now gets
   the axes of the rig's root before anything is bound, aligned or posed (`tfp_graft_view`). In the game: 0.002 to
   0.006 m level, 0.044 m at 35 degrees down, 0.076 m at 70 down, 0.036 m at 35 up.
   Corrected after review (not yet seen in the game): the axes are those of rig node 4, the parent of node 5 and of
   the eye (`tfp_graft_view_node`), not of the rig's root, node 7. Node 7 is under node 5, where the placement and the
   motion rotation are written, so with node 7's axes that rotation cancelled for the driven arms and gun.
2. **"Switching weapons causes a momentary vignette."** The game's first-person vignette, which the mod switches off
   behind the depth-of-field lease: every swap ended the view for 0.3 s, the vignette (and `dof_enabled`) was given
   back and taken again. Both are now held for 1.5 s after the view goes away while first person is on
   (`tfp_wire_effects_vignette_view`), given back at once when first person is switched off. In the game: 0 restores
   over four swaps, one of each at F11 off.
3. **"Configurable dampening for the camera moves... they're very intense right now."**
   - `camera_damping` (0 to 100 %, default 60; 100 % is 300 ms): the kick is low-passed in time (`tfp_kick_step`
     over `tfp_kick_raw`), and in head-camera frames on foot the eye follows the head through the same low-pass with
     the lag held under 0.10 m (`src/camera_damp.h`). Not in full aim, not for a body that is down or off foot.
   - The kick's sideways part (about the view's up axis) counts for a fifth (`TFP_KICK_YAW_SHARE`): it is the body
     twisting under the head, and it turned the aim. In the game at the saved 35 % / 8 degrees: walk 1.8, back 1.5,
     strafe 3.3, sprint 2.7, crouch 1.4 degrees (5 to 8 before); prone 4.6, dive 5.5, grenade 5.7, stim 6.2.
   - Defaults of kick strength and limit were left as they are; the user's saved values are theirs.

Also in feel.36: the field-of-view lock's arithmetic in `src/fov_lock.h` with tests, gliding out as it glides in;
jump detection after an event during 600 ms (300 before: walking out of an emote the rig re-seated 0.10 m in one
frame half a second after the view came back; 0.10 to 0.12 m then, 0.045 to 0.052 m now, four runs).

In the game on feel.36: matrix 35 of 35, twice. A death from real damage (three injuries through the HD2 runtime)
stayed at the head and came back after the respawn; a hit of 30 gave a 1.6 degree flinch. A dive begun 0.9 s after a swap first read as
0.122 m in one frame; that was the judge: the trace stops half a second after an edge and starts again at the next,
and `matrix.judge` compared the frames on either side of the pause. It no longer does (`GAP_MS`); the same dive is
0.042 to 0.057 m (three runs). Numbers before this date for phases that span two trace windows may be too high for
the same reason.

### feel.37 (2026-11-22): a fifth review, of the feel.35 and feel.36 changes

Four areas, each finding checked twice; ten confirmed, none a crash. Fixed, each with a test that failed first and an
independent check afterwards:

1. The view frame taken from rig node 7 also held the rotation the pose helper puts on node 5 (placement
   yaw/pitch/roll, sprint tilt, sway), which then cancelled for the driven arms and gun. The frame is node 4, the
   parent of node 5 and of the eye (`tfp_graft_view_node`; the bind is refused if the links are otherwise).
2. A rest is measured within 8 degrees of level (20 before): one kept at 15 to 20 degrees was about 2 cm off at
   level and never replaced.
3. The effects hold is keyed on first person being switched on (`controller.enabled`), not on the phase; an explicit
   stop stops at once; the depth of field's hold has its own exposed rule and a test.
4. The eye damping filtered the eye's forward arm with the head, so a turn of the view swung the eye up to 0.10 m.
   Only the head against the body root is damped; a carried lag is let go over 40 ms when a gate closes.
5. The field-of-view glide runs on the host clock in every rendered frame; `lock_field` has tests through the helper.

In the game on feel.37: socket off the rig's own 0.010 to 0.025 m level, 0.048 m at 35 degrees down, 0.080 m at 70
down, 0.037 m at 35 up (node 4 pitches with the view: the recordings could not show that); the sprint pose turns the
rifle (motion line: sprint, 24.6 degrees); four swaps without a give-back of vignette or depth of field, one of each
at F11 off; a grenade and a stim while turning the view 0.014 and 0.029 m.


### The soak of feel.37 (2026-11-22, branch `soak`; offline, not yet run in the game)

`tools/harness/soak.py`: 301 random actions back to back, 21,512 frames held. What it flagged, and what was done:

1. **0.58 m in one frame, a backwards dive begun 170 ms after an entry (19:45:12.826).** The wanted eye went on at
   its 0.08 m a frame in the world; the presented eye went 0.70 m and 0.61 m of correction appeared; one frame later
   the wanted eye itself moved 0.44 m. A correction is only taken up when the eye's step in the body root's frame is
   off its speed by more than the measure, so the step in the root's frame was about 0.6 m while the step in the
   world was ordinary: the root (its place or its turn; the lines of that build do not say which) was re-seated in
   that frame and the rig eye followed a frame later. The continuity now also gets the eye in the world
   (`tfp_continuity_step_in`): a jump in the root's frame whose root part is over the measure, and that is smaller
   in the world, is measured in the world; what is carried is turned into the root's new axes and goes on. A jump in
   both frames alike is the eye re-seated, as before; one in the world only is nothing, as before. The `Trace:` rows
   now end with `root=(x y z) looked=0|1 taken=jump|edge|root|restart|-`, and `Smooth:` counts the root's moves set
   aside.
2. **0.129 m in a frame in which the wanted eye stood (19:46:24.272).** Replaying the logged wanted eye through the
   filter gives the logged corrections to the fourth decimal (the body stood). After that entry the rig eye went
   back and forth between two places 0.19 m apart from frame to frame. The flips were taken up; when the run's 70 ms
   were over, the frame's own step, itself a flip, was believed as the eye's new speed (5.3 and 6.5 m/s), and the
   presented eye went off at it: 0.143 m at 266 ms after the entry, 0.134 and 0.204 m at 466 and 499 ms, 0.43 m
   carried. The speed believed is now the middle one of the run's last frames (four at most, each axis by itself).
   The logged sequence is a test: no frame over 0.05 m while the wanted eye stands, 0.25 m carried at most.
3. **"0.163 m in one frame, correction 0" in head-camera frames inside a swap (19:46:57.893, 19:51:00.303; 0.183 m
   at 19:46:15.493).** Not the eye. The body ran at 0.165 m a frame in all three (every row before says so), and on
   the frame the view left the native view the game set the camera twice, 0.7 ms apart (`t=-1.0 kind=6`, then
   `t=-0.3 kind=4`, the same eye): the second row's `step=0.0001` is that frame again, and the next row's 0.163 m is
   the body's travel as before. The jump the detection in head frames was added for came one frame later in each
   and was taken up (`correction=0.1342`, `0.2115`). `matrix.judge` did not flag these three (a State line ended the
   window in between), but it would have with the State line a tick later; `trace.windows` now takes two rows
   closer than 5 ms as one frame.
4. **One frame missed, helper error 15, "no proof named" (19:54:43, before 19:54:43.391 by the counters).** Error 15
   is `unparented()` refusing an ordinary-camera frame: either the camera has a parent (and no native view of the
   last frames to repeat), or it has none but its node's local and world position are more than 0.01 m apart. The
   log could not say which; the body had been thrown 6 m a second before and had no weapon in its hands, which
   speaks for the second, but that is a guess. Nothing was changed in what is presented. The `Missed:` line now
   names which of the two (`TFP_CAMERA_WHY_LINKED`, `TFP_CAMERA_WHY_SPACE`) and, for the second, the distance.

Not done: in the same dive the presented eye fell up to 1.13 m behind the rig's eye over the next 330 ms (no step
over 0.2 m): the dive's own acceleration, inside the 600 ms after the entry, read as a run of jumps. With the root in
the `Trace:` rows the next soak can tell the root's part from the eye's there.

#### After the check of these fixes (2026-11-22, same branch; offline)

An independent check found three gaps; each has tests now (`tests/camera_continuity_test.c`,
`tests/test_harness_trace.py`).

1. **The middle of a run's frames is itself a flip with two or three flips in the run** (35 to 70 ms a frame: 0.16 m
   in a frame while the wanted eye stood, 0.35 to 0.64 m carried, as before the fix). The speed believed is now what
   the run's last frames agree on (`tfp_continuity_believed`, each axis by itself): the newest frame and those before
   it for as long as none goes the other way from the one after it; one frame alone is no speed, of two the slower,
   of three or four the middle. The run's way divided by its time was tried and is wrong twice: an odd number of
   flips ends 0.19 m from where it began (2.7 m/s), and a re-seat in the frame a motion begins counts as speed.
   Measured, 2 to 12 flips of 0.19 m at 4 to 100 ms a frame: 0.19 m carried at most; at most 0.053 m in a frame
   while the wanted eye stands up to 40 ms a frame, 0.066 m at 50 ms and 0.088 m at 66 ms. The last two are the
   catch-up of the 0.19 m itself (1.3 m/s at its fastest, `ease_accel`), not a believed speed, and were left.
   A start at 3 m/s where jumps are looked for is believed as 3 m/s when the run's 70 ms are over, at every frame
   time; 300 ms after the start the presented eye is 0.22 to 0.43 m behind (16.7 to 66 ms a frame): the 70 ms held
   back and a catch-up of 400 ms, as before.
2. **The root branch of item 1 above misfired on a genuine rig re-seat in a fast turn.** It compared with the eye's
   speed in the world averaged over 50 ms; that speed turns with the body, and the average's lag passed for the
   root's part (0.26 m of step change for 0.17 m, the eye 0.8 m ahead of a root turning at 12 rad/s). The root's part
   is now how far the root itself is off its own even course, seen at the eye: the root follows from the eye in the
   world and in the root's frame; its course is its turning (last frame's) and its speed (50 ms average), kept in the
   world or along its own axes, whichever fits the frame better. A root off its course keeps the course for that
   frame, and what it did is looked at once more in the next: kept, it was a new speed (a throw), not a re-seat.
   A 0.15 m re-seat in a turn (864 cases: 4 to 100 ms, 0 to 6 m/s, 0 to 12 rad/s, each way) and a body thrown in a
   turn (72 cases) are now to the millimetre what they are without the root; item 1's tests pass unchanged.
   That the body root turns with the view is still an assumption; the `root=` in `Trace:` will show it.
3. **`trace.windows` above 200 frames a second**: every row was under 5 ms after the row just kept, so a window
   became one row. A row is the same frame again only under 5 ms after that frame's first row and with another kind
   or the same eye (0.002 m). Left: a change of kind with a moved eye under 5 ms after a frame's first row still
   reads as that frame set twice.

### feel.38 in the game: the soak replayed (2026-11-22)

`tools/harness/soak.py 12 1` on feel.37: 301 actions, 21,512 frames, 1 missed, 6 flagged, largest step 0.584 m. The
same seed on feel.38: 302 actions, 21,784 frames, 0 missed, 5 flagged, largest step 0.215 m (the same backwards
dive, action 6), the others 0.062 to 0.091 m. With the root in the trace the dive reads: the root's own step grows
0.095, 0.138, 0.185 m a frame as the dive starts, the wanted eye moves 0.431 m in the frame the rig follows, and the
presented eye 0.299 m (taken=jump+root). What is left is about 0.11 m over the root's own step in one frame of a
dive at 6 m/s; left as it is. The arm drive's refusals during swaps ("player or weapon not proven", up to 19 frames)
are as before: those frames show the rig's own pose.

### feel.39 (2026-11-22): two more soaks

Seed 2 on feel.38: 309 actions, 21,705 frames, 2 missed, largest step 0.099 m. Seed 2 again and seed 3 on feel.39:
557 actions, 39,810 frames, 0 missed.

1. The two missed frames (helper error 15, now named): `Missed: the camera's local and world position were 0.423 m
   apart`, a grenade thrown while walking. The ordinary camera had no parent and its world matrix was behind its
   local position. Under the lock such a frame is held when the two are within 1.0 m (`unparented_behind`): what is
   written is the eye in the world. Without the lock it is refused as retail refuses it.
2. A swap in a sprint: at the change of camera the wanted eye went from 0.87 m ahead of the root (the head camera)
   to 0.29 m behind it (the rig), 1.17 m; `max_m` 1.0 cut the correction and the 0.17 m over it was shown at once,
   against the body's 0.15 m a frame: the eye stood still in the world for a frame (0.162, 0.009, 0.247 m). `max_m`
   is 1.6 and `teleport_m` 2.5. In the game: four swaps in a sprint 0.051 to 0.093 m, 0.41 to 0.50 m carried.
3. Not faults of the mod, read from the trace: a jump begun while walking (the root itself goes 0.03, 0.18, 0.36,
   0.40 m a frame: 0.19 m step change), and a fall or knock in an aim-walk (root 0.29 m a frame, then 0.97 m in one
   frame, the eye 1.40 m; no correction carried, frames before the edge).

Left: the same sprint swap inside a soak once measured 0.174 m; a stim and the stratagem list while walking 0.11 m.

The matrix on feel.39: 33 of 35, nothing missed; swap-primary 0.092 m and grenade-held 0.069 m over the 0.06 m limit.
Measured alone afterwards: twenty swaps 0.039 to 0.047 m, two held grenades 0.043 and 0.044 m. So the two were what
the phases before them left behind, not a change in the swap itself; the soak is the test for that. A stim and the
stratagem list while walking, three each: 0.034 to 0.052 m.


### After feel.39 (2026-11-22): the root's turning (not yet in a build)

A soak of feel.39 (18,108 frames) left steps of 0.06 to 0.17 m that the continuity itself made. The Trace lines of
feel.39 carry the root's place and not its axes, so the cases were replayed through the filter alone with the axes
fitted (`tools/harness/replay.py`, `replay_filter.c`: in each frame the turn with which the filter of that build
presents the eye the log shows). What the three largest have in common is the root's axes turning:

1. 22:38:05.662, head-camera frames inside a swap. With the axes left as they were the filter of feel.39 shows
   0.053 m in that frame; the log shows 0.176 m. With the axes turned by 0.26 rad in that one frame (and by under
   0.02 rad in the frames after) it shows the log's eye to the millimetre in all ten frames. The root rule of
   feel.38 did not fire (the step against the root on its course, 0.205 m, was larger than the step in the root's
   frame, 0.08 m). The 0.149 m taken up a frame earlier was kept against the root, 0.87 m away, and went round with
   its axes.
2. 22:33:17.3 to .7, the native view after a hold ended on a landing. The fit gives a root turning on the spot by
   0.2 to 0.6 rad a frame for nine frames while the rig eye stands in the world 0.6 m from it (not exact: up to
   0.07 m off the log in four of those frames). In the root's frame that eye goes round; each frame was a jump, the
   run ended, the speed was believed, the next frame was a jump again: 0.61 m carried and the presented eye going
   0.06 m a frame while the wanted one stood, then 0.139 m in the frame the turning stopped.
3. 22:28:33.416, head-camera frames after a change of camera: the same as 1 with less turn (0.09 to 0.17 rad a
   frame, the eye 1.68 m from the root): 0.048 then 0.141 m, 0.047 m beyond the root's own change of step.

The filter now reads every step a second way, with the root's turning left out (`camera_continuity.h`, THE
TURNING), and uses that reading in a frame the axes leave their course while something is taken up, and for an eye
that has not been going round with the root. The three cases as tests (`tests/reference/soak-6-steps.h`): 0.133,
0.236 and 0.047 m beyond the root's own change of step before, 0.046, 0.027 and 0.019 m now. The Trace lines carry
`axes=(...)`, the root's three axis rows, so the next soak needs no fit.

Left, both in that soak and not changed by this:
- 22:33:17.304, a landing: the root stops dead in one frame (0.250 to 0.001 m a frame), the rig eye in two (0.265,
  0.200, 0.054 m). The first is taken up as a jump (speed kept), the second is "slowing" and followed: the presented
  eye stops in one frame, 0.256 to 0.015 m, a frame after the root did.
- 22:34:02.810, a change of camera while 0.6 m is being caught up at 0.18 m a frame: the curve starts again from
  the present value and speed with a length that follows from the distance alone, so it brakes harder than
  `ease_accel` (0.178, 0.134, 0.053 m). Giving every restarted curve a length that allows for its speed fails two
  of the existing checks of a start at 3 m/s and was not kept.
- 22:34:02.543: the wanted eye's own slowing (0.068 m) with 0.9 m carried; followed, as designed.

### The camera style (2026-11-22): Stable or Bodycam (branch `style`, cut from `steps`; offline, not yet run in the game)

The player, after playing feel.39: "Having the "bodycam" mode and a more stable traditional FPS camera would be
optimal". Earlier the same day: "configurable dampening for the camera moves would be good too. They're very
intense right now."

So the lock gives one of two cameras, chosen by one option, `bodycam` (Locked view tab, a checkbox "Bodycam" directly
above the kick sliders; live, read with the sliders once a second by `lock_note`).

- **Bodycam** (on): what the code did until now, unchanged: the kick sliders and Camera damping as they are.
- **Stable** (off, the default): the numbers below, which are all in `src/camera_damp.h` (`TFP_STABLE_*`,
  `TfpCameraStyle`, `tfp_camera_style`); `tfp_kick_style_tuning` (`src/camera_lock.h`) and `lock_damp_eye` use them.

| | Bodycam | Stable | why |
|---|---|---|---|
| kick on foot (and in the native view) | View kick strength, up to View kick limit | none (gain 0) | walking, strafing, crouching, a reload, a grenade or a stim are not something a traditional camera shows |
| kick of a body thrown, down or off foot | View kick when thrown, up to 35 degrees | half of that slider, up to 35 degrees | being thrown should still be felt |
| the kick's low-pass | Camera damping (60 %: 180 ms) | 250 ms | a sway, not a jolt |
| head camera eye, low-pass | Camera damping (60 %: 180 ms) | 350 ms | the head's bobbing and jerks in an action mostly stay out of the view |
| head camera eye, most lag | 0.10 m | 0.25 m | at 0.10 m the eye is pulled along by any larger movement; 0.25 m lets the slow filter work, and a crouch or going prone (0.5 m) is still followed |

Everything else about the eye's damping is as it was: only the head's place against the root is filtered, the
forward and up offset come from the fresh view, never in full aim, never on a body that is down or off foot, a
carried lag is let go on 40 ms, the damped eye is handed to `last_relative`. The native view's eye is the game's in
both styles. In Stable the three sliders that only Bodycam uses (kick strength, kick limit, camera damping) are
drawn greyed with one line under the Bodycam row; "View kick when thrown" stays live. A `locked_view.json` without
the member reads as off with no note and no `.newer` copy, like `camera_damping` before it. So an update from
feel.39 changes the camera to Stable; ticking Bodycam gives the old one back with the saved slider values.

Measured in the tests (not in the game):

- A 20 degree nod of the head in one frame, 144 frames a second, sliders at their defaults
  (`tests/camera_lock_test.c`): Bodycam on foot 3.21 degrees (0.256 in one frame), Bodycam thrown 6.42 (0.517);
  Stable on foot exactly none, Stable thrown 2.78 (0.181). A thrown body that is on foot again: the 2.69 degrees
  carried are let go by at most 0.074 a frame, on the same 250 ms.
- Through the helper at 30 frames a second (`tests/camera_death_test.cpp` 6c): a thrown head, 28.6 degrees in one
  frame: Bodycam 8.83 degrees (3.03 in one frame), Stable 3.81 (1.12). The same nod on foot: Bodycam turns the view
  4.36 degrees in the ordinary camera and 1.53 in the native view, Stable 0.
- The eye's filter alone (`tests/camera_damp_test.c`): a 0.05 m jerk of the head there and back within 100 ms moves
  the eye 0.008 m in Stable (0.014 m in Bodycam at 60 %, 0.05 m undamped). A 0.5 m drop that stays: at most 0.25 m
  behind, within 0.05 m after 570 ms, 0.008 m left at 1.2 s.
- Through the helper (`tests/camera_smooth_test.cpp` 19), an action in the head camera (that jerk, then a second
  of 0.02 m bobbing four times a second): Bodycam at 60 % moves the presented eye 0.0084 m at most, 0.039 m in all,
  0.66 mm in one frame; Stable 0.0060 m, 0.022 m, 0.37 mm. The head camera's own 100 ms smoothing has already
  widened the jerk before either style sees it, which is why the farthest point differs less than the filters
  alone do. A 0.5 m drop in 150 ms: at most 0.31 m behind (0.25 m and that smoothing), within 0.05 m after 860 ms,
  0.019 m at 1.2 s. 0.25 m carried when the body leaves 'on foot': 0.20 m in the first frame out, 0.002 m after
  200 ms.

Not as first asked: the 0.05 m jerk was to move the eye under 0.005 m. A first-order filter on 350 ms passes
0.008 m of it; under 0.005 m needs about 650 ms, which follows a crouch to within 0.05 m only after about a second
(0.039 m left at 1.2 s). The 350 ms were kept and the test holds what they give (under 0.010 m).

Open: none of this has been seen in the game. Whether 350 ms reads as steady or as a camera on a string during a
stim, and whether a Stable camera should take any kick from a landing, is for the next session played by hand.

### feel.40 in the game (2026-11-22)

Both styles, ten actions each (walk, back, strafe, crouch, dive, grenade, melee, stim, reload, swap), nothing missed:
Stable 0.0 degrees of kick in all ten, steps up to 0.037 m; Bodycam, ticked through the panel, 2.4 to 5.8 degrees
(walk 3.7, back 4.0, strafe 4.5, crouch 2.4, dive 5.3, grenade 5.7, melee 3.7, stim 5.8, reload 4.7, swap 5.4), steps
up to 0.043 m. The checkbox saves and takes effect at once; it was left unticked.

The soak of seed 3 in Stable stopped itself after 94 actions (3.5 minutes): the game window was no longer in front
and the input helper refuses then. Up to there: nothing missed, nothing flagged, and no step change of 0.06 m or more
in 784 traced frames, where the same seed on feel.39 had 19 in 2,121 (7 of them the filter's). A full run is owed.

A thing the soak logs show and that is not a fault as far as the logs can say: the arm drive's `Rest:` line reads the
socket 0.18 m and about 98 degrees off the rig's own for seconds on end in soaks (two thirds of the seconds, on
feel.39 as well), beginning without a new binding. In a soak the player walks into walls and stands there, where the
body turns the weapon aside; the rig's own pose is not what the arms should match then. Not seen in a capture yet.

The owed soaks on feel.40, Stable: seed 3, 247 actions, 18,081 frames; seed 5, 269 actions, 18,146 frames; nothing
missed in either. Classified as before: seed 3 has 7 steps of 0.06 m or more that are the filter's own (0.163 m the
largest), where feel.39 had 7 on the same seed (0.174 m); seed 5 adds 4. So reading a step with the root's turning
left out did not lower the count in play, and did not raise it. It stays: with `axes=` in the Trace these two logs
can be replayed exactly (`/home/goose/.cache/tfp-soak-8.modlog`, `tfp-soak-9.modlog` on the machine that ran them).



### After feel.40 (2026-11-22): the full soaks replayed exactly (branch `logs`; offline, not yet in a build)

(Corrected the same day: the count used here, by the lengths of the steps, does not see an eye that changes
direction, and one of the two fixes made a fault it could not see. That fix is out again. Read this section with
"What the count did not see" below; the figures here are by the lengths.)

Two full soaks of feel.40 in Stable (seeds 3 and 5; one session, 3,592 Trace rows in 3,511 frames) left 11 steps of
0.06 to 0.163 m counted as the filter's. The Trace lines of feel.40 carry the root's axes, so the filter can be run
again as the helper ran it (`tools/harness/replay.py`: every Trace row one call, the second row of a frame the game
set twice too, the logged axes; the fit of the axes is only for older logs now).

**How exact.** The filter's state is known from the first row of a stretch at which the log carries no correction:
3,447 of the 3,592 rows. In 3,418 of those the filter of feel.40, replayed, shows the logged eye within 0.005 m
(all 1,807 of seed 3's). The other 29 rows are two events, each a decision that fell within the rounding of the
lines: at 00:34:32.739 a step of 0.0008 m whose direction decides between "slowing down" and "jump", and at
00:35:09.798 a step 0.0001 m over the measure (0.0407 against 0.0406 m, the measure itself moved by 0.1 ms of frame
time). All 11 steps are in frames that replay exactly. The Trace lines now carry the helper's clock (`tick=`), full
aim (`quiet=`), the filter's first frame (`start` in `taken=`), and places to 0.01 mm.

**The 11 steps by cause.**

1. *The body's own change of speed in one frame* (6: 00:21:53.294 0.163 m, 00:21:53.362 0.072 m, 00:27:13.539
   0.094 m, 00:30:09.385 0.100 m, 00:33:34.846 0.100 m, 00:38:46.845 0.098 m; a swap in a sprint three times, a
   sprint begun, a dive backwards half a second in, a dive begun). The root changes its speed in one frame (its step 0.237 to 0.053 m at .294, 0.199 to
   0.108 m at .846) and the eye follows a frame later. The first frame reads as the root re-seated and the eye goes on
   as it went; in the next the root's new course is kept and the eye, against the root, has the new speed whole. In
   three of them the wanted eye's own step changed as much (0.175, 0.081, 0.106 m): they count as the filter's only
   because 0.05 to 0.18 m was still carried. This is the design ("the body's own travel is not a jump") and is left.
   Handing the root's change of speed to the curve was tried: it hides five of the six (not 00:30:09.385), at
   0.06 m of eye ahead of the body per m/s with the curve at its longest (worked out, not measured: about 0.3 m for
   the stop at .294), and on a landing (soak 6, a root that stops from 7.5 m/s in a frame) it carried 0.79 m of eye
   into the ground, which `soak6_turn_after_hold` forbids. Not done.
2. *The end of a run of jumps* (4, and part of 00:30:09.385: 00:26:06.399 0.078 m, 00:30:10.988 0.064 m,
   00:33:35.314 0.062 m, 00:36:03.183 0.067 m). A run ended at the first frame that was no jump, and that frame was
   followed as an ordinary one: the presented eye, held at the kept speed through the run, took the wanted eye's
   speed in one frame. In three of them that frame was 0.039 m off the kept speed, 0.001 m under the measure.
   NOT fixed. Tried: the run's end taken up as its time-out is, the speed the run's frames and that one agree on
   handed to the curve. That hid the four from the count by lengths and threw the eye backwards elsewhere; taken out
   (below). 00:30:10.988 is gone with the fix of 3, the other three stand. (Keeping a run going above half the
   measure was tried first: it broke the flicker test and one of soak 6.)
3. *The speed believed for a motion dying down* (1: 00:26:06.131 0.077 m). 0.26, 0.21, 0.136, 0.083 m a frame; the
   middle, 0.175 m, was believed; the next frame went 0.053 m and was followed as slowing down, 0.12 m at once.
   Fixed: frames each slower than the one before are believed at the newest.

**Replayed, by the lengths** (the frames where the replay is exact; both logs, the second holds the first): 11 of
the filter's on the filter of feel.40 (0.163, 0.101, 0.100, 0.099, 0.094 the largest), 9 with the fix of 3 alone,
which is what the branch now has (00:26:06.131 and 00:30:10.988 gone). With the hand-over of 2 as well it was 5, all
of cause 1, and that figure was what the first write-up of this section gave.

#### What the count did not see (2026-11-22, the check of the two fixes)

**The measure.** The log's judge, and `replay.py` after it, took the change of the presented step as the difference
of two lengths, `| |c-b| - |b-a| |`. An eye that goes 0.04 m one way and then 0.04 m back has changed its step by
0.08 m and by nothing in length. So a filter that turns the eye round was counted as better than one that lets a step
through. Nor did the count say what a filter carries: a step can be hidden by carrying more for longer.
`replay.py` now judges the change as a vector, `| (c-b) - (b-a) |` (the wanted eye's the same way), prints the count
by lengths beside it, and gives for each filter the correction summed over the exact frames (metre-frames) and the
longest time more than 0.05 m is carried on after a change of camera (to the first frame at or under it; jumps taken
up on the way keep the time running).

**The table again** (exact frames; "feel.40" is main's filter, "3 alone" the branch as it now is, "2 and 3" the
branch as first written; the game's own and reduced by the vector measure):

| Log | Filter | Filter's steps, vector | largest (m) | by lengths | largest (m) | game's own | reduced | metre-frames carried | longest carry (ms) |
|---|---|---|---|---|---|---|---|---|---|
| Soak 8 (seed 3) | feel.40 | 12 | 0.214, 0.142, 0.107 | 7 | 0.163 | 5 | 2 | 180.8 | 833 |
| Soak 8 | 3 alone | 9 | 0.214, 0.142, 0.107 | 5 | 0.163 | 5 | 2 | 180.9 | 468 |
| Soak 8 | 2 and 3 | 6 | 0.217, 0.142, 0.095 | 4 | 0.164 | 5 | 3 | 192.1 | 800 |
| Soak 9 (both seeds) | feel.40 | 24 | 0.214, 0.142, 0.137 | 11 | 0.163 | 9 | 5 | 316.3 | 833 |
| Soak 9 | 3 alone | 20 | 0.214, 0.142, 0.137 | 9 | 0.163 | 9 | 5 | 315.0 | 566 |
| Soak 9 | 2 and 3 | 12 | 0.217, 0.142, 0.137 | 5 | 0.164 | 9 | 5 | 338.9 | 800 |

By the vector measure feel.40 has 24 steps, not 11. The 13 the lengths did not see are an eye that changes
direction by 0.06 to 0.12 m in a frame; 11 of them are still there (00:24:56.788, 00:25:52.009, 00:27:08.298,
00:30:10.655, 00:30:53.355 0.117 m and .421, 00:33:32.074, 00:33:35.513, 00:37:57.933, 00:39:30.581 and .882) and
have not been looked into for their causes. The largest, 0.214 m, is 00:21:53.294, the body's own stop (cause 1).
The older soaks, fitted axes, where the replay follows: soaks 2, 3 and 6 have 13, 26 and 14 by the vector measure
on the filter of feel.40 and 15, 20 and 11 now; by the lengths 8, 6, 5 and 8, 6, 6.

**The hand-over of 2 was a fault** (found by the check, with probes of its own; `tests/camera_continuity_test.c`,
THE RUN'S END IS NO SPEED, and `tests/reference/soak-9-runs.h`). At a run's end it believed what the run's jump
frames and the ending frame agreed on. The run's frames are the re-seat, and a re-seat is no speed:

- A rig re-seated in two frames, 0.09 m in each, that then creeps on at 0.5 mm a frame. The three frames go the same
  way and none is slower than the one before, so the re-seat was believed as the eye's speed and handed to the
  curve. At 16.7 ms a frame the presented eye went 0.072 m backwards in one frame and 0.24 m in all, 0.41 m was
  carried for 0.18 m, and it took 400 ms; at 8 ms 0.058 m a frame, 0.41 m in all, 0.59 m carried; at 33.3 ms
  0.046 m. Without the hand-over: never backwards, 0.18 m, 200 ms. The error grows with the frame rate (a speed is
  a step divided by the frame time) and the soaks ran at 30 frames a second only.
- A re-seat against the way of a moving eye (2 m/s, 0.15 m): the jump and the frame after it go opposite ways, one
  frame agrees with itself, 0 was believed and the eye that went on was taken up frame after frame: 0.21 to 0.23 m
  carried for 0.15 m.
- In soak 9 twice, both under the 0.06 m that is counted: 00:27:21.652 (re-seated 0.08 and 0.49 m, then standing:
  2.38 m/s believed for 0.01 m/s, what was carried grew from 0.446 to 0.532 m while the wanted eye stood, the
  catch-up 100 ms longer) and 00:30:02.242 (0.07, 0.14 and 0.18 m: 2.80 m/s believed for 0.14 m/s, the next two
  frames jumps for it).
- Of the four steps it was written for, by the vector measure: 00:33:35.314 was mended; 00:26:06.399 and
  00:30:10.988 were hidden by the second fault itself (0 believed where the eye happened to be nearly at rest);
  00:36:03.183 came back four frames later as the eye turning round, 0.074 m at 00:36:03.316 (1.81 m/s believed
  for a rig eye that stood from the next frame on).

**Why it was taken out and not repaired.** The repair had to keep the four steps mended by the vector measure, keep
every earlier test, and pass the cases above. What was tried:

- *A run goes on through its calm frames.* Within a run a frame that is no jump is taken up as well when to follow
  it would change the presented eye's step by more than the measure; once the run's time is over and two such frames
  have come in a row, what those frames agree on (not the run's jump frames) is believed. This passes the cases
  above (at 00:27:21.652 the kept speed, 0.47 m/s, stands one frame longer than on feel.40 before 0.01 m/s is
  believed) and every earlier test but the one for 00:26:06.399, and no probe scene is worse than feel.40 in its
  step (8 of 436 carry more: bursts of 0.09 m a frame at 50 ms, 0.16 to 0.24 m, where the step is halved). It mends 00:36:03.183, 00:33:35.314
  and 00:30:10.988 by both measures and does not mend 00:26:06.399: soak 9 has 17 by the vector measure and 7 by the
  lengths (two new: 00:30:09.419 0.069 m, 00:38:46.878 0.072 m), 322.9 metre-frames, 902 ms; soak 8 has 10, one more
  than the fix of 3 alone.
- *The same, with every calm frame of a run taken up.* Mends all four, but a rig that creeps on settles after 270 to
  300 ms, what is carried grows by 0.025 m while the wanted eye stands at 00:27:21.7, and the soak 6 check after a
  change of camera fails.
- *The same, with slowing frames held for the run's time too*, which is what 00:26:06.399 needs. Mends all four, and
  an eye that stops after a burst then coasts past it: 10 probe scenes worse by up to 0.04 m of step, up to
  0.08 m of overshoot, and the same soak 6 check fails.

00:26:06.399 is not a fault of the run's end at all. The run ends a frame earlier, and that frame can be followed
within the measure; the step is the frame after it, the rig eye's own slowing (0.025 m, followed as a motion slowing
down, by design) on top of the curve's own change of step in its first frames (0.030 m: 24 m/s^2 is 0.027 m a frame
per frame at 30 frames a second). Each is within what the design allows and together they are 0.066 m. The same sum
is behind the other two: up to 0.04 m under the measure and 0.027 m of the curve. To mend it the eye's own slowing
would have to be held or the curve begun more gently, and both are other changes than this one with costs of their
own. So the hand-over is out, the first variant is not in (it does not meet the condition and its gain is 3 of 20),
and its header is kept outside the tree (`~/.cache/tfp-logs-work/run-hold-variant/`) should it be taken up again.
The three steps are marked "not mended" in the tests, which hold the filter to adding nothing to the logged ones.

Considered and left: counting the curve's start speed in its length for every jump (the curves begun in a catch-up
reach 36 to 53 m/s^2 against the 24 asked for, 0.04 to 0.06 m a frame per frame at 30 frames a second) changed no
count and broke the test of a start at 3 m/s.

### feel.41 in the game (2026-11-22)

All 46 test scripts and the harness's python tests pass. The matrix in Stable: 34 of 35, nothing missed, no kick in
any phase; jump-vault 0.085 m (the body's own lunge when a jump begins while walking, as read from the trace on
feel.39), swaps 0.043 and 0.048 m, stim 0.036 m.

The soaks owed on feel.41: Stable, seed 3: 248 actions, 18,077 frames, nothing missed; Bodycam (ticked through the
panel, unticked after), seed 6: 259 actions, 18,111 frames, nothing missed. Judged by `tools/harness/replay.py` (the
vector measure, exact frames):

| Run | Filter's steps of 0.06 m or more | Largest | Carried | Longest carry after a change of camera |
|---|---|---|---|---|
| feel.40, Stable, seed 3 | 12 | 0.214 m | 180.8 m-frames | 833 ms |
| feel.41, Stable, seed 3 | 5 | 0.090 m | 106.3 m-frames | 501 ms |
| feel.41, Bodycam, seed 6 | 4 | 0.071 m | (the log holds both runs) | |

The same seed is not the same play (the timing of each action against the game differs from run to run), so the drop
from 12 to 5 is more than the one filter change accounts for in the replay of the feel.40 log (12 to 9); it is one
run against one run. Nothing got worse.


### After feel.41 (2026-11-22): the nine steps of its soaks by cause (branch `round2`; offline, not yet in a build)

The two soaks of feel.41 (`/home/goose/.cache/tfp-soak-10.modlog`, Stable, and `tfp-soak-11.modlog`, the same session
continued in Bodycam, which holds the first) replayed through the filter: 9 steps of 0.06 m or more by the vector
measure are the filter's where the replay is exact. That figure is with `replay.py`'s default for the build that
wrote the log, which is still feel.40's filter (9a3470b): it shows the feel.41 log in 3,156 of the 3,327 rows whose
state is known. With the filter that wrote the log (`--logged 66ee111`) all 3,327 are exact and there is a tenth
(02:30:34.577). Both are given below. A log of feel.41 or later wants `--logged` with its own build.

**The steps** (all in the native view; "taken" is the log's `taken=` in the nine frames before the step, the step's
own frame in brackets, and the two after; "own" is the wanted eye's own change of step, "curve" the change of the
correction's step in that frame, both as vectors, which add up to the shown one only when they point the same way):

| Time | Vector | Lengths | Own | Curve | Root's change | Carried | Taken | What it is |
|---|---|---|---|---|---|---|---|---|
| 02:21:31.078 | 0.062 | 0.047 | 0.035 | 0.028 | 0.000 | 0.064 | `jump - - - - - - edge - [-] jump -` | C |
| 02:21:31.445 | 0.064 | 0.062 | 0.048 | 0.015 | 0.000 | 0.000 | `- jump jump jump - - - - - [-] jump -` | D |
| 02:21:44.166 | 0.060 | 0.060 | 0.056 | 0.004 | 0.000 | 0.075 | `- jump jump jump - - - jump - [-] - jump` | D |
| 02:22:52.808 | 0.090 | 0.023 | 0.026 | 0.072 | 0.079 | 0.231 | `- - - - - - - jump root [jump+root] jump jump` | A |
| 02:22:52.842 | 0.078 | 0.061 | 0.079 | 0.054 | 0.012 | 0.203 | `- - - - - - jump root jump+root [jump] jump -` | E |
| 02:29:59.588 | 0.071 | 0.031 | 0.069 | 0.006 | 0.064 | 0.080 | `- - edge jump - - - - root [root] - jump` | E |
| 02:29:59.622 | 0.063 | 0.063 | 0.068 | 0.010 | 0.004 | 0.033 | `- edge jump - - - - root root [-] jump jump` | E |
| 02:31:12.739 | 0.066 | 0.052 | 0.006 | 0.072 | 0.000 | 0.055 | `jump - - - - - - jump jump [-] - -` | B |
| 02:34:34.959 | 0.062 | 0.020 | 0.020 | 0.079 | 0.001 | 0.091 | `jump jump - jump jump - jump jump jump [-] - -` | B |
| 02:30:34.577 (the tenth) | 0.061 | 0.060 | 0.069 | 0.129 | 0.011 | 0.170 | `- jump - jump - jump - jump jump [-] - -` | D |

**By cause.**

A. *The root keeps a turning it began the frame before, under an eye that does not go round with it* (1:
   02:22:52.808, the largest). A fault of the filter, mended. A dive forwards out of a sprint ("Motion: action=dive").
   The body leaves the ground and rolls: its root's axes turn by 0.063, 0.057, 0.052, 0.048 rad a frame about the
   root's own forward, and the rig eye is 1.25 m above the root, so it goes 0.07 m a frame sideways in the root's
   frame while it goes straight on in the world (its step in the world changed by 0.026 m). The frame the axes left
   their course in read as the root re-seated, rightly. In the next the root kept the turning, the filter took that
   for its course (THE COMPARISON), and the eye, not going round, was 0.068 m off the speed kept for it in the
   root's frame; with the turning left out it was 0.019 m off. The frame and the two after it were taken up as
   jumps, each holding the presented eye against axes that rolled on: it went 0.080, 0.060, 0.043 and 0.027 m to
   the side in four frames where the wanted eye went 0.017 m a frame, and 0.16 m of the correction was sideways.
   Neither rule of THE TURNING saw it: the axes were on their (new) course, and the average over 100 ms had one
   frame of evidence (0.016 m against the 0.020 m asked for).
B. *A run's last frame a hair under the measure* (2: 02:31:12.739, 02:34:34.959). A fault of the filter, mended;
   the same as three of the four of the feel.40 soaks that were marked not mended. At 02:31:12.739 the native view
   was entered after a swap, the body prone and still, and the rig eye was re-seated in three frames, 0.053, 0.052
   and 0.045 m. 0.048 and 0.046 m off the kept speed were jumps; the third was 0.0396 m off it against a measure of
   0.0406 m at that frame time, ended the run and was shown whole on the curve's steepest frame: the presented eye
   went 0.008 and then 0.060 m. At 02:34:34.959 it shows four frames later: the frame at .827 was 0.0375 m off the
   kept speed against 0.0411 m, was followed and averaged into the kept speed, half of a speed the eye did not have
   (1.29 m/s); the next three frames were jumps against it while the presented eye went on at it, and the frame the
   eye stopped in was "slowing down" and followed: 0.041 m one way and then 0.021 m the other.
C. *A followed frame under the measure on a curve that was begun steeper than asked for* (1: 02:21:31.078). The
   filter's, and not mended. The rig eye's own first move after a change of camera, 0.035 m (under the measure,
   followed, as designed), in the second frame of the curve begun at the change of camera from 0.089 m with the
   0.78 m/s the catch-up before it still had: 149 ms long by its distance alone, so 45 m/s^2 where 24 is asked for
   and 0.028 m of change in that frame. The sum is what was behind 00:26:06.399 of the feel.40 soak as well.
D. *The measure counting the eye's own move* (3: 02:21:31.445, 02:21:44.166 and the tenth, 02:30:34.577). Not
   faults. At .445 the rig eye stops (0.043 to 0.005 m a frame, 0.048 m of change) in the frame a catch-up ends
   (its last 0.021 and 0.004 m): the filter adds 0.015 m, and the step is 0.003 m short of counting as the game's
   own (the wanted eye's change must be 0.8 of the shown). At 02:21:44.166 the rig eye goes to and fro as the
   weapon comes back after an action (0.056 m), and at 02:30:34.577 it swings against the root after a swap in a
   sprint (0.069 m; the filter shows 0.061 m): both count as the filter's only because 0.03 m or more is carried in
   those frames, which a player does not see as a step. The filter adds 0.004 m to the one and takes 0.008 m off
   the other.
E. *The body's own change of speed in one frame, the eye a frame later* (3: 02:22:52.842, 02:29:59.588 and .622).
   The design working ("the body's own travel is not a jump"; cause 1 of the feel.40 soaks). At 02:22:52.842 it is
   the same dive as A a frame on: the root lost a third of its speed as the body left the ground (its step 0.190 to
   0.121 m) and the eye's step in the world followed in the next frame (0.190 to 0.116 m, 0.079 m). At
   02:29:59.588 a dive begun straight after a swap: the root's step 0.096, 0.140, 0.203 m, the eye's 0.083, 0.139,
   0.202 m a frame later (0.069 and 0.068 m), shown as 0.071 and 0.063 m.

**What was changed** (`src/camera_continuity.h`; the tests are built from the logged frames,
`tests/reference/soak-11-steps.h`, and fail on main's filter with the logged steps, 0.090 and 0.066 m).

1. For A, THE ONSET: a frame in which the root keeps a course it took up alone the frame before is followed, not
   taken up, when its step is a jump in the root's frame and none with the turning left out (the two half the
   measure apart), the axes are on that course within half the measure and their turn of the frame moves the eye by
   half the measure or more, and the eye has not been seen to go round with the root of late. What is carried keeps
   its direction in the world in that frame and the eye's speed in the root's frame is believed at once, the turning
   in it. 02:22:52.808: 0.090 to 0.039 m, one jump taken up in five frames where three were, 0.027 m of the
   correction to the side where 0.157 m was. The step after it, E's, is 0.085 m where it was 0.078 m (the eye's own
   0.079 m; the frame before it is no longer held back), and the frame after that 0.058 m where it was 0.034 m.
   Made up, at four frame times (the soaks ran at 30 frames a second only): a body running at 6 m/s whose root
   begins to roll at 1.8, 4 or 8 rad/s under an eye 1.25 m up that goes straight on, with and without a re-seat of
   0.15 m two frames before. Main's filter was right in 14 of the 24 and in the other 10 carried the eye round:
   up to 0.83 m to the side and 1.01 m carried (8 rad/s at 8 ms), 0.113 m of step change (1.8 rad/s at 50 ms).
   Now nothing is taken up for the roll in any of the 24 (0.032 m of step change at most, the re-seat's own). An eye
   that does go round with the roll is the same to the digit in all 24. The three conditions each came from a case
   that went wrong without it: without "of late" a rig re-seated in an even turn of 6 to 12 rad/s at 50 ms a frame
   was let through whole (six of the 864 cases of the existing test); without "a course kept from the frame
   before" a body thrown while it turns carried 0.67 m for 0.37 m (nine of its 72 cases; the two readings differ
   there by the speeds kept for them, not by a turn); without "the turn moves the eye" the frame 00:30:53.388 of
   the second feel.40 soak, whose kept course does not turn, was followed and made 0.135 m where 0.055 and 0.085 m
   had been.
2. For B: once in a run, within its time, a frame that is under the measure by a tenth of it at most and goes the
   way the jump before it went (within 45 degrees) is taken up with the run. Nothing is believed for it; the kept
   speed stands as in every frame of a run. 02:31:12.739: 0.066 to 0.028 m (0.018 m more carried); 02:34:34.959:
   0.062 to 0.027 m. Of the feel.40 soaks: 00:36:03.183 0.068 to 0.029 m, 00:33:35.314 0.074 to 0.051 m,
   00:33:35.513 0.085 to 0.046 m (0.06 m more carried), 00:39:30.882 0.063 to 0.028 m; four under the count as well
   (0.051 to 0.054 m, now 0.019 to 0.039 m). From change 1 to change 2 no step of 0.05 m or more in the four logs
   grew by more than 0.002 m and none appeared.
   What it costs, made up at four frame times: a re-seat of 0.15 m and one more frame at 0.97 of the measure the
   same way, then rest: the step changed by 0.040, 0.042, 0.051 and 0.112 m (8, 16.7, 33.3, 50 ms a frame), now by
   0.001, 0.005, 0.016 and 0.029 m, with 0.027 to 0.063 m more carried and 32 to 100 ms longer to settle. When the
   eye goes on like that for eight frames (a motion, not a re-seat's end) the same more is carried, as long, and the
   step is 0.001 to 0.004 m more. That is why it is once in a run: with every such frame taken up, at 8 ms a frame,
   0.28 m more was carried and it took 230 ms longer. A frame the other way, or at 0.8 of the measure, is as before.
   Tried and not kept: three quarters of the measure (soak 11 down to 5, but the dying-down probe at 8 ms carried
   0.027 m more and settled 8 ms later); the same with a speed believed when such a frame comes after the run's time
   (two frames of the logs worse: 02:21:31.078 0.062 to 0.068 m, 00:32:09.176 0.040 to 0.050 m).

**Replayed** (exact frames, by the vector measure; soaks 8 and 9 are the feel.40 logs with the filter under test;
"by 66ee111" and "by 9a3470b" say which filter decides what is exact):

| Log | Filter | Filter's steps | largest (m) | game's own | reduced | by lengths | metre-frames | longest carry (ms) |
|---|---|---|---|---|---|---|---|---|
| Soak 8 | main | 9 | 0.214, 0.142, 0.107 | 5 | 2 | 5 | 180.9 | 468 |
| Soak 8 | round2 | 9 | 0.214, 0.142, 0.107 | 5 | 2 | 5 | 181.0 (+0.1 %) | 468 |
| Soak 9 | main | 20 | 0.214, 0.142, 0.137 | 9 | 5 | 9 | 315.0 | 566 |
| Soak 9 | round2 | 16 | 0.214, 0.142, 0.137 | 9 | 5 | 7 | 316.4 (+0.4 %) | 566 |
| Soak 10, by 66ee111 | main | 5 | 0.090, 0.078, 0.064 | 7 | 2 | 2 | 119.7 | 566 |
| Soak 10, by 66ee111 | round2 | 4 | 0.085, 0.064, 0.062 | 7 | 1 | 2 | 119.9 (+0.2 %) | 501 |
| Soak 10, by 9a3470b | main | 5 | 0.090, 0.078, 0.064 | 7 | 2 | 2 | 106.3 | 501 |
| Soak 10, by 9a3470b | round2 | 4 | 0.085, 0.064, 0.062 | 7 | 1 | 2 | 107.3 (+0.9 %) | 501 |
| Soak 11, by 66ee111 | main | 10 | 0.090, 0.078, 0.071 | 7 | 3 | 4 | 210.7 | 566 |
| Soak 11, by 66ee111 | round2 | 7 | 0.085, 0.071, 0.064 | 7 | 2 | 4 | 211.9 (+0.6 %) | 501 |
| Soak 11, by 9a3470b | main | 9 | 0.090, 0.078, 0.071 | 7 | 2 | 3 | 173.5 | 501 |
| Soak 11, by 9a3470b | round2 | 6 | 0.085, 0.071, 0.064 | 7 | 1 | 3 | 175.6 (+1.2 %) | 501 |

With change 1 alone soaks 8 and 9 are main's to the digit, soak 10 is 4 and soak 11 is 9 (8 by 9a3470b), with 0.5 %
less carried. What is left in soak 11: C (1), D (3), E (3). The older soaks with fitted axes, where the replay
follows (2, 3 and 6): 15, 20 and 11 on main, 14, 20 and 12 now. The one more in soak 6 (22:37:22.227, 0.070 m) is in a
stretch where the fit turns the axes by 0.22, 0.85 and 0.36 rad in three frames and the replay carries 0.53 m where
the log carried 0.30 m; it is the onset rule acting on turns the game did not make, and is noted, not explained away:
axes that really jumped about like that (a body thrown about) are not in any log with real axes.

**The probes** (`tools/harness/probes.py`, `probe_filter.c`; `tests/test_harness_probes.py` runs them with the
harness tests). The made-up scenes the check of the feel.41 branch was made with, each at 8, 16.7, 33.3 and 50 ms a
frame, with main's numbers recorded as the bound (`tools/harness/probes-main.txt`): the largest change of the
presented step as a vector (m), the largest correction carried (m), the time to settle (ms to the last frame over
0.005 m). The branch is the same as main to the last digit in all 84 (21 scenes at four frame times); the filter
of feel.40 is worse in two (dying-down at 33.3 and 50 ms), which the test holds the comparison to.

| Scene | 8 ms | 16.7 ms | 33.3 ms | 50 ms |
|---|---|---|---|---|
| a re-seat of 0.15 m, standing | 0.0014, 0.1500, 168 | 0.0055, 0.1500, 167 | 0.0187, 0.1500, 166 | 0.0325, 0.1500, 150 |
| the same, walking at 4 m/s | 0.0014, 0.1500, 168 | 0.0055, 0.1500, 167 | 0.0187, 0.1500, 166 | 0.0325, 0.1500, 150 |
| 0.09 m in each of two frames, then creeping 0.5 mm a frame | 0.0019, 0.1793, 192 | 0.0056, 0.1769, 200 | 0.0156, 0.1687, 200 | 0.0900, 0.0000, 0 |
| 0.05 m in each of three frames, then creeping | 0.0016, 0.1472, 184 | 0.0046, 0.1393, 184 | 0.0120, 0.1167, 200 | 0.0500, 0.0000, 0 |
| 0.15 m against the way of an eye going 2 m/s | 0.0014, 0.1500, 168 | 0.0055, 0.1500, 167 | 0.0187, 0.1500, 166 | 0.0325, 0.1500, 150 |
| 0.15 m along its way | 0.0014, 0.1500, 168 | 0.0055, 0.1500, 167 | 0.0187, 0.1500, 166 | 0.0325, 0.1500, 150 |
| the start of a sprint, no re-seat | 0.0017, 0.0000, 0 | 0.0072, 0.0000, 0 | 0.0279, 0.0000, 0 | 0.0611, 0.0000, 0 |
| head to native in a sprint, 1.17 m apart | 0.0027, 1.1718, 384 | 0.0113, 1.1718, 384 | 0.0406, 1.1718, 366 | 0.0824, 1.1718, 350 |
| a flicker of 0.19 m, 2 to 12 frames (the worst) | 0.0014, 0.1900, 160 | 0.0068, 0.1900, 267 | 0.0244, 0.1900, 500 | 0.0417, 0.1900, 650 |
| a motion dying down (0.26, 0.21, 0.136, 0.083, 0.053 m a frame) | 0.0341, 0.7308, 408 | 0.0368, 0.6982, 434 | 0.0250, 0.6168, 466 | 0.0480, 0.5436, 450 |
| the root 0.6 m on in one frame, the eye a frame later | 0.0015, 0.6000, 368 | 0.0061, 0.6000, 367 | 0.0220, 0.6000, 400 | 0.0465, 0.6000, 400 |

At 50 ms a frame the measure is 0.09 m (36 m/s^2 times the frame time squared), so the two spread re-seats are no
jumps there and are shown as they come. None of these scenes has a frame in the tenth under the measure or a root
that turns, which is why neither change moves them; what the changes do in scenes that have is in 1 and 2 above and
in the tests.

**What was not changed, and why.**

- C, the curve begun steeper than asked for. Two lengths for a curve that starts with a speed were tried on top of
  the two changes. *Every restarted curve allows for its whole start speed* (the earlier section's "considered and
  left", measured now): three existing checks fail (a start at 3 m/s, twice; soak 8's stop in a sprint), soak 9
  carries 339.6 metre-frames (+7.8 % on main), the longest carry is 635 ms on all four logs (468 and 566), and 37
  of the 84 probes are worse (the spread re-seats settle 17 to 50 ms later). *Only the part of the start speed that
  makes the correction grow*: one existing check fails (soak 8's stop in a sprint), soak 9 carries 323.0
  metre-frames (+2.5 %) and soak 11 218.9 (+3.9 %), the longest carry is 601 ms on soaks 9 to 11, and 28 probes are
  worse (a flicker settles up to 150 ms later). Both take soak 9 from 16 to 12, and neither removes 02:21:31.078.
  Outside the conditions; not done.
- D is the measure. To count 02:21:31.445 as the game's own the judge's 0.8 would have to be 0.75; the two with a
  correction carried are the game's own by any reading. The judge was left as it is, so the figures stay comparable.
- E is the design, as before: handing the root's change of speed to the curve carries the eye into the ground on a
  landing (the feel.40 section).
- Not looked into: of the 16 left in soak 9, the nine that no section names a cause for (00:24:56.788,
  00:25:52.009, 00:27:08.298, 00:30:10.655, 00:30:53.355 and .421, 00:33:32.074, 00:37:57.933, 00:39:30.581).
  00:30:53.355 and .421 (0.117 and 0.085 m with 0.97 m carried) look like a speed kept across a change of camera
  in a sprint, read once in passing; they are the next thing to look at.

Tests on the branch: `tests/run-camera-continuity.sh` 2,462 checks (main's 2,396 unchanged and 66 new), the other
four `run-camera-*.sh` pass (2,765, 229, 643 and 260 checks), the harness's python tests 24 (21 and the three for
the probes), all 46 `tests/run-*.sh` pass, `build.sh --feel` builds. Nothing here has been run in the game.

### feel.42 in the game (2026-11-22)

46 test scripts and the harness's python tests pass (the probes among them). Matrix, Stable: 35 of 35. Soak, Bodycam,
seed 6: 259 actions, 18,101 frames, nothing missed, one flagged (0.062 m). The Stable soak before it stopped itself
after 14 actions (the game window was not in front) and is owed again. By `replay.py` on the session's log (matrix and
both soaks): 5 of the filter's steps of 0.06 m or more, 98.1 metre-frames carried, 435 ms the longest carry after a
change of camera.

