/* Native quaternion/scene-space math shared by the C policies.
 * No engine, Lua, CFFI, C++ runtime or mutable global state. */
#include "presentation.h"
#include <math.h>
#include <string.h>
#define FP_PI 3.14159265358979323846f
static float clamp(float n, float low, float high) {
  return fmaxf(low, fminf(high, n));
}
static float limit(float n, float bound) { return clamp(n, -bound, bound); }
FpQuat fp_q_identity(void) { return (FpQuat){0, 0, 0, 1}; }
FpQuat fp_q_mul(FpQuat a, FpQuat b) {
  return (FpQuat){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                  a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                  a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
FpQuat fp_q_inverse(FpQuat q) { return (FpQuat){-q.x, -q.y, -q.z, q.w}; }
bool fp_q_valid(FpQuat q) {
  float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
  return isfinite(n) && fabsf(n - 1) < .025f;
}
void fp_q_rotate(FpQuat q, const float *v, float *out) {
  FpQuat r =
      fp_q_mul(fp_q_mul(q, (FpQuat){v[0], v[1], v[2], 0}), fp_q_inverse(q));
  out[0] = r.x;
  out[1] = r.y;
  out[2] = r.z;
}
FpQuat fp_q_look(float yaw, float pitch) {
  return fp_q_mul((FpQuat){0, 0, sinf(yaw / 2), cosf(yaw / 2)},
                  (FpQuat){sinf(pitch / 2), 0, 0, cosf(pitch / 2)});
}
bool fp_q_angles(FpQuat q, float *yaw, float *pitch) {
  if (!yaw || !pitch || !fp_q_valid(q))
    return false;
  float x = 2 * (q.x * q.y - q.w * q.z), y = 1 - 2 * (q.x * q.x + q.z * q.z),
        z = 2 * (q.y * q.z + q.w * q.x);
  *yaw = atan2f(-x, y);
  *pitch = asinf(limit(z, 1));
  return fabsf(*pitch) < FP_PI * .47f;
}
bool fp_q_matrix(const float *m, FpQuat *out) {
  float n[9] = {0};
  if (!m || !out)
    return false;
  for (unsigned a = 0; a < 3; a++) {
    float length = 0;
    for (unsigned b = 0; b < 3; b++)
      length += m[a * 4 + b] * m[a * 4 + b];
    if (!isfinite(length) || fabsf(length - 1) > .04f)
      return false;
    for (unsigned b = 0; b < 3; b++)
      n[a * 3 + b] = m[a * 4 + b];
  }
  for (unsigned a = 0; a < 3; a++)
    for (unsigned b = a + 1; b < 3; b++) {
      float d = 0;
      for (unsigned i = 0; i < 3; i++)
        d += n[a * 3 + i] * n[b * 3 + i];
      if (fabsf(d) > .02f)
        return false;
    }
  float det = n[0] * (n[4] * n[8] - n[5] * n[7]) -
              n[1] * (n[3] * n[8] - n[5] * n[6]) +
              n[2] * (n[3] * n[7] - n[4] * n[6]);
  if (det < .95f)
    return false;
  float trace = n[0] + n[4] + n[8];
  FpQuat q;
  if (trace > 0) {
    float s = sqrtf(trace + 1) * 2;
    q = (FpQuat){(n[5] - n[7]) / s, (n[6] - n[2]) / s, (n[1] - n[3]) / s,
                 s / 4};
  } else if (n[0] > n[4] && n[0] > n[8]) {
    float s = sqrtf(1 + n[0] - n[4] - n[8]) * 2;
    q = (FpQuat){s / 4, (n[3] + n[1]) / s, (n[6] + n[2]) / s,
                 (n[5] - n[7]) / s};
  } else if (n[4] > n[8]) {
    float s = sqrtf(1 + n[4] - n[0] - n[8]) * 2;
    q = (FpQuat){(n[3] + n[1]) / s, s / 4, (n[7] + n[5]) / s,
                 (n[6] - n[2]) / s};
  } else {
    float s = sqrtf(1 + n[8] - n[0] - n[4]) * 2;
    q = (FpQuat){(n[6] + n[2]) / s, (n[7] + n[5]) / s, s / 4,
                 (n[1] - n[3]) / s};
  }
  if (!fp_q_valid(q))
    return false;
  *out = q;
  return true;
}
