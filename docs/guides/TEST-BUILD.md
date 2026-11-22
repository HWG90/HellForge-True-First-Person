# True First Person for HellForge: test build 0.6.0-feel.34

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
