/* Round 20c (review 28, I2): the binder lines of the sight in aim, the near clip and the "Aim:" line (src/aim_bind.h),
 * which the Windows binders (src/wire_win_pose.c, src/mod_view.c) call and no other test reaches. The real pose, near
 * and field-of-view helpers, nothing armed: what each line hands over. */
#include "../src/aim_bind.h"
#include <stdio.h>
#include <string.h>

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
  /* start-up values: retail's helpers */
  TfpPoseAdsProbe ads; bool count = true, steady = true; float lens = -1;
  CHECK(tfp_pose_ads_probe(&ads) && !ads.aim_steady && !ads.editing && ads.sight == 0 && ads.wanted == 0);
  tfp_fov_aim_watch_probe(&count, &steady, &lens);
  CHECK(!count && !steady && lens == 0 && tfp_near_ads_source() == NULL);
  CHECK(ads.aim_in_ms == 180 && tfp_fov_ads_enter_ms() == 180 && tfp_near_ads_enter_ms() == 180);   /* round 42: the clock's own time */

  /* the pose host: the frame's lens goes to the field-of-view helper (else every "Aim:" lens field reads "not noted"),
   * and the near helper reads the pose helper's closer in its own calls */
  TfpPoseHost host; memset(&host, 0, sizeof host);
  tfp_aim_bind_pose(&host);
  CHECK(host.note_lens != NULL && tfp_near_ads_source() == tfp_pose_ads_near);
  if (host.note_lens) host.note_lens(host.user, false, .40f, .35f);
  tfp_fov_aim_watch_probe(NULL, NULL, &lens);
  CHECK(lens == .35f);

  /* the switches: without zoom_steady.txt (the default) full aim is feel.51's and only counted for the "Aim:" line */
  tfp_aim_bind_switches(false);
  tfp_fov_aim_watch_probe(&count, &steady, NULL);
  CHECK(tfp_pose_ads_probe(&ads) && !ads.aim_steady && count && !steady);
  tfp_aim_bind_switches(true);
  tfp_fov_aim_watch_probe(&count, &steady, NULL);
  CHECK(tfp_pose_ads_probe(&ads) && ads.aim_steady && count && steady);
  tfp_aim_bind_switches(false);
  tfp_fov_aim_watch_probe(&count, &steady, NULL);
  CHECK(tfp_pose_ads_probe(&ads) && !ads.aim_steady && count && !steady);

  /* the tick: the near helper's sight (none without its lease), the wanted distance in metres, the editor on "ads" */
  tfp_aim_bind_tick(true, .35f, 15, false, 180);
  CHECK(tfp_pose_ads_probe(&ads) && ads.sight == .35f && ads.wanted == .15f && !ads.editing);
  tfp_aim_bind_tick(false, .35f, 0, true, 180);
  CHECK(tfp_pose_ads_probe(&ads) && ads.sight == 0 && ads.wanted == 0 && ads.editing);
  tfp_aim_bind_tick(true, .22f, 5, false, 180);
  CHECK(tfp_pose_ads_probe(&ads) && ads.sight == .22f && ads.wanted == .05f && !ads.editing);
  /* round 42: the aim-in time to all three helpers, in whole ms; not a number or out of 0 .. 300: the clock's own 180 */
  CHECK(tfp_pose_ads_probe(&ads) && ads.aim_in_ms == 180 && tfp_fov_ads_enter_ms() == 180 && tfp_near_ads_enter_ms() == 180);
  tfp_aim_bind_tick(true, .22f, 5, false, 0);
  CHECK(tfp_pose_ads_probe(&ads) && ads.aim_in_ms == 0 && tfp_fov_ads_enter_ms() == 0 && tfp_near_ads_enter_ms() == 0);
  tfp_aim_bind_tick(true, .22f, 5, false, 90);
  CHECK(tfp_pose_ads_probe(&ads) && ads.aim_in_ms == 90 && tfp_fov_ads_enter_ms() == 90 && tfp_near_ads_enter_ms() == 90);
  tfp_aim_bind_tick(true, .22f, 5, false, NAN);
  CHECK(tfp_pose_ads_probe(&ads) && ads.aim_in_ms == 180 && tfp_fov_ads_enter_ms() == 180 && tfp_near_ads_enter_ms() == 180);
  CHECK(tfp_aim_bind_aim_in(-10) == 180 && tfp_aim_bind_aim_in(301) == 180 && tfp_aim_bind_aim_in(300) == 300 && tfp_aim_bind_aim_in(9.6) == 10);

  tfp_near_set_ads_source(NULL);
  printf("aim bind: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
