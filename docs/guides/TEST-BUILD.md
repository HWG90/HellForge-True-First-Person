# True First Person for HellForge: test build 0.6.0-feel.18

For HellForge on game build 25480438. This is a test build: it works, it is not finished, and I want to
hear what breaks.

## Install

1. Close the game and the HellForge launcher.
2. Put the ZIP in HellForge's `mods` folder (or unpack it into `mods/true-first-person`).
3. Switch off any other first-person mod (First Person Controller, First Person + VR Mode, the rig trial).
   Two of them cannot run together.
4. Optional: DBF-MCM for HellForge gives the settings menu (DEL). Goose Overlay Host gives the weapon
   placement editor (Page Up). The mod runs without both.

## Use

- **F11** switches first person on and off. Use it in a mission, on foot, with a weapon in hand.
- The settings menu is "True First Person" in DBF-MCM.

## What was checked in the game (2026-11-20, solo missions)

- F11 on and off, also pressed four times within a second.
- Standing, walking, sprinting, strafing, crouching, diving: the view stays first person.
- Hip fire, holding aim, firing while aiming, reloading: the view stays first person.
- Weapon swap, grenade, stim: the view stays first person (see the limits below).
- The helmet, torso and shoulder pads are hidden from the camera; sleeves and hand grips are on.
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

## Test switch

A file `head_parts.txt` beside the DLL holding one digit chooses which armour parts are hidden from the
camera: 1 head, 2 torso, 4 shoulder pads, added together (7 is the default). Only for testing.
