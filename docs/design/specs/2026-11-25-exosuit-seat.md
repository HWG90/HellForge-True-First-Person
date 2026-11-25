# Round 38: first person in an exosuit, and seeing through the cockpit

## What the user saw

Inside an EXO suit's cockpit (0.6.0-feel.55, `~/.cache/tfp-round37/user-mech.webp`) the "arms went nuts". The user
wants first person kept in the suit (round 37's hand-back to the game's camera was reverted), and then asked whether
the cockpit could be made transparent from the inside.

## Cause (round 37b, from the log and the game data)

- The first-person arm rig was not on screen at all: the arm drive only runs in camera kind 6 (`Arms: +0 frames
  driven ... +125 frames in another camera`), and the game parks and hides the rig outside kind 6 (`rig_eye` stays
  where the diver boarded while the suit walks away).
- The stretched "sleeve" was the diver's own right arm. Show torso was on, so the armour and its sleeves were drawn,
  seen end-on from a lens 0.18 m in front of the head node, beside the shoulder.
- The lock held the seat as if the body were thrown: the eye on the bobbing head node, its forward step along the
  game camera's direction (looking sideways swung the eye round the head), the seated view eased over 180 ms, the
  thrown kick on top.
- The cockpit is closed: no window, no glass material, no camera node in `combat_walker.unit`.

## What round 38 does

1. **The seat rule** (round 37's, `src/camera_vehicle.h`): seated (`flags[14] & 4`), off foot (layer 14 not 36)
   and not in physics. A valve (seated, on foot) and a thrown or dead body keep their old handling.
2. **The eye** (`src/camera_helper.c` `seat_present`): the head's offset in the body root's frame, frozen once it
   has been steady within 2 cm for 300 ms, plus 0.18 m along the root's level forward and 0.04 m up. Never along the
   game camera. **The direction**: the game's own on the same frame, no ease, no kick, no veil.
3. **Entry and exit** glide between eyes (smoothstep, 300 to 600 ms, a cut beyond 8 m); the continuity is let go
   at both ends. The exit glide runs in the head camera and in the game's native first-person view alike
   (`seat_native`: the rig eye plus the frame's parent-local argument, the glided point written back as the
   argument; only the position glides there, the direction is the native view's own). Native frames begin it again
   from the view last shown; any frame the lock does not show ends it, so it never resumes from the cockpit. The
   independent check of 2026-11-25 had found a 2.52 m cut leaving straight into the native view, and a glide
   interrupted by native frames stepping 2.46 m to the head and then 2.21 m back toward the cockpit
   (`~/.cache/tfp-verify50-mech/probe`); `tests/camera_seat_test.cpp` cases 6 to 8 hold every step under 0.15 m.
   Measured with the probe's own sequences, drawing native frames at rig eye plus argument: 0.000 m and 0.052 m.
4. **The whole diver hidden** (`src/head_visibility.c`, `TfpHeadIo.seat`): all 30 appearance slots, the body's own
   meshes and the backpack lose camera and shadow passes from the first seat frame to the end of the glide out,
   whatever the camera kind of its frames (a kind 6 frame of the glide drew the diver again before the check). The
   first-person rig is left to the game. Leaving gives back exactly what was there (head and shoulders hidden, the
   torso as the menu says).
5. **See through the exosuit cockpit** (option, default on; `TfpHeadIo.cockpit`, `src/cockpit_find.h`).

## Seeing through the cockpit: what is possible

Every EXO model is one unit, `content/fac_helldivers/vehicles/combat_walker/combat_walker` (resource hash
`e3455edad2b3e479` at unit +0x358; checked the same way the first-person rig's `fp_cha_avatar` hash is). Its weapons
are units of their own (`combat_walker_autocannon_*`, `_gatling`, `_missile_launcher`, ...).

The suit's whole body is **one engine mesh**, `g_combat_walker` (with `_LOD1` to `_LOD3` and a shadow mesh). The
parts `walker_l_front_whole`, `walker_r_front_whole` (the cockpit halves, console and sticks included),
`walker_back_*`, `walker_pelvis_*`, `walker_*_leg_*` and `stratagem_launcher_*` are **visibility masks**: UDIM
regions of that mesh that the game shows and hides in the shader (whole / damaged), not meshes
`Unit.set_mesh_visibility` can reach (filediver `extractor/geometry` and `datalibrary/visibility_mask.go`). Rendered
from the seat eye, the two front halves fill 100 % of the view; without them 99 % is open.

So the only safe, engine-supported way is to hide the camera pass of the suit's body mesh and its levels of detail,
for the local camera only, while seated in first person. The console and sticks go with it; the suit's shadow (its
own mesh) and its weapons stay. Writing the game's visibility-mask state was not attempted: it lives in game.dll's
entity components, is rewritten by the game on damage, and nothing about it is proven on build 25480438.

**Which suit**: found by guarded reads only (`src/cockpit_find.h`): alive in the unit registry, the suit's
resource, the body's world, and either the body's link (+0x1D0) names it or its `attach_driver` node is within 1 m
of the body's root; two such suits refuse both. Looked for with the engine's unit inventory at most every 60 frames
while none is known, before head visibility's update, never inside it. Another player's suit cannot pass.

**Restored** on leaving the seat, the option off, death (the seat rule fails), a stop (the mod switched off) and
the suit destroyed or changed (its rows are retired with no write), by head visibility's own restore.

## Log lines

- `Seat: in an exosuit: first person kept; ...` on entry; `Seat: left the exosuit: ...` on leaving.
- `Seat: cockpit: seen through: the suit 0x..., found by ...` / `no suit proven ...` / `... drawn again`.
- Once a second while seated (`src/seat_log.h`): frames, the eye in the root frame, the anchor and glide, both
  hands from the eye, diver rows hidden, the cockpit, and the arm drive's frames (none driven in the suit).

## Only the game can show

- That the diver is linked to the suit, or that `attach_driver` sits within 1 m of the body's root (the Seat:
  cockpit line says which way it was found, and the distance).
- That hiding `g_combat_walker` from inside looks right, and that the game never rewrites that mesh's pass while
  seated (an outside write is left alone by retail's foreign-mask rule).
- What the hands do in the cockpit (the Seat line's hand positions), and the feel of entry, exit and turning.
- Whether the game ever leaves the seat straight into its native first-person view, or flips between that view and
  the head camera during the exit (the fake memory proves the glide through both; the game decides which happens).
- The native-view exit's direction: only the position glides there; a direction step at the change, if the suit
  aimed far from where the diver looks once out, would be the game's own.

## Other vehicles

The seat rule is any seat: seated, off foot, not in physics. An FRV's seat (driver or passenger) therefore keeps
first person the same way: the eye on the head's anchor in the body root's frame, the game's direction, the diver
hidden; nothing is seen through, because the suit search only takes a unit with the exosuit's resource
(`camera_seat_test` case 9 drives an FRV seat at 20 m/s and turning: the eye stays within 1 mm of the anchor).
Only the game can show whether an FRV seat really reads as seated and off foot (the Seat line says so on entry),
whether the root frame is steady enough on rough ground, and whether the hidden diver hides anything the player
wants to see in an open seat (the gunner's hands on the turret).
