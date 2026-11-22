/* The binder of the view build: the translated units joined behind the runtime host.
 *
 *   tfp_runtime.c (runtime.lua)  ->  TfpRtHost  ->  wire_rt.c:       backend_ops, controller, camera / FOV / near / blur
 *                                                                     adapters (and through them the four helpers),
 *                                                                     transition, the clock, the mouse, the session log
 *                                                   wire_recovery.c: recovery
 *                                                   wire_off.c:      every other module, answering "off"
 *
 * Nothing here is Windows: memory, native calls and page questions come in as a TfpOpsHost, time, the
 * mouse and the log as a TfpWirePlatform, the helpers as their function tables. src/mod_view.c and
 * src/wire_win.c supply the real ones; the tests supply scripted ones. docs/wiring/WIRING-VIEW.md lists
 * every binding. */
#ifndef TFP_WIRE_H
#define TFP_WIRE_H
#include "tfp_runtime.h"
#include "backend_ops.h"
#include "controller.h"
#include "camera_adapter.h"
#include "fov_adapter.h"
#include "near_adapter.h"
#include "blur_adapter.h"
#include "transition.h"
#include "wire_log.h"
#include "wire_action.h"

typedef struct TfpWirePlatform {
  void *user;
  /* Seconds, never going back; NAN: no clock. Retail's is Application.time_since_launch; the view
   * build's is the performance counter (docs/wiring/WIRING-VIEW.md, differences). */
  double (*clock)(void *user);
  /* The level of a mouse button: 1 down, 0 up, -1 no answer. button 1: right, 2: middle. */
  int (*mouse)(void *user, int button);
  void (*log)(void *user, const char *text);
} TfpWirePlatform;
typedef struct TfpWireHelpers {
  const TfpCameraHelperApi *camera;
  const TfpFovHelperApi *fov;
  const TfpNearHelperApi *near_clip;
  const TfpBlurHelperApi *blur;
} TfpWireHelpers;

/* One native-view lease: the controller's view of it, the backend's, and what the lens adapters read. */
typedef struct TfpWireLease {
  TfpLease view;            /* first: a TfpLease pointer is the lease */
  TfpOpsLease ops;
  uint64_t serial;          /* never repeats: the identity the FOV and blur leases bind to */
  TfpFovPoseLease fov;
  TfpBlurPoseLease blur;
  TfpNearPoseLease near_clip;
} TfpWireLease;

/* A snapshot handed to the runtime: the table's ref points here. */
typedef struct TfpWireSnap {
  TfpOpsSnapshot s;
  char identity[24], player[24];
} TfpWireSnap;

enum { TFP_WIRE_SNAPS = 48, TFP_WIRE_VIEWS = 8, TFP_WIRE_ARENA = 16384 };
/* Whose state sits in TfpWire.ext[]. */
enum { TFP_WIRE_EXT_SETTINGS, TFP_WIRE_EXT_POSE, TFP_WIRE_EXT_MOTION, TFP_WIRE_EXT_ENGINE, TFP_WIRE_EXT_EFFECTS, TFP_WIRE_EXT_LOG,
       TFP_WIRE_EXT_SPARE, TFP_WIRE_EXT_MENU, TFP_WIRE_EXT_EDITOR, TFP_WIRE_EXT_PANEL, TFP_WIRE_EXT_N };
/* What the action hold's line says about the pose helper and the two rigs: gathered by the feel build
 * (src/mod_view.c), which has the helper and the arm drive; a has_* false or a NULL text prints "-". */
typedef struct TfpWireHoldProbe {
  const char *gun_parent;                 /* "rig", "body", "other", "none" */
  bool has_camera, camera_parent_is_rig_eye, has_pose, pose_armed, has_eye, has_rig_eye;
  uint32_t pose_error;
  uint64_t node5_calls;                   /* running count; the line prints the difference to the line before */
  double eye[3], rig_eye[3];
} TfpWireHoldProbe;
struct TfpWire;
/* The action hold (the Weapon put-away option, src/wire_options.h; tfp_wire_hold_set). Everything false, 0 and NULL while off:
 * then nothing here is called and nothing is written. */
typedef struct TfpWireHold {
  bool on, was_holding, has_mask, gun_off_rig;
  uint32_t mask;                          /* the action flags of the line before */
  unsigned after;                         /* ticks still to be written after a change of the flags */
  double next_line;
  uint64_t node5_calls;
  /* Each tick: armed (a lease runs and an action would be held) and holding (one is). The feel build
   * hands them to the pose helper and the arm drive. May be NULL. */
  void (*apply)(struct TfpWire *w, bool armed, bool holding);
  /* For the line. view NULL: the player is not proven. weapon_unit: the lease's gun, 0 without a lease. May be NULL. */
  void (*probe)(struct TfpWire *w, const TfpOpsHold *view, uint32_t weapon_unit, TfpWireHoldProbe *out);
} TfpWireHold;
typedef struct TfpWire {
  TfpRt rt;
  TfpRtHost host;
  TfpWirePlatform platform;
  /* Optional, set after tfp_wire_init: where the last mouse answer came from ("engine", "windows"), for the session log. */
  const char *(*mouse_source)(void);
  TfpWireHelpers helpers;
  TfpOpsHost memory;           /* the caller's: memory, native calls, page questions */
  TfpOpsHost ops_host;         /* what the backend gets: the same through this binder, plus runtime.lua's four hooks */
  TfpOps ops;
  /* The pose (editor) helper is not wired in the view build: this stand-in arms at once, places
   * nothing and reports a steady, error-free lease (docs/wiring/WIRING-VIEW.md, "no off path in retail"). */
  TfpOpsPoseHelper null_pose;
  TfpOpsHandle null_handle;
  bool null_armed;
  TfpBackendApi api;
  TfpController controller;
  TfpOpsToken token;
  bool has_token;
  struct { const TfpView *view; TfpOpsSnapshot s; } views[TFP_WIRE_VIEWS]; /* the snapshots behind the controller's TfpViews */
  unsigned view_next;
  char view_reason[TFP_WIRE_VIEWS][160];
  uint64_t lease_serial;
  TfpCameraAdapterApi camera_api;
  TfpCameraAdapter camera;
  TfpFovIo fov_io;
  TfpFovLease fov;
  TfpBlurIo blur_io;
  TfpBlurLease blur;
  TfpNearIo near_io;
  TfpNearLease near_clip;
  TfpTransition transition;
  TfpTransitionPacket packet;   /* the packet behind the TRANS table's ref */
  bool has_packet;
  TfpWireSnap snaps[TFP_WIRE_SNAPS];
  unsigned snap_next;
  char arena[TFP_WIRE_ARENA];
  size_t arena_used;
  int depth;                    /* nesting of tfp_wire_call */
  double revision;              /* backend.action_placement_revision */
  bool has_revision, global_ours;
  /* The aim button between ticks (src/wire_action.h): the snapshot the keeper would act on, kept for the probe. */
  TfpAimWatch aim;
  TfpOpsSnapshot aim_snapshot;
  bool aim_has_snapshot;
  /* F11: set by the key poll, taken by menu:update() at the end of the frame, as retail's menu does. */
  bool toggle_requested;
  /* The last diagnostics row (runtime.lua:513), for the session log. */
  bool has_row, row_has_eye, row_has_fov, frame_full;
  uint64_t frame_identity;      /* the weapon-level identity of this frame's full snapshot */
  double row_eye[3], row_fov_native, row_fov_applied;
  TfpViewLog log;
  char entry_fault[256];
  /* ---- the feel build (docs/wiring/WIRING-FEEL.md): each module's binder hangs its state and hooks here.
   * All NULL in the view build, which then behaves exactly as before. ---- */
  void *ext[TFP_WIRE_EXT_N];
  /* The pose (editor) helper behind the backend; NULL: the stand-in above. Set before on_enable. */
  const TfpOpsPoseHelper *pose_helper;
  /* A placement value the runtime hands back (a ref made by Actions.resolve and friends) decoded;
   * the shipped default table is decoded by the binder itself. */
  bool (*placement_decode)(struct TfpWire *w, TfpRtValue value, TfpOpsPlacement *out);
  /* More fields for the LEASE table (the helper's depth_stats / input_stats functions) and for
   * controller:result() (blend). */
  void (*lease_extra)(struct TfpWire *w, const TfpWireLease *lease, TfpRtTable *out);
  void (*controller_extra)(struct TfpWire *w, TfpRtTable *out);
  /* After on_update, before the session-log sample: modules add their per-frame numbers. */
  void (*frame_extra)(struct TfpWire *w);
  /* The pose helper's running counts at the last status poll (controller.lua's weapon_updates /
   * zero_restores, which the backend's statistics do not carry). false: none. */
  bool (*pose_counts)(const struct TfpWire *w, double *weapon_updates, double *zero_restores);
  TfpWireHold hold;
} TfpWire;

/* Builds the host (off answers first, then the wired view) and the runtime's start state. The
 * TfpOpsHost's four hook pointers are filled in here; its other members are the caller's. */
void tfp_wire_init(TfpWire *w, const TfpOpsHost *memory, const TfpWirePlatform *platform, const TfpWireHelpers *helpers);
/* One of the runtime's functions, from outside the runtime (the tick, a key): texts handed over
 * during it stay valid until it returns. */
TfpRtStatus tfp_wire_call(TfpWire *w, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue *ret);
/* The per-update order: on_update, then one session-log sample. `extra` goes on the View line. */
void tfp_wire_frame(TfpWire *w, const char *extra);
/* Between ticks, from the engine's script call (game thread only). due: a probe may be made now (armed, and
 * not too many this frame). probe: the levels just read (1 down, 0 up, -1 no answer); true when the keeper
 * acted. Its writes are the keeper's own two, on the tick's snapshot, re-checked against memory. */
bool tfp_wire_aim_due(TfpWire *w);
bool tfp_wire_aim_probe(TfpWire *w, int right, int middle);

/* The action hold's switch. Before on_enable (the controller made there takes it); later calls reach the
 * controller and the backend that exist, so it may be switched in the middle of a session: on the game thread,
 * and not while the controller holds or a lease waits for its native exit (tfp_wire_options_busy). Off after
 * tfp_wire_init. */
void tfp_wire_hold_set(TfpWire *w, bool on);
bool tfp_wire_hold_on(const TfpWire *w);
/* The hold's part of a tick; tfp_wire_frame calls it after on_update. Does nothing while the switch is off. */
void tfp_wire_hold_frame(TfpWire *w);
/* The "Hold:" line. view NULL: the player is not proven, `unproven` says why (may be NULL). */
void tfp_wire_hold_line(const TfpWire *w, const TfpOpsHold *view, const char *unproven, const TfpWireHoldProbe *probe, uint64_t node5_delta,
                        char *out, size_t size);

/* Glue with logic, exported for tests/wire_glue_test.c. */
void tfp_wire_snapshot_table(TfpWire *w, const TfpOpsSnapshot *s, TfpRtTable *out);
void tfp_wire_lease_table(const TfpWireLease *lease, TfpRtTable *out, char identity[24], char player[24]);
void tfp_wire_controller_state(TfpWire *w, TfpRtTable *out);
void tfp_wire_view(const TfpOpsSnapshot *s, const char *reason_storage, TfpView *out);
/* controller.lua:112-135 (set_placement): 0 accepted, else the refusal text. */
const char *tfp_wire_placement_check(const double xyz[3], const double angles[3]);
void tfp_wire_identity_text(char out[24], char level, uint64_t key);

/* ---- for the other binder files (wire_*.c of the feel build) ---------------------------------
 * A module binder is `tfp_wire_<module>_bind(TfpWire *, …)`, called after tfp_wire_init and before
 * the runtime's on_enable: it keeps its state in w->ext[], overrides the pointers of w->host that
 * wire_off.c answered, and may set the hooks above and the members of w->ops_host. */
/* A text that stays valid until the outermost runtime call returns. */
const char *tfp_wire_keep(TfpWire *w, const char *text);
/* A Lua error from a host pointer: `return tfp_wire_raise(h, "…")`. */
bool tfp_wire_raise(TfpRtHost *h, const char *text);
/* The snapshot behind a SNAP table's ref (NULL: not one of this binder's). */
TfpWireSnap *tfp_wire_snap_of(TfpWire *w, const TfpRtTable *table);
/* The controller's lease (NULL: none), and the binder's lease behind the backend's. */
TfpWireLease *tfp_wire_held(TfpWire *w);
const TfpWireLease *tfp_wire_lease_of_ops(const TfpOpsLease *lease);
/* The lease behind a LEASE table's ref. */
const TfpWireLease *tfp_wire_lease_of_table(const TfpRtTable *table);
/* The runtime's monotonic clock (its entry `clock`); NAN: none. */
double tfp_wire_clock(TfpWire *w);
/* The mod's log (one line). */
void tfp_wire_say(TfpWire *w, const char *text);
/* controller:result().mode == 'pose_offset'. */
bool tfp_wire_pose_offset(const TfpWire *w);
const char *tfp_wire_phase_text(TfpPhase phase);
/* A placement value: the shipped default table, or whatever placement_decode knows. */
bool tfp_wire_placement_of(TfpWire *w, TfpRtValue value, TfpOpsPlacement *out);
/* controller:get_placement() by its numbers (its table carries the identity &w->ops.pose_placement). false: a field is not a number. */
bool tfp_wire_controller_placement(TfpWire *w, TfpOpsPlacement *out);
/* A number that is an address (a Lua number holding an integer below 2^53). */
bool tfp_wire_address_of(double value, uint64_t *out);
#endif
