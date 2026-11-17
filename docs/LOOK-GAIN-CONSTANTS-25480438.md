# Look-gain constants on build 25480438

The look-gain normalisation in `src/pose_helper.c` had 27 names for 20 values that retail (26568081) knows and this build did not (`docs/POSE-HELPER-TRANSLATION.md`, "Open"; `docs/WIRING-INVENTORY.md` section 2 row A). This is the result of looking each one up in the images, on 2026-11-16.

**19 of the 20 values (26 of the 27 names) are established from the images. One is open: `B_PIN_EXIT_FNV`.** The 26 are entered in the constant block. `B_INPUT_ESTABLISHED` is still 0, so the normalisation refuses exactly as before; nothing here has run in the game.

Everything below is re-derived by `tools/find_look_gain.py` (PASS / FAIL per name; `-v` prints the bytes; `--check-c src/pose_helper.c` compares the constant block). It reads the four images and writes nothing.

## Images

File offset = RVA in all four. Identity is the PE time stamp and image size.

| image | build | stamp, size | file |
|---|---|---|---|
| game.dll here | 25480438 | `0x6ab3b43f`, `0x4744000` (`profile.h` `FP_GAME_STAMP`, `FP_GAME_SIZE`) | `~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll` |
| exe here | 25480438 | `0x6ab382e4`, `0x39e8000` (`FP_EXE_STAMP`, `FP_EXE_SIZE`) | `~/Applications/HellForge/mods-retired/localappdata-1019/LocalAppData/LinksLinkToMemory/r6_reload_exe_20261017.bin` |
| game.dll retail | 26568081 | `0x6af322c1`, `0x3382000` | `~/Projects/Linux HD2 Mods/builds/26568081/runtime/images/game-26568081.dll` |
| exe retail | 26568081 | `0x6af2d5e6`, `0x29fe000` | `~/Projects/Linux HD2 Mods/builds/26568081/runtime/images/exe-26568081.bin` |

The retail images are the right ones and readable where it matters: all 16 of retail's pins (`native_ads_editor.cpp` `INPUT_PINS`) hash to retail's own values in them.

## Method

1. **Hash.** `input_code` (c:782-800, cpp:602-618) computes FNV-1a 64 (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`) over `length` bytes from `game + offset` or `exe + offset`. The lengths are literals in `INPUT_PINS` (c:761-777), the same as retail's; only the offset and the hash are build constants.
2. **Matching a span.** Retail's span is disassembled (`objdump`), and every rip displacement and relative branch target is made a wildcard ("targets wild"). Where the builds are known to differ in structure offsets, 32-bit displacements and immediates are wildcards too ("fields wild"). The pattern is searched in the whole image of this build. A value is taken only when there is exactly one match.
3. **Checking the match.** The two spans are then compared instruction by instruction: same boundaries, same bytes outside the wildcards. Every field that differs is listed and has to be a difference already known from another source (`B_SETTINGS_DEGREES`, `B_GAME_PRESETS`, `B_WORLD_ENTITY_MAP`, …). No span has an unexplained difference, which also means the dump holds no inline patch in them.
4. **Data addresses.** A data constant is the address that the *matched instruction* names here, not a search for the number (the same number can sit at several addresses). Every matched load of a retail address, across all matched spans, has to name one single address here; the bytes at both addresses have to be equal.
5. **Anchors.** The four pins established earlier (evaluator, solver, steering, enter) are re-compared the same way first, and serve as the matched spans for step 4. Evaluator: 2051 instructions each, 205 relocated targets, 45 field changes, nothing else. Solver: 293, no field change. Steering: 68, 4 field changes (avatar stride and steering fields). Enter: 665, 13 field changes, and one instruction with its two address registers swapped (`lea r8d,[rdx+r10]` / `[r10+rdx]`, retail `0x110ff63`, here `0x127e093`).

## The table

"R" = retail address, "H" = this build. Addresses are `game+` unless marked `exe+`.

### Code pins

| name | retail | here | evidence | confidence |
|---|---|---|---|---|
| `B_PIN_2` | `0x111a948`, 24 bytes | `0x1288908` | It is the evaluator's jump table: the 24 bytes straight after the evaluator span in both builds (`0x1286330 + 9688`). The evaluator's one table load names it: R `0x11186c0` `418b8c81 48a91101` `mov ecx,[r9+rax*4+0x111a948]`, H `0x1286680` `418b8c81 08892801` `mov ecx,[r9+rax*4+0x1288908]`. Six entries, each retail's + `0x16dfc0` (the evaluator's move). R bytes `cd861101 d6861101 e0861101 f4861101 f4861101 ea861101`, H bytes `8d662801 96662801 a0662801 b4662801 b4662801 aa662801` | high |
| `B_PIN_2_FNV` | `0xded70c3fd0eba95c` | `0x6f23c15a305e84dc` | hash of the 24 bytes | high |
| `B_PIN_3` | `0x1114950`, 1724 bytes | `0x1282a40` | targets wild: 1 match in the image for the whole 1724 bytes (and 1 for the first 64 alone). 371 instructions each, 30 relocated targets, no other difference. First 24 bytes equal: `488bc4564881ec600100000f2978b8488bf2440f2940a844` | high |
| `B_PIN_3_FNV` | `0x3690803fb2e8bced` | `0x6d6b65a1d985444d` | hash of the 1724 bytes | high |
| `B_PIN_EXP_FNV` | `0x1f8ff70`, 1013 bytes, `0xe4a380a8807b6ec2` | `0xfa3d329f82edb886` (at `B_GAME_EXP` `0x21087d0`) | targets wild: 1 match for the whole 1013 bytes, at the address already known. 184 instructions each, 65 relocated targets, no other difference. **Length:** this build's function table has an entry `0x21087d0`-`0x2108bc5`, which is 1013 bytes: the function ends where retail's span ends. The three 512-byte lookup tables it reads are equal in both images (R `0x2250210`, `0x2250410`, `0x2250610`; H `0x23d07c0`, `0x23d09c0`, `0x23d0bc0`) | high |
| `B_PIN_6` | `0x1109640`, `0x1a0` bytes | `0x1277780` | fields wild: 1 match (1 for the first 64 bytes with targets wild only). 100 instructions each, 11 relocated targets, one field change: R `0x11096a2` `f30f10b0 88c40a00`, H `0x12777e2` `f30f10b0 08c40a00`, which is `B_SETTINGS_DEGREES` (`0xac488` → `0xac408`). The function is `0x19f` bytes in both (this build's function table: `0x1277780`-`0x127791f`); the `0x1a0`th byte is padding, `c3` in retail's image and `cc` here, and is in the hash as it is. The exit function calls it in both builds | high |
| `B_PIN_6_FNV` | `0xf17ad9ae58e57ab0` | `0x36566b360cb60898` | hash of the `0x1a0` bytes | high |
| `B_PIN_7` | `0x110b830`, `0x5b0` bytes | `0x1279970` | Retail's span is ragged: it starts 2 bytes before an instruction boundary (inside the rip displacement of the load at R `0x110b829`) and ends 5 bytes into a 7-byte `lea`. The whole instructions in between (R `0x110b832`-`0x110bddb`) match in 1 place, fields wild (1 for the first 64 bytes, targets wild only): 326 instructions each, 34 relocated targets, 3 field changes = `B_GAME_PRESETS + 0x8c` twice (`0x2cb21cc` → `0x32f9a1c`) and the span's own jump table (`0x110c2a8` → `0x127a3e8`, moved with the code). Ends: the load before it is the same instruction and reads the same number (R `0x110b829` `f3440f101d 0ab91301` → `0x224713c`; H `0x1279969` `f3440f101d 4adf1401` → `0x23c78bc`); the `lea` after it is `lea r10,[image base]` in both (R `0x110bddb` `4c8d15 1e42effe`, H `0x1279f1b` `4c8d15 de60d8fe`) | high |
| `B_PIN_7_FNV` | `0xd77b549041450f76` | `0xcb8c325895c1bf1d` | hash of the `0x5b0` bytes | high |
| `B_PIN_9` | `0xe69ca0`, `0xdc` bytes | `0xfd9ba0` | The entity → avatar lookup. Fields wild: 1 match. 59 instructions each, 3 relocated targets, 5 field changes, all the two world fields known from elsewhere: `0xf24990`/`98`/`9c`/`a0` → `0xf22ec8`/`d0`/`d4`/`d8` (`B_WORLD_ENTITY_MAP`) and `0x1e693d` → `0x1e65e4` (× 8 = `B_WORLD_ENTITY_AVATAR` `0xf32f20`). Second witness: the steering pin's call at H `0x128d848` (`e853c3d4ff`) goes to it, as retail's at `0x111f888` goes to `0xe69ca0` | high |
| `B_PIN_9_FNV` | `0x91b780e54d5bb826` | `0x29608cd098aef135` | hash of the `0xdc` bytes | high |
| `B_PIN_12` | `exe+0x1f3050`, 51 bytes | `exe+0x1fd190` | targets wild: 1 match in the executable. 15 instructions, the only difference is the call displacement (R `e872d6eaff`, H `e82207eaff`; both go to the function the world-matrix getter calls, R `0xa06d0`, H `0x9d8c0`). `0x90` below `B_EXE_WORLD_MATRIX` in both builds | high |
| `B_PIN_12_FNV` | `0xdafbc85ae8dffa23` | `0x2d9df1a66a439fb6` | hash of the 51 bytes | high |
| `B_PIN_13` | `exe+0x1f3090`, 76 bytes | `exe+0x1fd1d0` | targets wild: 1 match in the executable. 22 instructions, two call displacements differ. `0x50` below `B_EXE_WORLD_MATRIX` in both builds | high |
| `B_PIN_13_FNV` | `0x9f25c14a58784f1f` | `0xe99ec6ca69868d09` | hash of the 76 bytes | high |
| `B_PIN_EXIT_FNV` | `0x11108a0`, `0x1dd0` bytes, `0x1a26ca7b7bbba077` | **OPEN** (candidate `0xab98f35fc8fb95d5`, not entered) | see "Open" below | |

### The seven numbers

Each is the address the matched load names; the 8 bytes at both addresses are equal (the number and its neighbour); all are in this build's read-only data.

| name | retail | here | bytes at both | matched loads (R → H) | confidence |
|---|---|---|---|---|---|
| `B_CONST_ONE` | `0x22465c4` | `0x23c6d70` | `0000803f` (1.0f) | 5: evaluator `0x111870f` → `0x12866cf`, solver `0x1109373`, pin 6 `0x1109677`, pin 7 `0x110b90a`, enter `0x110ffae` | high |
| `B_CONST_MINUS_ONE` | `0x2247894` | `0x23c800c` | `000080bf` (-1.0f) | 3: evaluator `0x11187d1` → `0x1286791`, solver `0x1109342`, pin 7 `0x110b981` | high |
| `B_CONST_RAD_TO_DEG` | `0x2246fb0` | `0x23c7730` | `e02e6542` | 3: evaluator `0x111889a` → `0x128685a`, pin 6 `0x1109719`, `0x110978e` | high |
| `B_CONST_MINUS_HALF_PI` | `0x22478c8` | `0x23c8040` | `db0fc9bf` | 1: solver `0x1109398` → `0x12774d8` | high |
| `B_CONST_HALF_PI` | `0x2246774` | `0x23c6f1c` | `db0fc93f` | 1: solver `0x11093c3` → `0x1277503` | high |
| `B_CONST_MINUS_75` | `0x2247ab0` | `0x23c8228` | `000096c2` (-75.0f) | 1: evaluator `0x111865e` → `0x128661e` | high |
| `B_CONST_DOUBLE_ONE` | `0x2246900` | `0x23c7098` | `000000000000f03f` (1.0) | 2: evaluator `0x111866f` → `0x128662f`, `0x11186fe` → `0x12866be` | high |

Across all matched spans no retail data address maps to two addresses here.

### Steering cells and the device field

| name | retail | here | evidence | confidence |
|---|---|---|---|---|
| `B_GAME_NO_AVATAR` | `0x2e3fcb8` | `0x3484788` | steering pin: R `0x111f8a0` `3b05 1204d201` `cmp eax,[rip]`, H `0x128d860` `3b05 226f1f02`. One matched read | high |
| `B_GAME_HOST_AVATAR` | `0x2e3fa9c` | `0x348456c` | pin 9, two matched reads: R `0xe69cb9` `8b05 dd5dfd01`, H `0xfd9bb9` `8b05 ada94a02`; R `0xe69d68`, H `0xfd9c68`. The 16 bytes before the cell are equal in both images (`986538bf24eed0f202bce2f657d7f6e1`) | high |
| `B_GAME_IDLE_AVATAR` | `0x2e3f0e4` | `0x3483c34` | three matched reads: steering R `0x111f88f` `8b05 4ff8d101`, H `0x128d84f` `8b05 df631f02`; evaluator R `0x1118d55`, H `0x1286d15`; enter R `0x111001f`, H `0x127e14f`. The 16 bytes before the cell are equal in both images | high |
| `B_INPUT_DEVICE` | owner `+0x324` | `0x324` | The evaluator loads `rdi` from `B_GAME_INPUT_OWNER` (H `0x12865fe` `488b3d 13691f02` → `0x347cf18`, the hook's return address) and reads `mov eax,[rdi+0x324]` twice: H `0x128678b` and `0x128686a`, bytes `8b8724030000`, the same as retail `0x11187cb` and `0x11188aa`. The flag bytes after it are unchanged too (`[rdi+0xa7af8]`, `+0xa7b00`, `+0xa7b01`, `+0xa7b44`, `+0xa7b50`) | high |

### Confirmed on the way

Two items `docs/POSE-HELPER-TRANSLATION.md` lists as not verified (Open 5 and 6) have a matched instruction now:

- `B_GAME_CLOCK` `0x3326348`: the evaluator's load at R `0x1118a17` (→ `0x2ce14f8`) is H `0x12869d7` (→ `0x3326348`).
- `B_WORLD_ENTITY_AVATAR` `0xf32f20`: pin 9's `lea rax,[rax+0x1e65e4]` then `[r11+rax*8]` at H `0xfd9c50` is `world + 0xf32f20 + row*24`.

## Open: `B_PIN_EXIT_FNV`

Retail pins `0x1dd0` bytes from `game+0x11108a0`. The address here is known (`B_PIN_EXIT` `0x127e9d0`); the hash is not entered, because on this build the function is a different body.

- Retail's function is `0x1d9d` bytes, 1696 instructions (`ret` at `0x111263c`). Retail's `0x1dd0` covers it, 3 padding bytes, and the first `0x30` bytes of the next function (`0x1112640`).
- This build's function is `0x1d58` bytes, 1683 instructions (function table: pieces from `0x127e9d0` to `0x1280728`, `ret` at `0x1280727`), then 8 padding bytes, then the same next function at `0x1280730` (its 34 instructions equal retail's).
- The first 39 instructions are equal. After that 1378 instructions pair up once stack-slot numbers are ignored; the rest differ in register choice, stack slots (retail `[rbp-0x80]`, here `[rsp+0x70]`), block order, and where one register save sits. Direct calls: 72 in retail, 73 here (one helper is called 5 times instead of 4); of 23 distinct retail callees 20 have the same first 40 bytes here, 3 changed. The 27 indirect calls are the same slots in both. The preset table is named once in each (R `0x1110fe0`, H `0x127f235`, `lea rax,[B_GAME_PRESETS]`).
- So it reads as the same source function compiled differently, but that is a judgement, not a byte match, and retail's reason for the pin (the exit path is one of the "outer preset consumers" that must not interpret a private preset index) was argued on retail's body.
- **Candidate:** the `0x1dd0` bytes the C would read from `game+0x127e9d0` hash to `0xab98f35fc8fb95d5`. They are the whole function, its padding, the whole next function (`0x6b` bytes) and its padding, ending exactly at the next function start `game+0x12807a0`. The length is a literal in `INPUT_PINS`, not a build constant; the span that corresponds to retail's exactly (function + padding + `0x30` bytes) would be `0x1d90` bytes and needs that literal changed.
- To close it: read this build's exit function for how it uses the preset index (the block around `0x127f235`), then either enter the candidate or decide the pin is not needed here.

## What the constant block holds now

`src/pose_helper.c`, constant block: the 26 names above with their values and a one-line source each; `B_PIN_EXIT_FNV` 0; `B_INPUT_ESTABLISHED` 0. With the flag 0, `input_code` returns false before it reads anything, so no entered value is used by this build. `tests/run-diff-pose.sh` and `tests/run-vectors-pose-adapter.sh` pass with the values entered and report the same comparisons as before (the look-gain test object replaces all 27 names with its own, `tests/diff_pose_helper_open.h`).

Text elsewhere that still says "nine pins and seven constant addresses are open": the comment above `input_code` (c:779-781), `docs/POSE-HELPER-TRANSLATION.md` sections 2 and 6, `docs/WIRING-INVENTORY.md` row A, `docs/BUILD-DATA-25480438.md` (generated). Not edited here.

## Unproven until it runs in the game

Everything above is from files. None of it has been read from the running game.

1. **The running game's bytes.** The hashes are of dumps taken on 2026-10-17 (exe) and 2026-10-22 (game.dll). In the game the 15 spans must hash to the same values: another mod's inline patch in a span, or a code page the game has not yet unpacked, makes `input_code` answer false (it retries every 250 ms). Check: with the flag on in a test build, `stats.reason` leaves `INPUT_CODE_UNAVAILABLE`; or log the 16 hashes once.
2. **The exit pin** (above). Until it is decided, `input_code` cannot pass.
3. **The three steering cells at run time.** They are writable data, so the images only show where they are. In play: the "no avatar" cell differs from the local avatar id; the host cell is used when the local entity is `0x7fff`; `input_steering` returns true with the two floats the game adds.
4. **`B_INPUT_DEVICE`.** That the field holds 0 for mouse and 1 or 2 for a pad on this build rests on the evaluator being the same code. Check: the value changes when switching between mouse and pad.
5. **The normalisation itself** has never run on this build: the two assembly thunks (`docs/WIRING-INVENTORY.md` A') are not wired, and the earlier pins' hashes, `B_GAME_CLOCK` and `B_WORLD_ENTITY_AVATAR` are also image-only.
6. **The seven numbers** are read-only data; the image is enough for them. Listed for completeness: `input_code` compares the bits each time.

## Ready-to-paste, if the exit candidate is accepted

```c
#define B_PIN_EXIT_FNV      UINT64_C(0xab98f35fc8fb95d5) /* 0x1dd0 bytes from game+0x127e9d0: the exit function (0x1d58), padding, the next function, padding */
#define B_INPUT_ESTABLISHED 1
```

Update `OPEN` / `ESTABLISHED` in `tools/find_look_gain.py` at the same time.
