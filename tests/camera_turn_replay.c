/* Round 49: THE CARRY of src/camera_turn.h run again over logged frames (tests/test_harness_turn.py). Standard input:
 * one frame a line, "tick kind x y z": the helper's clock (ms), the camera kind, and the direction the frame was
 * shown with on a build without the carry (the Trace row's turn=: the game's own direction; "0 0 0": not known).
 * Standard output, a line for a line: "x y z carried began", the direction the frame is shown with now, the degrees
 * carried in it and the degrees a change of camera began a carry with. The log has the forward axis only, so the
 * rotation is rebuilt level (no roll); a frame nothing is carried in gets its own numbers back as they were read. */
#include "camera_turn.h"
#include <stdio.h>
static bool level(const float *f, TfpQ *q) {
  const float n = sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]), h = sqrtf(f[0]*f[0]+f[1]*f[1]);
  if (!(n > .5f) || !(h > 1e-4f*n)) return false;
  const float fwd[3] = {f[0]/n, f[1]/n, f[2]/n}, right[3] = {fwd[1]*n/h, -fwd[0]*n/h, 0};
  const float up[3] = {right[1]*fwd[2]-right[2]*fwd[1], right[2]*fwd[0]-right[0]*fwd[2], right[0]*fwd[1]-right[1]*fwd[0]};
  const float basis[9] = {right[0], right[1], right[2], fwd[0], fwd[1], fwd[2], up[0], up[1], up[2]};
  return tfp_turn_basis_q(basis, q);
}
int main(void) {
  TfpTurn t; tfp_turn_reset(&t);
  double tick; unsigned kind; float f[3]; char line[256];
  while (fgets(line, sizeof line, stdin)) {
    if (sscanf(line, "%lf %u %f %f %f", &tick, &kind, &f[0], &f[1], &f[2]) != 5) return 2;
    TfpQ game = tfp_q_identity(); float began = 0, out[3] = {f[0], f[1], f[2]};
    const bool known = level(f, &game);
    const TfpQ carry = tfp_turn_step(&t, game, known, kind, tick, 250, &began);
    if (known && !tfp_turn_none(carry)) { const float ahead[3] = {0, 1, 0}; tfp_q_rotate(tfp_turn_apply(game, carry), ahead, out); }
    printf("%.6f %.6f %.6f %.3f %.3f\n", (double)out[0], (double)out[1], (double)out[2], tfp_turn_none(carry) ? 0.0 : (double)tfp_turn_degrees(carry), (double)began);
  }
  return 0;
}
