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
