/* The feel build (0.6.0-feel.1): the view build's binder with the module binders joined on.
 *
 *   wire_settings.c  mod.config / mod.set, the placement profiles, the menu's option methods
 *   wire_pose.c      the pose (editor) helper behind the backend: weapon and arm placement
 *   wire_motion.c    bob, sway and per-action placements
 *   wire_engine.c    head visibility, model shadows, sleeves, hand grips (engine script functions)
 *   wire_effects.c   depth of field, vignette, aim markers, movement, the native aim option
 *
 * No Windows here: src/mod_view.c (compiled with TFP_FEEL) supplies the platform halves.
 * docs/WIRING-FEEL.md lists every binding. */
#ifndef TFP_WIRE_FEEL_H
#define TFP_WIRE_FEEL_H
#include "wire_pose.h"
#include "wire_settings.h"
#include "wire_motion.h"
#include "wire_engine.h"
#include "wire_effects.h"

typedef struct TfpWireFeelParts {
  const char *settings_json;           /* the bytes of settings.json; NULL: no file (retail's defaults) */
  size_t settings_length;
  const char *settings_origin;
  const TfpPoseHelperApi *pose;        /* NULL: the view build's stand-in stays */
  const TfpPoseAdapterIo *pose_io;
  const TfpWireEngineApi *engine;      /* NULL: head, shadows, sleeves and grips stay off */
  const TfpFxPlatform *effects;        /* NULL: the five effect modules stay off */
} TfpWireFeelParts;
typedef struct TfpWireFeel {
  TfpWirePose pose;
  TfpFx fx;
  bool has_pose, has_motion, has_engine, has_effects;
  bool second_open;
  double second_start;
  char line[900];
} TfpWireFeel;

/* After tfp_wire_init, before the runtime's on_enable. Each part logs its own Ready / Refused line. */
void tfp_wire_feel_bind(TfpWire *w, TfpWireFeel *feel, const TfpWireFeelParts *parts);
/* First thing in every tick, inside the engine's temporary frame. */
void tfp_wire_feel_begin(TfpWire *w);
/* The aim_markers table of the settings store as the keys that are true (TfpFxOptions.markers_decode). */
bool tfp_wire_feel_markers(TfpWire *w, TfpRtValue value, TfpAimMarkersSaved *out);
#endif
