/* The placement editor as the motion runtime's `editor` (retail runtime.lua:673-674:
 * MotionRuntime.new(controller, backend, editor, ...)). Round 16, 2026-11-24.
 *
 * The editor's yaw, pitch and roll are drawn by the motion runtime, not by the controller: the controller
 * and the pose helper take a placement's x, y, z only (src/pose_adapter.c), and the rotation reaches the
 * rig as the motion modifier the runtime hands the pose helper (set_pose_motion; the rig node it is
 * written on carries the arms and the gun, src/arm_graft.h tfp_graft_view_node). While the editor is
 * active the runtime draws editor:preview_rotation() alone (motion_runtime.lua:281-296); in play it adds
 * the action placement's yaw, pitch and roll to its own (:384-388). Until round 16 the feel build gave
 * the runtime no editor, so the editor's rotation drags changed the draft and nothing drawn.
 *
 * e NULL: no editor is bound; it answers as an idle one. */
#ifndef TFP_EDITOR_MOTION_H
#define TFP_EDITOR_MOTION_H
#include "motion_runtime.h"
#include "placement_editor.h"
#include <string.h>

/* editor:is_active(): self.phase ~= 'idle' (placement_editor.lua:192). Never raises. */
static inline bool tfp_editor_motion_is_active(const TfpEditor *e, bool *active) {
  *active = e && tfp_editor_is_active(e);
  return true;
}
/* editor:preview_rotation(): {yaw, pitch, roll} of the draft while active, else nil (placement_editor.lua:196). */
static inline TfpMotionRuntimeGot tfp_editor_motion_preview(const TfpEditor *e, TfpActionPlacement *out) {
  double rotation[3];
  if (!e || !tfp_editor_preview_rotation(e, rotation)) return TFP_MOTION_GOT_NIL;
  *out = (TfpActionPlacement){0, 0, 0, rotation[0], rotation[1], rotation[2], TFP_PLACE_YAW | TFP_PLACE_PITCH | TFP_PLACE_ROLL};
  return TFP_MOTION_GOT_VALUE;
}
/* Round 20 (tests/editor_ads_test.c): the editor opened in full aim. Not in retail.
 * The action the binder opens the editor on: "ads" in full aim (what the player sees there is the ADS placement:
 * the hip placement's weight is 0 in full aim), NULL otherwise (the editor's own first, idle). */
static inline const char *tfp_editor_motion_open_action(bool full_aim) { return full_aim ? "ads" : NULL; }
/* The ADS draft, for the pose helper's ADS placement every frame while the editor is active on the "ads" action
 * (editor_preview_ads in src/motion_runtime.h). NIL: closed, opening, or another action. */
static inline TfpMotionRuntimeGot tfp_editor_motion_ads_draft(const TfpEditor *e, TfpActionPlacement *out) {
  double v[6];
  if (!e || !tfp_editor_is_active(e) || strcmp(tfp_editor_preview_action(e), "ads") || !tfp_editor_preview_place(e, v)) return TFP_MOTION_GOT_NIL;
  *out = (TfpActionPlacement){v[0], v[1], v[2], v[3], v[4], v[5], TFP_PLACE_ALL};
  return TFP_MOTION_GOT_VALUE;
}
/* What the controller (the hip placement) is handed for the editor's C_SET_PLACEMENT: while the "ads" action is
 * edited, the placement the editor opened with, so editing the ADS placement leaves the hip one alone (in retail the
 * hip took the ADS draft, invisibly in full aim, and slid from it when the aim ended). Else the value given. */
static inline TfpEdPlace tfp_editor_motion_controller_value(const TfpEditor *e, const TfpEdPlace *given) {
  TfpEdPlace original;
  if (e && tfp_editor_is_active(e) && !strcmp(tfp_editor_preview_action(e), "ads") && tfp_editor_original_place(e, &original)) return original;
  return *given;
}
#endif
