# What differs on build 25480438

Two kinds of entry: behaviour that has to differ from retail on this build (none yet), and build data established by hand because the retail kit's migration records do not carry it (`docs/BUILD-DATA-25480438.md` holds everything they do carry).

## Behaviour

None yet.

## Build data established by hand

| name | retail (26568081) | here (25480438) | evidence | status |
|---|---|---|---|---|
| inventory pool (backpack rows: map at +0x28, owners at +0x40, rows at +0x50, 48 bytes each) | `game+0x2ce18e8` | `game+0x3326738` | First Person Controller `src/native_view.c` `selected_weapon()` reads the same pool with the same field offsets; that code ran in the game on 2026-11-16 | used in game, layout of row byte 12 (backpack entity) not yet read here |
| PE identity of game.dll | stamp `0x6af322c1`, size `0x3382000` | stamp `0x6ab3b43f`, size `0x4744000` | header of `native-work/evidence/game-pre1020.dll` | confirmed |
| PE identity of helldivers2.exe | stamp `0x6af2d5e6`, size `0x29fe000` | stamp `0x6ab382e4`, size `0x39e8000` | donor `src/profile.h`, proved in the game by the 0.5.34 port | confirmed |

Every code pin in `src/profile.h` (14), `src/native_contract.h` (25) and `src/feature_contract.h` matches the two images byte for byte (`tools/verify_profile.py`, 2026-11-16). The probe reports the first two lists from the running game.
