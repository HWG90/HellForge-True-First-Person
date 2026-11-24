# The Warp Pack's glow in first person (2026-11-24, round 22)

The user: "When using Warp Pack the effect from the backpack is visible in first person."

The glow is particle effects that the game's EntityEffect component plays for the pack (gadget-kit
`docs/warp-pack-glow-investigation.md`, 26088801). TFP can hide only mesh draws, so hiding the pack's meshes cannot remove it.
The game hides these effects itself only in full aim-down-sights. Its displacement updater sees the native full-ADS flag
(avatar flag bit 48) rise and stops the pack's effect events. TFP shows its first-person view only while that bit is clear,
so the effects keep playing beside the eye. This round does what the game does in full ADS, but ties it to TFP owning the
view.

**Nothing here has run in the game.** This file records where the game keeps what the fix needs on 25480438, each
value proven from the listing.

Every address below is from build 25480438. "The image" is `native-work/evidence/game-pre1020.dll`, a memory image of
game.dll in which file offset equals RVA; its code section runs from 0x1000 to 0x2110a93. Listings are
`objdump -D -b binary -m i386:x86-64 -M intel` of that image (`~/.cache/tfp-round9/dis.sh START STOP`). Call and RIP-relative
cross-references were found by scanning the whole code section for `e8`/`e9` rel32 and `48/4c 8b /r [rip+disp32]`. gadget-kit's
numbers are for 26088801. None of them is used: each value was found again here.

## 1. The backpack read (inventory slot +12)

`backend_ops.c` reads the player's inventory row at `[game+0x3326738]`, using map +0x28, owners +0x40 and 48-byte rows at
+0x50, then takes the dword at row +0xc.

- The game's slot switch, called once per inventory row, reads the item of slot type `r14` from the row:

  ```
  797bf3: mov r9,[rip+…]            # 0x3326738   the inventory pool
  797ca9: lea rdx,[rax+rax*2]
  797cad: shl rdx,0x4                # row = index * 48
  797cb1: lea eax,[r14-0x1]
  797cb5: add rdx,[r9+0x50]          # + rows
  797cbe: … jmp [table 0x797d78]     # cases: 0x797cd2 [rdx], 0x797cd9 [rdx+4], 0x797ce1 [rdx+8],
                                     #        0x797cf1 [rdx+0x10], 0x797cf1 [rdx+0x10], 0x797ce9 [rdx+0xc]
  ```

  Slot types 1, 2 and 3 are at +0, +4 and +8. Types 4 and 5 are at +0x10, and type 6 is at **+0xc**. The same switch appears
  at 0x7723f4 (table 0x772458) and 0x7986e6 (table 0x798780).
- Unequipping an item compares it with row +0xc and passes slot type 6 (`5c6d49: cmp [rax+0xc],ecx` … `5c6d59: mov r9d,0x6`).
- 26251122's switch (`game-26251122.dll` 0x7a3583 `mov r9,[rip→0x2ce18e8]`, 0x7a3645, table 0x7a3708) has the same
  mapping. On that line, row +12 held the Warp Pack in the game (gadget-kit: entity 938, type B49756795D9B0E68). Cowboybingus'
  hover pack also uses row +12; it was written for this build, and its MAP.json notes that the jump-pack routine reads
  row +0xc as the pack.
- On this build the read found a pack in the game on 2026-11-17 (feel.1 and feel.2, sessions 0446bfc5 and 662b3a8f). The
  backpack row reached head visibility's check 728 (commits c1b9fb1 and 8c9b7fa). Those logs are no longer on disk.

**Why every log since says "none":** a "none" is returned only after the map found the avatar and the row's owner
descriptor matched. So the row was the player's own, and only its +0xc word was empty. No logged session wore a backpack.
The hover pack's reader says `no_backpack` in the same ten sessions and never changes. The read stays as it is. To settle it
in one game run, the "none" line now carries the row's six words, and the Head line carries `hidden_backpack`.

## 2. The displacement updater and its manager

The updater is `game+0x88c130(M, entity)`. The per-frame loop `game+0x88a700(M, dt)` calls it once for every row of M
(`88a7a0` … `88a7c2: call 0x88c130`); that loop is called from the world update at 0x573881. A spawn path at 0x88a5e0 and
another at 0x547700 reset a row and call it too.

| Name | 25480438 | Evidence |
|---|---|---|
| manager M | `[game+0x3326ac8]` | `88a5fb: mov rdi,[rip→0x3326ac8]` … `88a68f: mov rcx,rdi` … `88a69f: call 0x88c130`; the same at `54771c`/`5477bf`; the settings lookup reads it at `50fa62` |
| entity map | M+0x20 (buckets), +0x28 capacity (a power of two), +0x2c empty key, +0x30 multiplier | `88c153`…`88c1b7`: `slot = (n + key*mult) & (cap-1)`, buckets `{key, index}` of 8 bytes |
| row count | M+0x10 | `88a778: cmp [rcx+0x10],r15d` (the per-frame loop's bound) |
| descriptors | `[M+0x38] + index*8` → the pack's 24-byte entity record (+0 type, +8 entity, +0xc unit) | `88c1dd`/`88c1f6: mov r14,[rax+r12*8]`; `88c220: mov ecx,[r14+0x8]`; `88c2b0: mov ecx,[r14+0xc]` |
| ADS cache byte | `[M+0x40] + index*0x334 + 0x330` | `88c5a6: imul r15,r12,0x334` / `88c5ad: add r15,[rax+0x40]` / `88c637: cmp [r15+0x330],bpl` |
| cached mode word | `[M+0x48] + index*24 + 0x10` | `88c1fa: lea rax,[r12+r12*2]` → `[rsp+0x48]`; `88c20a: mov rax,[rsi+0x48]` → `[rsp+0xd8]`; `88c824: cmp [rcx+rax*8+0x10],r9d`; `88c83c: mov [rcx+rax*8+0x10],r9d` |
| charge | `[M+0x50] + index*4` (float) | `88c1fe: mov r15,[rsi+0x50]`; `88c7d2: movss xmm0,[rcx+rbx*4]` |

**Bit 48.** This is the native full-ADS flag, the byte at `avatar_owner + 0x53e886 + index*0x1238`, bit 0
(`88c59f: mov r11,[rip→0x3326d20]` … `88c61b: imul rax,rcx,0x1238` / `88c622: movzx ebp,[rax+r11+0x53e886]` /
`88c62b: and bpl,1`). It is TFP's `FP_AVATAR_FLAGS + 6`, the same bit that `snapshot.aiming` reads.

**What the updater does with it:**

- **0x88c637** compares the bit with the ADS cache byte.
  - On a rising edge (`88c64d`), it stops the stop list of all three modes. Each mode has five event ids at
    `settings + 0x1d8 + mode*0x34`. For each id it asks `0x8abf40` whether the event is playing, and if so stops it with
    `0x8abb20` (`88c69e`). Then it writes the cache byte (`88c6cb`).
  - On a falling edge (`88c6f1`), it writes the byte and sets `r12b = 1` (`88c6f8`). That forces the replay below even when
    the mode has not changed.
- **0x88c824** compares the mode (0 by default, 1 or 2 by charge: `88c7d7`/`88c7eb` against settings `+0x90`/`+0x8c`) with the
  cached mode word. On a difference, or with `r12b` set, it writes the word (`88c83c`) and finds the mode's row (`88c850: cmp [rbx],r9d`,
  rows from `settings + 0x1c0`, stride 0x34). Then, for each of the five events:
  - it stops the row's stop list at +0x18 (`88c870`…`88c89f`);
  - unless the bit is set, it plays the row's play list at +0x4 with `0x8ab570` (`88c8a4`…`88c90c`).

  It then runs a second path for row +0x2c and +0x30 (`0x88be00`), which this round leaves alone, as native ADS does.

**The mode table**, at `settings + 0x1c0 + mode*0x34`:
- +0x0: the mode's tag;
- +0x4: five play event ids;
- +0x18: five stop event ids;
- +0x2c and +0x30: the second path's ids.

The ADS stop loop walks `settings + 0x1d8` (the +0x18 lists) for three modes (`88c652: mov r12d,3`, `88c658: add r15,0x1d8`,
`88c6b1: add r15,0x34`, inner count `r13 = 5`).

**The settings** are the return value of `game+0x50fa40(descriptor)`:
- First it looks for a per-entity override: map M+0x60 (+0x68 capacity, +0x6c empty, +0x70 multiplier), keyed by the
  descriptor's entity, rows `[M+0xa0] + index*0x278` (`50fa7b`…`50faf4`).
- Otherwise it uses the type table: `T = [[game+0x346bf98] + 0xf12d08]`, which has two 16-byte buckets
  `{u64 type, u32 index}` probed from `type & 1`, with rows at `T + 0x20 + index*0x278` (`50f4b0`…`50f51a`).

The key is the 8-byte type at descriptor +0.

**Writing 0xffffffff to the cached mode word** makes the next updater run (the same frame, since HellForge ticks before
the game's update) see a difference and replay the current mode's effects. The game itself writes 0xffffffff into the
same 24-byte row (+0 and +8) at spawn (`88a689`, `88a692`) before calling the updater.

## 3. The effect calls

| Name | 25480438 | Evidence |
|---|---|---|
| effect manager E | `[game+0x3326570]` | `88c68a: mov rcx,[rip→0x3326570]` before the stop call; `8abf61: mov rsi,[rip→0x3326570]` inside the query |
| playing? | `uint32 game+0x8abf40(ignored, entity, event)` | It loads E itself (`8abf61`), looks the entity up in E+0x20 and returns 0 when it is absent (`8abfb2`). It checks the unit through API +0x720 and finds `event` among the 32 slots of the resource (`8ac020: cmp [rax],ebx`, stride 0x50). It returns the runtime id `[E+0x48 row + slot*4]`, which is nonzero while the event plays (`8ac031`). |
| stop | `void game+0x8abb20(E, entity, event, filter, replicate, flag)` | For each matching slot with a live id, it calls the world's particle API +0x2a0 (type 1) or +0x2b0 (type 2) and zeroes the id (`8abc15`…`8abc68`). Only `replicate == 1` sends the network message (`8abc6c: cmp dword [rsp+0x90],1` → `8abcc8: call 0xbde430`). The updater passes `filter = 0`, `replicate = 0`, `flag = 0` (`88c683`, `88c696`, `88c691`). |

**The stop must not be called for an entity E does not hold.** Its lookup falls through to index 0xffffffff
(`8abb9a`) and indexes `[E+0x38]` with it. The updater only stops an event that `0x8abf40` has just reported as playing. The
mod does the same, in the same tick and on the same thread, so the entity is known to be present.

The replicate argument is passed as 0. The stop is therefore local: other players' clients run their own updaters and keep
seeing the glow.

## 4. Fingerprints

The mod reads each of these from the running game.dll before its first call, and acts only if all of them match:

| Name | RVA | Bytes | What it pins |
|---|---|---|---|
| stop | 0x8abb20 | `48895c241848896c242056574154415641574883ec4033ed418bf13b15df80bd02418bf88bda4c8bf9744f` | the stop's prologue and arguments (E in r15, entity, event, filter) |
| stop_replicate | 0x8abc6c | `83bc2490000000017557` | the network send needs the fifth argument == 1 |
| playing | 0x8abf40 | `48895c240848896c2410488974241857415641574883ec203b15c27cbd02418bd8488b3508a6a702` | the query's prologue, E from game+0x3326570 |
| bit48 | 0x88c614 | `b8ffffffff8bc84869c138120000420fb6ac1886e853004080e5014088ac24c8000000` | the full-ADS byte: owner +0x53e886, stride 0x1238, bit 0 |
| ads_cache | 0x88c637 | `4138af300300000f84870000004084ed0f84a4000000` | the cache byte +0x330 and the two edges |
| ads_stops | 0x88c64d | `4c8b7c244041bc030000004981c7d801000090498bf7498bdf498bed0f1f8000000000448b03418b5608e8c4f8010085c07423448b064533c9418b5608488b0ddf9ea902c644242800c744242000000000e87df401004883c3044883c6044883ed0175bf` | the ADS stop loop: settings +0x1d8, query then stop, arguments 0, 0, 0 |
| mode_cache | 0x88c817 | `488b442448488b8c24d800000044394cc11075094584e40f8442010000488b5c24404533e444894cc1104881c3c0010000418bc40f1f440000` | the cached mode word +0x10 (stride 24), the forced replay, the mode table at +0x1c0 |
| mode_stops | 0x88c870 | `448b441f18418b5608e8c2f6010085c07422448b441e18458bcc418b5608488b0ddb9ca90244886424284489642420e87cf20100` | the stop list at +0x18 |
| frame_loop | 0x88a7a0 | `488b42384b8d3c7f488b72484a8b1cf8488bcbe88852c8ff8b53084c8bf0488b4da8e869190000` | M+0x38 descriptors, M+0x48 rows, the updater called each frame |
| manager | 0x88a5fb | `488b3dc6c4a902` | M is `[game+0x3326ac8]` |
| settings | 0x50fa40 | `4883ec284c8bd14885c9750733c04883c428c38b41083b05c841f7020f84930000004c8b1d5f70e1024533c048895c243048896c24384889742440458b4b68` | the override map M+0x60 |
| settings_rows | 0x50fad6 | `3901751b8b410483f8ff74134869c078020000490383a00000004883c428c3498b0a4883c428e9aff9ffff` | the override rows `[M+0xa0]`, stride 0x278, and the fallback |
| settings_type | 0x50f4b0 | `4885c9744c488b05dccaf502448bc14183e0014533c94c8b90082df1000f1f00418bc048c1e0044903c2488b10483bd174224885d2741a418bc0418d50014533c041ffc183f801440f45c24183f90272cf33c0c34885c074f88b40084869c878020000498d42204803c1c3` | the type table `[[game+0x346bf98]+0xf12d08]`, two buckets, rows +0x20, stride 0x278 |

The plan named 0x8abb20, 0x88c637 and 0x88c824; 0x88c824 lies inside `mode_cache`. Each byte string above was cut from the
image with `d[rva:rva+n].hex()`. The module checks three more:
- `playing_scan` at 0x8abfda (the query's slot scan: resource +0x38, stride 0x50, 32 slots, runtime `[E+0x48] + row*0x218`);
- `resource` at 0x4fc3a0 and `resource_rows` at 0x4fc419. These are the effect resource lookup: `[[game+0x346bf98]+0xf127b8]`,
  0x560 buckets of `{u64 type, u32 index}` probed from `type % 0x560`, with resources at `+0x5600 + index*0xf48`.

The module reads what is playing from memory, as 0x8abf40 does, so it needs these three as well. It never calls 0x8abf40.

## 5. What the mod does (src/warp_glow.c, src/wire_warp.c)

Every tick on the game thread, after head visibility's update (src/mod_view.c `warp_note`):

1. **Take a player snapshot and read the backpack** (`tfp_ops_backpack`).
2. **If the pack's type** (descriptor +0) is B49756795D9B0E68, read:
   - its displacement row in M, which must hold the same entity and the same type;
   - the settings and the mode table;
   - bit 48, the ADS cache byte, the cached mode word and the charge;
   - which ids of the table are playing. This is read from memory the way 0x8abf40 answers: the first slot of the pack's
     effect resource with that id, and that slot's runtime id is nonzero.

   The `Warp:` line says all of this on every change and once a second. A second line gives the mode table when it is
   first read or changes.
3. **The stops.** All of these must hold:
   - `warp_hide.txt` is present;
   - every fingerprint matches;
   - TFP owns the view: it is switched on and head visibility is active;
   - bit 48 is clear;
   - the effect manager holds the pack.

   Then, for each id of the three stop lists that plays, call `game+0x8abb20(E, pack, id, 0, 0, 0)` (`TFP_OPS_EFFECT_STOP`,
   SEH-guarded in src/wire_win.c). This is the same set and the same arguments as the game's full-ADS loop at 0x88c64d.

   This repeats in every tick in which one of them plays. That covers the three cases where the game plays them again: a
   mode change (0x88c824), a bit-48 falling edge (`r12b = 1` at 0x88c6f8) and a re-entry of the view.
4. **The restore.** It runs once stops have been sent for a pack and then one of these happens:
   - the view leaves TFP;
   - the switch is off;
   - the backpack is no longer that Warp Pack (dropped or changed);
   - the mod is taken out live.

   It finds the same pack again: M is unchanged, the map gives the same row, the descriptor and type are the same. It then
   writes 0xffffffff to the cached mode word through HellForge's write. The game's next update (in the same frame, since
   HellForge ticks first) sees a different mode and plays the current one. If the manager no longer holds the pack, nothing
   is written and nothing is owed.
5. **Never touched:** bit 48 and the ADS cache byte are never written. While bit 48 is set, nothing is called: the game
   does the stops itself in full ADS.

`tests/run-warp-glow.sh` runs the module on mocked memory laid out as above, with a stop that zeroes the runtime id as
0x8abb20 does. It checks:
- the stops on entry, on a mode change, on an aim release and on a re-entry;
- the restore on exit, on a drop, when the switch goes and at the mod's stop;
- that nothing runs without the Warp Pack, without a backpack read, with a fingerprint that differs, with code that cannot
  be read, or with a pack the effect manager does not hold;
- that a stop which raises is refused;
- a 4000-tick random run, after which every write is the mode word's 0xffffffff and bit 48 never changed.

**Not covered**, as in native ADS:
- one-shot warp effects outside the 3 x 5 table;
- the second path (`0x88be00`, row +0x2c/+0x30).

If the glow stays while the `Warp:` line says nothing of the table plays, the effect is not in the table. The fallback is
the rig's `World.link_particles` hook, filtered on the pack's unit; this round does not build it.
