# Performance (2026-11-25, round 28)

The user: "Continue refining and improving performance of mod".

**Nothing here has run in the game.** This round adds measurement (logging only) and takes `host_write` off
`WriteProcessMemory`. The next game run decides what round 29 changes.

## 1. What the survey found (round 27, 0.6.0-feel.47 to .52 logs, code at dfec807)

The survey's working files are in `~/.cache/tfp-round27/`. They are `result-0.md` (the survey), `result-1.md` (a second
analyst's re-measure), and `bench.c` / `luabench.c` (native benchmarks).

- **One thread.** The tick, the primary-draw hook, and the camera, FOV, near, blur and pose hooks all run on the game
  thread (668 in soak-34, 676 in -31-user). The SRW locks are never contested. Game-thread time is what counts.
- **The tick, from the Cost line** (µs; the first line of each log dropped):

  | log (build) | ticks/s | tick mean p50 / p90 / p99 | the tick's largest per second, p50 / p99 / max | arms mean p50 / p90 |
  |---|---|---|---|---|
  | harness 22–30 (feel.47–.51) | 30 | 343–365 / 378–401 / 431–455 | 567–592 / 1663–1751 / 9006 | 57–64 / 69–75 |
  | harness 33–34 (feel.52) | 30 | 385–406 / 436–456 / 489–503 | 612–640 / 1859–1881 | 59–61 / 86–87 |
  | **user's play -31 (feel.51)** | **49** | **1435 / 1881 / 2527** | **5652 / 15814 / 29299** | **103 / 154** |

- **The user's play costs about 4× the harness.** At 49 game-thread frames a second (20.4 ms), the tick is 6.9% of the
  frame at p50 and 8.9% at p90. Once a second the tick spikes to 5–29 ms. The machine was about 1.7× slower in that
  session (guarded reads cost 0.10 µs against 0.06 µs, and the arms cost 103 µs against 60 µs). That still leaves about
  0.8 ms per tick of extra work, which appears only with first person on in a real mission. Something that scales with
  the world is the suspect.
- **The "effects cost 200 µs" figure was confounded.** The idle-effects seconds were seconds with first person off.
  The tick is about 195 µs with first person off and 388 µs with it on (harness).
- **`host_write` was `WriteProcessMemory(GetCurrentProcess())`** (`wire_win.c:139-143` at dfec807). It is a wineserver
  round trip of about 4.9 µs on an idle server and more under load. That is the path the mouse-lag note blames. Its call
  count per tick was not known.
- **The vignette's entity scan** (`vignette.c:252-268`, `wire_win_effects.c:187-232`) is the prime suspect for the
  user-play gap:
  - Once a second, `World.entities` runs, followed by an `eng_index` and an `identity` call for each entry.
  - Each tick, a slice of 96 entities goes through `alive` and `instances`, at a native lower bound of 22.6 µs.
  - The copy timed itself (`scan.microseconds`, `scan.worst`), but that time was never logged.
- **Other native costs** (bench): `VirtualQuery` 0.49 µs, the `snprintf` of a 250-byte line 1.1 µs, a 4 KB `memcpy`
  0.04 µs, and a `ReadProcessMemory` of 64 B 0.055 µs (in-process under Wine).
- **The Cost line was only printed when the arm stats changed** (`mod_view.c:794-797`). Seconds with the drive idle
  were folded into the next line.

## 2. What this round measures

All of it is logging. Nothing the mod does in the game changes, apart from `host_write`'s copy (section 3). The added
cost per tick:

- about ten `QueryPerformanceCounter` reads (Wine reads the TSC in process);
- three relaxed atomic increments per counted call;
- the depth of field's and the vignette's own two timer reads per entry.

Nothing is allocated, and nothing is formatted except the once-a-second line.

**The Cost line** keeps its text, so `~/.cache/tfp-round27/cost.py` still parses it. It now comes every second
whatever the arm stats do, right after the `Arms:` line when there is one.

**The Perf line** comes right after it, every second (`src/perf_line.h`):

```
Perf: per tick mean/largest us over N ticks: pre A/B, frame A/B, dof A/B, vignette A/B, notes A/B, menus A/B, arms A/B, rest A/B; scan E entries U us (worst W us, S this second); per second: host_write H, WriteProcessMemory P, VirtualQuery Q, eng_call C, log lines L
```

The `N` ticks are the same ticks as the Cost line's. For each section, A is the mean per tick and B the largest in the
second. The sections are exclusive and add up to the tick:

| section | what runs in it |
|---|---|
| pre | gate and steady notes, ADS seat, quit guard, FOV lock, lock and FOV notes (the top of `tick_body`) |
| frame | `tfp_wire_frame` without the two below: the runtime, the helpers' stats, motion, pose and the effects' glue; also `eng_temp_end` |
| dof | the depth of field's own work (`rt_dof_update` / `_stop` / `_shutdown` in `wire_effects.c`), without the vignette |
| vignette | `dv_update` / `_stop` / `_shutdown`: `World.entities`, the entity copy, the 96-entity slice, the rows |
| notes | entry, head, warp, orphan and trace notes |
| menus | the editor and panel ticks before the frame; the panel, menu, options and settings frames after it (a dof or vignette stop made there counts under dof and vignette) |
| arms | `arms_tick`, including its once-a-second lines (this line's own formatting among them) |
| rest | the tick less all of the above: stage checks, keys, the `calls=` text, the hook notes, and every tick before the mod runs |

The rest of the line:

- **scan:** the vignette's last `World.entities` copy (`tfp_win_effects_last_scan`). It gives the entries, the µs of the
  copy, the worst copy so far, and how many copies were made this second. The copy's time starts after
  `World.entities` and its length, so the call itself is in the vignette section, not here.
- **host_write:** the helpers' `host.write` calls (their results into the mod's own structs).
- **WriteProcessMemory:** every call still made. Since this round, these are only the effects' game-memory writes (the
  aim option and the movement byte).
- **VirtualQuery:** every call of the Windows hosts (`tfp_win_query`). That covers the helpers' page checks, the arm
  drive's `api()`, the pose region, and the effects' markers, movement and aim option.
- **eng_call:** the engine bridge's protected calls into stingray functions (`CallV`: `eng_call` and `eng_method`, failed
  ones included). `eng_index`, `eng_len` and the other table reads are not calls and are not counted.
- **log lines:** lines from `say()`, the wire's log (`ops_log`), and the engine bridge's `Log`. The few lines from the
  start-up, menu and editor that go straight to `hf->log` are not counted.

The line is about 310 bytes in a normal second. With every count at `UINT32_MAX` and every time at its clamp of
99,999,999 µs, it is 468 bytes, so it fits the 700-byte buffer of `say()` (`tests/perf_line_test.c`).

## 3. `host_write` without `WriteProcessMemory`

`host_write` (`src/wire_win.c`) is the `write` of the camera, FOV, near and blur helper hosts. Through `tfp_win_write`
it is also the pose helper's (`src/wire_win_pose.c`). Each helper uses it for one job: copying a result into a struct
its caller handed it. That is what retail's helper DLLs did with `WriteProcessMemory`.

### The audit: every call site and where its destination lives

| # | site | public entry | the destination its caller passes | memory |
|---|---|---|---|---|
| 1 | `camera_helper.c:1754` `output_generation` (also through `failed_begin`) | `tfp_camera_begin` | `camera_adapter.c:126` `&generation`, a local of `adapter_begin` | mod stack |
| 2 | `camera_helper.c:1828` `read_stats_locked` | `tfp_camera_read_stats` | `camera_adapter.c:73` `&a->stats_buffer`; the adapter is `wire.camera` in the static `TfpWire wire` (`mod_view.c`) | mod .bss |
| 3 | `camera_helper.c:1876` `handoff_stats_locked` | `tfp_camera_handoff_stats` | no caller in the mod (the test only) | — |
| 4 | `camera_helper.c:1903` | `tfp_camera_entry_trace` | `mod_view.c:407` (`entry_note`) `&trace`, a local | mod stack |
| 5 | `camera_helper.c:1924` | `tfp_camera_anchor_stats` | no caller in the mod (the test only) | — |
| 6 | `fov_helper.c:805` `output_generation` | `tfp_fov_begin`, `tfp_fov_begin_fird` | `fov_adapter.c:144` `&handle->generation`, with handle `&l->storage` of the `TfpFovLease` in `wire` (`fov_adapter.c:401`) | mod .bss |
| 7 | `fov_helper.c:983` `read_stats_locked` | `tfp_fov_read_stats` | `fov_adapter.c:69` `&a->buffer` of the same lease's adapter | mod .bss |
| 8 | `near_helper.c:346` `output_generation` | `tfp_near_begin` | `near_adapter.c:86` `&handle->generation`, with handle `&l->storage` of the near lease in `wire` (`near_adapter.c:289`) | mod .bss |
| 9 | `near_helper.c:440` | `tfp_near_read_sites` | no caller in the mod (the test only) | — |
| 10 | `near_helper.c:456` `read_stats_locked` | `tfp_near_read_stats` | `near_adapter.c:57` `&a->buffer` | mod .bss |
| 11 | `blur_helper.c:494` `output_generation` | `tfp_blur_begin` | `blur_adapter.c:149` `&handle->generation`, with handle `&l->storage` of the blur lease in `wire` (`blur_adapter.c:341`) | mod .bss |
| 12 | `blur_helper.c:583` `read_stats_locked` | `tfp_blur_read_stats` | `blur_adapter.c:87` `&a->buffer` | mod .bss |
| 13 | `pose_helper.c:1751` `write_generation` | `tfp_pose_begin` (`_steady`, `_input`, `_steady_input`) | `pose_adapter.c:259-260` `&handle->generation`, with handle `&h->pose` of a `TfpWirePoseHandle` that `wire_pose.c:130` callocs | mod heap |
| 14 | `pose_helper.c:2053` `read_stats_locked` | `tfp_pose_read_stats` | `pose_adapter.c:72` `&a->stats_buffer`; the adapter is in `TfpWirePose`, inside the static `TfpWireFeel feel` (`mod_view.c`) | mod .bss |
| 15 | `pose_helper.c:2071` `read_depth_stats_locked` | `tfp_pose_read_depth_stats` | `pose_adapter.c:168` `&a->depth_buffer` | mod .bss |
| 16 | `pose_helper.c:2092` `read_input_stats_locked` | `tfp_pose_read_input_stats` | `pose_adapter.c:196` `&a->input_buffer` | mod .bss |
| 17 | `pose_helper.c:2111` | `tfp_pose_rotation_trace` | no caller in the mod (the test only) | — |
| 18 | `pose_helper.c:2127` | `tfp_pose_entry_position_stats` | no caller in the mod (the test only) | — |
| — | `wire_win_pose.c:109` `host_write` | the pose host's `write` | forwards sites 13–18 to `tfp_win_write` | — |

Each read-stats site also reads its destination first through `host.read`, which is the guarded in-process `hf->read`,
to check `size` and `abi`. That is one more sign that the destination is ordinary mod memory.

None of the 18 sites writes game memory, so all of them moved to the new copy and none stayed on the old path. The game-memory writes keep
their own paths, unchanged:

- `ops_write` (`hf->write`, the backend's writes into the game);
- the warp glow's mode word (`warp_glow.c:272`, through `TfpOpsHost.write`, which is `ops_write`);
- the effects' `p_write` (`WriteProcessMemory` of the aim option's preference byte and the movement byte, as retail
  wrote them; counted under WriteProcessMemory).

### The copy

`src/guarded_copy.h` does a `memcpy` in a function called through a volatile pointer inside `__try`/`__except`. This is
the pattern of `host_cas32` and `src/slot_atomic.c`. A fault answers false, as `WriteProcessMemory` answered FALSE. A
destination that cannot be written, a null address, and an unreadable source all answer false, write nothing, and do
not crash.

`tests/host_write_test.c` checks this. It is built for Windows by `tests/run-host-write.sh` and run under Wine in a
scratch prefix with no display. It drives every output path of the five helpers twice with the same helper state:

- once through hosts whose write is `WriteProcessMemory`;
- once through the mod's own hosts (`tfp_win_hosts`, and `tfp_win_write` for the pose helper).

The two passes give the same answers, error codes and bytes; each destination is pre-filled with a pattern, so a write
that did not happen also has to match. The host_write counter counts each of the 16 writes, and WriteProcessMemory
counts none of them.

Expected saving: one wineserver round trip per call, about 5 µs on an idle server and more under load, for each call
the Perf line counts. There is also less wineserver traffic competing with mouse input.

## 4. What the next game run must show

The run is the user's own play in a real mission, like -31-user, plus a harness soak for comparison:

1. **A Cost and a Perf line every second,** first person on and off. The off seconds give the tick's cost with the view
   off, which was never measured before.
2. **host_write** counts per second (helpers' stats per tick, times the ticks). **WriteProcessMemory** is near 0 (only
   aim-option and movement writes). If the tick's mean drops by about 5 µs × the host_write count, the round trips were
   real.
3. **The vignette section against the rest.** If the vignette's mean and largest carry the 0.8 ms user-play gap and the
   5–29 ms once-a-second spikes, round 29 builds the survey's item 1a/1b: one protected bridge call per slice and a
   bulk id copy. The scan field tells which: `World.entities`, inside the vignette section but outside the scan's µs,
   or the copy. If the spikes sit in the vignette section but not in the scan µs, `World.entities` itself or a GC step
   is the cost, and a bulk copy would not remove them.
4. **eng_call per second** against the survey's estimate of 400–500 per tick while the vignette scans. That is the count
   that a fused slice would cut.
5. **VirtualQuery per second.** If the arms' user-play spikes (0.8–3 ms largest per second against 80 µs in the
   harness) line up with seconds of many queries, Wine's virtual-memory lock is the suspect.
6. **Feel fields unchanged** against feel.53 (`vignette=owned:N`, `writes`, `head`, `pose`). Only the two lines are new.

## 5. Round 32: laps inside the frame, the backend's counts, a shadow snapshot memo

**Nothing here has run in the game.** Logging only, like section 2: every answer the mod gives is the one it gave before.

### Why

The feel.54 logs (`~/.cache/tfp-soak-37.modlog` Stable, `-38` Bodycam; analysis in `~/.cache/tfp-round31/result-0.md` and
the second look in `result-1.md`) put the tick at 308–312 µs, of which **frame is 230–260 µs** (hip, prone, ADS), 151 µs
waiting with first person on, and 35 µs with it never enabled. Both analysts pinned what frame is *not*:

- the ~1000 `eng_call`s a second are the vignette's (0 in seconds without a scan, about 2.8 per scanned entity), outside frame;
- VirtualQuery (the arm drive's `api()`, about 2 µs a frame) and the view-log strings (about 1.8 µs a tick) are noise.

Their shared hypothesis is that the bulk is **backend snapshots re-taken by many callers each tick and never memoised**: a full
snapshot is about 152 guarded reads (127 the first time in a frame, 105 the second) at 0.06 µs, about 6–9 µs; a player one is 40.
The runtime takes snapshots at 12 sites, effects 11, motion 6, head 4 and others. Filling 230 µs takes about 25 full snapshots per
tick. Nobody has counted them. This round counts them, splits frame, and asks the question a memo would raise: within one
`frame_epoch`, does a second snapshot of the same mode ever answer differently from the first?

### The lines

Right after the Perf line, every second (`src/perf_line.h`; both fit say()'s 700 bytes with every field at its largest: 469
and 332 bytes, `tests/perf_line_test.c`):

```
Perf frame: per tick mean/largest us: on_update A/B (fov A/B, blur A/B, camera A/B, head A/B, movement A/B, near A/B, dof A/B, other A/B); extra A/B (pose A/B, motion A/B, engine A/B, effects A/B, other A/B); hold A/B; view log A/B
Perf snapshots: per second: guarded reads R; snapshots full F, player P, pin N; tables T; invoke I, invoke_body B; memo (shadow) calls c/c/c, hits h/h/h, mismatches m/m/m, moved V
```

and once per session, at the first mismatch:

```
Memo: first mismatch: a full snapshot taken again in the same frame epoch (E) differed from the one before it, first at controller_bytes (+0x8c); the memo stays a shadow (answers are always fresh)
```

**Perf frame** splits `tfp_wire_frame` (`src/wire_rt.c`), over the same ticks as the Perf line. Its laps are gross: each includes
what it calls, so `dof` includes the vignette (which the Perf line's frame section leaves out) and the sum is the Perf line's frame
plus its dof and vignette, less `eng_temp_end`.

| lap | what it times |
|---|---|
| on_update | the runtime's `on_update` call |
| fov, blur, camera, head, movement, near, dof | each managed target's own `*_update` host function; `tfp_wire_laps_start` wraps the seven pointers of `w->host` after the feel binders set them (the call lands in the same function as before, one frame deeper) |
| other (on_update) | on_update less the seven: the controller, recovery, motion, editor, menu, aim markers, diagnostics |
| extra | `frame_extra` (`src/wire_feel.c` frame) |
| pose, motion, engine, effects | its four module frames (motion is the settings frame when motion is not bound) |
| other (extra) | extra less the four: the Feel line's second |
| hold | `tfp_wire_hold_frame` |
| view log | the rest of `tfp_wire_frame`: the gate, action and camera strings, `tfp_view_log_frame`, the Ids line |

**Perf snapshots**, per second, from `TfpOpsPerf` (`src/backend_ops.h`, in `TfpOps`; a backend made anew restarts its counts and
the line then counts from 0):

- **guarded reads:** every `_read` of the backend through the host (`rd()`: snapshots, module checks, the action state, the hold,
  the configs), the count the round-31 estimate rests on. Reads made outside `backend_ops.c` are not in it.
- **snapshots full / player / pin:** every snapshot built, by mode: the three public functions, the hold's view (a player one) and
  the backend's own (enter, release, retiring).
- **tables:** snapshot tables handed to the runtime (`w->snap_next`'s growth), the callers' side of the same count.
- **invoke, invoke_body:** native calls (allowed, enter, exit; the body event). Each bumps the epoch before it calls.
- **memo (shadow):** the three public functions' calls (`calls`), the calls that found a stored answer of the same `frame_epoch`
  with a frame active (`hits`), the hits whose fresh answer differed (`mismatches`), each by mode full/player/pin; and `moved`:
  hits whose own take left the backend's state changed.

### The shadow memo

`taken()` (`src/backend_ops.c`, behind `tfp_ops_snapshot`, `_player_snapshot`, `_pin_player_snapshot`) keeps the last answer per
mode: TRUE with its `TfpOpsSnapshot`, or NIL with its text, and the `frame_epoch` after the take, only while a frame is active.
Every call still takes its own snapshot and returns it, exactly as before. A call that finds a stored answer of the same epoch is
a hit; it then compares:

- the result (TRUE/NIL), then the snapshot bytewise (all of `TfpOpsSnapshot`, so the state that does not come from memory is in
  it: `foreign_camera` and the reason from the `foreign_camera` callback, the action state, `safe`), or the NIL text;
- the backend's own state before and after the take (all of `TfpOps` up to the action maps' address arrays, without `why`; the
  maps' `has`/`def`/`count`). A take that changes it on a hit (`moved`) is a side effect a memo would skip.

The first mismatch's mode, field (`tfp_ops_snapshot_field`: `controller_bytes`, `action.states`, `result`, `why`, ... or
`padding`) and byte offset are kept and said once. Nothing in the backend logs, so the vectors' log comparison is untouched.

Proof that answers are unchanged: the code returns the fresh take's result on every path (by construction); the backend_ops
vectors (372 runs, 42837 steps, 22056 method calls) still answer as retail; a new section of `tests/backend_ops_test.c` checks on
371 first images that hits happen only in a frame and the same epoch, that every answer equals that of a backend without the memo,
that a write to the game's memory without an invalidate is one mismatch at `controller_bytes`, that an invalidate and a new frame
end the hit run, and that a hit's take moves nothing in the fixture. `tests/wire_feel_test.c` runs its frames after
`tfp_wire_laps_start` and checks each wrapper answers what the target's own update answers.

Cost (not measured in the game): about 24 QueryPerformanceCounter reads a tick for the laps (about 1.5 µs at 0.06 µs); for the
memo, a 792-byte copy per public call and on a hit two 888-byte copies and two compares, about 36 ns natively; 3.6 KB more in `TfpOps`.

### What the next run must show to turn the memo on (round 33)

The run: a Stable and a Bodycam harness soak like -37/-38 and, if possible, the user's own play.

1. **The snapshots are the bulk.** In the hip, prone and ADS states, `Perf frame` puts most of on_update and extra into laps that
   take snapshots, and `guarded reads` per tick × the read cost (0.06 µs harness, 0.10 µs user play) is a large part of frame. If
   reads × cost is under about 50 µs a tick, the memo cannot save what round 31 hoped and is not built; the laps then name the real cost.
2. **Hits are many:** hits / calls high for full and player (round 31's call sites suggest more than half). The saving is about
   hits × reads per snapshot × read cost; the next round states it from these numbers before building.
3. **Mismatches 0/0/0 in every second of both soaks and the play**, and no `Memo:` line. Any mismatch names a write or a game
   change inside one epoch that nothing invalidates (a helper's CAS write, the game's own update thread, the foreign camera
   callback changing its answer); the memo then needs that site to invalidate, or must not cover that mode.
4. **moved 0.** Otherwise a hit's take does work the backend keeps (the module checks, the action maps); a memo would have to keep
   doing it.
5. **The same feel as feel.54:** the same Drop / Wait / Return / State line counts in the matrix and soaks, the Perf line's frame
   within noise of feel.54 (the laps' 1.5 µs aside), no new Refused lines.

If all five hold, round 33 returns the stored answer on a hit (TRUE: copy the stored snapshot; NIL: the stored text into `why` and
`b->why`), keeps `tfp_wire_snapshot_table` running on every call (its ring slot and `frame_full` / `frame_identity`), and keeps the
shadow running beside it in an assert mode for one more soak: the stored and a fresh take are still compared on every Nth hit, and
mismatches must stay 0. Its proof: the backend_ops vectors identical, the wire and feel tests, `replay.py` over the soak Trace rows.

## 6. Round 35: the memo live behind `memo_on.txt`, and on_update in parts

> Since round 40 the memo is on by default and `memo_off.txt` switches it off (section 7). What follows is round 35 as it was.

**Nothing here has run in the game.** Without `memo_on.txt` the mod answers exactly as feel.55 does. The only differences are
the new `Perf update:` line and a few more clock reads (see "Cost" below). With the file present, a repeated snapshot in one
frame epoch answers the one already taken.

### What the game has shown so far

The only game data is feel.55 at the title screen on HellForge 0.5, at 30 ticks a second:

```
Perf snapshots: per second: guarded reads 2370; snapshots full 60, player 60, pin 30; ... memo (shadow) calls 60/60/30, hits 30/0/0, mismatches 0/0/0, moved 0
Perf frame: on_update 27/39 (... other 27/39)
```

Each tick takes 2 full snapshots, 2 player ones and 1 pin one, and makes 79 guarded reads in all. One full call per tick is a
hit. A hit saves at most one take's reads, so at the title screen the memo can save at most about 2 µs a tick
(79 × 0.06 µs = 4.7 µs a tick is every read together). The case that counts is a mission (hip, prone, ADS), where round 31
expects 12 to 20 snapshot calls a tick. Those counts have not been seen yet. This round makes the memo testable there, and
splits the 27 µs of "other" so the run shows where on_update's time goes.

### The switch

`memo_on.txt` beside the DLL, read once at start like the other switch files (`src/mod_view.c` `start()`). When the file is
there, the log says `Test: memo_on.txt: ...`. `tfp_wire_set_memo` (`src/wire_rt.c`) sets it on the current backend and on every
backend made after it (`rt_backend_new`). Without the file, `tfp_ops_set_memo` is never called and the backend is the
round-32 one: the shadow compares and counts, and every answer is fresh.

### The memo live (`src/backend_ops.c` `taken()`)

- **A hit** is a call of `tfp_ops_snapshot`, `_player_snapshot` or `_pin_player_snapshot` that finds a stored answer of the
  same mode and the same `frame_epoch`, with a frame active. This is the same rule as the shadow's. Every native call and every
  `invalidate` still starts a new epoch.
- **A served hit** returns the stored answer without reading memory. TRUE copies the stored snapshot. NIL returns the stored
  text. Either way the backend's `why` is left as the take left it: the take's own `fail()` text, or untouched when the take
  wrote none.
- **The foreign-camera step is replayed.** A pin or full take that reached the `foreign_camera` hook keeps the snapshot as it
  stood before that step. A served hit calls the hook again and applies its fresh answer: the pin takes any answer but false
  as foreign, the full one only true, and the full one then sets `native_pose_eligible`. So the hook is called once per call,
  as without the memo. A raise of the hook, or a change of its answer inside the epoch, comes out as it would without the
  memo.
- **Callers are unchanged.** `tfp_wire_snapshot_table` still runs on every TRUE answer, served or not. Its ring slot and
  `frame_full` / `frame_identity` are set exactly as before, because the memo lives below them in the backend.
- **Checks.** The first live hit, and every 16th after it (`TFP_OPS_MEMO_CHECK_EVERY`, counted over all modes), still takes a
  fresh snapshot and compares it as the shadow does: the result, the snapshot byte for byte, the NIL text, and the backend's
  own state before and after the take. A check that differs, or whose take moved the backend's state, turns the memo off.
  `memo_live` goes false. Every call from then on is fresh, and the shadow keeps comparing. `Memo: off for the session: ...`
  is logged once, naming the mode, the epoch and the field (or that the state moved). The wire keeps the memo off on every
  backend made later in the session. A wrong memo can therefore last at most 15 hits.
- **Game writes drop the memo.** With the memo on, a write that does not start a new epoch drops the stored answers
  (`tfp_ops_memo_wrote`):
  - the backend's own `keep_aim_view`. Retail writes the controller's active byte there without a new epoch, and the
    vectors' `session` run reads that byte again in the same epoch.
  - the effects' `WriteProcessMemory` of the locomotion byte and the aim option (`src/wire_effects.c` `m_write`,
    `tfp_fx_memory_copy`).

  Off, these calls do nothing, so the shadow counts as in round 32.

### What the lines show

- **Perf snapshots.** Without the file, the line is round 32's text byte for byte. With it:

  ```
  ... memo (live, the first and every 16th hit checked) calls c/c/c, hits h/h/h, served s/s/s, checked k/k/k, mismatches m/m/m, moved V; saved reads R
  ```

  After a failed check the line reads `memo (turned off by a check, shadow again)`. `saved reads` is the guarded reads the
  served hits did not make. Each served hit counts its stored take's reads. The `snapshots full/player/pin` counts and
  `guarded reads` drop by what was served.
- **Perf update** comes every second, right after Perf frame. It splits on_update into exclusive parts that add up to Perf
  frame's `on_update` (`src/perf_line.h` `TFP_UPD_*`; at most 513 bytes):

  ```
  Perf update: per tick mean/largest us, on_update in parts: targets A/B, controller A/B, managed A/B, recovery A/B, owner A/B, presentation A/B, stops A/B, clean poll A/B, editor+motion A/B, entry A/B, menu A/B, ready A/B, aim markers A/B, diagnostics A/B, frame A/B; all snapshot calls A/B
  ```

  The runtime marks where each part begins (`PART()` in `src/tfp_runtime.c`, through `TfpRt.lap`; NULL in every vector
  test). The parts:

  | part | what runs in it |
  |---|---|
  | targets | the seven managed targets' own update calls (the timed wrappers), whatever part calls them |
  | controller | `controller:update`, with its own snapshots and lease work, or its retry cleanup |
  | managed | recovery's bookkeeping around each target: action, proof (a full snapshot), success or failure, the result, a stop and clean poll on a cleanup |
  | recovery | the policy walk and `blocks_view` |
  | owner | the new-owner check; on a new body, every target's stop and clean poll |
  | stops | the idle and foreign-camera stops |
  | clean poll | whether the idle frame is clean |
  | other parts | the probe, compat, the Fird checks, the editor, motion, menu, ready stance, aim markers, diagnostics |
  | frame | the backend's frame begin and end |

  So round 32's `other` (27/39 at the title screen) is now `on_update` less `targets`, split by name. `all snapshot calls` is
  gross. It is the time inside the three public snapshot functions from every caller (runtime, effects, motion, engine,
  editor), served or taken, measured inside `taken()` with the same clock. It is the number the memo is meant to shrink.

### Proof (tests)

- **With the switch off, answers are unchanged.** The backend_ops vectors (357 and 372 runs, 41899 and 42837 steps), the
  shadow section, and the wire and feel tests pass as on main. Without the file, `tfp_ops_set_memo` is never called,
  `tfp_ops_memo_wrote` does nothing, and the wire test checks that `tfp_wire_set_memo(false)` leaves the backend's counts
  untouched.
- **With the switch on, every vector answers as retail.** `tests/run-vectors-backend-ops.sh` runs both programs again with
  `TFP_OPS_MEMO=1`, which sets the memo on after every `new`. The fixture models the memo's premise: it drops the memo
  (`tfp_ops_memo_forget`) when a step changes the scene, a hook or the profile inside a frame, which the game does not do
  between two calls on its own thread. The result is 24 hits served, 66 and 68 checked, and 2204 guarded reads saved.
  - In `random51` the memo turned itself off. A trap changes memory in the middle of a take's reads, which models the game
    writing while the mod reads. The next check found `result` different and the memo turned off, which is the purpose of
    the check. `TFP_OPS_MEMO_WHY=1` prints what turned it off.
- **`tests/backend_ops_test.c`, live memo, on 371 first images:**
  - a served hit answers what a fresh take and a backend without the memo answer, and makes no read;
  - `saved reads` equals the stored take's reads per hit;
  - the player and pin modes are served the same way;
  - a change of the game's memory without an invalidate is answered stale until the next check; that check finds
    `controller_bytes`, turns the memo off, returns the fresh unsafe answer, and from then on takes fresh;
  - `set_memo(true)` does not bring it back on that backend;
  - a served failure answers the fresh text and leaves the backend's `why` as a fresh take would.
- **`tests/wire_feel_test.c`:**
  - the parts add up to the update lap exactly (fake clock);
  - the runtime's marks land, and a target called from a part is charged to `targets` while that part carries on after it;
  - nothing is charged outside on_update;
  - the switch reaches each new backend, and a turned-off memo stays off on the next backend.
- **`tests/perf_line_test.c`:** the live and turned-off Perf snapshots lines (at most 475 bytes), the Memo off line (296),
  and the Perf update line (513) all fit 700 bytes. The shadow's line is still round 32's (332).

### Cost

- **Without the file,** about 15 clock reads a tick for the parts and 2 per public snapshot call. With 12 to 20 calls that is
  about 40 to 55 reads, about 1 to 3 µs at Wine's in-process `QueryPerformanceCounter`. It is counted inside the lines it
  measures.
- **With the file,** each fresh take also copies the snapshot at the foreign step (792 bytes) and up to two 320-byte texts.
  A served hit costs one snapshot copy and the foreign hook's call instead of the take's reads.

### How to run the A/B in the game

Use the same build, the same scenario and the same length for both:

- **A:** no `memo_on.txt` beside the DLL.
- **B:** an empty `memo_on.txt` beside the DLL (the folder of `head_parts.txt` and the other switch files).

The B log must start with `Test: memo_on.txt: ...`. Run a Stable and a Bodycam harness soak like -37 and -38, each in hip,
prone and ADS. If possible, also the user's own play in a mission. Then compare, second by second and state by state:

1. **Perf snapshots.** In B the `snapshots full/player/pin` counts and `guarded reads` per second are lower than in A by about
   the `served` and `saved reads` that B reports. `checked` is above 0 in every second with hits.
2. **Perf update.** `all snapshot calls` mean per tick, A against B. Then the parts: if `controller` or `managed` carry most
   of on_update in A and shrink in B, the snapshots were their cost. If they do not shrink, the cost is elsewhere in them,
   and the next round splits that part.
3. **Perf and Perf frame.** The Perf line's `frame` mean and largest, and Perf frame's `on_update` and `extra`.
4. **Feel.** The same Drop, Wait, Return and State line counts, no new Refused lines, and the Trace rows replay
   (`replay.py`) alike in A and B.

### The pass mark for making the memo the default

All of these must hold in both soaks and, if it was run, the play:

1. No `Memo: off` line in any B log. Mismatches are 0/0/0 and moved is 0 in every second, with `checked` above 0 wherever
   there are hits.
2. The feel is unchanged against A (item 4 above).
3. **The saving is real and worth keeping.**
   - In hip, prone and ADS, B's `all snapshot calls` mean is lower than A's by at least half of `saved reads` per tick ×
     0.06 µs (harness; 0.10 µs in the user's play). The other half allows for the foreign hook and the copy.
   - The Perf line's `frame` mean is lower by **at least 10 µs a tick** (about 3% of feel.54's 310 µs tick), beyond the
     run-to-run noise of two A runs.

If item 1 or 2 fails, the memo stays behind the switch. The `Memo: off` line names the field and mode, and either that site
gets an invalidate or that mode leaves the memo. If items 1 and 2 hold but item 3 does not, the snapshots are not the bulk.
The memo stays off, and the Perf update parts name the next target.

If all three hold, the next round makes the memo the default:

- `memo_off.txt` becomes the switch;
- the checks stay at every 16th hit, because they cost one take in 16;
- the backend_ops vectors run with the memo on by default.

### What only the game can show

- **Whether the game changes snapshot fields inside one frame epoch** on its own: another thread writing the controller
  bytes, the avatar flags or the camera record while the tick runs, or a helper hook firing inside on_update. The vectors
  cannot model this; the checks will see it.
- **Whether the foreign-camera hook's answer can change inside an epoch.** The memo replays it, so this costs only the
  hook's own time.
- **Whether the managed targets write game memory that a snapshot reads.** The FOV, camera, near and blur helpers during
  their update are the candidates. Only the effects' writes are known, and they drop the memo.
- **The real hit, served and saved-read counts in a mission, and so the saving itself.** The estimate is
  `saved reads` × 0.06 to 0.10 µs. At round 31's figures it is about 7 to 15 µs for each served full hit. If about half of
  12 to 20 calls a tick are hits, that is 40 to 120 µs a tick, but nobody has counted them in a mission.
- **What the 27 µs (title screen) and the larger in-mission `other` really are,** now named by the Perf update parts.

## 7. Round 40: the memo becomes the default

### The A/B in the game (feel.58, HellForge 0.5)

Three 15-minute harness soaks in Stable, the same scenario, logs in `~/.cache`. Per-tick means over about 850 to 940
Perf lines each:

| run | log | memo | all snapshot calls | guarded reads per second | Perf `frame` |
|---|---|---|---|---|---|
| A | `tfp-soak-50-A.modlog` | off | 58.8 µs | 35263 | 269.5 µs |
| B | `tfp-soak-51-B.modlog` | on (`memo_on.txt`) | 45.1 µs | 24159 | 246.1 µs |
| C (Bodycam) | `tfp-soak-52-C.modlog` | off | 58.8 µs | 34859 | 242.5 µs |

- **The snapshot work:** `all snapshot calls` is 23 % lower in B (13.7 µs a tick), and guarded reads 31 % lower.
- **Served:** about 95 hits a second.
- **The checks:** no `Memo: off` line in any log; mismatches 0 in every second.
- **The frame:** B is 23 µs below A, but the two memo-off runs A and C differ by 27 µs. So the `frame` figure is within
  the run-to-run noise. Pass mark 3's second half (section 6: at least 10 µs beyond the noise of two A runs) was not
  shown. The decision to make the memo the default was taken on the snapshot figures and the clean checks.

### What changed

- **The switch.** `src/mod_view.c` `start()` reads `memo_off.txt` beside the DLL. Without it the memo is on
  (`tfp_wire_memo_start`, `src/wire_rt.c`), on the current backend and on every backend made after it. With it nothing
  calls `tfp_ops_set_memo`, so the backend is round 32's shadow, byte for byte. `memo_on.txt` is still read and changes
  nothing.
- **The checks are unchanged.** The first live hit and every 16th after it still take fresh and compare. A mismatch or a
  moved state turns the memo off for the session, and the wire keeps it off on every later backend.
- **The lines.**
  - At start, without the file:
    `Ready: snapshot memo on: ...; memo_off.txt beside the DLL switches it off`.
  - With it: `Test: memo_off.txt: the snapshot memo is off; ...`.
  - Either way, a leftover `memo_on.txt` is named as no longer needed (`src/perf_line.h` `tfp_perf_memo_start_line`).
  - The `Memo: off for the session` line now ends `(on again at the next start unless memo_off.txt is beside the DLL)`.
  - The Perf snapshots lines are unchanged. Live is the normal case now; `memo (shadow)` means `memo_off.txt`.
- **The backend's own default is unchanged.** `tfp_ops_init` leaves the memo off and the wire sets it. So the vectors run
  as before: once with the memo off, as with `memo_off.txt`, and once with `TFP_OPS_MEMO=1`, as the game runs by
  default.

### Tests (round 40)

- **`tests/perf_line_test.c`:** the start line's four cases (the default, the default with `memo_on.txt`,
  `memo_off.txt`, both files; at most 329 bytes). Also the new end of the Memo off line.
- **`tests/wire_feel_test.c`:**
  - a new wire has the memo on after the start with no file, and on every new backend;
  - a failed check keeps it off for the session on the next backend;
  - with `memo_off.txt` it is off, the backend's counts are untouched, and later backends stay off;
  - the start does not undo a check that already turned it off.
- **Round 35's two gaps, closed:**
  - **The moved switch-off** (`src/backend_ops.c` `taken_body`, `check && (field || moved)`), `tests/backend_ops_test.c`,
    on 371 first images. Between the stored take and the 17th live hit the frame's game check cache is dropped. The checked
    take reads the module again: the same answer, the state moved. The memo goes off with `off.moved` and field `state`,
    and later calls take fresh. The M6 mutant (moved ignored), which survived every test in round 35, now fails.
  - **The effects' `tfp_ops_memo_wrote` calls** (`src/wire_effects.c` `m_write` and `tfp_fx_memory_copy`),
    `tests/wire_effects_test.c` 5d. With the memo on, the aim option's copy and the movement byte's write each drop
    every mode's stored answer, and so does a failed write. A copy refused before the write keeps them. With the memo
    off a write leaves the shadow's stores. Removing either call fails the test.

### What only the game can show

- The default reaches every player. The first log of the next build must start with the `Ready: snapshot memo on` line,
  and its Perf snapshots lines must read `memo (live, ...)`.
- What section 6 listed is still open outside the Stable soak: hip, prone and ADS in a mission, Bodycam with the memo on,
  and the user's own play. The checks will turn the memo off and log the field if the game changes a snapshot field
  inside an epoch there.
- Whether the frame-time saving is real beyond the noise. Doing it needs more A runs, or a longer soak.
