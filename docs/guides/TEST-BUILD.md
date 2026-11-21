# True First Person for HellForge: test build 0.6.0-feel.19

For HellForge on game build 25480438. This is a test build: it works, it is not finished, and I want to
hear what breaks.

## Install

Take the **complete** ZIP unless you already run DBF-MCM and NEST for HellForge.

1. Close the game and the HellForge launcher.
2. Unpack `True-First-Person-0.6.0-feel.19-HellForge-25480438-complete.zip` into HellForge's `mods` folder.
   It adds three folders:
   - `true-first-person`: the mod.
   - `mcm`: DBF-MCM 0.1.74-hellforge.3, the settings menu (DEL).
   - `overlay-host`: NEST 0.8.3-tfp.1, the shared in-game UI the menu and the weapon placement editor
     (Page Up) draw through.
3. Switch off any other first-person mod (First Person Controller, First Person + VR Mode, the rig trial).
   Two of them cannot run together.
4. Start HellForge and check that all three are switched on in the mod list.

If you already have `mcm` or `overlay-host`, keep yours when they are newer and take only
`true-first-person` (or the smaller `-test.zip`, which holds just the mod). The mod itself starts without
the other two; you then have F11 and the default settings, but no menu and no editor.

## Use

- **F11** switches first person on and off. Use it in a mission, on foot, with a weapon in hand.
- The settings menu is "True First Person" in DBF-MCM.

## Changed since 0.6.0-feel.18

- **Sleeves and hand grips are off.** They attach armour pieces to the first-person arms and correct the
  hand's grip every frame. They had never run on this game build before feel.18, and a tester reported the
  hands doing odd things. In my own captures the hands looked the same with them on and off, so I cannot
  say this fixes that report; it removes the part I trust least. A file `arms_on.txt` beside the DLL
  switches them back on.

## What was checked in the game (2026-11-20, solo missions)

- F11 on and off, also pressed four times within a second.
- Standing, walking, sprinting, strafing, crouching, diving: the view stays first person.
- Hip fire, holding aim, firing while aiming, reloading: the view stays first person.
- Weapon swap, grenade, stim: the view stays first person (see the limits below).
- The helmet, torso and shoulder pads are hidden from the camera.
- Most of this list was run on feel.18. On feel.19 I ran two missions (F11, hip, looking down, walking,
  aiming, rifle and pistol, leaving and quitting), once with sleeves and grips off and once with them on.
- Leaving a mission with the view on, and quitting the game afterwards.

## Limits you will see

- **Swap, grenade and stim move the eye.** For these three the game drops its first-person camera for
  0.3 to 1.5 seconds. The mod keeps you in first person from the character's head during that time, then
  returns to the weapon view. The return can shift the camera by up to 40 cm.
- **During those actions you see the character's own arms**, so they are only on screen when they are in
  front of the eyes. Looking straight ahead you may see just the pistol, the grenade or the stim.
- **Reloads have no hand animation in the weapon view.** The game's first-person arms are a fixed pose
  per weapon plus recoil; the game has no first-person reload animation to show.
- **Holding aim raises the weapon to the shoulder.** Looking through the sight is a separate option
  ("true native ADS" in the menu) and was not part of this check.
- **Dying leaves first person.** The game's death camera takes over.
- **The crash while the game closes is guarded.** Build 25480438 has a fault in its own shutdown (it
  releases some of its tables through the wrong allocator) that crashed about half of my sessions, with or
  without first person in use. This build skips that faulty release. In 9 sessions since, none produced the
  crash report; one still ended with an error code and no report. Tell me if you see a crash on quit.

## Not checked yet

Vehicles and emplacements, multiplayer, stratagem throws and call-ins, picking up items, the placement
editor, changing settings in the menu while playing, weapons other than a primary rifle, a support weapon
and a pistol.

## Reporting a problem

Send the file `logs/sessions/<newest folder>/mod-goose.true-first-person.log` from the HellForge folder,
and say what you did in the seconds before it went wrong. A clip helps most.

## Test switches

A file `head_parts.txt` beside the DLL holding one digit chooses which armour parts are hidden from the
camera: 1 head, 2 torso, 4 shoulder pads, added together (7 is the default). Only for testing.

An empty file `arms_on.txt` beside the DLL switches sleeves and hand grips on. Only for testing.
