/* tests/clip_frames_test.cpp: the binder line of src/aim_bind.h as src/wire_win_pose.c calls it (C: the header is C). */
#include "../src/aim_bind.h"
#include <string.h>
void clip_frames_bind(void) {
  TfpPoseHost host;
  memset(&host, 0, sizeof host);
  tfp_aim_bind_pose(&host);
}
void clip_frames_unbind(void) { tfp_near_set_ads_source(NULL); tfp_near_set_fov_source(NULL); }
