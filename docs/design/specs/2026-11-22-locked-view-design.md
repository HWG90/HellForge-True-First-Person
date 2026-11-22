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
any camera kind (the death cameras, kinds 10 and 11, included). The eye is the head, so it follows the body to the
ground. In a camera that is not kind 4 or 6 the direction is the last first-person direction. No glide in from the
game's camera: the lock never shows the way from third person. A spectator camera on another avatar is left alone
(the camera record is not ours).

Open, to be settled in the game: what the controller above the helper does on death (it retires the lease today);
which meshes are hidden then; respawn.

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
