/* The "module off" answers of the view build (0.6.0-view.1).
 *
 * The runtime order (tfp_runtime.c, retail runtime.lua) calls every module of the mode through
 * TfpRtHost. The view build binds only the view (backend, controller, camera, FOV, near, blur,
 * recovery, transition, the clock and the mouse) to real code. Every other pointer gets one of the
 * fixed answers below: what the runtime sees from a module that exists, is idle, clean and never
 * refuses, so the per-update order runs exactly as it does in retail with those modules doing
 * nothing. docs/WIRING-VIEW.md says for each group whether retail has such an off path itself.
 *
 * The lists are X-macros so that tests/wire_off_test.c can wrap every one of them and compare its
 * answer, call by call, with what the scripted collaborator of the retail reference run answered
 * (tests/vectors/view_off_vectors.h, cut from the unmodified runtime.lua). No Windows, no game. */
#ifndef TFP_WIRE_OFF_H
#define TFP_WIRE_OFF_H
#include "tfp_runtime.h"

#define TFP_OFF_EDITOR "The placement editor is not part of the view build"
#define TFP_OFF_MARKERS "Aiming markers are not part of the view build"
#define TFP_OFF_FIRD "Fird Person is not part of the view build"
#define TFP_OFF_CAMERA_DEBUG "Retired in the camera helper"
#define TFP_OFF_PREFLIGHT "Preflight is not part of the view build"

/* bool f(TfpRtHost *h): returns (true). */
#define TFP_OFF_S0(X) \
  X(editor_new) X(editor_update) X(menu_new) X(menu_begin_shutdown) X(menu_update) X(compat_new) X(model_shadows_new) \
  X(head_new) X(movement_new) X(dof_new) X(motion_new) X(motion_update) X(native_aim_source) X(native_aim_new) \
  X(aim_markers_source) X(aim_markers_new) X(actions_configure_catalogue) X(head_positioner_new) X(fird_update) \
  X(fird_entry_new) X(fird_entry_cancel) X(fird_warm_new) X(fird_render_new) X(ready_pose_new) X(held_fire_new) \
  X(ready_timing_new) X(capture_gate_new) X(capture_gate_reset)

/* bool f(TfpRtHost *h, TfpRtValue *out1): X(name, kind, text). */
#define TFP_OFF_S1(X) \
  X(editor_is_active, FALSE, 0) X(editor_status, NIL, 0) X(menu_native_ads, FALSE, 0) X(menu_ads_mode, NIL, 0) \
  X(compat_foreign, FALSE, 0) X(compat_is_clean, TRUE, 0) X(head_is_clean, TRUE, 0) X(movement_is_clean, TRUE, 0) \
  X(arm_pieces_is_clean, TRUE, 0) X(grip_hand_is_clean, TRUE, 0) X(dof_is_clean, TRUE, 0) X(dof_status, TEXT, "off") \
  X(native_aim_is_clean, TRUE, 0) X(aim_markers_is_clean, TRUE, 0) X(fird_is_enabled, FALSE, 0) X(fird_is_clean, TRUE, 0) \
  X(fird_aim_new, NIL, 0) X(fird_native_ads_new, NIL, 0) X(fird_fov_new, NIL, 0) X(fird_entry_is_enabled, FALSE, 0) \
  X(fird_entry_is_pending, FALSE, 0) X(fird_entry_uses_fp, FALSE, 0) X(fird_entry_status, NIL, 0) \
  X(fird_warm_is_clean, TRUE, 0) X(ready_pose_is_enabled, FALSE, 0) X(ready_pose_is_clean, TRUE, 0) \
  X(ready_source_new, NIL, 0) X(diagnostics_snapshot, NIL, 0)

/* bool f(TfpRtHost *h, TfpRtValue *out1, TfpRtValue *out2): X(name, kind of out1, text of out2 or 0 for nil). */
#define TFP_OFF_S2(X) \
  X(editor_shutdown, TRUE, 0) X(editor_request_open, FALSE, TFP_OFF_EDITOR) X(menu_shutdown, TRUE, 0) \
  X(compat_update, TRUE, 0) X(compat_shutdown, TRUE, 0) X(compat_prepare, TRUE, 0) X(compat_stop, TRUE, 0) \
  X(head_stop, TRUE, 0) X(head_update, TRUE, 0) X(head_shutdown, TRUE, 0) \
  X(movement_stop, TRUE, 0) X(movement_update, TRUE, 0) X(movement_shutdown, TRUE, 0) \
  X(arm_pieces_stop, TRUE, 0) X(grip_hand_stop, TRUE, 0) \
  X(dof_update, TRUE, 0) X(dof_stop, TRUE, 0) X(dof_shutdown, TRUE, 0) X(motion_reset, TRUE, 0) \
  X(native_aim_shutdown, TRUE, 0) X(native_aim_update, TRUE, 0) X(native_aim_stop, TRUE, 0) \
  X(aim_markers_shutdown, TRUE, 0) X(aim_markers_stop, TRUE, 0) X(aim_markers_update, TRUE, 0) \
  X(aim_markers_target, NIL, 0) X(aim_markers_setting, NIL, 0) \
  X(camera_observe_parent, NIL, 0) X(camera_profile, NIL, 0) X(camera_profile_stats, NIL, 0) X(camera_kick_stats, NIL, 0) \
  X(camera_parent_stats, NIL, 0) X(camera_handoff_stats, NIL, 0) X(camera_free_aim_stats, NIL, 0) \
  X(head_positioner_get_saved, NIL, 0) X(head_positioner_placement_target, NIL, 0) X(head_positioner_is_enabled, FALSE, 0) \
  X(head_positioner_is_clean, TRUE, 0) X(head_positioner_get_placement, NIL, 0) X(head_positioner_result, NIL, 0) \
  X(fird_render_pose, TRUE, 0) X(fird_stop, TRUE, 0) X(fird_refresh_target, FALSE, TFP_OFF_FIRD) \
  X(fird_start_native, FALSE, TFP_OFF_FIRD) X(fird_cached_capture, NIL, 0) X(fird_get_placement, NIL, 0) \
  X(fird_result, NIL, 0) X(fird_placement_target, NIL, 0) X(fird_entry_update, TRUE, 0) \
  X(fird_entry_request, FALSE, TFP_OFF_FIRD) X(fird_warm_stop, TRUE, 0) X(fird_render_stop, TRUE, 0) \
  X(fird_render_start, FALSE, TFP_OFF_FIRD) X(ready_pose_stop, TRUE, 0) X(held_fire_stop, TRUE, 0)

/* bool f(TfpRtHost *h, TfpRtTable *out1): X(name, shape). EMPTY: a table with no field the runtime
 * reads; IDLE: {phase='idle'}; DOF: {owned=false}; ABSENT: nil. */
#define TFP_OFF_ST(X) \
  X(compat_result, EMPTY) X(head_result, IDLE) X(movement_result, IDLE) X(dof_result, DOF) X(motion_result, EMPTY) \
  X(native_aim_result, EMPTY) X(aim_markers_result, EMPTY) X(arm_pieces_result, EMPTY) X(grip_hand_result, EMPTY) \
  X(ready_pose_result, EMPTY) X(held_fire_result, EMPTY) X(fird_reference_status, ABSENT) X(fird_render_result, ABSENT)

/* TfpRtValue f(TfpRtHost *h): the fields. X(name, kind). REF_EMPTY: runtime.lua's `{}`. */
#define TFP_OFF_G0(X) \
  X(camera_retired, NIL) X(aim_markers_fault, NIL) X(actions_actions, NIL) \
  X(fird_module_native_animation_default, FALSE) X(fird_module_render_pose_verified, FALSE) \
  X(fird_native_animation, NIL) X(fird_phase, NIL) X(fird_enabled, NIL) X(fird_writes, NIL) X(fird_weapon_key, NIL) \
  X(fird_reference_needed, NIL) X(fird_scene_fov, NIL) X(fird_render_frames, NIL) \
  X(profile_body_sleeves, FALSE) X(profile_grip_hand, FALSE) X(profile_seated_head, TRUE) X(config_table, REF_EMPTY) \
  X(backend_pin_first_person, FALSE) X(backend_camera_ready, NIL)

/* Sets every pointer of the stubbed groups (and leaves the optional ones the runtime tests for at
 * NULL: the four menu options, arm_pieces_new, grip_hand_new, head_sdk_handles, camera_is_pinned,
 * camera_look_stats, backend_pin_player_snapshot, mod_set). Pointers of the wired view are not
 * touched. */
void tfp_wire_off_bind(TfpRtHost *h);

/* The value helpers the binder files share. */
static inline TfpRtValue tfp_wire_nil(void) { TfpRtValue v = {TFP_RT_NIL, 0, 0, 0, 0}; return v; }
static inline TfpRtValue tfp_wire_bool(bool b) { TfpRtValue v = {b ? TFP_RT_TRUE : TFP_RT_FALSE, 0, 0, 0, 0}; return v; }
static inline TfpRtValue tfp_wire_number(double n) { TfpRtValue v = {TFP_RT_NUMBER, n, 0, 0, 0}; return v; }
static inline TfpRtValue tfp_wire_ref(const void *p) { TfpRtValue v = {TFP_RT_REF, 0, 0, 0, p}; return v; }
TfpRtValue tfp_wire_text(const char *text); /* NULL: nil */
#endif
