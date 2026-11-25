# The left hand on the weapon's grip (round 23, 2026-11-24)

The user's report, with a screenshot: "Hand does not properly grip weapon attachments. I.e. not gripping fore-end
vertical grip". In the shot a scoped rifle is at the hip in first person, and the left hand lies flat against the side of
the receiver by the magazine well. It should be round the fore-end or the vertical grip under the barrel.

## Diagnosis

### Where the left hand comes from today

With Animated arms on, the arm drive (`src/arm_drive.c`, `src/arm_graft.h`) writes the third-person body's arms onto the
first-person rig at the primary draw. Rig nodes 10 to 49 are copied from the body's bones of the same names.

- The two arm roots, node 10 (left upper arm) and node 30 (right upper arm), hang on the rig's presentation root, node 7.
  Each is the body's bone seen from the body's head and view (the anchor), times one `alignment`.
- The rest of each arm keeps the body's own local poses: node 12 (forearm) under 10, node 13 (hand) under 12, and the
  fingers under 13.
- `alignment` is chosen so that the body's right-hand weapon socket lands on the rig's own weapon socket (node 8). It is
  0.3.37's `graft_align_action`, kept per weapon by the resting-pose rules.

So the drawn gun sits where the body's right hand holds it, and the drawn left hand sits where the body's left hand is
relative to the body's right hand.

### Why that is wrong for the left hand

The game does not hold the body's left hand by its right hand. Retail `grip_hand.lua` measured this live on 2026-11-13
and 14 and wrote it at the top of the file. Every weapon unit carries a left-hand grip node, name `67C94E4B`. The game
pins the body's left hand on that node exactly, in position and rotation, in the world. It does so also while the
first-person rig is the view, which is when the gun hangs on the rig and not on the body. With a foregrip fitted, the
attachment has its own node and the hand is on that one (0.0 mm), while the weapon's own node is 17 cm away. On a few
weapons the game does not pin the hand at all (54 cm off).

The retired controller's capture (`paired-arm-poses-0.3.34.npz`, 120 frames over 9 s, read only) shows the same thing.
The body's left hand stays 0.226 m from the rig's weapon socket in every frame, rigidly, while the body sways. It is
also 0.54 m from the body's own weapon socket. The body's left hand is on the first-person gun's grip in the world.

When the arm drive moves the body's arms through the right hand's alignment, that left hand is carried away from the
grip it was on. On the recorded frames, with the drive's own maths (bind, align to the rig's socket, pose; checked in C
by `tests/arm_drive_test.c` `test_grip_holds` and in numpy), the drawn left hand is this far from the grip as the drive
draws the gun:

| frame | 0 | 15 | 30 | 45 | 60 | 75 | 90 | 105 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| distance (m) | 0.388 | 0.384 | 0.392 | 0.402 | 0.365 | 0.388 | 0.415 | 0.400 |
| turn (degrees) | 17.4 | 18.2 | 16.5 | 14.4 | 12.4 | 15.5 | 18.3 | 18.9 |

The rig's own animated left hand is 0.070 m from the same node. The drawn hand is 37 to 42 cm back along the gun,
which is the receiver and magazine well in the screenshot. Weapons with a foregrip or vertical grip show it most,
because their grip is furthest forward. The hand cannot follow an attachment either: nothing in the drive knows the
attachment's node.

The drive does read the rig's world matrices and refreshes the gun's units. It does not read any weapon node for the
left hand: `grip_hand.c` is the only code that knows node `67C94E4B`, and it is refused (next section).

### Why `grip_hand.c` is held back, and why it stays so

The refusal reads: "in 0.6.0-feel.18 the hand grips turned a hand bone by 70 degrees every frame and by up to 180
degrees and 0.55 m at times". No log of that run survives in the repository. Only the refusal text (commit 87e1a3a)
does. Reading the module (a line-for-line translation of retail `grip_hand.lua`) against that observation finds the
following.

- **The 70 degrees every frame is what the module was built to write.** It puts the rig's hand on the grip node's
  rotation, which retail measured at up to 54 degrees from the rig's own animated hand on a rifle. It writes that
  offset again on every update, because the offsets live on the arm pieces' linked bones and must be renewed each frame.
  A big write every frame is how the module works, not a defect.
- **The 180 degrees is a quaternion sign defect.** The hand's turn is `Quaternion.lerp(hand_old, grip_rotation,
  weight)` (gh 547, 579, 611, 391). The C value maths' `q_lerp` (`src/wire_engine.c`) blends the four components
  without checking the sign. A rotation and its negated quaternion are the same rotation, and the engine's world
  rotations come with either sign. When the two inputs fall on opposite signs, a weight between 0 and 1 goes the long
  way round: up to 180 degrees, and through a near-zero quaternion at the middle. The weight is fractional whenever the
  hand is 20 to 32 cm from the grip (`weight()`) or while easing in. The in-game check of `Quaternion.lerp` compares
  one pair whose dot product is positive, so it passes either way.
- **The 0.55 m is the remembered grip coming back at full weight.** When no node is pinned and the bone in use is
  lost (`kept < 0`, for instance after a swap), the `memory` loop in `left_arm` (`src/grip_hand.c`) takes the last remembered candidate
  that is still a live unit. Retail's own notes say the weapon on the back is still a live unit with a grip node.
  `pin` is then set to 1 at once, with no ease, so the hand jumps to a remembered grip up to `NONE` = 32 cm from the
  animated hand. The arm solve can add its own swing to that.
- **And it no longer fits.** The module reads the rig's world poses in the tick and writes offsets onto armour pieces.
  Since round 13 the rig holds the game's own values whenever the tick looks, and the drawn arms are the drive's
  (`2026-11-24-residual-jumps.md`, the table under "Round 13"). Its offsets would be computed against arms that are not
  drawn.

The first two can be fixed: a sign check in the blend, and an ease plus a distance test on the remembered grip. The
third cannot without rebuilding the module on the drive. So the module stays held back, `arms_on.txt` still switches it
on to compare, and its insights move into the arm drive. Those insights are the grip node, choosing the node the
body's hand is on, keeping that node while the hand is on no other, and the body's finger shape.

## The fix

`src/offhand_grip.h` (pure maths) and `src/arm_drive.c` (reading the gun and switching), behind "First Person Arms".

### The target, read each driven frame (`grip_target`)

1. The gun's units are collected as the drive already does for its refresh: the gun on the rig's socket, then its
   attachments, parents first.
2. Each unit's names are read once, and the nodes named `67C94E4B` are kept, up to 4 a unit. A unit is read again
   when its object, reference or node count changes.
3. Each grip node's world position is compared with the body's left hand (body node `l_hand`). The node the body's hand
   is on (within 3 cm) is taken and kept: a foregrip or vertical grip's own node when one is fitted, the gun's own
   otherwise. The retail rule applies: the node in use stays unless the hand is on another. It is let go when the gun
   changes.
4. The node is expressed in the frame of the rig's weapon socket through each unit's own matrices. That is the grip
   relative to its unit's root, times the root's local record on its parent's node, up to the gun's root and its link to
   the socket (link `8 << 16 | 2`). A unit the game evaluated a frame behind its parent still gives the right relation.
   When the chain does not hold (a link that is not kind 2, a matrix that is not rigid), the last good reading is used.
   With none, the state is "stale".
5. The hand is to be on the node while the body's hand is within 3 cm of it, and stays held until the body's hand is
   10 cm away. The target is the node times the socket local this frame writes, so it is the grip where the gun is drawn.

### The reach (`tfp_offhand_step`), in the frame of node 7

The joints are the upper arm (node 10, of 7), the forearm (12, of 10) and the hand (13, of 12). Node 11 (the upper-arm
twist), 29 and 58 (forearm), and the fingers ride on them.

- **The hand.** It goes from where the body's pose drew it towards the target by the weight. The weight eases over
  0.15 s (a smoothstep), the retail EASE. The hand takes the target's turn by the same weight, the shorter way round:
  the sign of the quaternion is chosen, never inherited.
- **The arm.** The shoulder stays. The elbow is put in the plane of shoulder, target and a pole that points down and a
  little out (−0.35, −0.15, −0.92 in node 7's axes, where the rig's own left elbow hangs at the hip). A quarter of the
  previous frame's side is added, so the elbow does not jump where the target comes near the pole's line. The arm never
  straightens past 97 % of its length; a grip further away is reached as near as that allows.
- **Corrections.** The result is a correction of each joint's rotation. Positions, which are the bone lengths, are never
  written differently. A correction changes from the previous frame's by at most 720 degrees a second and never more
  than 30 degrees in one frame: 5.8 degrees at 8 ms, 12 at 16.7 ms, 24 at 33.3 ms, 30 at 50 ms. No solve can turn a joint
  over in one frame, and letting go returns to the body's pose at the same rate.
- **When it does not reach.** A target further than 0.60 m from the drawn hand, or than 1.15 times the arm's length
  from the shoulder, is not taken; the arm eases back to the body's pose. The measured offset is 0.37 to 0.42 m. A pose
  that is not rigid, or a bone with no length, leaves that frame as the drive had it.
- **Time.** It comes from the tick's clock (`tfp_arm_drive_set_rest`). The first frame, a frame with no time since the
  last, or a gap over 250 ms (another camera, a refusal) changes nothing. After such a gap, or on another rig, the
  corrections start again from none.

### When it is off

- The option "First Person Arms" (stored key `fird_person_arms`, default on). The menu no longer greys it out unless the
  arm drive's render hook contract is refused. Animated arms must be on too: it is the drive.
- The test file `grip_off.txt` beside the DLL leaves the hand as the body puts it, to compare.
- A handgun at the hip, by the catalogue's family `sidearm`: held in one hand, as the user asked earlier.
- Aiming, both full ADS and point shooting: the drive's copy fades out (weight 0, `src/arm_weight.h`) and the game's
  own aim pose holds the gun. The reach is not asked for then and eases back in when the copy fades back.
- The body's hand on no grip node: a reload, an ammo check, a melee, and weapons the game does not pin. Each shows as
  the body has it.
- No gun on the rig (a hold, a stow).

### The log

- At start: `Grip: the left hand of the drawn arms holds the weapon's grip node ...`, or with `grip_off.txt`,
  `Test: grip_off.txt: ...`.
- Once a second while the drive runs:
  `Grip: 60 frames, 58 held: on the grip; an attachment's grip node (2 grip nodes on 3 of the gun's units), the body's
  hand 0.002 m from it; hand 0.000 m from the grip (most 0.001 while held), largest joint step 3.1 degrees (allowed 12.0),
  weight 1.00`.
- Once whenever it lets go for a new reason: `Grip: left as the body holds it: the body's left hand is on no grip
  (a reload, or a weapon the game does not pin the hand to)`. The other reasons are a handgun at the hip, aiming, no
  gun, no grip node, the gun's units could not be read, out of reach or too far, and pose not usable.

## Tests

- `tests/run-offhand-grip.sh` (`tests/offhand_grip_test.c`): a model left arm with the rig's bone lengths (0.3027 and
  0.2872 m), built with the sanitizers and again at -O2.
  - 400 random targets in reach: the hand ends on them within 0.02 mm, turned as they are within 0.01 degrees. The
    shoulder stays, the bone lengths are kept, the positions written are the drive's bit for bit, and the result is
    rigid.
  - 100 targets past the arm's length (within REACH): the hand stops on the line to them at 97 % of the arm, with the
    elbow off the line and below it.
  - Refusals: out of reach, too far from the drawn hand, no target, not wanted, not rigid, no time, a bone of no length.
    Each eases back or leaves the poses bit for bit.
  - Release: back to the drive's pose within the step limit every frame, ending on it exactly.
  - 40 random target paths of 400 frames at each of 8, 16.7, 33.3 and 50 ms, with jumps, dropped targets and unwanted
    frames: no written joint ever turns more than the frame's allowance.
  - A target going round the shoulder, through the side and behind it, at three heights: the elbow's side moves by at
    most 7.3 degrees in a frame and never turns over.
  - 2,000 rotations: the shorter-way share of a turn, its negated quaternion, and the round trip.
- `tests/run-arm-drive.sh` (`tests/arm_drive_test.c`): the real drive on the fake player with a recorded frame.
  - `test_grip_holds`: with the grip node where the recorded body's left hand is, the drawn hand goes from 0.388 m /
    17.4 degrees off to 0.0000 m / 0.00 degrees. It gets nearer in every frame of the ease, and the gun is drawn where
    it was. Only nodes 10, 12 and 13 change, and only in rotation. Switched off, it returns to the plain drive exactly.
  - `test_grip_foregrip`: an attachment's node where the body's hand is, and the gun's own 17 cm from it: the
    attachment's node is held. With the gun evaluated a frame behind its rig (4 cm off), the hand stays on the grip.
  - `test_grip_left_alone`: the body's hand off every grip, one-handed, aiming (weight 0) and switched off. The drawn
    arm is the plain drive's bit for bit, with the right state. Held, then the body's hand leaves: back within the limits.
  - `test_grip_step_limit`: a body alternating between two recorded frames and a wandering grip at 8, 17, 33 and 50 ms
    frames. The largest correction step equals the allowance (5.8, 12.2, 23.8, 30.0 degrees) and never exceeds it.

## What only the game can show

- Whether the game pins the body's left hand on the first-person gun's grip node on this build (25480438) and these
  weapons. That is retail's live finding on the 2026-11 build, and the 0.3.34 capture agrees. A weapon where it does not
  says `Grip: left as the body holds it: the body's left hand is on no grip`.
- Whether node `67C94E4B` is on the gun and its attachments here (the `Grip:` line counts nodes and units).
- How it looks: the wrist, the fingers (the body's shape round this grip) and the sleeve at the elbow.
- Whether the ease and the limits read as smooth when the hand comes off for a reload and goes back.

## To try in the game

Animated arms and First Person Arms on. Watch the `Grip:` lines, and repeat one case with `grip_off.txt` to compare.

1. **A rifle with a vertical grip or foregrip** (the screenshot's case) at the hip, standing still. The left hand
   should be round the vertical grip or fore-end, fingers closed on it. Expect `held` near all frames, "an attachment's
   grip node", the body's hand under 0.01 m and the hand under 0.005 m from the grip.
2. **The same rifle without the attachment**, or another rifle: the hand on the gun's own fore-end, "the gun's own
   grip node".
3. **The Scythe** (energy, no magazine): the hand on its fore-end.
4. **A pistol** (Talon, Peacemaker) at the hip: one hand, `Grip: left as the body holds it: a handgun at the hip`. Then
   point shooting and full ADS: the game's own two-handed aim pose.
5. **Walk, sprint, crouch, prone**: the hand stays on the grip. Prone and sprint are where the body's arms differ most,
   so look for the elbow crossing the view or the hand stopping short; the largest joint step in the line says how
   hard the limits worked.
6. **Reload, ammo check, melee**: the hand leaves the grip as the body's does, and comes back within about 0.15 s after.
   No snap, no 180-degree turn. `left as the body holds it: ... on no grip` during the reload.
7. **Aim in and out** (full ADS and point shooting): no jump of the left hand when the copy fades back in.
8. **Swap weapons** several times: the new weapon's grip is found (the node count may change), with no reach towards
   the weapon on the back.
9. **`grip_off.txt`**: the hand back by the receiver, as in the screenshot. That confirms the change is this.

## Round 26: the hand on the grip, and steady (2026-11-24)

feel.52 (soaks 33 and 34, the `Grip:` lines) found the node every time (the attachment's, 2 nodes on 2 of the gun's
units) and never refused one, but held only 2,714 of 19,690 and 7,016 of 21,362 driven frames. While held the hand was
usually 0.03 to 0.05 m from the grip, at most 0.106 m. The largest joint step reached the frame's allowance (23 to 30
degrees) in 41 of 228 "on the grip" seconds, and at reloads, out of reach and easing.

### Why the hand stopped short

- In a held frame (full weight, no step cut) the hand is wherever the solve put it, so the miss is the solve's own. The
  only way the solve misses its target there is the 97 % straightening limit: the hand stops on the line to the grip at
  0.97 of the arm, and the miss is the grip's distance from the shoulder less that.
- The most the miss can be is then (REACH − STRETCH) × arm = (1.15 − 0.97) × 0.5899 m = 0.106 m. The log's most while
  held is 0.106 m. The median 0.043 m is a grip 1.04 to 1.05 arm lengths from the shoulder.
- Why it is that far: the drawn shoulder is the body's, carried by the alignment that puts the body's right hand on the
  rig's socket: it sits by the drawn gun as the body's shoulder sits by the body's right hand. The body's real shoulder
  reaches the grip in the world (0.36 m away on the 0.3.34 capture, 0.62 of the arm), but the body's right hand is not
  where the drawn gun is. On the capture's gun (grip 0.21 m forward of the socket) the drawn shoulder is 0.78 to 0.85
  arm lengths from the grip (the rig's own animated shoulder 0.845). Every 0.1 m further forward along the gun adds about 0.12. A fore-end or
  vertical grip, which feel.52's rifles carried, is 1.0 to 1.15 arm lengths away.
- Not the cause: the bone lengths (the rig's 0.3027 and 0.2872 m are the body's own), the ease (weight 1.00 in every
  held line), the target read a frame late (that would miss while moving and match while still; the misses are steady,
  0.048 m with a 1.0 degree step).

### Why it was held so little

Of the frames with the grip on, by the line's state:

| | soak 33 | soak 34 |
| --- | --- | --- |
| a handgun at the hip (one hand, as designed) | 14,026 | 10,585 |
| the body's hand on no grip (reloads, melee) | 1,303 | 1,368 |
| out of reach or too far | 540 | 768 |
| no gun on the rig, aiming | 727 | 1,444 |
| easing | 331 | 474 |
| on the grip | 2,763 | 6,723 |

Without the handgun the hand was held in 48 % and 65 % of the frames. The rest: reloads and aiming, which are as
designed; the refusals, part of them a grip swaying about 1.15 arm lengths (taken, refused, taken); and frames with a
cut step, which are not counted as held.

### Why the joint steps reached the limit

- **Easing** moved the hand 0.39 m in 0.15 s: about 4.5 frames at 30 frames a second, so each frame wanted more than
  the allowance. Easing out when the target went (a reload) had no ease at all: the corrections went back to none at
  the limit's rate.
- **The ease began and ended with a jump.** The reach at a small weight solved the arm for a hand barely moved, which
  still put the elbow in the pole's plane at once.
- **What was limited** was each joint's correction, not the joint as drawn. A body whose arm swings while the grip
  stays (the body's left arm follows the grip as the view turns) needs a correction that changes as fast as the body,
  and the limit then made the drawn arm swing instead. On a model arm swinging 20 degrees a frame at 8.3 ms, main held
  0 of 60 frames with the hand up to 0.079 m off.
- Not the cause: the target jumping between nodes (the gun's own node is 17 cm from the attachment's and is never taken
  while the hand is on the attachment's), or the arc's axis turning over (the body's upper arm is 31 to 33 degrees from
  the line to the grip on the capture, far from 180).

### The fix (`src/offhand_grip.h`)

- **The shoulder comes forward.** Where the grip is further than 0.97 of the arm, node 10's position moves along the
  line to it by the rest, up to 0.21 of the arm's length (0.124 m), at most 0.6 m/s. It stands in for the clavicle the
  rig does not have. Within 0.97 of the arm nothing moves and the positions are the drive's bit for bit.
- **The ease is a share of the full reach.** The full reach is solved every frame, and each joint takes the share of its
  correction the weight says, the shorter way. Weight 0 is the drive's pose exactly, so it neither starts nor ends with
  a jump. The ease is a smoothstep over 0.15 s at the quickest, slowed so that it turns no joint by more than half the
  frame's allowance. When the target goes, it eases out on the last full reach instead of dropping it.
- **The written joint is limited, not the correction.** Each written rotation turns at most 720 degrees a second and 30
  in a frame from the last frame's written rotation, so the flip limits stand.
- **The twist about each bone is the body's.** Each frame's reach is the drive's pose swung onto it (as main solved
  it); the last reach, swung onto this one, turns its twist towards that at 90 degrees a second at most. The bones point
  where the reach puts them either way; only the turn about each bone is followed at that rate. (The first version
  carried the last reach whole, which left the twist unanchored: a grip circling a 3 cm loop once a second at 0.8 of
  the arm took the upper arm, forearm and hand against the body's from 36.6/81.7/50.1 degrees to 172.6/122.2/168.3 in
  1000 loops while the hand stayed on the grip. Taken whole from the body each frame, a body arm jittering 20 degrees
  a frame twisted the drawn one by 12 degrees a frame.)
- **Out of reach has a margin.** A grip is taken within 1.15 arm lengths and let go beyond 1.18 once held.

### Numbers before and after

The same scenes on main's solver and on this one (`tests/offhand_grip_test.c`, the model arm, 30 frames a second unless
said):

| scene (from the logged numbers) | main | round 26 |
| --- | --- | --- |
| grip at 1.05 arm lengths | held, 0.0472 m off | held, 0.0000 m off, shoulder 0.047 m forward |
| grip at 1.149 arm lengths | held, 0.1056 m off | held, 0.0000 m off, shoulder 0.106 m forward |
| grip swaying 1.14 to 1.16 | 2 of 120 frames held, 67 out of reach, step 24.0 | 120 of 120 held, step 0.0 |
| ease in and out, 0.39 m | 5 frames at the allowance (24.0) | largest step 12.0, on the grip after 8 frames |
| ease in, target lost | 6 frames at the allowance | largest step 12.0 |
| body's arm jittering 20 degrees a frame (10 to 30 from its pose), 8.3 ms | 0 of 60 held, 0.091 m off, step 15.8 (allowed 6.0) | 60 of 60 held, 0.000 m off, step 0.9; back on a calm hold's arm 9 frames after it stops |
| 3 cm loop a second, 0.8 of the arm, 2000 loops | joints against the body's 36.57/82.55/50.29 throughout | the same, 36.57/82.55/50.29 throughout |
| 1 cm loop, 3000 loops; 1.05 of the arm, 1 and 3 cm | steady | steady: drift 0.000 degrees, the hand 0.0000 m off |

In the drive with the recorded body (`tests/arm_drive_test.c` `test_grip_reach_forward`, the grip 0.19 m further
forward along the gun, 1.04 arm lengths from the drawn shoulder): the hand 0.0000 m from the grip, the shoulder 0.043 m
forward, held steps under 1 degree. The moving-body test's largest step is now under the allowance at 17, 33 and 50 ms
frames (8.3, 13.3, 17.0 against 12.2, 23.8, 30.0); at 8 ms, with the body's hand leaving the grip for a frame every 37,
it still reaches it.

### The line, for the next game run

`Grip: ... weight 1.00; reach 1.05 of the arm (most 1.08), shoulder forward 0.047 m (most 0.065), held step 0.4 degrees,
the body's arm turned 2.0 degrees, 0 frames cut, not taken: 0 out of reach, 0 too far; as drawn the hand 0.001 m from
the gun's grip (most 0.002)`

- **reach / shoulder forward**: how far the grips are in arm lengths, and how far the shoulder came. Over 1.18 means a
  grip the arm lets go of; a shoulder often near 0.12 m wants a look at the sleeve.
- **held step**: the largest written turn in a frame held on the grip, the steady case. It should stay well under the
  allowance; if not, **the body's arm turned** says whether the body's own pose moved that much in a frame.
- **frames cut / not taken**: the limits holding the arm back, and refusals split into out of reach and too far from
  the drawn hand (feel.52 counted them together).
- **as drawn**: after the rig and the gun are evaluated for the draw, the rig's hand from the gun's grip node, both as
  the game evaluated them, in frames held at full weight (not while aiming fades in or out). The solver's "hand from the grip" is its own number. This one says whether the target was
  where the gun is drawn: a frame behind, another socket, or a scale on the gun would show here and nowhere else.

### What only the game can show

- Whether "as drawn" is under 0.01 m. The solver's numbers cannot see the evaluated gun.
- How a shoulder 5 to 12 cm forward looks: the sleeve at the shoulder and whether the upper arm crosses the view.
- Whether the held step stays small while turning, walking and sprinting.
- Whether the slower ease (about 0.27 s for a 0.39 m reach at 30 frames a second, against 0.15 s) reads as late
  after a reload.
