/* See wire_off.h. Every function here answers a constant. */
#include "wire_off.h"
#include <string.h>

TfpRtValue tfp_wire_text(const char *text) {
  TfpRtValue v = {TFP_RT_NIL, 0, 0, 0, 0};
  if (!text) return v;
  v.kind = TFP_RT_TEXT, v.text = text, v.length = strlen(text);
  return v;
}
#define nil tfp_wire_nil
#define boolean tfp_wire_bool
#define text tfp_wire_text
static TfpRtValue of_NIL(const char *t) { (void)t; return nil(); }
static TfpRtValue of_TRUE(const char *t) { (void)t; return boolean(true); }
static TfpRtValue of_FALSE(const char *t) { (void)t; return boolean(false); }
static TfpRtValue of_TEXT(const char *t) { return text(t); }
static TfpRtValue of_REF_EMPTY(const char *t) { (void)t; return tfp_wire_ref(&tfp_rt_empty_table); }
static void clear(TfpRtTable *t, bool present) {
  memset(t, 0, sizeof *t);
  t->present = present;
  if (present) t->ref = &tfp_rt_empty_table;
}
static void shape_EMPTY(TfpRtTable *t) { clear(t, true); }
static void shape_ABSENT(TfpRtTable *t) { clear(t, false); }
static void shape_IDLE(TfpRtTable *t) { clear(t, true); t->f[TFP_RT_TARGET_PHASE] = text("idle"); }
static void shape_DOF(TfpRtTable *t) { clear(t, true); t->f[TFP_RT_DOF_OWNED] = boolean(false); }

#define S0(name) static bool off_##name(TfpRtHost *h) { (void)h; return true; }
TFP_OFF_S0(S0)
#define S1(name, kind, t) static bool off_##name(TfpRtHost *h, TfpRtValue *o1) { (void)h; *o1 = of_##kind(t); return true; }
TFP_OFF_S1(S1)
#define S2(name, kind, t) \
  static bool off_##name(TfpRtHost *h, TfpRtValue *o1, TfpRtValue *o2) { (void)h; *o1 = of_##kind(0); *o2 = text(t); return true; }
TFP_OFF_S2(S2)
#define ST(name, shape) static bool off_##name(TfpRtHost *h, TfpRtTable *o1) { (void)h; shape_##shape(o1); return true; }
TFP_OFF_ST(ST)
#define G0(name, kind) static TfpRtValue off_##name(TfpRtHost *h) { (void)h; return of_##kind(0); }
TFP_OFF_G0(G0)

/* ---- the ones with arguments ---- */
#define ANSWER(first, second) do { *o1 = (first); *o2 = (second); return true; } while (0)
static bool off_editor_cancel(TfpRtHost *h, const char *reason, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)reason; ANSWER(boolean(true), nil());
}
static bool off_motion_capture_neutral(TfpRtHost *h, bool native_ads, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)native_ads; ANSWER(boolean(true), nil());
}
static bool off_camera_free_aim(TfpRtHost *h, double ms, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)ms; ANSWER(boolean(false), text(TFP_OFF_CAMERA_DEBUG));
}
static bool off_aim_markers_toggle(TfpRtHost *h, TfpRtValue key, TfpRtValue identity, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)key; (void)identity; ANSWER(boolean(false), text(TFP_OFF_MARKERS));
}
static bool off_aim_markers_set_setting(TfpRtHost *h, TfpRtValue value, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)value; ANSWER(boolean(false), text(TFP_OFF_MARKERS));
}
/* Actions.resolve / default_placement: no saved placements, so retail's shipped default
 * {x=.15, y=0, z=-.12} (controller.lua:22, runtime.lua:807), the table the runtime itself exports. */
static bool off_actions_resolve(TfpRtHost *h, TfpRtValue key, TfpRtValue action, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)key; (void)action; ANSWER(tfp_wire_ref(tfp_rt_default_placement), nil());
}
static bool off_actions_saved(TfpRtHost *h, TfpRtValue key, TfpRtValue action, TfpRtValue value, TfpRtValue all, TfpRtValue *o1) {
  (void)h; (void)key; (void)action; (void)value; (void)all; *o1 = nil(); return true;
}
static bool off_actions_weapon_info(TfpRtHost *h, TfpRtValue key, TfpRtTable *o1) { (void)h; (void)key; shape_ABSENT(o1); return true; }
static bool off_head_positioner_save(TfpRtHost *h, TfpRtValue v, TfpRtValue k, TfpRtValue i, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)v; (void)k; (void)i; ANSWER(boolean(false), text(TFP_OFF_EDITOR));
}
static bool off_two_values(TfpRtHost *h, TfpRtValue a, TfpRtValue b, TfpRtValue *o1, TfpRtValue *o2) { /* set_placement, start */
  (void)h; (void)a; (void)b; ANSWER(boolean(false), text(TFP_OFF_EDITOR));
}
static bool off_fird_two_values(TfpRtHost *h, TfpRtValue a, TfpRtValue b, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)a; (void)b; ANSWER(boolean(false), text(TFP_OFF_FIRD));
}
static bool off_fird_new(TfpRtHost *h, const TfpRtValue *options) { (void)h; (void)options; return true; }
static bool off_fird_capture(TfpRtHost *h, TfpRtValue token, TfpRtTable *o1) { (void)h; (void)token; shape_ABSENT(o1); return true; }
static bool off_fird_start(TfpRtHost *h, TfpRtValue reference, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)reference; ANSWER(boolean(false), text(TFP_OFF_FIRD));
}
static bool off_snapshot_refused(TfpRtHost *h, const TfpRtTable *snapshot, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)snapshot; ANSWER(boolean(false), text(TFP_OFF_FIRD));
}
static bool off_fird_warm_update(TfpRtHost *h, const TfpRtTable *snapshot, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)snapshot; ANSWER(boolean(true), nil());
}
static bool off_set_pair(TfpRtHost *h, TfpRtValue a, TfpRtValue b) { (void)h; (void)a; (void)b; return true; }
static bool off_ready_pose_update(TfpRtHost *h, const TfpRtContext *c, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)c; ANSWER(boolean(true), nil());
}
static bool off_ready_pose_filter_owner(TfpRtHost *h, const TfpRtContext *c, TfpRtValue *o1) { (void)h; (void)c; *o1 = nil(); return true; }
static bool off_held_fire_update(TfpRtHost *h, TfpRtValue a, const TfpRtContext *c, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)a; (void)c; ANSWER(boolean(true), nil());
}
static bool off_held_fire_matches(TfpRtHost *h, TfpRtValue a, const TfpRtContext *c, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)a; (void)c; ANSWER(boolean(false), nil());
}
static bool off_ready_timing_sample(TfpRtHost *h, const TfpRtValue *timing, TfpRtTable *o1, TfpRtValue *o2) {
  (void)h; (void)timing; shape_ABSENT(o1); *o2 = text(TFP_OFF_FIRD); return true;
}
static bool off_capture_gate_scene_lens(TfpRtHost *h, const TfpRtTable *s, TfpRtValue *o1) { (void)h; (void)s; *o1 = nil(); return true; }
static bool off_capture_gate_update(TfpRtHost *h, const TfpRtValue *gate, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)gate; ANSWER(boolean(false), text(TFP_OFF_FIRD));
}
/* The pose helper is not wired: a lease has no depth_stats / input_stats function (the LEASE fields say
 * so), so the runtime never calls these two. */
static bool off_helper_depth_stats(TfpRtHost *h, const TfpRtTable *lease, TfpRtValue handle, TfpRtTable *o1, TfpRtValue *o2) {
  (void)h; (void)lease; (void)handle; shape_ABSENT(o1); *o2 = text("No active depth-compensation lease"); return true;
}
static bool off_helper_input_stats(TfpRtHost *h, const TfpRtTable *lease, TfpRtValue handle, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)lease; (void)handle; ANSWER(nil(), text("No active first-person input lease"));
}
static bool off_arm_update(TfpRtHost *h, const TfpRtTable *snapshot, bool wanted, TfpRtValue handles) {
  (void)h; (void)snapshot; (void)wanted; (void)handles; return true;
}
/* The rig and weapon positions of the diagnostics record (runtime.lua:476-511, inside a pcall): the
 * lookup answers nil, indexing it raises inside that pcall, and those fields stay nil. Their offsets
 * (+0x80, +0x88, node 5) are retail's and were not established on build 25480438. */
static bool off_backend_alignment_context(TfpRtHost *h, const TfpRtValue *actx, TfpRtTable *o1) {
  (void)h; (void)actx; shape_ABSENT(o1); return true;
}
static bool off_backend_prepare_pose(TfpRtHost *h, const TfpRtTable *snapshot, TfpRtValue *o1, TfpRtValue *o2) {
  (void)h; (void)snapshot; ANSWER(boolean(false), text(TFP_OFF_PREFLIGHT));
}
static bool off_backend_pose_status(TfpRtHost *h, const TfpRtTable *lease, TfpRtValue *o1) { (void)h; (void)lease; *o1 = nil(); return true; }
static bool off_set_value(TfpRtHost *h, TfpRtValue value) { (void)h; (void)value; return true; }
static TfpRtValue off_actions_observed(TfpRtHost *h, TfpRtValue action) { (void)h; (void)action; return nil(); }
static TfpRtValue off_config_field(TfpRtHost *h, const char *name) { (void)h; (void)name; return nil(); }
static TfpRtValue off_config_action_mapped(TfpRtHost *h, TfpRtValue action) { (void)h; (void)action; return boolean(false); }
static TfpRtValue off_ref_field(TfpRtHost *h, TfpRtValue ref, const char *name) { (void)h; (void)ref; (void)name; return nil(); }
static void off_fird_weapon_snapshot(TfpRtHost *h, TfpRtTable *out) { (void)h; shape_ABSENT(out); }

void tfp_wire_off_bind(TfpRtHost *h) {
#define BIND(name, ...) h->name = off_##name;
  TFP_OFF_S0(BIND)
  TFP_OFF_S1(BIND)
  TFP_OFF_S2(BIND)
  TFP_OFF_ST(BIND)
  TFP_OFF_G0(BIND)
#undef BIND
  h->editor_cancel = off_editor_cancel;
  h->motion_capture_neutral = off_motion_capture_neutral;
  h->camera_free_aim = off_camera_free_aim;
  h->aim_markers_toggle = off_aim_markers_toggle;
  h->aim_markers_set_setting = off_aim_markers_set_setting;
  h->actions_resolve = off_actions_resolve;
  h->actions_default_placement = off_actions_resolve;
  h->actions_saved = off_actions_saved;
  h->actions_weapon_info = off_actions_weapon_info;
  h->head_positioner_save = off_head_positioner_save;
  h->head_positioner_set_placement = off_two_values;
  h->head_positioner_start = off_two_values;
  h->fird_new = off_fird_new;
  h->fird_capture = off_fird_capture;
  h->fird_start = off_fird_start;
  h->fird_prepare_native_ads = off_snapshot_refused;
  h->fird_set_placement = off_fird_two_values;
  h->fird_capture_set_aim = off_set_pair;
  h->fird_warm_update = off_fird_warm_update;
  h->ready_pose_update = off_ready_pose_update;
  h->ready_pose_filter_owner = off_ready_pose_filter_owner;
  h->held_fire_update = off_held_fire_update;
  h->held_fire_matches = off_held_fire_matches;
  h->ready_timing_sample = off_ready_timing_sample;
  h->capture_gate_scene_lens = off_capture_gate_scene_lens;
  h->capture_gate_update = off_capture_gate_update;
  h->capture_gate_token_set_capture = off_set_pair;
  h->helper_depth_stats = off_helper_depth_stats;
  h->helper_input_stats = off_helper_input_stats;
  h->arm_pieces_update = off_arm_update;
  h->grip_hand_update = off_arm_update;
  h->backend_alignment_context = off_backend_alignment_context;
  h->backend_prepare_pose = off_backend_prepare_pose;
  h->backend_pose_status = off_backend_pose_status;
  h->backend_set_fird_camera_offset = off_set_value;
  h->actions_observed = off_actions_observed;
  h->config_field = off_config_field;
  h->config_action_mapped = off_config_action_mapped;
  h->ref_field = off_ref_field;
  h->fird_weapon_snapshot = off_fird_weapon_snapshot;
  /* Optional pointers that stay NULL, each retail's own "this method / module does not exist":
   * menu_fird_person_arms, menu_show_torso, menu_near_options (near clip 0 / 0), menu_motion_options,
   * arm_pieces_new, grip_hand_new (runtime.lua:698-706: a missing sleeve module never stops the view),
   * head_sdk_handles, camera_is_pinned, camera_look_stats, backend_pin_player_snapshot, mod_set. */
}
