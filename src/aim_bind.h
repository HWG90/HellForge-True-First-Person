/* The binder lines of round 20 (the sight in aim, the near clip in full aim, the "Aim:" line) and round 20c's test
 * switch, in one place a test reaches (review 28, I2: tests/aim_bind_test.c). The Windows binders call these and
 * nothing else for it: src/wire_win_pose.c tfp_aim_bind_pose, src/mod_view.c tfp_aim_bind_switches at start and
 * tfp_aim_bind_tick every tick. No engine reads here. */
#ifndef TFP_AIM_BIND_H
#define TFP_AIM_BIND_H
#include "fov_helper.h"
#include "near_helper.h"
#include "pose_helper.h"
#include <math.h>
#include <stdbool.h>

/* The pose helper's frame lens goes to the field-of-view helper: without it every "gate", "optic" and "game/lens"
 * field of the "Aim:" line reads "not noted". */
static inline void tfp_aim_bind_note_lens(void *user, bool gated, float target, float lens) {
  (void)user;
  tfp_fov_note_lens(gated, target, lens);
}
/* Before tfp_pose_set_host(host): the lens note on the pose host, and the near helper asks the pose helper in each of
 * its own calls how far the gun is drawn in (so the near clip moves in with the gun in the same frame). */
static inline void tfp_aim_bind_pose(TfpPoseHost *host) {
  host->note_lens = tfp_aim_bind_note_lens;
  tfp_near_set_ads_source(tfp_pose_ads_near);
}
/* At start: zoom_steady.txt (round 20c, docs/guides/AIM-TEST.md). Without it full aim is feel.51's (the held depth and
 * the field-of-view correction off) and full aim is only counted for the "Aim:" line; with it both corrections are on. */
static inline void tfp_aim_bind_switches(bool zoom_steady) {
  tfp_pose_set_aim_steady(zoom_steady);
  tfp_fov_set_aim_watch(true, zoom_steady);
}
/* Every tick: the game's sight the near helper saw (`near_on` false: no near lease, no sight), the wanted distance
 * (locked_view.json ads_sight_cm) and whether the placement editor is on its ADS action
 * (src/editor_motion.h tfp_editor_motion_editing_ads). */
static inline void tfp_aim_bind_tick(bool near_on, float sight, double wanted_cm, bool editing_ads) {
  tfp_pose_set_ads_seat(near_on ? sight : 0, isfinite(wanted_cm) ? (float)(wanted_cm/100) : 0);
  tfp_pose_set_ads_editing(editing_ads);
}
#endif
