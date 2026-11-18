/* What build 25480438 does differently from retail's build during actions, and the binder's answers.
 * Read from the session of 2026-11-18 (log 76bf7718, 0.6.0-feel.8); docs/design/ACTION-CAMERA.md section 5.
 *
 *  - Picking something up runs under action mask 0x22. Retail's head camera accepts 0, 2 and 0x20 only, so the
 *    game's third-person camera was shown (five times, each a 2.7 to 2.8 m jump). The head camera, and the head
 *    hiding that belongs to it, now accept 0x22 as they accept a vault's 0x20.
 *  - A stim is mask 2, then 0 for about 650 ms, then 2 (retail's build: mask 64, the native view stays). Retail's
 *    held eye tolerates 300 ms of mask 0, so the eye let go mid-stim and the native view came back with a cut of
 *    0.42 to 0.58 m. The tolerance is 900 ms here, and after it has let go (a grenade held longer) the eye glides
 *    back while the weapon is drawn.
 *  - The aim-view keeper (backend.lua:1340) must see the aim button change before the game acts on it. Read from
 *    the engine in the tick, every change arrived after the game had acted (6 of 6 releases dropped the view; the
 *    keeper fired on 5 presses instead, as full aim began). So: Windows' level first, a second look when the
 *    engine's script update begins, and no keeping in the first moments of full aim.
 * Platform-free and header-only: tests/wire_action_test.c. */
#ifndef TFP_WIRE_ACTION_H
#define TFP_WIRE_ACTION_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TFP_ACTION_PICKUP_MASK 0x22u
#define TFP_ACTION_EYE_IDLE_MS 900u
#define TFP_ACTION_EYE_RETURN true
#define TFP_ACTION_AIM_GUARD_SECONDS 0.15 /* full aim younger than this is beginning, not ending */
#define TFP_ACTION_PROBE_GAP_SECONDS 0.001
#define TFP_ACTION_PROBES_PER_FRAME 3u /* each asks Windows: few, for wineserver's sake */

/* backend.lua:224 head_camera_transition, with the pick-up mask beside the vault's. */
static inline bool tfp_action_head_camera(bool retail_answer, uint32_t camera_kind, bool seated, const uint8_t controller_bytes[6]) {
  uint32_t mask;
  memcpy(&mask, controller_bytes, 4);
  return retail_answer || (camera_kind == 4 && !seated && mask == TFP_ACTION_PICKUP_MASK && controller_bytes[5] == 0);
}

/* The aim button between ticks. The tick arms it while the keeper could act (the runtime's own conditions and
 * a snapshot in full aim on the native view) and notes the levels it read; a probe from the engine's script
 * call reads again and asks for the keeper when a level changed. */
typedef struct TfpAimWatch {
  bool armed, was_active, has_active_since;
  double active_since, last_probe;
  bool has_level[3]; int level[3]; /* by button: 1 right, 2 middle */
  unsigned frame_probes, probes, probe_edges, probe_keeps, guarded;
} TfpAimWatch;

/* Once a tick, before the levels are read. */
static inline void tfp_aim_watch_frame(TfpAimWatch *w, double now, bool controller_active, bool armed) {
  if (controller_active && !w->was_active) w->active_since = now, w->has_active_since = true;
  if (!controller_active) w->has_active_since = false;
  w->was_active = controller_active;
  w->armed = armed && controller_active;
  w->frame_probes = 0;
}
/* A level the tick read (the runtime keeps its own edge memory; this is the probe's). */
static inline void tfp_aim_watch_note(TfpAimWatch *w, int button, int level) {
  if (button != 1 && button != 2) return;
  w->has_level[button] = level >= 0, w->level[button] = level;
}
/* May the keeper act now? Not while full aim is only beginning: a change of the button seen then is the press
 * that raised the weapon, arriving late. */
static inline bool tfp_aim_watch_allowed(TfpAimWatch *w, double now) {
  if (w->has_active_since && now >= w->active_since && now - w->active_since < TFP_ACTION_AIM_GUARD_SECONDS) { w->guarded++; return false; }
  return true;
}
/* Is a probe due? At most TFP_ACTION_PROBES_PER_FRAME between two ticks, not closer than a millisecond. */
static inline bool tfp_aim_watch_due(TfpAimWatch *w, double now) {
  if (!w->armed || w->frame_probes >= TFP_ACTION_PROBES_PER_FRAME) return false;
  if (w->frame_probes && now >= w->last_probe && now - w->last_probe < TFP_ACTION_PROBE_GAP_SECONDS) return false;
  w->frame_probes++, w->probes++, w->last_probe = now;
  return true;
}
/* The levels a probe read (-1: no answer). True when the keeper is wanted: retail's rule (runtime.lua:405-421),
 * either edge of the aim button or a press of the aim-mode button. One change is reported once. */
static inline bool tfp_aim_watch_probe(TfpAimWatch *w, int right, int middle) {
  bool edge = false;
  if (right >= 0) {
    if (w->has_level[1] && w->level[1] != right) edge = true;
    w->has_level[1] = true, w->level[1] = right;
  }
  if (middle >= 0) {
    if (w->has_level[2] && middle && !w->level[2]) edge = true;
    w->has_level[2] = true, w->level[2] = middle;
  }
  if (edge) w->probe_edges++;
  return edge;
}
#endif
