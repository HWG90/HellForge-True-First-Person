/* See wire_feel.h. */
#include "wire_feel.h"
#include "wire_recovery.h"
#include <stdio.h>
#include <string.h>

static TfpWireFeel *feel_of(TfpWire *w) { return w->ext[TFP_WIRE_EXT_LOG]; }

bool tfp_wire_feel_markers(TfpWire *w, TfpRtValue value, TfpAimMarkersSaved *out) {
  const TfpSettingsValue *v = tfp_wire_settings_value_of(w, value);
  if (!v || v->kind != TFP_SETTINGS_MARKERS) return false;
  out->count = 0;
  for (size_t i = 0; i < v->count && out->count < TFP_AIM_MARKERS_SAVED; i++)
    snprintf(out->keys[out->count++], sizeof out->keys[0], "%s", v->ids[i]);
  return true;
}

/* Once per tick after on_update: every module's numbers, and once a second while the mode is on the
 * `Feel:` line (the View line of that second is the view build's own). */
static void frame(TfpWire *w) {
  TfpWireFeel *f = feel_of(w);
  int64_t mark = tfp_wire_lap_now(w); /* round 32: the extra's laps of the Perf frame line (wire.h) */
  if (f->has_pose) tfp_wire_pose_frame(w);
  tfp_wire_lap(w, TFP_LAP_POSE, &mark);
  if (f->has_motion) tfp_wire_motion_frame(w);
  else tfp_wire_settings_frame(w);
  tfp_wire_lap(w, TFP_LAP_MOTION, &mark);
  if (f->has_engine) tfp_wire_engine_frame(w);
  tfp_wire_lap(w, TFP_LAP_ENGINE, &mark);
  if (f->has_effects) tfp_wire_effects_frame(w);
  tfp_wire_lap(w, TFP_LAP_EFFECTS, &mark);
  if (!w->rt.has[TFP_RT_O_CONTROLLER] || !w->controller.enabled || w->rt.clock_previous.kind != TFP_RT_NUMBER) {
    f->second_open = false;
    return;
  }
  double now = w->rt.clock_previous.number;
  if (!f->second_open) { f->second_open = true, f->second_start = now; return; }
  if (now - f->second_start < 1) return;
  f->second_start = now;
  size_t n = (size_t)snprintf(f->line, sizeof f->line, "Feel: t=%.1f", now);
#define PART(on, call) \
  if (on && n + 2 < sizeof f->line) { f->line[n++] = ' '; f->line[n] = 0; n += call(w, f->line + n, sizeof f->line - n); }
  PART(f->has_pose, tfp_wire_pose_second)
  PART(f->has_motion, tfp_wire_motion_second)
  PART(f->has_engine, tfp_wire_engine_second)
  PART(f->has_effects, tfp_wire_effects_second)
#undef PART
  tfp_wire_say(w, f->line);
}

void tfp_wire_feel_begin(TfpWire *w) {
  TfpWireFeel *f = feel_of(w);
  if (f && f->has_engine) tfp_wire_engine_begin(w);
}

void tfp_wire_feel_bind(TfpWire *w, TfpWireFeel *f, const TfpWireFeelParts *parts) {
  memset(f, 0, sizeof *f);
  w->ext[TFP_WIRE_EXT_LOG] = f;
  /* Settings first: placements travel through its refs, and the motion binder refuses without it. */
  bool settings = tfp_wire_settings_bind(w, parts->settings_json, parts->settings_length, parts->settings_origin);
  if (!settings) tfp_wire_say(w, "Refused: settings: out of memory; the view build's answers stay (default placement, no saved placements)");
  else tfp_wire_settings_bind_locked(w, parts->locked_json, parts->locked_length, parts->locked_origin);
  if (parts->pose) {
    tfp_wire_pose_bind(w, &f->pose, parts->pose, parts->pose_io, TFP_POSE_ADAPTER_PROFILE_SHIPPED);
    w->pose_counts = tfp_wire_pose_counts;
    f->has_pose = true;
  } else tfp_wire_say(w, "Refused: pose: no helper was given; the weapon and arms stay where the game puts them");
  f->has_motion = settings && tfp_wire_motion_bind(w);
  if (parts->engine) f->has_engine = tfp_wire_engine_bind(w, parts->engine, NULL);
  else tfp_wire_say(w, "Refused: head visibility, model shadows, arm pieces, grip hand: no engine bridge");
  if (parts->effects) {
    TfpFxOptions options;
    memset(&options, 0, sizeof options);
    if (settings) options.markers_decode = tfp_wire_feel_markers, options.markers_value = tfp_wire_settings_markers;
    tfp_wire_effects_bind(w, &f->fx, parts->effects, &options);
    f->has_effects = true;
  } else tfp_wire_say(w, "Refused: depth of field, vignette, aim markers, movement, native aim option: no platform");
  /* Last: its placements go through the settings binder's decoder, which it chains to. */
  if (parts->editor && settings && tfp_wire_editor_bind(w, parts->editor)) {
    f->has_editor = true;
    tfp_wire_say(w, "Ready: placement editor bound (retail's editor through the runtime's own controller and store; the weapon mode only: "
                    "the eye-camera mode needs the head positioner, which is off)");
  } else tfp_wire_say(w, "Off: placement editor (no overlay platform, or no settings store)");
  w->frame_extra = frame;
}

/* ---- live replace -------------------------------------------------------------------------------- */
void tfp_wire_feel_facts(TfpWire *w, TfpWireFeelFacts *out) {
  const TfpWireFeel *f = feel_of(w);
  memset(out, 0, sizeof *out);
  out->view_on = w->controller.enabled;
  out->phase = w->controller.phase;
  out->lease = tfp_wire_held(w) != NULL;
  out->recovering = tfp_wire_recovery_busy();
  out->unsaved = tfp_wire_settings_unsaved(w);
  out->editor = tfp_wire_editor_active(w);
  if (!f) return;
  if (f->has_pose) {
    unsigned pose = tfp_wire_pose_held(w);
    out->pose_lease = pose & 1u, out->look_pending = (pose & 2u) != 0;
  }
  if (f->has_engine) {
    unsigned engine = tfp_wire_engine_held(w);
    out->head = engine & 1u, out->sleeves = (engine & 2u) != 0, out->grip = (engine & 4u) != 0;
  }
  if (f->has_effects) {
    const TfpFx *fx = tfp_wire_effects_of(w);
    out->dof = !tfp_dof_setting_is_clean(&fx->dof.dof);
    out->vignette = !tfp_vignette_is_clean(&fx->vignette);
    out->markers = !tfp_aim_markers_is_clean(&fx->markers);
    out->movement = !tfp_movement_is_clean(&fx->movement);
    out->aim_option = !tfp_native_aim_is_clean(&fx->aim);
  }
}
const char *tfp_wire_feel_held_of(const TfpWireFeelFacts *f) {
  if (f->view_on) return "the view is on; press F11 to switch it off first";
  if (f->phase == TFP_PHASE_CLEANUP_PENDING) return "the view's cleanup is still pending; wait a moment and try again";
  if (f->phase != TFP_PHASE_IDLE && f->phase != TFP_PHASE_DISABLED) return "the view has not finished switching off; wait a moment and try again";
  if (f->lease) return "a native-view lease is still held";
  if (f->recovering) return "a part that failed is still being recovered";
  if (f->pose_lease) return "the pose helper still holds its lease on the weapon and arms";
  if (f->look_pending) return "the look-gain normalisation still holds a value in the game's input";
  if (f->head) return "head visibility still owns hidden meshes";
  if (f->sleeves) return "arm pieces are still linked to the first-person arms";
  if (f->grip) return "the grip hand has not put its bones back";
  if (f->dof) return "depth of field has not been restored";
  if (f->vignette) return "the vignette has not been restored";
  if (f->markers) return "the aim markers have not been shown again";
  if (f->movement) return "the movement focus byte has not been restored";
  if (f->aim_option) return "the native aim option has not been restored";
  if (f->editor) return "the placement editor is open; close it first";
  if (f->unsaved) return "there are settings that could not be saved; see the Refused line above";
  return NULL;
}
const char *tfp_wire_feel_held(TfpWire *w) {
  TfpWireFeelFacts facts;
  tfp_wire_feel_facts(w, &facts);
  return tfp_wire_feel_held_of(&facts);
}
