/* The retail camera helper (native/first_person/native_ads_camera.cpp with
 * native_ads_camera.h at 0.5.41-smooth.66) in C11 for build 25480438.
 * Behaviour is retail's; what differs is listed in
 * docs/translation/CAMERA-HELPER-TRANSLATION.md. No Windows calls: everything the retail
 * file took from Windows or from its NADSC_FIXTURE seams comes through
 * TfpCameraHost. */
#ifndef TFP_CAMERA_HELPER_H
#define TFP_CAMERA_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- native_ads_camera.h: the structures, same fields, order and sizes ---- */
#define TFP_CAMERA_ENTRY_TRACE_ROWS 4
typedef struct TfpCameraEntryTraceRow {
  uint32_t frame, linked, rig_known, padding;
  float argument[3], camera_world[3], camera_local[3], rig_world[3], head_world[3], padding2;
} TfpCameraEntryTraceRow;
typedef struct TfpCameraEntryTraceData {
  uint32_t size, count, exit_holds, hold_exit;
  TfpCameraEntryTraceRow rows[TFP_CAMERA_ENTRY_TRACE_ROWS];
} TfpCameraEntryTraceData;
typedef struct TfpCameraConfig {
  uint32_t size, abi;
  uint64_t game, exe, context, camera, avatar_owner;
  uint32_t avatar_index, avatar, body, camera_unit;
  /* Horizontal view clearance/world-up metres; head damping is root-local. */
  float forward, up, smoothing_ms, blend_ms;
  uint32_t max_gap_ms, reserved;
} TfpCameraConfig;
typedef struct TfpCameraStats {
  uint32_t size, abi, installed, armed, error, reserved;
  uint64_t seen, forwarded, replaced, native6_forwarded, resets;
  float blend, head_distance, last_dt, padding;
} TfpCameraStats;
typedef struct TfpCameraHandoffData {
  uint32_t size, rotation_proven;
  float entry_ms, rotation_ms;
  uint64_t entry_started, entry_frames, entry_rejected, rotation_started, rotation_frames, rotation_rejected, proof_mismatches;
  uint64_t native_mismatches;
  uint32_t native_proven, entry_last_delay, entry_max_delay, padding;
  uint32_t sample_reasons[8];
  uint32_t pending_paths[4];
  float rotation_start_gap, padding2;
} TfpCameraHandoffData;
typedef struct TfpCameraAnchorData {
  uint32_t size, enabled, known, frames;
  float current[3], padding;
} TfpCameraAnchorData;
_Static_assert(sizeof(TfpCameraEntryTraceRow) == 80 &&
               sizeof(TfpCameraEntryTraceData) == 16 + 80 * TFP_CAMERA_ENTRY_TRACE_ROWS, "entry trace ABI");
_Static_assert(sizeof(TfpCameraConfig) == 88, "config ABI");
_Static_assert(sizeof(TfpCameraStats) == 80, "stats ABI");
_Static_assert(sizeof(TfpCameraHandoffData) == 152, "handoff data ABI");
_Static_assert(sizeof(TfpCameraAnchorData) == 32, "anchor data layout");

/* Error codes, retail's numbers (cpp:24-27). */
enum {
  TFP_CAMERA_OK = 0, TFP_CAMERA_INVALID_CONFIG = 1, TFP_CAMERA_BUSY = 2, TFP_CAMERA_READ_FAILED = 3,
  TFP_CAMERA_IMAGE_MISMATCH = 4, TFP_CAMERA_CODE_MISMATCH = 5, TFP_CAMERA_API_MISMATCH = 6,
  TFP_CAMERA_OWNER_MISMATCH = 7, TFP_CAMERA_UNIT_MISMATCH = 8, TFP_CAMERA_PIN_FAILED = 9,
  TFP_CAMERA_SLOT_NOT_WRITABLE = 10, TFP_CAMERA_CAS_MISMATCH = 11, TFP_CAMERA_CAS_FAULT = 12,
  TFP_CAMERA_CLEANUP_PENDING = 13, TFP_CAMERA_POSE_INVALID = 14, TFP_CAMERA_SPACE_INVALID = 15,
  TFP_CAMERA_GRAPH_MISMATCH = 16, TFP_CAMERA_TOKEN_MISMATCH = 17, TFP_CAMERA_ON_FOOT_REQUIRED = 18
};

/* ---- the host: what retail took from Windows ---- */
typedef void (*TfpCameraSetter)(void *camera, uint32_t unit, const float *native);
enum { TFP_CAMERA_LOCK_CONTROL = 0, TFP_CAMERA_LOCK_POLICY = 1 };
typedef struct TfpCameraHost {
  void *user;
  /* The value the helper puts in the dispatch slot and recognises there as its
   * own (retail: the address of setter_hook). In the game: the wrapper that
   * calls tfp_camera_setter_from with its own return address. */
  uint64_t hook;
  /* ReadProcessMemory on this process: all `length` bytes copied, or false.
   * The helper has already bounded `at` and `length`. */
  bool (*read)(void *user, uint64_t at, void *out, size_t length);
  /* Asked only after a failed read inside an owner proof: is the whole span
   * committed and readable now (retail readable_span, VirtualQuery over at
   * most four regions, none past 0x800000000000). */
  bool (*readable_span)(void *user, uint64_t at, size_t length);
  /* Milliseconds as a double (retail now(): QueryPerformanceCounter). */
  double (*now)(void *user);
  /* `at` (already known 8-aligned) is 8 committed, writable bytes of the image
   * loaded at `exe` (retail writable_slot after its alignment test). */
  bool (*writable_slot)(void *user, uint64_t at, uint64_t exe);
  /* `at` is committed, executable code in a mapped image (retail executable). */
  bool (*executable)(void *user, uint64_t at);
  /* InterlockedCompareExchangePointer on the 8-byte slot at `slot`: store
   * `desired` when it holds `expected`; *observed = the value found. False
   * only when the operation itself faulted (retail's __except). */
  bool (*cas)(void *user, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed);
  /* WriteProcessMemory on this process: all `length` bytes written, or false. */
  bool (*write)(void *user, void *out, const void *data, size_t length);
  /* GetModuleHandleExW(FROM_ADDRESS|PIN) for the module holding `address`. */
  bool (*pin)(void *user, uint64_t address);
  /* The two SRW locks, by TFP_CAMERA_LOCK_*. */
  void (*lock_exclusive)(void *user, int lock);
  void (*unlock_exclusive)(void *user, int lock);
  void (*lock_shared)(void *user, int lock);
  void (*unlock_shared)(void *user, int lock);
  /* Optional. NULL: the original setter is called directly, as retail does.
   * Set: the helper hands the call over (a test whose "original" is not real
   * code at exe+setter). Must call `original`'s behaviour exactly once. */
  void (*forward)(void *user, uint64_t original, void *camera, uint32_t unit, const float *native);
} TfpCameraHost;

/* Supply the host once, before any other call. The struct is copied. */
void tfp_camera_set_host(const TfpCameraHost *host);
/* All static state back to its start-up values (the host is kept). */
void tfp_camera_reset_for_tests(void);

/* The hook. `return_address` is the caller's return address (retail:
 * _ReturnAddress()); the game build's wrapper supplies it. */
void tfp_camera_setter_from(uint64_t return_address, void *camera, uint32_t unit, const float *native);

/* ---- exports: retail's NADSC_* with the same signatures and results ---- */
uint32_t tfp_camera_validate(const TfpCameraConfig *config);
/* 0: refused, generation 0; 1: armed; 2: the slot could not be restored. */
uint32_t tfp_camera_begin(const TfpCameraConfig *config, uint64_t *generation);
uint32_t tfp_camera_end(uint64_t generation);
uint32_t tfp_camera_read_stats(uint64_t generation, TfpCameraStats *stats);
uint32_t tfp_camera_error(void);
uint32_t tfp_camera_set_presentation(uint64_t generation);
uint32_t tfp_camera_set_handoff(uint64_t generation, float entry_ms, float rotation_ms);
uint32_t tfp_camera_handoff_stats(uint64_t generation, TfpCameraHandoffData *out);
uint32_t tfp_camera_set_cut_hold(uint64_t generation, uint32_t mode);
uint32_t tfp_camera_entry_trace(TfpCameraEntryTraceData *out);
uint32_t tfp_camera_set_weapon_key(uint64_t generation, uint32_t key);
uint32_t tfp_camera_anchor_stats(TfpCameraAnchorData *out);
/* Beyond retail (docs/design/ACTION-CAMERA.md), for what build 25480438 does differently. Process-wide, set once
 * by the binder; the defaults (0, 300, false) are retail's behaviour exactly.
 *   extra_head_mask  one more action mask the head camera accepts beside 0, 2 and 0x20 (0: none)
 *   eye_idle_ms      how long the held eye tolerates mask 0 around a swap (retail: 300)
 *   eye_return       after the eye let go, glide back to the incoming weapon's remembered eye while it is drawn */
void tfp_camera_set_action_tuning(uint32_t extra_head_mask, uint32_t eye_idle_ms, bool eye_return);
/* Beyond retail: the camera lock. While it is on and the view is presented, the head camera is kept through every
 * action mask, off-foot state and camera kind of the player's own avatar (a death camera included), without the
 * glide in from the game's camera; and the view's rotation takes a kick from the head's own movement
 * (src/camera_lock.h): `gain`/`max_degrees` on foot, the `thrown_` pair in any other state. Off: retail's behaviour. */
void tfp_camera_set_lock(bool on, float gain, float max_degrees, float settle_ms, float thrown_gain, float thrown_max_degrees);
/* missed: frames of the presented view that were neither the native view nor the head camera (0xffff: the
 * camera record was not ours), with the kind, the helper's error and the proof that failed (TFP_CAMERA_WHY_*) of
 * the last one. off_foot, ragdoll, head_distance: the body on the last head-camera frame the lock held (not in the
 * on-foot animation state; the avatar's ragdoll flag; metres from the unit root to the head). */
typedef struct TfpCameraLockStats { uint64_t frames, foreign_frames, missed; uint32_t last_kind, missed_kind, missed_error; float peak_kick_degrees;
  uint32_t missed_why, off_foot, ragdoll; float head_distance; } TfpCameraLockStats;
/* Counts since the start; the peak is the largest kick since the last call. */
void tfp_camera_lock_stats(TfpCameraLockStats *out);
/* Which proof of the head camera refused a frame (beside the error number, which retail shares between several). */
enum {
  TFP_CAMERA_WHY_NONE = 0,
  TFP_CAMERA_WHY_GRAPH,      /* 16: the body's node graph is not the captured one (count, tables, head or neck name) */
  TFP_CAMERA_WHY_WORLDS,     /* 16: the body's world matrices could not be read, or the root or head is not finite */
  TFP_CAMERA_WHY_RANGE,      /* 16: the head is not 0.05 to 3 m from the unit root (and the lock is not holding) */
  TFP_CAMERA_WHY_NECK,       /* 16: the head is beyond 3 m from the root and not 0.005 to 0.5 m from its neck */
  TFP_CAMERA_WHY_ANIMATION,  /* 7: the animation instance, graph, layer array or layer 14 state changed since sampled */
  TFP_CAMERA_WHY_OWNER,      /* 7 or 3: the avatar, body, registry or camera unit is no longer the armed one */
  TFP_CAMERA_WHY_REQUEST,    /* 7: the gate, the dispatch slot or the top camera request changed during the frame */
  TFP_CAMERA_WHY_HEADER,     /* 7: the body's node count or tables changed during the frame */
  TFP_CAMERA_WHY_NAMES,      /* 7: the head or neck name changed during the frame */
  TFP_CAMERA_WHY_PARENT      /* 7: the camera got a parent during the frame */
};
const char *tfp_camera_why_text(uint32_t why);
/* Beyond retail, lock frames only: the body is "down" when physics has it: the head is beyond walking range of the
 * unit root (the root stays where the character fell or died) or the body is off foot with the avatar's ragdoll
 * flag set. The head camera then stays at the head (a head beyond 3 m from the root is taken when it is 0.005 to
 * 0.5 m from its neck, the parent node the graph proof names), the direction is the last first-person one with
 * the thrown kick on it, and eases back to the game's when the body is up again. Nothing is kept between frames
 * but that direction: the body is proven again in every frame.
 * frames: head-camera frames shown while down; refused: frames of the game's own camera while down (or within a
 * second after), with the kind, error and proof of the last; head: the head node in the world on the last frame. */
typedef struct TfpCameraDownStats { uint64_t frames, refused; uint32_t kind, error, why, ragdoll, off_foot; float head[3], head_distance; } TfpCameraDownStats;
void tfp_camera_down_stats(TfpCameraDownStats *out);
/* The body reference whose head the lock held the camera at, off foot, within the last 250 ms; 0 otherwise. What
 * head hiding asks in place of the on-foot answer (src/head_visibility.h held_off_foot). */
uint32_t tfp_camera_lock_off_foot_body(void);
uint64_t tfp_camera_lock_frames(void); /* frames the lock has held so far: rising while the view is shown */
/* Frames the view was shown so far (the head camera or the native view, or any camera the lock holds), with the
 * camera lock on or off. What the field-of-view lock follows. */
uint64_t tfp_camera_view_frames(void);
/* Beyond retail, and only in frames the camera lock holds: the presented eye is kept continuous in the body's frame
 * across every change of camera (head camera, native view, the repeated native argument) and one-frame moves of the
 * first-person rig around an action (tfp_camera_continuity_event); the difference is carried as a correction and
 * eased out (src/camera_continuity.h). While it is on, the lock's eased entry into the native view does not run:
 * the entry is one of those changes of camera. In the
 * native view the argument is parent-local and is converted through the rig eye's world transform. `trace_frames`:
 * every presented frame is kept in a ring for tfp_camera_trace_read. Both off: nothing is reached or read. */
void tfp_camera_set_continuity(bool on, bool trace_frames);
/* Something that can re-seat the first-person rig just happened (a lease or a hold begun or ended). For the next
 * 300 ms of native frames a one-frame move of the rig eye is taken up; the helper opens the same window by itself
 * when the control mask, the top request's kind or weapon, or the weapon key changes. Outside those windows the rig
 * eye's motion is the game's own and is shown as it is; a change of camera is always taken up. */
void tfp_camera_continuity_event(void);
/* Beyond retail, and only in frames the camera lock holds. `on`: the game still presents the first-person request
 * (kind 6) although its exit has been asked for and the gun has left the first-person rig (a stim keeps the request
 * for about 0.85 s). The rig's eye then sits at the body's head; while this is on, a native frame presents the head
 * camera's eye instead (the head, `forward` along the level view direction, `up`), as the head source of the
 * continuity, so the way there and the head camera's first frame afterwards have no step. Not in full aim; a frame
 * whose head or rig eye cannot be proven is presented as any other native frame and counted as refused. Off (the
 * default) and without the lock: nothing is reached or read. */
void tfp_camera_set_orphan(bool on);
/* Counts since the start; behind_m: on the last such frame, how far behind the presented point the game's own eye
 * was, along the level view direction. */
typedef struct TfpCameraOrphanStats { uint64_t frames, refused; float behind_m; } TfpCameraOrphanStats;
void tfp_camera_orphan_stats(TfpCameraOrphanStats *out);
typedef struct TfpCameraContinuityStats { uint64_t jumps, edges, restarts; float largest_m; } TfpCameraContinuityStats;
/* Counts since the start; the largest correction carried since the last call. */
void tfp_camera_continuity_stats(TfpCameraContinuityStats *out);
/* The trace: a State edge was just written. The presented frames of the next 500 ms, and the six before it that
 * were not yet handed out, are given to tfp_camera_trace_read (oldest first, each once). `ms` counts from the edge.
 * source: 1 head camera, 2 native view, 3 the last native argument repeated. */
typedef struct TfpCameraTraceRow { float ms; uint32_t kind, source; float eye[3], wanted[3], step, correction, entry_ease; } TfpCameraTraceRow;
void tfp_camera_trace_edge(void);
uint32_t tfp_camera_trace_read(TfpCameraTraceRow *rows, uint32_t capacity);
#endif
