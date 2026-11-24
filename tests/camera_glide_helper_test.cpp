// The swap's eye glide through the camera helper (src/camera_glide.h as src/camera_helper.c uses it), on the fake game
// memory of tests/camera_smooth_test.cpp, whose scene and helpers these are (its own checks are not run here).
#define main camera_smooth_main
#include "camera_smooth_test.cpp"
#undef main

// A swap back to a weapon whose remembered eye is `way` metres behind the eye of the one in the hands, standing, at
// 33.3 ms a frame, the continuity off (the glide is judged raw). `moved`: how far the drawn eye went in each of the
// swap's first 12 head-camera frames, in the body's frame (the first of them holds the native view's eye; the glide
// begins in the second); returns how far it is from the remembered eye after them. `back_at`, when above 0: in that
// frame the swap goes back to the weapon that was in the hands, and there is no eye to glide to any more.
static float swap_back(bool lock, float way, float *moved, int back_at = 0) {
  fresh(lock, false);
  Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
  const uint32_t first = 0x1111, second = 0x2222;
  CHECK(tfp_camera_set_weapon_key(g.gen, first) == 1);
  standing(g, 12, 33.3, false); standing(g, 20, 33.3, true);
  float remembered[3]; std::memcpy(remembered, g.body, sizeof remembered);
  standing(g, 6, 33.3, false, 2); standing(g, 20, 33.3, true);      // out and back: the first weapon's eye is remembered
  standing(g, 3, 33.3, false, 2);
  CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1);
  g.eye[1] += way; standing(g, 20, 33.3, true);                     // the second weapon in the hands, its eye `way` ahead
  CHECK(tfp_camera_set_weapon_key(g.gen, first) == 1);
  float last[3]; std::memcpy(last, g.body, sizeof last);
  for (int i = 0; i < 12; ++i) {
    if (back_at && i == back_at) CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1);
    g.hip(2); g.frame(33.3);
    float square = 0; for (int k = 0; k < 3; ++k) square += (g.body[k] - last[k]) * (g.body[k] - last[k]);
    moved[i] = std::sqrt(square); std::memcpy(last, g.body, sizeof last);
  }
  float square = 0; for (int k = 0; k < 3; ++k) square += (g.body[k] - remembered[k]) * (g.body[k] - remembered[k]);
  g.end();
  return std::sqrt(square);
}

// ---- The held eye let go (round 19). ----
// The judge's limit on a frame's change of step: 0.06 m at 30 frames a second. A change of step is a rate of change of
// speed times the frame's time squared, so 0.06 m there is 54 m/s^2; at other frame times the limit is that rate's
// (dt/33.3 ms)^2 or, where it is less, 0.06 m in proportion to the frame time (the stricter of the two everywhere).
static float judge_limit(double dt) { const double r = dt / 33.3; return static_cast<float>(.06 * std::min(r, r * r)); }
struct Release {
  float largest_change = 0;   // the largest change of step of the drawn eye, as a vector, over the whole head-camera stretch
  double first_move_ms = -1;  // since the view left the native view: the first frame the drawn eye moved by more than 0.1 mm
  double still_ms = -1;       // the first frame from which on the drawn eye does not move any more
  bool toward = true;         // from the first move on, the drawn eye never went further from where it ends
  float held = 0;             // how far the held eye was from the head camera's own when it was let go
  float from_head_eye = 0;    // at the end
  float from_end[3] = {};     // where it ends, in the body's frame
};
// An emote: the rifle (`first`) up in the native view, its eye `way` behind the head camera's own (as the LAS-5 Scythe's
// is); the hands go empty (mask 0) and the view goes to the head camera for `ms`. The held eye is let go 900 ms after
// the hands went empty (TFP_ACTION_EYE_IDLE_MS, as the feel build sets it). `then`: at `then_ms` after the view left,
// 0: nothing; 1: the same rifle back in the hands (mask 2); 2: the sidearm (`second`, whose eye is remembered, 0.11 m
// in front of the head) comes up (mask 2). `long_hold`: the head camera holds mask 2 (a long swap) for 1700 ms first,
// so the hold lapses on its 2500 ms and is let go while the hands are full; `then` then comes after that.
static Release emote(bool lock, bool smooth, double dt, float way, int then = 0, double then_ms = 0, bool long_hold = false, float *sidearm = nullptr, float *rifle = nullptr) {
  fresh(lock, smooth);
  tfp_camera_set_action_tuning(0, 900, true);
  Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
  const uint32_t first = 0x1111, second = 0x2222;
  const int settle = static_cast<int>(400 / dt) + 1;
  CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1);
  standing(g, settle, dt, false); standing(g, settle, dt, true);
  if (sidearm) std::memcpy(sidearm, g.body, 12);
  standing(g, 3, dt, false, 2);                                  // the sidearm's eye is remembered as the view leaves
  CHECK(tfp_camera_set_weapon_key(g.gen, first) == 1);
  g.eye[1] = .17f - way; standing(g, settle, dt, true);          // the rifle in the hands, its eye `way` behind the head camera's
  if (rifle) std::memcpy(rifle, g.body, 12);
  Release r; float last[3], step[3] = {}, ends[3]; std::memcpy(last, g.body, 12);
  const int frames = static_cast<int>(3000 / dt);
  float at[400][3]; int count = 0; float held_before = g.from_head_eye();
  for (int i = 1; i <= frames && count < 400; ++i) {
    const double t = i * dt; uint32_t mask = long_hold && t <= 1700 ? 2 : 0;
    if (then && t >= then_ms) { mask = 2; if (then == 2) CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1); }
    g.hip(mask); g.frame(dt);
    float now[3], change[3];
    for (int k = 0; k < 3; ++k) { now[k] = g.body[k] - last[k]; change[k] = now[k] - step[k]; }
    const float moved = std::sqrt(now[0] * now[0] + now[1] * now[1] + now[2] * now[2]);
    r.largest_change = std::max(r.largest_change, std::sqrt(change[0] * change[0] + change[1] * change[1] + change[2] * change[2]));
    if (moved > 1e-4f && r.first_move_ms < 0) { r.first_move_ms = t; r.held = held_before; }
    held_before = g.from_head_eye();
    if (moved > 1e-5f) r.still_ms = -1; else if (r.still_ms < 0) r.still_ms = t;
    std::memcpy(at[count++], g.body, 12);
    std::memcpy(step, now, 12); std::memcpy(last, g.body, 12);
  }
  std::memcpy(ends, g.body, 12); std::memcpy(r.from_end, ends, 12);
  float before = 1e9f; bool started = false;
  for (int i = 0; i < count; ++i) {
    float d = 0; for (int k = 0; k < 3; ++k) d += (at[i][k] - ends[k]) * (at[i][k] - ends[k]);
    d = std::sqrt(d);
    if (i * dt + dt >= r.first_move_ms && r.first_move_ms >= 0) { if (started && d > before + 1e-5f) r.toward = false; started = true; }
    before = d;
  }
  r.from_head_eye = g.from_head_eye();
  if (std::getenv("TFP_RELEASE_DEBUG")) std::printf("  emote lock=%d smooth=%d dt=%.1f way=%.2f then=%d at %.0f long=%d: change %.4f (limit %.4f) first %.1f still %.1f toward %d held %.3f head %.4f\n",
      lock, smooth, dt, way, then, then_ms, long_hold, r.largest_change, judge_limit(dt), r.first_move_ms, r.still_ms, r.toward, r.held, r.from_head_eye);
  g.end();
  return r;
}
// How far `p` is from the triangle a, b, c (from the nearest of its sides where it has no area): 0 inside it.
static float outside_span(const float *p, const float *a, const float *b, const float *c) {
  auto sub = [](const float *x, const float *y, float *o) { for (int k = 0; k < 3; ++k) o[k] = x[k] - y[k]; };
  auto dot = [](const float *x, const float *y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; };
  auto to_side = [&](const float *x, const float *y) {
    float d[3], q[3]; sub(y, x, d); sub(p, x, q); const float l = dot(d, d);
    const float s = l > 0 ? std::min(1.0f, std::max(0.0f, dot(q, d) / l)) : 0;
    float r[3]; for (int k = 0; k < 3; ++k) r[k] = q[k] - s * d[k];
    return std::sqrt(dot(r, r));
  };
  const float sides = std::min(to_side(a, b), std::min(to_side(b, c), to_side(c, a)));
  float u[3], v[3], w[3], n[3]; sub(b, a, u); sub(c, a, v); sub(p, a, w);
  n[0] = u[1] * v[2] - u[2] * v[1]; n[1] = u[2] * v[0] - u[0] * v[2]; n[2] = u[0] * v[1] - u[1] * v[0];
  const float area = dot(n, n);
  if (!(area > 1e-10f)) return sides;
  // the foot of `p` on the plane, inside the triangle: the distance to the plane; else the nearest side
  const float uu = dot(u, u), uv = dot(u, v), vv = dot(v, v), wu = dot(w, u), wv = dot(w, v), den = uu * vv - uv * uv;
  const float s = (vv * wu - uv * wv) / den, t = (uu * wv - uv * wu) / den;
  if (s >= 0 && t >= 0 && s + t <= 1) return std::fabs(dot(w, n)) / std::sqrt(area);
  return sides;
}
// A long swap whose hold lapses while the swap's glide is under way (round 19 review: BLOCKING 1, IMPORTANT 2). The
// hands stay full (mask 2) from the view leaving the native view; the weapon identity changes `key` frames from the frame
// in which the hold lapses (its 2500 ms; negative: before it), from the weapon in the hands to the other, whose eye is
// remembered. `reverse` false: the rifle's eye is `way` behind the head camera's and the sidearm's (0.11 m ahead of it)
// is the incoming one; true: the other way round, the incoming eye `way` behind. Ways of 0.44 and 0.70 m are a glide
// of 0.55 and 0.81 m: retail's (GATE). Judged from the lapse frame on, over the next 1100 ms (the swap's glide, the
// release and the eye's return to the incoming weapon), against the same swap with no lapse near it (`alone`, recorded
// by `record`: the swap 10 frames into the hold; with the continuity on, a frame of the swap's glide alone can be over
// the judge's limit, the continuity taking it up as before round 19):
struct Lapse {
  float at_lapse = 0;      // the change of step of the drawn eye, as a vector, in the frame the hold lapses
  float largest = 0;       // the largest of those from that frame on
  float over_bound = 0;    // the most any of them is over the bound (the judge's limit, retail's own slowing, or that frame
                           // of the swap alone): <= 0 is within
  float outside = 0;       // the most the drawn eye is ever outside the span of the held eye, the incoming eye and the head
                           // camera's own eye (the triangle of the three), from the swap on
};
#define ALONE_FRAMES 160
static void body_of(const Game &g, const float *world, float *out) {
  for (int axis = 0; axis < 3; ++axis) { out[axis] = 0; for (int i = 0; i < 3; ++i) out[axis] += (world[i] - g.s.root[12 + i]) * g.s.root[axis * 4 + i]; }
}
static Lapse lapse_swap(bool smooth, double dt, float way, int key, bool reverse, const float *alone = nullptr, float *record = nullptr) {
  fresh(true, smooth);
  tfp_camera_set_action_tuning(0, 900, true);
  Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
  const uint32_t rifle = 0x1111, sidearm = 0x2222, held = reverse ? sidearm : rifle, coming = reverse ? rifle : sidearm;
  const int settle = static_cast<int>(400 / dt) + 1; const float near = g.eye[1], far = .17f - way;
  float span[3][3];                                               // the held eye, the incoming eye, the head camera's
  g.eye[1] = reverse ? far : near;
  CHECK(tfp_camera_set_weapon_key(g.gen, coming) == 1);
  standing(g, settle, dt, false); standing(g, settle, dt, true);
  std::memcpy(span[1], g.body, 12);
  standing(g, 3, dt, false, 2);                                   // the incoming weapon's eye is remembered as the view leaves
  CHECK(tfp_camera_set_weapon_key(g.gen, held) == 1);
  g.eye[1] = reverse ? near : far; standing(g, settle, dt, true);
  std::memcpy(span[0], g.body, 12);
  Lapse r; float last[3], step[3] = {}; std::memcpy(last, g.body, 12);
  int lapse = 1; while (!((lapse - 1) * dt > 2500)) ++lapse;     // the hold began in the first head-camera frame
  const int swap = record ? 10 : lapse + key, frames = (record ? swap : lapse) + static_cast<int>(1100 / dt);
  if (record) for (int j = 0; j < ALONE_FRAMES; ++j) record[j] = 0;
  for (int i = 1; i <= frames; ++i) {
    if (i == swap) CHECK(tfp_camera_set_weapon_key(g.gen, coming) == 1);
    g.hip(2); g.frame(dt);
    if (i == 1) { float head[3]; g.head_eye(head); body_of(g, head, span[2]); }
    float now[3], change[3];
    for (int k = 0; k < 3; ++k) { now[k] = g.body[k] - last[k]; change[k] = now[k] - step[k]; }
    const float changed = std::sqrt(change[0] * change[0] + change[1] * change[1] + change[2] * change[2]);
    const float had = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);
    if (i >= swap) r.outside = std::max(r.outside, outside_span(g.body, span[0], span[1], span[2]));
    if (record && i >= swap && i - swap < ALONE_FRAMES) { record[i - swap] = changed; r.largest = std::max(r.largest, changed); }
    if (i == lapse) r.at_lapse = changed;
    if (i >= lapse && !record) {
      r.largest = std::max(r.largest, changed);
      // the bound: the judge's limit, (the continuity off) what retail's own glide slows the eye by in the frame, or what
      // the swap's glide alone changes it by in that frame of it
      float bound = std::max(judge_limit(dt), smooth ? 0.0f : had * static_cast<float>(1 - std::exp(-dt / 70)) + 1e-4f);
      if (alone && i >= swap && i - swap < ALONE_FRAMES) bound = std::max(bound, alone[i - swap] + 1e-4f);
      r.over_bound = std::max(r.over_bound, changed - bound);
    }
    std::memcpy(step, now, 12); std::memcpy(last, g.body, 12);
  }
  if (std::getenv("TFP_LAPSE_TABLE")) std::printf("  %s swap %s way=%.2f dt=%4.1f smooth=%d key %+3d: at lapse %.4f largest %.4f over bound %+.4f outside %.4f\n",
      record ? "alone" : "lapse", reverse ? "reverse" : "forward", way, dt, smooth, key, r.at_lapse, r.largest, r.over_bound, r.outside);
  g.end();
  return r;
}
static float apart3(const float *a, const float *b) { float d = 0; for (int k = 0; k < 3; ++k) d += (a[k] - b[k]) * (a[k] - b[k]); return std::sqrt(d); }
// A take-over (a weapon drawn while the eye is let go, scenes 3 to 5 below) goes on at the release's rates, not the swap
// glide's 250 ms (src/camera_glide.h THE RELEASE): no frame's step changes by more than in the release of the same way at
// the same frame time, within this share (the take-over begins with the speed the eye has; 1.11 at most seen).
// On the swap glide's rates it is up to 2.4 times as hard, 1.5 to 1.9 at 33 and 50 ms a frame (review of round 19, M4).
#define TAKE_OVER 1.15f
static void hold_release() {
  const double frames[4] = {8, 16.7, 33.3, 50};
  for (int f = 0; f < 4; ++f) for (int smooth = 0; smooth < 2; ++smooth) {
    const double dt = frames[f]; float released = 0;
    // 1. The emote, the Scythe's 0.44 m: held for 900 ms, then let go from rest to rest over 300 to 450 ms, no frame's
    // step changing by more than the judge allows, at the head camera's own eye at the end and never further from it on
    // the way. (Before round 19: faded on 80 ms, the first frame 0.15 m at 33 ms a frame.)
    {
      const Release r = emote(true, smooth, dt, .44f);
      CHECK(r.held > .43f && r.held < .46f);
      CHECK(r.first_move_ms > 900 - dt && r.first_move_ms <= 900 + 2 * dt + .5);
      CHECK(r.largest_change <= judge_limit(dt));
      CHECK(r.still_ms - r.first_move_ms >= 300 - dt && r.still_ms - r.first_move_ms <= 450 + dt);
      CHECK(r.toward && r.from_head_eye < 1e-3f);
      released = r.largest_change;
    }
    // 2. A short way (0.10 m) is let go over 300 ms all the same: no faster than the long one.
    {
      const Release r = emote(true, smooth, dt, .10f);
      CHECK(r.largest_change <= judge_limit(dt) && r.still_ms - r.first_move_ms >= 300 - dt && r.from_head_eye < 1e-3f);
    }
    // 3. The same rifle back in the hands 150 ms into the letting go (the hold is 1050 ms old, under its 2500 ms): the
    // hold takes over and the eye comes to rest where it is, without a step. At 2500 ms the hold lapses with the hands
    // full, and the eye's return glides it back to the rifle's eye, from where it rests.
    {
      float rifle[3]; const Release r = emote(true, smooth, dt, .44f, 1, 1050, false, nullptr, rifle);
      CHECK(r.largest_change <= judge_limit(dt) && r.largest_change <= TAKE_OVER * released);
      CHECK(apart3(r.from_end, rifle) < 1e-3f);
    }
    // 4. The sidearm, whose eye is remembered, comes up 150 ms into the letting go: the glide to its eye takes over from
    // where the eye is and the way it is going, without a step, and ends there.
    {
      float sidearm[3]; const Release r = emote(true, smooth, dt, .44f, 2, 1050, false, sidearm);
      CHECK(r.largest_change <= judge_limit(dt) && r.largest_change <= TAKE_OVER * released);
      CHECK(apart3(r.from_end, sidearm) < 1e-3f);
    }
    // 5. A long swap: the hold lapses on its 2500 ms while the hands are full and the eye is let go; 150 ms into that the
    // sidearm comes up (the eye's return, TFP_ACTION_EYE_RETURN): it takes over without a step and ends at its eye.
    {
      float sidearm[3]; const Release r = emote(true, smooth, dt, .44f, 2, 2650, true, sidearm);
      CHECK(r.first_move_ms > 2500 - dt && r.first_move_ms <= 2500 + 2 * dt + .5);
      CHECK(r.largest_change <= judge_limit(dt) && r.largest_change <= TAKE_OVER * released);
      CHECK(apart3(r.from_end, sidearm) < 1e-3f);
    }
  }
  // 7. A long swap whose hold lapses while the swap's glide is under way: the weapon identity changes in every frame from
  // 10 before the frame the hold lapses in to 2 after it, both ways, 0.44 and 0.70 m, the continuity off and on, 8 to 50
  // ms a frame. The swap's glide runs to its end and the release begins from rest after it (src/camera_glide.h THE
  // LAPSE): the drawn eye never leaves the span of the held eye, the incoming eye and the head camera's own (within
  // 1 mm, or what the continuity does with the swap alone: 1.9 and 3.2 mm for 0.70 m at 16.7 ms), and from the lapse
  // on no frame's step changes by more than the judge allows, (the continuity off) retail's own glide slows it, or the
  // same frame of the swap alone. (Review of round 19: a release that took over retail's glide with its speed went up
  // to 0.29 m behind the incoming eye and 0.45 m outside the span (BLOCKING 1); one from rest stopped it dead, 0.08 m
  // of step change at 33 ms a frame; with the continuity on the lapse frame was 0.048 m at 33 ms, main 0.009
  // (IMPORTANT 2).)
  for (int f = 0; f < 4; ++f) for (int smooth = 0; smooth < 2; ++smooth) for (float way : {.44f, .70f}) for (int reverse = 0; reverse < 2; ++reverse)
    {
      float alone[ALONE_FRAMES]; const Lapse a = lapse_swap(smooth, frames[f], way, 0, reverse, nullptr, alone);
      for (int key = -10; key <= 2; ++key) {
        const Lapse r = lapse_swap(smooth, frames[f], way, key, reverse, alone);
        CHECK(r.outside <= std::max(1e-3f, a.outside + 1e-4f));
        CHECK(r.over_bound <= 0);
      }
    }
  // 6. Without the lock it is retail's: faded on 80 ms, its first frame the largest (0.15 m at 33 ms a frame).
  {
    const Release r = emote(false, false, 33.3, .44f);
    CHECK(r.largest_change > .14f && r.largest_change < .17f && r.still_ms - r.first_move_ms < 500);
  }
}

int main() {
  W.build(HERE); // the world, as camera_smooth_main sets it up
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  float retail[12], locked[12];
  // ---- 1. 0.2 m. Without the lock it is retail's glide: its first frame is its largest, 38 % of the way, and 1 mm
  // is left after 12 frames. Under the lock it begins from rest, no step changes by more than the slowing-down rate
  // gives (21 m/s^2: 0.023 m at this frame time), and it is at the remembered eye itself after 233 ms. ----
  {
    const float left_retail = swap_back(false, .2f, retail), left_locked = swap_back(true, .2f, locked);
    CHECK(retail[0] < 1e-4f && std::fabs(retail[1] - .2f * (1 - std::exp(-33.3f / 70))) < .002f && left_retail > .0005f && left_retail < .002f);
    for (int i = 2; i < 12; ++i) CHECK(retail[i] < retail[i - 1]);
    CHECK(locked[0] < 1e-4f && locked[1] > .005f && locked[1] < .009f && left_locked < 1e-4f);
    float largest = 0, travelled = 0; int top = 1;
    for (int i = 1; i < 12; ++i) { largest = std::max(largest, std::fabs(locked[i] - locked[i - 1])); travelled += locked[i]; if (locked[i] > locked[top]) top = i; }
    CHECK(largest < .025f && std::fabs(travelled - .2f) < .002f);
    for (int i = 2; i < 12; ++i) CHECK(i <= top ? locked[i] > locked[i - 1] : locked[i] <= locked[i - 1]);
    for (int i = 8; i < 12; ++i) CHECK(locked[i] < 1e-5f);
  }
  // ---- 2. 0.3 m is more than the lock's glide is for (GATE in src/camera_glide.h): retail's glide with the lock too,
  // frame for frame. ----
  {
    const float left_retail = swap_back(false, .3f, retail), left_locked = swap_back(true, .3f, locked);
    CHECK(std::fabs(retail[1] - .3f * (1 - std::exp(-33.3f / 70))) < .002f && std::fabs(left_locked - left_retail) < 1e-5f);
    for (int i = 0; i < 12; ++i) CHECK(std::fabs(locked[i] - retail[i]) < 1e-5f);
  }
  // ---- 3. The swap goes back to the weapon it came from, 100 ms into a glide of 0.2 m: no eye to glide to any more.
  // Without the lock the eye stops in that frame, as retail's does. Under the lock, where the eye is going 1.4 m/s by
  // then, it comes to rest over the next frames (0.07 s and 0.05 m). ----
  {
    swap_back(false, .2f, retail, 4); swap_back(true, .2f, locked, 4);
    for (int i = 4; i < 12; ++i) CHECK(retail[i] < 1e-5f);
    CHECK(locked[3] > .035f && locked[4] > .015f && locked[4] < locked[3] && locked[5] < locked[4]);
    float went = 0; for (int i = 4; i < 12; ++i) went += locked[i];
    CHECK(went > .03f && went < .06f);
    for (int i = 7; i < 12; ++i) CHECK(locked[i] < 1e-5f);
  }
  hold_release();
  std::printf("camera glide through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
