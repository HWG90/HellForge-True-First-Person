/* Round 24 (beyond retail; the camera helper uses it only while the camera lock holds a frame): what the continuity
 * and the eye damping are told during a clamber or a vault. The arithmetic only: no engine reads here, so
 * tools/harness/replay_filter.c can make the same decisions over a log.
 *
 * A clamber or a vault runs under an action mask with bit 0x20: 0x20 a low vault (the native view is held through
 * it), 0x22 a high clamber with the gun stowed (the head camera shows it; src/wire_action.h). Seen in the user's log
 * of 2026-11-24 on 0.6.0-feel.51 (three high clambers, 20:27:42, 20:27:50, 20:28:13, all in Stable):
 *  - the window in which jumps are looked for (camera_helper.c smooth_detect, SMOOTH_EVENT_MS) was opened again by
 *    every change of mask (0 -> 0x22 -> 0x20), and it looks at head-camera frames too. The head's own crouch against
 *    a root that rises 1.8 m in 0.3 s is a step change of 0.1 m a frame against the root: four frames were taken
 *    up as jumps, the run's speed (the head going up with the root) was handed to the ease-out, and the eye went on
 *    rising 0.5 to 0.6 m past where the head stopped (0.646 m carried), then sank back over 0.3 s;
 *  - the Stable eye damping (350 ms, 0.25 m) lagged the same way and held the eye at its limit above the head.
 * So under such a mask:
 *  - a head-camera frame with the body root moving is not looked at for jumps: the head is the body's own motion
 *    there (no held eye, no re-seated rig; the swap's held eye that made the head camera be looked at is mask 2);
 *  - a change of mask into, out of or within the traversal masks does not open the window again (a change of
 *    weapon, of the request, of the camera, and the binder's events still do; the native view held through a low
 *    vault keeps its window from the hold's own event, and the rig's re-seats there are still taken up);
 *  - a run of jumps that ends in such a head-camera frame hands no speed to the curve (camera_continuity.h, THE
 *    CLIMB; not in the native view held through a vault, where that made the shown eye stop short);
 *  - the head camera's eye is not damped: a lag carried is let go on 40 ms (as at full aim), nothing new is damped;
 *    and not for TFP_TRAVERSAL_AFTER_MS after the mask has left the bit either: the clamber at 20:28:13 ends with
 *    the body standing up on mask 2 (the gun being drawn), the head 0.4 m up against the root in 450 ms, and Stable's
 *    damping held the eye 0.25 m below it, still 0.22 m once the root had stopped. */
#ifndef TFP_CAMERA_TRAVERSAL_H
#define TFP_CAMERA_TRAVERSAL_H
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define TFP_TRAVERSAL_BIT 0x20u          /* the action mask's bit for a clamber or a vault */
#define TFP_TRAVERSAL_ROOT_M_S .5f       /* the body root moving at this or more (m/s, in the world) */
#define TFP_TRAVERSAL_ROOT_LINGER_MS 300.0 /* ... counts as moving for this long after (a stand-up after the climb) */
#define TFP_TRAVERSAL_AFTER_MS 600.0     /* the eye damping stays off this long after the mask left the bit (the stand-up) */

static inline bool tfp_traversal(uint32_t mask) { return (mask & TFP_TRAVERSAL_BIT) != 0; }

/* Does a change of the control mask from `was` to `now` open the window in which jumps are looked for? */
static inline bool tfp_traversal_mask_rearms(uint32_t was, uint32_t now) { return was != now && !tfp_traversal(was) && !tfp_traversal(now); }

/* The body root's motion: moving while it goes TFP_TRAVERSAL_ROOT_M_S or more, and for TFP_TRAVERSAL_ROOT_LINGER_MS
 * after. A pause longer than `max_gap_ms` starts it again (not moving). */
typedef struct TfpRootMotion { bool known; float root[3]; double tick, until; } TfpRootMotion;
static inline bool tfp_root_motion_step(TfpRootMotion *m, const float *root, double tick, uint32_t max_gap_ms) {
  if (!isfinite(root[0]) || !isfinite(root[1]) || !isfinite(root[2]) || !isfinite(tick)) { m->known = false; return false; }
  if (m->known && tick > m->tick && tick-m->tick <= max_gap_ms) {
    const float d[3] = {root[0]-m->root[0], root[1]-m->root[1], root[2]-m->root[2]};
    const float speed = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])*1000/(float)(tick-m->tick);
    if (speed >= TFP_TRAVERSAL_ROOT_M_S) m->until = tick+TFP_TRAVERSAL_ROOT_LINGER_MS;
  } else if (!m->known || tick < m->tick || tick-m->tick > max_gap_ms) m->until = 0;
  m->known = true; m->tick = tick;
  for (unsigned i = 0; i < 3; ++i) m->root[i] = root[i];
  return tick < m->until;
}

/* Jumps looked for in this frame: `detect` as the window says, less a head-camera frame under a traversal mask while
 * the body root moves. */
static inline bool tfp_traversal_detect(bool detect, bool head_source, uint32_t mask, bool root_moving) {
  return detect && !(head_source && tfp_traversal(mask) && root_moving);
}

/* A traversal now or within TFP_TRAVERSAL_AFTER_MS: true while it is. A pause longer than `max_gap_ms` forgets it. */
typedef struct TfpTraversalWatch { bool known; double tick, until; } TfpTraversalWatch;
static inline bool tfp_traversal_recent(TfpTraversalWatch *w, uint32_t mask, double tick, uint32_t max_gap_ms) {
  if (!isfinite(tick)) { w->known = false; return tfp_traversal(mask); }
  if (!w->known || tick < w->tick || tick-w->tick > max_gap_ms) w->until = 0;
  if (tfp_traversal(mask)) w->until = tick+TFP_TRAVERSAL_AFTER_MS;
  w->known = true; w->tick = tick;
  return tick < w->until;
}
/* The head camera's eye damping runs in this frame: on foot as the helper says, and no traversal now or of late
 * (`climbing`: tfp_traversal_recent). */
static inline bool tfp_traversal_damps(bool on_foot, bool climbing) { return on_foot && !climbing; }
#endif
