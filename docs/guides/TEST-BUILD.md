# True First Person for HellForge: test build 0.6.0-feel.43

For HellForge on game build 25480438. This is a test build: it works, it is not finished, and I want to
hear what breaks.

## Install

Take the **complete** ZIP unless you already run NEST for HellForge.

1. Close the game and the HellForge launcher.
2. Unpack the ZIP into HellForge's `mods` folder. It adds:
   - `true-first-person`: the mod.
   - `overlay-host`: NEST 0.8.3-tfp.1, the shared in-game UI the settings panel and the weapon placement
     editor draw through.
3. Switch off any other first-person mod (First Person Controller, First Person + VR Mode, the rig trial).
   Two of them cannot run together.
4. Start HellForge and check that both are switched on in the mod list.

DBF-MCM is no longer needed. The mod has its own panel.

## Use

- **F11** switches first person on and off. Use it in a mission, on foot, with a weapon in hand.
- **F10** opens the settings panel "True First Person" (also in NEST's list, Page Down). Three tabs:
  First person, Bob and sway, Locked view.
- **Page Up** opens the weapon placement editor.

## New: camera style (Stable or Bodycam)

In 0.6.0-feel.40. From playing 0.6.0-feel.39: the camera moved too much, and what was asked for was the bodycam
look as a choice beside a steadier, traditional first-person camera.

There is a new checkbox **Bodycam** on the Locked view tab, above the kick sliders. It changes at once, no restart.

- **Off (Stable), the default now.** A steady first-person camera. Walking, strafing, crouching, reloading, a
  grenade or a stim no longer turn the view at all. During a grenade, stim, melee or emote the camera follows your
  head slowly, so the head's bobbing mostly stays out of the view; a crouch or going prone is still followed within
  about a second. Being thrown or ragdolled still sways the view, at half the strength and slowly.
- **On (Bodycam).** The camera as it was in 0.6.0-feel.39: the view moves with your head, takes kicks from your
  body's movement and follows the head through actions.

"View kick strength", "View kick limit" and "Camera damping" belong to Bodycam; they are greyed while it is off and
keep their values. "View kick when thrown" works in both.

To get the old behaviour back: tick **Bodycam**. Your slider values are still there.

Tested in the game: ten actions in each style (Stable: no kick at all; Bodycam: 2.4 to 5.8 degrees); I have not played it yet.

## Fixed in 0.6.0-feel.43

- **A dive begun just as the weapon view comes back** jerked the camera 18 cm in one frame. The smoothing kept going
  at the speed of the camera it had just left and then lost that speed all at once. It lets it down gradually now:
  8 cm in that frame, which is what the arms themselves do there.

Tested in the game: all 35 scripted actions pass, and ten minutes of random overlapping actions in Stable had no
frame out of first person.

## Fixed in 0.6.0-feel.42

Two more faults of the camera smoothing, found by replaying my test runs frame by frame:

- **A dive out of a sprint** swung the camera up to 21 cm sideways for a moment: your body starts to roll, your
  eyes go straight on, and the smoothing took that for the eyes jumping. Now 4 cm.
- **The last bit of the arms settling after a swap** was sometimes shown in one go: 6 to 8 cm, now 3 to 5.

Tested in the game: all 35 scripted actions pass, and ten minutes of random overlapping actions in Bodycam had no
frame out of first person and one camera step over 6 cm (6.2 cm).

## Fixed in 0.6.0-feel.41

- **The camera catching up after a dive or a swap** settles sooner: when a movement of the head was dying down, the
  smoothing went on at its earlier speed for a moment and then had to come back. The longest catch-up after a change
  of camera in my test runs went from 0.8 s to 0.5 s.

What is still there, measured honestly: in twenty minutes of random overlapping actions on this build (ten in each
camera style, no frame out of first person) the smoothing itself made nine camera steps of 6 cm or more, the
largest 9 cm. The build before made twelve in ten minutes, up to 21 cm. An attempt to remove more of them made the
camera reverse in some cases, and I took it out again.

## Fixed in 0.6.0-feel.39

From two more soak runs (random actions back to back, 557 actions on 0.6.0-feel.38 and feel.39):

- **Two frames in third person** during a grenade thrown while walking. The game's camera had no parent but its
  own position was a frame or two behind, and the mod refused to place it. It is held now; no frame was lost in
  39,810 frames afterwards.
- **Swapping weapons while sprinting** made the camera stand still for a frame and then leap 24 cm. The game puts
  its first-person rig 1.2 m behind where the head camera is in a sprint, and only 1 m of that change was eased.
  All of it is now: 5 to 9 cm between two moments in four tries.

Still there: when the game itself throws your body about (a fall, a jump lunge), the view moves with it; those
are the game's own movements and are not smoothed.

## Fixed in 0.6.0-feel.38

Found by a soak test: 300 random actions back to back for twelve minutes, many of them overlapping the way a player
does (a grenade while sprinting, a swap during a dive).

- **A dive backwards right after a weapon swap** threw the camera 58 cm in one frame. The game moves your body a
  frame before the arms follow, and the camera smoothing took that for the eye jumping. Now 21 cm in that frame,
  at a dive that itself moves 19 cm a frame; the same sequence replayed in the game.
- **Camera drifting off after the arms flicker.** Right after first person came back, the arm rig sometimes flickered
  between two places for a few frames, and the smoothing then sent the camera off at several metres a second for a
  moment (13 cm steps with nothing behind them). Fixed, at any frame rate. Tested outside the game.
- **One frame in third person** during a crouch and walk, once in 21,512 frames. Not found again in the replay
  (0 in 21,784). The log now says which check failed if it happens.

In the replay the largest steps were that dive (21 cm) and four others between 6 and 9 cm.

## Fixed in 0.6.0-feel.37

Found by a review of the feel.36 changes before anyone played them:

- **Weapon placement rotation and the sprint pose with Animated arms.** The arm fix of feel.36 also cancelled the
  rotation the mod itself puts on the weapon: a placement's yaw, pitch or roll, and the sprint tilt, no longer turned
  the gun. They do again, and the arms still stay in place when looking up or down. Tested in the game.
- **Camera damping while turning.** Turning the view during a grenade, stim or swap could swing the eye sideways by
  up to 10 cm. Only the head's own movement is damped now. Tested in the game: 1.4 to 2.9 cm.
- **Camera damping when a dive or full aim begins** no longer lets go of its lag in one frame.
- **Vignette and depth of field** also stay off through a swap made during an action that delays the weapon change.
- **Field-of-view glide** is smooth at high frame rates (it moved in steps of the system clock).

## New and fixed in 0.6.0-feel.36

From playing 0.6.0-feel.34 by hand:

- **Arms when looking up or down.** The gun arm stretched away and the weapon ended up to half a metre from its
  place. It now stays where it is at a level view. Tested in the game: within 1 cm level, 4 cm at 35 degrees
  down, 8 cm at 70 degrees down.
- **Dark rim at every weapon swap.** The game's first-person vignette, and its depth of field, came back for a third
  of a second at each swap. Both stay off through a swap now and come back when first person is switched off.
  Tested in the game.
- **Camera damping (new slider, Locked view tab, default 60 %).** Steadies the view against your body's movement:
  the view kicks build up over time, and during a grenade, stim, melee or emote the eye follows the head more calmly.
  0 % is the old behaviour. Full aim is never damped.
- **Kicks from walking.** Your head turning sideways as the body twists (strafing, walking backwards) counts for a
  fifth of what it did. Kicks from walking, backing and strafing went from 5 to 7 degrees to 1.5 to 3.3.
- **Walking out of an emote** stepped the camera 10 to 12 cm; now 5 cm.
- **Field-of-view lock** eases out when switched off, as it eases in.

If the view still moves too much for you: lower "View kick strength" and "View kick limit" on the same tab, and
raise "Camera damping".

## Fixed in 0.6.0-feel.34

From a review of everything added since 0.6.0-feel.19 for crashes and thread faults (none found) and behaviour:

- **Field of view lock with Camera lock off.** It did nothing. The two are separate toggles now. Tested in the game.
- **Changing a key in the panel** no longer fires that key's action with the press that bound it. Tested in the game
  for the toggle key.
- **Kicks at high frame rates** were weaker and shorter the higher the frame rate. They are the same at any rate now.
  Tested outside the game.
- **Older NEST (0.4.0 and 0.5.0):** the settings panel now appears there. The placement editor still needs the NEST
  in the complete ZIP. Not tested on an older NEST.
- **The panel window keeps its place and size** between game starts.
- **The placement editor key row does not take F8**, which is HellForge's own editor key, and says so.

## Fixed in 0.6.0-feel.33

- **Looking down while swapping or closing the stratagem list.** Coming back to the weapon moved the camera 9 to
  13 cm over two moments when you were looking down. It is now eased: 3 to 7 cm looking down, 3 to 4 cm looking
  level. Tested in the game.

## Fixed in 0.6.0-feel.32

Found by a review of the code, not seen in play:

- **Arms after an action begun while aiming.** If the arms had to be set up again in the middle of a grenade or
  stim while you held aim, the weapon could stay displaced afterwards until you stood still. It is now lined up
  again when the action ends.
- **Settings copies.** A `settings.json` that could not be written got one more identical copy at every start
  until the nine names were used up. An identical copy is now reused.
- **A `locked_view.json` from a later build** is copied to `locked_view.json.newer` before this build writes it,
  so options this build does not know are not lost.

## Fixed in 0.6.0-feel.26

- **Stim: your own helmet from behind.** In 0.6.0-feel.25 every stim showed the back of your helmet for
  about half a second, the rifle on your back in front of it. The game keeps its own first-person camera
  for most of a stim and moves it into the head once the weapon is away. The view now goes to the eyes
  instead, as it does for a grenade, and your head, torso and shoulders stay hidden for the whole stim.
  Built and tested outside the game; not yet seen in play. The Locked view toggle must be on; with Smooth
  eye off the view goes to the eyes in one step instead of a glide.

## What is new since 0.6.0-feel.19

- **Locked view.** Once on, the view stays first person through every action: swaps, grenades, stims,
  emotes, vaults, dives, going prone. Nothing drops to third person any more.
- **Animated arms.** The first-person arms follow your character's own animation: sprint, reload, ammo
  check, aiming, melee. A pistol is held in one hand at the hip and in two when you aim.
- **Weapon put-away.** On a swap, grenade or stim you see the arms put the weapon away before the view
  goes to the hands.
- **Smooth eye.** The camera no longer pops when the game changes its own camera. Returning to the weapon
  after a swap moved the camera 14 to 17 cm in one go before; it is now eased, with 3 to 4 cm at most
  between two moments.
- **Field of view lock.** One field of view (80 degrees by default, 50 to 120 in the panel) whatever the
  weapon or action. "Zoom when aiming" keeps the weapon's own zoom in full aim.
- **Kicks.** The view tilts a little with what your body does: a dive, a landing, being thrown. Mouse look
  and recoil do not cause it. Strength and limit are in the panel.
- **Own settings panel.** F10. Animated arms, Weapon put-away and Smooth eye are options on the Locked
  view tab and switch while you play.
- **Dying.** The view is meant to stay at the head as the body falls. See the limits.

## Fixed since 0.6.0-feel.25

- **Stim:** the view no longer sits behind your own helmet for half a second.
- **Resting weapon:** after swapping back to a weapon it rests where your placement puts it, not in the middle
  between both forearms.
- **Aiming:** holding aim brings the rifle up to the shoulder again (with animated arms it sank out of view).

## What was checked in the game (2026-11-22 and 23, solo missions)

A scripted run of 32 actions, each judged by two rules: no frame left first person, and the camera never
jumped more than 5 cm.

- **Pass:** standing, walking, backing, strafing, sprinting, looking around, crouching, going prone, diving
  forward and back, standing up, firing, aiming, firing and walking while aiming, reloading, ammo check,
  swapping to secondary, support and primary, quick swaps, pistol fire, aim and reload, grenade, melee,
  emote, F11 off and on.
- **Just over the limit, by 1 cm or less:** stim (6 cm), vault (5.5 cm).
- **Arms:** recorded moving with the body's arms in idle, walk, sprint, fire and aim; seen in captures on
  the rifle in sprint, reload and ammo check, and on the pistol.

- **Stratagems (checked 2026-11-22):** opening and closing the list, entering a code and throwing an SOS Beacon
  all stayed first person, 3.7 cm at most between two moments.

## Limits you will see

- **Stratagem list and beacon in hand.** The view is from your character's head with nothing in your hands in
  view; you do not see the beacon before you throw it.

- **Grenade and stim with empty hands.** After the put-away the view is from your character's head and
  the arms are the character's own, so you see them when they pass in front of the eyes.
- **Dying.** The view falls with the body and stays at the head until the game sends the replacement. Seen
  four times, produced on purpose: standing, aiming, sprinting and in the middle of a swap. On the ground you
  see whatever lies in front of the face, which can be your own dropped rifle or arm. A death by explosion has
  not been seen.
- **Holding aim raises the weapon to the shoulder.** Looking through the sight is a separate option
  ("true native ADS") and was not part of these checks.
- **The settings panel:** all three tabs were opened in the game, and Camera lock, Animated arms, Weapon
  put-away and Smooth eye were switched off and on there and seen to take effect at once. The field-of-view and kick
  sliders were set there too. The toggle key was changed to F9
  and back there.
- **The crash while the game closes is guarded** (an engine fault of build 25480438, with or without first
  person). Tell me if you see a crash on quit.

## Settings files (from 0.6.0-feel.25)

Your settings are in `%LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson\`.

- **Two files.** `settings.json` holds the weapon placements and the menu values. `locked_view.json` holds
  the options of the Locked view tab.
- **Settings written by 0.6.0-feel.20 to feel.24 are converted on the first start.** Those builds kept the
  Locked view options inside `settings.json`, and 0.6.0-feel.19 and older then refused the whole file and
  wrote their defaults over it, placements included. The first start of this build moves the options to
  `locked_view.json` and writes `settings.json` without them. A copy of the file as it was is kept as
  `settings.json.before-feel25`. After that 0.6.0-feel.19 and older read `settings.json` again.
- **Going back to 0.6.0-feel.20 to feel.24** is safe for the placements. Those builds do not read
  `locked_view.json`, so their Locked view options start at the defaults, and their next save puts the
  options back into `settings.json`; this build converts it again on its next start.
- **A file this build cannot read is never written over without a copy.** Before the first save it is
  copied to `settings.json.refused` (or `.refused-2` and so on; `locked_view.json.refused` for the other
  file). A file that is there but would not open at the start is copied to `settings.json.unread` in the
  same way. The log says `Kept: ...` when either happens.
- **A `locked_view.json` from a later build is kept too.** When it names options this build does not know,
  the log says `Note: ... skipped: ...`, and before this build first saves the file it is copied to
  `locked_view.json.newer`.
- **One copy per file.** When a copy with the same contents is already there it is used, and no further
  numbered copy is made. A save that keeps failing leaves one copy, not one per start.

## Not checked yet

Vehicles and emplacements, multiplayer, stratagems other than the SOS Beacon (Resupply, support weapons,
sentries), picking up items, terminals,
climbing ladders, the placement editor, weapons other than a rifle, a support weapon and a pistol.

## Reporting a problem

Send the file `logs/sessions/<newest folder>/mod-goose.true-first-person.log` from the HellForge folder,
and say what you did in the seconds before it went wrong. A clip helps most.

## Switches for testing

Empty files beside the DLL, read when the game starts:

| File | Effect |
|---|---|
| `lock_off.txt` | the locked view, kicks, smooth eye and field-of-view lock off |
| `arms_off.txt` / `arms_live.txt` | animated arms off / on, whatever the panel says |
| `hold_off.txt` / `hold_lease.txt` | weapon put-away off / on, whatever the panel says |
| `smooth_off.txt` | smooth eye off |
| `trace_off.txt` | no per-frame `Trace:` lines in the log |
| `mcm_on.txt` | the settings go to DBF-MCM instead of the mod's own panel |
| `head_parts.txt` | one digit: which armour parts are hidden from the camera (1 head, 2 torso, 4 shoulders, added) |
