/* C port of the accepted post-pin camera_correction.h behavior. */
#include "correction.h"
#include <math.h>
#include <string.h>
static bool vector(const float *p) {
  return p && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]) &&
         fabsf(p[0]) <= 16 && fabsf(p[1]) <= 16 && fabsf(p[2]) <= 16;
}
static bool residual(const float *a, const float *b) {
  double sum = 0;
  for (unsigned i = 0; i < 3; i++) {
    double d = (double)a[i] - b[i];
    sum += d * d;
  }
  return sum <= 1;
}
void fp_correction_reset(FpCorrection *s) {
  memset(s, 0, sizeof *s);
  s->progress = 1;
}
bool fp_correction_remember(FpCorrection *s, const float p[3], uint32_t weapon,
                            double tick) {
  if (!vector(p) || !weapon || weapon == UINT32_MAX || !isfinite(tick) ||
      tick < 0 || (s->observed && tick < s->last_tick)) {
    fp_correction_reset(s);
    return false;
  }
  fp_correction_reset(s);
  s->remembered = s->observed = true;
  s->weapon = weapon;
  s->native_tick = s->last_tick = tick;
  memcpy(s->native_relative, p, 12);
  return true;
}
bool fp_correction_apply(FpCorrection *s, const float p[3], uint32_t weapon,
                         double tick, float out[3], float duration,
                         uint32_t gap) {
  bool valid = vector(p);
  if (out && valid)
    memcpy(out, p, 12);
  if (!out || !valid || !weapon || weapon == UINT32_MAX || !isfinite(tick) ||
      tick < 0 || !isfinite(duration) || duration < 0 || duration > 500 ||
      gap < 16 || gap > 1000) {
    fp_correction_reset(s);
    return false;
  }
  if (!duration ||
      (s->observed && (weapon != s->weapon || tick < s->last_tick ||
                       tick - s->last_tick > gap))) {
    fp_correction_reset(s);
    return true;
  }
  if (s->remembered) {
    if (tick < s->native_tick || tick - s->native_tick > gap ||
        !residual(s->native_relative, p)) {
      fp_correction_reset(s);
      return true;
    }
    s->remembered = false;
    s->active = true;
    s->started = tick;
    s->used = 0;
    s->progress = 0;
    s->duration = duration;
    memcpy(s->last, s->native_relative, 12);
  }
  if (!s->active)
    return true;
  if (!residual(s->last, p)) {
    fp_correction_reset(s);
    return true;
  }
  double fraction = fmin(1, (tick - s->started) / s->duration);
  s->progress = (float)fraction;
  s->last_tick = tick;
  s->observed = true;
  if (fraction >= 1) {
    s->active = false;
    s->used = 1;
    memcpy(s->last, p, 12);
    return true;
  }
  double current = fraction * fraction * (3 - 2 * fraction);
  double step =
      fmax(0, fmin(1, (current - s->used) / fmax(1e-12, 1 - s->used)));
  if (!step)
    memcpy(out, s->last, 12);
  else
    for (unsigned i = 0; i < 3; i++)
      out[i] = (float)(s->last[i] + (p[i] - s->last[i]) * step);
  memcpy(s->last, out, 12);
  s->used = current;
  return true;
}
