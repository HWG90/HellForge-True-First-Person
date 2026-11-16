# The read-only probe (0.6.0-probe.1)

A small separate mod, `goose.true-first-person-probe`, that checks on the real game what the offline tests cannot: that the player, weapon and camera-request data are where the build profile says they are on build 25480438.

## What it does and does not do

Every game update it reads memory through HellForge's guarded reader and writes to its own log:

- once, as soon as a player exists: the identity of `game.dll` and `helldivers2.exe`, and one line per code pin (`Pin: ok name …`, `Pin: MISMATCH …`, `Pin: unreadable …`), then `Pins: N ok, N mismatched, N unreadable`;
- `Snapshot: …` whenever the player or weapon state changes (at most 10 lines a second; `held=N` counts what was left out);
- `Request: …` whenever the game's camera request queue changes;
- `Weapon: … key=…` when the held weapon changes (the key is what saved per-weapon placements are stored under).

It installs nothing in the game: no hooks, no table swaps, no game calls, no memory writes, no key handling, no menu. F11 is not touched. The DLL imports only the C runtime and KERNEL32 (the `VirtualProtect` name in it belongs to the compiler's own start-up code, as in every DLL built this way).

## Install (only on the user's word, game and HellForge closed)

Copy the folder `dist/true-first-person-probe` into the HellForge install's `mods` folder. To remove it, delete that folder. It can run next to the First Person Controller; it does not interact with it.

## The session

Any ordinary play will do, a few minutes: load a mission, draw a weapon, aim, swap weapons, use a stim, throw a grenade. With the First Person Controller on (F11), kind 6 rows appear too, which is useful.

## What a good log looks like

- `Image: game.dll ok … helldivers2.exe ok` and `Pins: 39 ok, 0 mismatched, 0 unreadable`.
- At the hip in third person: `Snapshot: kind=4 safe=1 active=0 mask=0 guard=0 aim=0 …` with non-zero `body`, `rig`, `weapon`, `unit`.
- During a weapon swap: `mask=2`, `safe=0`; during a stim: `mask=64`; aiming with the game's sights: `active=1 aim=1`.
- `weapon=` and `unit=` change on a swap; `Weapon: … key=` follows with a 16-digit key that is not all zeros.
- With the Controller's F11 on: `kind=6` while its arms are up.

Anything else (a `Snapshot: error=…` that stays during play, a mismatched pin, a `weapon=-` while a gun is in hand) points at a wrong offset, and its text says which read failed.

The log is `mod-goose.true-first-person-probe.log` in the session folder under the install's `logs/sessions/`.
