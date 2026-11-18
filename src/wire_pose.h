/* The pose (editor) helper of the feel build, bound for real: weapon and arm placement.
 *
 *   backend_ops.c (backend.lua)  ->  TfpOpsPoseHelper  ->  wire_pose.c  ->  pose_adapter.c (pose.lua)  ->  pose_helper.c
 *                                                                                                          (native_ads_editor.cpp)
 *
 * It replaces the stand-in of the view build (src/wire_rt.c, "the pose stand-in"). Nothing here is
 * Windows: the helper's own host (memory, the dispatch slots, locks, the six hook values) is set by
 * src/wire_win_pose.c in the mod and by the test in tests/wire_pose_test.c.
 *
 * Look gain ("input mode"): the profile asks for it as retail does (normalize_zoom_input = true), the
 * helper installs retail's two extra hooks and its code proof refuses on build 25480438, exactly as
 * retail's helper behaves when one of its pins differs: no gain is ever changed. See
 * docs/build-25480438/BUILD-DIFFERENCES.md, "Look gain". */
#ifndef TFP_WIRE_POSE_H
#define TFP_WIRE_POSE_H
#include "wire.h"
/* controller.h (through wire.h) and pose_helper.h both name a type TfpPoseStats: the controller's view
 * of a status poll, and NADSE_Stats. In every file that includes this header the helper's one is
 * TfpPoseHelperStats. Neither has linkage, and the layout of the helper's is asserted in
 * pose_helper.h. Include this header before pose_helper.h / pose_adapter.h, never after them. */
#ifdef TFP_POSE_HELPER_H
#error "include wire_pose.h before pose_helper.h"
#endif
#define TfpPoseStats TfpPoseHelperStats
#include "pose_adapter.h"
#undef TfpPoseStats

/* A handle as the backend holds it (retail: the adapter's handle table, compared by identity). One
 * allocation per begin that took a generation, never reused, like a lease. */
typedef struct TfpWirePoseHandle {
  TfpOpsHandle ops;            /* first: a TfpOpsHandle pointer is the handle */
  TfpPoseHandle pose;
  bool steady;
} TfpWirePoseHandle;

/* The weapon placement jump meter (tfp_wire_pose_frame). */
typedef struct TfpWirePoseMeter {
  const TfpWirePoseHandle *handle; /* the lease the last sample belongs to; NULL: no last sample */
  double shift[3], step[3], q[4], turn;
  /* the running second */
  double jump_m, jump_deg, blend;
  unsigned frames, sampled, placements, motions, begins, finishes;
  bool has_blend;
} TfpWirePoseMeter;

typedef struct TfpWirePose {
  TfpWire *w;
  TfpOpsPoseHelper helper;     /* what w->pose_helper points at */
  TfpPoseAdapter adapter;
  TfpPoseAdapterIo io;
  TfpPosePrepared prepared;    /* prepare()'s token: retail's {owner=self} */
  TfpOpsToken preflight;       /* the debug entry `preflight` */
  /* What the helper last accepted, for the meter: hip placement, ADS placement, motion offset and rotation. */
  double hip[3], ads[3], motion[3], q[4];
  /* The last statistics the backend polled (once per update while a lease runs). */
  bool polled, has_view;
  const TfpWirePoseHandle *polled_handle;
  TfpPoseStatsView view;
  /* The tables handed to the runtime. */
  TfpPoseDepthView depth;
  TfpPoseInputView input;
  TfpWirePoseMeter meter;
  /* The session log. */
  bool validated;
  uint32_t error_said;         /* the helper error number last reported */
  int look_said;               /* the look-gain state last reported: -1 none yet */
  unsigned look_seconds;       /* seconds the current lease has been sampled */
  const TfpWirePoseHandle *look_handle;
  char refusal[360];           /* the refusal last written, so a repeated one is one line */
  char look[24];               /* the look= field of the second's fragment */
} TfpWirePose;

/* The linked helper (the tfp_pose_* exports of pose_helper.h). */
const TfpPoseHelperApi *tfp_wire_pose_linked(void);

/* After tfp_wire_init and before the runtime's on_enable. `state` must outlive the binder. `io`: the
 * time of the adapter's statistics tick (retail: GetTickCount64; no clock function). Sets
 * w->pose_helper, w->ext[TFP_WIRE_EXT_POSE], w->lease_extra, w->controller_extra and the host's
 * helper_depth_stats, helper_input_stats and backend_prepare_pose; logs one `Ready: pose …` line. */
void tfp_wire_pose_bind(TfpWire *w, TfpWirePose *state, const TfpPoseHelperApi *api, const TfpPoseAdapterIo *io,
                        TfpPoseAdapterProfile profile);

/* Once per tick, after on_update (it has the signature of TfpWire.frame_extra). Adds this frame to the
 * weapon placement jump meter:
 *
 *   shift(t) = (hip placement + motion offset) * blend(t)  [+ ADS placement while the helper's target weight is 0]
 *   turn(t)  = the angle between the motion rotation of this frame and of the frame before
 *   wjump    = the largest |(shift(t) - shift(t-1)) - (shift(t-1) - shift(t-2))| of the second, in metres,
 *              and the largest |turn(t) - turn(t-1)|, in degrees
 *
 * hip / ADS placement, motion offset and rotation are the values the helper last accepted from the
 * backend; blend and target weight are the helper's own (NADSE_Stats.blend, .target_weight: the weight
 * its last rig-root write used), taken from the status poll the controller makes each update. shift is
 * therefore the rig-root shift this mod commands in rig-local metres, the same expression the helper
 * adds to the game's own pose (pose_helper.c position_locked). The second difference is what the frame
 * before does not explain: a steady slide gives 0, an ease gives millimetres (a 150 ms blend of the
 * shipped placement at 60 frames a second: about 1.4 cm at most, in the two frames where the ease
 * starts and ends), a snap gives its size. Not in it, because the helper keeps them to itself: the
 * draw animation of a steady entry, the zoom depth term, and the game's own animation underneath.
 * A lease's first sample only starts the history (the size of what appears with a steady entry is on
 * the `Pose: begin` line). Frames without a poll of an armed lease break the history. */
void tfp_wire_pose_frame(TfpWire *w);

/* Once a second while the mode is on: writes
 *   pose=armed blend=1.00 wjump=0.0012m/0.31deg placements=N motion=N err=0 writes=N look=refused(2)
 * and starts a new second. pose: armed, expired, unarmed (a handle is owned but the helper is not
 * armed), none. placements / motion: calls the helper accepted this second. err: the helper's error
 * number. writes: shifted rig-root writes of this lease so far. look: off, refused(reason), on, -.
 * Also reports a change of the look-gain state (`Refused: look gain: …`). Returns the characters
 * written (0 when not bound). Never call it from a hook. */
size_t tfp_wire_pose_second(TfpWire *w, char *out, size_t size);

/* The helper's running counts from the last status poll (for controller.lua's weapon_updates /
 * zero_restores); false: no poll yet. */
/* For a live replace: bit 0, the adapter owns a helper lease; bit 1, the linked helper's look-gain
 * normalisation holds something in the game (tfp_pose_input_held). Reads only. */
unsigned tfp_wire_pose_held(const TfpWire *w);
bool tfp_wire_pose_counts(const TfpWire *w, double *weapon_updates, double *zero_restores);

/* Exported for tests/wire_pose_test.c. */
void tfp_wire_pose_config(const TfpOpsHelperConfig *from, TfpPoseAdapterConfig *out);
const char *tfp_wire_pose_error_text(uint32_t code);
#endif
