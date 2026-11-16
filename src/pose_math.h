/* Pure arithmetic of the retail editor/pose helper, translated from
 * native/first_person/presentation_phase.h, zoom_input_math.h (the helpers
 * zoom_gain_math.h uses) and zoom_gain_math.h at 0.5.41-smooth.66. No engine
 * reads, no pointers kept. Expressions keep retail's types, order and SSE
 * scalar rounding points so results are bit-identical. Valid as C and as
 * C++, so a test can include it next to the retail headers. Do not compile
 * with fast-math or FMA contraction. */
#ifndef TFP_POSE_MATH_H
#define TFP_POSE_MATH_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
#include <emmintrin.h>

/* std::max(a,b) and std::min(a,b) exactly (they are not fmaxf/fminf when a
 * NaN is involved): max returns b only when a<b, min returns b only when b<a. */
static inline float tfp_std_maxf(float a, float b) { return a < b ? b : a; }
static inline float tfp_std_minf(float a, float b) { return b < a ? b : a; }

/* ---- presentation_phase.h ---- */
/* h:10 */
static inline float tfp_phase_eased(float p) { return p*p*(3-2*p); }
/* h:11-14 */
typedef struct TfpPhase {
  uint64_t epoch, used_epoch, published_at;
  float progress, used; /* progress starts at 1 */
  bool ads;
} TfpPhase;
/* h:28, State{} */
static inline void tfp_phase_reset(TfpPhase *s) { memset(s, 0, sizeof *s); s->progress = 1; }
/* h:15-19 */
static inline bool tfp_phase_publish(TfpPhase *s, uint64_t e, float p, uint32_t a, uint64_t now) {
  if (!e || !isfinite(p) || p < 0 || p > 1 || a > 1 || e < s->epoch
      || (e == s->epoch && (p < s->progress || s->ads != (a != 0)))) return false;
  s->epoch = e; s->progress = p; s->ads = a != 0; s->published_at = now; return true;
}
/* h:20. now 0 = retail's default argument. */
static inline bool tfp_phase_matches(const TfpPhase *s, bool actual_ads, uint64_t now) {
  return s->epoch != 0 && s->ads == actual_ads
      && (!now || !s->published_at || (now >= s->published_at && now - s->published_at <= 250));
}
/* h:21-27 */
static inline float tfp_phase_step(TfpPhase *s, bool actual_ads) {
  if (!tfp_phase_matches(s, actual_ads, 0)) return 1;
  if (s->used_epoch != s->epoch) { s->used_epoch = s->epoch; s->used = 0; }
  const float current = tfp_phase_eased(s->progress);
  const float amount = s->progress >= 1 ? 1
      : tfp_std_maxf(0.0f, tfp_std_minf(1.0f, (current - s->used)/tfp_std_maxf(.000001f, 1 - s->used)));
  s->used = current; return amount;
}

/* ---- zoom_input_math.h: the helpers (its own prepare() is not called by the editor) ---- */
typedef double (*TfpExpFn)(double);
#define TFP_ZOOM_RAD_TO_DEG 57.2957763671875f /* zin:18 */
#define TFP_ZOOM_MIN_RATIO (1.0f/4096.0f)     /* zin:19 */
#define TFP_ZOOM_MAX_RATIO 16.0f
#define TFP_ZOOM_MAX_COMPONENT 1000000.0f     /* zin:20 */
/* zin:28-43 */
static inline uint32_t tfp_zoom_bits(float v) { uint32_t b = 0; memcpy(&b, &v, 4); return b; }
static inline bool tfp_zoom_ordinary(float v) {
  const uint32_t a = tfp_zoom_bits(v) & 0x7fffffffU;
  return a < 0x7f800000U && (a == 0 || a >= 0x00800000U);
}
static inline bool tfp_zoom_bounded(float v) { return tfp_zoom_ordinary(v) && fabsf(v) <= TFP_ZOOM_MAX_COMPONENT; }
static inline float tfp_zoom_mul(float a, float b) { return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static inline float tfp_zoom_add(float a, float b) { return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static inline float tfp_zoom_sub(float a, float b) { return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static inline float tfp_zoom_divide(float a, float b) { return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_set_ss(b))); }
static inline float tfp_zoom_narrow(double v) { return _mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), _mm_set_sd(v))); }
static inline float tfp_zoom_filtered(float raw, float history, float a, float b) {
  return tfp_zoom_add(tfp_zoom_mul(a, raw), tfp_zoom_mul(b, history));
}
static inline float tfp_zoom_neighbour(float v, bool upward) {
  uint32_t n = tfp_zoom_bits(v);
  if ((n & 0x7fffffffU) == 0) n = upward ? 1U : 0x80000001U;
  else if ((n & 0x80000000U) == 0) n = upward ? n + 1 : n - 1;
  else n = upward ? n - 1 : n + 1;
  float result = 0; memcpy(&result, &n, 4); return result;
}

/* ---- zoom_gain_math.h ---- */
/* gain:10-12 */
enum { TFP_GAIN_READY = 0, TFP_GAIN_IDENTITY = 1, TFP_GAIN_INVALID_INPUT = 2, TFP_GAIN_UNSUPPORTED_ENVIRONMENT = 3,
       TFP_GAIN_EXP_UNAVAILABLE = 4, TFP_GAIN_INVALID_COEFFICIENT = 5, TFP_GAIN_SINGULAR = 6, TFP_GAIN_ILL_CONDITIONED = 7,
       TFP_GAIN_GAIN_RANGE = 8, TFP_GAIN_UNREPRESENTABLE = 9, TFP_GAIN_GATE_MISMATCH = 10, TFP_GAIN_EXCESS_ERROR = 11 };
/* gain:13-20 */
#define TFP_GAIN_MIN_GAIN 0.000001f
#define TFP_GAIN_MAX_GAIN 1000000.0f
#define TFP_GAIN_MIN_GAIN_RATIO (1.0f/4096.0f)
#define TFP_GAIN_MAX_GAIN_RATIO 4096.0f
#define TFP_GAIN_MIN_CONDITION (1.0/4096.0)
#define TFP_GAIN_ABS_TOLERANCE 0.0000002
#define TFP_GAIN_REL_TOLERANCE 0.000002
#define TFP_GAIN_MAX_INCREMENT_ERROR 0.000002
#define TFP_GAIN_PITCH_ADD_BOUND 0.0000002384185791015625
/* gain:21-34. Same fields and order (input_same in the helper compares
 * dt_eval..device as four adjacent floats). Defaults: sign {1,1}, the rest 0. */
typedef struct TfpGainInput {
  float raw[3], history[3], dt_eval, lens_radians, tpv_degrees;
  float device, sign[2], steering[2], context[2], native_gain[2], dt_global;
  bool pitch_gate;
} TfpGainInput;
typedef struct TfpGainPipeline { float before_gate, after_gate, increment; bool gate_zero; } TfpGainPipeline;
typedef struct TfpGainAxis {
  float original_gain, gain, x, target;
  TfpGainPipeline natural, wanted, expected; bool input_dependent;
} TfpGainAxis;
typedef struct TfpGainPrepared {
  float gains[2], native_history[3], ratio, alpha, beta;
  uint32_t mxcsr; bool changed, input_dependent; TfpGainAxis axis[2];
} TfpGainPrepared;
static inline void tfp_gain_input_reset(TfpGainInput *in) { memset(in, 0, sizeof *in); in->sign[0] = 1; in->sign[1] = 1; }
/* gain:35 */
static inline bool tfp_gain_accepted(uint32_t r) { return r == TFP_GAIN_READY || r == TFP_GAIN_IDENTITY; }
/* gain:36-40 */
static inline bool tfp_gain_valid(float g, float original) {
  if (!tfp_zoom_ordinary(g) || g < TFP_GAIN_MIN_GAIN || g > TFP_GAIN_MAX_GAIN) return false;
  const double relative = (double)g/original;
  return relative >= TFP_GAIN_MIN_GAIN_RATIO && relative <= TFP_GAIN_MAX_GAIN_RATIO;
}
/* gain:41-60 */
static inline bool tfp_gain_pipeline(unsigned axis, float value, float gain, float context, float dt_eval, float dt_global,
                                     bool pitch_gate, TfpGainPipeline *out) {
  memset(out, 0, sizeof *out);
  if (dt_eval > 0) {
    const float divisor = tfp_zoom_mul(dt_eval, gain);
    if (!tfp_zoom_ordinary(divisor) || divisor <= 0) return false;
    const float contribution = tfp_zoom_divide(context, divisor);
    if (!tfp_zoom_bounded(contribution)) return false;
    value = tfp_zoom_add(value, contribution);
  }
  if (!tfp_zoom_bounded(value)) return false;
  out->before_gate = value; out->gate_zero = axis == 1 && pitch_gate && value > 0;
  out->after_gate = out->gate_zero ? 0.0f : value;
  /* Native yaw XORs the sign before multiplication and then halves the angle. */
  const float signed_value = axis == 0 ? -out->after_gate : out->after_gate;
  out->increment = tfp_zoom_mul(tfp_zoom_mul(signed_value, gain), dt_global);
  if (axis == 0) out->increment = tfp_zoom_mul(out->increment, 0.5f);
  return tfp_zoom_bounded(out->increment);
}
/* gain:61-65 (not called by the editor; kept with the header) */
static inline float tfp_gain_pitch_after_base(float base, float increment) {
  const float angle = tfp_zoom_add(base, increment);
  const float lower = -1.5707963705062866f, upper = 1.5707963705062866f;
  return lower > angle ? lower : _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(upper), _mm_set_ss(angle)));
}
/* gain:81-143: everything after `PreserveEnvironment preserve(csr)`. The
 * caller restores MXCSR on return, which is the guard's destructor. */
static inline uint32_t tfp_gain_prepare_guarded(const TfpGainInput *in, TfpExpFn native_exp, TfpGainPrepared *output, uint32_t csr) {
  output->mxcsr = csr;
  output->ratio = tfp_zoom_divide(tfp_zoom_mul(in->lens_radians, TFP_ZOOM_RAD_TO_DEG), in->tpv_degrees);
  if (!tfp_zoom_ordinary(output->ratio) || output->ratio < TFP_ZOOM_MIN_RATIO || output->ratio > TFP_ZOOM_MAX_RATIO) return TFP_GAIN_INVALID_INPUT;
  float a = 1;
  if (in->dt_eval > 0) {
    if (!native_exp) return TFP_GAIN_EXP_UNAVAILABLE;
    const double e = native_exp((double)tfp_zoom_mul(in->dt_eval, -75.0f));
    if ((_mm_getcsr() & 0xffc0U) != (csr & 0xffc0U)) return TFP_GAIN_UNSUPPORTED_ENVIRONMENT;
    if (!isfinite(e) || e < 0 || e > 1) return TFP_GAIN_INVALID_COEFFICIENT;
    a = tfp_zoom_narrow(_mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(1.0), _mm_set_sd(e))));
  }
  if (!tfp_zoom_ordinary(a) || a < 0 || a > 1) return TFP_GAIN_INVALID_COEFFICIENT;
  output->alpha = a; output->beta = tfp_zoom_sub(1, a);
  for (unsigned i = 0; i < 3; ++i) {
    output->native_history[i] = tfp_zoom_filtered(in->raw[i], in->history[i], a, output->beta);
    if (!tfp_zoom_bounded(output->native_history[i])) return TFP_GAIN_UNREPRESENTABLE;
  }
  for (unsigned i = 0; i < 2; ++i) {
    TfpGainAxis *axis = &output->axis[i]; axis->original_gain = axis->gain = in->native_gain[i];
    axis->input_dependent = in->steering[i] != 0;
    output->input_dependent = output->input_dependent || axis->input_dependent;
    const float inverted = tfp_zoom_mul(in->sign[i], output->native_history[i]);
    const float unzoomed = tfp_zoom_mul(in->device, inverted), zoomed = tfp_zoom_mul(in->device, tfp_zoom_mul(output->ratio, inverted));
    axis->x = tfp_zoom_add(zoomed, in->steering[i]); axis->target = tfp_zoom_add(unzoomed, in->steering[i]);
    if (!tfp_zoom_bounded(axis->x) || !tfp_zoom_bounded(axis->target)
        || !tfp_gain_pipeline(i, axis->x, axis->original_gain, in->context[i], in->dt_eval, in->dt_global, in->pitch_gate, &axis->natural)
        || !tfp_gain_pipeline(i, axis->target, axis->original_gain, in->context[i], in->dt_eval, in->dt_global, in->pitch_gate, &axis->wanted)) return TFP_GAIN_UNREPRESENTABLE;
    float centre = 0;
    if (!axis->input_dependent) {
      /* Common S=0: choose the same gain even while input is zero or
       * changes after sampling. No predicted-U inverse or neighbour
       * selection may make this gain depend on raw/history/device/dt. */
      centre = tfp_zoom_divide(axis->original_gain, output->ratio);
    } else {
      if (axis->x == axis->target || (axis->natural.gate_zero == axis->wanted.gate_zero
          && tfp_zoom_bits(axis->natural.increment) == tfp_zoom_bits(axis->wanted.increment))) { axis->expected = axis->natural; continue; }
      if (axis->x == 0 || axis->target == 0) return TFP_GAIN_SINGULAR;
      const double x_scale = fabs((double)zoomed) + fabs((double)in->steering[i]);
      const double target_scale = fabs((double)unzoomed) + fabs((double)in->steering[i]);
      /* Relative cancellation only; no absolute slow-pan epsilon. */
      if ((x_scale > 0 && fabsf(axis->x) < TFP_GAIN_MIN_CONDITION*x_scale)
          || (target_scale > 0 && fabsf(axis->target) < TFP_GAIN_MIN_CONDITION*target_scale)) return TFP_GAIN_ILL_CONDITIONED;
      centre = tfp_zoom_narrow((double)axis->original_gain*axis->target/axis->x);
    }
    if (!tfp_gain_valid(centre, axis->original_gain)) return TFP_GAIN_GAIN_RANGE;
    float candidates[3];
    candidates[0] = centre; candidates[1] = tfp_zoom_neighbour(centre, false); candidates[2] = tfp_zoom_neighbour(centre, true);
    bool found = false; double best = (double)INFINITY;
    const unsigned candidate_count = axis->input_dependent ? 3 : 1;
    for (unsigned k = 0; k < candidate_count; ++k) { const float g = candidates[k]; if (tfp_gain_valid(g, axis->original_gain)) {
      TfpGainPipeline expected; memset(&expected, 0, sizeof expected);
      if (!tfp_gain_pipeline(i, axis->x, g, in->context[i], in->dt_eval, in->dt_global, in->pitch_gate, &expected)) continue;
      if (expected.gate_zero != axis->wanted.gate_zero) continue;
      const double error = fabs((double)expected.increment - axis->wanted.increment);
      if (isfinite(error) && error < best) { found = true; best = error; axis->gain = g; axis->expected = expected; }
    }}
    if (!found) return TFP_GAIN_GATE_MISMATCH;
    if (best > TFP_GAIN_MAX_INCREMENT_ERROR
        || best > TFP_GAIN_ABS_TOLERANCE + TFP_GAIN_REL_TOLERANCE*fabs((double)axis->wanted.increment)) return TFP_GAIN_EXCESS_ERROR;
  }
  for (unsigned i = 0; i < 2; ++i) { output->gains[i] = output->axis[i].gain;
    output->changed = output->changed || tfp_zoom_bits(output->gains[i]) != tfp_zoom_bits(in->native_gain[i]); }
  return output->changed ? TFP_GAIN_READY : TFP_GAIN_IDENTITY;
}
/* gain:66-143 */
static inline uint32_t tfp_gain_prepare(const TfpGainInput *source, TfpExpFn native_exp, TfpGainPrepared *output) {
  const TfpGainInput in = *source; memset(output, 0, sizeof *output);
  for (unsigned i = 0; i < 2; ++i) output->gains[i] = in.native_gain[i];
  if (!tfp_zoom_ordinary(in.dt_eval) || in.dt_eval < -1 || in.dt_eval > 1 || !tfp_zoom_ordinary(in.dt_global)
      || in.dt_global < 0 || in.dt_global > 1 || !tfp_zoom_ordinary(in.device) || in.device < 0 || in.device > TFP_ZOOM_MAX_COMPONENT
      || !tfp_zoom_ordinary(in.lens_radians) || in.lens_radians <= 0 || in.lens_radians >= 3.141592654f
      || !tfp_zoom_ordinary(in.tpv_degrees) || in.tpv_degrees < 1 || in.tpv_degrees >= 180) return TFP_GAIN_INVALID_INPUT;
  for (unsigned i = 0; i < 3; ++i) if (!tfp_zoom_bounded(in.raw[i]) || !tfp_zoom_bounded(in.history[i])) return TFP_GAIN_INVALID_INPUT;
  for (unsigned i = 0; i < 2; ++i) if ((in.sign[i] != 1 && in.sign[i] != -1) || !tfp_zoom_bounded(in.steering[i])
      || !tfp_zoom_bounded(in.context[i]) || !tfp_zoom_ordinary(in.native_gain[i])
      || in.native_gain[i] < TFP_GAIN_MIN_GAIN || in.native_gain[i] > TFP_GAIN_MAX_GAIN) return TFP_GAIN_INVALID_INPUT;
  const uint32_t csr = _mm_getcsr();
  if ((csr & 0x6000U) != 0 || (csr & 0x1f80U) != 0x1f80U) return TFP_GAIN_UNSUPPORTED_ENVIRONMENT;
  const uint32_t reason = tfp_gain_prepare_guarded(&in, native_exp, output, csr);
  _mm_setcsr(csr); /* ~PreserveEnvironment */
  return reason;
}
#endif
