# True First Person for HellForge: test build 0.6.0-feel.59

For HellForge 0.4 and 0.5 on game build 25480438. This is a test build: it works, it is not finished, and I want to
hear what breaks.

## Install

1. Close the game and the HellForge launcher.
2. Extract the ZIP into HellForge's `mods` folder. Don't put the ZIP itself in `mods`: it holds three mods, and
   HellForge 0.4 takes a ZIP there as one mod (it says "no hellforge-mod.json ... in its one top-level folder"). It adds three folders:
   - `true-first-person`: the mod.
   - `mcm`: DBF-MCM 0.1.74-hellforge.4, the settings menu. With it, every option is on the mod's
     "True First Person" page.
   - `overlay-host`: NEST 0.8.3-tfp.1, the shared in-game UI the menu, the settings panel and the weapon
     placement editor draw through.
3. Switch off any other first-person mod (First Person Controller, First Person + VR Mode, the rig trial).
   Two of them cannot run together.
4. Start HellForge and check that all three are switched on in the mod list.

If you already have `mcm` or `overlay-host`, keep yours when they are newer and take only
`true-first-person` from the ZIP. Without DBF-MCM the mod opens its
own panel with the same pages instead.

## Use

- **U** switches first person on and off (F11 until now; F11 is HellForge's Armor LUT editor). Use it in a mission,
  on foot, with a weapon in hand. Another key can be set in the settings: "Toggle True First Person".
- **F10** opens the settings: the "True First Person" page in DBF-MCM, or without DBF-MCM the mod's own
  panel (also in NEST's list, Page Down). Three pages: First person, Bob and sway, Locked view.
- **Page Up** opens the weapon placement editor.

## New: camera style (Stable or Bodycam)

In 0.6.0-feel.40. From playing 0.6.0-feel.39: the camera moved too much, and what was asked for was the bodycam
look as a choice beside a steadier, traditional first-person camera.

**Camera style** on the Locked view page picks it: click `< Stable >` to switch (since 0.6.0-feel.52 a one-click
selector; before, a Bodycam checkbox). It changes at once, no restart.

- **Stable, the default.** A steady first-person camera. Walking, strafing, crouching, reloading, a
  grenade or a stim no longer turn the view at all. During a grenade, stim, melee or emote the camera follows your
  head slowly, so the head's bobbing mostly stays out of the view; a crouch or going prone is still followed within
  about a second. Being thrown or ragdolled still sways the view, at half the strength and slowly.
- **Bodycam.** The camera as it was in 0.6.0-feel.39: the view moves with your head, takes kicks from your
  body's movement and follows the head through actions.

"View kick strength", "View kick limit" and "Camera damping" belong to Bodycam; they are greyed in Stable and
keep their values. "View kick when thrown" works in both.

To get the old behaviour back: choose **Bodycam**. Your slider values are still there.

Tested in the game: ten actions in each style (Stable: no kick at all; Bodycam: 2.4 to 5.8 degrees); I have not played it yet.

## Changed and fixed in 0.6.0-feel.59

Everything since 0.6.0-feel.56. This is a test build: it has passed my tests, but the items marked *not played yet*
have not been played in the game, and the grip and reload changes have only run in my automated game tests.

- **Exosuit: first person stays on.** *Not played yet.*
  - The camera sits at your helmet and turns with the mech.
  - Your diver is hidden while you are seated.
  - Getting in and out glides.
  - New option **See through the exosuit cockpit** (on by default) hides the mech's body from your own view. The
    console and sticks go with it, because they are one model with the hull. Other players still see your mech.
- **Reloads at the hip:** the view no longer slides back and forward, so the gun no longer looks like it jumps forward
  and back. A few frames at the very start and end of a reload can still slide a little; a fix is coming.
- **Support hand:** it now takes a fitted foregrip much more readily (0.6.0-feel.56 almost never took the
  Liberator's).
- **Pressing F11** now shows a one-time note that first person is on U (F11 is HellForge's Armor LUT editor).
  *Not played yet.*

**Known issues in this build:**
- On HellForge 0.5 the support hand may still be drawn in the game's own pose, even though the mod places it on the
  grip. A fix is in progress.
- In an exosuit, belt gear such as the sample holder can still show. A fix is in progress.
- At a grenade or item throw the camera can fall behind your head for a moment, so your back and cape show. A fix is
  in progress.
- Small camera jumps in dives (about 7 cm).
- Scope of Authority 2.27.13 can crash HellForge 0.5 at the title screen. That is not this mod.

## Changed and fixed in 0.6.0-feel.56

Everything since 0.6.0-feel.52. This is a test build: I have played 0.6.0-feel.55 on HellForge 0.5; this build as a
whole, and the items marked *not played yet*, have passed my tests but have not been played in the game.

- **HellForge 0.5.** HellForge 0.5 moved some of the game's records the mod reads; it now finds them where the game's
  own code puts them, on 0.4 and 0.5. (0.6.0-feel.55; played on 0.5.)
- **U switches first person on and off, not F11.** HellForge 0.5's Armor LUT editor opens on F11. If you never changed
  the key, it is U now; nothing to do. A key you picked yourself is kept, and F11 can no longer be picked.
- **U does nothing while you type** in chat, the settings menu, the panel or the placement editor. *Not played yet.*
- **Your support hand on foregrips.** *Not played yet.*
  - The left hand goes on a fitted vertical or fore grip and closes round it.
  - It stays there while you aim and while you walk.
  - While aiming, the hand is now placed on every two-handed gun, so aiming looks a little different. Tell me how it
    feels.
- **Performance.** The mod's time per frame is 13 to 19 % shorter (0.6.0-feel.54). A test switch, a file
  `memo_on.txt` beside the DLL, turns on a further saving; it is off by default.

**Known issues in this build:**
- In an exosuit the first-person view was still wrong (fixed in 0.6.0-feel.59).
- Scope of Authority 2.27.13 can crash HellForge 0.5 at the title screen. That is not this mod.
- From 0.6.0-feel.52, still open: a swap in the middle of a dive can push the view forward for a frame or two; in
  Stable, after a swap on a slope the view can trail for half a second; backpack pick-ups can drift the view briefly;
  the Warp Pack's glow shows unless you try `warp_hide.txt`; in DBF-MCM, Enter does not change a `< >` row.

## Fixed in 0.6.0-feel.52

- **Your support hand holds the weapon.** On rifles the left hand now reaches the fore-end or vertical grip
  instead of resting against the receiver. Pistols at the hip stay one-handed. "First Person Arms" in the
  menu switches it; a file `grip_off.txt` beside the DLL brings the old hand back to compare.
- **Clambering no longer floats the view.** On a high clamber the view rose half a metre above your head and
  sank back; it now stays with you. In Stable the head's own bob in a climb now shows instead of being
  smoothed.
- **No snap about a second after an emote, the stratagem menu, a grenade or a stim.** The view used to jump
  about 15 cm in one frame when it let go of the held weapon view; it now glides back.
- **Sprinting or diving straight out of an emote** no longer throws the view for a frame.
- **Aiming:**
  - New option "Aim field of view": the game's zoom (as before), your locked view, or a value you choose.
  - The placement editor now works while you aim, on the aim position.
  - "Sight distance in aim" can bring the sight closer to your eye. It is off (0) by default.
- **Settings menu:** Camera style and Motion preset are one-click `< Stable >` selectors instead of
  drop-downs.
- **Placement editor:** right-drag turns the weapon, middle-drag rolls it; the mouse wheel is depth (Page Up
  and Page Down no longer change depth).
- **Checked in the game (2026-11-24, solo missions):** a 13-minute run of random actions in each camera style
  and the 37-phase check (34 passed). No crash. Bodycam's largest jump fell from 0.46 m to 0.16 m.

**Known issues in this build:**
- A weapon swap in the middle of a dive can push the view up to half a metre forward for a frame or two.
- The support hand can sit a few centimetres off the grip, and may twitch at reloads and swaps.
- In Stable, after a swap on a slope the view can trail about 0.4 m for half a second.
- Not yet tried in the game: the Aim field of view option and the editor while aiming.
- In DBF-MCM, Enter does not change a `< >` row; click it, or use Left and Right. If the menu's list runs down
  by itself, tell me what you had plugged in (controller, Steam Input).
- The Warp Pack's glow still shows in first person. A fix is in this build but off: put a file named
  `warp_hide.txt` beside the DLL to try it, and tell me what you see.
- Backpack pick-ups can still drift the view briefly.

## Fixed in 0.6.0-feel.51

- **Your own helmet no longer fills the view** for a moment at a weapon swap or a grenade throw. When the
  view hands over to the head camera, the game draws your body again for a frame or two before the mod
  hides it; the view now steps in front of the head for those frames.
- **Aiming down sights is steady.** The zoom no longer breathes in and out while you aim. The game sets the
  field of view twice a frame and the two were pulling against each other.
- **The settings menu shows everything.** With DBF-MCM installed, every option is on its "True First Person"
  page: First person, Bob and sway, and Locked view (camera style, field of view and zoom when aiming, view
  kick, camera damping, animated arms, weapon put-away, smooth eye). F10 opens it. Without DBF-MCM, F10 opens
  the mod's own panel with the same pages. Camera style is now a choice of Stable or Bodycam, and the
  Bodycam-only sliders are greyed out in Stable. "Fird Person Arms" is now spelled right.
- **The placement editor rotates.** Right-drag turns the weapon (yaw and pitch), middle-drag rolls it, live,
  and it is saved with the placement. While the editor is open, bob and sway pause.
- **Page Up and Page Down no longer change depth in the placement editor.** Use the mouse wheel. Page Up
  still opens and closes the editor.
- If you used my older experimental "True First Person" page in MCM: that came from a separate test mod, not
  this one. Remove it; this build has its own page.
- **Checked in the game (2026-11-24, solo missions):** the same runs of random actions as for feel.50, in
  both camera styles, and the 35-phase check. No crash. The helmet was moved clear at all 66 swaps and throws
  in Stable; in Bodycam 3 frames in a whole session still caught it.

**Known issues in this build:**
- Sprinting or diving straight out of an emote can jump the view 0.2 to 0.6 m for a frame. feel.50 has this
  too.
- About a second after the hands go empty at a swap, grenade or stratagem, the view can step about 0.15 m as
  it settles back.
- At a swap, throw or stratagem the view moves forward for one or two frames (up to about 0.3 m) instead of
  showing the inside of the helmet. I have not measured how visible that is yet; tell me if you notice it.

## Fixed in 0.6.0-feel.50

- **The view no longer lurches when the arms move.** The game places the first-person view by where the rig
  holds the gun, and the mod's animated arms were moving that point. So a melee threw the view 0.7 m in one
  frame, and dives, aiming, reloads and the arms settling after a swap all slid it forward and back. The mod
  now gives the game its own values back after drawing the arms. This was in every build with Animated arms.
- **Checked in the game (2026-11-24, solo missions):** the same runs of random actions before and after. In
  the Stable style 17 in 100 actions had a jump of the view over my limit (0.06 m) before and 10 in 100 after;
  in Bodycam 23 before and 12 after. Aiming, pistol aiming, melee and prone with a stim left the list. No
  crash.
- The gun can now sit a little nearer or farther than before in the middle of an action, because the view no
  longer moves to hide it. At rest it is where it was.
- (Fixed in feel.51:) for a moment after a grenade throw begins, and sometimes at a weapon swap, the back of
  your own helmet filled the view.
- **Not fixed:** a backward dive still throws the view (up to 0.6 m in one frame); smaller jumps remain in
  dives, vaults, going prone and reloads (0.06 to 0.14 m).
- For testing: `arms_live_keep.txt` beside the DLL brings the old behaviour back.

## Fixed in 0.6.0-feel.48

- **The view no longer shakes back and forth while standing.** After a swap, a sprint stop or an action the
  view could shake about 0.2 m forward and back on every frame, for up to three seconds. The game's own
  unzoom gate flips on alternate frames and moves the first-person rig with it. The mod now holds the rig at
  its settled distance. This was in every build back to feel.37.
- If an action landed in the middle of such a shake the view was thrown up to 0.4 m. That is gone too
  (feel.47).
- **Checked in the game (2026-11-24, solo mission):** the same 20-minute run of random actions on feel.47 and
  feel.48. feel.47 had three long shakes; feel.48 had none. No crash.
- **Not fixed:** single small jumps of the view remain in a jump or vault (up to 0.14 m; 0.17 m in Bodycam),
  a dive (0.06 to 0.10 m; a backward dive in Bodycam 0.29 m), a weapon swap while sprinting (0.11 m), going
  prone and using a stim (0.16 m), and just over the limit I test against (0.06 m) in quick swaps and the
  stratagem menu. They are next.
- For testing: a file named `depth_steady_off.txt` beside the DLL switches the new hold off.

## Fixed in 0.6.0-feel.46

- When the view was catching up after an action and the body then slowed, the view could carry on at the
  old speed for a moment. It is now let down gently.

## Fixed in 0.6.0-feel.45

Found in the logs of long scripted runs (random actions back to back), not by playing:

- **The view slid past a dead stop.** When the eye stopped at once, the smoothing sometimes took the stop for a
  jump and let the view glide up to 12 cm past where it should rest, then pulled it back. A stop is shown as a
  stop now.
- **A missed frame while moving** (the eye stands for one frame and goes on) could start a catch-up of 20 to
  60 cm lasting half a second. It is followed now. This was found by a check, and seen once in the logs in a
  milder form.
- **Swapping to a weapon whose eye is close to the old one** (up to 26 cm apart): the eye's glide to the new
  weapon now starts from rest instead of at full speed. Swaps between weapons further apart glide as before:
  a gentle glide there made single frames worse in tests, so it is not in.

Tested in the game: the scripted run of 35 actions passes, and ten minutes of random actions in each camera style
(264 and 266 actions) left no frame out of first person and did not crash. I have not played it yet.

What you can still see: in those runs 15 of the 530 actions had one camera step of 6 to 18 cm, most of them a
dive or a jump begun while walking, or a movement begun within a second of a swap. After a sprint with a swap
the view can sit 7 to 47 cm off the game's eye for about a second while it catches up evenly.

## Fixed in 0.6.0-feel.44

Found by a review of the camera style, before anyone played it:

- **Switching between Stable and Bodycam in the middle of a movement** could pull the eye up to 15 cm, or snap the
  view's tilt, in one frame. A switch is eased now. Tested in the game, both ways, while crouched with the stratagem
  list open: 2 to 2.5 cm.
- **The greyed sliders** (kick strength, kick limit, camera damping while Bodycam is off) show their descriptions
  again when you point at them. Seen in the game.

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
  emote, first person off and on.
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
| `depth_off.txt` | the mod's zoom depth is forced to nothing on every frame: neither the rig nor its root is shifted for the optic (to find out whether that shift is part of the unzoom gate's flipping, `docs/design/specs/2026-11-23-gate-flip.md`) |
| `mcm_on.txt` | the settings go to DBF-MCM instead of the mod's own panel |
| `head_parts.txt` | one digit: which armour parts are hidden from the camera (1 head, 2 torso, 4 shoulders, added) |
