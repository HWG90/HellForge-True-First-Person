/* native/first_person/native_ads_camera.cpp (0.5.41-smooth.66, 1,304 lines)
 * in C11, for build 25480438. Native FPV is forwarded exactly. Qualified
 * normal-camera eye correction only. No Unit writes.
 *
 * A translation, not a rewrite: every check, its order, every reset, counter
 * and expression is retail's. "cpp:a-b" above a function is its line range in
 * the retail file. What differs is in docs/translation/CAMERA-HELPER-TRANSLATION.md. */
#include "camera_helper.h"
#include "head_log.h"
#include "camera_lock.h"
#include "camera_turn.h"
#include "camera_continuity.h"
#include "camera_alternation.h"
#include "camera_damp.h"
#include "camera_traversal.h"
#include "camera_glide.h"
#include "camera_vehicle.h"
#include "camera_clear.h"
#include "camera_math.h"
#include "trace_edges.h"
#include <stdatomic.h>

/* ======================================================================
 * Build constants, build 25480438. Each line: retail 26568081's value, and
 * how this build's value was established.
 *   build_map  = tools/build_map.py --to-here (the kit's migration records)
 *   image      = bytes read from game-pre1020.dll / r6_reload_exe_20261017.bin
 *   layout     = structure layout, same value in retail; the named file of
 *                this repository reads the same field on this build
 * ====================================================================== */
/* -- image identity -- */
#define B_GAME_STAMP        UINT32_C(0x6ab3b43f) /* retail 0x6af322c1; build_map; profile.h FP_GAME_STAMP; image PE header */
#define B_GAME_SIZE         UINT32_C(0x4744000)  /* retail 0x3382000;  build_map; profile.h FP_GAME_SIZE; image PE header */
#define B_EXE_STAMP         UINT32_C(0x6ab382e4) /* retail 0x6af2d5e6; build_map; profile.h FP_EXE_STAMP; image PE header */
#define B_EXE_SIZE          UINT32_C(0x39e8000)  /* retail 0x29fe000;  build_map; profile.h FP_EXE_SIZE; image PE header */
/* -- code addresses -- */
#define B_EXE_SETTER        UINT32_C(0x30deb0)   /* retail SETTER 0x2dd5a0; build_map; native_contract.h camera_position */
#define B_GAME_CALL         UINT32_C(0x127a135)  /* retail CALL 0x110bff5; build_map; native_contract.h camera_position_call */
#define B_EXE_GRAPH_GETTER  UINT32_C(0x2bd870)   /* retail GRAPH_GETTER 0x28d3c0; build_map; native_contract.h graph_getter */
#define B_EXE_ANIM_GETTER   UINT32_C(0x2bd9c0)   /* retail 0x28d510; build_map; native_contract.h anim_getter */
#define B_EXE_UNIT_WORLD    UINT32_C(0x1fd220)   /* retail 0x1f30e0; build_map; native_contract.h unit_world_matrix */
#define B_GAME_CALL_PAIR    UINT32_C(0x127a127)  /* retail 0x110bfe7; build_map; native_contract.h camera_scratch_pair (= B_GAME_CALL-14, as retail) */
/* -- code bytes -- */
#define B_HEX_CALL          "41ff5120"           /* retail the same; image at game+0x127a135; native_contract.h */
#define B_HEX_SETTER        "48895c2408574883ec60488bd9498bf88bcae8f9f9d8ff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920"
                                                 /* 48 bytes as retail; retail call displacement 1931dcff, here f9f9d8ff; image at exe+0x30deb0 */
#define B_HEX_UNIT_WORLD    "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3"
                                                 /* 47 bytes as retail; retail call displacement e2d5eaff, here 9206eaff; image at exe+0x1fd220 */
#define B_HEX_GRAPH_GETTER  "488d4160c3"         /* retail the same; image at exe+0x2bd870; native_contract.h */
#define B_HEX_ANIM_GETTER   "488b8178010000c3"   /* retail the same; image at exe+0x2bd9c0; native_contract.h */
#define B_HEX_CALL_PAIR     "4c8d45c08b5308488b0b4c8b482041ff5120488b05c8c10a024c8d45008b5308488b0b4c8b482041ff5128"
                                                 /* 43 bytes as retail; retail rip displacement c854bd01, here c8c10a02 (-> game+0x3326308); image at game+0x127a127 */
/* -- globals -- */
#define B_GAME_CONTEXT      UINT32_C(0x346d560)  /* retail 0x2e287d0; build_map; profile.h FP_CAMERA */
#define B_GAME_AVATAR_OWNER UINT32_C(0x3326d20)  /* retail 0x2ce1ee0; build_map; profile.h FP_AVATAR */
#define B_GAME_CONTROLLER   UINT32_C(0x347ce70)  /* retail 0x2e381a8; build_map; profile.h FP_CONTROLLER */
#define B_EXE_REGISTRY      UINT32_C(0x1a100f0)  /* retail 0x1ab76f8; build_map; profile.h FP_REGISTRY */
#define B_GAME_STATE        UINT32_C(0x3326340)  /* retail 0x2ce1508; build_map; profile.h FP_STATE */
#define B_STATE_SCENE       UINT32_C(0xac21c)    /* retail 0xac29c;   build_map; profile.h FP_STATE_SCENE */
#define B_GAME_API_ROOT     UINT32_C(0x3326308)  /* retail 0x2ce14c8; build_map; profile.h FP_API_ROOT; target of the rip load in B_HEX_CALL_PAIR */
/* -- avatar owner rows -- */
#define B_AVATAR_FLAGS      UINT32_C(0x53e880)   /* retail 0x53e888; build_map; profile.h FP_AVATAR_FLAGS */
#define B_AVATAR_RIG        UINT32_C(0x53e9b8)   /* retail 0x53e9c0; build_map; profile.h FP_AVATAR_RIG */
#define B_AVATAR_STRIDE     UINT32_C(0x1238)     /* retail 0x1240;   build_map; profile.h FP_AVATAR_STRIDE */
/* Beyond retail (the camera lock): a byte per avatar, bit 0x10 = ragdoll; src/arm_drive.c and src/backend_ops.c B_AVATAR_PHYSICS. */
#define B_AVATAR_PHYSICS        UINT32_C(0x547844)
#define B_AVATAR_PHYSICS_STRIDE UINT32_C(0x78)
/* -- structure layout shared by both builds -- */
#define L_AVATAR_COUNT      0x6C   /* layout; snapshot.c (avatar_owner+0x6c) */
#define L_AVATAR_ROWS       0x110  /* layout; snapshot.c (avatar_owner+0x110+index*8); descriptor: avatar +8, body +12 */
#define L_FLAG_FIRE         12     /* flag byte 12 bit 0x40. No reader on this build; feeds only Policy.fire, which nothing reads (see Open) */
#define L_FLAG_SHOULDER_A   10     /* layout; snapshot.c flags[10]&4 */
#define L_FLAG_SHOULDER_B   15     /* layout; snapshot.c flags[15]&32 */
#define L_FLAG_SEATED       14     /* layout; snapshot.c flags[14]&4 */
#define L_REGISTRY_BLOCK    0x88   /* layout; backend.c (units +0x88, capacity +0x98, generations +0xa0) */
#define L_REGISTRY_CAPACITY 0x98   /* layout; backend.c */
#define L_API_CAMERA        0x20   /* CameraAPI table at root+0x20: image, "4c8b4820" mov r9,[rax+0x20] in B_HEX_CALL_PAIR */
#define L_API_SETTER        0x20   /* position setter slot at api+0x20: image, "41ff5120" call [r9+0x20] */
#define L_CONTEXT_RING      0x1F8  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_IDLE      0x1F00 /* layout; snapshot.c, backend.c */
#define L_CONTEXT_RECORDS   0x200  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_RECORD    0xE8   /* layout; snapshot.c, backend.c */
#define L_CONTEXT_BASIS     0x0C   /* layout; the 0.5.34 port read the camera matrix at context+0xc in the game (camera_policy.c:472) */
#define L_VT_GRAPH_GETTER   0xE8   /* layout; scene.c fp_graph */
#define L_VT_ANIM_GETTER    0x1B0  /* layout; action.c */
#define L_UNIT_GRAPH        0x60   /* layout; scene.c (camera+0x18 == object+0x60) */
#define L_UNIT_HEADER       0x70   /* layout; scene.c (count +0x70; in the 56-byte header: worlds +0x18, links +0x28, hashes +0x30) */
#define L_UNIT_EXTERNAL     0x78   /* layout; scene.c (camera.object+0x78 == parent worlds) */
#define L_UNIT_WORLDS       0x88   /* layout; scene.c */
#define L_UNIT_LINKS        0x98   /* layout; scene.c */
#define L_UNIT_HASHES       0xA0   /* layout; scene.c */
#define L_UNIT_ANIM         0x178  /* layout; action.c */
#define L_UNIT_PARENT       0x1D0  /* layout; scene.c */
#define L_UNIT_RESOURCE     0x358  /* layout; action.c, backend.c */
#define L_CAMERA_GRAPH      0x18   /* layout; scene.c */
#define L_CAMERA_NODE       0x20   /* layout; scene.c */
#define L_BODY_RESOURCE     UINT64_C(0x4D1C334D294DFA97) /* same; action.c */
#define L_HEAD_HASH         UINT32_C(0x8C5570C9) /* same; scene.c */
#define L_HEAD_PARENT_HASH  UINT32_C(0xE9ECCA21) /* same; scene.c */
#define L_RIG_NODES         63     /* same; scene.c fp_graph_rig */
#define L_RIG_CAMERA_NODE   50     /* same; scene.c */
#define L_RIG_CAMERA_HASH   UINT32_C(0x60ED8C39) /* same; scene.c */
#define L_RIG_CAMERA_LINK   UINT32_C(0x40001)    /* same; scene.c */
#define L_CAMERA_RIG_LINK   UINT32_C(0x00320002) /* same; scene.c fp_camera_native_parent */
#define L_ANIM_LAYERS       31     /* same; action.c */
#define L_ANIM_LAYER        13     /* same; action.c (its layer 14, zero-based 13) */
#define L_STATE_ON_FOOT     36     /* same; action.c */
#define L_STATE_BRIDGE      3      /* same; action.c */
#define L_SCENE_SHIP        3      /* same; action.c (which also accepts 4; retail's helper accepts 3 only, kept) */
#define L_CONTEXT_CAMERA    0      /* layout; backend.c (camera pointer at context+0) */
#define L_CONTEXT_UNIT      8      /* layout; backend.c (camera unit at context+8) */
#define L_CONTROL_AIM       4      /* layout; snapshot.c controller byte 4 (bytes 0-3 are the mask) */
#define L_CONTROL_GUARD     5      /* layout; snapshot.c controller byte 5 */
#define L_ANIM_GRAPH        0x28   /* layout; action.c (instance+0x28) */
#define L_ANIM_ACTIVE       0x50   /* layout; action.c (instance+0x50) */
#define L_ANIM_LAYER_COUNT  4      /* layout; action.c (graph header word 1) */
#define L_ANIM_TABLE        8      /* layout; action.c (graph header word 2) */
#define L_GRAPH_COUNT       0x10   /* layout; scene.c (object+0x60+0x10 = its count at +0x70) */
#define L_GRAPH_LOCALS      0x20   /* layout; scene.c (object+0x80) */
#define L_GRAPH_WORLDS      0x28   /* layout; scene.c (object+0x88) */
#define L_HEADER_WORLDS     0x18   /* layout; in the 56 bytes at L_UNIT_HEADER: = L_UNIT_WORLDS */
#define L_HEADER_LINKS      0x28   /* layout; = L_UNIT_LINKS */
#define L_HEADER_HASHES     0x30   /* layout; = L_UNIT_HASHES */
#define L_MATRIX            0x40   /* layout; image: "48c1e106" (shl rcx,6) in unit_world_matrix and unit_local_position */
#define L_MATRIX_POSITION   0x30   /* layout; scene.c (matrix floats 12-14) */
#define L_LOCAL_POSITION    0x24   /* layout; image: "4883c124" in native_contract.h unit_local_position */
#define L_ARGUMENT_ROTATION 64     /* bytes from the position argument to the rotation the game passes next: image, "4c8d45c0" lea r8,[rbp-0x40] then "4c8d4500" lea r8,[rbp+0] in B_HEX_CALL_PAIR */
/* ====================================================================== */

#define HIGH UINT64_C(0x800000000000)

/* cpp:16-22 */
static TfpCameraHost host;
static _Atomic uintptr_t original;
static _Atomic uint32_t installed, armed, reserved, last_error;
static _Atomic uint64_t callback_generation;
static _Atomic uint64_t seen, forwarded, replaced, native6_forwarded, resets, return_address;
static uint64_t slot = 0, slot_image = 0, generation_counter = 0, owned_generation = 0, retired_generation = 0;
static bool possible = false;
enum { OK = 0, INVALID_CONFIG = 1, BUSY = 2, READ_FAILED = 3, IMAGE_MISMATCH = 4,
       CODE_MISMATCH = 5, API_MISMATCH = 6, OWNER_MISMATCH = 7, UNIT_MISMATCH = 8, PIN_FAILED = 9,
       SLOT_NOT_WRITABLE = 10, CAS_MISMATCH = 11, CAS_FAULT = 12, CLEANUP_PENDING = 13,
       POSE_INVALID = 14, SPACE_INVALID = 15, GRAPH_MISMATCH = 16, TOKEN_MISMATCH = 17, ON_FOOT_REQUIRED = 18 };

static void control_acquire(void) { host.lock_exclusive(host.user, TFP_CAMERA_LOCK_CONTROL); }
static void control_release(void) { host.unlock_exclusive(host.user, TFP_CAMERA_LOCK_CONTROL); }
static void policy_acquire(void) { host.lock_exclusive(host.user, TFP_CAMERA_LOCK_POLICY); }
static void policy_release(void) { host.unlock_exclusive(host.user, TFP_CAMERA_LOCK_POLICY); }
static void policy_acquire_shared(void) { host.lock_shared(host.user, TFP_CAMERA_LOCK_POLICY); }
static void policy_release_shared(void) { host.unlock_shared(host.user, TFP_CAMERA_LOCK_POLICY); }

/* cpp:28-76, without the fields only the retired features used (profile_*,
 * motion, free_*, observe_*, kick_*). `side` stays: it is in the target
 * expression, and is always 0 now that NADSC_SetOffsets is gone. */
typedef struct Policy {
  float side;
  TfpCorrection correction;
  float last_relative[3]; double last_relative_tick; bool relative_valid;
  bool presentation_enabled; uint32_t current_weapon;
  bool full_ads, shoulder, fire;
  TfpCameraConfig c; uint64_t controller, descriptor, registry, units, generations;
  uint64_t body_object, camera_object, hashes, links, sampled_worlds; uint32_t count, head_node;
  uint64_t foot_instance, foot_graph, foot_array, foot_current, foot_definition;
  uint8_t descriptor_bytes[20]; bool safe, fallback_safe, tracking, native6_seen; double previous, native6_at;
  float relative_head[3], blend, head_distance, last_dt;
  /* Opt-in handoff eases (tfp_camera_set_handoff); zero keeps both cuts exact. */
  float handoff_entry_ms, handoff_rotation_ms;
  TfpHandoffPosition entry; TfpHandoffRotation turn; TfpRotationProof rotation_proof;
  TfpQ forwarded_q; double forwarded_q_tick; bool forwarded_q_valid;
  uint64_t entry_started, entry_frames, entry_rejected, turn_started, turn_frames, turn_rejected;
  /* The view the last native6 frame was rendered with (track_native_rotation). */
  TfpQ native_q; double native_q_tick; bool native_q_valid;
  uint32_t sample_reasons[8], entry_delay_frames, entry_last_delay, entry_max_delay; bool entry_waiting;
  uint32_t pending_paths[4]; float turn_start_gap;
  bool hold_exit, last_native_valid; float last_native[4], last_head_world[3];
  bool hold_eye, native_eye_valid, eye_delta_active, eye_returning;
  float native_eye[3], eye_delta[3]; double native_eye_tick, eye_hold_started, eye_swap_tick; uint32_t control_mask, eye_hold_frames;
  uint32_t weapon_key, native_weapon_key, stretch_key, stretch_key_frames, eye_glides;
  TfpEyeGlide eye_glide; /* beyond retail: the glide of eye_delta under way in the frames the lock holds (src/camera_glide.h) */
  bool guard_only, guard_grace; double last_view_tick; uint32_t guard_grace_frames;
  bool hold_seated, seated_now, anchor_ads, native_full_ads, ads_gap_active; double ads_gap_started; uint32_t ads_gap_frames, ads_learned; double last_native_tick; uint32_t exit_holds;
  float head_apart; /* beyond retail: head to neck on a body that is down, for the Dead line */
  bool lock_now, lock_off_foot; /* beyond retail: this frame is held by the camera lock; the body is not on foot */
  /* Beyond retail, lock frames: the avatar's ragdoll flag; the head is beyond walking range of the unit root; the
   * body is down (tfp_camera_down_stats); the animation chain was read this frame; which proof refused the frame. */
  bool lock_ragdoll, head_far, lock_down, foot_sampled; uint32_t why;
  uint32_t seated_frames, entry_trace_frame; TfpQ seated_q; bool seated_q_valid; uint32_t entry_trace_pad, entry_trace_count; TfpCameraEntryTraceRow entry_trace[TFP_CAMERA_ENTRY_TRACE_ROWS];
} Policy;
/* Policy{}: the default member initialisers. Everything 0/false except the
 * correction's (progress 1, duration 150) and the identity quaternions. */
/* Beyond retail (tfp_camera_set_action_tuning); the defaults are retail's behaviour. */
static _Atomic uint32_t tuning_extra_mask, tuning_eye_idle_ms = 300, tuning_eye_return;
/* Beyond retail (tfp_camera_set_lock): the camera lock. Off by default, and then nothing here runs. */
static _Atomic uint32_t lock_on, lock_last_kind;
static _Atomic uint64_t lock_frames, view_frames, lock_foreign_frames, lock_missed; static _Atomic uint32_t lock_missed_kind, lock_missed_error;
static volatile float lock_entry_ms = 140; /* the eased entry into the native view while the lock is on (retail: none) */
static volatile float lock_gain, lock_max, lock_settle = 300, lock_thrown_gain, lock_thrown_max, lock_peak;
static volatile float lock_damp_ms; /* the camera damping option as a time constant (src/camera_damp.h); 0: none */
static _Atomic uint32_t lock_bodycam = 1; /* the camera style (src/camera_damp.h TfpCameraStyle): 1 Bodycam, the sliders as they are; 0 Stable */
static TfpEyeDamp lock_eye_damp;    /* the head camera's eye under that option (lock_damp_eye). Policy lock. */
static TfpDampFrame lock_damp_frame; /* round 49: the frame that eye is damped in (src/camera_damp.h tfp_damp_frame_step). Policy lock. */
static _Atomic uint32_t damp_frame_on; /* tfp_camera_set_damp_frame (a test switch, off by default: the root's own axes, as before) */
static TfpKick lock_kick; static TfpQ lock_view = {0, 0, 0, 1}, lock_last_kick = {0, 0, 0, 1}; static bool lock_view_known; static double lock_tick;
/* Beyond retail: the body down under the lock (camera_helper.h TfpCameraDownStats). lock_carry: what is left of the
 * held direction after the body is up again, in the game's view frame, eased out (LOCK_CARRY_MS). Policy lock. */
#define LOCK_CARRY_MS 120.0f
/* Round 49, THE CARRY (src/camera_turn.h): the direction last shown, and what of it is carried across a change between
 * the native view and the ordinary camera. lock_game_cam: where the game last put its own ordinary camera (the kind 4
 * position argument, before the head camera's eye replaces it), for the log. Policy lock. */
static TfpTurn lock_turn; static float lock_game_cam[3]; static double lock_game_cam_tick = -1;
/* Round 49 (camera_helper.h tfp_camera_set_dive): the binder's word that the body is in a dive. */
static _Atomic uint32_t dive_on;
#define LOCK_DOWN_MS 1000.0   /* a refused frame this long after a down frame still counts as the down body's */
#define LOCK_OFF_FOOT_MS 250.0
static TfpQ lock_carry = {0, 0, 0, 1}; static bool lock_was_held; static double lock_down_until;
/* lock_first: the last first-person direction: the view of the last native (kind 6) frame or ordinary (kind 4) frame
 * on foot. Not lock_view, which is the view last shown and follows the game's camera on a body that is off foot but
 * not yet known to be down (a death that does not set the ragdoll flag, until the head is 3 m away). Policy lock. */
static TfpQ lock_first = {0, 0, 0, 1}; static bool lock_first_known;
static _Atomic uint64_t lock_down_frames, lock_down_refused, lock_off_foot_at;
static _Atomic uint32_t lock_missed_apart; /* a float's bits: space_why */
static _Atomic uint32_t lock_missed_why, lock_down_kind, lock_down_error, lock_down_why, lock_body_ragdoll, lock_body_off_foot, lock_off_foot_body;
static _Atomic uint32_t lock_body_distance, lock_down_distance, lock_down_head[3]; /* floats as their bits */
/* Round 38 (camera_helper.h TfpCameraSeatStats, src/camera_vehicle.h): first person in an exosuit's or vehicle's seat.
 * seat_in: the last frame of this camera was a seat. seat_glide runs into the seat (seat_glide_pending: it begins on
 * the first seat frame that is shown) or, with seat_out, out of it. seat_from: the view last shown under the lock and
 * its rotation (the head camera's eye, the seat's, the native view's while the glide out runs in it, or otherwise in the
 * native view the camera as the engine drew it last): where a glide starts. The glide out runs in head-camera and
 * native frames alike (seat_back, seat_native); any other frame ends it. seat_anchor: the head's offset in the root frame, frozen once settled. seat_hand_*: the body's hand
 * nodes, looked up once per body graph. Policy lock. */
static TfpSeatGlide seat_glide; static TfpSeatAnchor seat_anchor; static bool seat_in, seat_out, seat_glide_pending;
static uint32_t seat_out_kind; /* the camera kind of the last frame of the glide out (4 or 6): into the native view it begins again */
static float seat_from[3]; static TfpQ seat_from_q = {0, 0, 0, 1}; static double seat_from_tick; static bool seat_from_known;
static uint64_t seat_hand_hashes; static uint32_t seat_hand_node[2], seat_hand_count; static bool seat_hand_known;
static _Atomic uint64_t seat_frames, seat_refused, seat_glided, seat_entries, seat_exits, seat_cuts, seat_body_at;
static _Atomic uint32_t seat_in_kind, seat_in_ms, seat_out_ms, seat_now, seat_settled, seat_gliding, seat_hands, seat_body;
static _Atomic uint32_t seat_in_metres, seat_out_metres, seat_offset[3], seat_hand[6]; /* floats as their bits */
/* Beyond retail, and only in a frame the lock holds (policy.lock_now): the presented eye is kept continuous across
 * every change of camera and every one-frame move of the rig (camera_continuity.h; tfp_camera_set_continuity), and
 * the frames around a State edge are kept for the binder's "Trace:" lines (tfp_camera_trace_edge). All under the
 * policy lock. Off, or without the lock: none of it is reached and no memory is read for it. */
enum { SMOOTH_HEAD = 1, SMOOTH_NATIVE = 2, SMOOTH_REPEAT = 3 };
static _Atomic uint32_t smooth_on, trace_on;
static TfpContinuity smooth; static const TfpContinuityTuning smooth_tuning = TFP_CONTINUITY_DEFAULT;
/* THE ALTERNATION, watched only (src/camera_alternation.h): the frames in which the wanted eye goes back and forth
 * are counted for the "Alternation:" line. Nothing is done about them; the filter is given the eye as before. */
static TfpAlternation alternation;
static struct { uint64_t frames, looked, fire; float largest; uint32_t kind; } alternation_seen;
/* Within the native view a jump is looked for only for SMOOTH_EVENT_MS after something that can re-seat the rig: the
 * control mask, the top request's kind or weapon, or the binder's weapon key changed, or the binder said so (a lease
 * or a hold begun or ended: tfp_camera_continuity_event). Anywhere else the rig eye's motion is the game's own (a
 * crouch, a dive, a fast turn about the body) and is shown as it is.
 * With continuity on, the eased entry (lock_entry_ms) does not run: the change of camera is taken up here, whole, on
 * a curve that keeps the eye's speed and whose length follows from the distance. Two eases on top of each other
 * read each other's steps as jumps. */
#define SMOOTH_EVENT_MS 600.0 /* 300 until 0.6.0-feel.35: the rig was seen re-seating half a second after the view came back */
static _Atomic uint32_t smooth_event;
/* Beyond retail, and only in a frame the lock holds (tfp_camera_set_orphan): the binder says that the game still
 * presents the first-person request although the mod has asked for its exit and the gun is off the rig (a stim: the
 * game keeps the request for about 0.85 s under mask 0x40). The rig's eye then goes to the body's head (seen in the
 * game 2026-11-23: 0.48 m in two frames, to 0.21 m behind the head camera's eye, inside the helmet). In those frames
 * the eye presented is the head camera's own point, handed to the continuity as the head source, and converted to
 * the rig eye's frame for the game's parent-local argument. Off (the default): nothing here is reached. */
static _Atomic uint32_t orphan_on, orphan_behind; /* orphan_behind: a float's bits */
/* Beyond retail, round 14: the veil (camera_helper.h tfp_camera_set_veil). The hook writes under the policy lock, the
 * tick writes veil_armed, veil_open and veil_ticks; both are the game thread (the "Hook:" line names the thread). */
static _Atomic uint32_t veil_armed, veil_open, veil_ticks, veil_last_kind, veil_largest; /* veil_largest: a float's bits */
static _Atomic uint64_t veil_leaves, veil_frames, veil_moved, veil_behind;
/* Round 19 (tfp_camera_set_veil_second): the veil opens at the tick that sees the leave; the hook notes the leave in
 * veil_pending and the tick takes it. Both are the game thread, as above. */
static _Atomic uint32_t veil_second, veil_pending;
/* Round 26: the vertical field of view the frames are drawn with (a float's bits; 0: not known), and the frames the
 * head was below the view in (under) or the move was over TFP_CAMERA_VEIL_MOST (far). */
static _Atomic uint32_t veil_fov;
static _Atomic uint64_t veil_under, veil_far;
/* Round 30: leaves kept pending by veil_same_frame_leave. */
static _Atomic uint64_t veil_held_back;
/* Round 26: the last veiled frame's move and time, let out after the veil closes (game thread, under the policy lock). */
static float veil_out_move[3]; static double veil_out_tick; static bool veil_out_on;
static float veil_out_ahead; /* round 49: how far in front of the head node the eye was before that move, along the level view (negative: behind) */
/* Round 30 (camera_helper.h TFP_CAMERA_VEIL_LET_IN_M): a leave kept pending by veil_same_frame_leave whose view stays
 * away is veiled from its second head-camera frame; the move is let in, veil_in_frames counting the veiled frames
 * (game thread, under the policy lock). */
static bool veil_in_on; static unsigned veil_in_frames;
/* Round 32 (camera_helper.h TFP_CAMERA_VEIL_CARRY_FAR_M): the eye last handed to the game in a frame of the lock, in
 * the world, and when; and a kept leave's frame clamped to tfp_camera_veil_let_in(1) from it: what the clamp held back
 * (veil_carry), let out over TFP_CAMERA_VEIL_LET_OUT_MS from veil_carry_tick in the head-camera frames after it. All
 * the game thread, under the policy lock. */
static float veil_eye_last[3]; static double veil_eye_tick; static bool veil_eye_known;
/* Round 55, THE ENTRY: what the last head-camera frame's eye shown was moved after the continuity (the veil, its
 * let-out, the floor, the clamp's carry), in that frame's root axes, and the frame's tick: the continuity is handed it
 * at an entry into the native view (smooth_present), so it lets the rest out. */
static float veil_shown_moved[3]; static double veil_shown_tick; static bool veil_shown_on;
static float veil_carry[3]; static double veil_carry_tick; static bool veil_carry_on;
static _Atomic uint64_t veil_clamped;
/* Round 44 (src/camera_clear.h): the push of the eye in front of the head and its last frame's figures; the frames the
 * floor moved and its largest move (a float's bits); the body whose held eye a head-camera frame showed last, and
 * when (tfp_camera_lock_eye_held_body). The game thread, under the policy lock, as the veil's. */
static TfpClear clear_state; static _Atomic uint32_t clear_on;
static _Atomic uint64_t clear_floored; static _Atomic uint32_t clear_largest;
static _Atomic uint32_t eye_held_body; static _Atomic uint64_t eye_held_at;
static _Atomic uint64_t orphan_frames, orphan_refused;
static struct { bool known; uint32_t mask, kind, weapon, key; double until; } smooth_watch;
/* Round 24 (src/camera_traversal.h): the body root's motion, for the head-camera frames of a clamber. Policy lock. */
static TfpRootMotion smooth_root; static TfpTraversalWatch damp_climb;
/* Round 24, THE SETTLE (tfp_camera_settle_arm): set by the binder, taken by the next frame; settle_armed_at: when (the
 * helper's clock; 0: not armed). Policy lock. */
#define SETTLE_ARM_MS 2000.0
static _Atomic uint32_t settle_arm, settle_on; static double settle_armed_at; /* settle_on: tfp_camera_set_settle (off by default) */
#define TRACE_RING 128u
#define TRACE_BEFORE 6u   /* frames before the edge that are written with it */
#define TRACE_MS 500.0
typedef struct TraceFrame { double tick; uint32_t kind, source; float eye[3], wanted[3], step, correction, entry_ease, root[3]; uint32_t what; float axes[9], damp[3], view[3], sock; uint32_t arms; float veil[3], turn[3], veil_head[2];
  float head_shown[2], clear; bool head_known; /* round 44 */
  float turn_carry, turn_edge, game_turn[3], game_cam[3]; bool game_cam_known; /* round 49 */
  float hold[3]; bool hold_known, dive; /* round 49 */ } TraceFrame;
/* Round 24, the log only: the native view's parent (the rig eye's axis rows) in the frame being presented, for the
 * direction shown (lock_rotation: the argument there is parent-local). trace_parent_tick: whose frame; policy lock. */
static float trace_parent[9]; static double trace_parent_tick = -1;
/* What the eye damping did in the frame being presented, for its trace row (lock_damp_eye writes, smooth_present
 * reads and clears): what it added to the eye in the world, whether it ran, and whether the body was off foot. That
 * last is asked only in a head-camera frame (on_foot()): Policy.lock_off_foot in a native frame is the answer of the
 * last head-camera frame, and is not written to a native row. And which way the view pointed (written by whoever
 * calls smooth_present; see TfpCameraTraceRow). Policy lock. */
static struct { float added[3], view[3]; bool ran, off_foot; } trace_damp;
static struct { TraceFrame frames[TRACE_RING]; uint64_t head, sent; double edge, until; float last[3]; double last_tick; bool last_known, open; } trace;
static TfpTraceSelf trace_self; /* the window the eye opens itself (src/trace_edges.h); policy lock */
/* The native eye is not remembered for its weapon while it is being moved about (set in the native view). */
static float eye_watch[3]; static bool eye_watch_known; static double eye_unsettled_until;
static void policy_init(Policy *p) {
  memset(p, 0, sizeof *p);
  tfp_correction_reset(&p->correction);
  p->turn.last = tfp_q_identity();
  p->forwarded_q = tfp_q_identity();
  p->native_q = tfp_q_identity();
  p->seated_q = tfp_q_identity();
}
/* cpp:77. The same start-up values, as a static initialiser. */
static Policy policy = {
  .correction = {.progress = 1, .handoff_duration_ms = 150},
  .turn = {.last = {0, 0, 0, 1}},
  .forwarded_q = {0, 0, 0, 1}, .native_q = {0, 0, 0, 1}, .seated_q = {0, 0, 0, 1},
};

/* cpp:78-86 */
typedef struct EyeMemory {
  uint32_t key; float delta[3];
  bool ads_known; float ads[3], ads_candidate[3]; uint32_t ads_frames;
  bool tpv_known; uint32_t tpv_node;
} EyeMemory;
static EyeMemory eye_memory[16]; static uint32_t eye_memory_next = 0; /* policy_lock; kept across leases */
/* cpp:87-90 */
static EyeMemory *eye_memory_find(uint32_t key) {
  if (key) for (unsigned i = 0; i < 16; ++i) if (eye_memory[i].key == key) return &eye_memory[i];
  return NULL;
}
/* cpp:91-96 */
static EyeMemory *eye_memory_entry(uint32_t key) {
  if (!key) return NULL;
  EyeMemory *entry = eye_memory_find(key);
  if (!entry) { entry = &eye_memory[eye_memory_next++ % 16]; memset(entry, 0, sizeof *entry); entry->key = key; }
  return entry;
}
/* Beyond retail: an entry made for the aim anchor alone has no eye yet. */
static bool eye_memory_recorded(uint32_t key) {
  const EyeMemory *entry = eye_memory_find(key);
  return entry && entry->delta[0]*entry->delta[0]+entry->delta[1]*entry->delta[1]+entry->delta[2]*entry->delta[2] > 1e-6f;
}
/* cpp:97-99 */
static void eye_memory_store(uint32_t key, const float *delta) {
  EyeMemory *entry = eye_memory_entry(key);
  if (entry) memcpy(entry->delta, delta, 12);
}

/* cpp:100-122. The three display floats are kept as their bit patterns in
 * lock-free 32-bit atomics (C has no always-lock-free guarantee for
 * _Atomic float); the values read back are the values stored. */
typedef struct Display { float blend, head_distance, last_dt; } Display;
static Display display_cache; /* control_lock only; never touched by callbacks. */
static _Atomic uint32_t display_sequence;
static _Atomic uint32_t display_blend, display_head_distance, display_last_dt;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && sizeof(float) == 4, "Camera diagnostics require lock-free floats");
static uint32_t float_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bits_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
/* cpp:107-113 */
static void publish_diagnostics(const Policy *p) {
  atomic_fetch_add_explicit(&display_sequence, 1, memory_order_seq_cst);
  atomic_store_explicit(&display_blend, float_bits(p->blend), memory_order_seq_cst);
  atomic_store_explicit(&display_head_distance, float_bits(p->head_distance), memory_order_seq_cst);
  atomic_store_explicit(&display_last_dt, float_bits(p->last_dt), memory_order_seq_cst);
  atomic_fetch_add_explicit(&display_sequence, 1, memory_order_seq_cst);
}
/* cpp:114-122 */
static void read_diagnostics(TfpCameraStats *s) {
  const uint32_t before = atomic_load_explicit(&display_sequence, memory_order_seq_cst);
  if ((before & 1) == 0) {
    Display next;
    next.blend = bits_float(atomic_load_explicit(&display_blend, memory_order_seq_cst));
    next.head_distance = bits_float(atomic_load_explicit(&display_head_distance, memory_order_seq_cst));
    next.last_dt = bits_float(atomic_load_explicit(&display_last_dt, memory_order_seq_cst));
    if (before == atomic_load_explicit(&display_sequence, memory_order_seq_cst)) display_cache = next;
  }
  s->blend = display_cache.blend; s->head_distance = display_cache.head_distance; s->last_dt = display_cache.last_dt;
}
/* cpp:139-151 */
static double now(void) { return host.now(host.user); }

/* cpp:152-163. Fault evidence belongs to one explicit owner-data guard.
 * The constructor/destructor pair is written out in owner_status. */
typedef struct OwnerReadProbe { struct OwnerReadProbe *previous; bool transport, unsafe; } OwnerReadProbe;
static _Thread_local OwnerReadProbe *owner_read_probe = NULL;
/* cpp:164-178 */
static bool readable_span(uint64_t at, size_t length) { return host.readable_span(host.user, at, length); }
/* cpp:179-204 */
static bool read(uint64_t at, void *out, size_t length) {
  if (at < 65536 || at >= HIGH || length == 0 || length > 4096 || at + length >= HIGH) {
    if (owner_read_probe) owner_read_probe->unsafe = true;
    return false;
  }
  const bool complete = host.read(host.user, at, out, length);
  if (complete) return true;
  if (owner_read_probe) {
    if (readable_span(at, length)) owner_read_probe->transport = true;
    else owner_read_probe->unsafe = true;
  }
  return false;
}
/* cpp:205 */
#define value(at, out) read((at), &(out), sizeof(out))
/* cpp:206-210 */
static bool pointer(uint64_t at, uint64_t *out) {
  if (!value(at, *out)) return false;
  if (*out >= 65536 && *out < HIGH) return true;
  if (owner_read_probe) owner_read_probe->unsafe = true;
  return false;
}
/* cpp:211 */
static void error(uint32_t code) { atomic_store_explicit(&last_error, code, memory_order_release); }
/* cpp:212 */
static uint32_t reject(uint32_t code) { error(code); return 0; }
/* cpp:213-219. IMAGE_DOS_HEADER is 64 bytes (e_magic +0, e_lfanew +0x3c);
 * IMAGE_NT_HEADERS64 is 264 (Signature +0, Machine +4, TimeDateStamp +8,
 * OptionalHeader.Magic +24, SizeOfImage +80). Same two reads, same sizes. */
static bool image(uint64_t base, uint32_t stamp, uint32_t length) {
  uint8_t dos[64] = {0}, nt[264] = {0};
  uint16_t e_magic = 0, machine = 0, magic = 0; int32_t e_lfanew = 0; uint32_t signature = 0, time_date_stamp = 0, size_of_image = 0;
  if (!read(base, dos, sizeof dos)) return false;
  memcpy(&e_magic, dos, 2); memcpy(&e_lfanew, dos + 0x3c, 4);
  if (!(e_magic == 0x5A4D && e_lfanew >= 64 && e_lfanew <= 4096)) return false;
  if (!read(base + (uint64_t)(int64_t)e_lfanew, nt, sizeof nt)) return false;
  memcpy(&signature, nt, 4); memcpy(&machine, nt + 4, 2); memcpy(&time_date_stamp, nt + 8, 4);
  memcpy(&magic, nt + 24, 2); memcpy(&size_of_image, nt + 80, 4);
  return signature == 0x00004550 && machine == 0x8664 && magic == 0x20b
      && time_date_stamp == stamp && size_of_image == length;
}
/* cpp:220-227 */
static uint8_t hex_digit(char c) { return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
static bool bytes(uint64_t at, const char *hex) {
  uint8_t actual[64] = {0}; const size_t count = strlen(hex) / 2;
  if (!count || count > sizeof actual || !read(at, actual, count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (actual[i] != (uint8_t)((hex_digit(hex[i*2]) << 4) | hex_digit(hex[i*2+1]))) return false;
  }
  return true;
}
/* cpp:228-232 */
static bool executable(uint64_t at) { return host.executable(host.user, at); }
/* cpp:233 */
static bool finite3(const float *p) { return isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]); }
/* cpp:234-236 */
static float distance(const float *a, const float *b) {
  const float x = a[0]-b[0], y = a[1]-b[1], z = a[2]-b[2]; return sqrtf(x*x+y*y+z*z);
}
/* cpp:237-244 */
static float dot3(const float *a, const float *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static bool orthonormal(const float *right, const float *forward, const float *up) {
  if (!finite3(right) || !finite3(forward) || !finite3(up)) return false;
  const float cross[3] = {right[1]*forward[2]-right[2]*forward[1], right[2]*forward[0]-right[0]*forward[2], right[0]*forward[1]-right[1]*forward[0]};
  return fabsf(dot3(right, right)-1) < .02f && fabsf(dot3(forward, forward)-1) < .02f && fabsf(dot3(up, up)-1) < .02f
      && fabsf(dot3(right, forward)) < .02f && fabsf(dot3(right, up)) < .02f && fabsf(dot3(forward, up)) < .02f
      && dot3(cross, up) > .98f;
}
/* cpp:245-252. capacity 0 = read it from the registry (retail's default argument). */
static bool resolve(const Policy *p, uint32_t ref, uint64_t *object, uint32_t capacity) {
  const uint32_t index = ref & 0x3fffff; uint8_t first = 0, last = 0; uint64_t again = 0;
  return (capacity || value(p->registry + L_REGISTRY_CAPACITY, capacity)) && capacity > 0 && capacity <= 0x400000 && index < capacity
      && value(p->generations + index, first) && first == (uint8_t)(ref >> 22)
      && pointer(p->units + (uint64_t)index*8, object)
      && value(p->generations + index, last) && last == first
      && pointer(p->units + (uint64_t)index*8, &again) && again == *object;
}
/* cpp:253-258 */
typedef struct RegistryBlock { uint64_t units, padding; uint32_t capacity, padding2; uint64_t generations; } RegistryBlock;
_Static_assert(sizeof(RegistryBlock) == 32, "registry read span");
static bool registry_block(uint64_t registry, RegistryBlock *b) {
  return read(registry + L_REGISTRY_BLOCK, b, sizeof *b) && b->units >= 65536 && b->units < HIGH
      && b->generations >= 65536 && b->generations < HIGH && b->capacity > 0 && b->capacity <= 0x400000;
}
/* cpp:259-308 */
static bool owner(Policy *p, bool capture) {
  const TfpCameraConfig *c = &p->c; uint64_t ctx = 0, camera = 0, avatar_owner = 0, controller = 0, descriptor = 0, registry = 0, units = 0, generations = 0;
  uint32_t count = 0, camera_unit = 0; uint8_t d[20] = {0}, flags[24] = {0}, controls[6] = {0}, context[12] = {0}; RegistryBlock block; memset(&block, 0, sizeof block);
  if (!pointer(c->game + B_GAME_CONTEXT, &ctx) || ctx != c->context || !read(ctx, context, sizeof context)) return false;
  memcpy(&camera, context + L_CONTEXT_CAMERA, 8); if (camera != c->camera) return false;
  memcpy(&camera_unit, context + L_CONTEXT_UNIT, 4); if (camera_unit != c->camera_unit) return false;
  if (!pointer(c->game + B_GAME_AVATAR_OWNER, &avatar_owner) || avatar_owner != c->avatar_owner
      || !pointer(c->game + B_GAME_CONTROLLER, &controller) || !read(controller, controls, 6)
      || !value(avatar_owner + L_AVATAR_COUNT, count) || count == 0 || count > 8 || c->avatar_index >= count
      || !pointer(avatar_owner + L_AVATAR_ROWS + (uint64_t)c->avatar_index*8, &descriptor) || !read(descriptor, d, 20)
      || !read(avatar_owner + B_AVATAR_FLAGS + (uint64_t)c->avatar_index*B_AVATAR_STRIDE, flags, 24)
      || !pointer(c->exe + B_EXE_REGISTRY, &registry) || !registry_block(registry, &block)) return false;
  units = block.units; generations = block.generations;
  uint32_t avatar = 0, body = 0, mask = 0; memcpy(&avatar, d + 8, 4); memcpy(&body, d + 12, 4); memcpy(&mask, controls, 4);
  p->control_mask = mask;
  if (avatar != c->avatar || body != c->body) return false;
  if (capture) { p->controller = controller; p->descriptor = descriptor; p->registry = registry; p->units = units; p->generations = generations; memcpy(p->descriptor_bytes, d, 20); }
  else if (controller != p->controller || descriptor != p->descriptor || registry != p->registry || units != p->units
      || generations != p->generations || memcmp(d, p->descriptor_bytes, 20) != 0) return false;
  uint64_t body_object = 0, camera_object = 0;
  if (!resolve(p, c->body, &body_object, block.capacity) || !resolve(p, c->camera_unit, &camera_object, block.capacity)) return false;
  /* Both references used the single bounded header read. Close that header
   * after their generation/object double-checks; never reuse a changed bound. */
  RegistryBlock closing; memset(&closing, 0, sizeof closing);
  if (!registry_block(registry, &closing) || closing.units != block.units || closing.generations != block.generations || closing.capacity != block.capacity) return false;
  if (capture) { p->body_object = body_object; p->camera_object = camera_object; }
  else if (body_object != p->body_object || camera_object != p->camera_object) return false;
  p->fire = (flags[L_FLAG_FIRE] & 0x40) != 0; p->full_ads = controls[L_CONTROL_AIM] != 0; p->shoulder = !p->full_ads && (flags[L_FLAG_SHOULDER_A] & 4) != 0 && (flags[L_FLAG_SHOULDER_B] & 32) != 0;
  const bool seated = (flags[L_FLAG_SEATED] & 4) != 0, available = controls[L_CONTROL_GUARD] == 0 && !seated;
  p->safe = (mask == 0 || mask == 2) && available;
  const uint32_t extra = atomic_load_explicit(&tuning_extra_mask, memory_order_relaxed);
  p->fallback_safe = (mask == 0 || mask == 2 || mask == 0x20 || (extra && mask == extra)) && available;
  p->seated_now = seated;
  if (p->hold_seated && seated && mask == 0 && controls[L_CONTROL_GUARD] == 0) p->fallback_safe = true;
  p->guard_only = mask == 0 && controls[L_CONTROL_GUARD] != 0 && (flags[L_FLAG_SEATED] & 4) == 0;
  return true;
}
/* cpp:309-315. OwnerReadProbe's constructor and destructor written out. */
static uint32_t owner_status(Policy *p, bool capture) {
  OwnerReadProbe probe; probe.previous = owner_read_probe; probe.transport = false; probe.unsafe = false;
  owner_read_probe = &probe;
  uint32_t result;
  if (owner(p, capture)) result = OK;
  else result = probe.transport && !probe.unsafe ? READ_FAILED : OWNER_MISMATCH;
  owner_read_probe = probe.previous;
  return result;
}
/* cpp:316-323 */
static bool top(Policy *p, uint32_t *kind) {
  uint32_t ring[2] = {0}, r[4] = {0};
  if (!read(p->c.context + L_CONTEXT_RING, ring, sizeof ring) || ring[0] >= 32 || ring[1] >= 32) return false;
  const uint32_t head = ring[0], tail = ring[1];
  const uint64_t at = head == tail ? p->c.context + L_CONTEXT_IDLE : p->c.context + L_CONTEXT_RECORDS + ((tail+31)%32)*L_CONTEXT_RECORD;
  if (!read(at, r, 16) || r[1] != p->c.avatar) return false;
  *kind = r[0] & 0xffff; if (*kind == 6) p->current_weapon = r[2];
  return *kind != 6 || (head != tail && r[3] == p->c.body);
}
/* cpp:324-344. The executable() test is retail's non-fixture path. */
static bool graph(Policy *p, bool capture) {
  uint64_t vt = 0, getter = 0, hashes = 0, links = 0; uint32_t count = 0; uint8_t header[56] = {0};
  if (!pointer(p->body_object, &vt) || !pointer(vt + L_VT_GRAPH_GETTER, &getter) || getter != p->c.exe + B_EXE_GRAPH_GETTER
      || !bytes(getter, B_HEX_GRAPH_GETTER) || !read(p->body_object + L_UNIT_HEADER, header, sizeof header)) return false;
  memcpy(&count, header, 4); memcpy(&links, header + L_HEADER_LINKS, 8); memcpy(&hashes, header + L_HEADER_HASHES, 8);
  if (count == 0 || count > 512 || links < 65536 || links >= HIGH || hashes < 65536 || hashes >= HIGH) return false;
  if (!executable(getter)) return false;
  if (capture) {
    uint32_t found = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) { uint32_t h = 0; if (!value(hashes + (uint64_t)i*4, h)) return false;
      if (h == L_HEAD_HASH) { if (found != UINT32_MAX) return false; found = i; } }
    if (found == UINT32_MAX) return false;
    p->hashes = hashes; p->links = links; p->count = count; p->head_node = found;
  } else if (hashes != p->hashes || links != p->links || count != p->count) return false;
  uint32_t head_hash = 0, link = 0, parent_hash = 0; const uint64_t offset = (uint64_t)p->head_node*4;
  if (!value(hashes + offset, head_hash) || head_hash != L_HEAD_HASH || !value(links + offset, link)
      || (link & 0xffff) != 1 || (link >> 16) >= count || !value(hashes + (uint64_t)(link >> 16)*4, parent_hash)
      || parent_hash != L_HEAD_PARENT_HASH) return false;
  return true;
}
/* cpp:345-352. root_basis may be NULL. */
/* Beyond retail, `lock` (the camera lock holds this frame, or is on while arming): physics can carry the skeleton
 * away from the unit root, which stays where the character fell or died (seen in the game 2026-11-22: 4.28 m). A
 * head beyond the 3 m is then taken when it is where its neck is: the parent node, whose link and name graph() has
 * just proven, 0.005 to 0.5 m from it in the same world array (the retired controller's HEAD_AT_NECK rule,
 * native_view.c:803-814). Without `lock`: retail's bound and reads. */
static bool points(Policy *p, float *root, float *head, float *root_basis, bool lock) {
  float matrix[16] = {0}; uint64_t matrices = 0;
  p->head_far = false; p->why = TFP_CAMERA_WHY_GRAPH;
  if (!graph(p, false)) return false;
  p->why = TFP_CAMERA_WHY_WORLDS;
  if (!pointer(p->body_object + L_UNIT_WORLDS, &matrices)
      || !read(matrices, matrix, 64) || !read(matrices + (uint64_t)p->head_node*L_MATRIX + L_MATRIX_POSITION, head, 12)
      || !orthonormal(matrix, matrix + 4, matrix + 8) || !finite3(matrix + 12) || !finite3(head)) return false;
  p->sampled_worlds = matrices; memcpy(root, matrix + 12, 12);
  if (root_basis) for (unsigned i = 0; i < 3; ++i) memcpy(root_basis + i*3, matrix + i*4, 12);
  p->head_distance = distance(root, head);
  if (isfinite(p->head_distance) && p->head_distance >= .05f && p->head_distance <= 3.0f) { p->why = TFP_CAMERA_WHY_NONE; return true; }
  p->why = TFP_CAMERA_WHY_RANGE;
  if (!lock || !(p->head_distance > 3.0f) || !isfinite(p->head_distance)) return false;
  p->head_far = true; p->why = TFP_CAMERA_WHY_NECK;
  uint32_t link = 0; float neck[3] = {0};
  if (!value(p->links + (uint64_t)p->head_node*4, link) || (link & 0xffff) != 1 || (link >> 16) >= p->count
      || !read(matrices + (uint64_t)(link >> 16)*L_MATRIX + L_MATRIX_POSITION, neck, 12) || !finite3(neck)) return false;
  const float apart = distance(head, neck);
  /* Seen in the game 2026-11-23 (a grenade in the hand): the head was refused here for the whole death, so the
   * game's death camera showed. A body torn by an explosion has its head off the neck or on it exactly; both are
   * still this unit's own head node in this unit's own world array. Refused only when it is nowhere near the body. */
  p->head_apart = apart;
  if (!(apart <= 3.0f) || !(p->head_distance <= 60.0f)) return false;
  p->why = TFP_CAMERA_WHY_NONE; return true;
}
/* Beyond retail, lock frames only. The avatar's ragdoll flag, and, for a body that is not on foot (on_foot() stored
 * nothing), this frame's animation chain for the final proof: what commit_current compares is then what this frame
 * saw, not an on-foot sample of another time. A chain that cannot be read at all (foot_sampled false) is not
 * compared: the body, its graph and the request are still closed by the final proof. */
static void lock_body(Policy *p) {
  uint8_t physics = 0;
  p->lock_ragdoll = value(p->c.avatar_owner + B_AVATAR_PHYSICS + (uint64_t)p->c.avatar_index*B_AVATAR_PHYSICS_STRIDE, physics) && (physics & 0x10) != 0;
  p->foot_sampled = true;
  if (!p->lock_off_foot) return;
  uint64_t instance = 0, g = 0, array = 0, current = 0;
  p->foot_sampled = pointer(p->body_object + L_UNIT_ANIM, &instance) && pointer(instance + L_ANIM_GRAPH, &g)
      && pointer(instance + L_ANIM_ACTIVE, &array) && pointer(array + L_ANIM_LAYER*8, &current);
  if (p->foot_sampled) { p->foot_instance = instance; p->foot_graph = g; p->foot_array = array; p->foot_current = current; }
}
/* cpp:353-405 */
static bool on_foot(Policy *p) {
  uint64_t vt = 0, getter = 0, resource = 0, instance = 0, g = 0, array = 0, current = 0;
  uint32_t layers = 0, table_offset = 0, layer_offset = 0, count = 0, state_offset = 0;
  if (!pointer(p->body_object, &vt) || !pointer(vt + L_VT_ANIM_GETTER, &getter) || getter != p->c.exe + B_EXE_ANIM_GETTER
      || !bytes(getter, B_HEX_ANIM_GETTER) || !value(p->body_object + L_UNIT_RESOURCE, resource) || resource != L_BODY_RESOURCE
      || !pointer(p->body_object + L_UNIT_ANIM, &instance) || !pointer(instance + L_ANIM_GRAPH, &g) || !value(g + L_ANIM_LAYER_COUNT, layers) || layers != L_ANIM_LAYERS
      || !value(g + L_ANIM_TABLE, table_offset) || table_offset < 12 || table_offset > 0x10000) return false;
  const uint64_t table = g + table_offset;
  if (!value(table + 4 + L_ANIM_LAYER*4, layer_offset) || layer_offset < 12 || layer_offset > 0x1000000) return false;
  const uint64_t definition = table + layer_offset;
  if (!value(definition + 8, count) || count <= L_STATE_ON_FOOT || count > 2048 || !value(definition + 12 + L_STATE_ON_FOOT*4, state_offset)
      || state_offset < 12 || state_offset > 0x1000000 || !pointer(instance + L_ANIM_ACTIVE, &array)
      || !pointer(array + L_ANIM_LAYER*8, &current)) return false;
  uint32_t state = L_STATE_ON_FOOT, role = 0; uint64_t settings = 0;
  const uint64_t role_address = p->c.avatar_owner + B_AVATAR_FLAGS + (uint64_t)p->c.avatar_index*B_AVATAR_STRIDE;
  if (current != definition + state_offset) {
    state = L_STATE_BRIDGE;
    uint32_t scene = 0;
    if (!value(definition + 12 + L_STATE_BRIDGE*4, state_offset) || state_offset < 12 || state_offset > 0x1000000
        || current != definition + state_offset || !value(role_address, role) || (role & 6) != 2
        || !pointer(p->c.game + B_GAME_STATE, &settings) || !value(settings + B_STATE_SCENE, scene) || scene != L_SCENE_SHIP) return false;
  }
  /* Match the native first-index lookup even when resources alias states. */
  uint32_t prefix[37] = {0};
  if (!read(definition + 12, prefix, (state+1)*sizeof(uint32_t)) || prefix[state] != state_offset) return false;
  for (uint32_t i = 0; i < state; ++i) if (prefix[i] < 12 || prefix[i] > 0x1000000 || prefix[i] == state_offset) return false;
  uint64_t again = 0; uint32_t check = 0;
  const bool stable = pointer(p->body_object + L_UNIT_ANIM, &again) && again == instance && pointer(instance + L_ANIM_GRAPH, &again) && again == g
      && pointer(instance + L_ANIM_ACTIVE, &again) && again == array && pointer(array + L_ANIM_LAYER*8, &again) && again == current
      && value(g + L_ANIM_LAYER_COUNT, check) && check == layers && value(g + L_ANIM_TABLE, check) && check == table_offset
      && value(table + 4 + L_ANIM_LAYER*4, check) && check == layer_offset && value(definition + 8, check) && check == count
      && value(definition + 12 + (uint64_t)state*4, check) && check == state_offset;
  if (!stable) return false;
  p->foot_instance = instance; p->foot_graph = g; p->foot_array = array; p->foot_current = current; p->foot_definition = definition;
  if (state == L_STATE_ON_FOOT) return true;
  /* The bridge exception is live state, never a cached mod toggle. Recheck
   * the role, scene owner and current body generation before substituting. */
  uint32_t current_role = 0, scene = 0; uint64_t current_resource = 0;
  return value(role_address, current_role) && current_role == role
      && pointer(p->c.game + B_GAME_STATE, &again) && again == settings && value(settings + B_STATE_SCENE, scene) && scene == L_SCENE_SHIP
      && pointer(p->c.exe + B_EXE_REGISTRY, &again) && again == p->registry
      && resolve(p, p->c.body, &again, 0) && again == p->body_object
      && value(p->body_object + L_UNIT_RESOURCE, current_resource) && current_resource == resource;
}
/* cpp:406-415. Local/world coincidence is mandatory: native kind6 uses another parent space. */
static bool unparented(const Policy *p) {
  uint64_t parent = 1, g = 0, locals = 0, worlds = 0; uint32_t index = 0, count = 0; float a[3] = {0}, b[3] = {0};
  if (!value(p->camera_object + L_UNIT_PARENT, parent) || parent != 0 || !pointer(p->c.camera + L_CAMERA_GRAPH, &g) || g != p->camera_object + L_UNIT_GRAPH
      || !value(p->c.camera + L_CAMERA_NODE, index) || !value(g + L_GRAPH_COUNT, count) || count == 0 || count > 512 || index >= count
      || !pointer(g + L_GRAPH_LOCALS, &locals) || !pointer(g + L_GRAPH_WORLDS, &worlds)
      || !read(locals + (uint64_t)index*L_MATRIX + L_LOCAL_POSITION, a, 12)
      || !read(worlds + (uint64_t)index*L_MATRIX + L_MATRIX_POSITION, b, 12) || !finite3(a) || !finite3(b)) return false;
  return distance(a, b) <= .01f;
}
/* Beyond retail: which half of unparented() refused, for the lock's Missed line (error 15 is both). The same reads
 * again, nothing else. `apart`: metres between the camera node's local and world position where both were read. */
static uint32_t space_why(const Policy *p, float *apart) {
  uint64_t parent = 0, g = 0, locals = 0, worlds = 0; uint32_t index = 0, count = 0; float a[3] = {0}, b[3] = {0};
  *apart = 0;
  if (value(p->camera_object + L_UNIT_PARENT, parent) && parent != 0) return TFP_CAMERA_WHY_LINKED;
  if (pointer(p->c.camera + L_CAMERA_GRAPH, &g) && value(p->c.camera + L_CAMERA_NODE, index) && value(g + L_GRAPH_COUNT, count) && index < count
      && pointer(g + L_GRAPH_LOCALS, &locals) && pointer(g + L_GRAPH_WORLDS, &worlds)
      && read(locals + (uint64_t)index*L_MATRIX + L_LOCAL_POSITION, a, 12) && read(worlds + (uint64_t)index*L_MATRIX + L_MATRIX_POSITION, b, 12)
      && finite3(a) && finite3(b)) *apart = distance(a, b);
  return TFP_CAMERA_WHY_SPACE;
}
/* Beyond retail, under the lock only: the camera has no parent and its world matrix is a little behind its local
 * position (seen in the game 2026-11-22: 0.423 m for two frames of a grenade thrown while walking; the game had not
 * worked the camera's world out again). What is written for the ordinary camera is the eye in the world, and a camera
 * without a parent takes it as that whatever its own world matrix still says, so the frame is held. Further apart
 * than LOCK_SPACE_M the two are not one camera a frame or two apart, and the frame is refused as before. */
#define LOCK_SPACE_M 1.0f
static bool unparented_behind(const Policy *p) {
  float apart = 0;
  return space_why(p, &apart) == TFP_CAMERA_WHY_SPACE && apart > 0 && apart <= LOCK_SPACE_M;
}
/* cpp:416-419 */
static bool held_axes(TfpQ q, float *forward, float *up) {
  const float y[3] = {0, 1, 0}, z[3] = {0, 0, 1}; tfp_q_rotate(q, y, forward); tfp_q_rotate(q, z, up);
  return finite3(forward) && finite3(up);
}
/* cpp:420-425 */
static bool basis(const Policy *p, float *forward, float *up) {
  float frame[11] = {0};
  if (!read(p->c.context + L_CONTEXT_BASIS, frame, sizeof frame)) return false;
  memcpy(forward, frame + 4, 12); memcpy(up, frame + 8, 12);
  return orthonormal(frame, forward, up);
}
/* cpp:426-431 */
static void reset(Policy *p, bool keep_native6) {
  p->tracking = false; p->blend = 0; p->last_dt = 0; p->forwarded_q_valid = false;
  if (!keep_native6) { p->native6_seen = false; tfp_correction_reset(&p->correction); p->relative_valid = false; tfp_ease_reset(&p->entry.ease); tfp_ease_reset(&p->turn.ease); p->native_q_valid = false; p->entry_waiting = false;
    p->native_eye_valid = false; p->eye_delta_active = false; }
  atomic_fetch_add_explicit(&resets, 1, memory_order_relaxed);
}
/* cpp:432-445. The alignment test stays here; the page test is the host's. */
static bool writable_slot(uint64_t at, uint64_t exe) {
  if ((at & 7) != 0) return false;
  return host.writable_slot(host.user, at, exe);
}
/* cpp:446-451 */
static bool slot_read(uint64_t at, uint64_t *out) {
  return value(at, *out);
}
/* cpp:452-464. On the global `slot`, as retail. */
static bool cas(uint64_t expected, uint64_t desired, uint64_t *observed) {
  return host.cas(host.user, slot, expected, desired, observed);
}
/* cpp:465, 1027 */
static uint64_t callback(void) { return host.hook; }
/* cpp:466-470 */
static bool dispatch_owned(const Policy *p) {
  uint64_t root = 0, api = 0, target = 0;
  return pointer(p->c.game + B_GAME_API_ROOT, &root) && pointer(root + L_API_CAMERA, &api) && api + L_API_SETTER == slot
      && slot_read(slot, &target) && target == callback();
}
/* cpp:476 */
typedef struct ParentProof { uint64_t parent, poses, links, hashes, parent_links; uint32_t ref, link; } ParentProof;
/* cpp:477-492 */
static bool parent_current(const Policy *p, const ParentProof *proof) {
  uint64_t again = 0; uint32_t check = 0;
  return value(p->c.camera + L_CAMERA_NODE, check) && check == 0
      && pointer(p->camera_object + L_UNIT_LINKS, &again) && again == proof->links
      && value(proof->parent + L_UNIT_HEADER, check) && check == L_RIG_NODES
      && pointer(p->camera_object + L_UNIT_PARENT, &again) && again == proof->parent
      && pointer(p->camera_object + L_UNIT_EXTERNAL, &again) && again == proof->poses
      && value(proof->links, check) && check == proof->link
      && value(p->c.avatar_owner + B_AVATAR_RIG + (uint64_t)p->c.avatar_index*B_AVATAR_STRIDE, check) && check == proof->ref
      && resolve(p, proof->ref, &again, 0) && again == proof->parent
      && pointer(proof->parent + L_UNIT_WORLDS, &again) && again == proof->poses
      && pointer(proof->parent + L_UNIT_LINKS, &again) && again == proof->parent_links
      && pointer(proof->parent + L_UNIT_HASHES, &again) && again == proof->hashes
      && value(proof->hashes + L_RIG_CAMERA_NODE*4, check) && check == L_RIG_CAMERA_HASH
      && value(proof->parent_links + L_RIG_CAMERA_NODE*4, check) && check == L_RIG_CAMERA_LINK;
}
/* cpp:493-518. position, proof and matrix_basis may be NULL. */
static bool direct_camera_parent(Policy *p, TfpQ *out, float *position, ParentProof *proof, float *matrix_basis) {
  uint64_t links = 0, parent = 0, expected = 0, poses = 0, external = 0, hashes = 0, parent_links = 0;
  uint32_t link = 0, ref = 0, count = 0, hash = 0, parent_link = 0, index = 0; uint8_t header[56] = {0};
  if (!value(p->c.camera + L_CAMERA_NODE, index) || index != 0
      || !pointer(p->camera_object + L_UNIT_LINKS, &links) || !value(links, link) || link != L_CAMERA_RIG_LINK
      || !pointer(p->camera_object + L_UNIT_PARENT, &parent)
      || !value(p->c.avatar_owner + B_AVATAR_RIG + (uint64_t)p->c.avatar_index*B_AVATAR_STRIDE, ref)
      || !resolve(p, ref, &expected, 0) || expected != parent
      || !read(parent + L_UNIT_HEADER, header, sizeof header)) return false;
  memcpy(&count, header, 4); memcpy(&poses, header + L_HEADER_WORLDS, 8);
  memcpy(&parent_links, header + L_HEADER_LINKS, 8); memcpy(&hashes, header + L_HEADER_HASHES, 8);
  if (count != L_RIG_NODES || hashes < 65536 || hashes >= HIGH || parent_links < 65536 || parent_links >= HIGH
      || poses < 65536 || poses >= HIGH || !value(hashes + L_RIG_CAMERA_NODE*4, hash) || hash != L_RIG_CAMERA_HASH
      || !value(parent_links + L_RIG_CAMERA_NODE*4, parent_link) || parent_link != L_RIG_CAMERA_LINK
      || !pointer(p->camera_object + L_UNIT_EXTERNAL, &external) || external != poses) return false;
  float matrix[16] = {0};
  if (!read(poses + L_RIG_CAMERA_NODE*L_MATRIX, matrix, 64) || !finite3(matrix + 12)) return false;
  if (matrix_basis) {
    if (!orthonormal(matrix, matrix + 4, matrix + 8)) return false;
    for (unsigned axis = 0; axis < 3; ++axis) memcpy(matrix_basis + axis*3, matrix + axis*4, 12);
  } else if (!tfp_matrix_q(matrix, out)) return false;
  if (position) memcpy(position, matrix + 12, 12);
  const ParentProof sampled = {parent, poses, links, hashes, parent_links, ref, link};
  if (proof) { *proof = sampled; return true; } /* caller closes after its remaining sampled reads */
  return parent_current(p, &sampled);
}
/* cpp:519-544 */
typedef struct PendingParent { uint64_t parent, poses; uint32_t path; } PendingParent;
static PendingParent pending_parent_default(void) { PendingParent v; v.parent = 0; v.poses = 0; v.path = 2; return v; }
static bool pending_camera_parent(Policy *p, float *position, float *matrix_basis, PendingParent *out) {
  uint64_t parent = 0, poses = 0, hashes = 0, parent_links = 0; uint32_t count = 0, hash = 0, parent_link = 0, index = 0, ref = 0; uint8_t header[56] = {0};
  *out = pending_parent_default();
  if (!value(p->c.camera + L_CAMERA_NODE, index) || index != 0 || !value(p->camera_object + L_UNIT_PARENT, parent)) return false;
  uint32_t path = 1;
  if (parent == 0) { path = 0;
    if (!value(p->c.avatar_owner + B_AVATAR_RIG + (uint64_t)p->c.avatar_index*B_AVATAR_STRIDE, ref) || !resolve(p, ref, &parent, 0)) return false;
  } else if (parent < 65536 || parent >= HIGH) return false;
  if (!read(parent + L_UNIT_HEADER, header, sizeof header)) return false;
  memcpy(&count, header, 4); memcpy(&poses, header + L_HEADER_WORLDS, 8);
  memcpy(&parent_links, header + L_HEADER_LINKS, 8); memcpy(&hashes, header + L_HEADER_HASHES, 8);
  float matrix[16] = {0};
  if (count != L_RIG_NODES || hashes < 65536 || hashes >= HIGH || parent_links < 65536 || parent_links >= HIGH
      || poses < 65536 || poses >= HIGH || !value(hashes + L_RIG_CAMERA_NODE*4, hash) || hash != L_RIG_CAMERA_HASH
      || !value(parent_links + L_RIG_CAMERA_NODE*4, parent_link) || parent_link != L_RIG_CAMERA_LINK
      || !read(poses + L_RIG_CAMERA_NODE*L_MATRIX, matrix, 64) || !finite3(matrix + 12) || !orthonormal(matrix, matrix + 4, matrix + 8)) return false;
  for (unsigned axis = 0; axis < 3; ++axis) memcpy(matrix_basis + axis*3, matrix + axis*4, 12);
  memcpy(position, matrix + 12, 12); out->parent = parent; out->poses = poses; out->path = path; return true;
}
/* cpp:545-551. The wielded weapon's root world position. Read-only. */
static bool weapon_position(Policy *p, uint32_t ref, float *position) {
  uint64_t object = 0, worlds = 0;
  if (!ref || ref == UINT32_MAX || ref == p->c.body || ref == p->c.camera_unit || !resolve(p, ref, &object, 0)
      || !pointer(object + L_UNIT_WORLDS, &worlds) || !read(worlds + L_MATRIX_POSITION, position, 12) || !finite3(position)) return false;
  uint64_t again = 0; return resolve(p, ref, &again, 0) && again == object;
}
/* cpp:552-558 */
static bool weapon_body_node(Policy *p, uint32_t ref, uint32_t *node) {
  uint64_t object = 0, parent = 0, links = 0, again = 0; uint32_t link = 0;
  if (!ref || ref == UINT32_MAX || !resolve(p, ref, &object, 0) || !value(object + L_UNIT_PARENT, parent) || parent != p->body_object
      || !pointer(object + L_UNIT_LINKS, &links) || !value(links, link) || (link & 0xffff) != 2 || (link >> 16) >= p->count) return false;
  *node = link >> 16; return resolve(p, ref, &again, 0) && again == object;
}
/* cpp:559-567 */
static bool anchor_source(Policy *p, const EyeMemory *known, uint32_t ref, float *position) {
  if (known->tpv_known && known->tpv_node < p->count) {
    uint64_t matrices = 0;
    if (pointer(p->body_object + L_UNIT_WORLDS, &matrices) && matrices == p->sampled_worlds
        && read(matrices + (uint64_t)known->tpv_node*L_MATRIX + L_MATRIX_POSITION, position, 12) && finite3(position)) return true;
  }
  return weapon_position(p, ref, position);
}
/* cpp:568-637.
 * reason: 1 input/root read, 2 parent proof, 3 owner, 4 unsafe mask, 5 request, 6 weapon/ADS changed, 7 late parent/root change. */
typedef struct NativeSample { bool valid, allow_pending, pending; uint32_t reason; float relative[3], root[16], parent_position[3], parent_basis[9]; } NativeSample;
static uint32_t remember_native(Policy *p, const float *native, double tick, bool capture, NativeSample *sample) {
  TfpQ parent = tfp_q_identity(); float parent_position[3] = {0}, local[3] = {0}, root[16] = {0}, parent_basis[9] = {0}; uint64_t matrices = 0; ParentProof proof; memset(&proof, 0, sizeof proof);
  const uint32_t weapon = p->current_weapon; uint8_t full_ads = 0;
  const bool inputs = capture && value(p->controller + L_CONTROL_AIM, full_ads)
      && read((uint64_t)(uintptr_t)native, local, 12) && finite3(local)
      && pointer(p->body_object + L_UNIT_WORLDS, &matrices) && read(matrices, root, 64)
      && orthonormal(root, root + 4, root + 8) && finite3(root + 12);
  bool sampled = inputs && direct_camera_parent(p, &parent, parent_position, &proof, parent_basis);
  PendingParent pending = pending_parent_default(); bool use_pending = false;
  if (inputs && !sampled && sample && sample->allow_pending) {
    use_pending = sampled = pending_camera_parent(p, parent_position, parent_basis, &pending);
    ++p->pending_paths[pending.path & 3];
  }
  if (sample) sample->reason = !inputs ? 1 : !sampled ? 2 : 0;
  /* Nothing is admitted until a single complete fresh owner/dispatch proof;
   * passive owner-loss health is retained even when optional sampling fails. */
  const uint32_t ownership = owner_status(p, false);
  if (ownership != OK || !dispatch_owned(p)) { tfp_correction_reset(&p->correction); if (sample) sample->reason = 3; return ownership != OK ? ownership : OWNER_MISMATCH; }
  uint64_t again = 0; uint32_t kind = 0;
  if (sample && !sample->reason) {
    if (!p->safe) sample->reason = 4;
    else if (!top(p, &kind) || kind != 6) sample->reason = 5;
    else if (p->current_weapon != weapon || p->full_ads != (full_ads != 0)) sample->reason = 6;
    else if (!pointer(p->body_object + L_UNIT_WORLDS, &again) || again != matrices) sample->reason = 7;
    else if (!use_pending && !parent_current(p, &proof)) sample->reason = 7;
  }
  if (use_pending) {
    /* Close the single-frame rig read: same rig, same pose table, same node. */
    PendingParent closing = pending_parent_default(); float ignored_position[3] = {0}, ignored_basis[9] = {0};
    const bool same = pending_camera_parent(p, ignored_position, ignored_basis, &closing) && closing.parent == pending.parent && closing.poses == pending.poses;
    if (!same) { ++p->pending_paths[3]; if (!sample->reason) sample->reason = 7; }
    if (!same || !p->safe || !top(p, &kind) || kind != 6 || p->current_weapon != weapon || p->full_ads != (full_ads != 0)
        || !pointer(p->body_object + L_UNIT_WORLDS, &again) || again != matrices) { tfp_correction_reset(&p->correction); return OK; }
    float offset[3] = {0}, relative[3] = {0};
    for (unsigned component = 0; component < 3; ++component) for (unsigned axis = 0; axis < 3; ++axis)
      offset[component] += local[axis]*parent_basis[axis*3+component];
    for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
      relative[axis] += (parent_position[component]+offset[component]-root[12+component])*root[axis*4+component];
    /* No history is recorded from an unproven link. */
    tfp_correction_reset(&p->correction);
    if (tfp_correction_finite(relative)) {
      sample->valid = sample->pending = true; memcpy(sample->relative, relative, 12); memcpy(sample->root, root, 64);
      memcpy(sample->parent_position, parent_position, 12); memcpy(sample->parent_basis, parent_basis, 36);
    }
    return OK;
  }
  if (!sampled || !p->safe || !top(p, &kind) || kind != 6 || p->current_weapon != weapon || p->full_ads != (full_ads != 0)
      || !pointer(p->body_object + L_UNIT_WORLDS, &again) || again != matrices || !parent_current(p, &proof)) { tfp_correction_reset(&p->correction); return OK; }
  float offset[3] = {0}, relative[3] = {0};
  for (unsigned component = 0; component < 3; ++component) for (unsigned axis = 0; axis < 3; ++axis)
    offset[component] += local[axis]*parent_basis[axis*3+component];
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
    relative[axis] += (parent_position[component]+offset[component]-root[12+component])*root[axis*4+component];
  memcpy(p->native_eye, relative, 12); p->native_eye_tick = tick; p->native_eye_valid = tfp_correction_finite(relative);
  if (sample && tfp_correction_remember(&p->correction, relative, p->current_weapon, tick)) {
    sample->valid = true; memcpy(sample->relative, relative, 12); memcpy(sample->root, root, 64);
    memcpy(sample->parent_position, parent_position, 12); memcpy(sample->parent_basis, parent_basis, 36);
  } else if (!sample) tfp_correction_remember(&p->correction, relative, p->current_weapon, tick);
  return OK;
}
/* cpp:638-660. Eye ease into native6. */
static bool ease_native_entry(Policy *p, const NativeSample *sample, const float *native, double tick, bool from_head, const float *head_relative, float *adjusted) {
  if (p->handoff_entry_ms <= 0 || !sample->valid || p->full_ads) {
    if (p->entry.ease.active || from_head) ++p->entry_rejected;
    tfp_ease_reset(&p->entry.ease); return false;
  }
  if (from_head) { if (tfp_handoff_position_begin(&p->entry, head_relative, tick, p->handoff_entry_ms)) ++p->entry_started; else ++p->entry_rejected; }
  if (!p->entry.ease.active) return false;
  float presented[3] = {0}, world[3] = {0}, local[3] = {0};
  if (!tfp_handoff_position_apply(&p->entry, sample->relative, tick, p->c.max_gap_ms, presented, 1.0f)) return false;
  for (unsigned component = 0; component < 3; ++component) { world[component] = sample->root[12+component];
    for (unsigned axis = 0; axis < 3; ++axis) world[component] += presented[axis]*sample->root[axis*4+component]; }
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
    local[axis] += (world[component]-sample->parent_position[component])*sample->parent_basis[axis*3+component];
  if (!read((uint64_t)(uintptr_t)native, adjusted, 16) || !finite3(local)) { tfp_ease_reset(&p->entry.ease); ++p->entry_rejected; return false; }
  memcpy(adjusted, local, 12);
  /* A quick exit continues from what was shown, not from the native eye. */
  if (!sample->pending) tfp_correction_remember(&p->correction, presented, p->current_weapon, tick);
  ++p->entry_frames; return true;
}
/* cpp:661-666 */
static bool rendered_rotation(const Policy *p, TfpQ *rendered) {
  float world[16] = {0}; uint64_t g = 0, worlds = 0; uint32_t index = 0, count = 0;
  return pointer(p->c.camera + L_CAMERA_GRAPH, &g) && g == p->camera_object + L_UNIT_GRAPH && value(p->c.camera + L_CAMERA_NODE, index)
      && value(g + L_GRAPH_COUNT, count) && count > 0 && count <= 512 && index < count && pointer(g + L_GRAPH_WORLDS, &worlds)
      && read(worlds + (uint64_t)index*L_MATRIX, world, 64) && tfp_matrix_q(world, rendered);
}
/* cpp:667-676 */
static void track_native_rotation(Policy *p, double tick) {
  TfpQ rendered = tfp_q_identity();
  if (!rendered_rotation(p, &rendered)) { p->native_q_valid = false; return; }
  p->native_q = tfp_q_normalised(rendered); p->native_q_tick = tick; p->native_q_valid = true;
}
/* cpp:677-708. View-direction ease out of native6. */
_Static_assert(sizeof(TfpQ) == 16, "quaternion argument layout");
static void ease_normal_rotation(Policy *p, const float *native, double tick, bool from_native) {
  if (p->handoff_rotation_ms <= 0) { tfp_ease_reset(&p->turn.ease); p->forwarded_q_valid = false; return; }
  TfpQ desired = tfp_q_identity(), rendered = tfp_q_identity();
  if (!read((uint64_t)(uintptr_t)native + L_ARGUMENT_ROTATION, &desired, 16) || !tfp_q_valid(desired)) {
    if (p->turn.ease.active || from_native) ++p->turn_rejected;
    tfp_ease_reset(&p->turn.ease); p->forwarded_q_valid = false; return;
  }
  const bool have_rendered = rendered_rotation(p, &rendered);
  /* The camera's world matrix is not a reliable native6 record on the first
   * normal4 frame; start from the view recorded on the last native6 frame. */
  const bool have_native = p->native_q_valid && tick >= p->native_q_tick && tick-p->native_q_tick <= p->c.max_gap_ms;
  if (p->forwarded_q_valid && have_rendered && tick >= p->forwarded_q_tick && tick-p->forwarded_q_tick <= p->c.max_gap_ms)
    tfp_rotation_proof_sample(&p->rotation_proof, p->forwarded_q, rendered);
  TfpQ out = desired;
  if (!p->rotation_proof.proven) { if (from_native) ++p->turn_rejected; tfp_ease_reset(&p->turn.ease); }
  else {
    if (from_native) {
      if (have_native && tfp_handoff_rotation_begin(&p->turn, p->native_q, tick, p->handoff_rotation_ms)) { ++p->turn_started;
        p->turn_start_gap = have_rendered ? tfp_degrees_between(p->native_q, rendered) : -1; }
      else ++p->turn_rejected;
    }
    if (p->turn.ease.active && tfp_handoff_rotation_apply(&p->turn, desired, tick, p->c.max_gap_ms, &out, 120.0f)) {
      memcpy((float *)(uintptr_t)native + L_ARGUMENT_ROTATION/4, &out, 16); ++p->turn_frames;
    } else out = desired;
  }
  p->forwarded_q = out; p->forwarded_q_tick = tick; p->forwarded_q_valid = true; p->native_q_valid = false;
}
/* cpp:709-732. Final mutable proof. */
static bool commit_current(Policy *p, uint32_t expected_kind, uint32_t *failure) {
  uint32_t kind = 0, count = 0, hash = 0, link = 0, parent_hash = 0; uint64_t hashes = 0, links = 0, matrices = 0, parent = 1;
  uint8_t header[56] = {0}; uint64_t foot = 0; *failure = OWNER_MISMATCH;
  /* Beyond retail: a body the lock holds off foot whose animation chain could not be read has nothing to compare
   * (lock_body); p->why says which proof refused (the reads and their order are retail's). */
  p->why = TFP_CAMERA_WHY_ANIMATION;
  if (!(p->lock_now && p->lock_off_foot && !p->foot_sampled)
      && (!pointer(p->body_object + L_UNIT_ANIM, &foot) || foot != p->foot_instance
      || !pointer(p->foot_instance + L_ANIM_GRAPH, &foot) || foot != p->foot_graph
      || !pointer(p->foot_instance + L_ANIM_ACTIVE, &foot) || foot != p->foot_array
      || !pointer(p->foot_array + L_ANIM_LAYER*8, &foot) || foot != p->foot_current)) return false;
  p->why = TFP_CAMERA_WHY_OWNER;
  const uint32_t ownership = owner_status(p, false); if (ownership != OK) { *failure = ownership; return false; }
  p->why = TFP_CAMERA_WHY_REQUEST;
  if (!(p->fallback_safe || p->lock_now || (p->guard_grace && p->guard_only)) || !dispatch_owned(p) || !top(p, &kind) || kind != expected_kind) return false;
  p->why = TFP_CAMERA_WHY_HEADER;
  if (!read(p->body_object + L_UNIT_HEADER, header, sizeof header)) return false;
  memcpy(&count, header, 4); memcpy(&matrices, header + L_HEADER_WORLDS, 8);
  memcpy(&links, header + L_HEADER_LINKS, 8); memcpy(&hashes, header + L_HEADER_HASHES, 8);
  if (!(count == p->count && matrices == p->sampled_worlds && links == p->links && hashes == p->hashes)) return false;
  p->why = TFP_CAMERA_WHY_NAMES;
  if (!(value(hashes + (uint64_t)p->head_node*4, hash) && hash == L_HEAD_HASH
      && value(links + (uint64_t)p->head_node*4, link) && (link & 0xffff) == 1 && (link >> 16) < count
      && value(hashes + (uint64_t)(link >> 16)*4, parent_hash) && parent_hash == L_HEAD_PARENT_HASH)) return false;
  p->why = TFP_CAMERA_WHY_PARENT;
  if (!(value(p->camera_object + L_UNIT_PARENT, parent) && parent == 0)) return false;
  p->why = TFP_CAMERA_WHY_NONE; return true;
}
/* cpp:733-746 observe_camera_parent: retired (NADSC_ObserveParent), not translated. */

/* Beyond retail. With the lock on: the view's rotation takes a kick from the head's own movement, and in a camera
 * that is not the game's first-person or ordinary one (a death camera) the direction is the last first-person one.
 * `native` + L_ARGUMENT_ROTATION is the rotation the game passes next: world for an unparented camera, parent-local
 * in the native view (kind 6), where a rotation in the view's own frame is applied the same way. */
static TfpQ lock_carry_step(float passed_ms) {
  if (lock_carry.w == 1 && lock_carry.x == 0 && lock_carry.y == 0 && lock_carry.z == 0) return lock_carry;
  const TfpQ next = tfp_slerp(lock_carry, tfp_q_identity(), 1-expf(-passed_ms/LOCK_CARRY_MS));
  return tfp_q_valid(next) && tfp_degrees_between(next, tfp_q_identity()) > .05f ? tfp_q_normalised(next) : tfp_q_identity();
}
static void lock_rotation(Policy *p, const float *native, uint32_t kind, double tick) {
  if (!p->lock_now) { lock_view_known = false; tfp_kick_reset(&lock_kick); lock_last_kick = tfp_q_identity(); lock_carry = tfp_q_identity(); lock_was_held = false; lock_first_known = false;
    tfp_turn_reset(&lock_turn); lock_game_cam_tick = -1; return; }
  float *argument = (float *)(uintptr_t)native + L_ARGUMENT_ROTATION/4;
  TfpQ given = tfp_q_identity(), view = tfp_q_identity(), head = tfp_q_identity(); float matrix[16] = {0}; uint64_t matrices = 0;
  const bool foreign = kind != 4 && kind != 6;
  /* held: the direction is not the game's but the last first-person one: a camera that is not the ordinary one, or
   * the ordinary one on a body that is down (its camera circles the body from outside). */
  const bool held = foreign || p->lock_down;
  atomic_fetch_add_explicit(&lock_frames, 1, memory_order_relaxed); atomic_store_explicit(&lock_last_kind, kind, memory_order_relaxed);
  if (foreign) atomic_fetch_add_explicit(&lock_foreign_frames, 1, memory_order_relaxed);
  /* (A frame that cannot be read is shown as the game gives it: a carry under way ends with it, and it is no start for
   * the next change. Left alone, the frame after snapped back to what was carried and eased again.) */
  if (!read((uint64_t)(uintptr_t)argument, &given, 16) || !tfp_q_valid(given)) { tfp_kick_reset(&lock_kick); lock_last_kick = tfp_q_identity(); tfp_turn_reset(&lock_turn); return; }
  given = tfp_q_normalised(given);
  if (kind == 6) {
    TfpQ rendered = tfp_q_identity(); /* last frame's view in the world, with last frame's kick in it */
    if (!rendered_rotation(p, &rendered)) { tfp_kick_reset(&lock_kick); lock_last_kick = tfp_q_identity(); tfp_turn_reset(&lock_turn); return; }
    view = tfp_q_normalised(tfp_q_mul(tfp_q_normalised(rendered), tfp_q_inv(lock_last_kick)));
    lock_carry = tfp_q_identity(); lock_was_held = false;
  } else if (held) {
    const float passed = lock_tick > 0 && tick > lock_tick ? (float)(tick-lock_tick) : 0;
    if (lock_first_known) {
      /* Coming from frames that showed the game's direction off foot, the difference is carried and eased out. */
      if (!lock_was_held) lock_carry = lock_view_known ? tfp_q_normalised(tfp_q_mul(tfp_q_inv(lock_first), lock_view)) : tfp_q_identity();
      else lock_carry = lock_carry_step(passed);
      view = tfp_q_normalised(tfp_q_mul(lock_first, lock_carry));
    }
    else if (lock_view_known) view = lock_view;
    else if (foreign) return;
    else view = given;
    lock_was_held = true;
  } else {
    /* The ordinary camera with the game's own direction. Straight after a held stretch (a ragdoll that was
     * survived) the difference to the held direction is carried and eased out, so the view does not turn in one frame. */
    if (lock_was_held && lock_view_known) lock_carry = tfp_q_normalised(tfp_q_mul(tfp_q_inv(given), lock_view));
    lock_was_held = false;
    view = given;
    if (lock_carry.w != 1 || lock_carry.x != 0 || lock_carry.y != 0 || lock_carry.z != 0) {
      lock_carry = lock_carry_step(lock_tick > 0 && tick > lock_tick ? (float)(tick-lock_tick) : 0);
      view = tfp_q_normalised(tfp_q_mul(given, lock_carry));
    }
  }
  /* Round 49, THE CARRY (src/camera_turn.h): at a change between the native view and the ordinary camera the game's
   * direction can be 26 to 73 degrees off the one shown the frame before. The difference is carried and eased out. The
   * native frame's direction in the world is the rig eye's axes times the argument (what turn= is written from), not
   * lock_view: the camera node's world matrix was 36 to 117 degrees off in orphaned frames. A held frame (a death
   * camera, a body that is down) has its own carry above and ends this one. */
  TfpQ turn = tfp_q_identity(), game = view; float turn_began = 0, game_position[4] = {0};
  if (held) tfp_turn_reset(&lock_turn);
  else {
    const bool known = kind != 6 || (trace_parent_tick == tick && tfp_turn_world(trace_parent, given, &game));
    /* Not in full aim: the sights stay on the eye there (as the damping and the continuity leave it alone). */
    if (p->full_ads) tfp_turn_drop(&lock_turn, game, known, kind, tick);
    else turn = tfp_turn_step(&lock_turn, game, known, kind, tick, p->c.max_gap_ms, &turn_began);
    if (kind != 6) view = tfp_turn_apply(view, turn);
    if (kind == 4 && read((uint64_t)(uintptr_t)native, game_position, 16) && finite3(game_position)) { memcpy(lock_game_cam, game_position, 12); lock_game_cam_tick = tick; }
  }
  lock_view = view; lock_view_known = true;
  if (kind == 6 || (kind == 4 && !held && !p->lock_off_foot)) { lock_first = view; lock_first_known = true; }
  const float dt = lock_tick > 0 && tick > lock_tick ? (float)(tick-lock_tick)/1000 : 0; lock_tick = tick;
  TfpQ kick = tfp_q_identity();
  if (pointer(p->body_object + L_UNIT_WORLDS, &matrices) && read(matrices + (uint64_t)p->head_node*L_MATRIX, matrix, 64) && tfp_matrix_q(matrix, &head)) {
    const bool thrown = held || (kind != 6 && p->lock_off_foot); /* a death camera, a body that is down, a dive: not an action on foot */
    const TfpCameraStyle style = tfp_camera_style(atomic_load_explicit(&lock_bodycam, memory_order_relaxed) != 0, lock_damp_ms);
    /* (Stable: no kick on foot, and one that is carried when a thrown body is up again is eased out by the same low-pass) */
    const TfpKickTuning tuning = tfp_kick_style_tuning(&style, thrown, lock_gain, lock_max, lock_thrown_gain, lock_thrown_max, lock_settle);
    kick = tfp_kick_step(&lock_kick, view, head, dt, &tuning); /* damped already: this is what is applied, and taken out again next frame */
  } else tfp_kick_reset(&lock_kick);
  const float size = tfp_degrees_between(kick, tfp_q_identity()); if (size > lock_peak) lock_peak = size;
  const TfpQ out = tfp_kick_apply(kind == 6 ? tfp_turn_apply(given, turn) : view, kick);
  lock_last_kick = kick;
  if (tfp_q_valid(out)) memcpy(argument, &out, 16);
  /* Round 24, the log only: the direction the frame is shown with (the rotation handed to the game, its forward axis in
   * the world; in the native view the argument is the rig eye's local one), on this frame's Trace row. The diagnosis
   * suspects a turn of 27 to 96 degrees in one frame at the hand-off to and from the head camera; nothing said it. */
  if (atomic_load_explicit(&trace_on, memory_order_relaxed) && trace.head && tfp_q_valid(out)) {
    TraceFrame *row = &trace.frames[(trace.head-1) % TRACE_RING];
    static const float world_axes[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const bool said = row->tick == tick && (kind != 6 || trace_parent_tick == tick);
    if (said) tfp_turn_forward(kind != 6 ? world_axes : trace_parent, out, row->turn);
    /* Round 49, the log only: what is carried in this frame, and on the frame a change of camera began a carry the
     * probe: the game's own direction (what turn= would have been), and where the game last put its ordinary camera
     * (in this frame at a leave of the native view; in the last ordinary frame at an entry). Why the game's two
     * cameras point apart is not known; these and the row's root axes (the body's heading) are what a cause can be
     * read from. */
    if (row->tick == tick) {
      row->turn_carry = tfp_turn_none(turn) ? 0 : tfp_turn_degrees(turn);
      row->turn_edge = turn_began;
      if (turn_began > 0) {
        const float ahead[3] = {0, 1, 0};
        tfp_q_rotate(game, ahead, row->game_turn);
        if (lock_game_cam_tick >= 0 && tick >= lock_game_cam_tick && tick-lock_game_cam_tick <= p->c.max_gap_ms) { memcpy(row->game_cam, lock_game_cam, 12); row->game_cam_known = true; }
      }
    }
  }
}

/* Round 55, THE DIRECTION SHOWN (src/camera_turn.h tfp_turn_peek). The head camera's eye was built along the context's
 * axes (basis()), and lock_rotation's carry was put on the rotation only after the eye was built. In the game those
 * axes are the game's own new direction in the frame of a leave of the native view and the direction shown the frame
 * before in every frame after it, so in the frame a carry began the eye, the push, the veil and head_shown went 0.18 m
 * and more along a direction up to 63 degrees off the one shown, and from the next frame along the one shown: a flick
 * across the view of about 0.49 m x 2 sin(theta/2), there and back (soak 67 #29, 11:13:22.390, built 62.0 degrees
 * against 117.3 shown, the step changed across the view by 0.519 m; the matrix dive-swap 0.366). Here the frame's turn
 * is worked out before the eye is built, from the state lock_rotation will start from and the rotation it will read:
 * true, with `shown` the rotation the frame will be shown with (less the kick), only where something is carried in it.
 * Otherwise false, and the eye is built as before, bit for bit. Not in a held frame (a body that is down: lock_view
 * already), full aim (nothing carried there), a seat or the glide out of one (seat_back turns the view itself there,
 * after lock_rotation), or any camera but the ordinary one. (Where the retail rotation handoff
 * eases the argument after the eye is built, ease_normal_rotation, the carry here is the one on the argument as it
 * stands now.) Policy lock. */
static bool lock_turn_preview(const Policy *p, const float *native, uint32_t kind, double tick, TfpQ *shown) {
  if (!p->lock_now || kind != 4 || p->lock_down || p->full_ads || seat_in || seat_out) return false;
  TfpQ given = tfp_q_identity();
  if (!read((uint64_t)(uintptr_t)native + L_ARGUMENT_ROTATION, &given, 16) || !tfp_q_valid(given)) return false;
  given = tfp_q_normalised(given);
  /* The view lock_rotation's ordinary branch gives is the game's, times what is left of a held stretch's carry
   * (lock_carry). That carry is never under way in a frame whose turn carry is: a native frame (the only start of a
   * turn carry) sets lock_carry to the identity, and a held frame resets lock_turn, so the first ordinary frame after
   * one has no turn state to carry from. So the game's own is the view here, the same to the bit wherever the peek
   * gives a carry. (Round 55's review: the held-stretch lines once here could change no frame; no test could see them.) */
  const TfpQ carry = tfp_turn_peek(&lock_turn, given, true, kind, tick, p->c.max_gap_ms);
  if (tfp_turn_none(carry)) return false;
  *shown = tfp_turn_apply(given, carry);
  return tfp_q_valid(*shown);
}

/* A head-camera frame the lock held was presented: the body's state for the "Lock:" and "Dead:" lines, and for
 * head hiding (tfp_camera_lock_off_foot_body). `head` is the head node in the world, read and proven this frame. */
static void lock_body_shown(Policy *p, uint32_t kind, const float *head, double tick) {
  atomic_store_explicit(&lock_body_off_foot, p->lock_off_foot ? 1u : 0u, memory_order_relaxed);
  atomic_store_explicit(&lock_body_ragdoll, p->lock_ragdoll ? 1u : 0u, memory_order_relaxed);
  atomic_store_explicit(&lock_body_distance, float_bits(p->head_distance), memory_order_relaxed);
  if (p->lock_off_foot || p->lock_down) {
    atomic_store_explicit(&lock_off_foot_body, p->c.body, memory_order_relaxed);
    atomic_store_explicit(&lock_off_foot_at, (uint64_t)tick, memory_order_relaxed);
  }
  if (!p->lock_down) return;
  lock_down_until = tick+LOCK_DOWN_MS;
  atomic_fetch_add_explicit(&lock_down_frames, 1, memory_order_relaxed); atomic_store_explicit(&lock_down_kind, kind, memory_order_relaxed);
  atomic_store_explicit(&lock_down_distance, float_bits(p->head_distance), memory_order_relaxed);
  for (unsigned i = 0; i < 3; ++i) atomic_store_explicit(&lock_down_head[i], float_bits(head[i]), memory_order_relaxed);
}

/* ---- beyond retail: continuity of the presented eye, and the trace (lock frames only) ---- */
static void smooth_forget(void) { tfp_continuity_reset(&smooth); memset(&alternation, 0, sizeof alternation); trace.last_known = false; trace_self.known = 0; eye_watch_known = false; eye_unsettled_until = 0;
  memset(&smooth_watch, 0, sizeof smooth_watch); memset(&smooth_root, 0, sizeof smooth_root); memset(&damp_climb, 0, sizeof damp_climb); settle_armed_at = 0; }
/* True: jumps are looked for in this native frame. Called once for every frame presented under the lock. */
static bool smooth_detect(const Policy *p, uint32_t kind, uint32_t source, const float *root, double tick) {
  const bool said = atomic_exchange_explicit(&smooth_event, 0, memory_order_relaxed) != 0;
  /* Round 24: a change of mask into, out of or within a clamber's or a vault's (bit 0x20) opens nothing by itself
   * (src/camera_traversal.h: a clamber's 0 -> 0x22 -> 0x20 kept the window open through the whole climb). */
  if (said || (smooth_watch.known && (tfp_traversal_mask_rearms(smooth_watch.mask, p->control_mask) || kind != smooth_watch.kind
      || p->current_weapon != smooth_watch.weapon || p->weapon_key != smooth_watch.key))) smooth_watch.until = tick+SMOOTH_EVENT_MS;
  smooth_watch.known = true; smooth_watch.mask = p->control_mask; smooth_watch.kind = kind; smooth_watch.weapon = p->current_weapon; smooth_watch.key = p->weapon_key;
  /* The head camera too (seen in the game 2026-11-22: 0.15 m in one frame inside a swap, when the held eye lets go
   * or starts for the incoming weapon's remembered eye). */
  /* ... but not a clamber's or a vault's while the body root moves (round 24, src/camera_traversal.h): the head's
   * crouch against a rising root was taken up as four jumps and the eye floated 0.6 m above the head. */
  const bool moving = tfp_root_motion_step(&smooth_root, root, tick, p->c.max_gap_ms);
  (void)tfp_traversal_recent(&damp_climb, p->control_mask, tick, p->c.max_gap_ms); /* kept in every frame; lock_damp_eye asks it */
  /* A re-seat can take several frames, and begin on the last frame of the time (seen in the game 2026-11-22: 0.08 m
   * a frame for three frames, 270 ms after the entry, with the view pitched down). A run of jumps that began inside
   * the time is looked at to its end; the run itself is bounded (TFP_CONTINUITY_RUN_MS), so this holds nothing open. */
  const bool in_run = smooth.run && tick >= smooth.run_started && tick-smooth.run_started <= TFP_CONTINUITY_RUN_MS;
  return tfp_traversal_detect(tick < smooth_watch.until || in_run, source == SMOOTH_HEAD, p->control_mask, moving);
}
/* The eye about to be presented, in the world; `root`/`root_basis` are the body root's position and axis rows.
 * `node`: in the head camera's own frame, the point its eye is built on (the head node as the head camera's
 * smoothing left it, before the eye damping), in the world; NULL in every other frame. True: `shown` differs from
 * `wanted` and is what to present. Always records the frame for the trace. */
static bool smooth_present(Policy *p, uint32_t kind, uint32_t source, const float *root, const float *root_basis, const float *wanted, const float *node,
                           double tick, float *shown) {
  float wanted_relative[3] = {0}, relative[3] = {0}, correction = 0, moved = 0; uint32_t what = 0; bool looked = false;
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
    wanted_relative[axis] += (wanted[component]-root[component])*root_basis[axis*3+component];
  memcpy(shown, wanted, 12);
  if (atomic_load_explicit(&smooth_on, memory_order_relaxed)) {
    /* Within the head camera only a change of camera is taken up: its eye turns about the head with the view. */
    /* A body that is down moves against the unit root, which stays behind: its head's own motion is not a jump. Nor
     * is the head's in a head-camera frame of a body that is off foot and not yet known to be down (a death that
     * never sets the ragdoll flag, the head still within 3 m; a throw's first frames): taken for a re-seated rig, it
     * left the eye behind the head for up to half a second. On foot the head-frame jump of a swap is still taken up,
     * and native frames are as before. smooth_detect is called first: it keeps the watch on every frame. */
    const bool detect = smooth_detect(p, kind, source, root, tick) && !p->lock_down && !(source == SMOOTH_HEAD && p->lock_off_foot);
    const uint64_t jumps = smooth.jumps, edges = smooth.edges, reseats = smooth.reseats, restarts = smooth.restarts, cuts = smooth.cuts; const bool began = !smooth.known;
    looked = detect;
    /* THE CUT (round 19, camera_continuity.h): in a head-camera frame of a body on foot the filter is also given the
     * head node the eye is built on, before the eye damping, so a pose cut out of an emote is taken up as a change of
     * camera. The node, not the eye: the eye is 0.18 m ahead of it along the level view, and a flick of the mouse swings
     * it round the node by up to 0.36 m in one frame against a root that does not turn with the view (round 19 review:
     * flicks of 95 and 180 degrees were cuts). Not off foot or down (a throw is the body's own motion, as above), not
     * in full aim or its gap (the sights stay on the eye; nor is the head known for the frame after), not in any other
     * camera of the lock (kind 5) or the orphaned native view (kind 6). */
    float head[3] = {0};
    const bool head_given = node && source == SMOOTH_HEAD && kind == 4 && !p->lock_down && !p->lock_off_foot && !p->full_ads && !p->ads_gap_active;
    if (head_given) for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
      head[axis] += (node[component]-root[component])*root_basis[axis*3+component];
    /* The eye in the world goes with it: the root re-seated by itself is not the eye jumping (camera_continuity.h). */
    /* round 24, THE SETTLE: the native view coming back on mask 0 (after a backpack is picked up, the gun drawn again)
     * holds the eye until the rig is steady against the root, then eases once. A test switch (tfp_camera_set_settle,
     * settle_on.txt), off by default: off, the filter is main's frame for frame. */
    if (atomic_exchange_explicit(&settle_arm, 0, memory_order_relaxed)) settle_armed_at = tick;
    const bool armed = settle_armed_at > 0 && tick >= settle_armed_at && tick-settle_armed_at <= SETTLE_ARM_MS;
    smooth.settle_ok = atomic_load_explicit(&settle_on, memory_order_relaxed) && (armed || smooth.settling) && source != SMOOTH_HEAD && p->control_mask == 0 && !p->full_ads;
    const uint64_t settles = smooth.settles;
    /* Round 55, THE ENTRY: a native frame straight after a head-camera frame whose eye shown was moved after the
     * continuity (the veil, being let out): the continuity is handed the eye shown, eye + that move, as the last frame's,
     * so the change of camera lets the rest out. The native view used to cut it in a frame (the matrix's
     * stand-up-prone-head, 11:09:12.265: a veil of 0.264 m, the step changed by 0.164 and then 0.300). */
    if (veil_shown_on && source != SMOOTH_HEAD && smooth.known && smooth.source == SMOOTH_HEAD && smooth.tick == veil_shown_tick && tick > smooth.tick) {
      for (unsigned axis = 0; axis < 3; ++axis) smooth.wanted[axis] += veil_shown_moved[axis];
    }
    veil_shown_on = false;
    correction = tfp_continuity_step_cut(&smooth, wanted_relative, wanted, root_basis, head_given ? head : NULL, source == SMOOTH_REPEAT ? SMOOTH_NATIVE : source,
                                         detect, p->full_ads, tick, p->c.max_gap_ms, &smooth_tuning, relative);
    if (smooth.settles != settles) settle_armed_at = 0; /* one settle for one arm */
    if (correction > 1e-6f && tfp_finite3(relative)) for (unsigned component = 0; component < 3; ++component) { shown[component] = root[component];
      for (unsigned axis = 0; axis < 3; ++axis) shown[component] += relative[axis]*root_basis[axis*3+component]; }
    else correction = 0;
    what = (detect ? TFP_CAMERA_TRACE_LOOKED : 0u) | (smooth.jumps != jumps ? TFP_CAMERA_TRACE_JUMP : 0u) | (smooth.edges != edges ? TFP_CAMERA_TRACE_EDGE : 0u)
        | (smooth.reseats != reseats ? TFP_CAMERA_TRACE_ROOT : 0u) | (smooth.restarts != restarts ? TFP_CAMERA_TRACE_RESTART : 0u)
        | (began ? TFP_CAMERA_TRACE_START : 0u) | (p->full_ads ? TFP_CAMERA_TRACE_QUIET : 0u) | (smooth.cuts != cuts ? TFP_CAMERA_TRACE_CUT : 0u)
        | (smooth.settling || smooth.settles != settles ? TFP_CAMERA_TRACE_SETTLE : 0u);
  }
  /* Counted only: the eye the filter was given, whether or not the continuity is on. */
  if (tfp_alternation_step(&alternation, wanted_relative, source == SMOOTH_REPEAT ? SMOOTH_NATIVE : source, p->full_ads, tick, p->c.max_gap_ms, &moved)) {
    ++alternation_seen.frames; alternation_seen.looked += looked ? 1u : 0u; alternation_seen.fire += p->fire ? 1u : 0u; alternation_seen.kind = kind;
    if (moved > alternation_seen.largest) alternation_seen.largest = moved;
  }
  if (atomic_load_explicit(&trace_on, memory_order_relaxed)) {
    TraceFrame *row = &trace.frames[trace.head++ % TRACE_RING];
    row->tick = tick; row->kind = kind; row->source = source; memcpy(row->eye, shown, 12); memcpy(row->wanted, wanted, 12);
    row->step = trace.last_known && tick >= trace.last_tick && tick-trace.last_tick <= p->c.max_gap_ms ? distance(shown, trace.last) : 0;
    row->correction = correction; memcpy(row->root, root, 12); memcpy(row->axes, root_basis, 36);
    row->what = what | (trace_damp.off_foot ? TFP_CAMERA_TRACE_OFF_FOOT : 0u) | (trace_damp.ran ? TFP_CAMERA_TRACE_DAMPED : 0u);
    memcpy(row->damp, trace_damp.added, 12); memcpy(row->view, trace_damp.view, 12);
    row->entry_ease = p->entry.ease.active && p->entry.ease.duration > 0 ? (float)fmin(1.0, fmax(0.0, (tick-p->entry.ease.started)/p->entry.ease.duration)) : 1;
    row->sock = 0; row->arms = 0; /* tfp_camera_trace_draw says them, after this frame's draw */
    memset(row->veil, 0, sizeof row->veil); /* the setter says it, after the veil (round 19) */
    memset(row->veil_head, 0, sizeof row->veil_head); /* and where the head was (round 26) */
    memset(row->head_shown, 0, sizeof row->head_shown); row->head_known = false; row->clear = 0; /* and from the eye shown (round 44) */
    memset(row->turn, 0, sizeof row->turn); /* lock_rotation says it, after the rotation is final (round 24) */
    row->turn_carry = row->turn_edge = 0; memset(row->game_turn, 0, sizeof row->game_turn); memset(row->game_cam, 0, sizeof row->game_cam); row->game_cam_known = false; /* and these (round 49) */
    memset(row->hold, 0, sizeof row->hold); row->hold_known = false; /* the head camera's branch says it */
    row->dive = atomic_load_explicit(&dive_on, memory_order_relaxed) != 0;
    if (trace.head-trace.sent > TRACE_RING) trace.sent = trace.head-TRACE_RING;
    /* The log only: the eye opens a window itself where its step changes by TFP_TRACE_SELF_M or more
     * (src/trace_edges.h: a melee has no State line and no change of action). In an open window the time goes on and
     * the window lasts TRACE_MS from here. */
    if (tfp_trace_self_step(&trace_self, shown, tick, p->c.max_gap_ms) >= TFP_TRACE_SELF_M) {
      row->what |= TFP_CAMERA_TRACE_SELF;
      if (!(trace.open && tick <= trace.until)) { trace.edge = tick; if (trace.head-trace.sent > TRACE_BEFORE+1u) trace.sent = trace.head-(TRACE_BEFORE+1u); }
      trace.until = tick+TRACE_MS; trace.open = true;
    }
  }
  memset(&trace_damp, 0, sizeof trace_damp); /* the next frame's is written before it is presented, or there is none */
  memcpy(trace.last, shown, 12); trace.last_tick = tick; trace.last_known = true;
  memcpy(veil_eye_last, shown, 12); veil_eye_tick = tick; veil_eye_known = true; /* round 32: a head-camera frame's is set again after the veil */
  return correction > 0;
}
/* Beyond retail (the camera damping option, and the camera style: Bodycam damps with that option's time constant
 * and 0.10 m of lag at most, Stable with its own 350 ms and 0.25 m, src/camera_damp.h): in a head-camera frame the
 * eye is at the head, so every movement of the head moves the view.
 * Here the head's place against the body root (`head`: as the retail smoothing left it, the
 * point the eye is built on) goes through a low-pass (src/camera_damp.h), in the root's frame (round 49 below: or, by a
 * test switch, in a level one), so the body's own travel is not held back, and the eye is moved by that lag. As with the retail smoothing, the eye's place in front
 * of and above the head is not filtered: it comes from this frame's view, so turning the view (or the body under
 * it) does not swing the eye. Only in the ordinary camera's frames (kind 4) of a body on foot that the lock holds:
 * a body that is down or off foot is followed, full aim keeps the sights on the eye, and the native view is the
 * game's own. When one of those begins while a lag is carried, the lag is let go over a few frames (40 ms), not in
 * one, and nothing new is damped meanwhile. A change of the camera style or of the camera damping option while a
 * lag is carried does not move the eye by itself either: a lower limit comes into force at a bounded rate, and a lag
 * the option leaves no damping for is let go at that rate. `eye` is the wanted eye in the world and takes the damped one, before
 * the continuity sees it. True: the state is kept for the next frame; the caller forgets it in every frame that
 * returns false or does not come here, so it starts from the true eye after those. */
static bool lock_damp_eye(Policy *p, uint32_t kind, const float *root, const float *root_basis, const float *view, const float *head, double tick, float *eye) {
  const TfpCameraStyle style = tfp_camera_style(atomic_load_explicit(&lock_bodycam, memory_order_relaxed) != 0, lock_damp_ms);
  const float damp_ms = style.eye_damp_ms;
  /* on_foot: a frame the damping is for. Without it the lag is let go on 40 ms; with it and no time constant
   * (Bodycam with the camera damping option at 0, come to while a lag was carried) more slowly, src/camera_damp.h. */
  /* Round 24: nor in a clamber or a vault, nor in the stand-up after it (src/camera_traversal.h): Stable's 0.25 m lag
   * held the eye above a head that crouched against a rising root, and below it as the body stood up. What is carried
   * is let go on 40 ms, as at full aim. */
  const bool climbing = tfp_traversal_recent(&damp_climb, p->control_mask, tick, p->c.max_gap_ms);
  /* Round 49: nor in a dive in the Stable style (camera_helper.h tfp_camera_set_dive): a dive is "on foot" to the
   * layer lock_off_foot reads, and Stable's lag with the continuity's carry held the eye 0.36 to 0.38 m above the rig
   * eye at the landing. Replayed over soak 60's dive + swap (19:38:33.9 to 34.6) without it: the float 0.355 m to
   * 0.142. Bodycam's 0.10 m follows the head as before. */
  const bool diving = atomic_load_explicit(&dive_on, memory_order_relaxed) != 0 && atomic_load_explicit(&lock_bodycam, memory_order_relaxed) == 0;
  const bool on_foot = tfp_traversal_damps(kind == 4 && !p->lock_down && !p->lock_off_foot && !p->full_ads && !p->ads_gap_active, climbing || diving);
  const bool damp = on_foot && damp_ms > 0;
  trace_damp.off_foot = p->lock_off_foot;
  float relative[3] = {0}, damped[3] = {0}, behind[3] = {0}, lag = 0;
  /* Round 49, a test switch (tfp_camera_set_damp_frame, damp_frame_on.txt), off by default: not in the root's own axes
   * but in a level frame that turns with the root only as far as the `view` turns the same way (src/camera_damp.h
   * tfp_damp_frame_step): a root that turns under a still view (a get-up from lying on the back, 16.5 degrees a
   * frame) swung the lag round with it, 0.31 m in a frame. It starts with the damping. Off, `frame` is the root's
   * axes and every frame is main's, bit for bit. */
  float frame[9]; bool own = true;
  if (!lock_eye_damp.known) tfp_damp_frame_reset(&lock_damp_frame);
  if (atomic_load_explicit(&damp_frame_on, memory_order_relaxed)) own = tfp_damp_frame_step(&lock_damp_frame, root_basis, view, tick, p->c.max_gap_ms, frame);
  else memcpy(frame, root_basis, sizeof frame);
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
    relative[axis] += (head[component]-root[component])*frame[axis*3+component];
  if (damp) {
    if (!tfp_finite3(relative)) return false;
    lag = tfp_eye_damp_follow(&lock_eye_damp, relative, tick, p->c.max_gap_ms, damp_ms, style.eye_lag_m, damped);
    for (unsigned axis = 0; axis < 3; ++axis) behind[axis] = damped[axis]-relative[axis];
    trace_damp.ran = true;
  } else if (!((lag = on_foot ? tfp_eye_damp_release_slowly(&lock_eye_damp, tick, p->c.max_gap_ms, behind)
                              : tfp_eye_damp_release(&lock_eye_damp, tick, p->c.max_gap_ms, behind)) > 0)) return false;
  if (lag > 0 && tfp_finite3(behind)) {
    for (unsigned component = 0; component < 3; ++component) { float added = 0;
      for (unsigned axis = 0; axis < 3; ++axis) added += behind[axis]*frame[axis*3+component];
      eye[component] += added; trace_damp.added[component] = added; }
    /* where the eye is shown: what the way into the native view starts from (in the root's axes) */
    for (unsigned axis = 0; axis < 3; ++axis) { float in_root = behind[axis];
      if (!own) { in_root = 0; for (unsigned component = 0; component < 3; ++component) in_root += trace_damp.added[component]*root_basis[axis*3+component]; }
      p->last_relative[axis] += in_root; }
  }
  return true;
}
/* The rig eye the camera is (or is about to be) a child of, read and closed as remember_native does. */
static bool smooth_parent(Policy *p, float *position, float *parent_basis) {
  TfpQ ignored = tfp_q_identity(); ParentProof proof; memset(&proof, 0, sizeof proof);
  if (direct_camera_parent(p, &ignored, position, &proof, parent_basis)) return parent_current(p, &proof);
  PendingParent first = pending_parent_default(), closing = pending_parent_default(); float again[3] = {0}, again_basis[9] = {0};
  return pending_camera_parent(p, position, parent_basis, &first) && pending_camera_parent(p, again, again_basis, &closing)
      && closing.parent == first.parent && closing.poses == first.poses;
}
/* A frame whose position argument is parent-local (the native view, or its last argument repeated). `given` is the
 * 16-byte argument about to be forwarded; true: `adjusted` holds the argument to forward instead. `sample`, when
 * valid, already holds this frame's proven body root and rig eye; otherwise they are read here (an action mask the
 * retail sample refuses: a stim). Nothing is read without the lock. */
static bool smooth_native(Policy *p, const NativeSample *sample, const float *given, uint32_t kind, uint32_t source, double tick, float *adjusted) {
  float local[4] = {0}, root[16] = {0}, position[3] = {0}, parent_basis[9] = {0}, root_basis[9] = {0}, wanted[3] = {0}, shown[3] = {0}; uint64_t matrices = 0;
  if (!read((uint64_t)(uintptr_t)given, local, 16) || !finite3(local)) return false;
  if (sample && sample->valid) { memcpy(root, sample->root, 64); memcpy(position, sample->parent_position, 12); memcpy(parent_basis, sample->parent_basis, 36); }
  else if (!pointer(p->body_object + L_UNIT_WORLDS, &matrices) || !read(matrices, root, 64) || !orthonormal(root, root + 4, root + 8) || !finite3(root + 12)
      || !smooth_parent(p, position, parent_basis)) return false;
  for (unsigned axis = 0; axis < 3; ++axis) memcpy(root_basis + axis*3, root + axis*4, 12);
  for (unsigned component = 0; component < 3; ++component) { wanted[component] = position[component];
    for (unsigned axis = 0; axis < 3; ++axis) wanted[component] += local[axis]*parent_basis[axis*3+component]; }
  if (finite3(parent_basis + 3)) memcpy(trace_damp.view, parent_basis + 3, 12); /* the rig eye's forward axis: for the trace only */
  memcpy(trace_parent, parent_basis, 36); trace_parent_tick = tick; /* round 24: the direction shown, for the trace only */
  if (!finite3(wanted) || !smooth_present(p, kind, source, root + 12, root_basis, wanted, NULL, tick, shown)) return false;
  float moved[3] = {0};
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
    moved[axis] += (shown[component]-position[component])*parent_basis[axis*3+component];
  if (!finite3(moved)) return false;
  memcpy(local, moved, 12); memcpy(adjusted, local, 16); return true;
}

/* A native frame the binder called orphaned (orphan_on): the head camera's eye in place of the rig's. `given` is the
 * 16-byte parent-local argument about to be forwarded; true: `adjusted` holds the argument to forward instead. The
 * point is the head camera's (the kind 4 branch below: the head, c.forward along the level view direction, c.up),
 * with the direction this frame is shown with, so the first head-camera frame after the game's exit wants the same
 * point and the continuity sees no change of camera there. Any read or proof that fails: false, and the frame goes
 * the way of every other native frame (smooth_native).
 * Round 49: that direction is THE DIRECTION of src/camera_turn.h, the rig eye's axes times the rotation the game
 * passes (`native` + L_ARGUMENT_ROTATION, as the game gave it). Until then it was lock_view, the camera node's world
 * matrix less the kick, which in these frames is not the view: 36, 90 and 117 degrees off the rows either side in
 * all six orphan runs of soaks 60 and 62, the eye up to 0.10 m behind the head node, a jump of 0.25 to 0.31 m taken
 * at the leave and the veil pushing. The rig eye's axes go to trace_parent, so the rows carry turn= and lock_rotation
 * knows the frame's direction for THE CARRY. */
static bool orphan_head(Policy *p, const float *native, const float *given, uint32_t kind, double tick, float *adjusted) {
  float local[4] = {0}, root[3] = {0}, head[3] = {0}, root_basis[9] = {0}, forward[3] = {0}, position[3] = {0}, parent_basis[9] = {0};
  TfpQ rotation = tfp_q_identity();
  const uint32_t why = p->why; const bool far = p->head_far;
  const bool pointed = points(p, root, head, root_basis, false);
  p->why = why; p->head_far = far; /* the head camera's own diagnostics are not this frame's */
  if (!pointed || !read((uint64_t)(uintptr_t)native + L_ARGUMENT_ROTATION, &rotation, 16) || !tfp_q_valid(rotation)
      || !read((uint64_t)(uintptr_t)given, local, 16) || !finite3(local) || !smooth_parent(p, position, parent_basis)) return false;
  tfp_turn_forward(parent_basis, tfp_q_normalised(rotation), forward);
  if (!finite3(forward)) return false;
  memcpy(trace_parent, parent_basis, 36); trace_parent_tick = tick;
  float horizontal[3] = {forward[0], forward[1], 0};
  float length = sqrtf(horizontal[0]*horizontal[0]+horizontal[1]*horizontal[1]);
  if (length < .0001f) { horizontal[0] = root_basis[3]; horizontal[1] = root_basis[4]; length = sqrtf(horizontal[0]*horizontal[0]+horizontal[1]*horizontal[1]); }
  if (!(length >= .0001f)) return false;
  horizontal[0] /= length; horizontal[1] /= length;
  float wanted[3] = {0}, shown[3] = {0}, moved[3] = {0}, behind = 0;
  for (unsigned i = 0; i < 3; ++i) {
    wanted[i] = head[i]+horizontal[i]*p->c.forward+(i == 0 ? horizontal[1]*p->side : i == 1 ? -horizontal[0]*p->side : p->c.up);
    float game = position[i]; for (unsigned axis = 0; axis < 3; ++axis) game += local[axis]*parent_basis[axis*3+i];
    behind += (wanted[i]-game)*horizontal[i]; /* how far behind the head camera's eye the game put its own */
  }
  if (!finite3(wanted) || !isfinite(behind)) return false;
  memcpy(trace_damp.view, forward, 12);
  smooth_present(p, kind, SMOOTH_HEAD, root, root_basis, wanted, NULL, tick, shown);
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
    moved[axis] += (shown[component]-position[component])*parent_basis[axis*3+component];
  if (!finite3(moved)) return false;
  memcpy(local, moved, 12); memcpy(adjusted, local, 16);
  atomic_store_explicit(&orphan_behind, float_bits(behind), memory_order_relaxed);
  return true;
}

/* Beyond retail, round 14 (tfp_camera_set_veil). */
/* A world begun or ended: the kind of the last frame and an open veil belong to the old one (a native frame there
 * followed by the new world's first head-camera frame is no leave). The counts stay. */
static void veil_forget(void) {
  atomic_store_explicit(&veil_last_kind, 0, memory_order_relaxed); atomic_store_explicit(&veil_open, 0, memory_order_relaxed);
  atomic_store_explicit(&veil_ticks, 0, memory_order_relaxed); atomic_store_explicit(&veil_pending, 0, memory_order_relaxed);
  veil_out_on = false; veil_in_on = false; veil_in_frames = 0; veil_eye_known = false; veil_carry_on = false;
  veil_shown_on = false; /* round 55 */
  clear_state = (TfpClear){0}; /* round 44 */
}
/* Round 32: a kept leave's frame (veil_same_frame_leave) whose eye is more than tfp_camera_veil_let_in(1) from the eye
 * the game was last handed (`seen`, at `seen_tick`; the native frame of full aim of the same game frame) is clamped
 * to that from it, and what the clamp held back is let out over the head-camera frames after it (veil_carry_step).
 * Main showed that frame the whole way at once when nothing held the native eye and the continuity, in full aim, took
 * nothing up (soak 37, 10:07:04.745: 0.425 m). Within the hold (the eye kept at the native one) the step is under
 * the cap and nothing changes. `eye`: the frame's eye after everything else, taken. True: clamped. */
static bool veil_clamp_kept(const float *seen, double seen_tick, double tick, uint32_t max_gap_ms, float *eye) {
  if (!(tick >= seen_tick && tick-seen_tick <= max_gap_ms) || !finite3(seen) || !finite3(eye)) return false;
  const float d[3] = {eye[0]-seen[0], eye[1]-seen[1], eye[2]-seen[2]}, length = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]), most = tfp_camera_veil_let_in(1);
  if (!(length > most) || !(length <= TFP_CAMERA_VEIL_CARRY_FAR_M)) return false;
  float clamped[3];
  for (unsigned i = 0; i < 3; ++i) clamped[i] = seen[i]+d[i]*(most/length);
  if (!finite3(clamped)) return false;
  for (unsigned i = 0; i < 3; ++i) { veil_carry[i] = clamped[i]-eye[i]; eye[i] = clamped[i]; }
  veil_carry_tick = tick; veil_carry_on = true; atomic_fetch_add_explicit(&veil_clamped, 1, memory_order_relaxed);
  return true;
}
/* Round 32: a head-camera frame after a clamped kept leave: what the clamp held back, let out
 * (tfp_camera_veil_let_out from the clamped frame), added to the eye after the veil. */
static void veil_carry_step(double tick, float *eye) {
  if (!veil_carry_on || !(tick > veil_carry_tick)) return;
  const float w = tfp_camera_veil_let_out(tick-veil_carry_tick);
  if (!(w > 0)) { veil_carry_on = false; return; }
  for (unsigned i = 0; i < 3; ++i) eye[i] += veil_carry[i]*w;
}
/* Round 30 (camera_helper.h TFP_CAMERA_VEIL_SAME_FRAME_MS): a leave noted in a head-camera frame evaluated within that
 * time of a native frame of full aim, once the frame's control mask is read (`mask`; UINT32_MAX: not read). No action
 * owns the view (0): the game's own camera at the aim's release, seen to go straight back to the native view; the leave
 * is kept pending, so this frame is not moved and the tick that sees it opens the veil for the head-camera frames after
 * it, if any. Any other mask: a leave as every other, opened here. True: the veil is open for this frame. */
static bool veil_same_frame_leave(uint32_t mask) {
  if (mask != 0 && mask != UINT32_MAX) {
    atomic_store_explicit(&veil_open, 1, memory_order_relaxed);
    atomic_store_explicit(&veil_ticks, 0, memory_order_relaxed); atomic_fetch_add_explicit(&veil_leaves, 1, memory_order_relaxed);
    return true;
  }
  atomic_store_explicit(&veil_pending, 1, memory_order_relaxed); atomic_fetch_add_explicit(&veil_held_back, 1, memory_order_relaxed);
  veil_in_on = true; veil_in_frames = 0; /* if the view stays away, the veil's move is let in (TFP_CAMERA_VEIL_LET_IN_M) */
  return false;
}
/* The eye of a head-camera frame while the veil is open: at least TFP_CAMERA_VEIL_CLEAR in front of the head along
 * `horizontal` (the level view direction, unit length, z 0). `move`: a body on foot; otherwise only looked at.
 * true: the eye is that far in front (moved there, or already). */
/* Round 26: the least move along `horizontal` after which the head (TFP_CAMERA_VEIL_HEAD_M behind the node to
 * TFP_CAMERA_VEIL_CLEAR in front, TFP_CAMERA_VEIL_HEAD_M up, down and to either side) is wholly below the bottom edge
 * of the view from the eye; 0: it is already; INFINITY: no move along the view does it. `ahead`: the head node in
 * front of the eye along `horizontal`; `forward`: the view direction. Worked in the vertical plane of the view: the
 * bottom edge is the line through the eye at the view's pitch less half the vertical field of view (and
 * TFP_CAMERA_VEIL_FOV_SPARE); it is a plane holding the camera's right axis, so how far to the side the head is does
 * not matter (no roll in this camera). Out of view below it: bx*z - bz*x < 0 for every corner (x along the view, z up,
 * from the eye; (bx, bz) the edge's direction), so for its worst corner. */
static float veil_under_move(const float *head, const float *forward, const float *eye, float ahead) {
  const float fov_given = bits_float(atomic_load_explicit(&veil_fov, memory_order_relaxed));
  const float half = (fov_given > 0 ? fov_given : TFP_CAMERA_VEIL_FOV_MOST)/2+TFP_CAMERA_VEIL_FOV_SPARE;
  const float flat = sqrtf(forward[0]*forward[0]+forward[1]*forward[1]), edge = atan2f(forward[2], flat)-fminf(half, 1.55f);
  const float drop = eye[2]-head[2], r = TFP_CAMERA_VEIL_HEAD_M;
  if (!isfinite(edge) || !isfinite(drop) || !(edge > -1.55f)) return INFINITY; /* looking (nearly) straight down: the head is in view */
  const float bx = cosf(edge), bz = sinf(edge);
  if (bz < 0) { /* the edge points down: the worst corner is the head's top front, `ahead`+CLEAR along, r above the node */
    if (!(drop > r)) return INFINITY;
    const float move = ahead+TFP_CAMERA_VEIL_CLEAR-bx*(drop-r)/(-bz);
    return move > 0 ? move+.005f : 0; /* 5 mm past the edge, for rounding */
  }
  /* The edge points up (the view raised past half its field): the worst corner is the top back, and a move forward
   * only brings it into view. */
  return bx*(r-drop)-bz*(ahead-r) < 0 ? 0 : INFINITY;
}
static bool veil_apply(const float *head, const float *horizontal, const float *forward, bool move, float *eye) {
  const float ahead = (eye[0]-head[0])*horizontal[0]+(eye[1]-head[1])*horizontal[1];
  if (!isfinite(ahead) || !finite3(eye)) return false;
  if (ahead >= TFP_CAMERA_VEIL_CLEAR) return true;
  if (!move) return false;
  const float whole = TFP_CAMERA_VEIL_CLEAR-ahead, under = finite3(forward) ? veil_under_move(head, forward, eye, -ahead) : INFINITY;
  const float step = fminf(whole, under);
  /* Round 26: over TFP_CAMERA_VEIL_MOST the body has left the camera: no move (camera_helper.h). */
  if (!(step <= TFP_CAMERA_VEIL_MOST)) { atomic_fetch_add_explicit(&veil_far, 1, memory_order_relaxed); return false; }
  if (step < whole) atomic_fetch_add_explicit(&veil_under, 1, memory_order_relaxed);
  if (!(step > 0)) return true; /* the head is below the view already */
  const float moved[3] = {eye[0]+horizontal[0]*step, eye[1]+horizontal[1]*step, eye[2]};
  if (!finite3(moved)) return false;
  memcpy(eye, moved, 12);
  atomic_fetch_add_explicit(&veil_moved, 1, memory_order_relaxed);
  if (step > bits_float(atomic_load_explicit(&veil_largest, memory_order_relaxed))) atomic_store_explicit(&veil_largest, float_bits(step), memory_order_relaxed);
  return true;
}
/* ---- Round 38: first person in an exosuit's or vehicle's seat under the lock (src/camera_vehicle.h). Policy lock. ---- */
static void seat_note(const float *eye, TfpQ q, double tick) {
  if (!finite3(eye) || !tfp_q_valid(q)) { seat_from_known = false; return; }
  memcpy(seat_from, eye, 12); seat_from_q = tfp_q_normalised(q); seat_from_tick = tick; seat_from_known = true;
}
static bool seat_from_fresh(const Policy *p, double tick) { return seat_from_known && tick >= seat_from_tick && tick-seat_from_tick <= p->c.max_gap_ms; }
/* The native view: the camera in the world as the engine drew it last (its argument is the rig's, parent-local). */
static void seat_note_rendered(const Policy *p, double tick) {
  float world[16] = {0}; uint64_t g = 0, worlds = 0; uint32_t index = 0, count = 0; TfpQ q = tfp_q_identity();
  if (pointer(p->c.camera + L_CAMERA_GRAPH, &g) && g == p->camera_object + L_UNIT_GRAPH && value(p->c.camera + L_CAMERA_NODE, index)
      && value(g + L_GRAPH_COUNT, count) && count > 0 && count <= 512 && index < count && pointer(g + L_GRAPH_WORLDS, &worlds)
      && read(worlds + (uint64_t)index*L_MATRIX, world, 64) && tfp_matrix_q(world, &q)) seat_note(world + L_MATRIX_POSITION/4, q, tick);
  else seat_from_known = false;
}
/* The body publishes its seat for head hiding: from the first seat frame to the end of the glide out. */
static void seat_body_note(const Policy *p, double tick) {
  atomic_store_explicit(&seat_body, p->c.body, memory_order_relaxed); atomic_store_explicit(&seat_body_at, (uint64_t)tick, memory_order_relaxed);
}
/* The glide out is over (reached, or ended by a frame it cannot run in): nothing of it is carried on, the continuity
 * starts again from what is shown, and the seat body is no longer published (the diver is drawn again). */
static void seat_out_end(void) {
  const bool was = seat_out;
  seat_out = false; tfp_seat_glide_reset(&seat_glide); atomic_store_explicit(&seat_gliding, 0, memory_order_relaxed);
  if (was) { smooth_forget(); atomic_store_explicit(&seat_body, 0, memory_order_relaxed); }
}
static void seat_left_at_once(void) { /* left on a frame that is neither the head camera's nor the native view: no glide back */
  seat_in = false; seat_out = false; seat_glide_pending = false; tfp_seat_glide_reset(&seat_glide); tfp_seat_anchor_reset(&seat_anchor);
  atomic_store_explicit(&seat_out_ms, 0, memory_order_relaxed); atomic_store_explicit(&seat_out_metres, float_bits(-1), memory_order_relaxed);
  atomic_store_explicit(&seat_now, 0, memory_order_relaxed); atomic_store_explicit(&seat_gliding, 0, memory_order_relaxed);
  atomic_store_explicit(&seat_body, 0, memory_order_relaxed);
  atomic_fetch_add_explicit(&seat_exits, 1, memory_order_relaxed);
}
static void seat_forget(void) {
  seat_in = false; seat_out = false; seat_glide_pending = false; seat_out_kind = 0; tfp_seat_glide_reset(&seat_glide); tfp_seat_anchor_reset(&seat_anchor);
  seat_from_known = false; seat_hand_known = false;
  atomic_store_explicit(&seat_now, 0, memory_order_relaxed); atomic_store_explicit(&seat_gliding, 0, memory_order_relaxed);
}
/* The body's hand nodes (named l_hand and r_hand like the rig's, src/arm_graft.h), and where they are from `eye` along
 * the root's axes, for the Seat line: what the diver's own hands do in the cockpit. The log only. */
static void seat_hands_note(const Policy *p, const float *basis, const float *eye) {
  static const uint32_t names[2] = {UINT32_C(0x5c0c68d3), UINT32_C(0x248004f5)};
  if (!seat_hand_known || seat_hand_hashes != p->hashes || seat_hand_count != p->count) {
    seat_hand_known = true; seat_hand_hashes = p->hashes; seat_hand_count = p->count; seat_hand_node[0] = seat_hand_node[1] = UINT32_MAX;
    for (uint32_t i = 0; i < p->count && i < 1024; ++i) {
      uint32_t hash = 0; if (!value(p->hashes + (uint64_t)i*4, hash)) break;
      for (unsigned h = 0; h < 2; ++h) if (hash == names[h] && seat_hand_node[h] == UINT32_MAX) seat_hand_node[h] = i;
    }
  }
  uint32_t hands = 0;
  for (unsigned h = 0; h < 2; ++h) {
    float at[3] = {0}, local[3] = {0};
    if (seat_hand_node[h] == UINT32_MAX || !read(p->sampled_worlds + (uint64_t)seat_hand_node[h]*L_MATRIX + L_MATRIX_POSITION, at, 12) || !finite3(at)) continue;
    tfp_seat_local(basis, eye, at, local); hands |= 1u << h;
    for (unsigned i = 0; i < 3; ++i) atomic_store_explicit(&seat_hand[h*3+i], float_bits(local[i]), memory_order_relaxed);
  }
  atomic_store_explicit(&seat_hands, hands, memory_order_relaxed);
}
/* A frame of a seat (tfp_vehicle_seat). The eye: the root's frozen head anchor plus the configured forward step along
 * the root's level forward and the up step (tfp_seat_eye); the direction: the game's on this frame, untouched. On its
 * first frame the head camera's state is let go (the continuity, the eye's smoothing, the held direction and the
 * kick); a glide begins from the view last shown, and while it runs its eye and rotation are shown. The final proof
 * a held frame has comes before anything is written. OK: `adjusted` is the eye to show (and, while gliding, the
 * game's argument holds the glide's rotation); anything else: the proof that refused, and the game's frame is shown. */
static uint32_t seat_present(Policy *p, uint32_t kind, const float *native, double tick, float *adjusted) {
  float *argument_q = (float *)(uintptr_t)native + L_ARGUMENT_ROTATION/4;
  if (!seat_in) {
    seat_in = true; seat_out = false; seat_glide_pending = true;
    smooth_forget(); reset(p, false); tfp_seat_glide_reset(&seat_glide); tfp_seat_anchor_reset(&seat_anchor);
    atomic_store_explicit(&seat_in_kind, kind, memory_order_relaxed); atomic_fetch_add_explicit(&seat_entries, 1, memory_order_relaxed);
  }
  /* No kick, no held or eased direction in a seat: the head camera's rotation state starts again when it is left. */
  lock_view_known = false; tfp_kick_reset(&lock_kick); lock_last_kick = tfp_q_identity(); lock_carry = tfp_q_identity(); lock_was_held = false; lock_first_known = false; tfp_turn_reset(&lock_turn);
  lock_tick = tick; veil_out_on = veil_carry_on = false;
  p->tracking = false; /* the head camera's eye starts again when the seat is left */
  atomic_store_explicit(&seat_now, 1, memory_order_relaxed);
  float root[3] = {0}, head[3] = {0}, basis[9] = {0}, relative[3] = {0}, offset[3] = {0}, eye[3] = {0}; TfpQ game_q = tfp_q_identity();
  uint32_t failure = POSE_INVALID;
  if (!read((uint64_t)(uintptr_t)native, adjusted, 16) || !finite3(adjusted) || !read((uint64_t)(uintptr_t)argument_q, &game_q, 16) || !tfp_q_valid(game_q)) failure = POSE_INVALID;
  else if (!unparented(p)) failure = SPACE_INVALID;
  else if (!points(p, root, head, basis, true) || p->head_far) failure = GRAPH_MISMATCH;
  else if (tfp_seat_local(basis, root, head, relative), !tfp_seat_anchor_step(&seat_anchor, relative, tick, offset)
      || !tfp_seat_eye(root, basis, offset, p->c.forward, p->c.up, eye)) failure = POSE_INVALID;
  else if (commit_current(p, kind, &failure)) failure = OK;
  if (failure != OK) { atomic_fetch_add_explicit(&seat_refused, 1, memory_order_relaxed); return failure; }
  game_q = tfp_q_normalised(game_q);
  if (seat_glide_pending) {
    seat_glide_pending = false;
    const bool from = seat_from_fresh(p, tick);
    const bool glide = from && tfp_seat_glide_begin(&seat_glide, seat_from, seat_from_q, eye, tick);
    if (!glide) atomic_fetch_add_explicit(&seat_cuts, 1, memory_order_relaxed);
    atomic_store_explicit(&seat_in_ms, glide ? (uint32_t)seat_glide.ms : 0, memory_order_relaxed);
    atomic_store_explicit(&seat_in_metres, float_bits(from ? distance(seat_from, eye) : -1), memory_order_relaxed);
  }
  TfpQ q = game_q; memcpy(adjusted, eye, 12);
  const bool glided = seat_glide.active && !seat_out && tfp_seat_glide_apply(&seat_glide, eye, game_q, tick, adjusted, &q);
  if (glided) { memcpy(argument_q, &q, 16); atomic_fetch_add_explicit(&seat_glided, 1, memory_order_relaxed); }
  else if (!seat_out) tfp_seat_glide_reset(&seat_glide);
  atomic_store_explicit(&seat_gliding, glided ? 1u : 0u, memory_order_relaxed);
  seat_note(adjusted, q, tick);
  memcpy(veil_eye_last, adjusted, 12); veil_eye_tick = tick; veil_eye_known = true; memcpy(p->last_head_world, adjusted, 12);
  float shown[3] = {0}; tfp_seat_local(basis, root, adjusted, shown);
  for (unsigned i = 0; i < 3; ++i) atomic_store_explicit(&seat_offset[i], float_bits(shown[i]), memory_order_relaxed);
  atomic_store_explicit(&seat_settled, seat_anchor.settled ? 1u : 0u, memory_order_relaxed);
  seat_hands_note(p, basis, adjusted);
  seat_body_note(p, tick);
  /* Held frames: the lock's count and the body's state for the Lock and Body lines and head hiding. */
  atomic_fetch_add_explicit(&lock_frames, 1, memory_order_relaxed); atomic_store_explicit(&lock_last_kind, kind, memory_order_relaxed);
  lock_body_shown(p, kind, head, tick);
  atomic_fetch_add_explicit(&seat_frames, 1, memory_order_relaxed);
  return OK;
}
/* A head-camera frame under the lock, with its eye and rotation final: the first after a seat begins the glide back
 * from the seat's last view, and while that runs it is what is shown; the continuity's state is let go at its end, so
 * nothing it carried is eased in twice. `eye` is the frame's eye. */
static void seat_back(const Policy *p, const float *native, double tick, float *eye) {
  float *argument_q = (float *)(uintptr_t)native + L_ARGUMENT_ROTATION/4;
  TfpQ q = tfp_q_identity();
  const bool readable = read((uint64_t)(uintptr_t)argument_q, &q, 16) && tfp_q_valid(q) && finite3(eye);
  if (readable) q = tfp_q_normalised(q);
  if (seat_in) {
    seat_in = false; seat_glide_pending = false; tfp_seat_anchor_reset(&seat_anchor); atomic_store_explicit(&seat_now, 0, memory_order_relaxed);
    const bool from = readable && seat_from_fresh(p, tick);
    seat_out = from && tfp_seat_glide_begin(&seat_glide, seat_from, seat_from_q, eye, tick);
    if (!seat_out) tfp_seat_glide_reset(&seat_glide);
    atomic_store_explicit(&seat_out_ms, seat_out ? (uint32_t)seat_glide.ms : 0, memory_order_relaxed);
    atomic_store_explicit(&seat_out_metres, float_bits(from ? distance(seat_from, eye) : -1), memory_order_relaxed);
    atomic_fetch_add_explicit(&seat_exits, 1, memory_order_relaxed);
  }
  /* Back from the native view in the middle of the glide out, the glide goes on from where the native frames left it
   * (seat_native began it again there): the head camera's eye it now aims at is eased out of the native view's by the
   * lock as at every leave of it, so the change of target is no step (camera_seat_test cases 7 and 7b). */
  seat_out_kind = 4;
  if (seat_out) {
    TfpQ glided = q;
    if (readable && tfp_seat_glide_apply(&seat_glide, eye, q, tick, eye, &glided)) {
      memcpy(argument_q, &glided, 16); q = glided; memcpy(veil_eye_last, eye, 12); seat_body_note(p, tick);
      atomic_store_explicit(&seat_gliding, 2, memory_order_relaxed);
    } else seat_out_end();
  }
  if (readable) seat_note(eye, q, tick); else seat_from_known = false;
}
/* Round 38, checker 2026-11-25: a native frame (kind 6) under the lock out of a seat or in the glide out of one. The
 * game draws the camera at the rig eye plus the frame's parent-local argument (smooth_native), so the glide runs here
 * too: from the view last shown to that point, written back as the argument. Leaving the seat straight into the
 * native view showed a 2.5 m cut; a glide out of the seat interrupted by native frames came back to the cockpit's
 * glide. Here a native frame ends the seat (seat_in) and starts the glide out, or takes it on from the view last
 * shown, on its clock, when the frame before was the head camera's; its end, or a rig that cannot be read, ends it (seat_out_end).
 * The direction is the native view's own (lock_rotation): only the position glides. `given` is the 16-byte argument
 * about to be forwarded; true: `adjusted` holds the argument to forward instead. */
static bool seat_native(Policy *p, const float *given, double tick, float *adjusted) {
  if (!seat_in && !seat_out) return false;
  float local[4] = {0}, position[3] = {0}, basis[9] = {0}, target[3] = {0};
  bool readable = read((uint64_t)(uintptr_t)given, local, 16) && finite3(local) && smooth_parent(p, position, basis);
  if (readable) {
    for (unsigned c = 0; c < 3; ++c) { target[c] = position[c]; for (unsigned axis = 0; axis < 3; ++axis) target[c] += local[axis]*basis[axis*3+c]; }
    readable = finite3(target);
  }
  if (seat_in) {
    seat_in = false; seat_glide_pending = false; tfp_seat_anchor_reset(&seat_anchor); atomic_store_explicit(&seat_now, 0, memory_order_relaxed);
    const bool from = readable && seat_from_fresh(p, tick);
    seat_out = from && tfp_seat_glide_begin(&seat_glide, seat_from, seat_from_q, target, tick);
    if (!seat_out) tfp_seat_glide_reset(&seat_glide);
    atomic_store_explicit(&seat_out_ms, seat_out ? (uint32_t)seat_glide.ms : 0, memory_order_relaxed);
    atomic_store_explicit(&seat_out_metres, float_bits(from ? distance(seat_from, target) : -1), memory_order_relaxed);
    atomic_fetch_add_explicit(&seat_exits, 1, memory_order_relaxed);
    if (!seat_out) { atomic_store_explicit(&seat_body, 0, memory_order_relaxed); atomic_store_explicit(&seat_gliding, 0, memory_order_relaxed); }
  } else if (seat_out_kind != 6) {
    /* From the head camera: the glide goes on from the view last shown, on its own clock (tfp_seat_glide_rebase), so
     * however often the game switches views the exit ends within its 300-600 ms (camera_seat_test case 6, switching). */
    seat_out = readable && seat_from_fresh(p, tick) && tfp_seat_glide_rebase(&seat_glide, seat_from, seat_from_q, tick);
    if (!seat_out) { seat_out = true; seat_out_end(); }
  }
  seat_out_kind = 6;
  float shown[3] = {0}; TfpQ ignored = tfp_q_identity();
  if (!seat_out || !readable || !tfp_seat_glide_apply(&seat_glide, target, seat_from_q, tick, shown, &ignored)) { seat_out_end(); return false; }
  float moved[3] = {0};
  for (unsigned axis = 0; axis < 3; ++axis) for (unsigned c = 0; c < 3; ++c) moved[axis] += (shown[c]-position[c])*basis[axis*3+c];
  if (!finite3(moved)) { seat_out_end(); return false; }
  memcpy(adjusted, local, 16); memcpy(adjusted, moved, 12);
  /* Where a glide begins again: this point, with the direction the engine drew last (the start's while unknown). */
  TfpQ drawn = tfp_q_identity();
  seat_note(shown, rendered_rotation(p, &drawn) && tfp_q_valid(drawn) ? drawn : seat_from_q, tick);
  memcpy(veil_eye_last, shown, 12); seat_body_note(p, tick);
  atomic_store_explicit(&seat_gliding, 2, memory_order_relaxed);
  return true;
}
/* The original setter, called as retail calls it (cpp:751, 1020). */
static void call_original(void *camera, uint32_t unit, const float *output) {
  const uintptr_t target = atomic_load_explicit(&original, memory_order_acquire);
  if (host.forward) host.forward(host.user, (uint64_t)target, camera, unit, output);
  else ((TfpCameraSetter)target)(camera, unit, output);
}
/* cpp:747-1026 setter_hook. `caller` replaces _ReturnAddress(). The policy
 * lock is released at the single exit below (retail's Unlock guard). */
void tfp_camera_setter_from(uint64_t caller, void *camera, uint32_t unit, const float *native) {
  atomic_fetch_add_explicit(&seen, 1, memory_order_relaxed);
  const uint64_t entered_generation = atomic_load_explicit(&callback_generation, memory_order_acquire);
  if (!atomic_load_explicit(&armed, memory_order_acquire) || caller != atomic_load_explicit(&return_address, memory_order_relaxed)) {
    atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed); call_original(camera, unit, native); return;
  }
  policy_acquire();
  const float *output = native; _Alignas(16) float adjusted[4] = {0}; bool diagnostics_changed = false, lock_shown = false, lock_asked = false, eye_damped = false; uint32_t lock_kind = 0;
  bool veil_frame = false, veil_clear = false; /* beyond retail, round 14: this frame is drawn while the veil is open; its eye is in front of the head */
  bool veil_deferred = false; /* round 30: a leave whose veil waits for this frame's control mask (veil_same_frame_leave) */
  bool veil_kept = false; /* round 32: that leave kept pending (veil_clamp_kept) */
  bool seat_frame = false; /* round 38: this frame is an exosuit's or vehicle's seat (seat_present) */
  bool clear_ran = false; float clear_meant = 0; /* round 44: the push was asked in this frame; how far in front it meant the eye */
  bool seat_handled = false, seat_native_glide = false; /* round 38: the glide out looked at this frame (seat_back, seat_native); it glided a native one */
  const float seen_before[3] = {veil_eye_last[0], veil_eye_last[1], veil_eye_last[2]}; /* round 32: the eye the last frame handed over */
  const double seen_before_tick = veil_eye_tick; const bool seen_before_known = veil_eye_known;
  double frame_tick = 0; policy.why = TFP_CAMERA_WHY_NONE; policy.lock_down = false; policy.head_far = false; policy.lock_ragdoll = false;
  if (entered_generation && entered_generation == atomic_load_explicit(&callback_generation, memory_order_acquire)
      && atomic_load_explicit(&armed, memory_order_acquire) && (uint64_t)(uintptr_t)camera == policy.c.camera && unit == policy.c.camera_unit) {
    diagnostics_changed = true;
    uint32_t kind = 0; const double tick = now(); frame_tick = tick;
    if (policy.native6_seen && (tick < policy.native6_at || tick-policy.native6_at > policy.c.max_gap_ms)) policy.native6_seen = false;
    const bool known_kind = top(&policy, &kind);
    policy.lock_now = known_kind && policy.presentation_enabled && atomic_load_explicit(&lock_on, memory_order_relaxed);
    if (policy.lock_now && lock_entry_ms > 0 && policy.handoff_entry_ms <= 0) policy.handoff_entry_ms = lock_entry_ms;
    lock_asked = policy.presentation_enabled && atomic_load_explicit(&lock_on, memory_order_relaxed); lock_kind = known_kind ? kind : 0xffff;
    /* Beyond retail, round 14: the first frame of the lock that is not the native view after one that was opens the veil.
     * Round 19, with veil_second.txt: it only notes the leave, and the tick that sees it opens the veil. */
    if (atomic_exchange_explicit(&veil_last_kind, lock_kind, memory_order_relaxed) == 6 && known_kind && kind != 6 && policy.lock_now
        && atomic_load_explicit(&veil_armed, memory_order_relaxed)) {
      veil_in_on = false; veil_in_frames = 0; /* round 30: only a leave kept pending lets the veil in */
      veil_carry_on = false; /* round 32: nor is an earlier kept leave's clamp let out into this one */
      /* A leave while the veil is open (one that went straight back to the native view and left again) is noted in
       * either way: the tick that sees it keeps the veil open for one more tick (tfp_camera_veil_tick). */
      if (atomic_load_explicit(&veil_second, memory_order_relaxed) || atomic_load_explicit(&veil_open, memory_order_relaxed))
        atomic_store_explicit(&veil_pending, 1, memory_order_relaxed);
      /* Round 30: a second evaluation in the native frame's own game frame, out of full aim, is decided once this
       * frame's control mask is read (below, veil_same_frame_leave). */
      else if (policy.native_full_ads && tick >= policy.native6_at && tick-policy.native6_at < TFP_CAMERA_VEIL_SAME_FRAME_MS) veil_deferred = true;
      else {
        atomic_store_explicit(&veil_open, 1, memory_order_relaxed);
        atomic_store_explicit(&veil_ticks, 0, memory_order_relaxed); atomic_fetch_add_explicit(&veil_leaves, 1, memory_order_relaxed);
      }
    }
    veil_frame = atomic_load_explicit(&veil_open, memory_order_relaxed) != 0;
    if (!(known_kind && kind == 4 && policy.lock_now)) veil_out_on = veil_carry_on = false; /* round 26: the let-out is the head camera's only (round 32: the carry too) */
    if (!lock_asked && smooth.known) smooth_forget();
    const bool supported = known_kind && (kind == 4 || kind == 6 || policy.lock_now);
    if (supported && policy.presentation_enabled) atomic_fetch_add_explicit(&view_frames, 1, memory_order_relaxed);
    if (supported && kind == 6) {
      /* Read-only sampling can precede the terminal lease proof because
       * this branch ALWAYS forwards the game's entire native frame. */
      NativeSample sample; memset(&sample, 0, sizeof sample); float head_relative[3] = {0};
      const bool from_head_frame = policy.relative_valid && tick >= policy.last_relative_tick && tick-policy.last_relative_tick <= policy.c.max_gap_ms;
      const bool handoff = policy.handoff_entry_ms > 0 || policy.handoff_rotation_ms > 0 || policy.anchor_ads;
      const bool from_head = handoff && policy.relative_valid && tick >= policy.last_relative_tick
          && tick-policy.last_relative_tick <= policy.c.max_gap_ms;
      if (from_head) memcpy(head_relative, policy.last_relative, 12);
      sample.allow_pending = from_head && policy.handoff_entry_ms > 0;
      const uint32_t current = remember_native(&policy, native, tick, policy.presentation_enabled, handoff ? &sample : NULL);
      atomic_fetch_add_explicit(&native6_forwarded, 1, memory_order_relaxed);
      if (current != OK) { atomic_store_explicit(&armed, 0, memory_order_release); reset(&policy, false); error(current); }
      else { reset(&policy, true); policy.native6_seen = true; policy.native6_at = tick; policy.relative_valid = false; tfp_ease_reset(&policy.turn.ease);
        policy.eye_delta_active = false;
        policy.native_full_ads = policy.full_ads; policy.ads_gap_active = false;
        if (policy.anchor_ads && policy.full_ads && sample.valid && !sample.pending && policy.native_weapon_key
            && policy.native_weapon_key == policy.weapon_key) {
          float weapon[3] = {0}, seen_at[3] = {0};
          if (weapon_position(&policy, policy.native_weapon_key, weapon)) {
            /* In the native view the camera sits on the proven rig node: its basis rows are right, forward, up. */
            for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
              seen_at[axis] += (weapon[component]-sample.parent_position[component])*sample.parent_basis[axis*3+component];
            EyeMemory *entry = eye_memory_entry(policy.native_weapon_key);
            if (entry) {
              float square = 0; for (unsigned axis = 0; axis < 3; ++axis) { const float d = seen_at[axis]-entry->ads_candidate[axis]; square += d*d; }
              if (finite3(seen_at) && square <= .005f*.005f) { if (++entry->ads_frames >= 5 && seen_at[0]*seen_at[0]+seen_at[1]*seen_at[1]+seen_at[2]*seen_at[2] <= 1.0f) {
                  if (!entry->ads_known) ++policy.ads_learned;
                  memcpy(entry->ads, seen_at, 12); entry->ads_known = true; } }
              else { memcpy(entry->ads_candidate, seen_at, 12); entry->ads_frames = 1; }
            }
          }
        }
        /* The weapon this native stretch belongs to: the key reported unchanged for five frames. */
        if (policy.weapon_key != policy.stretch_key) { policy.stretch_key = policy.weapon_key; policy.stretch_key_frames = 1; }
        else if (++policy.stretch_key_frames >= 5) policy.native_weapon_key = policy.stretch_key;
        /* Read-only records for the exit hold and the entry trace. */
        if (from_head_frame) { policy.entry_trace_frame = 0; policy.entry_trace_count = 0; }
        if (policy.entry_trace_frame < 4 && policy.entry_trace_count < TFP_CAMERA_ENTRY_TRACE_ROWS) {
          TfpCameraEntryTraceRow *row = &policy.entry_trace[policy.entry_trace_count++]; memset(row, 0, sizeof *row);
          row->frame = policy.entry_trace_frame++; uint64_t parent = 0, g = 0, worlds = 0, locals = 0;
          read((uint64_t)(uintptr_t)native, row->argument, 12);
          row->linked = value(policy.camera_object + L_UNIT_PARENT, parent) && parent != 0 ? 1u : 0u;
          if (pointer(policy.c.camera + L_CAMERA_GRAPH, &g) && pointer(g + L_GRAPH_WORLDS, &worlds) && pointer(g + L_GRAPH_LOCALS, &locals)) {
            read(worlds + L_MATRIX_POSITION, row->camera_world, 12); read(locals + L_LOCAL_POSITION, row->camera_local, 12); }
          PendingParent pending = pending_parent_default(); float rig_basis[9] = {0};
          row->rig_known = pending_camera_parent(&policy, row->rig_world, rig_basis, &pending) ? 1u : 0u;
          memcpy(row->head_world, policy.last_head_world, 12);
        } else if (policy.entry_trace_frame < 4) ++policy.entry_trace_frame;
        policy.last_view_tick = tick;
        policy.last_native_valid = read((uint64_t)(uintptr_t)native, policy.last_native, 16) && finite3(policy.last_native);
        policy.last_native_tick = tick;
        if (handoff) {
          if (from_head) { policy.entry_waiting = true; policy.entry_delay_frames = 0; }
          if (!sample.valid) { ++policy.sample_reasons[sample.reason & 7]; if (policy.entry_waiting) ++policy.entry_delay_frames; }
          else if (policy.entry_waiting) { policy.entry_waiting = false; policy.entry_last_delay = policy.entry_delay_frames;
            if (policy.entry_delay_frames > policy.entry_max_delay) policy.entry_max_delay = policy.entry_delay_frames; }
          if (policy.handoff_rotation_ms > 0) track_native_rotation(&policy, tick);
          const bool carried = policy.lock_now && atomic_load_explicit(&smooth_on, memory_order_relaxed); /* the entry is the continuity's */
          if (carried) tfp_ease_reset(&policy.entry.ease);
          else if (policy.handoff_entry_ms > 0 && ease_native_entry(&policy, &sample, native, tick, from_head, head_relative, adjusted)) output = adjusted;
        }
        if (policy.lock_now) {
          /* Beyond retail. A native eye that moved 5 cm in a frame is being re-seated by the game (its gun left the
           * hands): for 200 ms it is not what is remembered as this weapon's eye. */
          if (policy.native_eye_valid && policy.native_eye_tick == tick) {
            if (eye_watch_known && distance(policy.native_eye, eye_watch) > .05f) eye_unsettled_until = tick+200.0;
            memcpy(eye_watch, policy.native_eye, 12); eye_watch_known = true;
          }
          const bool orphan = atomic_load_explicit(&orphan_on, memory_order_relaxed) != 0 && !policy.full_ads;
          seat_handled = true;
          if ((seat_native_glide = seat_native(&policy, output, tick, adjusted))) output = adjusted; /* round 38: the glide out of a seat owns the eye */
          else if (orphan && orphan_head(&policy, native, output, kind, tick, adjusted)) {
            /* Not the weapon's eye: nothing of it is held or repeated once the game's exit arrives. */
            output = adjusted; policy.native_eye_valid = false; memcpy(policy.last_native, adjusted, 12);
            atomic_fetch_add_explicit(&orphan_frames, 1, memory_order_relaxed);
          } else {
            if (orphan) atomic_fetch_add_explicit(&orphan_refused, 1, memory_order_relaxed);
            if (smooth_native(&policy, handoff ? &sample : NULL, output, kind, SMOOTH_NATIVE, tick, adjusted)) output = adjusted;
          }
        }
        /* cpp:823 observe_camera_parent(policy,tick): retired, dropped. */
        lock_rotation(&policy, native, kind, tick); lock_shown = true;
        if (policy.lock_now && !seat_native_glide) seat_note_rendered(&policy, tick); /* round 38: where a glide into a seat starts */
        error(OK); }
    } else {
      const uint32_t current = owner_status(&policy, false);
      if (veil_deferred) veil_kept = !(veil_frame = veil_same_frame_leave(current == OK ? policy.control_mask : UINT32_MAX));
      if (current != OK || !dispatch_owned(&policy)) {
        atomic_store_explicit(&armed, 0, memory_order_release); reset(&policy, false); error(current != OK ? current : OWNER_MISMATCH);
        policy.why = TFP_CAMERA_WHY_OWNER;
      } else if (!supported) { reset(&policy, false); }
      else if (!policy.fallback_safe && !policy.lock_now && !(policy.guard_grace = policy.guard_only && policy.last_view_tick > 0
              && tick >= policy.last_view_tick && tick-policy.last_view_tick <= 250.0)) { reset(&policy, false); }
      /* on_foot also samples what the final proof compares, so it runs under the lock too; only its answer is set aside. */
      else if ((policy.lock_off_foot = !on_foot(&policy)) && !policy.lock_now) { reset(&policy, false); error(ON_FOOT_REQUIRED); }
      /* Round 38: under the lock, a body seated, off foot and not in physics is in an exosuit's or a vehicle's seat:
       * first person kept, the eye fixed in the root's frame and the game's direction (src/camera_vehicle.h). lock_body
       * reads the ragdoll flag (and the animation chain the final proof compares) for every frame the lock may hold. */
      else if (policy.lock_now && (lock_body(&policy), tfp_vehicle_seat(policy.seated_now, policy.lock_off_foot, policy.lock_ragdoll))) {
        seat_frame = true;
        const uint32_t seat_error = seat_present(&policy, kind, native, tick, adjusted);
        if (seat_error == OK) { output = adjusted; lock_shown = true; atomic_fetch_add_explicit(&replaced, 1, memory_order_relaxed); error(OK); }
        else { reset(&policy, false); error(seat_error); }
      }
      else {
        float root[3] = {0}, head[3] = {0}, forward[3] = {0}, up[3] = {0}, root_basis[9] = {0};
        /* A seated interaction (turning a valve): with cut-hold bit 3 the view stays at the head. */
        const bool seated_view = policy.hold_seated && policy.seated_now && policy.presentation_enabled;
        if (!seated_view) policy.seated_q_valid = false;
        else if (!policy.seated_q_valid) {
          TfpQ seen_q = tfp_q_identity();
          if (rendered_rotation(&policy, &seen_q) && tfp_q_valid(seen_q)) { policy.seated_q = tfp_q_normalised(seen_q); policy.seated_q_valid = true; }
        }
        const bool held_view = seated_view && policy.seated_q_valid;
        const bool pointed = points(&policy, root, head, root_basis, policy.lock_now);
        /* Beyond retail: physics has the body (camera_helper.h TfpCameraDownStats). */
        policy.lock_down = pointed && policy.lock_now && (policy.head_far || (policy.lock_off_foot && policy.lock_ragdoll));
        if (!pointed) { reset(&policy, false); error(GRAPH_MISMATCH); }
        else if (!unparented(&policy) && !(policy.lock_now && unparented_behind(&policy))) {
          /* The request is already normal4 but the camera is still linked to the first-person rig.
           * While that link is the proven one, repeat the last native6 argument. */
          TfpQ parent = tfp_q_identity();
          if (policy.hold_exit && policy.native6_seen && policy.last_native_valid && tick >= policy.last_native_tick
              && tick-policy.last_native_tick <= policy.c.max_gap_ms && direct_camera_parent(&policy, &parent, NULL, NULL, NULL)
              && read((uint64_t)(uintptr_t)native, adjusted, 16)) {
            memcpy(adjusted, policy.last_native, 12); output = adjusted; ++policy.exit_holds; lock_shown = true; /* the last native view repeated: still first person */
            policy.last_native_tick = tick;
            if (policy.lock_now) smooth_native(&policy, NULL, adjusted, kind, SMOOTH_REPEAT, tick, adjusted); /* the rig may stand still while the body moves on */
          }
          reset(&policy, true); error(SPACE_INVALID);
          /* Not repeated: the frame is the game's own. Which of the two it was goes to the Missed line. */
          if (policy.lock_now && !lock_shown) { float apart = 0; policy.why = space_why(&policy, &apart);
            atomic_store_explicit(&lock_missed_apart, float_bits(apart), memory_order_relaxed); }
        }
        else if (!read((uint64_t)(uintptr_t)native, adjusted, 16) || !finite3(adjusted)
            || (held_view ? !held_axes(policy.seated_q, forward, up)
                : (!basis(&policy, forward, up) || (!policy.lock_now && (distance(adjusted, head) > 8 || distance(adjusted, root) > 10))))) {
          reset(&policy, false); error(POSE_INVALID);
        } else {
          /* The game's argument is not a usable start for the blend in the held view. */
          if (held_view) { policy.blend = 1; memcpy(adjusted, head, 12); }
          /* Beyond retail: in a camera that is not the game's ordinary one the eye is placed along the last
           * first-person direction, not along that camera's own. */
          if (policy.lock_now && (kind != 4 || policy.lock_down) && lock_view_known) held_axes(lock_view, forward, up);
          /* Round 55, THE DIRECTION SHOWN: in a frame that carries a turn, the eye, the push, the veil and head_shown
           * go along the direction the frame will be shown with, not the context's (lock_turn_preview). */
          else if (!held_view) {
            TfpQ shown_q = tfp_q_identity(); float shown_forward[3], shown_up[3];
            if (lock_turn_preview(&policy, native, kind, tick, &shown_q) && held_axes(shown_q, shown_forward, shown_up)) { memcpy(forward, shown_forward, 12); memcpy(up, shown_up, 12); }
          }
          /* Remove current body-root rotation before damping animation.
           * Fresh root translation/rotation is restored after filtering. */
          float local_head[3] = {0};
          for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
            local_head[axis] += (head[component]-root[component])*root_basis[axis*3+component];
          float dt = 0;
          if (policy.tracking && tick >= policy.previous && tick-policy.previous <= policy.c.max_gap_ms) dt = (float)(tick-policy.previous)/1000;
          else { reset(&policy, policy.native6_seen); memcpy(policy.relative_head, local_head, 12); }
          policy.tracking = true; policy.previous = tick; policy.last_dt = dt;
          const float k = policy.c.smoothing_ms > 0 ? 1-expf(-dt*1000/policy.c.smoothing_ms) : 1;
          for (unsigned i = 0; i < 3; ++i) policy.relative_head[i] += (local_head[i]-policy.relative_head[i])*k;
          float lag[3] = {0};
          for (unsigned component = 0; component < 3; ++component) {
            lag[component] = root[component]-head[component];
            for (unsigned axis = 0; axis < 3; ++axis) lag[component] += policy.relative_head[axis]*root_basis[axis*3+component];
          }
          const float lag_length = sqrtf(lag[0]*lag[0]+lag[1]*lag[1]+lag[2]*lag[2]);
          if (lag_length > .06f) for (unsigned i = 0; i < 3; ++i) lag[i] *= .06f/lag_length;
          policy.blend = policy.c.blend_ms > 0 ? fminf(1, policy.blend+dt*1000/policy.c.blend_ms) : 1;
          if (policy.lock_now) policy.blend = 1; /* the lock never shows the way in from the game's own camera */
          /* Native6 arguments are parent-local. Never reuse those values
           * for a world-space bridge or expose a TPV frame on retirement. */
          const bool from_native = policy.native6_seen; tfp_ease_reset(&policy.entry.ease);
          if (policy.native6_seen) { policy.blend = 1; policy.native6_seen = false; }
          const float weight = policy.blend*policy.blend*(3-2*policy.blend);
          /* Pitching/rolling the look direction must not orbit the eye
           * vertically into the helmet. At a vertical look use body yaw. */
          float horizontal[3] = {forward[0], forward[1], 0};
          float horizontal_length = sqrtf(horizontal[0]*horizontal[0]+horizontal[1]*horizontal[1]);
          if (horizontal_length < .0001f) {
            horizontal[0] = root_basis[3]; horizontal[1] = root_basis[4];
            horizontal_length = sqrtf(horizontal[0]*horizontal[0]+horizontal[1]*horizontal[1]);
          }
          if (horizontal_length < .0001f) { reset(&policy, false); error(POSE_INVALID); }
          else {
            horizontal[0] /= horizontal_length; horizontal[1] /= horizontal_length;
            for (unsigned i = 0; i < 3; ++i) {
              const float target = head[i]+lag[i]+horizontal[i]*policy.c.forward+(i == 0 ? horizontal[1]*policy.side : i == 1 ? -horizontal[0]*policy.side : policy.c.up);
              adjusted[i] += (target-adjusted[i])*weight;
            }
            float desired_relative[3] = {0}, relative[3] = {0};
            for (unsigned axis = 0; axis < 3; ++axis) for (unsigned component = 0; component < 3; ++component)
              desired_relative[axis] += (adjusted[component]-root[component])*root_basis[axis*3+component];
            bool eye_held = false;
            if (policy.hold_eye && policy.presentation_enabled) {
              if (from_native) {
                policy.eye_delta_active = false; policy.eye_returning = false; tfp_eye_glide_reset(&policy.eye_glide);
                if (policy.native_eye_valid && tick >= policy.native_eye_tick && tick-policy.native_eye_tick <= policy.c.max_gap_ms) {
                  float square = 0;
                  for (unsigned axis = 0; axis < 3; ++axis) { policy.eye_delta[axis] = policy.native_eye[axis]-desired_relative[axis]; square += policy.eye_delta[axis]*policy.eye_delta[axis]; }
                  /* More than a metre apart is a different view, not the same eye. */
                  if (isfinite(square) && square <= 1.0f) {
                    policy.eye_delta_active = true; policy.eye_hold_started = tick; policy.eye_swap_tick = tick;
                    if (!(policy.lock_now && tick < eye_unsettled_until)) eye_memory_store(policy.native_weapon_key, policy.eye_delta);
                  }
                }
              }
              else if ((!policy.eye_delta_active || (policy.lock_now && tfp_eye_glide_releasing(&policy.eye_glide))) && policy.control_mask == 2 && policy.weapon_key
                  && atomic_load_explicit(&tuning_eye_return, memory_order_relaxed) && eye_memory_recorded(policy.weapon_key)) {
                /* Beyond retail: the eye let go (hands empty for longer than the tolerance) and a weapon whose eye
                 * is known is being drawn. Start from where the eye is and glide to that eye, so the native view
                 * returns to a camera that is already there. (Round 19: while it is being let go, from where it is
                 * and the way it is going: the release glides on to that eye, carrying the eye's speed only as far as
                 * it can lose it before that eye, src/camera_glide.h THE TAKE-OVER. That glide stays a release until it ends,
                 * so this branch runs again in every frame of it and starts the hold anew each time: the 2500 ms hold
                 * counts from the take-over's end, up to its 300 to 450 ms later than from the weapon being drawn.
                 * Left so: the take-over ends at the incoming eye, where the hold then keeps it.) */
                if (!policy.eye_delta_active) { memset(policy.eye_delta, 0, sizeof policy.eye_delta); tfp_eye_glide_reset(&policy.eye_glide); }
                policy.eye_delta_active = true; policy.eye_returning = true; policy.eye_hold_started = tick; policy.eye_swap_tick = tick;
              }
              if (policy.eye_delta_active) {
                /* Kept through a weapon swap (mask 2) for at most 2500 ms, and for tuning_eye_idle_ms of mask 0 around it
                 * (retail's 300; the binder sets TFP_ACTION_EYE_IDLE_MS, 900 on build 25480438: src/wire_action.h). */
                if (policy.control_mask == 2) policy.eye_swap_tick = tick;
                const double idle = (double)atomic_load_explicit(&tuning_eye_idle_ms, memory_order_relaxed);
                const bool keep = tick-policy.eye_hold_started <= 2500.0 && (policy.control_mask == 2
                    || (policy.control_mask == 0 && tick >= policy.eye_swap_tick && tick-policy.eye_swap_tick <= idle));
                /* A different weapon is coming up and its eye is known: glide there during the swap. */
                const EyeMemory *incoming = keep && (policy.weapon_key != policy.native_weapon_key || policy.eye_returning) ? eye_memory_find(policy.weapon_key) : NULL;
                /* (Beyond retail, in a frame the lock holds: a way of up to 0.2625 m is glided from rest to rest, so its
                 * first frame is no jump for the continuity to take up; a longer one is retail's there too. And an eye
                 * that is under way when there is no eye to go to any more comes to rest, it does not stop in the
                 * frame. src/camera_glide.h.) */
                /* (Beyond retail, round 19: a glide that began as the release and is under way goes on to the incoming
                 * eye on the release's rates; src/camera_glide.h THE RELEASE.) */
                if (incoming) {
                  if (policy.lock_now && tfp_eye_glide_releasing(&policy.eye_glide)) {
                    if (tfp_eye_glide_release(&policy.eye_glide, policy.eye_delta, incoming->delta, dt)) ++policy.eye_glides;
                  } else if (policy.lock_now ? tfp_eye_glide_lock_step(&policy.eye_glide, policy.eye_delta, incoming->delta, dt)
                                             : tfp_eye_glide_retail(policy.eye_delta, incoming->delta, dt)) ++policy.eye_glides;
                } else if (policy.lock_now && keep) tfp_eye_glide_rest(&policy.eye_glide, policy.eye_delta, dt);
                if (!policy.lock_now) tfp_eye_glide_reset(&policy.eye_glide);
                if (!keep && policy.lock_now) {
                  /* Beyond retail, round 19: the hold lapsed; the held eye is let go from rest to rest over 300 to 450 ms
                   * (src/camera_glide.h THE RELEASE), not on retail's 80 ms fade, whose first frame was 0.15 m. A swap's
                   * glide under way runs to its end first and the release begins from rest after it (THE LAPSE). */
                  static const float own[3] = {0, 0, 0};
                  if (!tfp_eye_glide_lapse(&policy.eye_glide, policy.eye_delta, own, dt)) { policy.eye_delta_active = false; tfp_eye_glide_reset(&policy.eye_glide); }
                } else if (!keep) {
                  const float fade = dt > 0 ? expf(-dt/.08f) : 1; float square = 0;
                  for (unsigned axis = 0; axis < 3; ++axis) { policy.eye_delta[axis] *= fade; square += policy.eye_delta[axis]*policy.eye_delta[axis]; }
                  if (square < .002f*.002f) policy.eye_delta_active = false;
                }
              }
              if (policy.eye_delta_active) {
                for (unsigned axis = 0; axis < 3; ++axis) relative[axis] = desired_relative[axis]+policy.eye_delta[axis];
                for (unsigned component = 0; component < 3; ++component) { adjusted[component] = root[component];
                  for (unsigned axis = 0; axis < 3; ++axis) adjusted[component] += relative[axis]*root_basis[axis*3+component]; }
                tfp_correction_reset(&policy.correction); eye_held = true; ++policy.eye_hold_frames;
              }
            }
            if (eye_held) {}
            else if (policy.presentation_enabled && tfp_correction_apply(&policy.correction, desired_relative, policy.current_weapon, tick, relative, 150, policy.c.max_gap_ms)) {
              for (unsigned component = 0; component < 3; ++component) { adjusted[component] = root[component];
                for (unsigned axis = 0; axis < 3; ++axis) adjusted[component] += relative[axis]*root_basis[axis*3+component]; }
            } else memcpy(relative, desired_relative, 12);
            if (policy.anchor_ads && policy.presentation_enabled) {
              /* Learned on every normal4 frame, the first one after native6 included. */
              const uint32_t keys[2] = {policy.weapon_key, policy.native_weapon_key};
              for (unsigned n = 0; n < 2; ++n) {
                const uint32_t key = keys[n];
                uint32_t node = 0;
                if (key && weapon_body_node(&policy, key, &node)) { EyeMemory *entry = eye_memory_entry(key); if (entry) { entry->tpv_node = node; entry->tpv_known = true; } }
              }
              if (from_native) {
                const EyeMemory *known = policy.native_full_ads ? eye_memory_find(policy.native_weapon_key) : NULL;
                policy.ads_gap_active = known && known->ads_known; policy.ads_gap_started = tick;
              }
              if (policy.ads_gap_active) {
                /* Full strength for 50 ms (the kit re-enters in two frames), then it lets go. */
                const double age = tick-policy.ads_gap_started;
                const float anchor_weight = age <= 50 ? 1.0f : (float)exp(-(age-50)/50.0);
                const EyeMemory *known = eye_memory_find(policy.native_weapon_key); float weapon[3] = {0}, right[3] = {0};
                if (anchor_weight < .02f || !known || !known->ads_known || !anchor_source(&policy, known, policy.native_weapon_key, weapon)) policy.ads_gap_active = false;
                else {
                  right[0] = forward[1]*up[2]-forward[2]*up[1]; right[1] = forward[2]*up[0]-forward[0]*up[2]; right[2] = forward[0]*up[1]-forward[1]*up[0];
                  float anchored[3] = {0}, square = 0;
                  for (unsigned i = 0; i < 3; ++i) { anchored[i] = weapon[i]-(right[i]*known->ads[0]+forward[i]*known->ads[1]+up[i]*known->ads[2]);
                    const float d = anchored[i]-adjusted[i]; square += d*d; }
                  /* More than 0.8 m from the ordinary eye is not this view any more. */
                  if (!finite3(anchored) || square > .8f*.8f) policy.ads_gap_active = false;
                  else {
                    for (unsigned i = 0; i < 3; ++i) adjusted[i] += (anchored[i]-adjusted[i])*anchor_weight;
                    for (unsigned axis = 0; axis < 3; ++axis) { relative[axis] = 0; for (unsigned component = 0; component < 3; ++component)
                      relative[axis] += (adjusted[component]-root[component])*root_basis[axis*3+component]; }
                    ++policy.ads_gap_frames;
                  }
                }
              }
            }
            /* Round 44, THE PUSH (src/camera_clear.h): in a head-camera frame of the lock with head visibility active, the
             * eye the frame is built on is moved along the level view to TFP_CAMERA_CLEAR_M in front of the head node,
             * before the continuity and the damping, so they and the way back into the native view start from it. */
            if (policy.lock_now && kind == 4 && !held_view && atomic_load_explicit(&veil_armed, memory_order_relaxed) && atomic_load_explicit(&clear_on, memory_order_relaxed)) {
              const TfpClearMode mode = policy.lock_down ? TFP_CLEAR_DOWN : policy.lock_off_foot ? TFP_CLEAR_LET_GO : TFP_CLEAR_ON;
              const float ahead = tfp_clear_ahead(adjusted, head, horizontal);
              const float push = tfp_clear_step(&clear_state, mode, ahead, veil_frame && !veil_kept && !veil_in_on);
              clear_ran = true;
              if (mode == TFP_CLEAR_ON && clear_state.run && !clear_state.far) clear_meant = ahead+push; else clear_meant = -INFINITY;
              if (push > 0) {
                adjusted[0] += horizontal[0]*push; adjusted[1] += horizontal[1]*push;
                for (unsigned axis = 0; axis < 3; ++axis) { relative[axis] = 0; for (unsigned component = 0; component < 3; ++component)
                  relative[axis] += (adjusted[component]-root[component])*root_basis[axis*3+component]; }
              }
            }
            /* Round 44: the body whose eye the swap holds or lets go in a head-camera frame (Show torso waits for it). */
            if (policy.lock_now && kind == 4) {
              atomic_store_explicit(&eye_held_body, eye_held ? policy.c.body : 0u, memory_order_relaxed); atomic_store_explicit(&eye_held_at, (uint64_t)tick, memory_order_relaxed);
            }
            /* Revalidate mutable ownership, request and graph after all
             * sampling, directly before making a corrected native call. */
            uint32_t commit_error = OWNER_MISMATCH;
            if (!commit_current(&policy, kind, &commit_error)) { reset(&policy, false); if (commit_error == READ_FAILED) atomic_store_explicit(&armed, 0, memory_order_release); error(commit_error); }
            else { memcpy(policy.last_relative, relative, 12); policy.last_relative_tick = tick; policy.relative_valid = true;
              if (policy.lock_now) { float shown[3] = {0}; const float smoothed[3] = {head[0]+lag[0], head[1]+lag[1], head[2]+lag[2]};
                eye_damped = lock_damp_eye(&policy, kind, root, root_basis, forward, smoothed, tick, adjusted);
                if (finite3(forward)) memcpy(trace_damp.view, forward, 12);
                if (smooth_present(&policy, kind, SMOOTH_HEAD, root, root_basis, adjusted, smoothed, tick, shown)) memcpy(adjusted, shown, 12); }
              memcpy(policy.last_head_world, adjusted, 12);
              /* Beyond retail, round 14: after everything that keeps the eye (the swap's hold and glide, the continuity, the
               * damping, the trace), so only what this frame hands the game changes. */
              const float before[3] = {adjusted[0], adjusted[1], adjusted[2]};
              bool veil_logged = false;
              if (veil_frame && policy.lock_now && kind == 4) {
                veil_clear = veil_apply(head, horizontal, forward, !policy.lock_down && !policy.lock_off_foot, adjusted);
                /* Round 30: after a leave kept pending, the move is let in (camera_helper.h tfp_camera_veil_let_in). */
                if (veil_in_on) {
                  const float most = tfp_camera_veil_let_in(++veil_in_frames);
                  const float d[3] = {adjusted[0]-before[0], adjusted[1]-before[1], adjusted[2]-before[2]};
                  const float length = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                  if (length > most) { for (unsigned i = 0; i < 3; ++i) adjusted[i] = before[i]+d[i]*(most/length); veil_clear = false; }
                }
                /* Round 26: what the move was, for the let-out after the veil closes. */
                /* Round 49: a veiled frame that moves less than what is left of an earlier frame's move (nothing: the
                 * head too far to move to, soak 60, 19:38:37.383, 0.248 m then 0; or less: the head below a view that
                 * has pitched, soak 62, 19:45:10.494, 0.120 m then 0.055) wrote its own move over the one kept, and the
                 * difference was dropped in one frame. The earlier move and its time are kept and it is let out in
                 * this frame as in a frame after the veil. What the eye itself has gained on the head since that
                 * frame is taken off first (a held eye being let go under the veil needs less of the move, and the
                 * veil's own then stands); a frame whose own move is the larger is the move kept from then on. */
                const float veil_move[3] = {adjusted[0]-before[0], adjusted[1]-before[1], adjusted[2]-before[2]};
                const float veil_ahead = (before[0]-head[0])*horizontal[0]+(before[1]-head[1])*horizontal[1];
                const float veil_w = veil_out_on && !policy.lock_down && tick >= veil_out_tick ? tfp_camera_veil_let_out(tick-veil_out_tick) : 0;
                const float veil_own = sqrtf(veil_move[0]*veil_move[0]+veil_move[1]*veil_move[1]+veil_move[2]*veil_move[2]);
                const float veil_kept = sqrtf(veil_out_move[0]*veil_out_move[0]+veil_out_move[1]*veil_out_move[1]+veil_out_move[2]*veil_out_move[2]);
                const float veil_left = veil_kept*veil_w-fmaxf(0, veil_ahead-veil_out_ahead);
                if (veil_w > 0 && veil_kept > 0 && isfinite(veil_left) && veil_left > veil_own+1e-4f)
                  for (unsigned i = 0; i < 3; ++i) adjusted[i] = before[i]+veil_out_move[i]*(veil_left/veil_kept);
                else {
                  memcpy(veil_out_move, veil_move, 12); veil_out_ahead = veil_ahead;
                  veil_out_tick = tick; veil_out_on = veil_own > 0;
                }
                veil_logged = true;
              } else if (veil_out_on && !veil_frame && policy.lock_now && kind == 4 && !policy.lock_down && tick >= veil_out_tick) {
                /* Round 26: the veil closed: its last move is let out (camera_helper.h tfp_camera_veil_let_out). */
                const float w = tfp_camera_veil_let_out(tick-veil_out_tick);
                if (w > 0) for (unsigned i = 0; i < 3; ++i) adjusted[i] += veil_out_move[i]*w;
                else veil_out_on = false;
              } else veil_out_on = false;
              /* Round 32: a kept leave's frame is clamped to the let-in cap from the native frame of its own game frame,
               * and what that held back is let out in the head-camera frames after it (veil_clamp_kept). */
              if (veil_kept && policy.lock_now && kind == 4) {
                if (seen_before_known && seen_before_tick == policy.native6_at) (void)veil_clamp_kept(seen_before, seen_before_tick, tick, policy.c.max_gap_ms, adjusted);
              } else if (policy.lock_now && kind == 4 && !policy.lock_down) veil_carry_step(tick, adjusted);
              else veil_carry_on = false;
              /* Round 44, THE FLOOR (src/camera_clear.h): after the continuity, the damping, the veil, its let-out and the
               * carry, the eye shown is as far in front of the head as the push meant; so the let-out ends at the larger
               * of the held eye and the head plus the clearance. Not in a kept leave's frames (their caps hold there). */
              if (clear_ran && isfinite(clear_meant) && !veil_kept && !veil_in_on && !veil_carry_on) {
                const float move = tfp_clear_floor(adjusted, head, horizontal, clear_meant);
                if (move > 0) {
                  atomic_fetch_add_explicit(&clear_floored, 1, memory_order_relaxed);
                  if (move > bits_float(atomic_load_explicit(&clear_largest, memory_order_relaxed))) atomic_store_explicit(&clear_largest, float_bits(move), memory_order_relaxed);
                }
                if (veil_frame && !veil_clear && tfp_clear_ahead(adjusted, head, horizontal) >= TFP_CAMERA_VEIL_CLEAR-1e-4f) veil_clear = true;
              }
              /* Round 55 did not cap the veil's open (0.39 to 0.45 m along the view at leaves out of prone, dive-swaps,
               * quick swaps, aim swaps: soak 68 #68, #82, #83, #200, the matrix's dive-swap). Every one of those leaves is
               * on foot (off_foot=0 on its rows), so the matrix's behind_head judge and the round 44 throw-eye judges
               * measure it, and any cap leaves those frames behind the head (the throw-eye judges: 0.05 to 0.19 m behind,
               * where this keeps 0.10 m in front). The whole move stays in the leave's frame, as in round 44. */
              if (policy.lock_now && kind == 4) {
                /* Round 55, THE ENTRY: what this frame's eye shown was moved after the continuity, for the continuity at a
                 * native frame next (smooth_present). Nothing moved: nothing handed on. */
                const float moved[3] = {adjusted[0]-before[0], adjusted[1]-before[1], adjusted[2]-before[2]};
                veil_shown_on = finite3(moved) && (moved[0] != 0 || moved[1] != 0 || moved[2] != 0);
                for (unsigned axis = 0; axis < 3; ++axis) { veil_shown_moved[axis] = 0;
                  for (unsigned component = 0; component < 3; ++component) veil_shown_moved[axis] += moved[component]*root_basis[axis*3+component]; }
                veil_shown_tick = tick;
              } else veil_shown_on = false;
              memcpy(veil_eye_last, adjusted, 12); veil_eye_tick = tick; veil_eye_known = true;
              /* Round 19, the log only: the move on this frame's Trace row (smooth_present wrote it above, with the eye
               * before the veil), so that eye + veil is what is shown. Round 26: the let-out too, and in a frame the veil
               * was open for, where the head node was from the eye before it. */
              if (atomic_load_explicit(&trace_on, memory_order_relaxed) && trace.head) {
                TraceFrame *row = &trace.frames[(trace.head-1) % TRACE_RING];
                if (row->tick == tick) {
                  for (unsigned i = 0; i < 3; ++i) row->veil[i] = adjusted[i]-before[i];
                  if (veil_logged) { row->veil_head[0] = (head[0]-before[0])*horizontal[0]+(head[1]-before[1])*horizontal[1]; row->veil_head[1] = head[2]-before[2]; }
                  /* Round 49, the log only: the difference a swap holds the eye at in this frame (the glide's part
                   * in a dive's float was inferred from timing; nothing said the delta). */
                  if (eye_held) { memcpy(row->hold, policy.eye_delta, 12); row->hold_known = true; }
                  /* Round 44: where the head node was from the eye shown, and the push (src/camera_clear.h). */
                  if (policy.lock_now && kind == 4) {
                    row->head_shown[0] = -tfp_clear_ahead(adjusted, head, horizontal); row->head_shown[1] = head[2]-adjusted[2]; row->head_known = true;
                    row->clear = clear_ran ? clear_state.push : 0;
                  }
                }
              }
              if (policy.fallback_safe) policy.last_view_tick = tick; else ++policy.guard_grace_frames;
              if (held_view) {
                /* Follow the game's direction from the head, gliding there from the view the
                 * interaction began with; an unreadable rotation holds. */
                TfpQ wanted = tfp_q_identity();
                if (read((uint64_t)(uintptr_t)native + L_ARGUMENT_ROTATION, &wanted, 16) && tfp_q_valid(wanted) && dt > 0) {
                  const TfpQ next = tfp_slerp(policy.seated_q, tfp_q_normalised(wanted), 1-expf(-dt/.18f));
                  if (tfp_q_valid(next)) policy.seated_q = next;
                }
                memcpy((float *)(uintptr_t)native + L_ARGUMENT_ROTATION/4, &policy.seated_q, 16);
                tfp_ease_reset(&policy.turn.ease); policy.forwarded_q_valid = false; ++policy.seated_frames;
              } else if (policy.presentation_enabled) ease_normal_rotation(&policy, native, tick, from_native);
              lock_rotation(&policy, native, kind, tick); lock_shown = true;
              if (policy.lock_now) lock_body_shown(&policy, kind, head, tick);
              if (policy.lock_now) { seat_back(&policy, native, tick, adjusted); seat_handled = true; } /* round 38: out of a seat, and where a glide into one starts */
              output = adjusted; atomic_fetch_add_explicit(&replaced, 1, memory_order_relaxed); error(OK); }
          }
        }
      }
    }
  }
  if (diagnostics_changed && !eye_damped) tfp_eye_damp_reset(&lock_eye_damp); /* any other frame of this camera: the damping starts again */
  if (diagnostics_changed && !clear_ran) clear_state = (TfpClear){0}; /* round 44: any other frame drops the push */
  /* Round 38: a seat left on a frame that is not the head camera's has no glide back; any frame the lock did not show
   * is no start for a glide; without the lock nothing of a seat is kept. */
  if (diagnostics_changed) {
    if (seat_in && !seat_frame) seat_left_at_once();
    if (seat_out && !seat_handled) seat_out_end(); /* a frame the glide out did not run in ends it: it never resumes from the cockpit */
    if (!lock_shown) seat_from_known = false;
    if (!policy.lock_now) { seat_out_end(); seat_from_known = false; }
  }
  /* Serialize actual adjusted call with End, not just the calculation. */
  if (lock_asked && !lock_shown) { /* a frame of the view that was not first person: what the test matrix counts */
    atomic_fetch_add_explicit(&lock_missed, 1, memory_order_relaxed); atomic_store_explicit(&lock_missed_kind, lock_kind, memory_order_relaxed);
    atomic_store_explicit(&lock_missed_error, atomic_load_explicit(&last_error, memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&lock_missed_why, policy.why, memory_order_relaxed);
    /* The game's own camera while the body is down, or straight after (the request taken away from the corpse). */
    if (lock_kind != 6 && (policy.head_far || (policy.lock_now && policy.lock_off_foot && policy.lock_ragdoll) || frame_tick < lock_down_until)) {
      atomic_fetch_add_explicit(&lock_down_refused, 1, memory_order_relaxed); atomic_store_explicit(&lock_down_kind, lock_kind, memory_order_relaxed);
      atomic_store_explicit(&lock_down_error, atomic_load_explicit(&last_error, memory_order_relaxed), memory_order_relaxed);
      atomic_store_explicit(&lock_down_why, policy.why, memory_order_relaxed);
    }
  }
  if (veil_frame) { /* beyond retail, round 14 */
    atomic_fetch_add_explicit(&veil_frames, 1, memory_order_relaxed);
    /* Only frames that are not the native view: a native frame is the game's own camera, which the veil never moves (a
     * leave that went straight back within a tick draws some while it is open). */
    if (lock_kind != 6 && (!veil_clear || output != adjusted)) atomic_fetch_add_explicit(&veil_behind, 1, memory_order_relaxed);
  }
  atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed); call_original(camera, unit, output);
  if (diagnostics_changed) publish_diagnostics(&policy);
  policy_release();
}
/* cpp:1028-1042. The slot must hold exe+setter (retail's non-fixture path). */
static bool contract(const TfpCameraConfig *c, uint64_t *setter, uint64_t *address) {
  if (!image(c->game, B_GAME_STAMP, B_GAME_SIZE) || !image(c->exe, B_EXE_STAMP, B_EXE_SIZE)) return reject(IMAGE_MISMATCH) != 0;
  if (!bytes(c->game + B_GAME_CALL, B_HEX_CALL) || !bytes(c->exe + B_EXE_SETTER, B_HEX_SETTER)
      || !bytes(c->exe + B_EXE_UNIT_WORLD, B_HEX_UNIT_WORLD)
      || !bytes(c->exe + B_EXE_GRAPH_GETTER, B_HEX_GRAPH_GETTER) || !bytes(c->exe + B_EXE_ANIM_GETTER, B_HEX_ANIM_GETTER)) return reject(CODE_MISMATCH) != 0;
  uint64_t root = 0, api = 0, target = 0;
  if (!pointer(c->game + B_GAME_API_ROOT, &root) || !pointer(root + L_API_CAMERA, &api) || !slot_read(api + L_API_SETTER, &target)) return reject(API_MISMATCH) != 0;
  *address = api + L_API_SETTER; if (!writable_slot(*address, c->exe)) return reject(SLOT_NOT_WRITABLE) != 0;
  if (target != c->exe + B_EXE_SETTER) return reject(API_MISMATCH) != 0;
  if (!executable(target)) return reject(API_MISMATCH) != 0;
  *setter = target; return true;
}
/* cpp:1043-1050 */
static bool config(const TfpCameraConfig *input, TfpCameraConfig *c) {
  if (!read((uint64_t)(uintptr_t)input, c, sizeof *c) || c->size != sizeof *c || c->abi != 1 || c->reserved != 0 || c->avatar_index >= 8
      || !c->avatar || c->avatar == 0xffffffff || !c->body || c->body == 0xffffffff || !c->camera_unit || c->camera_unit == 0xffffffff
      || c->context < 65536 || c->camera < 65536 || !isfinite(c->forward) || fabsf(c->forward) > .5f
      || !isfinite(c->up) || fabsf(c->up) > .5f || !isfinite(c->smoothing_ms) || c->smoothing_ms < 0 || c->smoothing_ms > 500
      || !isfinite(c->blend_ms) || c->blend_ms < 0 || c->blend_ms > 1000 || c->max_gap_ms < 16 || c->max_gap_ms > 1000) return reject(INVALID_CONFIG) != 0;
  return true;
}
/* cpp:1051-1058 */
static bool prepared(Policy *next) {
  uint32_t kind = 0; float root[3] = {0}, head[3] = {0}, f[3] = {0}, u[3] = {0};
  const uint32_t current = owner_status(next, true); if (current != OK) return reject(current) != 0;
  if (!next->safe || !top(next, &kind) || (kind != 4 && kind != 6)) return reject(OWNER_MISMATCH) != 0;
  if (!graph(next, true) || !points(next, root, head, NULL, atomic_load_explicit(&lock_on, memory_order_relaxed) != 0)) return reject(GRAPH_MISMATCH) != 0;
  if (kind == 4 && !unparented(next)) return reject(SPACE_INVALID) != 0;
  if (!basis(next, f, u)) return reject(POSE_INVALID) != 0;
  return true;
}
/* cpp:1059-1062 */
static bool pin(uint64_t address) { return host.pin(host.user, address); }
/* cpp:1063-1072 */
static bool restore_slot(void) {
  if (possible) {
    uint64_t current = 0; if (!slot_read(slot, &current)) return false;
    if (current == callback()) {
      if (!writable_slot(slot, slot_image)) return false;
      uint64_t observed = 0; if (!cas(callback(), (uint64_t)atomic_load(&original), &observed)) return false;
    }
    possible = false;
  }
  atomic_store_explicit(&installed, 0, memory_order_release); retired_generation = owned_generation; owned_generation = 0; atomic_store_explicit(&reserved, 0, memory_order_release); return true;
}
/* cpp:1073-1075 */
static bool output_generation(uint64_t *output, uint64_t generation) {
  return host.write(host.user, output, &generation, 8);
}
/* cpp:1076-1079 */
static uint32_t failed_begin(uint32_t cause, uint64_t *output) {
  atomic_store_explicit(&armed, 0, memory_order_release); atomic_store_explicit(&callback_generation, 0, memory_order_release); policy_acquire(); policy_release();
  if (!restore_slot()) { error(CLEANUP_PENDING); return 2; }
  output_generation(output, 0); return reject(cause);
}
/* cpp:1080-1086. Each export: the body runs between retail's lock and its
 * Unlock guard, so every return releases the control lock. */
static uint32_t validate_locked(const TfpCameraConfig *input) {
  TfpCameraConfig c; memset(&c, 0, sizeof c); if (!config(input, &c)) return 0; if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; policy_init(&next); next.c = c; uint64_t target = 0, address = 0;
  if (!prepared(&next) || !contract(&c, &target, &address)) return 0;
  const uint32_t current = owner_status(&next, false); if (current != OK) return reject(current);
  error(OK); return 1;
}
uint32_t tfp_camera_validate(const TfpCameraConfig *input) {
  control_acquire(); const uint32_t result = validate_locked(input); control_release(); return result;
}
/* cpp:1087-1121 */
static uint32_t begin_locked(const TfpCameraConfig *input, uint64_t *output) {
  if (!output_generation(output, 0)) return reject(INVALID_CONFIG);
  TfpCameraConfig c; memset(&c, 0, sizeof c); if (!config(input, &c)) return 0; if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; policy_init(&next); next.c = c; uint64_t target = 0, address = 0;
  if (!prepared(&next) || !contract(&c, &target, &address)) return 0;
  if (atomic_load(&original) && (uint64_t)atomic_load(&original) != target) return reject(API_MISMATCH);
  if (!pin((uint64_t)(uintptr_t)&tfp_camera_begin)) return reject(PIN_FAILED);
  if (!pin(c.exe)) return reject(PIN_FAILED);
  atomic_store_explicit(&original, (uintptr_t)target, memory_order_release); slot = address; slot_image = c.exe; possible = false;
  if (++generation_counter == 0) ++generation_counter;
  if (!output_generation(output, generation_counter)) return reject(INVALID_CONFIG);
  owned_generation = generation_counter; atomic_store_explicit(&reserved, 1, memory_order_release); possible = true;
  display_cache.blend = next.blend; display_cache.head_distance = next.head_distance; display_cache.last_dt = next.last_dt; publish_diagnostics(&next);
  if (!writable_slot(slot, c.exe)) return failed_begin(SLOT_NOT_WRITABLE, output);
  uint64_t observed = 0; if (!cas(target, callback(), &observed)) return failed_begin(CAS_FAULT, output);
  if (observed != target) { possible = false; return failed_begin(CAS_MISMATCH, output); }
  atomic_store_explicit(&installed, 1, memory_order_release);
  policy_acquire(); uint32_t kind = 0;
  const uint32_t current = owner_status(&next, false);
  const bool stable = current == OK && top(&next, &kind) && (kind == 4 || kind == 6) && next.safe && graph(&next, false) && dispatch_owned(&next)
      && (kind != 4 || unparented(&next));
  if (stable) {
    smooth_forget(); veil_forget(); seat_forget();
    policy = next; atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&native6_forwarded, 0); atomic_store(&resets, 0);
    publish_diagnostics(&policy);
    atomic_store_explicit(&return_address, c.game + B_GAME_CALL + 4, memory_order_relaxed);
    atomic_store_explicit(&callback_generation, owned_generation, memory_order_release); atomic_store_explicit(&armed, 1, memory_order_release);
  }
  policy_release(); if (!stable) return failed_begin(current != OK ? current : OWNER_MISMATCH, output);
  error(OK); return 1;
}
uint32_t tfp_camera_begin(const TfpCameraConfig *input, uint64_t *output) {
  control_acquire(); const uint32_t result = begin_locked(input, output); control_release(); return result;
}
/* cpp:1122-1129 */
static uint32_t end_locked(uint64_t generation) {
  if (!generation) return reject(TOKEN_MISMATCH);
  if (!atomic_load(&reserved)) { if (generation != retired_generation) return reject(TOKEN_MISMATCH); error(OK); return 1; }
  if (generation != owned_generation) return reject(TOKEN_MISMATCH);
  atomic_store_explicit(&armed, 0, memory_order_release); atomic_store_explicit(&callback_generation, 0, memory_order_release); policy_acquire(); reset(&policy, false); smooth_forget(); veil_forget(); seat_forget(); publish_diagnostics(&policy); policy_release();
  if (!restore_slot()) return reject(CLEANUP_PENDING);
  error(OK); return 1;
}
uint32_t tfp_camera_end(uint64_t generation) {
  control_acquire(); const uint32_t result = end_locked(generation); control_release(); return result;
}
/* cpp:1130-1138 */
static uint32_t read_stats_locked(uint64_t generation, TfpCameraStats *output) {
  if (atomic_load(&reserved) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpCameraStats s; memset(&s, 0, sizeof s); if (!read((uint64_t)(uintptr_t)output, &s, sizeof s) || s.size != sizeof s || s.abi != 1) return reject(INVALID_CONFIG);
  read_diagnostics(&s);
  s.reserved = 0; s.padding = 0; s.installed = atomic_load(&installed); s.armed = atomic_load(&armed); s.error = atomic_load(&last_error);
  s.seen = atomic_load(&seen); s.forwarded = atomic_load(&forwarded); s.replaced = atomic_load(&replaced); s.native6_forwarded = atomic_load(&native6_forwarded); s.resets = atomic_load(&resets);
  return host.write(host.user, output, &s, sizeof s) ? 1 : reject(INVALID_CONFIG);
}
uint32_t tfp_camera_read_stats(uint64_t generation, TfpCameraStats *output) {
  control_acquire(); const uint32_t result = read_stats_locked(generation, output); control_release(); return result;
}
/* cpp:1139 */
uint32_t tfp_camera_error(void) { return atomic_load_explicit(&last_error, memory_order_acquire); }

/* cpp:1141-1211, 1225-1244: NADSC_SetOffsets, FreeAim, FreeAimStats,
 * ObserveParent, ParentStats, SetKick46, KickStats46, SetCameraMotion47,
 * Profile47, ProfileStats47 are retired and not translated. */

/* cpp:1213-1223 */
static uint32_t set_presentation_locked(uint64_t generation) {
  if (!atomic_load(&reserved) || !atomic_load(&armed) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  policy_acquire();
  const uint32_t current = owner_status(&policy, false);
  uint32_t failure = current;
  if (failure == OK && !dispatch_owned(&policy)) failure = API_MISMATCH;
  if (failure == OK && !bytes(policy.c.game + B_GAME_CALL_PAIR, B_HEX_CALL_PAIR)) failure = CODE_MISMATCH;
  if (failure == OK) { policy.presentation_enabled = true; tfp_correction_reset(&policy.correction); }
  policy_release(); return failure == OK ? 1 : reject(failure);
}
uint32_t tfp_camera_set_presentation(uint64_t generation) {
  control_acquire(); const uint32_t result = set_presentation_locked(generation); control_release(); return result;
}
/* cpp:1246-1257 */
static uint32_t set_handoff_locked(uint64_t generation, float entry_ms, float rotation_ms) {
  if (!atomic_load(&reserved) || !atomic_load(&armed) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  policy_acquire();
  const bool allowed = policy.presentation_enabled || (entry_ms == 0 && rotation_ms == 0);
  if (allowed) { policy.handoff_entry_ms = entry_ms; policy.handoff_rotation_ms = rotation_ms; tfp_ease_reset(&policy.entry.ease); tfp_ease_reset(&policy.turn.ease); policy.forwarded_q_valid = false; }
  policy_release(); if (!allowed) return reject(INVALID_CONFIG);
  error(OK); return 1;
}
uint32_t tfp_camera_set_handoff(uint64_t generation, float entry_ms, float rotation_ms) {
  if (!isfinite(entry_ms) || entry_ms < 0 || entry_ms > 500 || !isfinite(rotation_ms) || rotation_ms < 0 || rotation_ms > 500) return reject(INVALID_CONFIG);
  control_acquire(); const uint32_t result = set_handoff_locked(generation, entry_ms, rotation_ms); control_release(); return result;
}
/* cpp:1258-1267 */
static uint32_t handoff_stats_locked(uint64_t generation, TfpCameraHandoffData *out) {
  if (!atomic_load(&reserved) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  policy_acquire_shared();
  TfpCameraHandoffData s; memset(&s, 0, sizeof s);
  s.size = sizeof(TfpCameraHandoffData); s.rotation_proven = policy.rotation_proof.proven ? 1U : 0U; s.entry_ms = policy.handoff_entry_ms; s.rotation_ms = policy.handoff_rotation_ms;
  s.entry_started = policy.entry_started; s.entry_frames = policy.entry_frames; s.entry_rejected = policy.entry_rejected; s.rotation_started = policy.turn_started; s.rotation_frames = policy.turn_frames; s.rotation_rejected = policy.turn_rejected; s.proof_mismatches = policy.rotation_proof.mismatches;
  s.native_mismatches = 0; s.native_proven = policy.native_q_valid ? 1U : 0U; s.entry_last_delay = policy.entry_last_delay; s.entry_max_delay = policy.entry_max_delay; s.padding = 0; s.rotation_start_gap = policy.turn_start_gap; s.padding2 = 0;
  memcpy(s.sample_reasons, policy.sample_reasons, sizeof s.sample_reasons); memcpy(s.pending_paths, policy.pending_paths, sizeof s.pending_paths);
  policy_release_shared(); return host.write(host.user, out, &s, sizeof s) ? 1 : reject(INVALID_CONFIG);
}
uint32_t tfp_camera_handoff_stats(uint64_t generation, TfpCameraHandoffData *out) {
  control_acquire(); const uint32_t result = handoff_stats_locked(generation, out); control_release(); return result;
}
/* cpp:1269-1278 */
static uint32_t set_cut_hold_locked(uint64_t generation, uint32_t mode) {
  if (!atomic_load(&reserved) || !atomic_load(&armed) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  policy_acquire();
  const bool allowed = mode <= 15 && (policy.presentation_enabled || !mode);
  if (allowed) { policy.hold_exit = (mode & 1) != 0; policy.hold_eye = (mode & 2) != 0; policy.anchor_ads = (mode & 4) != 0; policy.hold_seated = (mode & 8) != 0;
    if (!policy.hold_eye) policy.eye_delta_active = false;
    if (!policy.anchor_ads) policy.ads_gap_active = false; }
  policy_release(); if (!allowed) return reject(INVALID_CONFIG);
  error(OK); return 1;
}
uint32_t tfp_camera_set_cut_hold(uint64_t generation, uint32_t mode) {
  control_acquire(); const uint32_t result = set_cut_hold_locked(generation, mode); control_release(); return result;
}
/* cpp:1279-1286 */
uint32_t tfp_camera_entry_trace(TfpCameraEntryTraceData *out) {
  control_acquire();
  TfpCameraEntryTraceData data; memset(&data, 0, sizeof data); data.size = sizeof data;
  policy_acquire_shared();
  data.count = policy.entry_trace_count; data.exit_holds = policy.exit_holds; data.hold_exit = (policy.hold_exit ? 1u : 0u)|(policy.hold_eye ? 2u : 0u)|(policy.anchor_ads ? 4u : 0u)|(policy.hold_seated ? 8u : 0u);
  memcpy(data.rows, policy.entry_trace, sizeof data.rows);
  policy_release_shared();
  const uint32_t result = host.write(host.user, out, &data, sizeof data) ? 1 : 0;
  control_release(); return result;
}
/* cpp:1288-1293 */
static uint32_t set_weapon_key_locked(uint64_t generation, uint32_t key) {
  if (!atomic_load(&reserved) || !atomic_load(&armed) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  policy_acquire(); policy.weapon_key = key; policy_release(); return 1;
}
uint32_t tfp_camera_set_weapon_key(uint64_t generation, uint32_t key) {
  control_acquire(); const uint32_t result = set_weapon_key_locked(generation, key); control_release(); return result;
}
/* cpp:1295-1304 */
uint32_t tfp_camera_anchor_stats(TfpCameraAnchorData *out) {
  control_acquire();
  TfpCameraAnchorData data; memset(&data, 0, sizeof data); data.size = sizeof data;
  policy_acquire_shared();
  data.enabled = policy.anchor_ads ? 1u : 0u; data.frames = policy.ads_gap_frames;
  for (unsigned i = 0; i < 16; ++i) if (eye_memory[i].key && eye_memory[i].ads_known) ++data.known;
  const EyeMemory *current = eye_memory_find(policy.native_weapon_key);
  if (current) if (current->ads_known) memcpy(data.current, current->ads, 12);
  policy_release_shared();
  const uint32_t result = host.write(host.user, out, &data, sizeof data) ? 1 : 0;
  control_release(); return result;
}

/* ---- not in retail: the host and the test reset ---- */
void tfp_camera_set_host(const TfpCameraHost *supplied) {
  if (supplied) host = *supplied; else memset(&host, 0, sizeof host);
 
}
void tfp_camera_set_action_tuning(uint32_t extra_head_mask, uint32_t eye_idle_ms, bool eye_return) {
  atomic_store(&tuning_extra_mask, extra_head_mask); atomic_store(&tuning_eye_idle_ms, eye_idle_ms ? eye_idle_ms : 300u);
  atomic_store(&tuning_eye_return, eye_return ? 1u : 0u);
}
void tfp_camera_set_lock(bool on, float gain, float max_degrees, float settle_ms, float thrown_gain, float thrown_max_degrees, float damping_ms, bool bodycam) {
  atomic_store(&lock_bodycam, bodycam ? 1u : 0u);
  lock_gain = gain; lock_max = max_degrees; lock_settle = settle_ms > 1 ? settle_ms : 300; lock_thrown_gain = thrown_gain; lock_thrown_max = thrown_max_degrees;
  lock_damp_ms = damping_ms > 0 ? damping_ms : 0;
  atomic_store(&lock_on, on ? 1u : 0u);
}
uint64_t tfp_camera_lock_frames(void) { return atomic_load(&lock_frames); }
uint64_t tfp_camera_view_frames(void) { return atomic_load(&view_frames); }
void tfp_camera_set_continuity(bool on, bool trace_frames) { atomic_store(&smooth_on, on ? 1u : 0u); atomic_store(&trace_on, trace_frames ? 1u : 0u); }
void tfp_camera_continuity_event(void) { atomic_store(&smooth_event, 1u); }
void tfp_camera_settle_arm(void) { atomic_store(&settle_arm, 1u); }
void tfp_camera_set_settle(bool on) { atomic_store(&settle_on, on ? 1u : 0u); }
void tfp_camera_set_damp_frame(bool on) { atomic_store(&damp_frame_on, on ? 1u : 0u); }
void tfp_camera_set_orphan(bool on) { atomic_store(&orphan_on, on ? 1u : 0u); }
void tfp_camera_set_dive(bool on) { atomic_store(&dive_on, on ? 1u : 0u); }
void tfp_camera_set_veil(bool on) {
  atomic_store(&veil_armed, on ? 1u : 0u);
  if (!on) { atomic_store(&veil_open, 0); atomic_store(&veil_pending, 0); }
}
void tfp_camera_veil_tick(bool hidden) {
  /* Round 19: the leave the hook noted, taken once. It is only ever noted while armed (with veil_second.txt) or while
   * the veil is open, and set_veil(false), set_veil_second(false) and the helper's begin and end drop it. */
  const bool leave = atomic_exchange(&veil_pending, 0) != 0;
  /* The tick that sees a leave opens the veil (veil_second.txt) or keeps it open (a second leave while it is open, in
   * either way), unless it already found the body drawn again and switched it off; it counts as the first of the two
   * ticks, so the next one closes it. */
  if (leave && !hidden) { atomic_store(&veil_ticks, 1); atomic_store(&veil_open, 1); atomic_fetch_add(&veil_leaves, 1); return; }
  if (!atomic_load(&veil_open)) return;
  if (hidden || atomic_fetch_add(&veil_ticks, 1) + 1 >= 2) atomic_store(&veil_open, 0);
}
void tfp_camera_set_veil_fov(float vertical_radians) {
  atomic_store(&veil_fov, isfinite(vertical_radians) && vertical_radians > 0 && vertical_radians < 3.1f ? float_bits(vertical_radians) : 0u);
}
void tfp_camera_set_veil_second(bool on) { atomic_store(&veil_second, on ? 1u : 0u); if (!on) atomic_store(&veil_pending, 0); }
void tfp_camera_veil_from_head(const TfpHeadTick *tick) {
  if (!tick) { tfp_camera_set_veil(false); return; }
  tfp_camera_veil_tick(tick->full && tick->on_again > 0 && tick->left_on == 0);
  tfp_camera_set_veil(tick->view && tick->active);
}
void tfp_camera_veil_stats(TfpCameraVeilStats *out) {
  out->leaves = atomic_load(&veil_leaves); out->frames = atomic_load(&veil_frames); out->moved = atomic_load(&veil_moved);
  out->behind = atomic_load(&veil_behind); out->largest_m = bits_float(atomic_load(&veil_largest)); out->second = atomic_load(&veil_second) != 0;
  out->under = atomic_load(&veil_under); out->too_far = atomic_load(&veil_far); out->held_back = atomic_load(&veil_held_back);
  out->clamped = atomic_load(&veil_clamped);
  out->floored = atomic_load(&clear_floored); out->floor_largest_m = bits_float(atomic_load(&clear_largest));
}
void tfp_camera_set_clear(bool on) { atomic_store(&clear_on, on ? 1u : 0u); }
uint32_t tfp_camera_lock_eye_held_body(void) {
  const uint32_t body = atomic_load(&eye_held_body);
  if (!body || !host.now) return 0;
  const double tick = now(), at = (double)atomic_load(&eye_held_at);
  return tick >= at && tick-at <= LOCK_OFF_FOOT_MS+1 ? body : 0; /* +1: the stored instant is a whole millisecond */
}
void tfp_camera_orphan_stats(TfpCameraOrphanStats *out) {
  if (!out) return;
  out->frames = atomic_load(&orphan_frames); out->refused = atomic_load(&orphan_refused); out->behind_m = bits_float(atomic_load(&orphan_behind));
}
void tfp_camera_continuity_stats(TfpCameraContinuityStats *out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
  if (!host.lock_exclusive) return; /* before the host is supplied there is nothing to report */
  policy_acquire();
  out->jumps = smooth.jumps; out->edges = smooth.edges; out->restarts = smooth.restarts; out->largest_m = smooth.largest; smooth.largest = 0;
  out->reseats = smooth.reseats; out->cuts = smooth.cuts;
  policy_release();
}
void tfp_camera_alternation_stats(TfpCameraAlternationStats *out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
  if (!host.lock_exclusive) return;
  policy_acquire();
  out->frames = alternation_seen.frames; out->looked = alternation_seen.looked; out->fire = alternation_seen.fire;
  out->largest_m = alternation_seen.largest; alternation_seen.largest = 0; out->kind = alternation_seen.kind;
  policy_release();
}
void tfp_camera_trace_edge(void) {
  if (!atomic_load(&trace_on) || !host.lock_exclusive || !host.now) return;
  policy_acquire();
  const double tick = now();
  trace.edge = tick; trace.until = tick+TRACE_MS; trace.open = true;
  if (trace.head-trace.sent > TRACE_BEFORE) trace.sent = trace.head-TRACE_BEFORE; /* the frames just before the edge, once */
  policy_release();
}
uint32_t tfp_camera_trace_read(TfpCameraTraceRow *rows, uint32_t capacity) {
  uint32_t count = 0;
  if (!rows || !atomic_load(&trace_on) || !host.lock_exclusive) return 0;
  policy_acquire();
  /* Without an open window nothing is handed out; the frames stay in the ring as the next edge's "before". */
  while (trace.open && count < capacity && trace.sent < trace.head) {
    const TraceFrame *frame = &trace.frames[trace.sent % TRACE_RING];
    if (frame->tick > trace.until) { trace.open = false; break; }
    ++trace.sent;
    TfpCameraTraceRow *row = &rows[count++];
    row->ms = (float)(frame->tick-trace.edge); row->kind = frame->kind; row->source = frame->source;
    memcpy(row->eye, frame->eye, 12); memcpy(row->wanted, frame->wanted, 12);
    row->step = frame->step; row->correction = frame->correction; row->entry_ease = frame->entry_ease;
    memcpy(row->root, frame->root, 12); memcpy(row->axes, frame->axes, 36); row->what = frame->what; row->tick = frame->tick;
    memcpy(row->damp, frame->damp, 12); memcpy(row->view, frame->view, 12); row->sock = frame->sock; row->arms = frame->arms;
    memcpy(row->veil, frame->veil, 12); memcpy(row->turn, frame->turn, 12); memcpy(row->veil_head, frame->veil_head, 8);
    memcpy(row->head_shown, frame->head_shown, 8); row->head_known = frame->head_known; row->clear = frame->clear;
    row->turn_carry = frame->turn_carry; row->turn_edge = frame->turn_edge; memcpy(row->game_turn, frame->game_turn, 12); memcpy(row->game_cam, frame->game_cam, 12); row->game_cam_known = frame->game_cam_known;
    memcpy(row->hold, frame->hold, 12); row->hold_known = frame->hold_known; row->dive = frame->dive;
  }
  policy_release();
  return count;
}
void tfp_camera_trace_draw(float sock_m, uint32_t arms) {
  if (!atomic_load(&trace_on) || !host.lock_exclusive) return;
  policy_acquire();
  if (trace.head) { TraceFrame *frame = &trace.frames[(trace.head-1) % TRACE_RING]; frame->sock = sock_m == sock_m ? sock_m : 0; frame->arms = arms; }
  policy_release();
}
void tfp_camera_lock_stats(TfpCameraLockStats *out) {
  if (!out) return;
  out->frames = atomic_load(&lock_frames); out->foreign_frames = atomic_load(&lock_foreign_frames); out->last_kind = atomic_load(&lock_last_kind);
  out->peak_kick_degrees = lock_peak; lock_peak = 0;
  out->missed = atomic_load(&lock_missed); out->missed_kind = atomic_load(&lock_missed_kind); out->missed_error = atomic_load(&lock_missed_error);
  out->missed_apart = bits_float(atomic_load(&lock_missed_apart));
  out->missed_why = atomic_load(&lock_missed_why); out->off_foot = atomic_load(&lock_body_off_foot); out->ragdoll = atomic_load(&lock_body_ragdoll);
  out->head_distance = bits_float(atomic_load(&lock_body_distance));
}
void tfp_camera_down_stats(TfpCameraDownStats *out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
  out->frames = atomic_load(&lock_down_frames); out->refused = atomic_load(&lock_down_refused);
  out->kind = atomic_load(&lock_down_kind); out->error = atomic_load(&lock_down_error); out->why = atomic_load(&lock_down_why);
  out->ragdoll = atomic_load(&lock_body_ragdoll); out->off_foot = atomic_load(&lock_body_off_foot);
  for (unsigned i = 0; i < 3; ++i) out->head[i] = bits_float(atomic_load(&lock_down_head[i]));
  out->head_distance = bits_float(atomic_load(&lock_down_distance));
}
void tfp_camera_seat_stats(TfpCameraSeatStats *out) {
  if (!out) return;
  memset(out, 0, sizeof *out);
  out->frames = atomic_load(&seat_frames); out->refused = atomic_load(&seat_refused); out->glided = atomic_load(&seat_glided);
  out->entries = atomic_load(&seat_entries); out->exits = atomic_load(&seat_exits); out->cuts = atomic_load(&seat_cuts);
  out->in_kind = atomic_load(&seat_in_kind); out->in_ms = atomic_load(&seat_in_ms); out->out_ms = atomic_load(&seat_out_ms);
  out->seated = atomic_load(&seat_now); out->settled = atomic_load(&seat_settled); out->gliding = atomic_load(&seat_gliding); out->hands = atomic_load(&seat_hands);
  out->in_metres = bits_float(atomic_load(&seat_in_metres)); out->out_metres = bits_float(atomic_load(&seat_out_metres));
  for (unsigned i = 0; i < 3; ++i) {
    out->offset[i] = bits_float(atomic_load(&seat_offset[i])); out->left[i] = bits_float(atomic_load(&seat_hand[i])); out->right[i] = bits_float(atomic_load(&seat_hand[3+i]));
  }
}
uint32_t tfp_camera_lock_seat_body(void) {
  const uint32_t body = atomic_load(&seat_body);
  if (!body || !host.now) return 0;
  const double tick = now(), at = (double)atomic_load(&seat_body_at);
  return tick >= at && tick-at <= LOCK_OFF_FOOT_MS+1 ? body : 0; /* 250 ms, as the off-foot body */
}
uint32_t tfp_camera_lock_off_foot_body(void) {
  const uint32_t body = atomic_load(&lock_off_foot_body);
  if (!body || !host.now) return 0;
  const double tick = now(), at = (double)atomic_load(&lock_off_foot_at);
  return tick >= at && tick-at <= LOCK_OFF_FOOT_MS+1 ? body : 0; /* +1: the stored instant is a whole millisecond */
}
const char *tfp_camera_why_text(uint32_t why) {
  static const char *const text[] = {"no proof named",
    "the body's node graph is not the one captured", "the body's world matrices could not be read",
    "the head is not 0.05 to 3 m from the unit root", "the head is beyond 3 m from the unit root and not at its neck",
    "the animation state changed during the frame", "the avatar, body or camera unit is no longer the armed one",
    "the camera request or the dispatch slot changed during the frame", "the body's node tables changed during the frame",
    "the head or neck name changed during the frame", "the camera got a parent during the frame",
    "the ordinary camera's request with the camera linked to a parent, and no native view of the last frames to repeat",
    "the camera has no parent but its local and world position are not the same point"};
  return why < sizeof text/sizeof *text ? text[why] : "unknown";
}
void tfp_camera_reset_for_tests(void) {
  atomic_store(&lock_on, 0); atomic_store(&smooth_on, 0); atomic_store(&trace_on, 0);
  atomic_store(&orphan_on, 0); atomic_store(&orphan_behind, 0); atomic_store(&orphan_frames, 0); atomic_store(&orphan_refused, 0);
  atomic_store(&veil_armed, 0); atomic_store(&veil_open, 0); atomic_store(&veil_ticks, 0); atomic_store(&veil_last_kind, 0); atomic_store(&veil_largest, 0);
  atomic_store(&veil_leaves, 0); atomic_store(&veil_frames, 0); atomic_store(&veil_moved, 0); atomic_store(&veil_behind, 0);
  atomic_store(&veil_second, 0); atomic_store(&veil_pending, 0);
  atomic_store(&veil_fov, 0); atomic_store(&veil_under, 0); atomic_store(&veil_far, 0); atomic_store(&veil_held_back, 0); veil_out_on = false; veil_in_on = false; veil_in_frames = 0; veil_shown_on = false;
  atomic_store(&veil_clamped, 0); veil_eye_known = false; veil_carry_on = false;
  clear_state = (TfpClear){0}; atomic_store(&clear_on, 0); atomic_store(&clear_floored, 0); atomic_store(&clear_largest, 0); atomic_store(&eye_held_body, 0); atomic_store(&eye_held_at, 0);
  atomic_store(&lock_frames, 0); atomic_store(&view_frames, 0); atomic_store(&lock_foreign_frames, 0); atomic_store(&lock_missed, 0); atomic_store(&lock_missed_kind, 0); atomic_store(&lock_missed_error, 0);
  atomic_store(&lock_missed_apart, 0);
  atomic_store(&lock_missed_why, 0); atomic_store(&lock_down_frames, 0); atomic_store(&lock_down_refused, 0); atomic_store(&lock_down_kind, 0);
  atomic_store(&lock_down_error, 0); atomic_store(&lock_down_why, 0); atomic_store(&lock_body_ragdoll, 0); atomic_store(&lock_body_off_foot, 0);
  atomic_store(&lock_off_foot_body, 0); atomic_store(&lock_off_foot_at, 0); atomic_store(&lock_body_distance, 0); atomic_store(&lock_down_distance, 0);
  lock_carry = tfp_q_identity(); lock_was_held = false; lock_down_until = 0; lock_view_known = false; lock_first_known = false; lock_tick = 0; tfp_kick_reset(&lock_kick); lock_last_kick = tfp_q_identity(); tfp_turn_reset(&lock_turn); lock_game_cam_tick = -1; atomic_store(&dive_on, 0);
  lock_damp_ms = 0; atomic_store(&lock_bodycam, 1); tfp_eye_damp_reset(&lock_eye_damp); tfp_damp_frame_reset(&lock_damp_frame); atomic_store(&damp_frame_on, 0);
  seat_forget(); seat_from_tick = 0; seat_from_q = tfp_q_identity(); seat_hand_hashes = 0; seat_hand_count = 0;
  atomic_store(&seat_frames, 0); atomic_store(&seat_refused, 0); atomic_store(&seat_glided, 0); atomic_store(&seat_entries, 0);
  atomic_store(&seat_exits, 0); atomic_store(&seat_cuts, 0); atomic_store(&seat_body_at, 0); atomic_store(&seat_body, 0);
  atomic_store(&seat_in_kind, 0); atomic_store(&seat_in_ms, 0); atomic_store(&seat_out_ms, 0); atomic_store(&seat_settled, 0); atomic_store(&seat_hands, 0);
  atomic_store(&seat_in_metres, 0); atomic_store(&seat_out_metres, 0);
  for (unsigned i = 0; i < 3; ++i) atomic_store(&seat_offset[i], 0);
  for (unsigned i = 0; i < 6; ++i) atomic_store(&seat_hand[i], 0);
  memset(&smooth, 0, sizeof smooth); memset(&trace, 0, sizeof trace); memset(&trace_self, 0, sizeof trace_self); eye_watch_known = false; eye_unsettled_until = 0;
  memset(&smooth_watch, 0, sizeof smooth_watch); atomic_store(&smooth_event, 0); memset(&trace_damp, 0, sizeof trace_damp); memset(&smooth_root, 0, sizeof smooth_root); memset(&damp_climb, 0, sizeof damp_climb);
  atomic_store(&settle_arm, 0); atomic_store(&settle_on, 0); settle_armed_at = 0;
  atomic_store(&original, 0);
  atomic_store(&installed, 0); atomic_store(&armed, 0); atomic_store(&reserved, 0); atomic_store(&last_error, 0);
  atomic_store(&callback_generation, 0);
  atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&native6_forwarded, 0); atomic_store(&resets, 0); atomic_store(&return_address, 0);
  slot = 0; slot_image = 0; generation_counter = 0; owned_generation = 0; retired_generation = 0; possible = false;
  policy_init(&policy);
  memset(eye_memory, 0, sizeof eye_memory); eye_memory_next = 0;
  atomic_store(&tuning_extra_mask, 0); atomic_store(&tuning_eye_idle_ms, 300); atomic_store(&tuning_eye_return, 0);
  display_cache.blend = 0; display_cache.head_distance = 0; display_cache.last_dt = 0;
  atomic_store(&display_sequence, 0); atomic_store(&display_blend, 0); atomic_store(&display_head_distance, 0); atomic_store(&display_last_dt, 0);
  owner_read_probe = NULL;
}
