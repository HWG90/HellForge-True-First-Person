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
 * (src/camera_lock.h): `gain`/`max_degrees` on foot, the `thrown_` pair in any other state. `damping_ms` is the
 * camera damping option as a time constant (src/camera_damp.h tfp_camera_damping_ms; 0: none): the kick is low-passed
 * with it, and so is the head camera's eye in the ordinary camera's frames of a body on foot (not in the native view,
 * not in full aim, not on a body that is down or off foot). `bodycam` is the camera style (src/camera_damp.h
 * TfpCameraStyle). True: all of the above as given. False, the Stable camera: no kick on foot, half of `thrown_gain`
 * low-passed on 250 ms in any other state, and the head camera's eye follows the head on 350 ms with up to 0.25 m of
 * lag; `gain`, `max_degrees` and `damping_ms` are then not used. Before the first call, and after a reset: true.
 * Off: retail's behaviour. */
void tfp_camera_set_lock(bool on, float gain, float max_degrees, float settle_ms, float thrown_gain, float thrown_max_degrees, float damping_ms, bool bodycam);
/* missed: frames of the presented view that were neither the native view nor the head camera (0xffff: the
 * camera record was not ours), with the kind, the helper's error and the proof that failed (TFP_CAMERA_WHY_*) of
 * the last one. off_foot, ragdoll, head_distance: the body on the last head-camera frame the lock held (not in the
 * on-foot animation state; the avatar's ragdoll flag; metres from the unit root to the head). missed_apart: for
 * TFP_CAMERA_WHY_SPACE, metres between the camera node's local and world position (0: not read). */
typedef struct TfpCameraLockStats { uint64_t frames, foreign_frames, missed; uint32_t last_kind, missed_kind, missed_error; float peak_kick_degrees;
  uint32_t missed_why, off_foot, ragdoll; float head_distance, missed_apart; } TfpCameraLockStats;
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
  TFP_CAMERA_WHY_PARENT,     /* 7: the camera got a parent during the frame */
  TFP_CAMERA_WHY_LINKED,     /* 15: the ordinary camera's request, the camera linked to a parent, no native view to repeat */
  TFP_CAMERA_WHY_SPACE       /* 15: no parent, but the camera's local and world position are over 0.01 m apart or unread */
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
/* Round 44: the body whose eye the swap's hold kept, glided or let go in a head-camera frame of the lock within the
 * last 250 ms, or 0. Show torso waits for it (src/wire_engine.h tfp_wire_engine_set_eye_held): the eye the hold keeps
 * is the native view's, in the body's neck and back. */
#define TFP_CAMERA_EYE_HELD 1
uint32_t tfp_camera_lock_eye_held_body(void);
/* Round 44: keep the head camera's eye in front of the head in frames of the lock with head visibility active
 * (src/camera_clear.h: the push and the floor). The binder turns it on (src/mod_view.c; off with clear_off.txt).
 * Off (the default; tfp_camera_reset_for_tests sets it off): nothing of it runs, the veil's frames are round 32's. */
void tfp_camera_set_clear(bool on);
uint64_t tfp_camera_lock_frames(void); /* frames the lock has held so far: rising while the view is shown */
/* Round 38 (src/camera_vehicle.h): under the lock, a frame of a body that is seated, off foot and not in physics (an
 * exosuit's or a vehicle's seat) stays first person. The eye is fixed in the body root's frame (the head's offset,
 * frozen once it has settled, plus the configured forward step along the root's level forward and the up step); the
 * direction is the game's own on that frame, with no ease and no kick; no veil. Entering glides from the last view
 * shown to the seat's eye and the game's direction; leaving glides from the seat's eye to the head camera's. Seat
 * frames count as held (tfp_camera_lock_frames) and as the shown view.
 * frames: seat frames shown; refused: seat frames whose proof failed (the game's own frame was shown); glided: seat
 * frames the entry glide wrote; entries, exits: seats begun and left; cuts: entries with no glide (nothing shown in
 * the last 250 ms to start from, or over 8 m away). Of the last entry: the camera kind, the glide's length (0: a cut)
 * and the way it had to go (-1: nothing to start from); of the last exit: the same (0 ms: none). seated: the last
 * frame was a seat; settled: the eye's anchor is frozen; gliding: 0 none, 1 into the seat, 2 out of it.
 * offset: the shown eye from the root (right, forward, up), m. hands: bit 0 the body's left hand node was read, bit 1
 * the right; left, right: each from the shown eye along the root's axes, m (what the diver's own hands do in the
 * cockpit). */
typedef struct TfpCameraSeatStats {
  uint64_t frames, refused, glided, entries, exits, cuts;
  uint32_t in_kind, in_ms, out_ms, seated, settled, gliding, hands;
  float in_metres, out_metres, offset[3], left[3], right[3];
} TfpCameraSeatStats;
void tfp_camera_seat_stats(TfpCameraSeatStats *out);
/* The body reference the lock shows from a seat, or is gliding out of one from, within the last 250 ms; 0
 * otherwise. What head hiding hides the whole diver for (src/head_visibility.h TfpHeadIo.seat). */
uint32_t tfp_camera_lock_seat_body(void);
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
/* Round 24: the native view was left because its request or firearm changed (a backpack picked up: the gun leaves the
 * hands and the game re-requests the native view). The next change of camera into the native view on mask 0, within
 * 2 s, may hold the eye until the rig has settled (THE SETTLE, src/camera_continuity.h). The binder calls it. */
void tfp_camera_settle_arm(void);
/* Round 24: THE SETTLE itself, a test switch (settle_on.txt beside the DLL); off by default and after
 * tfp_camera_reset_for_tests. Off, an armed entry is presented as without it. */
void tfp_camera_set_settle(bool on);
/* Beyond retail, and only in frames the camera lock holds. `on`: the game still presents the first-person request
 * (kind 6) although its exit has been asked for and the gun has left the first-person rig (a stim keeps the request
 * for about 0.85 s). The rig's eye then sits at the body's head; while this is on, a native frame presents the head
 * camera's eye instead (the head, `forward` along the level view direction, `up`), as the head source of the
 * continuity, so the way there and the head camera's first frame afterwards have no step. Not in full aim; a frame
 * whose head or rig eye cannot be proven is presented as any other native frame and counted as refused. Off (the
 * default) and without the lock: nothing is reached or read. */
void tfp_camera_set_orphan(bool on);
/* Beyond retail, round 49: the binder says each tick whether the body is in a dive (the motion runtime's action,
 * TFP_ACT_DIVE: from the launch to the landing, where it becomes prone). A dive is "on foot" to the layer the lock
 * reads, so the Stable style's eye damping ran through it: with the continuity's carry on top the eye stood 0.36 to
 * 0.38 m above the rig eye when the native view came back, and floated down after the landing (soak 60: 0.355 m).
 * While this is on, in the Stable style, the head camera's eye is not damped and a lag carried is let go on 40 ms, as
 * in a clamber or a vault (src/camera_traversal.h). Bodycam is as before. The Trace rows say dive=1 while it is on.
 * Off (the default) and without the lock: nothing changes. */
void tfp_camera_set_dive(bool on);
/* Counts since the start; behind_m: on the last such frame, how far behind the presented point the game's own eye
 * was, along the level view direction. */
typedef struct TfpCameraOrphanStats { uint64_t frames, refused; float behind_m; } TfpCameraOrphanStats;
void tfp_camera_orphan_stats(TfpCameraOrphanStats *out);
/* Beyond retail, round 14 (docs/design/specs/2026-11-24-helmet-in-view.md), and only in frames the camera lock holds:
 * the veil. At every leave of the native view the game switches the body's meshes back on in its own update, after
 * this mod's tick (HellForge's tick runs before the game's update), and head visibility switches them off on the next
 * tick: the frame between is drawn with the body in it (soak 27: 137 of 138 leaves). Its eye is the one the swap holds
 * (the outgoing weapon's), 0.22 m behind the head with the LAS-5 Scythe: the helmet fills the view. The veil opens at
 * the first frame of the lock that is not the native view after one that was, and closes at the tick whose head
 * visibility update found the body drawn again and switched all of it off (or at the second tick, or when switched
 * off). That is two frames a leave, about 66 ms: in every leave soak 27 traced (131) the first head-camera frame is
 * drawn before the tick that sees the leave, the body still hidden, and the frame with the body comes after that
 * tick. The first is veiled too: from the hook it cannot be told from the first frame of a leave whose tick came
 * first, which would be the frame with the body. While it is open, a head-camera frame of a body on foot whose eye
 * is less than TFP_CAMERA_VEIL_CLEAR in front of the head, along the level view direction, is drawn with the eye
 * moved forward to that distance; height, side and rotation stay. Only the position handed to the game in that frame
 * changes: the eye the swap holds and glides, the continuity, the damping and the trace row are those of the eye
 * before it.
 * set_veil: each tick: the view is on and head visibility active, whatever rows of the head it owns: until the first
 * leave of a session it owns none, and that leave draws the head as every other does (soak 27, 12:41:45.163 and
 * 12:41:50.071). false: no veil opens, an open one closes.
 * veil_tick: each tick, after head visibility's update: `hidden` = it found meshes drawn again and left none drawn.
 * A second leave while the veil is open (a leave that went straight back to the native view within a tick and left
 * again) keeps it open: the tick that sees that leave counts as the first of two again, unless it found the body drawn
 * again and switched it off, so the frame with that leave's body is veiled too (round 19).
 * The helper's begin and end forget the last frame's kind and close the veil: a native frame of the old world and the
 * new one's first head-camera frame are no leave.
 * Off (the default) and without the lock: nothing is reached or read. */
#define TFP_CAMERA_VEIL_CLEAR .10f
/* Round 26 (docs/design/specs/2026-11-24-helmet-in-view.md "Round 26"): how far the veil moves the eye. In feel.52's
 * Stable soak (~/.cache/tfp-soak-34.modlog) the only moves over 0.5 m were two swaps in the middle of a dive or lunge
 * (02:16:09.838 and 02:18:33.429: 0.548 and 0.534 m, then 0.527 and 0.495 m): the body had gone 0.43 to 0.45 m on
 * from the eye the swap holds, and the veil carried the eye after it for two frames and back. So:
 *  - The head is taken as a cube of TFP_CAMERA_VEIL_HEAD_M either way of the head node. An eye with all of that cube
 *    below the bottom edge of the view (the view's own pitch, half the vertical field of view and
 *    TFP_CAMERA_VEIL_FOV_SPARE more) has no head in its frame, wherever it is: the veil moves it no further than that
 *    takes (in a dive the head is well below the eye: 0.55 m in the estimate of the spec). Its field of view is
 *    tfp_camera_set_veil_fov's; not known, TFP_CAMERA_VEIL_FOV_MOST, the widest the settings allow.
 *  - A move over TFP_CAMERA_VEIL_MOST is not made: that is no weapon's eye behind the head (the LAS-5 Scythe's needs
 *    0.32 m, a prone swap 0.34) but a body that has left the camera, and the move would be the lurch. The frame keeps
 *    the held eye and is counted as left behind the head (and as `too_far`): a frame of the helmet, from at least 0.30 m
 *    behind it, accepted in that case only. Less than the whole way is never taken there: from nearer the head the
 *    helmet would fill more of the frame, or the eye would be inside it.
 *  - After the veil closes, its last move is let out over TFP_CAMERA_VEIL_LET_OUT_MS (tfp_camera_veil_let_out) instead
 *    of taken back in one frame: the step back was the lurch's other half. Only head-camera frames of the lock with
 *    the body not down are let out into; any other frame ends it. (Round 26 had it that the body is hidden by then,
 *    so the eye need not be clear: only the head, torso and shoulders are; the cape and the rest of the body are
 *    drawn, and a held eye let out behind the head showed them at every throw (round 44). The let-out now ends at
 *    the larger of the held eye and the head plus TFP_CAMERA_VEIL_CLEAR: src/camera_clear.h THE FLOOR.) */
#define TFP_CAMERA_VEIL_HEAD_M .15f
#define TFP_CAMERA_VEIL_MOST .40f
#define TFP_CAMERA_VEIL_FOV_MOST 2.0943951f   /* 120 degrees: tfp_settings.h fov_degrees at most */
#define TFP_CAMERA_VEIL_FOV_SPARE .08726646f  /* 5 degrees */
#define TFP_CAMERA_VEIL_LET_OUT_MS 300.0
/* Round 30 (docs/design/specs/2026-11-24-helmet-in-view.md "Round 30"): at the release of full aim the game can
 * evaluate the camera a second time in the native frame's own game frame (0.2 to 0.4 ms after it) with its ordinary
 * camera on top, and the next frame is the native view again: feel.53's soak (~/.cache/tfp-soak-36.modlog) has six,
 * each one frame, and the veil moved four of them 0.20 to 0.38 m forward and back (the judge read 0.390 to 0.745 m).
 * The body is never drawn there (the native view never left). A swap made in full aim leaves the same way (out of full
 * aim, within this time) but with the gun leaving the hands (control mask 2). So a leave noted in a head-camera frame
 * within TFP_CAMERA_VEIL_SAME_FRAME_MS of a native frame of full aim, in which no action owns the view (control mask
 * 0), does not open the veil there: it is kept pending, and the tick that sees it opens the veil (tfp_camera_veil_tick),
 * which moves only head-camera frames: nothing, when the view went straight back. Every other leave opens it as before. */
#define TFP_CAMERA_VEIL_SAME_FRAME_MS 2.0
/* Round 30 (review): a leave kept pending whose view does not go back (a knock-down or a gun dropped in full aim, say;
 * none in soaks 8 to 36) is veiled from its second head-camera frame, which the whole move would make jump by up to
 * TFP_CAMERA_VEIL_MOST in a frame between two head-camera frames. Its move is let in instead: the n-th veiled frame
 * after such a leave moves the eye at most tfp_camera_veil_let_in(n) = TFP_CAMERA_VEIL_LET_IN_M n (n+1) / 2, so the
 * step changes by at most TFP_CAMERA_VEIL_LET_IN_M a frame (under the judge's 0.06 m), and the let-out after the
 * veil closes starts from what was moved. The cost: those frames are short of TFP_CAMERA_VEIL_CLEAR (counted behind),
 * a frame or two of the helmet where the body is drawn, as a move over TFP_CAMERA_VEIL_MOST already accepts. */
#define TFP_CAMERA_VEIL_LET_IN_M .04f
static inline float tfp_camera_veil_let_in(unsigned frames) { return TFP_CAMERA_VEIL_LET_IN_M*(float)frames*(float)(frames+1)/2; }
/* Round 32 (docs/design/specs/2026-11-24-helmet-in-view.md "Round 32"): a kept leave whose view really leaves (soak
 * 37, 10:07:04.745: a bolt-action pistol fired in full aim; the game dropped the aim, and nothing held the native eye)
 * showed its frame the whole way from the native eye at once, 0.425 m, unveiled, because the continuity takes nothing
 * in full aim. Its step from the eye last handed to the game (the native frame of its own game frame) is clamped to
 * tfp_camera_veil_let_in(1), and what that held back is let out over TFP_CAMERA_VEIL_LET_OUT_MS
 * (tfp_camera_veil_let_out) in the head-camera frames after it; the native view coming back, a new leave, the body
 * down, or the helper's begin and end drop it. A step over TFP_CAMERA_VEIL_CARRY_FAR_M is another view, not the same
 * eye (as the swap's hold has it), and is not clamped. A kept frame within the swap's hold (the native eye kept) is
 * under the cap and unchanged: every kept leave of soaks 33 to 38 but that one. */
#define TFP_CAMERA_VEIL_CARRY_FAR_M 1.0f
/* What of the last move is still added `ms` after the last veiled frame: from 1 to 0 from rest to rest (1 - smoothstep),
 * so the step changes by at most 6 (dt/T)^2 of the move a frame: 0.058 m for 0.40 m at 50 ms a frame. */
static inline float tfp_camera_veil_let_out(double ms) {
  if (!(ms > 0)) return 1;
  if (!(ms < TFP_CAMERA_VEIL_LET_OUT_MS)) return 0;
  const float x = (float)(ms/TFP_CAMERA_VEIL_LET_OUT_MS);
  return 1-x*x*(3-2*x);
}
/* The vertical field of view the frames are drawn with (radians), each tick (src/mod_view.c head_note); 0 or not a
 * number: not known (TFP_CAMERA_VEIL_FOV_MOST). */
void tfp_camera_set_veil_fov(float vertical_radians);
void tfp_camera_set_veil(bool armed);
void tfp_camera_veil_tick(bool hidden);
/* Round 19, a test switch (the binder's file veil_second.txt; off by default, and tfp_camera_reset_for_tests sets it
 * off): the veil opens at the tick that sees the leave, not at the first head-camera frame, so in the order soak 27
 * traced (that frame drawn before the tick, the frame with the body after it) only the frame that draws the body is
 * moved. The hook notes the leave (the same first frame of the lock that is not the native view after one that was,
 * while armed); the next veil_tick opens the veil if it is still armed and did not find the body drawn again and
 * switched off already, and the tick after that closes it whatever it found, unless it saw another leave. A leave is
 * taken by one tick, opened or not. In the other order (the tick before the first head-camera frame, not seen in soak 27) that frame, the one
 * with the body, is not moved. The stats say which way it opens (second). */
void tfp_camera_set_veil_second(bool on);
/* Counts since the start: leaves the veil opened at or was kept open for; frames drawn while it was open; of them, frames whose eye it moved;
 * behind: of them, frames other than the native view whose eye was not proven TFP_CAMERA_VEIL_CLEAR in front of the
 * head (a body off foot, a frame the helper did not present, an eye it would not move): the body may have been in
 * the view. Native frames drawn while it is open (a leave that goes straight back within a tick) are counted in
 * `frames` only: they are the game's own camera, which the veil never moves. On foot `behind` stays 0. largest_m: the
 * largest move since the start. second: tfp_camera_set_veil_second is on. */
/* Round 26: under, of the frames it moved or found clear, those whose head was below the view before the whole way
 * to TFP_CAMERA_VEIL_CLEAR in front (moved less, or not at all); too_far, frames it left behind the head because the move
 * was over TFP_CAMERA_VEIL_MOST (counted in behind too). */
/* Round 30: held_back, leaves of a same-frame evaluation out of full aim with no action owning the view, kept pending
 * (TFP_CAMERA_VEIL_SAME_FRAME_MS). */
/* Round 32: clamped, kept leaves whose frame was clamped to the let-in cap (TFP_CAMERA_VEIL_CARRY_FAR_M); not in moved. */
/* Round 44 (src/camera_clear.h): floored, head-camera frames whose shown eye THE FLOOR moved forward (after the veil
 * and its let-out); floor_largest_m, its largest move since the start. */
typedef struct TfpCameraVeilStats { uint64_t leaves, frames, moved, behind; float largest_m; bool second; uint64_t under, too_far, held_back, clamped;
  uint64_t floored; float floor_largest_m; } TfpCameraVeilStats;
void tfp_camera_veil_stats(TfpCameraVeilStats *out);
/* The tick's whole part in the veil (src/mod_view.c head_note calls it after head visibility's update, `tick` filled as
 * the "Head:" line has it, view included). NULL, no tick read: switched off. Otherwise tfp_camera_veil_tick with
 * `hidden` = the update found meshes drawn again and left none drawn, then tfp_camera_set_veil. */
struct TfpHeadTick;
void tfp_camera_veil_from_head(const struct TfpHeadTick *tick);
typedef struct TfpCameraContinuityStats { uint64_t jumps, edges, restarts; float largest_m; uint64_t reseats, cuts; } TfpCameraContinuityStats;
/* Counts since the start; the largest correction carried since the last call. reseats: frames in which the body
 * root alone was re-seated (a jump in the root's frame with the eye going on evenly in the world; nothing taken up).
 * cuts: head-camera pose cuts taken up as a change of camera (round 19, THE CUT; not counted in edges). */
void tfp_camera_continuity_stats(TfpCameraContinuityStats *out);
/* The eye going back and forth, watched only (src/camera_alternation.h; nothing is held). Counts since the start:
 * frames the rule counted under the lock, how many of them in a frame in which jumps were looked for, how many with
 * the controller's fire flag set (flag byte 12 bit 0x40, which has no proven reader on this build). largest_m: the
 * largest move among them since the last call. kind: the camera kind of the last frame counted. */
typedef struct TfpCameraAlternationStats { uint64_t frames, looked, fire; float largest_m; uint32_t kind; } TfpCameraAlternationStats;
void tfp_camera_alternation_stats(TfpCameraAlternationStats *out);
/* The trace: a State edge was just written. The presented frames of the next 500 ms, and the six before it that
 * were not yet handed out, are given to tfp_camera_trace_read (oldest first, each once). `ms` counts from the edge.
 * The helper opens such a window itself in a frame whose eye says TFP_CAMERA_TRACE_SELF (the edge is that frame; in
 * a window that is open, the window lasts 500 ms from that frame and `ms` goes on).
 * source: 1 head camera, 2 native view, 3 the last native argument repeated. root: the body root the eye was
 * measured against, axes: that root's axis rows (the eye in the root's frame is axes times (eye - root); the
 * continuity works there, and a root whose axes turn in one frame moves that eye without moving the root). what:
 * TFP_CAMERA_TRACE_* of the frame. tick: the frame's time on the helper's own clock (ms), so that the time between
 * two frames is known across edges too. With tick, QUIET and START the rows hold all the continuity was given
 * (tools/harness/replay.py runs it again from them). */
enum { TFP_CAMERA_TRACE_LOOKED = 1,  /* jumps within the camera were looked for */
       TFP_CAMERA_TRACE_JUMP = 2,    /* a jump within the camera was taken up */
       TFP_CAMERA_TRACE_EDGE = 4,    /* a change of camera was taken up */
       TFP_CAMERA_TRACE_ROOT = 8,    /* the body root alone was re-seated: nothing taken up for it */
       TFP_CAMERA_TRACE_RESTART = 16, /* the continuity started again (a teleport, a pause) */
       TFP_CAMERA_TRACE_START = 32,  /* the continuity began from nothing in this frame (its first, or the first after the helper forgot it) */
       TFP_CAMERA_TRACE_QUIET = 64,  /* full aim: the continuity was told to take nothing up */
       TFP_CAMERA_TRACE_OFF_FOOT = 128, /* a head-camera frame of a body that was not on foot: its eye is not damped and jumps in it are not looked for. A native frame never says it: the question is not asked there */
       TFP_CAMERA_TRACE_DAMPED = 256, /* the eye damping ran in this frame (src/camera_damp.h); without it a `damp` that is not nothing is a lag being let go */
       TFP_CAMERA_TRACE_SELF = 512,  /* the presented eye changed its step by TFP_TRACE_SELF_M or more in this frame: the helper opened a window itself, or kept one open (src/trace_edges.h) */
       TFP_CAMERA_TRACE_CUT = 1024,  /* round 19: the head camera's pose cut was taken up as a change of camera (THE CUT, src/camera_continuity.h) */
       TFP_CAMERA_TRACE_SETTLE = 2048 /* round 24: THE SETTLE held the eye in this frame, or began or ended (src/camera_continuity.h) */ };
/* damp: what the eye damping added to the true eye of this frame, in the world (wanted - damp is the eye the head
 * camera would have had without it); zeros where it added nothing. view: which way the view pointed, a unit vector
 * in the world (the head camera's forward; in a native frame the forward axis of the rig eye the camera hangs on);
 * zeros where the frame did not have it. */
/* sock, arms: what the arm drive drew in this frame, as the binder told it (tfp_camera_trace_draw): the weapon socket
 * as drawn less the game's own, along the view, in metres, and TFP_ARM_DRAW_* (src/arm_drive.h; 0: not driven, or not
 * told for this frame). */
/* veil (round 19): how far the veil (tfp_camera_set_veil) moved the eye handed to the game in this frame, in the world;
 * zeros where it moved nothing. `eye` is the eye before it (what the continuity, the damping and `step` work on), so
 * the eye shown is eye + veil. Round 18 found the veil moving the shown eye up to 0.32 m forward for a frame or two at
 * most leaves of the native view, and no row said so. */
typedef struct TfpCameraTraceRow { float ms; uint32_t kind, source; float eye[3], wanted[3], step, correction, entry_ease; float root[3]; uint32_t what; float axes[9]; double tick; float damp[3], view[3]; float sock; uint32_t arms; float veil[3]; float turn[3];
  float veil_head[2]; /* round 26: in a frame the veil was open for, the head node from the eye before the veil: ahead along the level view, up (m); 0 0 otherwise */
  /* Round 44: in every head-camera frame of the lock, the head node from the eye shown (after the veil and the floor):
   * ahead along the level view, up (m), with head_known; and the push of src/camera_clear.h in that frame (m). */
  float head_shown[2]; bool head_known; float clear;
  /* Round 49 (src/camera_turn.h THE CARRY). turn_carry: how far the direction shown is off the game's own in this frame
   * (degrees; 0: nothing carried). turn_edge: the degrees a change of camera in this frame began a carry with (0: none),
   * and with it the probe: game_turn, the game's own direction in the world (what `turn` would have been), and
   * game_cam, where the game last put its ordinary camera (game_cam_known). */
  float turn_carry, turn_edge, game_turn[3], game_cam[3]; bool game_cam_known;
  /* Round 49, the log only. hold: in a head-camera frame whose eye a swap holds (the held eye, or its glide to the
   * incoming weapon's eye or its release), the held difference: the eye from the head camera's own, in the body root's
   * frame (m), with hold_known. dive: the binder said the body is in a dive (tfp_camera_set_dive). */
  float hold[3]; bool hold_known, dive;
} TfpCameraTraceRow;
void tfp_camera_trace_edge(void);
/* The arm drive's figures of the last primary draw, for the newest frame presented. The draw comes after the frame's
 * camera call and before the next tick, so the binder calls this once a tick, when the drive has seen a draw since the
 * last call, before it reads the rows. A frame nobody spoke for says arms 0. The log only. */
void tfp_camera_trace_draw(float sock_m, uint32_t arms);
uint32_t tfp_camera_trace_read(TfpCameraTraceRow *rows, uint32_t capacity);
#endif
