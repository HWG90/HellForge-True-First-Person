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
  std::printf("camera glide through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
