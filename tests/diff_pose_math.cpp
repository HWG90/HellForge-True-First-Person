// Differential test: the retail arithmetic headers of the editor/pose helper (presentation_phase.h,
// zoom_input_math.h, zoom_gain_math.h) and src/pose_math.h on the same inputs, every result compared bit
// for bit. Built only where the retail source is present (tests/run-diff-pose.sh). The retail tree is
// read, never written.
#include "presentation_phase.h"
#include "zoom_gain_math.h"
extern "C" {
#include "pose_math.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

static unsigned long long checks;
static const char *stage = "";
template <class A, class B> static void same(const A &a, const B &b, const char *what, unsigned long long step) {
  static_assert(sizeof(A) == sizeof(B), "compared values differ in size");
  ++checks;
  if (std::memcmp(&a, &b, sizeof a)) {
    std::fprintf(stderr, "pose math differs from retail: %s, %s at step %llu\n", stage, what, step);
    std::exit(1);
  }
}
static bool sameb(bool a, bool b, const char *what, unsigned long long step) { same(a, b, what, step); return a; }

static_assert(sizeof(ZoomGainMath::Input) == sizeof(TfpGainInput) && offsetof(ZoomGainMath::Input, dt_global) == offsetof(TfpGainInput, dt_global) &&
              offsetof(ZoomGainMath::Input, pitch_gate) == offsetof(TfpGainInput, pitch_gate) && offsetof(ZoomGainMath::Input, device) == offsetof(TfpGainInput, device),
              "the gain input has one layout");
static_assert(sizeof(ZoomGainMath::Prepared) == sizeof(TfpGainPrepared) && sizeof(ZoomGainMath::Axis) == sizeof(TfpGainAxis) &&
              sizeof(ZoomGainMath::Pipeline) == sizeof(TfpGainPipeline) && sizeof(PresentationPhase::State) == sizeof(TfpPhase), "one size");
static_assert(TFP_ZOOM_RAD_TO_DEG == ZoomInputMath::RAD_TO_DEG && TFP_ZOOM_MIN_RATIO == ZoomInputMath::MIN_RATIO && TFP_ZOOM_MAX_RATIO == ZoomInputMath::MAX_RATIO &&
              TFP_ZOOM_MAX_COMPONENT == ZoomInputMath::MAX_COMPONENT && TFP_GAIN_MIN_GAIN == ZoomGainMath::MIN_GAIN && TFP_GAIN_MAX_GAIN == ZoomGainMath::MAX_GAIN &&
              TFP_GAIN_MIN_GAIN_RATIO == ZoomGainMath::MIN_GAIN_RATIO && TFP_GAIN_MAX_GAIN_RATIO == ZoomGainMath::MAX_GAIN_RATIO &&
              TFP_GAIN_MIN_CONDITION == ZoomGainMath::MIN_CONDITION && TFP_GAIN_ABS_TOLERANCE == ZoomGainMath::ABS_TOLERANCE &&
              TFP_GAIN_REL_TOLERANCE == ZoomGainMath::REL_TOLERANCE && TFP_GAIN_MAX_INCREMENT_ERROR == ZoomGainMath::MAX_INCREMENT_ERROR &&
              TFP_GAIN_PITCH_ADD_BOUND == ZoomGainMath::PITCH_ADD_BOUND, "the constants are retail's");

static void same_pipeline(const ZoomGainMath::Pipeline &a, const TfpGainPipeline &b, unsigned long long step) {
  same(a.before_gate, b.before_gate, "pipeline before_gate", step); same(a.after_gate, b.after_gate, "pipeline after_gate", step);
  same(a.increment, b.increment, "pipeline increment", step); sameb(a.gate_zero, b.gate_zero, "pipeline gate_zero", step);
}
static void same_prepared(const ZoomGainMath::Prepared &a, const TfpGainPrepared &b, unsigned long long step) {
  same(a.gains, b.gains, "gains", step); same(a.native_history, b.native_history, "native_history", step);
  same(a.ratio, b.ratio, "ratio", step); same(a.alpha, b.alpha, "alpha", step); same(a.beta, b.beta, "beta", step);
  same(a.mxcsr, b.mxcsr, "mxcsr", step); sameb(a.changed, b.changed, "changed", step); sameb(a.input_dependent, b.input_dependent, "input_dependent", step);
  for (int i = 0; i < 2; ++i) {
    same(a.axis[i].original_gain, b.axis[i].original_gain, "axis original_gain", step); same(a.axis[i].gain, b.axis[i].gain, "axis gain", step);
    same(a.axis[i].x, b.axis[i].x, "axis x", step); same(a.axis[i].target, b.axis[i].target, "axis target", step);
    same_pipeline(a.axis[i].natural, b.axis[i].natural, step); same_pipeline(a.axis[i].wanted, b.axis[i].wanted, step); same_pipeline(a.axis[i].expected, b.axis[i].expected, step);
    sameb(a.axis[i].input_dependent, b.axis[i].input_dependent, "axis input_dependent", step);
  }
}
// The exp the game would supply, in the ways it can misbehave.
static int exp_mode = 0;
static unsigned exp_calls[2];
static int exp_side = 0;
static double test_exp(double x) {
  ++exp_calls[exp_side];
  switch (exp_mode) {
  case 1: return NAN;
  case 2: return 1.5;
  case 3: return -0.25;
  case 4: _mm_setcsr(_mm_getcsr() | 0x8000u); return std::exp(x); // leaves flush-to-zero set
  case 5: _mm_setcsr(_mm_getcsr() | 0x20u); return std::exp(x);   // leaves only an exception flag set
  case 6: return 1.0;
  case 7: return 0.0;
  case 8: return INFINITY;
  default: return std::exp(x);
  }
}

int main() {
  std::mt19937 rng(25480438);
  std::uniform_real_distribution<float> unit(-1, 1), wide(-20, 20), positive(0, 1);
  std::uniform_int_distribution<int> pick(0, 999);
  std::uniform_int_distribution<uint32_t> any;
  auto special = [&](float v) {
    const int p = pick(rng);
    if (p < 4) return NAN;
    if (p < 8) return p & 1 ? INFINITY : -INFINITY;
    if (p < 14) return p & 1 ? 0.0f : -0.0f;
    if (p < 18) return 1e-41f * (p & 1 ? 1 : -1); // subnormal
    if (p < 22) { float f; const uint32_t bits = any(rng); std::memcpy(&f, &bits, 4); return f; }
    if (p < 26) return v * 1e6f;
    return v;
  };
  const uint32_t start_csr = _mm_getcsr();

  // The scalar helpers.
  stage = "scalar helpers";
  for (unsigned long long i = 0; i < 1500000; ++i) {
    const float a = special(wide(rng)), b = special(wide(rng)), c = special(unit(rng)), d = special(unit(rng));
    same(ZoomInputMath::bits(a), tfp_zoom_bits(a), "bits", i);
    sameb(ZoomInputMath::ordinary(a), tfp_zoom_ordinary(a), "ordinary", i);
    sameb(ZoomInputMath::bounded(a), tfp_zoom_bounded(a), "bounded", i);
    same(ZoomInputMath::mul(a, b), tfp_zoom_mul(a, b), "mul", i);
    same(ZoomInputMath::add(a, b), tfp_zoom_add(a, b), "add", i);
    same(ZoomInputMath::sub(a, b), tfp_zoom_sub(a, b), "sub", i);
    same(ZoomInputMath::divide(a, b), tfp_zoom_divide(a, b), "divide", i);
    const double wide_value = static_cast<double>(a) * static_cast<double>(b) * (pick(rng) < 20 ? 1e30 : 1.0);
    same(ZoomInputMath::narrow(wide_value), tfp_zoom_narrow(wide_value), "narrow", i);
    same(ZoomInputMath::filtered(a, b, c, d), tfp_zoom_filtered(a, b, c, d), "filtered", i);
    same(ZoomInputMath::neighbour(a, false), tfp_zoom_neighbour(a, false), "neighbour down", i);
    same(ZoomInputMath::neighbour(a, true), tfp_zoom_neighbour(a, true), "neighbour up", i);
    sameb(ZoomGainMath::gain_valid(a, b), tfp_gain_valid(a, b), "gain_valid", i);
    sameb(ZoomGainMath::gain_valid(std::fabs(a), std::fabs(b)), tfp_gain_valid(std::fabs(a), std::fabs(b)), "gain_valid positive", i);
    same(ZoomGainMath::pitch_after_base(a, c), tfp_gain_pitch_after_base(a, c), "pitch_after_base", i);
    same(PresentationPhase::eased(c), tfp_phase_eased(c), "eased", i);
    same(std::max(a, b), tfp_std_maxf(a, b), "std::max", i);
    same(std::min(a, b), tfp_std_minf(a, b), "std::min", i);
    for (uint32_t r = 0; r < 12; ++r) sameb(ZoomGainMath::accepted(static_cast<ZoomGainMath::Reason>(r)), tfp_gain_accepted(r), "accepted", i);
    ZoomGainMath::Pipeline pa{}; TfpGainPipeline pb; std::memset(&pb, 0x5a, sizeof pb);
    const unsigned axis = static_cast<unsigned>(pick(rng) & 1); const bool gate = pick(rng) < 300;
    const float dt_eval = pick(rng) < 700 ? positive(rng) * .05f : special(unit(rng)), dt_global = pick(rng) < 900 ? positive(rng) * .05f : special(unit(rng));
    const float gain = pick(rng) < 800 ? positive(rng) * 2 + .001f : special(wide(rng));
    sameb(ZoomGainMath::pipeline(axis, a, gain, c, dt_eval, dt_global, gate, pa), tfp_gain_pipeline(axis, a, gain, c, dt_eval, dt_global, gate, &pb), "pipeline result", i);
    same_pipeline(pa, pb, i);
  }

  // The published presentation phase: long random walks through publish / matches / step / reset.
  stage = "presentation phase";
  for (unsigned run = 0; run < 3000; ++run) {
    PresentationPhase::State a; TfpPhase b; std::memset(&b, 0x5a, sizeof b); tfp_phase_reset(&b);
    uint64_t epoch = 1, now = 1000; float progress = 0;
    for (unsigned long long i = 0; i < 300; ++i) {
      const int p = pick(rng);
      now += p < 30 ? 0 : p < 60 ? 400 : p < 80 ? static_cast<uint64_t>(-5) : 4 + p % 30;
      if (p % 7 == 0) { ++epoch; progress = 0; } else progress += positive(rng) * .2f;
      const uint64_t e = p < 15 ? 0 : p < 40 ? epoch - 1 : p < 50 ? epoch + 3 : epoch;
      const float pr = p >= 100 && p < 130 ? special(progress) : p >= 130 && p < 150 ? progress - .3f : p >= 150 && p < 170 ? 1.0f : progress > 1 ? 1.0f : progress;
      const uint32_t ads = p >= 200 && p < 215 ? 2u : p >= 215 && p < 400 ? 1u : 0u;
      const uint64_t stamp = p >= 400 && p < 450 ? 0 : now;
      const bool actual = (pick(rng) & 3) == 0;
      switch (pick(rng) % 5) {
      case 0: case 1: sameb(a.publish(e, pr, ads, stamp), tfp_phase_publish(&b, e, pr, ads, stamp), "publish", i); if (e > epoch) epoch = e; break;
      case 2: sameb(a.matches(actual, stamp), tfp_phase_matches(&b, actual, stamp), "matches", i);
              sameb(a.matches(actual), tfp_phase_matches(&b, actual, 0), "matches (no clock)", i); break;
      case 3: same(a.step(actual), tfp_phase_step(&b, actual), "step", i); break;
      default: if (pick(rng) < 60) { a.reset(); tfp_phase_reset(&b); } break;
      }
      same(a.epoch, b.epoch, "epoch", i); same(a.used_epoch, b.used_epoch, "used_epoch", i); same(a.published_at, b.published_at, "published_at", i);
      same(a.progress, b.progress, "progress", i); same(a.used, b.used, "used", i); sameb(a.ads, b.ads, "ads", i);
    }
  }

  // The look-gain preparation: realistic inputs with every field sometimes out of range, the exp misbehaving,
  // and the floating-point environment set in each way the routine distinguishes.
  stage = "gain preparation";
  unsigned long long reasons[12] = {};
  for (unsigned long long i = 0; i < 1500000; ++i) {
    ZoomGainMath::Input in{};
    const int kind = pick(rng);
    auto maybe = [&](float v) { return kind < 60 ? special(v) : v; };
    for (float &v : in.raw) v = maybe(unit(rng) * (pick(rng) < 100 ? 0.0f : 1.0f));
    for (float &v : in.history) v = maybe(unit(rng) * (pick(rng) < 100 ? 0.0f : 1.0f));
    const float rates[] = {0.0f, 1.0f / 30, 1.0f / 60, 1.0f / 144, 1.0f / 240, -.01f, 1.0f, 1.5f};
    in.dt_eval = maybe(pick(rng) < 800 ? rates[pick(rng) % 5] : rates[pick(rng) % 8]);
    in.dt_global = maybe(pick(rng) < 900 ? rates[pick(rng) % 5] : rates[pick(rng) % 8]);
    in.lens_radians = maybe(pick(rng) < 950 ? .05f + positive(rng) * 1.6f : wide(rng));
    in.tpv_degrees = maybe(pick(rng) < 950 ? 50 + positive(rng) * 70 : wide(rng) * 10);
    in.device = maybe(pick(rng) < 950 ? positive(rng) * 3 : wide(rng) * 1e5f);
    for (float &v : in.sign) v = pick(rng) < 970 ? (pick(rng) & 1 ? 1.0f : -1.0f) : maybe(unit(rng));
    for (float &v : in.steering) v = pick(rng) < 500 ? 0.0f : maybe(unit(rng) * (pick(rng) < 100 ? 1e-6f : .2f));
    for (float &v : in.context) v = pick(rng) < 500 ? 0.0f : maybe(unit(rng) * .01f);
    for (float &v : in.native_gain) v = pick(rng) < 950 ? .01f + positive(rng) * 2 : maybe(wide(rng) * (pick(rng) & 1 ? 1e-7f : 1e5f));
    in.pitch_gate = pick(rng) < 200;
    if (kind >= 60 && kind < 90) { in.lens_radians = in.tpv_degrees / ZoomInputMath::RAD_TO_DEG; } // ratio about 1
    if (kind >= 90 && kind < 110) { in.raw[0] = in.history[0] = 0; in.steering[0] = 0; }
    if (kind >= 110 && kind < 130) { in.steering[1] = -in.device * in.sign[1] * in.history[1]; } // near cancellation
    TfpGainInput cin; std::memcpy(&cin, &in, sizeof cin);
    exp_mode = pick(rng) < 60 ? pick(rng) % 9 : 0;
    const int environment = pick(rng);
    uint32_t csr = start_csr;
    if (environment < 30) csr |= 0x8000u;          // flush to zero
    else if (environment < 60) csr |= 0x0040u;     // denormals are zero
    else if (environment < 75) csr |= 0x2000u;     // rounding down
    else if (environment < 90) csr |= 0x6000u;     // rounding towards zero
    else if (environment < 105) csr &= ~0x0800u;   // one exception unmasked (precision): refused before any arithmetic
    else if (environment < 120) csr |= 0x003fu;    // exception flags already set
    const bool null_exp = pick(rng) < 20;
    ZoomGainMath::Prepared a{}; TfpGainPrepared b; std::memset(&b, 0x5a, sizeof b);
    _mm_setcsr(csr); exp_side = 0;
    const uint32_t ra = static_cast<uint32_t>(ZoomGainMath::prepare(in, null_exp ? nullptr : test_exp, a));
    const uint32_t after_a = _mm_getcsr();
    _mm_setcsr(csr); exp_side = 1;
    const uint32_t rb = tfp_gain_prepare(&cin, null_exp ? nullptr : test_exp, &b);
    const uint32_t after_b = _mm_getcsr();
    _mm_setcsr(start_csr);
    same(ra, rb, "prepare result", i); same(after_a, after_b, "MXCSR after prepare", i);
    same(exp_calls[0], exp_calls[1], "calls of exp", i);
    same_prepared(a, b, i);
    if (std::memcmp(&cin, &in, sizeof cin)) { std::fprintf(stderr, "pose math: prepare changed its input at step %llu\n", i); return 1; }
    if (ra < 12) ++reasons[ra];
  }
  for (int r = 0; r < 12; ++r) if (!reasons[r] && r != 4) { std::fprintf(stderr, "pose math: the inputs never produced reason %d; the test does not cover it\n", r); return 1; }
  if (!reasons[4]) { std::fprintf(stderr, "pose math: no input reached the missing-exp refusal\n"); return 1; }
  std::printf("Pose math: %llu results identical, bit for bit, to the retail headers (SSE scalar helpers, gain pipeline, presentation phase, look-gain preparation; "
              "reasons ready %llu, identity %llu, invalid %llu, environment %llu, no exp %llu, coefficient %llu, singular %llu, ill-conditioned %llu, range %llu, "
              "unrepresentable %llu, gate %llu, error %llu)\n", checks, reasons[0], reasons[1], reasons[2], reasons[3], reasons[4], reasons[5], reasons[6], reasons[7],
              reasons[8], reasons[9], reasons[10], reasons[11]);
  return 0;
}
