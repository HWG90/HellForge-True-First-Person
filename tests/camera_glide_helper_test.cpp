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
// A long swap whose hold lapses while a retail glide (GATE: a way of more than 0.2625 m) is under way: the rifle's eye
// `way` behind the head camera's, the sidearm's (remembered) 0.11 m ahead of it; the hands stay full (mask 2) from the
// view leaving the native view, and the weapon identity changes to the sidearm at `key_ms`, a little before the hold
// lapses on its 2500 ms. Returns the change of step of the drawn eye in the frame the hold lapses (the first in which
// the eye is let go) and, in `before`, the step the eye had in the frame before it.
static float lapse_mid_glide(bool smooth, double dt, float way, double key_ms, float *before) {
  fresh(true, smooth);
  tfp_camera_set_action_tuning(0, 900, true);
  Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
  const uint32_t first = 0x1111, second = 0x2222;
  const int settle = static_cast<int>(400 / dt) + 1;
  CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1);
  standing(g, settle, dt, false); standing(g, settle, dt, true);
  standing(g, 3, dt, false, 2);
  CHECK(tfp_camera_set_weapon_key(g.gen, first) == 1);
  g.eye[1] = .17f - way; standing(g, settle, dt, true);
  float last[3], step[3] = {}, at_lapse = -1; std::memcpy(last, g.body, 12); bool swapped = false;
  for (int i = 1; i * dt <= 3000; ++i) {
    const double t = i * dt;
    if (!swapped && t >= key_ms) { CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1); swapped = true; }
    g.hip(2); g.frame(dt);
    float now[3], change[3];
    for (int k = 0; k < 3; ++k) { now[k] = g.body[k] - last[k]; change[k] = now[k] - step[k]; }
    // the hold began in the first head-camera frame (t = dt) and lapses in the first frame more than 2500 ms after it
    if (at_lapse < 0 && t - dt > 2500) {
      at_lapse = std::sqrt(change[0] * change[0] + change[1] * change[1] + change[2] * change[2]);
      *before = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);
    }
    std::memcpy(step, now, 12); std::memcpy(last, g.body, 12);
  }
  if (std::getenv("TFP_RELEASE_DEBUG")) std::printf("  lapse mid-glide smooth=%d dt=%.1f way=%.2f key at %.0f: change %.4f, step before %.4f\n", smooth, dt, way, key_ms, at_lapse, *before);
  g.end();
  return at_lapse;
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
  // 7. A long swap to a weapon whose eye is far (0.55 and 0.81 m: retail's glide, GATE) changes the weapon just before
  // the hold lapses on its 2500 ms, so the hold lapses while that glide is under way. The release goes on from the speed
  // the eye has: in the frame it begins the eye's step changes by no more than retail's glide would have slowed it in
  // that frame (its step times 1-exp(-dt/70 ms)), judged raw (the continuity off); with the continuity on, within the
  // judge's limit. (Review of round 19, "B lapse": the release began from rest and the eye stopped dead in one frame,
  // 0.05 m of step change at 33 ms a frame, 0.09 m for 0.81 m, 0.03 m at 8 ms.)
  for (int f = 0; f < 4; ++f) for (int smooth = 0; smooth < 2; ++smooth) for (float way : {.44f, .70f}) for (double early : {1.5, 2.5}) {
    const double dt = frames[f]; float before = 0;
    const float change = lapse_mid_glide(smooth, dt, way, 2500 - early * dt, &before);
    if (smooth) CHECK(change >= 0 && change <= judge_limit(dt));
    else CHECK(before > .02f && change >= 0 && change <= before * (1 - std::exp(-dt / 70)));
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
