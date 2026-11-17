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
 * src/wire_win.c supply the real ones; the tests supply scripted ones. docs/WIRING-VIEW.md lists
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

typedef struct TfpWirePlatform {
  void *user;
  /* Seconds, never going back; NAN: no clock. Retail's is Application.time_since_launch; the view
   * build's is the performance counter (docs/WIRING-VIEW.md, differences). */
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
typedef struct TfpWire {
  TfpRt rt;
  TfpRtHost host;
  TfpWirePlatform platform;
  TfpWireHelpers helpers;
  TfpOpsHost memory;           /* the caller's: memory, native calls, page questions */
  TfpOpsHost ops_host;         /* what the backend gets: the same through this binder, plus runtime.lua's four hooks */
  TfpOps ops;
  /* The pose (editor) helper is not wired in the view build: this stand-in arms at once, places
   * nothing and reports a steady, error-free lease (docs/WIRING-VIEW.md, "no off path in retail"). */
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
  /* F11: set by the key poll, taken by menu:update() at the end of the frame, as retail's menu does. */
  bool toggle_requested;
  /* The last diagnostics row (runtime.lua:513), for the session log. */
  bool has_row, row_has_eye, row_has_fov, frame_full;
  uint64_t frame_identity;      /* the weapon-level identity of this frame's full snapshot */
  double row_eye[3], row_fov_native, row_fov_applied;
  TfpViewLog log;
  char entry_fault[256];
} TfpWire;

/* Builds the host (off answers first, then the wired view) and the runtime's start state. The
 * TfpOpsHost's four hook pointers are filled in here; its other members are the caller's. */
void tfp_wire_init(TfpWire *w, const TfpOpsHost *memory, const TfpWirePlatform *platform, const TfpWireHelpers *helpers);
/* One of the runtime's functions, from outside the runtime (the tick, a key): texts handed over
 * during it stay valid until it returns. */
TfpRtStatus tfp_wire_call(TfpWire *w, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue *ret);
/* The per-update order: on_update, then one session-log sample. `extra` goes on the View line. */
void tfp_wire_frame(TfpWire *w, const char *extra);

/* Glue with logic, exported for tests/wire_glue_test.c. */
void tfp_wire_snapshot_table(TfpWire *w, const TfpOpsSnapshot *s, TfpRtTable *out);
void tfp_wire_lease_table(const TfpWireLease *lease, TfpRtTable *out, char identity[24], char player[24]);
void tfp_wire_controller_state(TfpWire *w, TfpRtTable *out);
void tfp_wire_view(const TfpOpsSnapshot *s, const char *reason_storage, TfpView *out);
/* controller.lua:112-135 (set_placement): 0 accepted, else the refusal text. */
const char *tfp_wire_placement_check(const double xyz[3], const double angles[3]);
void tfp_wire_identity_text(char out[24], char level, uint64_t key);
#endif
