# Live SDK input follow-up

Fresh-session binding evidence: build25480438, dev.3 session
`9472b065334c40c9a443d56cb03a3279`, generation1 census
`HF/logs/sessions/<session>/game/true-first-person-sdk-g1.tsv`.
This follow-up reads that existing census and the original captured EXE only.
**No discovered SDK function was invoked; no process attachment or game,
installed-mod or product write was performed.** Binding availability is live
evidence; the helper bodies below come from the recorded same-build EXE, not a
new live byte reread.

## Camera-shake linkage is device feedback

The Mouse and Pad callback names are now confirmed live. Names alone would be
misleading here: the traced linkage manages device feedback from camera shake,
and does not provide a native camera-look filter or authored view-shake toggle.

| Exact original EXE site | Observed operation |
| --- | --- |
| `0x483560..0x4835be` | `link_to_camera_shake` gets its closure device, first argument's world pointer and second argument key, then calls0x337b60 |
| `0x337b60..0x337bf2` | Reads world+0x2308c0; finds a keyed record in manager+0x60 and appends that device to its vector |
| `0x1b143b..0x1b1445` | World update calls manager update0x22bb40 |
| `0x22cf83..0x22d0ff` | Iterates the linked devices, reads device+0x178, caps at2 motors, distinguishes literal `ds_pad`, resolves motor IDs and writes device+0x180 motor record stride0x20, member+0x18 |
| `0x4829f0..0x482a1e` | Live-named `num_rumble_motors` returns device+0x178 |
| `0x482a20..0x482a87` | Live-named `set_rumble` writes **the same device+0x180 / stride0x20 / member+0x18** |
| `0x22d248..0x22d251` | Authored camera pose output0x4cf6f0 occurs separately after the device loop, including the path with no linked devices |

This concrete writer/reader match identifies the linked-list effect as rumble
motor feedback. It does not alter the game DLL's previously traced native FPS
look smoothing, lens-based ratio or scoped sensitivity. It is not a supported
route to smoothing the arms/weapon or removing transition jumps. This bounded
trace does not claim absence of every possible aliased engine consumer.

**Original-build unlink anomaly:** live `unlink_to_camera_shake` points to
EXE0x4835c0. Its entire95-byte/25-instruction body normalizes identically to
the link callback, and the resolved direct and indirect-cell call targets are
also identical. It too calls **0x337b60**, the append helper. A distinct
swap-removal helper exists at0x337c00, but neither callback calls it. Therefore
the discovered unlink name must not be treated as a safe inverse or called
speculatively. This may be a shipped bug or capture-specific fact; no behavior
across other builds is implied.

## Dead zones affect axis remapping, not the native FPS filter

The live Mouse table confirms `axis` at0x4826f0, `dead_zone` at0x482870 and
`set_dead_zone` at0x4828f0. The getter/setter access per-device+0x138 entries
of stride12, including mode and float amount. `Mouse.axis` obtains raw axis
vectors from device+0x108, chooses either that stored configuration or a
per-call override, and calls0x590ae0.

The helper's exact original behavior is threshold/remap processing:

- Mode1 applies independent component dead zones, zeroing within the
  threshold and rescaling outside it.
- Mode0 processes vector magnitude and direction, clamps magnitude and
  applies a radial dead zone.
- Other modes copy the raw vector. The captured census reports names `RAW`,
  `INDEPENDENT` and `CIRCULAR` but not their numeric values, so the numeric
  branches are not assigned those names from the census alone.

There is no time/history input to this helper: it is not the temporal filter
previously traced in the FPS evaluator. Modifying dead zones would alter axis
response and possibly user input preferences; it would not remove the later
native `R*D*U + S` lens/scoped-gain order. This pass does not establish that
the gameplay look producer uses this Lua default-config path in every device
mode, so it is not justified to change these settings as a TFP repair.

The completed bounded live **Input** table has only one function,
`event_queue` at0x486fc0. Neither that table nor the Mouse table contains a
named filter, smoothing or sensitivity setter. This is a bounded inventory
observation, not proof that such internals cannot exist elsewhere.

## Evidence and limits

[live-input-sdk-followup-evidence.json](live-input-sdk-followup-evidence.json)
contains14 exact byte-range witnesses, live row numbers, original EXE/census
hashes, and the link/unlink normalized and resolved-target comparisons.
The reproduction script and grouped original function assembly are under
`work/live-input-sdk-followup/`.

REA/Ghidra independently decoded the separately seeded callback slices;
complete responses are retained as `rea-link.json` and
`rea-unlink-seeded.json`. The initial combined slice did not automatically
define its unreachable second function; that failed request is retained as
`rea-unlink.json`, and the second callback was then opened with its own entry
seed. External helpers/IAT cells lie outside those raw slices. Their inferred
generic decompiler signature/calling convention is not accepted as a Win64
SDK declaration; the register-level and complete-capture assembly witnesses
carry the data-flow claims above. REA targets were closed after inspection.

No live behavior or visual acceptance is inferred from availability or static
data flow. This closes the narrow SDK-name follow-up without adding an input
hook, changing input settings or repeating the full SDK inventory.
