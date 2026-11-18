# Milestone 1, Part A: Foundation (build data, snapshot, session state machine) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A buildable C mod that reads, on build 25480438, exactly the snapshot the retail mode reads on its build, runs retail's session state machine against it, and proves both against the retail Lua itself; plus a read-only probe build that logs the snapshot in a real game session.

**Architecture:** The repository is seeded with the plumbing of the 0.5.34 C port (guarded reads, slot leases, engine bridge, build script, tests). Retail's `profile.lua`, the live half of `fp/backend.lua`, `transition.lua`, `recovery.lua` and `controller.lua` are translated one to one. The retail Lua files are run unmodified under LuaJIT to produce reference outputs that the C must reproduce. Nothing in this part hooks or calls the game.

**Tech Stack:** C11, zig cc (`x86_64-windows-gnu`) for the mod, native `cc` with ASan/UBSan for tests, `/usr/bin/luajit` for reference vectors, Python 3 for tools.

**Spec:** `docs/design/specs/2026-11-16-c-rewrite-design.md`. Behaviour reference: `docs/translation/RETAIL-SMOOTH66-INVENTORY.md` (sections 1.3, 2.2, 4, 6e are the ones this part uses).

Parts B to D of milestone 1 (camera policy; field of view, near and blur policies; visibility, effects and markers) get their own plans after this one.

## Global Constraints

- Retail source is read only: `~/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit/` at commit 1d0d453. Never write there. Reference runs copy nothing out of it except what a tool reads at run time.
- Donor: `~/Documents/Codex/2026-11-07/tr/work/true-first-person-hellforge-dev5`. Copied once in Task 1; never edited in place.
- Build 25480438 image: `~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll` (file offset = RVA).
- Every retail number carried over is a named constant with a comment giving the retail file and line. A value changed for this build is listed in `docs/build-25480438/BUILD-DIFFERENCES.md` with its evidence.
- Error texts of the snapshot are retail's, verbatim (inventory 1.3, "Failure ... assert texts").
- This part installs no hook, swaps no slot and calls no game function. The probe build only reads memory and writes its log.
- No install and no Wine or game run that opens a window without the user's word. No push. No attribution lines. Stage files by name.
- zsh: quote `echo "====="`; write `"${var}:"` before a colon.

## Review Focus

1. A structure offset that is right on retail and wrong here by a few bytes (the avatar pool stride is 0x1240 there and 0x1238 here): the snapshot must fail with a retail error text, never return a plausible wrong player. (Task 4 tests `wrong_stride_is_refused`, `descriptor_mismatch_is_refused`.)
2. Memory that becomes unreadable mid-snapshot (level load, respawn): every read is guarded; the snapshot returns an error, the state machine treats it as retail does. (Task 4 `unreadable_mid_snapshot`; Task 6 scripted case `snapshot_error_while_running`.)
3. The reference runner silently testing a stub instead of the retail file: each vector file records the retail file's SHA-256 and git commit, and the C test refuses vectors cut from another commit. (Task 5 `vectors_name_their_source`.)
4. A game build other than 25480438, or code another mod has changed at a pinned site: the profile check refuses with the pin's name. (Task 2 `wrong_build_is_refused`, `changed_pin_is_named`.)
5. Probe log volume in a long session: one line per change, at most 10 a second, a count of what was held back. (Task 7 `probe_rate_limit`.)

---

### Task 1: Seed the repository and prove the donor builds

**Files:**
- Create: `src/`, `tests/`, `tools/`, `build.sh`, `mod/hellforge-mod.json`, `LICENSE`, `NOTICE.md` copied from the donor; `docs/history/PROVENANCE.md`; `.gitignore` (`dist/`, `work/`, `__pycache__/`, `*.pdb`)
- Create: `src/smooth_meter.c`, `src/smooth_meter.h`, `src/request_log.c`, `src/request_log.h`, `tools/smooth_report.py`, and their tests, copied from `~/Projects/HellForge/Backports/true-first-person-controller` at branch `stage1a-aim`

- [ ] **Step 1:** Copy the donor's `src`, `tests`, `tools`, `build.sh`, `mod`, `LICENSE`, `NOTICE.md`. Do not copy `dist`, `work`, `docs`. Copy the donor's `docs/research/` to `docs/history/research-0.5.34/` (it holds this build's function contracts).
- [ ] **Step 2:** Write `docs/history/PROVENANCE.md`: donor path and the SHA-256 of each copied source file; the controller commit the meter came from; one paragraph stating that the donor's policy code (`camera_policy.c`, `pose_policy.c`, `fov_policy.c`, `near_policy.c`, `blur_policy.c`, `lifecycle.c`, `runtime.c`, `visibility_policy.c`, `models.c`, `effects.c`, `markers*.c`, `motion.c`, `placement.c`, `action.c`) is retail 0.5.34 behaviour awaiting replacement and is not trusted, while `backend.c` reads, `dispatch_lease.c`, `slot_*.c`, `engine.c`, `native_call.c`, `settings.c`, `ui.c`, `mod.c` are plumbing to be re-read against retail before use.
- [ ] **Step 3: Run** `bash build.sh` — Expected: `Built C11 True First Person development candidate`. **Run** `bash tests/run-native.sh` — Expected: exit 0. If either fails, find out why before changing anything (superpowers:systematic-debugging) and record it in `docs/history/PROVENANCE.md`.
- [ ] **Step 4:** Add the meter and recorder tests to `tests/run-native.sh`; run it — Expected: exit 0 with the `Smoothness meter:` and `Request recorder:` lines.
- [ ] **Step 5: Commit** everything by name — "Seed from the 0.5.34 C port's plumbing and the session log tools".

### Task 2: Build profile for 25480438 with verified pins

**Files:**
- Create: `src/profile25480438.h` (replaces the donor's `profile.h` for new code; the donor header stays until its last user is replaced)
- Create: `tools/verify_profile.py`, `tests/test_verify_profile.py`
- Create: `docs/build-25480438/BUILD-DATA-25480438.md`

**Interfaces:**
- Produces:

```c
typedef struct TfpPin { const char *name; uint32_t rva; const char *hex; } TfpPin;   /* hex with ?? wildcards */
typedef struct TfpProfile {
  uint32_t game_timestamp, game_image_size, exe_timestamp, exe_image_size;
  struct { uint32_t camera, controller, world, player, avatar, wield, weapons, inventory, state, api_root; } globals;
  struct { uint32_t enter, constructor, exit, allowed, evaluator, controller_enter, controller_exit,
           controller_update; } functions;
  struct { uint32_t unit_registry; } exe;
  const TfpPin *pins; unsigned pin_count;
} TfpProfile;
extern const TfpProfile tfp_profile;
```

- Values already established (each still to be confirmed against the image in Step 2): enter wrapper 0x127ddb0, constructor 0x127de20, exit 0x127e9d0, allowed 0x12b2d20, evaluator 0x1286330, controller enter 0x12b29b0, exit 0x12b2ae0, update 0x12b26a0, controller aim gate 0x12b28a3, input desired gate 0xa41c0b, allow-first-person field 0x12b2dfb; globals camera context `game+0x346d560`, controller `game+0x347ce70`, world `game+0x346bf98`, player `game+0x3326468`, avatar pool `game+0x3326d20`, wield `game+0x3326420`, inventory `game+0x3326738`, API root `game+0x3326308`. To be found: `weapons` (candidate `game+0x3326ce0`), `state` (candidate `game+0x3326340`, scene field candidate `+0xac21c`), the exe unit registry (the controller's `FPC_NATIVE_REGISTRY`), the three call sites `root_position_call`, `root_rotation_call`, `pose_branch_position_call` (retail 0x111a809, 0x111a829, 0x111a460; here near 0x127a11a to 0x127a14e and inside the evaluator), the PE timestamps and image sizes.
- `python3 tools/verify_profile.py --image <game image> [--header src/profile25480438.h]`: parses the pins from the header, checks each against the image, prints one line per pin (`ok name rva` or `MISMATCH name rva at byte N`), exit 0 only if all match.
- `docs/build-25480438/BUILD-DATA-25480438.md`: one table row per value in inventory section 4.2 that the live path uses: retail value, value here, how it was established (image disassembly with address, or the controller's `src/native_view.c` line that reads the same field and was run in the game on 2026-11-16), and `confirmed` or `candidate`.

- [ ] **Step 1: Write the failing test** `tests/test_verify_profile.py` (unittest): against a 4 KiB synthetic image and a temporary header: `all_pins_match` (exit 0), `wrong_build_is_refused` (one byte changed in a pin → exit 1, the line names the pin and the byte index), `changed_pin_is_named`, `wildcards_are_skipped` (`??` positions never mismatch), `pin_outside_image_is_refused`.
- [ ] **Step 2:** Establish each value. For every retail pin, locate the same code in this build's image (masked search as in the 2026-11-16 check, then disassemble with `objdump -D -b binary -m i386:x86-64`) and write the pin with wildcards at relocated displacement bytes only. For the three call sites, disassemble the evaluator's callee that writes the camera unit's local position and rotation and take the matching `call qword ptr [r?+0x38]` / `[+0x48]` sites; record the surrounding instructions in the build-data document.
- [ ] **Step 3: Implement** `tools/verify_profile.py` and `src/profile25480438.h`.
- [ ] **Step 4: Run** `python3 -m unittest tests/test_verify_profile.py` — Expected: OK. **Run** `python3 tools/verify_profile.py --image "~/Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll"` — Expected: every pin `ok`, exit 0.
- [ ] **Step 5: Commit** — "Build profile for 25480438: addresses and pins verified against the image".

### Task 3: Memory interface and a scene generator for both builds

**Files:**
- Create: `src/memory.h` (reader interface), `tests/scene_image.py` (generator), `tests/test_scene_image.py`

**Interfaces:**
- Produces:

```c
typedef bool (*TfpRead)(void *user, uint64_t address, void *out, size_t size); /* false: unreadable */
typedef struct TfpMemory { TfpRead read; void *user; uint64_t game, exe; } TfpMemory;
```

- `tests/scene_image.py`: `build(layout, scene) -> dict[address, bytes]` where `layout` is `RETAIL` (offsets of `fp/backend.lua`, inventory 1.3) or `HF` (offsets of `docs/build-25480438/BUILD-DATA-25480438.md`) and `scene` is a plain description: local player entity, avatar, body ref, rig ref, weapon entity and unit, request queue rows (kind, avatar, weapon, body), controller bytes, the 24 flag bytes, hash-map parameters. It writes the same logical scene in either layout, and can dump it as a flat file of `(address, length, bytes)` records that both the LuaJIT runner and the C tests load.
- Scenes defined once in `tests/scenes.py` and used by Tasks 4 and 6: `hip_rifle` (kind 4, safe), `rig_view` (kind 6 ours), `aiming_ads` (controller active), `swap_masked` (mask 2), `stim` (mask 64), `vault` (mask 0x20), `guard` (byte 5 set), `seated`, `foreign_request` (another avatar's request on top), `no_weapon`, `respawned` (new body ref and descriptor), `empty_queue` (fallback record used).

- [ ] **Step 1: Write the failing test** `tests/test_scene_image.py`: the two layouts of `hip_rifle` differ in addresses but decode to the same logical values through a tiny Python reader that follows each layout; the dump round-trips; overlapping records are refused.
- [ ] **Step 2: Run** it — Expected: FAIL (module missing).
- [ ] **Step 3: Implement** the generator and scenes.
- [ ] **Step 4: Run** — Expected: OK.
- [ ] **Step 5: Commit** — "One scene description, two build layouts: test images for the snapshot".

### Task 4: The snapshot

**Files:**
- Create: `src/snapshot.h`, `src/snapshot.c` (translation of `fp/backend.lua` lines 10 to 290 and `_alignment_context`, `_descriptor`, `_map`, `_top`; inventory 1.3)
- Test: `tests/snapshot_test.c`; add to `tests/run-native.sh`

**Interfaces:**
- Consumes: `TfpMemory` (Task 3), `tfp_profile` (Task 2), structure offsets from `docs/build-25480438/BUILD-DATA-25480438.md` as named constants in `src/layout25480438.h`.
- Produces:

```c
typedef struct TfpSnapshot {            /* field names and meanings are retail's (inventory 1.3) */
  uint64_t base, context, camera, record, world, controller, avatar_owner, flags_address;
  uint32_t head, tail, camera_kind, request_avatar, request_weapon;
  uint8_t controller_bytes[6], flags[24], avatar_descriptor[20], weapon_descriptor[20];
  uint32_t avatar, avatar_index, body, viewmodel, weapon, weapon_unit;
  uint64_t weapon_component_descriptor;
  bool controller_active, blocked, request_local, aiming, shoulder_aim, seated, reloading,
       safe, engine_safe, weapon_transition, head_camera_transition, weapon_ready;
  char player_identity[256], identity[384];
} TfpSnapshot;
typedef enum { TFP_SNAPSHOT_PLAYER, TFP_SNAPSHOT_FULL } TfpSnapshotScope;
/* true and *out filled, or false with retail's error text in error. */
bool tfp_snapshot(const TfpMemory *memory, TfpSnapshotScope scope, TfpSnapshot *out, char *error, size_t error_size);
```

- Not in this task: `action_state`, `on_foot`, `foreign_camera` (the full snapshot's last overrides). `safe` here is retail's player-level `safe`; `engine_safe` equals it. Those three arrive with Part B, and the function documents that.

- [ ] **Step 1: Write the failing tests** `tests/snapshot_test.c`, loading the `HF` dumps of Task 3: one test per scene asserting every field (`hip_rifle`: kind 4, `safe`, weapon and unit ids, `identity` contains the weapon unit; `rig_view`: kind 6, record equals the newest ring slot; `aiming_ads`: `controller_active`, `aiming`; `swap_masked`: `blocked`, `weapon_transition`, `!safe`; `stim`: `blocked`; `vault`: `head_camera_transition`; `guard`: `blocked`; `seated`: `seated`, `!safe`; `foreign_request`: false with "Camera is not owned by local player"; `no_weapon`: player scope true, full scope false with "Equip a firearm before starting the camera test"; `respawned`: `player_identity` differs from `hip_rifle`'s; `empty_queue`: record is the fallback). Then: `wrong_stride_is_refused` (the image built with the avatar stride 0x1240 → false with "Avatar descriptor changed" or "Local avatar retired", never true); `descriptor_mismatch_is_refused`; `unreadable_mid_snapshot` (the reader fails after N reads, for every N up to the total: always false with a retail text, `*out` untouched); `rig_view_other_weapon` ("ADS request belongs to another weapon"); `rig_view_other_body` ("ADS request body changed").
- [ ] **Step 2: Run** `bash tests/run-native.sh` — Expected: build failure, `snapshot.h` not found.
- [ ] **Step 3: Implement** `src/layout25480438.h` and `src/snapshot.c`. Read order and checks follow `fp/backend.lua` line by line; each check carries its retail line in a comment.
- [ ] **Step 4: Run** — Expected: exit 0, `Snapshot:` summary line.
- [ ] **Step 5: Reference run.** Write `tools/retail_ref/run_snapshot.lua` (LuaJIT): loads the retail `fp/backend.lua` unmodified through a shim that supplies `mod.require`, `compat51` and the backend's `io` fixture hook reading a `RETAIL` dump; prints the snapshot fields as JSON. Add `tests/test_snapshot_parity.py`: for every scene, the retail JSON on the `RETAIL` dump and `work/tests/snapshot_dump` (a small C program printing the same JSON on the `HF` dump) agree on every logical field (addresses are compared through the generator's address map; identities by their component values). **Run** — Expected: OK for all scenes. A scene the retail file cannot run under the shim is listed in the test with the reason, not skipped silently.
- [ ] **Step 6: Commit** — "Snapshot: retail's player and weapon snapshot on build 25480438, checked against the retail Lua".

### Task 5: Reference-vector runner; transition and recovery

**Files:**
- Create: `tools/retail_ref/shim.lua`, `tools/retail_ref/gen_vectors.lua`, `tools/retail_ref/gen.py` (writes `tests/vectors/*.json`), `tests/vectors/README.md`
- Create: `src/transition.h`, `src/transition.c`, `src/recovery.h`, `src/recovery.c`; tests `tests/transition_test.c`, `tests/recovery_test.c`, `tests/vectors_test.py`

**Interfaces:**
- `python3 tools/retail_ref/gen.py --retail "<kit folder>" --out tests/vectors`: runs `/usr/bin/luajit tools/retail_ref/gen_vectors.lua`, which loads retail `transition.lua` and `recovery.lua` unmodified and replays scripts (the retail fixtures' own sequences from `tests/first_person/`, read at run time, plus the extra cases below), writing inputs and outputs. Each JSON carries `retail_commit` (must be 1d0d453…) and the SHA-256 of each retail file used.
- Produces (names are retail's):

```c
typedef struct TfpTransitionPacket { uint64_t epoch; double progress; bool ads, merged, continuous; char reason[32]; } TfpTransitionPacket;
void tfp_transition_init(TfpTransition *t, double duration /*0.18*/, double max_gap /*0.25*/);
void tfp_transition_update(TfpTransition *t, const TfpSnapshot *s, double now, bool enabled, bool blocked, TfpTransitionPacket *out);
void tfp_transition_action(TfpTransition *t, const char *action, double now);
void tfp_transition_resume(TfpTransition *t, const char *identity, const char *player_identity, double now);

typedef enum { TFP_RECOVERY_WAIT, TFP_RECOVERY_UPDATE, TFP_RECOVERY_CLEANUP } TfpRecoveryAction;
/* classify / failure / cleanup / action / revalidate / success as in fp/recovery.lua; backoff 0.25, 0.5, 1 s */
```

- [ ] **Step 1: Write the failing tests.** `tests/vectors_test.py::vectors_name_their_source` (a vector file with another commit or a wrong file hash makes the C test program exit 2 with a message). `transition_test.c` and `recovery_test.c` read their JSON vectors and require every output field to match (numbers within 1e-9, strings exactly). Extra scripted cases beyond the retail fixtures: aim pressed and released inside one blend; an action arriving at progress 0.5; a gap longer than `max_gap`; `resume` with a different player identity; three failures of one policy in a row, then success; a failure during cleanup.
- [ ] **Step 2: Run** — Expected: FAIL (no vectors, no sources).
- [ ] **Step 3: Implement** the runner, generate the vectors, then implement `transition.c` and `recovery.c`.
- [ ] **Step 4: Run** `bash tests/run-native.sh` and `python3 -m unittest tests/vectors_test.py` — Expected: exit 0 / OK.
- [ ] **Step 5: Commit** vectors, runner and sources — "Transition clock and recovery ledger, matched to the retail Lua's output".

### Task 6: The session state machine

**Files:**
- Create: `src/controller.h`, `src/controller.c` (translation of `fp/controller.lua`, manual branch; inventory 2.2 and section 2.3 "controller"); `src/backend_api.h` (the calls the controller makes on the backend, as a table of function pointers)
- Create: `tools/retail_ref/gen_controller.lua`; test `tests/controller_test.c`

**Interfaces:**
- Consumes: `TfpSnapshot`, transition packet.
- Produces:

```c
typedef struct TfpBackendApi {           /* one entry per backend method the manual branch calls */
  bool (*snapshot)(void *user, TfpSnapshot *out, char *error, size_t n);
  bool (*player_snapshot)(void *user, TfpSnapshot *out, char *error, size_t n);
  bool (*camera_only_weapon)(void *user, const TfpSnapshot *s);
  bool (*prepare_pose)(void *user, const TfpSnapshot *expected, TfpLease *out, char *error, size_t n);
  bool (*enter_pose)(void *user, const TfpSnapshot *expected, TfpLease *lease, uint32_t remaining_ms, bool blend_entry, char *error, size_t n);
  bool (*pose_status)(void *user, TfpLease *lease, char *error, size_t n);
  bool (*release)(void *user, TfpLease *lease, char *error, size_t n);
  bool (*set_pose_transition)(void *user, TfpLease *lease, const TfpTransitionPacket *packet);
  void *user;
} TfpBackendApi;
typedef enum { TFP_PHASE_IDLE, TFP_PHASE_WAITING, TFP_PHASE_RUNNING, TFP_PHASE_CLEANUP_PENDING } TfpPhase;
void tfp_controller_init(TfpController *c, const TfpBackendApi *backend, double (*clock)(void *), void *clock_user);
bool tfp_controller_start(TfpController *c, char *error, size_t n);     /* F11 on */
void tfp_controller_stop(TfpController *c);                              /* F11 off */
void tfp_controller_suspend(TfpController *c, const char *why);
void tfp_controller_update(TfpController *c);
bool tfp_controller_retry_cleanup(TfpController *c);
TfpPhase tfp_controller_phase(const TfpController *c);
```

- Constants, retail's: first entry attempt 0.1 s after start (`controller.lua:210`), attempts at least 0.25 s apart (`:456`), `GUARD_HOLD` 0.75 s (`:19`), never an entry in the same update as a release, a blended re-entry (150 ms) after an ADS return on the same weapon and a steady entry otherwise, no lease for a camera-only weapon.
- The timed branch (menu test mode `native`) is not translated.

- [ ] **Step 1: Write the failing test.** `tools/retail_ref/gen_controller.lua` runs the retail `controller.lua` unmodified against a scripted backend and clock and records, per update, the phase and every backend call with its arguments, for these scripts: `start_enters_after_100ms`; `entry_attempts_250ms_apart` (snapshot unsafe, then safe); `swap` (running → mask 2 → release → waiting → safe → steady entry); `ads_and_back` (controller active → released → blended re-entry on the same weapon); `guard_holds_750ms`; `no_entry_in_release_update`; `camera_only_weapon_never_leases`; `stop_while_running` (cleanup, then idle); `cleanup_pending_retries` (release fails twice); `snapshot_error_while_running`; `respawn_while_running` (player identity changes); `suspend_and_resume`; `seated`; `stim_mask_64`; `aim_held_at_entry` (no entry while aiming). `tests/controller_test.c` replays the same scripts against `controller.c` with a C scripted backend and requires the same phase and call sequence, update by update.
- [ ] **Step 2: Run** — Expected: FAIL.
- [ ] **Step 3: Implement** `controller.c`.
- [ ] **Step 4: Run** `bash tests/run-native.sh` — Expected: exit 0, `Controller:` summary line naming the number of scripts and updates compared.
- [ ] **Step 5: Commit** — "Session state machine, matched update for update to the retail controller".

### Task 7: Read-only probe build

**Files:**
- Create: `src/probe_mod.c` (a second mod entry: tick only), `manifests/probe/hellforge-mod.json` (id `goose.true-first-person-probe`, version `0.6.0-probe.1`), `build.sh` target `--probe`, `docs/guides/LIVE-PROBE.md`
- Test: `tests/probe_log_test.c`

**Interfaces:**
- On every game-thread tick the probe takes the full snapshot through guarded reads and writes a line when any logical field changed, at most 10 lines a second (`held=N` counts what was left out): `Snapshot: kind=6 safe=1 active=0 mask=0 guard=0 aim=0 shoulder=0 seated=0 weapon=912 unit=4203232 rig=4203186 body=4203180` or `Snapshot: error=<retail text>`. Once at start: the result of every profile pin (`Pin: ok name` / `Pin: MISMATCH name`) read from the running game, and the PE timestamps and sizes. It also writes the `Request:` lines of the request recorder.
- No hook, no slot swap, no game call, no key handling.

- [ ] **Step 1: Write the failing test** `tests/probe_log_test.c`: the line formatter on three snapshots (exact strings), `probe_rate_limit` (50 changes in one second → 10 lines, the next line carries `held=40`), error line format.
- [ ] **Step 2: Run** — Expected: FAIL.
- [ ] **Step 3: Implement**; build with `bash build.sh --probe`.
- [ ] **Step 4: Run** `bash tests/run-native.sh` — Expected: exit 0. Check the DLL's imports with `objdump -p` — Expected: only CRT, KERNEL32 and USER32 names; no hook-related SDK use (`grep -c hook src/probe_mod.c` is 0).
- [ ] **Step 5:** Write `docs/guides/LIVE-PROBE.md`: what the probe does and does not do; install is a folder copy into `mods/`, on the user's word, with the game closed; any ordinary play session will do (draw a weapon, aim, swap, use a stim); what a good log looks like (every pin `ok`; `kind=4 safe=1` at the hip in third person; `mask=2` during a swap; `aim=1` and, with the controller mod on, `kind=6`).
- [ ] **Step 6: Commit**, then stop and report: what is proved offline, what only the probe session can prove (the structure offsets marked `candidate` in the build-data document), and ask for the user's word to install the probe.
