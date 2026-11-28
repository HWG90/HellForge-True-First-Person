// Round 53: full aim shown wider than the game's lens, through whole frames of the two real helpers as the game calls
// them (src/fov_helper.c and src/near_helper.c, each on the differential test's fake game memory), bound as the live
// binder binds them (src/aim_bind.h tfp_aim_bind_pose, called by src/wire_win_pose.c). A frame is the game's two calls
// of its field-of-view setter on the first-person camera (the hooked call, then the forwarded one) and its call of the
// near setter; where in the frame the near call falls is not known offline, so every scene is run with it before,
// between and after the two. What is judged is the near clip handed to the game's own setter.
// The runtime's own frame (tfp_wire_frame) hands the helper the hip and normal-aim distances only; that side, and the
// "Clip:" line, are judged through whole tfp_wire_frame frames in tests/wire_settings_test.c.
// No retail source needed. Linux (process_vm_readv).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>
#include <vector>
#define _Static_assert static_assert
extern "C" {
#include "near_helper.h"
#include "fov_helper.h"
#include "fov_lock.h"
}
#include "diff_near_helper_world.h"
#include "clip_frames.h"
using namespace dt;

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; std::printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

static World W;
static Scene scene;
static bool built;
static uint64_t now_us = 1000000, gen;
static struct { float meters; bool seen; } sent;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static uint64_t host_now(void *) { return now_us; }
static bool host_true_slot(void *, uint64_t, uint64_t) { return true; }
static bool host_executable(void *, uint64_t) { return true; }
static bool host_cas(void *, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint64_t *>(slot), expected, desired); return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) { std::memcpy(out, data, length); return true; }
static bool host_pin(void *, uint64_t) { return true; }
static void host_lock(void *, int) {}
static void host_forward(void *, uint64_t, void *, float meters) { sent.seen = true; sent.meters = meters; }

// Both helpers armed on the first-person camera at the hip. `hip`, `aim`: the distances the runtime hands the near
// helper (m; 0: the game's own). `bound`: the live binding; without it the near helper is main's.
static void fresh(float hip, float aim, bool bound) {
  if (!built) { W.build(HERE); W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker); built = true; }
  else tfp_near_end(gen);
  scene = Scene{};
  World::put<uint64_t>(W.slot(), W.native_value);
  TfpNearHost host{};
  host.hook = W.hook_value; host.read = host_read; host.now_us = host_now; host.writable_slot = host_true_slot; host.executable = host_executable;
  host.cas = host_cas; host.write = host_write; host.pin = host_pin; host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock;
  host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_near_reset_for_tests();
  tfp_near_set_host(&host);
  TfpNearConfig c{};
  c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA];
  c.avatar_owner = W.a[A_OWNER]; c.avatar_index = 0; c.hip = hip; c.aim = aim;
  gen = 0;
  W.apply(scene, 0);
  if (tfp_near_begin(&c, &gen) != 1) { std::printf("the near helper did not arm (error %u)\n", tfp_near_error()); std::exit(2); }
  fovw_fresh();
  if (bound) clip_frames_bind(); else clip_frames_unbind();
  now_us += 50000000;
}
enum View { HIP, SHOULDER, ADS };
static void view(View v) {
  scene.controls[4] = v == ADS ? 1 : 0;
  scene.flags[10] = v == SHOULDER ? 4 : 0; scene.flags[15] = v == SHOULDER ? 32 : 0;
  fovw_aim(v == ADS);
}
static float near_call(float game_near) {
  sent.seen = false;
  W.apply(scene, 0);
  tfp_near_setter_from(W.caller(World::CALL_SITE), reinterpret_cast<void *>(W.a[A_CAMERA]), game_near);
  if (!sent.seen) { std::printf("the near hook did not forward\n"); std::exit(2); }
  return sent.meters;
}
// One frame of the game, 1/116 s: its lens `lens` degrees at both field-of-view calls, its near clip `game_near` m.
// `where`: the near call before (0), between (1) or after (2) the two field-of-view calls. *shown: the field of view
// the screen keeps (degrees). Returns the near clip the game's setter was handed.
static float frame(int where, float lens, float game_near, float *shown = nullptr) {
  const double ms = 1000.0 / 116, gap = 1.3;
  now_us += static_cast<uint64_t>(ms * 1000);
  float handed = 0;
  if (where == 0) handed = near_call(game_near);
  fovw_call(ms - gap, true, lens);
  if (where == 1) handed = near_call(game_near);
  const float kept = fovw_call(gap, false, lens);
  if (where == 2) handed = near_call(game_near);
  if (shown) *shown = kept;
  return handed;
}
static const float RAD = 0.01745329251994329577f;
static float scaled(float game_near, float lens, float shown) {
  const float n = game_near * std::tan(lens * RAD / 2) / std::tan(shown * RAD / 2);
  return (shown - lens) * RAD > .003f ? (n < .05f ? .05f : n) : game_near; // (.003 rad: the lock's glide ends there, src/weapon_clip.h)
}

// A scene: the hip for a second, full aim for two, the hip again. Returns every frame's near clip.
struct Take { std::vector<float> near_m, shown; int aim_from, aim_to; };
static Take take(int where, bool bound, float lock, float aim_field, float hip_setting, float hip_lens, float aim_lens, float aim_near, float hip_near = .05f) {
  Take t;
  fresh(hip_setting, 0, bound);
  fovw_lock(lock, aim_field);
  float shown = 0;
  view(HIP);
  for (int i = 0; i < 116; ++i) { t.near_m.push_back(frame(where, hip_lens, hip_near, &shown)); t.shown.push_back(shown); }
  t.aim_from = static_cast<int>(t.near_m.size());
  view(ADS);
  for (int i = 0; i < 232; ++i) { t.near_m.push_back(frame(where, aim_lens, aim_near, &shown)); t.shown.push_back(shown); }
  t.aim_to = static_cast<int>(t.near_m.size());
  view(HIP);
  for (int i = 0; i < 116; ++i) { t.near_m.push_back(frame(where, hip_lens, hip_near, &shown)); t.shown.push_back(shown); }
  return t;
}
static bool same(const Take &a, const Take &b, int from, int to) {
  for (int i = from; i < to; ++i) if (bits(a.near_m[i]) != bits(b.near_m[i])) return false;
  return true;
}

int main() {
  static const char *const places[] = {"before", "between", "after"};
  for (int where = 0; where < 3; ++where) {
    // ---- 1. "Aim field of view: Your locked view", the lock at 90: full aim is shown at 90 while the game's lens is
    // 30 and its near clip the sight's 0.098 m (the Adjudicator in the user's log). The near clip handed over is the
    // scaled one, 0.05 m (0.098 x tan 15 / tan 45 = 0.026, never under the game's own 0.05), from the first frames. ----
    {
      const Take t = take(where, true, 90, TFP_FOV_AIM_LOCKED, 0, 70, 30, .098f);
      const Take main_ = take(where, false, 90, TFP_FOV_AIM_LOCKED, 0, 70, 30, .098f);
      CHECK(std::fabs(t.shown[t.aim_from + 100] - 90) < .01f);
      bool all = true, bounded = true;
      for (int i = t.aim_from + 1; i < t.aim_to; ++i) if (t.near_m[i] != .05f) all = false;       // (the near call before the frame's field of view: a frame late)
      for (int i = t.aim_from; i < t.aim_to; ++i) if (t.near_m[i] < .05f || t.near_m[i] > .098f) bounded = false;
      CHECK(all); CHECK(bounded);
      if (where == 2) CHECK(t.near_m[t.aim_from] == .05f);                                        // in the frame full aim begins
      CHECK(bits(main_.near_m[t.aim_to - 1]) == bits(.098f));                                     // main: the game's 0.098 m, drawn at 90 degrees
      CHECK(same(t, main_, 0, t.aim_from));                                                       // the hip before: main's, to the bit
      for (int i = t.aim_to + 60; i < static_cast<int>(t.near_m.size()); ++i) CHECK(bits(t.near_m[i]) == bits(main_.near_m[i])); // and after, once the ease is done
      std::printf("  near call %s the field-of-view calls, locked view 90: full aim near %.3f m (main %.3f m), shown %.1f degrees\n", places[where],
                  t.near_m[t.aim_to - 1], main_.near_m[t.aim_to - 1], t.shown[t.aim_to - 1]);
    }
    // ---- 2. "The game's zoom", the lock off and the lock on at 90 with the game's zoom already shown: every frame's
    // near clip is main's, to the bit (the hip, the frames going in, full aim, the frames going out). ----
    {
      const Take t = take(where, true, 0, TFP_FOV_AIM_ZOOM, .10f, 70, 30, .098f);
      const Take main_ = take(where, false, 0, TFP_FOV_AIM_ZOOM, .10f, 70, 30, .098f);
      CHECK(same(t, main_, 0, static_cast<int>(t.near_m.size())));
      CHECK(bits(t.near_m[t.aim_to - 1]) == bits(.098f) && std::fabs(t.shown[t.aim_to - 1] - 30) < 1e-4f);
    }
    // ---- 3. The aim-in glide: the lock at 90 with "The game's zoom". The shown field of view glides from 90 down to
    // the game's 30; while it is still wide the near clip is already the scaled one, and it goes out with the field of
    // view, every frame a little and never back, to the game's value to the bit. ----
    {
      const Take t = take(where, true, 90, TFP_FOV_AIM_ZOOM, 0, 70, 30, .098f);
      bool monotone = true, small = true, under = true, wide_scaled = true; int wide = 0;
      // (a near call that comes before the frame's later field-of-view call reads the frame before's pair: in the frame
      // full aim begins that is the hip's, and the scaled value is there a frame later)
      const int first = t.aim_from + (where == 2 ? 0 : 1);
      CHECK(t.near_m[first] == .05f && t.near_m[t.aim_from] >= .05f && t.near_m[t.aim_from] <= .098f);
      for (int i = first + 1; i < t.aim_to; ++i) {
        if (t.near_m[i] < t.near_m[i - 1]) monotone = false;
        if (t.near_m[i] - t.near_m[i - 1] > .01f) small = false;
        const int of = where == 2 ? i : i - 1;                                                    // the pair the near call could read: this frame's or the one before's
        if (t.near_m[i] > scaled(.098f, 30, t.shown[of]) + 1e-6f) under = false;
        if (t.shown[i] > 60) { ++wide; if (t.near_m[i] > .0551f) wide_scaled = false; }
      }
      CHECK(monotone); CHECK(small); CHECK(under); CHECK(wide > 3 && wide_scaled);
      CHECK(bits(t.near_m[t.aim_to - 1]) == bits(.098f) && std::fabs(t.shown[t.aim_to - 1] - 30) < .01f);
      // going out: the near clip of the hip is main's (the game's own here), whatever the lock still glides
      for (int i = t.aim_to; i < static_cast<int>(t.near_m.size()); ++i) CHECK(t.near_m[i] >= .05f && t.near_m[i] <= .098f);
      CHECK(t.near_m.back() == .05f);
      std::printf("  near call %s, lock 90 with the game's zoom: %d frames shown wider than 60 degrees, all at the scaled near clip; then out to %.3f m\n",
                  places[where], wide, t.near_m[t.aim_to - 1]);
    }
    // ---- 4. "A chosen value" of 50 with the lock off: the same 0.026 m under the sight line on screen (0.056 m). ----
    {
      const Take t = take(where, true, 0, 50 * RAD, 0, 70, 30, .098f);
      CHECK(std::fabs(t.shown[t.aim_to - 1] - 50) < .01f);
      CHECK(std::fabs(t.near_m[t.aim_to - 1] - scaled(.098f, 30, 50)) < 1e-5f && std::fabs(t.near_m[t.aim_to - 1] - .0563f) < 1e-4f);
      CHECK(std::fabs(tfp_near_ads_scale() - std::tan(15 * RAD) / std::tan(25 * RAD)) < 1e-5f);   // what full aim's last call scaled by
    }
    // ---- 5. A long scope (the Anti-Materiel Rifle's kind): the game's near clip 0.35 m at a 6 degree lens. Shown
    // at the locked 90 with no scope picture, main cuts everything nearer than 0.35 m: the scope, the receiver and the
    // stock. Here the near plane is at 0.05 m, so they are drawn. With the game's zoom nothing changes, to the bit. ----
    {
      const Take t = take(where, true, 90, TFP_FOV_AIM_LOCKED, .15f, 70, 6, .35f, .35f);
      const Take main_ = take(where, false, 90, TFP_FOV_AIM_LOCKED, .15f, 70, 6, .35f, .35f);
      for (int i = t.aim_from + 1; i < t.aim_to; ++i) CHECK(t.near_m[i] == .05f);
      CHECK(std::fabs(main_.near_m[t.aim_to - 1] - .35f) < 1e-6f);
      CHECK(same(t, main_, 0, t.aim_from) && std::fabs(t.near_m[t.aim_from - 1] - .15f) < 1e-6f); // the hip: the setting, main's
      const Take zoom = take(where, true, 0, TFP_FOV_AIM_ZOOM, .15f, 70, 6, .35f, .35f);
      const Take zoom_main = take(where, false, 0, TFP_FOV_AIM_ZOOM, .15f, 70, 6, .35f, .35f);
      CHECK(same(zoom, zoom_main, 0, static_cast<int>(zoom.near_m.size())) && bits(zoom.near_m[zoom.aim_to - 1]) == bits(.35f));
      std::printf("  near call %s, long scope (game near 0.35 m at 6 degrees) shown at 90: near %.3f m (main %.3f m); with the game's zoom %.3f m\n",
                  places[where], t.near_m[t.aim_to - 1], main_.near_m[t.aim_to - 1], zoom.near_m[zoom.aim_to - 1]);
    }
    // ---- 6. The hip and normal aim are main's whatever the fields of view: the lock at 80, 90 and 120, settings 5,
    // 15 and 40 cm, a sight's distance asked for by the game at the hip. ----
    for (float lock : {80.f, 90.f, 120.f}) for (float setting : {0.f, .15f, .40f}) {
      fresh(setting, setting, true);
      std::vector<float> with, without;
      fovw_lock(lock, TFP_FOV_AIM_LOCKED);
      view(HIP);      for (int i = 0; i < 60; ++i) with.push_back(frame(where, 70, i < 30 ? .05f : .173f));
      view(SHOULDER); for (int i = 0; i < 60; ++i) with.push_back(frame(where, 55, i < 30 ? .05f : .173f));
      fresh(setting, setting, false);
      fovw_lock(lock, TFP_FOV_AIM_LOCKED);
      view(HIP);      for (int i = 0; i < 60; ++i) without.push_back(frame(where, 70, i < 30 ? .05f : .173f));
      view(SHOULDER); for (int i = 0; i < 60; ++i) without.push_back(frame(where, 55, i < 30 ? .05f : .173f));
      bool equal = true;
      for (size_t i = 0; i < with.size(); ++i) if (bits(with[i]) != bits(without[i])) equal = false;
      CHECK(equal);
      CHECK(std::fabs(with[59] - (setting > .05f ? setting : .05f)) < 1e-3f);
    }
  }
  tfp_near_end(gen);
  clip_frames_unbind();
  std::printf("clip frames: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
