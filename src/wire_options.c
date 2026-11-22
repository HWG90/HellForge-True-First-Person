/* See wire_options.h. */
#include "wire_options.h"
#include "wire_settings.h"
#include <stdio.h>

static const char *const NAMES[TFP_FEATURE_COUNT] = {"Animated arms", "Weapon put-away", "Smooth eye"};
static bool option(TfpWire *w, TfpFeature feature) {
  switch (feature) {
    case TFP_FEATURE_ARMS: return tfp_locked_view_animated_arms(w);
    case TFP_FEATURE_HOLD: return tfp_locked_view_put_away(w);
    case TFP_FEATURE_SMOOTH: return tfp_locked_view_smooth_eye(w);
    default: return false;
  }
}
bool tfp_wire_options_wanted(TfpWire *w, const TfpWireOptions *o, TfpFeature feature, const char **file) {
  if (file) *file = NULL;
  if ((unsigned)feature >= TFP_FEATURE_COUNT) return false;
  const char *forced = o->force_off[feature] ? o->force_off[feature] : o->force_on[feature]; /* both there: off wins */
  if (!forced) return option(w, feature);
  if (file) *file = forced;
  return !o->force_off[feature];
}
bool tfp_wire_options_busy(const TfpWire *w) {
  return w->controller.holding || w->hold.was_holding || w->controller.phase == TFP_PHASE_CLEANUP_PENDING;
}
static void say(TfpWire *w, TfpFeature feature, const char *what, bool on, const char *file) {
  char line[300];
  if (file) snprintf(line, sizeof line, "Option: %s: %s%s (test file %s beside the DLL; the option on the Locked view tab is %s and is not used)",
                     NAMES[feature], what, on ? "on" : "off", file, option(w, feature) ? "on" : "off");
  else snprintf(line, sizeof line, "Option: %s: %s%s (the option on the Locked view tab)", NAMES[feature], what, on ? "on" : "off");
  tfp_wire_say(w, line);
}
void tfp_wire_options_start(TfpWire *w, TfpWireOptions *o) {
  for (int f = 0; f < TFP_FEATURE_COUNT; f++) {
    const char *file;
    o->on[f] = tfp_wire_options_wanted(w, o, (TfpFeature)f, &file);
    o->waiting[f] = false;
    if (o->set[f]) o->set[f](o->user, o->on[f]);
    say(w, (TfpFeature)f, "", o->on[f], file);
  }
  o->started = true;
}
void tfp_wire_options_tick(TfpWire *w, TfpWireOptions *o) {
  if (!o->started) return;
  const bool busy = tfp_wire_options_busy(w);
  for (int f = 0; f < TFP_FEATURE_COUNT; f++) {
    const char *file;
    const bool wanted = tfp_wire_options_wanted(w, o, (TfpFeature)f, &file);
    if (wanted == o->on[f]) { o->waiting[f] = false; continue; }
    if (busy) {
      if (!o->waiting[f]) {
        char line[200];
        snprintf(line, sizeof line, "Option: %s: switching %s waits until the action in progress is over", NAMES[f], wanted ? "on" : "off");
        tfp_wire_say(w, line);
      }
      o->waiting[f] = true;
      continue;
    }
    o->on[f] = wanted, o->waiting[f] = false;
    if (o->set[f]) o->set[f](o->user, wanted);
    say(w, (TfpFeature)f, "switched ", wanted, file);
  }
}
