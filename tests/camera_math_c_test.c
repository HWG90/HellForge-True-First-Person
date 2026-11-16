/* camera_math.h must also be valid C11 (the differential test builds it as C++). */
#include "camera_math.h"
#include <stdio.h>
int main(void) {
  TfpCorrection c;
  float native[3] = {0.1f, 0.2f, 0.3f}, target[3] = {0.1f, 0.25f, 0.3f}, out[3];
  tfp_correction_reset(&c);
  if (!tfp_correction_remember(&c, native, 7, 1000) || !tfp_correction_apply(&c, target, 7, 1010, out, 150, 250) ||
      !(out[1] >= 0.2f && out[1] < 0.25f) || !c.active) return 1;
  TfpQ q = tfp_q_identity();
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  if (!tfp_matrix_q(m, &q) || q.w != 1) return 1;
  puts("Camera math: compiles and runs as C");
  return 0;
}
