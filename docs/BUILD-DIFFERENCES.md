# What differs on build 25480438

Two kinds of entry: behaviour that has to differ from retail on this build (none yet), and build data established by hand because the retail kit's migration records do not carry it (`docs/BUILD-DATA-25480438.md` holds everything they do carry).

## Behaviour

None yet in the translated units. The view build (0.6.0-view.1) differs from retail in its plumbing only, each listed with its reason in `docs/WIRING-VIEW.md` section 6: the frame clock is the performance counter, the mouse buttons come from Windows, a raised frame is logged instead of switching the mode off, code pins are read as they were before any HellForge hook, and the pose helper is a stand-in.

## Build data established by hand

| name | retail (26568081) | here (25480438) | evidence | status |
|---|---|---|---|---|
| inventory pool (backpack rows: map at +0x28, owners at +0x40, rows at +0x50, 48 bytes each) | `game+0x2ce18e8` | `game+0x3326738` | First Person Controller `src/native_view.c` `selected_weapon()` reads the same pool with the same field offsets; that code ran in the game on 2026-11-16 | used in game, layout of row byte 12 (backpack entity) not yet read here |
| PE identity of game.dll | stamp `0x6af322c1`, size `0x3382000` | stamp `0x6ab3b43f`, size `0x4744000` | header of `native-work/evidence/game-pre1020.dll` | confirmed |
| PE identity of helldivers2.exe | stamp `0x6af2d5e6`, size `0x29fe000` | stamp `0x6ab382e4`, size `0x39e8000` | donor `src/profile.h`, proved in the game by the 0.5.34 port | confirmed |

Every code pin in `src/profile.h` (14), `src/native_contract.h` (25) and `src/feature_contract.h` matches the two images byte for byte (`tools/verify_profile.py`, 2026-11-16). The probe reports the first two lists from the running game.

## Look gain in the feel build (pose helper bound for real, `src/wire_pose.c`)

**Behaviour on this build: the look-gain normalisation never runs; scoped hip sensitivity stays the game's own.** The profile switch is left as retail ships it (`normalize_zoom_input = true`), because retail's own helper has a fallback for a code proof that does not pass, and the translation takes exactly that path:

| step | retail (`native/first_person/native_ads_editor.cpp`) | here (`src/pose_helper.c`) |
|---|---|---|
| the adapter asks for input mode | `pose.lua:217`, `:233-234` (`NADSE_ValidateInput`, `NADSE_BeginInput` / `NADSE_BeginSteadyInput`) | `src/pose_adapter.c:225`, `:258-259` |
| validation is the plain one | `NADSE_ValidateInput` = `NADSE_Validate` | `:1714` |
| begin does not look at the code proof. It takes UnitAPI+0x90 when the slot holds the pinned world-matrix getter (`input_sdk`) and starts with reason 2 | cpp:1430, cpp:1438-1441 (`reason = optional_sdk ? INPUT_CODE_UNAVAILABLE : INPUT_SDK_UNAVAILABLE`) | `:1631`, `:1637-1640` |
| the two extra values go in: `NADSE_Input00Thunk` in UnitAPI+0x00 instead of the plain getter hook, `NADSE_Input90Thunk` in UnitAPI+0x90 | cpp:1345-1348 (`callback`), cpp:1459-1472 | `:502-505`, `:1658-1664`, `:1671` |
| the proof is made lazily from the hooks, every 250 ms; while it fails nothing is normalised | cpp:619-625 (`input_check_code`), cpp:830 (`if(!input_state.code_ok…)return result`) | `:802-808`, `:1020-1021` |
| the proof itself | cpp:602-618: 16 hashes and 7 constants | `:782-783`: `B_INPUT_ESTABLISHED` is 0, so it answers false at once, which is what retail's answers when a pin differs |
| End takes both values out again with the other three | cpp:1349-1386 | `:1537-1571` |

So the lease does not fail and nothing has to be switched off: placement, ADS placement, motion, transitions, the draw and entry blend and the zoom depth work, the input statistics say `installed 1, active 0, reason 2`, and no gain, preset selector or private record is ever touched (`tests/run-wire-pose.sh` asserts all of it on the real helper; `tests/run-diff-pose.sh` asserts the same after every look evaluation of its production program). The cost is retail's: two more slot values while a lease runs, UnitAPI+0x90 among them, which forward every call that is not the look evaluator's without a lock, a counter or a read (cpp:812-816; `src/pose_helper.c:1062-1069`).

The two thunks are assembly (`src/pose_input_thunks.S`, a port of `native_ads_input.S`); the log says once `Refused: look gain: the look evaluator's code proof is not established on build 25480438 …`.

To carry no extra hook instead, hand `tfp_wire_pose_bind` the profile `{2, false}`: the adapter then calls `tfp_pose_validate` / `tfp_pose_begin` / `tfp_pose_begin_steady`, UnitAPI+0x90 is left alone and UnitAPI+0x00 gets the plain C getter hook (also tested). That would be a difference from retail's call sequence and is **not** what the feel build does.

When the open constants are established and `B_INPUT_ESTABLISHED` becomes 1, the same binding starts normalising with no change here; the log line then reads `Ready: look gain: normalisation is active`.
