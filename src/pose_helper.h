/* The retail editor/pose helper (native/first_person/native_ads_editor.cpp
 * with native_ads_editor.h at 0.5.41-smooth.66) in C11 for build 25480438.
 * Behaviour is retail's; what differs is listed in
 * docs/translation/POSE-HELPER-TRANSLATION.md. No Windows calls: everything the retail
 * file took from Windows, from its assembly thunks or from its NADSE_FIXTURE
 * seams comes through TfpPoseHost. */
#ifndef TFP_POSE_HELPER_H
#define TFP_POSE_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gate_log.h"
#include "depth_steady.h"
#include "entity_fields.h"

/* ---- native_ads_editor.h: the structures, same fields, order and sizes ---- */
typedef struct TfpPoseConfig {
  uint32_t size, abi;
  uint64_t game, exe, context, request, avatar_owner;
  /* duration_ms: 0 stays armed until End/guard failure; 1..30500 is bounded. */
  uint32_t avatar_index, avatar, weapon, body, viewmodel, duration_ms;
  uint64_t weapon_descriptor;
  uint32_t weapon_unit, blend_ms;
  float offset_x, offset_y, offset_z;
  uint32_t reserved;
} TfpPoseConfig;
typedef struct TfpPoseStats {
  uint32_t size, abi, installed, armed, expired, error;
  /* Legacy ABI field names: modified shared-rig calls and native-baseline restores. */
  uint64_t seen_position, seen_rotation, forwarded_position, forwarded_rotation, weapon_updates, zero_restores;
  float blend, target_weight;
} TfpPoseStats;
typedef struct TfpPoseDepthStats {
  uint32_t size, abi, installed, armed, supported, reason, zoom_index, reserved;
  uint64_t applied, unsupported, cache_refreshes, cache_checks, ads_bypassed;
  float native_distance, reference_distance, correction, zoom, target_fov, current_fov, gain, padding;
} TfpPoseDepthStats;
typedef struct TfpPoseInputStats {
  uint32_t size, abi, installed, active, reason, pending, cache_entries, reserved;
  uint64_t observed, normalized, unsupported, restored, foreign_wins, quarantined;
  float ratio, yaw_gain, pitch_gain, padding;
} TfpPoseInputStats;
/* Entry rotation guard trace. slot 1 = rotation setter, 2 = branch rotation
 * setter. caller is an offset into game.dll, or into the executable with the
 * top bit set, or 0xffffffff. */
#define TFP_POSE_ROTATION_TRACE_ROWS 64
typedef struct TfpPoseRotationTraceRow { uint32_t slot, ms, caller, replaced; float q[4]; } TfpPoseRotationTraceRow;
typedef struct TfpPoseRotationTraceData {
  uint32_t size, count, guarded, enabled;
  TfpPoseRotationTraceRow rows[TFP_POSE_ROTATION_TRACE_ROWS];
} TfpPoseRotationTraceData;
typedef struct TfpPoseEntryPositionData { uint32_t size, enabled, writes, remembered; } TfpPoseEntryPositionData;
_Static_assert(sizeof(TfpPoseConfig) == 104, "NADSE_Config ABI");
_Static_assert(sizeof(TfpPoseStats) == 80, "NADSE_Stats ABI");
_Static_assert(sizeof(TfpPoseDepthStats) == 104, "NADSE_DepthStats ABI");
_Static_assert(sizeof(TfpPoseInputStats) == 96, "NADSE_InputStats ABI");
_Static_assert(sizeof(TfpPoseRotationTraceData) == 16 + 32*TFP_POSE_ROTATION_TRACE_ROWS, "NADSE_RotationTraceData ABI");
_Static_assert(sizeof(TfpPoseEntryPositionData) == 16, "NADSE_EntryPositionData layout");

/* Error codes, retail's numbers (cpp:83-86). */
enum {
  TFP_POSE_OK = 0, TFP_POSE_INVALID_CONFIG = 1, TFP_POSE_BUSY = 2, TFP_POSE_READ_FAILED = 3,
  TFP_POSE_IMAGE_MISMATCH = 4, TFP_POSE_CODE_MISMATCH = 5, TFP_POSE_API_MISMATCH = 6,
  TFP_POSE_OWNER_MISMATCH = 7, TFP_POSE_UNIT_MISMATCH = 8, TFP_POSE_PIN_FAILED = 9,
  TFP_POSE_SLOT_NOT_WRITABLE = 10, TFP_POSE_CAS_MISMATCH = 11, TFP_POSE_CAS_FAULT = 12,
  TFP_POSE_CLEANUP_PENDING = 13, TFP_POSE_SIGHT_NODE_UNAVAILABLE = 14, TFP_POSE_GRAPH_MISMATCH = 15,
  TFP_POSE_POSE_INVALID = 16, TFP_POSE_TOKEN_MISMATCH = 17, TFP_POSE_DEADLINE_EXPIRED = 18
};
/* Input-normalisation reasons in TfpPoseInputStats.reason (cpp:557-559);
 * 0x100 + a TFP_GAIN_* value when the arithmetic refused. */
enum {
  TFP_POSE_INPUT_READY = 0, TFP_POSE_INPUT_DISABLED = 1, TFP_POSE_INPUT_CODE_UNAVAILABLE = 2,
  TFP_POSE_INPUT_SDK_UNAVAILABLE = 3, TFP_POSE_INPUT_OWNER = 4, TFP_POSE_INPUT_SAMPLE = 5, TFP_POSE_INPUT_MATH = 6,
  TFP_POSE_INPUT_POOL = 7, TFP_POSE_INPUT_FOREIGN = 8, TFP_POSE_INPUT_REENTRY = 9, TFP_POSE_INPUT_PENDING = 10,
  TFP_POSE_INPUT_SOURCE_CHANGED = 11, TFP_POSE_INPUT_ADS = 12
};

/* ---- the host: what retail took from Windows and from native_ads_input.S ---- */
typedef void (*TfpPoseSetter)(uint32_t unit, uint32_t node, const float *pose);
typedef const float *(*TfpPoseGetter)(uint32_t unit, uint32_t node);
enum { TFP_POSE_LOCK_CONTROL = 0, TFP_POSE_LOCK_POLICY = 1 };
/* The six values the helper can put in a dispatch slot and recognise there. */
enum {
  TFP_POSE_HOOK_POSITION = 0,        /* UnitAPI+0x38; retail position_hook */
  TFP_POSE_HOOK_ROTATION = 1,        /* UnitAPI+0x48; retail rotation_hook */
  TFP_POSE_HOOK_BRANCH_ROTATION = 2, /* UnitAPI+0x40; retail branch_rotation_hook */
  TFP_POSE_HOOK_GETTER = 3,          /* UnitAPI+0x00 without input mode; retail position_getter_hook */
  TFP_POSE_HOOK_GETTER_INPUT = 4,    /* UnitAPI+0x00 in input mode; retail NADSE_Input00Thunk */
  TFP_POSE_HOOK_INPUT90 = 5,         /* UnitAPI+0x90 in input mode; retail NADSE_Input90Thunk */
  TFP_POSE_HOOK_COUNT = 6
};
typedef struct TfpPoseHost {
  void *user;
  /* By TFP_POSE_HOOK_*: the game build's wrappers, each of which calls the
   * matching tfp_pose_*_from with what the retail hook or thunk captured. */
  uint64_t hooks[TFP_POSE_HOOK_COUNT];
  /* ReadProcessMemory on this process: all `length` bytes copied, or false.
   * The helper has already bounded `at` and `length`. */
  bool (*read)(void *user, uint64_t at, void *out, size_t length);
  /* GetTickCount64: milliseconds. */
  uint64_t (*tick)(void *user);
  /* GetCurrentThreadId. */
  uint32_t (*thread)(void *user);
  /* `at` (already known 8-aligned) is 8 committed, writable bytes of the image
   * loaded at `exe` (retail writable_slot after its alignment test). */
  bool (*writable_slot)(void *user, uint64_t at, uint64_t exe);
  /* `at` is committed, executable code in a mapped image (retail executable). */
  bool (*executable)(void *user, uint64_t at);
  /* InterlockedCompareExchangePointer on the 8-byte slot at `slot`; false only
   * when the operation itself faulted (retail's __except). */
  bool (*cas)(void *user, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed);
  /* WriteProcessMemory on this process: all `length` bytes written, or false. */
  bool (*write)(void *user, void *out, const void *data, size_t length);
  /* GetModuleHandleExW(FROM_ADDRESS|PIN) for the module holding `address`. */
  bool (*pin)(void *user, uint64_t address);
  /* The two SRW locks, by TFP_POSE_LOCK_*. */
  void (*lock_exclusive)(void *user, int lock);
  void (*unlock_exclusive)(void *user, int lock);
  void (*lock_shared)(void *user, int lock);
  void (*unlock_shared)(void *user, int lock);
  /* -- look-gain normalisation only -- */
  /* `at` (already known 4-aligned and in range) is 4 committed bytes with
   * read-write or write-copy protection (the VirtualQuery in input_cas32). */
  bool (*writable32)(void *user, uint64_t at);
  /* NADSE_InputCompareExchange (native_ads_input.S): lock cmpxchg on the 32
   * bits at `at`; *observed = the value found. False only when the write
   * faulted (the assembly's exception handler resumed at its recovery label). */
  bool (*cas32)(void *user, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed);
  /* VirtualQuery(at): the region holding `at`, its base, size and whether it
   * is free. False when the query fails. */
  bool (*region)(void *user, uint64_t at, uint64_t *base, uint64_t *size, bool *is_free);
  /* VirtualAlloc(at, size, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE): the
   * address obtained, or 0. The helper then writes that memory directly. */
  uint64_t (*allocate)(void *user, uint64_t at, uint64_t size);
  /* Optional. NULL: the game's exp at `address` is called directly, as retail
   * does. Set: the helper hands the call over (a test whose game image is not
   * code). Must return what the code at `address` returns for `x`. */
  double (*native_exp)(void *user, uint64_t address, double x);
  /* Optional. NULL: the original setter/getter is called directly, as retail
   * does. Set: the helper hands the call over. `which` is the slot number
   * 0..3 of the pose slots, 4 for UnitAPI+0x90. */
  void (*forward_setter)(void *user, unsigned which, uint64_t original, uint32_t unit, uint32_t node, const float *pose);
  const float *(*forward_getter)(void *user, unsigned which, uint64_t original, uint32_t unit, uint32_t node);
  /* Optional, not retail's (round 20, docs/design/specs/2026-11-23-gate-flip.md "Round 20"). NULL: nothing. Set:
   * called on each frame of the rig's node 5 whose kind-6 lens the held depth read with its proofs (the same
   * evaluation; the evaluator runs before the frame's hooked field-of-view call): the unzoom gate as the evaluator
   * took it (the avatar's packed bit 0, or request+0x8f), the lens target it chose, the lens after its step. The feel
   * build hands it to the field-of-view helper (tfp_fov_note_lens). */
  void (*note_lens)(void *user, bool gated, float target, float lens);
} TfpPoseHost;

/* Supply the host once, before any other call. The struct is copied. */
void tfp_pose_set_host(const TfpPoseHost *host);
/* All static state back to its start-up values (the host is kept). */
void tfp_pose_reset_for_tests(void);

/* ---- the five hook entries. `return_address` is the caller's return address
 * (retail: _ReturnAddress(), or the thunk's saved return address).
 * `native_stack` is the caller's stack pointer at the call: the address of the
 * hook's own return address plus 8 (retail: _AddressOfReturnAddress()+8, or
 * the thunk's entry RSP+8). `request` / `native_request` is the caller's R15
 * (the thunks), 0 for the plain getter hook. ---- */
void tfp_pose_position_from(uint64_t return_address, uint32_t unit, uint32_t node, const float *pose, uint64_t native_stack);
void tfp_pose_rotation_from(uint64_t return_address, uint32_t unit, uint32_t node, const float *pose);
void tfp_pose_branch_rotation_from(uint64_t return_address, uint32_t unit, uint32_t node, const float *pose);
const float *tfp_pose_position_getter_from(uint64_t return_address, uint32_t unit, uint32_t node, uint64_t native_stack, uint64_t native_request);
const float *tfp_pose_input90_from(uint64_t return_address, uint32_t unit, uint32_t node, uint64_t request, uint64_t native_stack);

/* ---- exports: retail's NADSE_* with the same signatures and results ---- */
uint32_t tfp_pose_validate(const TfpPoseConfig *config);
/* Begin: 0 rejected/no owned lease; 1 armed; 2 owned cleanup required. */
uint32_t tfp_pose_begin(const TfpPoseConfig *config, uint64_t *generation);
uint32_t tfp_pose_begin_steady(const TfpPoseConfig *config, uint64_t *generation);
uint32_t tfp_pose_validate_input(const TfpPoseConfig *config);
uint32_t tfp_pose_begin_input(const TfpPoseConfig *config, uint64_t *generation);
uint32_t tfp_pose_begin_steady_input(const TfpPoseConfig *config, uint64_t *generation);
uint32_t tfp_pose_read_input_stats(uint64_t generation, TfpPoseInputStats *stats);
uint32_t tfp_pose_set_placement(uint64_t generation, float x, float y, float z);
uint32_t tfp_pose_set_ads_placement(uint64_t generation, float x, float y, float z);
uint32_t tfp_pose_set_motion(uint64_t generation, float dx, float dy, float dz);
uint32_t tfp_pose_set_transform(uint64_t generation, float dx, float dy, float dz, float qx, float qy, float qz, float qw);
uint32_t tfp_pose_set_authored_transform(uint64_t generation, float dx, float dy, float dz, float qx, float qy, float qz, float qw);
uint32_t tfp_pose_set_zoom_policy(uint64_t generation, uint32_t active);
uint32_t tfp_pose_end(uint64_t generation);
uint32_t tfp_pose_read_stats(uint64_t generation, TfpPoseStats *stats);
uint32_t tfp_pose_read_depth_stats(uint64_t generation, TfpPoseDepthStats *stats);
uint32_t tfp_pose_error(void);
/* Not in retail's helper; read-only, for a live replace of the mod (docs/guides/LIVE-REPLACE.md): 1 while a
 * private preset index is published or waiting, or a private row is in use; 0 when the look-gain
 * normalisation holds nothing in the game. */
uint32_t tfp_pose_input_held(void);
uint32_t tfp_pose_set_transition(uint64_t generation, uint64_t epoch, float progress, uint32_t ads);
uint32_t tfp_pose_rotation_trace(TfpPoseRotationTraceData *out);
uint32_t tfp_pose_set_rotation_guard(uint32_t enabled);
uint32_t tfp_pose_entry_position_stats(TfpPoseEntryPositionData *out);
uint32_t tfp_pose_set_entry_position(uint32_t enabled);
uint32_t tfp_pose_set_entry_animation(uint32_t milliseconds, float drop, float back);
/* ---- the action hold (not in retail; a probe, docs/design/ACTION-CAMERA.md "Action hold") ----
 * Off (the start-up state, and the state for good unless tfp_pose_set_hold is called): retail exactly.
 * On: the ownership test of an armed lease (never the one of an entry) tolerates, for the SAME owned
 * record (the top camera request is still the lease's, kind 6, for the lease's avatar and body, on the
 * same rig, not seated):
 *   - any controller byte other than the aim byte (the action flags and the guard byte),
 *   - a weapon unit that is no longer live, a changed weapon descriptor, and a request that no longer
 *     names the lease's weapon.
 * Returns the previous value. The binder sets it every tick while the switch file is there: on while a
 * lease runs and the controller would hold an action, because the game's first write of an action comes
 * before the tick that sees the action (HellForge runs the tick before the game's update). */
uint32_t tfp_pose_set_hold(uint32_t on);
/* ---- the "Gate:" line and the depth switch (not in retail; docs/design/specs/2026-11-23-gate-flip.md) ----
 * tfp_pose_gate_take: what the helper counted of the game's unzoom gate and of its own zoom-depth term since the last
 * call (src/gate_log.h), for the log; the count begins again. `out` is the caller's own memory. Returns 1, or 0 before
 * the host is set. Counting changes nothing the helper does and reads nothing more from the game.
 * tfp_pose_set_gate_reads(1): the helper also reads the avatar's main row on every frame it writes the rig root (two
 * guarded reads: 0x3e bytes at avatar owner + 0x53d8b0 + index*0x1238 and the float at +0x11ac), for the same line.
 * Off at the start.
 * tfp_pose_set_depth_off(1): the zoom-depth term adds nothing on any frame (reason 11 in the depth statistics), so
 * neither the rig nor its root is shifted for the optic. A test switch (depth_off.txt): off at the start. */
uint32_t tfp_pose_gate_take(TfpGateSecond *out);
uint32_t tfp_pose_set_gate_reads(uint32_t on);
uint32_t tfp_pose_set_depth_off(uint32_t on);
/* The network-id map, the entity records and the player fields where the game's code has them (src/entity_fields.h),
 * for the look gain's steering read (the game's EntityOfNetwork, B_PIN_9, whose pin leaves those displacements out).
 * Call once before the helper runs; 1 taken, 0 refused (not resolved). Without it a shipped build's steering refuses. */
uint32_t tfp_pose_set_entity_fields(const TfpEntityFields *fields);
/* ---- the rig's depth held steady (not in retail; src/depth_steady.h, the same spec, "Round 9") ----
 * tfp_pose_set_depth_steady(mode): 0, the start: the helper as retail has it. 1: on every frame of the rig's node 5
 * with an evaluator frame, the game's own depth blend is worked out from the lens locals and the request's state the
 * zoom depth term already reads (one more guarded read of the request's 0x24 state bytes), and node 5 and the rig's
 * root are moved as a pair, through the term's own guarded writes, so that the depth is the settled distance of the
 * selected optic instead of following the lens target the game's unzoom gate switches. Where it has a number it goes
 * out in place of the zoom depth term's. Weighted by the hip share: nothing in full aim. 2 (depth_steady_off.txt):
 * worked out and counted, nothing added. Returns 1, or 0 for another mode.
 * Three things make it pass a frame beyond the rule's own: one of the zoom depth term's nine code pins is not this
 * build's (the evaluator's locals are then not read as a lens; asked once a lease, as the term does); a shift of more
 * than 1 m; and the root not following: after TFP_STEADY_ALONE frames running in which node 5 was moved and the
 * root's half was not done, or TFP_STEADY_ALONE_LEASE such frames in the lease, nothing more is held until the next
 * lease.
 * tfp_pose_steady_take: what was counted of it since the last call, for the "Steady:" line and the line that says a
 * stop once (tfp_steady_stop_line); the count begins again. Returns 1, or 0 before the host is set. */
uint32_t tfp_pose_set_depth_steady(uint32_t mode);
uint32_t tfp_pose_steady_take(TfpSteadySecond *out);
/* Round 49e: the weapon is a handgun (held in one hand at the hip), for the "Reload rest:" line of a reload's edges
 * (src/reload_hold.h); nothing is done by it. false: the start-up value. The binder sets it each tick. Any thread. */
void tfp_pose_set_sidearm(bool on);
typedef struct TfpPoseHoldProbe {
  uint32_t hold, armed, error;   /* the mode; whether a lease is armed; tfp_pose_error() */
  uint64_t node5_calls;          /* position-setter calls for node 5 of the watched rig, counted from the first tfp_pose_set_hold on */
} TfpPoseHoldProbe;
uint32_t tfp_pose_hold_probe(TfpPoseHoldProbe *out); /* 1; reads only */
/* Not in retail (round 20; src/ads_seat.h): the sight brought closer in full aim. `sight`: the game's near clip of a
 * sight (m; what src/near_helper.c saw), `wanted`: how far from the eye the sight is to be (m; 0 leaves the game's own
 * seat, the start-up value, so the helper is retail's until the binder calls this). Any thread; read in each frame.
 * tfp_pose_ads_probe: what the last frame of node 5 did with it, for the "Aim:" line and the near clip (closer). */
void tfp_pose_set_ads_seat(float sight, float wanted);
/* Not in retail (round 20c; docs/guides/AIM-TEST.md): the test switch zoom_steady.txt. true: the held depth works at
 * full strength in full aim too (src/depth_steady.h tfp_steady_aim) and a frame it passes there keeps the held distance
 * (tfp_steady_carry). false, the start-up value: the hip share weights it, nothing in full aim (feel.51). Either way
 * the frame's lens goes to host.note_lens and the depth in full aim is counted for the "Aim:" line. */
void tfp_pose_set_aim_steady(bool on);
typedef struct TfpPoseAdsProbe {
  bool armed, full_ads;
  float share;        /* the ADS share: 0 at the hip, 1 in full aim */
  float pull;         /* m toward the eye the sight asks for, glided */
  float closer;       /* m the gun is drawn closer than the game seats it: pull * share less the ADS placement's forward */
  float ads_forward;  /* m: the ADS placement's forward (the editor's ADS action) */
  float depth;        /* m: the game's own depth in the last frame of full aim (request+0x90 blended); NAN: not worked out */
  bool aim_steady;    /* round 20c: tfp_pose_set_aim_steady (zoom_steady.txt) */
  bool editing;       /* round 20c: tfp_pose_set_ads_editing */
  float sight, wanted; /* round 20c: what tfp_pose_set_ads_seat was handed last */
  uint32_t aim_in_ms;  /* round 42: what tfp_pose_set_ads_enter_ms was handed last (180 at start-up) */
} TfpPoseAdsProbe;
bool tfp_pose_ads_probe(TfpPoseAdsProbe *out); /* reads only */
/* For the near helper's own calls (tfp_near_set_ads_source): how far in the near clip goes in full aim this frame, `closer`
 * m (the gun's move toward the eye, leading it while it comes in: src/ads_seat.h tfp_ads_lead). 0 unless the player asks
 * for the near clip to follow (round 20c, review 28 B1): a wanted distance above 0 (tfp_pose_set_ads_seat), or the
 * placement editor on its ADS action (tfp_pose_set_ads_editing). false, 0: no lease. Any thread; reads only. */
bool tfp_pose_ads_near(float *closer);
/* Round 20c: the placement editor is on its ADS action (its draft is full aim's ADS placement, drawn live), so the near
 * clip follows the gun as with a wanted distance. false, the start-up value: a saved ADS placement is drawn as on
 * feel.51, the near clip the game's. The binder sets it each tick (src/aim_bind.h). Any thread. */
void tfp_pose_set_ads_editing(bool on);
/* Round 42, "Aim-in time" (locked_view.json ads_transition_ms; not in retail): the shared clock's ADS_ENTER time in ms. The
 * shared phase (tfp_pose_set_transition) already carries it; this is for the frames it does not: 0 takes the hip weight
 * to 0 at once in full aim where no packet matches (blend_ms is the config's, and never 0); below blend_ms (150) that blend
 * into full aim takes the aim-in time; and at 0 the frame the gun comes
 * in all at once gives the near clip no lead (src/ads_seat.h tfp_ads_lead_snap). Any other value: as before (180, the
 * start-up value). Any thread; read in each frame. */
void tfp_pose_set_ads_enter_ms(uint32_t ms);
#endif
