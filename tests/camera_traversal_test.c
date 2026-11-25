/* src/camera_traversal.h (round 24): what the continuity and the eye damping are told in a clamber or a vault. */
#include "camera_traversal.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
  /* the bit: a low vault 0x20, a high clamber 0x22; a swap 2, a stim 0x40, nothing 0 are not */
  CHECK(tfp_traversal(0x20) && tfp_traversal(0x22) && !tfp_traversal(0) && !tfp_traversal(2) && !tfp_traversal(0x40));
  /* the window opens again on a change of mask, not into, out of or within a traversal */
  CHECK(tfp_traversal_mask_rearms(0, 2) && tfp_traversal_mask_rearms(2, 0x40) && !tfp_traversal_mask_rearms(2, 2));
  CHECK(!tfp_traversal_mask_rearms(0, 0x22) && !tfp_traversal_mask_rearms(0x22, 0x20) && !tfp_traversal_mask_rearms(0x20, 2) && !tfp_traversal_mask_rearms(0x20, 0));
  /* the root's motion, at 8, 16.7, 33.3 and 50 ms a frame: 6 m/s is moving; it lingers 300 ms after it stops; 0.3 m/s is not */
  static const float times[] = {8, 16.7f, 33.3f, 50};
  for (unsigned d = 0; d < 4; ++d) {
    TfpRootMotion m = {0}; double tick = 1000; float z = 0; bool moving = false;
    for (int i = 0; i < 10; ++i, tick += times[d]) { const float root[3] = {0, 0, z}; moving = tfp_root_motion_step(&m, root, tick, 250); }
    CHECK(!moving);
    for (int i = 0; i < 10; ++i, tick += times[d]) { z += 6*times[d]/1000; const float root[3] = {0, 0, z}; moving = tfp_root_motion_step(&m, root, tick, 250); }
    CHECK(moving);
    const double stopped = tick-times[d];
    bool still_at = false;
    for (int i = 0; i < 40; ++i, tick += times[d]) { const float root[3] = {0, 0, z}; moving = tfp_root_motion_step(&m, root, tick, 250);
      if (tick-stopped < 300-1e-6 && !moving) still_at = true; }
    CHECK(!still_at && !moving);
    for (int i = 0; i < 20; ++i, tick += times[d]) { z += .3f*times[d]/1000; const float root[3] = {0, 0, z}; moving = tfp_root_motion_step(&m, root, tick, 250); CHECK(!moving); }
    /* a pause longer than the frame gap forgets it */
    { const float root[3] = {0, 0, z+1}; tick += 400; CHECK(!tfp_root_motion_step(&m, root, tick, 250)); }
    /* the traversal lingers 600 ms for the damping, and a pause forgets it */
    TfpTraversalWatch w = {0}; tick = 5000;
    CHECK(tfp_traversal_recent(&w, 0x20, tick, 250));
    bool gone_early = false; double t0 = tick;
    for (tick += times[d]; tick-t0 < 590; tick += times[d]) if (!tfp_traversal_recent(&w, 2, tick, 250)) gone_early = true;
    CHECK(!gone_early);
    for (; tick-t0 < 620; tick += times[d]) (void)tfp_traversal_recent(&w, 2, tick, 250);
    CHECK(!tfp_traversal_recent(&w, 2, tick, 250));
    CHECK(tfp_traversal_recent(&w, 0x22, tick, 250) && !tfp_traversal_recent(&w, 0, tick+300, 250));
  }
  /* detect: off for a head-camera frame of a traversal with the root moving, only */
  CHECK(!tfp_traversal_detect(true, true, 0x22, true) && !tfp_traversal_detect(true, true, 0x20, true));
  CHECK(tfp_traversal_detect(true, true, 0x20, false) && tfp_traversal_detect(true, false, 0x20, true) && tfp_traversal_detect(true, true, 2, true));
  CHECK(!tfp_traversal_detect(false, false, 0, false));
  CHECK(tfp_traversal_damps(true, false) && !tfp_traversal_damps(true, true) && !tfp_traversal_damps(false, false));
  printf("camera traversal: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
