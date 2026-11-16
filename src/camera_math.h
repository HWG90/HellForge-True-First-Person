/* Pure arithmetic of the retail camera helper, translated from
 * native/first_person/free_aim_math.h, camera_correction.h and
 * camera_handoff.h at 0.5.41-smooth.66. No engine reads, no pointers kept.
 * Expressions keep retail's types and order so results are bit-identical
 * (tests/diff_camera_math.cpp runs both on the same inputs). Valid as C and
 * as C++, so that test can include it next to the retail headers. */
#ifndef TFP_CAMERA_MATH_H
#define TFP_CAMERA_MATH_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- free_aim_math.h (the parts the live camera path uses) ---- */
#define TFP_PI 3.14159265358979323846f
typedef struct TfpQ { float x, y, z, w; } TfpQ;
static inline TfpQ tfp_q(float x, float y, float z, float w) { TfpQ q; q.x = x; q.y = y; q.z = z; q.w = w; return q; }
static inline TfpQ tfp_q_identity(void) { return tfp_q(0, 0, 0, 1); }
static inline TfpQ tfp_q_mul(TfpQ a, TfpQ b) {
  return tfp_q(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y, a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
               a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w, a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);
}
static inline void tfp_q_rotate(TfpQ q, const float *v, float *out) {
  const TfpQ r = tfp_q_mul(tfp_q_mul(q, tfp_q(v[0], v[1], v[2], 0)), tfp_q(-q.x, -q.y, -q.z, q.w));
  out[0] = r.x; out[1] = r.y; out[2] = r.z;
}
static inline TfpQ tfp_q_inv(TfpQ q) { return tfp_q(-q.x, -q.y, -q.z, q.w); }
static inline bool tfp_q_valid(TfpQ q) {
  const float n = q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
  return isfinite(n) && fabsf(n-1) < .025f;
}
/* The rotation of a 4x4 row matrix whose rows are unit and orthogonal. */
static inline bool tfp_matrix_q(const float *m, TfpQ *q) {
  float n[9];
  memset(n, 0, sizeof n);
  for (unsigned a = 0; a < 3; ++a) {
    float len = 0;
    for (unsigned b = 0; b < 3; ++b) len += m[a*4+b]*m[a*4+b];
    if (!isfinite(len) || fabsf(len-1) > .04f) return false;
    for (unsigned b = 0; b < 3; ++b) n[a*3+b] = m[a*4+b];
  }
  for (unsigned a = 0; a < 3; ++a)
    for (unsigned b = a+1; b < 3; ++b) {
      float d = 0;
      for (unsigned i = 0; i < 3; ++i) d += n[a*3+i]*n[b*3+i];
      if (fabsf(d) > .02f) return false;
    }
  const float det = n[0]*(n[4]*n[8]-n[5]*n[7])-n[1]*(n[3]*n[8]-n[5]*n[6])+n[2]*(n[3]*n[7]-n[4]*n[6]);
  if (det < .95f) return false;
  const float trace = n[0]+n[4]+n[8];
  if (trace > 0) { float s = sqrtf(trace+1)*2; *q = tfp_q((n[5]-n[7])/s, (n[6]-n[2])/s, (n[1]-n[3])/s, s/4); }
  else if (n[0] > n[4] && n[0] > n[8]) { float s = sqrtf(1+n[0]-n[4]-n[8])*2; *q = tfp_q(s/4, (n[3]+n[1])/s, (n[6]+n[2])/s, (n[5]-n[7])/s); }
  else if (n[4] > n[8]) { float s = sqrtf(1+n[4]-n[0]-n[8])*2; *q = tfp_q((n[3]+n[1])/s, s/4, (n[7]+n[5])/s, (n[6]-n[2])/s); }
  else { float s = sqrtf(1+n[8]-n[0]-n[4])*2; *q = tfp_q((n[6]+n[2])/s, (n[7]+n[5])/s, s/4, (n[1]-n[3])/s); }
  return tfp_q_valid(*q);
}

/* ---- camera_correction.h: a short handoff correction in body-root coordinates ---- */
#define TFP_CORRECTION_MAX_RESIDUAL 1.0f
#define TFP_CORRECTION_MAX_COMPONENT 16.0f
typedef struct TfpCorrection {
  float progress;            /* 1 when idle */
  bool active, remembered, observed;
  uint32_t current_weapon;
  double native_tick, last_tick, started, used;
  float handoff_duration_ms; /* 150 by default */
  float native_relative[3], last_presented[3];
} TfpCorrection;
static inline void tfp_correction_reset(TfpCorrection *s) {
  memset(s, 0, sizeof *s);
  s->progress = 1;
  s->handoff_duration_ms = 150;
}
static inline bool tfp_correction_finite(const float *p) {
  if (!p) return false;
  for (unsigned i = 0; i < 3; ++i) if (!isfinite(p[i]) || fabsf(p[i]) > TFP_CORRECTION_MAX_COMPONENT) return false;
  return true;
}
static inline bool tfp_correction_within(const float *a, const float *b) {
  double distance = 0;
  for (unsigned i = 0; i < 3; ++i) { const double d = (double)a[i]-b[i]; distance += d*d; }
  return distance <= (double)TFP_CORRECTION_MAX_RESIDUAL*TFP_CORRECTION_MAX_RESIDUAL;
}
static inline double tfp_ease(double x) { return x*x*(3-2*x); }
static inline bool tfp_weapon_valid(uint32_t weapon) { return weapon != 0 && weapon != UINT32_MAX; }
static inline void tfp_copy3(const float *source, float *destination) { for (unsigned i = 0; i < 3; ++i) destination[i] = source[i]; }
static inline bool tfp_correction_remember(TfpCorrection *s, const float *relative, uint32_t weapon, double tick) {
  if (!tfp_correction_finite(relative) || !tfp_weapon_valid(weapon) || !isfinite(tick) || tick < 0) { tfp_correction_reset(s); return false; }
  if (s->observed && tick < s->last_tick) { tfp_correction_reset(s); return false; }
  /* A different firearm starts a new record; nothing of the previous one follows it. */
  tfp_correction_reset(s);
  s->remembered = s->observed = true;
  s->current_weapon = weapon;
  s->native_tick = s->last_tick = tick;
  tfp_copy3(relative, s->native_relative);
  return true;
}
/* True: out holds a valid fresh desired position, corrected or passed through. */
static inline bool tfp_correction_apply(TfpCorrection *s, const float *desired_relative, uint32_t weapon, double tick,
                                        float *out, float duration_ms /*150*/, uint32_t max_gap_ms /*250*/) {
  const bool valid = tfp_correction_finite(desired_relative);
  if (out && valid) tfp_copy3(desired_relative, out);
  if (!out || !valid || !tfp_weapon_valid(weapon) || !isfinite(tick) || tick < 0 || !isfinite(duration_ms) || duration_ms < 0 ||
      duration_ms > 500 || max_gap_ms < 16 || max_gap_ms > 1000) { tfp_correction_reset(s); return false; }
  if (duration_ms == 0) { tfp_correction_reset(s); return true; }
  if (s->observed && (weapon != s->current_weapon || tick < s->last_tick || tick-s->last_tick > max_gap_ms)) { tfp_correction_reset(s); return true; }
  if (s->remembered) {
    if (tick < s->native_tick || tick-s->native_tick > max_gap_ms || !tfp_correction_within(s->native_relative, desired_relative)) {
      tfp_correction_reset(s); return true;
    }
    s->remembered = false; s->active = true; s->started = tick; s->used = 0; s->progress = 0; s->handoff_duration_ms = duration_ms;
    tfp_copy3(s->native_relative, s->last_presented);
  }
  if (!s->active) return true;
  if (!tfp_correction_within(s->last_presented, desired_relative)) { tfp_correction_reset(s); return true; }
  const double fraction = fmin(1.0, (tick-s->started)/s->handoff_duration_ms);
  s->progress = (float)fraction; s->last_tick = tick; s->observed = true;
  if (fraction >= 1) { s->active = false; s->used = 1; tfp_copy3(desired_relative, s->last_presented); return true; }
  const double current = tfp_ease(fraction);
  const double step = fmax(0.0, fmin(1.0, (current-s->used)/fmax(1e-12, 1-s->used)));
  /* Only the correction is carried, rebased from the last actual presentation. */
  if (step == 0) tfp_copy3(s->last_presented, out);
  else for (unsigned i = 0; i < 3; ++i) out[i] = (float)(s->last_presented[i]+(desired_relative[i]-s->last_presented[i])*step);
  tfp_copy3(out, s->last_presented); s->used = current; return true;
}

/* ---- camera_handoff.h: opt-in eases (both off in the shipped profile) and the rotation proof ---- */
typedef struct TfpEase { bool active; double started, last, used; float duration; } TfpEase;
static inline void tfp_ease_reset(TfpEase *e) { memset(e, 0, sizeof *e); }
static inline bool tfp_ease_begin(TfpEase *e, double tick, float duration_ms) {
  tfp_ease_reset(e);
  if (!isfinite(tick) || tick < 0 || !isfinite(duration_ms) || duration_ms <= 0 || duration_ms > 500) return false;
  e->active = true; e->started = e->last = tick; e->duration = duration_ms; return true;
}
/* Fraction of the remaining distance to spend on this frame; 1 = arrived. */
static inline double tfp_ease_step(TfpEase *e, double tick, uint32_t max_gap_ms) {
  if (!e->active) return 1;
  if (!isfinite(tick) || tick < e->last || tick-e->last > max_gap_ms) { tfp_ease_reset(e); return 1; }
  e->last = tick;
  const double fraction = fmin(1.0, (tick-e->started)/e->duration);
  if (fraction >= 1) { tfp_ease_reset(e); return 1; }
  const double current = fraction*fraction*(3-2*fraction);
  const double part = fmax(0.0, fmin(1.0, (current-e->used)/fmax(1e-12, 1-e->used)));
  e->used = current; return part;
}
static inline bool tfp_finite3(const float *p) { return p && isfinite(p[0]) && isfinite(p[1]) && isfinite(p[2]); }
typedef struct TfpHandoffPosition { TfpEase ease; float last[3]; } TfpHandoffPosition;
static inline bool tfp_handoff_position_begin(TfpHandoffPosition *s, const float *from, double tick, float duration_ms) {
  if (!tfp_finite3(from)) { tfp_ease_reset(&s->ease); return false; }
  if (!tfp_ease_begin(&s->ease, tick, duration_ms)) return false;
  for (unsigned i = 0; i < 3; ++i) s->last[i] = from[i];
  return true;
}
/* True: out holds an eased position. False: the ease is over or was dropped; out holds the target. */
static inline bool tfp_handoff_position_apply(TfpHandoffPosition *s, const float *desired, double tick, uint32_t max_gap_ms,
                                              float *out, float max_residual /*1.0*/) {
  if (!tfp_finite3(desired)) { tfp_ease_reset(&s->ease); return false; }
  for (unsigned i = 0; i < 3; ++i) out[i] = desired[i];
  if (!s->ease.active) return false;
  double distance = 0;
  for (unsigned i = 0; i < 3; ++i) { const double d = (double)s->last[i]-desired[i]; distance += d*d; }
  if (distance > (double)max_residual*max_residual) { tfp_ease_reset(&s->ease); return false; }
  const double part = tfp_ease_step(&s->ease, tick, max_gap_ms);
  if (part >= 1) return false;
  for (unsigned i = 0; i < 3; ++i) out[i] = (float)(s->last[i]+(desired[i]-s->last[i])*part);
  for (unsigned i = 0; i < 3; ++i) s->last[i] = out[i];
  return true;
}
static inline float tfp_q_dot(TfpQ a, TfpQ b) { return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w; }
static inline float tfp_degrees_between(TfpQ a, TfpQ b) {
  return 2*acosf(fminf(1.0f, fabsf(tfp_q_dot(a, b))))*180/TFP_PI;
}
static inline TfpQ tfp_q_normalised(TfpQ q) {
  const float n = sqrtf(tfp_q_dot(q, q));
  if (!(n > 0)) return tfp_q_identity();
  return tfp_q(q.x/n, q.y/n, q.z/n, q.w/n);
}
/* Shortest-arc interpolation; t=0 gives a, t=1 gives b (with b's sign). */
static inline TfpQ tfp_slerp(TfpQ a, TfpQ b, float t) {
  float d = tfp_q_dot(a, b);
  if (d < 0) { a = tfp_q(-a.x, -a.y, -a.z, -a.w); d = -d; }
  if (d > .9995f) return tfp_q_normalised(tfp_q(a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t, a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t));
  const float angle = acosf(fminf(1.0f, d)), s = sinf(angle);
  const float wa = sinf((1-t)*angle)/s, wb = sinf(t*angle)/s;
  return tfp_q_normalised(tfp_q(a.x*wa+b.x*wb, a.y*wa+b.y*wb, a.z*wa+b.z*wb, a.w*wa+b.w*wb));
}
typedef struct TfpHandoffRotation { TfpEase ease; TfpQ last; } TfpHandoffRotation;
static inline bool tfp_handoff_rotation_begin(TfpHandoffRotation *s, TfpQ from, double tick, float duration_ms) {
  if (!tfp_q_valid(from)) { tfp_ease_reset(&s->ease); return false; }
  if (!tfp_ease_begin(&s->ease, tick, duration_ms)) return false;
  s->last = tfp_q_normalised(from);
  return true;
}
/* Same contract as the position ease. A turn larger than max_degrees is another view: presented at once. */
static inline bool tfp_handoff_rotation_apply(TfpHandoffRotation *s, TfpQ desired, double tick, uint32_t max_gap_ms,
                                              TfpQ *out, float max_degrees /*120*/) {
  *out = desired;
  if (!tfp_q_valid(desired)) { tfp_ease_reset(&s->ease); return false; }
  if (!s->ease.active) return false;
  if (tfp_degrees_between(s->last, desired) > max_degrees) { tfp_ease_reset(&s->ease); return false; }
  const double part = tfp_ease_step(&s->ease, tick, max_gap_ms);
  if (part >= 1) return false;
  *out = tfp_slerp(s->last, desired, (float)part);
  if (!tfp_q_valid(*out)) { *out = desired; tfp_ease_reset(&s->ease); return false; }
  s->last = *out;
  return true;
}
/* The setter's rotation argument is proven to be the camera's world rotation before it is ever rewritten. */
#define TFP_ROTATION_PROOF_REQUIRED 12u
typedef struct TfpRotationProof { uint32_t matches; uint64_t mismatches; bool proven; } TfpRotationProof;
static inline void tfp_rotation_proof_sample(TfpRotationProof *p, TfpQ forwarded, TfpQ rendered) {
  if (!tfp_q_valid(forwarded) || !tfp_q_valid(rendered)) return;
  if (fabsf(forwarded.w) > .98f) return;
  if (tfp_degrees_between(forwarded, rendered) <= .25f) { if (++p->matches >= TFP_ROTATION_PROOF_REQUIRED) p->proven = true; }
  else { ++p->mismatches; p->matches = 0; p->proven = false; }
}
#endif
