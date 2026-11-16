// Differential test: the retail headers and src/camera_math.h on the same
// inputs, results compared bit for bit. Built only where the retail source is
// present (see tests/run-native.sh). The retail tree is read, never written.
#include "free_aim_math.h"
#include "camera_correction.h"
#include "camera_handoff.h"
extern "C" {
#include "camera_math.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

static unsigned long long checks;
template <class A, class B> static void same(const A &a, const B &b, const char *what, unsigned long long step) {
  static_assert(sizeof(A) == sizeof(B), "compared values differ in size");
  ++checks;
  if (std::memcmp(&a, &b, sizeof a)) {
    std::fprintf(stderr, "camera math differs from retail: %s at step %llu\n", what, step);
    std::exit(1);
  }
}
static bool sameb(bool a, bool b, const char *what, unsigned long long step) { same(a, b, what, step); return a; }

int main() {
  std::mt19937 rng(25480438);
  std::uniform_real_distribution<float> unit(-1, 1), wide(-20, 20), angle(-3.2f, 3.2f);
  std::uniform_int_distribution<int> pick(0, 99);
  auto special = [&](float v) {
    int p = pick(rng);
    if (p == 0) return NAN;
    if (p == 1) return INFINITY;
    if (p == 2) return 0.0f;
    return v;
  };
  // Quaternion helpers and the matrix decomposition.
  for (unsigned long long i = 0; i < 200000; ++i) {
    FreeAim::Q a{special(unit(rng)), unit(rng), unit(rng), unit(rng)}, b{unit(rng), unit(rng), unit(rng), unit(rng)};
    if (pick(rng) < 80) { a = CameraHandoff::normalised(a); b = CameraHandoff::normalised(b); }
    TfpQ ca = tfp_q(a.x, a.y, a.z, a.w), cb = tfp_q(b.x, b.y, b.z, b.w);
    same(FreeAim::mul(a, b), tfp_q_mul(ca, cb), "mul", i);
    same(FreeAim::inv(a), tfp_q_inv(ca), "inv", i);
    sameb(FreeAim::valid(a), tfp_q_valid(ca), "valid", i);
    float v[3] = {wide(rng), wide(rng), wide(rng)}, r1[3], r2[3];
    FreeAim::rotate(a, v, r1); tfp_q_rotate(ca, v, r2);
    same(r1, r2, "rotate", i);
    same(CameraHandoff::dot(a, b), tfp_q_dot(ca, cb), "dot", i);
    same(CameraHandoff::degrees_between(a, b), tfp_degrees_between(ca, cb), "degrees_between", i);
    same(CameraHandoff::normalised(a), tfp_q_normalised(ca), "normalised", i);
    float t = (unit(rng) + 1) / 2;
    same(CameraHandoff::slerp(a, b, t), tfp_slerp(ca, cb, t), "slerp", i);
    // A rotation matrix from a, sometimes scaled or sheared out of tolerance.
    float m[16] = {};
    float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
    FreeAim::rotate(a, x, m); FreeAim::rotate(a, y, m + 4); FreeAim::rotate(a, z, m + 8);
    if (pick(rng) < 10) m[pick(rng) % 11] *= 1 + unit(rng) * .1f;
    if (pick(rng) < 3) m[pick(rng) % 11] = special(m[0]);
    FreeAim::Q q1{}; TfpQ q2 = tfp_q_identity();
    if (sameb(FreeAim::matrix_q(m, q1), tfp_matrix_q(m, &q2), "matrix_q result", i)) same(q1, q2, "matrix_q", i);
  }
  // The exit position correction: long random walks through remember / apply / reset.
  for (unsigned run = 0; run < 400; ++run) {
    CameraCorrection::State a; TfpCorrection b; tfp_correction_reset(&b);
    double tick = 1000;
    float target[3] = {unit(rng), unit(rng), unit(rng)};
    for (unsigned long long i = 0; i < 600; ++i) {
      int p = pick(rng);
      tick += p < 2 ? -5.0 : p < 5 ? 400.0 : p < 10 ? 0.0 : 4 + (p % 30);
      for (float &c : target) c += unit(rng) * (p == 11 ? 3.0f : .01f);
      float in[3] = {special(target[0]), target[1], target[2]};
      uint32_t weapon = p == 12 ? 0u : p == 13 ? UINT32_MAX : 7u + (p > 95);
      float duration = p == 14 ? 0.f : p == 15 ? 600.f : p == 16 ? NAN : 150.f;
      uint32_t gap = p == 17 ? 8u : p == 18 ? 2000u : 250u;
      if (p >= 20 && p < 28) {
        sameb(a.remember(in, weapon, tick), tfp_correction_remember(&b, in, weapon, tick), "remember", i);
      } else if (p == 19) {
        a.reset(); tfp_correction_reset(&b);
      } else {
        float o1[3] = {9, 9, 9}, o2[3] = {9, 9, 9};
        sameb(a.apply(in, weapon, tick, o1, duration, gap), tfp_correction_apply(&b, in, weapon, tick, o2, duration, gap), "apply", i);
        same(o1, o2, "apply output", i);
      }
      same(a.progress, b.progress, "progress", i);
      sameb(a.active, b.active, "active", i);
    }
  }
  // The two opt-in eases and the rotation proof.
  for (unsigned run = 0; run < 300; ++run) {
    CameraHandoff::Position pa; TfpHandoffPosition pb; std::memset(&pb, 0, sizeof pb);
    CameraHandoff::Rotation ra; TfpHandoffRotation rb; std::memset(&rb, 0, sizeof rb); rb.last = tfp_q_identity();
    CameraHandoff::Proof fa; TfpRotationProof fb; std::memset(&fb, 0, sizeof fb);
    double tick = 500;
    float at[3] = {unit(rng), unit(rng), unit(rng)};
    FreeAim::Q view = CameraHandoff::normalised({unit(rng), unit(rng), unit(rng), unit(rng)});
    for (unsigned long long i = 0; i < 400; ++i) {
      int p = pick(rng);
      tick += p < 2 ? -3.0 : p < 4 ? 500.0 : 2 + (p % 25);
      for (float &c : at) c += unit(rng) * (p == 5 ? 2.0f : .02f);
      FreeAim::Q turn = CameraHandoff::normalised({unit(rng) * (p == 6 ? 1.f : .02f), unit(rng) * .02f, unit(rng) * .02f, 1});
      view = CameraHandoff::normalised(FreeAim::mul(view, turn));
      float in[3] = {special(at[0]), at[1], at[2]};
      FreeAim::Q qin{special(view.x), view.y, view.z, view.w};
      TfpQ cq = tfp_q(qin.x, qin.y, qin.z, qin.w);
      float duration = p == 7 ? 0.f : p == 8 ? 501.f : 60.f + p;
      if (p >= 10 && p < 16) {
        sameb(pa.begin(in, tick, duration), tfp_handoff_position_begin(&pb, in, tick, duration), "position begin", i);
        sameb(ra.begin(qin, tick, duration), tfp_handoff_rotation_begin(&rb, cq, tick, duration), "rotation begin", i);
      } else {
        float o1[3] = {9, 9, 9}, o2[3] = {9, 9, 9};
        sameb(pa.apply(in, tick, 250, o1), tfp_handoff_position_apply(&pb, in, tick, 250, o2, 1.0f), "position apply", i);
        same(o1, o2, "position output", i);
        FreeAim::Q q1{}; TfpQ q2 = tfp_q_identity();
        sameb(ra.apply(qin, tick, 250, q1), tfp_handoff_rotation_apply(&rb, cq, tick, 250, &q2, 120.0f), "rotation apply", i);
        same(q1, q2, "rotation output", i);
      }
      sameb(pa.active(), pb.ease.active, "position active", i);
      sameb(ra.active(), rb.ease.active, "rotation active", i);
      FreeAim::Q other = pick(rng) < 70 ? qin : CameraHandoff::normalised({unit(rng), unit(rng), unit(rng), unit(rng)});
      fa.sample(qin, other); tfp_rotation_proof_sample(&fb, cq, tfp_q(other.x, other.y, other.z, other.w));
      same(fa.matches, fb.matches, "proof matches", i);
      same(fa.mismatches, fb.mismatches, "proof mismatches", i);
      sameb(fa.proven, fb.proven, "proof proven", i);
    }
  }
  std::printf("Camera math: %llu results identical, bit for bit, to the retail headers (quaternions, matrix rotation, exit correction, eases, rotation proof)\n", checks);
  return 0;
}
