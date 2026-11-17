/* See wire_feel.h. */
#include "wire_feel.h"
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
  if (f->has_pose) tfp_wire_pose_frame(w);
  if (f->has_motion) tfp_wire_motion_frame(w);
  else tfp_wire_settings_frame(w);
  if (f->has_engine) tfp_wire_engine_frame(w);
  if (f->has_effects) tfp_wire_effects_frame(w);
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
  w->frame_extra = frame;
}
