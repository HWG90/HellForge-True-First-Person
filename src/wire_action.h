/* What build 25480438 does differently from retail's build during actions, and the binder's answers.
 * Read from the session of 2026-11-18 (log 76bf7718, 0.6.0-feel.8); docs/design/ACTION-CAMERA.md section 5.
 *
 *  - A high clamber (a ledge at head height, the gun stowed) runs under action mask 0x22, then 0x20 (a vault's).
 *    Retail's head camera accepts 0, 2 and 0x20 only, so the game's third-person camera was shown (five times, each a
 *    2.7 to 2.8 m jump). The head camera, and the head hiding that belongs to it, now accept 0x22 as they accept a
 *    vault's 0x20. The session of 2026-11-18 read 0x22 as picking something up ("the best reading, not a proven
 *    one", docs/design/ACTION-CAMERA.md); the user's log of 2026-11-24 (feel.51) has three 0x22 episodes, all
 *    clambers (the root 1.8 m up in each), and pick-ups on other masks: a backpack on 0, a support weapon on 2
 *    (round 24).
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

#define TFP_ACTION_CLAMBER_MASK 0x22u /* a high clamber; named the pick-up mask until round 24 */
#define TFP_ACTION_EYE_IDLE_MS 900u
#define TFP_ACTION_EYE_RETURN true
#define TFP_ACTION_AIM_GUARD_SECONDS 0.15 /* full aim younger than this is beginning, not ending */
#define TFP_ACTION_PROBE_GAP_SECONDS 0.001
#define TFP_ACTION_PROBES_PER_FRAME 3u /* each asks Windows: few, for wineserver's sake */

/* backend.lua:224 head_camera_transition, with the clamber's mask beside the vault's. */
static inline bool tfp_action_head_camera(bool retail_answer, uint32_t camera_kind, bool seated, const uint8_t controller_bytes[6]) {
  uint32_t mask;
  memcpy(&mask, controller_bytes, 4);
  return retail_answer || (camera_kind == 4 && !seated && mask == TFP_ACTION_CLAMBER_MASK && controller_bytes[5] == 0);
}

/* Beyond retail, and only with the camera lock (0.6.0-feel.26; seen in the game 2026-11-23). A stim on this build
 * runs under mask 0x40 in the native view: the game keeps its first-person request (kind 6) for about 0.85 s after
 * the mod has asked for the exit, moves the gun to the body about 30 ms in, and moves the rig's eye to the body's
 * head. Retail's head hiding lets go on the first tick of any mask other than 2 (the snapshot is not safe), so the
 * helmet, torso and shoulders were drawn around an eye that sat inside them.
 *
 * Head hiding: while the game presents the lease's own first-person request the head stays hidden, whatever the
 * action flags say. (The native view's eye is behind the body's head with the gun in the hands too; the view is
 * clean there only because the head is hidden.) lease_request: the controller's lease's request record, 0 without. */
static inline bool tfp_action_own_native(bool lock, uint32_t camera_kind, bool seated, uint64_t record, uint64_t lease_request) {
  return lock && camera_kind == 6 && !seated && lease_request != 0 && record == lease_request;
}
/* The eye: "orphaned" frames are those of that request after the mod has asked for its exit (the controller waits
 * for the game: cleanup pending) in which the gun is off the first-person rig: the rig's eye is no longer a
 * weapon's. One tick's view, read by the binder after the controller decided. */
enum { TFP_ORPHAN_GUN_UNKNOWN = 0, TFP_ORPHAN_GUN_RIG, TFP_ORPHAN_GUN_AWAY };
typedef struct TfpOrphanView {
  bool pending;        /* the controller is enabled and waits for the game to end the lease's request */
  uint64_t lease;      /* that lease's identity (never repeats), 0: none */
  bool proven;         /* the local player and its camera request were read on this tick */
  uint32_t camera_kind;
  bool record_ours, seated, full_aim, ragdoll, foreign_camera;
  int gun;             /* TFP_ORPHAN_GUN_*: the lease's gun's parent */
} TfpOrphanView;
typedef struct TfpOrphan { bool on; uint64_t lease; } TfpOrphan;
/* True while the frames are orphaned. It begins on a proven tick with the gun away and is kept through ticks that
 * cannot be read (one unread tick must not send the eye back into the helmet and out again); it ends when the wait
 * ends, the lease is another, the request on top is no longer the lease's kind 6, the gun is on the rig again, or
 * the view is the game's for another reason (seated, full aim, ragdoll, another camera's owner). */
static inline bool tfp_orphan_step(TfpOrphan *o, const TfpOrphanView *v) {
  if (!v->pending || !v->lease) { o->on = false; o->lease = 0; return false; }
  if (o->lease != v->lease) o->on = false;
  o->lease = v->lease;
  if (!v->proven) return o->on;
  if (v->camera_kind != 6 || !v->record_ours || v->seated || v->full_aim || v->ragdoll || v->foreign_camera || v->gun == TFP_ORPHAN_GUN_RIG)
    return o->on = false;
  if (v->gun == TFP_ORPHAN_GUN_AWAY) o->on = true;
  return o->on;
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
