/* The feel build (0.6.0-feel.1): the view build's binder with the module binders joined on.
 *
 *   wire_settings.c  mod.config / mod.set, the placement profiles, the menu's option methods
 *   wire_pose.c      the pose (editor) helper behind the backend: weapon and arm placement
 *   wire_motion.c    bob, sway and per-action placements
 *   wire_engine.c    head visibility, model shadows, sleeves, hand grips (engine script functions)
 *   wire_effects.c   depth of field, vignette, aim markers, movement, the native aim option
 *
 * No Windows here: src/mod_view.c (compiled with TFP_FEEL) supplies the platform halves.
 * docs/wiring/WIRING-FEEL.md lists every binding. */
#ifndef TFP_WIRE_FEEL_H
#define TFP_WIRE_FEEL_H
#include "wire_pose.h"
#include "wire_settings.h"
#include "wire_motion.h"
#include "wire_engine.h"
#include "wire_effects.h"
#include "wire_editor.h"

typedef struct TfpWireFeelParts {
  const char *settings_json;           /* the bytes of settings.json; NULL: no file (retail's defaults) */
  size_t settings_length;
  const char *settings_origin;
  const TfpPoseHelperApi *pose;        /* NULL: the view build's stand-in stays */
  const TfpPoseAdapterIo *pose_io;
  const TfpWireEngineApi *engine;      /* NULL: head, shadows, sleeves and grips stay off */
  const TfpFxPlatform *effects;        /* NULL: the five effect modules stay off */
  const TfpEditorPlatform *editor;     /* NULL: the placement editor stays off */
} TfpWireFeelParts;
typedef struct TfpWireFeel {
  TfpWirePose pose;
  TfpFx fx;
  bool has_pose, has_motion, has_engine, has_effects, has_editor;
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

/* ---- live replace (docs/guides/LIVE-REPLACE.md) ---------------------------------------------------------
 * What the mod still holds in the game. Gathered by reading the modules' own state: no game call, no
 * engine call, no write, so it may run on the loader's change thread once no tick is in flight. */
typedef struct TfpWireFeelFacts {
  bool view_on;        /* the controller is enabled (F11 on, running or waiting) */
  TfpPhase phase;      /* the controller's phase */
  bool lease;          /* a native-view lease is held */
  bool recovering;     /* the recovery ledger has a row */
  bool pose_lease, look_pending;
  bool head, sleeves, grip;
  bool dof, vignette, markers, movement, aim_option;
  bool editor;         /* the placement editor is open (or closing) */
  bool unsaved;        /* a settings change the file does not hold (the last write failed) */
} TfpWireFeelFacts;
void tfp_wire_feel_facts(TfpWire *w, TfpWireFeelFacts *out);
/* NULL: idle and clean. Otherwise one plain sentence naming the first thing still held. */
const char *tfp_wire_feel_held_of(const TfpWireFeelFacts *facts);
const char *tfp_wire_feel_held(TfpWire *w);
#endif
