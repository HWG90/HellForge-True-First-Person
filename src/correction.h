#ifndef TFP_CORRECTION_H
#define TFP_CORRECTION_H
#include <stdbool.h>
#include <stdint.h>
/* Body-relative POSITION ONLY handoff; double milliseconds retain fractional
 * QPC cadence. No engine pointers, input integration or orientation state. */
typedef struct FpCorrection {
  bool remembered, observed, active;
  uint32_t weapon;
  double native_tick, last_tick, started, used;
  float duration, progress, native_relative[3], last[3];
} FpCorrection;
void fp_correction_reset(FpCorrection *);
bool fp_correction_remember(FpCorrection *, const float[3], uint32_t, double);
bool fp_correction_apply(FpCorrection *, const float[3], uint32_t, double,
                         float[3], float duration_ms, uint32_t max_gap_ms);
#endif
