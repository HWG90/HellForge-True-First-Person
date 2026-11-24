// The veil (src/camera_helper.h tfp_camera_set_veil; docs/design/specs/2026-11-24-helmet-in-view.md, round 14) through
// the camera helper, on the fake game memory of tests/camera_smooth_test.cpp, whose scene and helpers these are (its
// own checks are not run here).
//
// What soak 27 showed: at every leave of the native view the game switches the body back on in its own update, after
// the mod's tick, and head visibility switches it off on the next tick, so one frame is drawn with the body in it. The
// eye of that frame is the one the swap holds (the outgoing weapon's), 0.22 m behind the head with the LAS-5 Scythe.
#define main camera_smooth_main
#include "camera_smooth_test.cpp"
#undef main

// How far the drawn eye is in front of the head, along the level view direction (the scripts look level).
static float ahead_of_head(const Game &g) {
  const float n = std::sqrt(g.ahead[0] * g.ahead[0] + g.ahead[1] * g.ahead[1]);
  return ((g.shown[0] - g.s.head[0]) * g.ahead[0] + (g.shown[1] - g.s.head[1]) * g.ahead[1]) / n;
}
static float up_of_head(const Game &g) { return g.shown[2] - g.s.head[2]; }
static TfpCameraVeilStats veil() { TfpCameraVeilStats s; std::memset(&s, 0, sizeof s); tfp_camera_veil_stats(&s); return s; }

// Standing, the weapon `rig_ahead` metres in front of the head (negative: behind it) and 0.13 m above it, in the
// native view for a second; `armed`: head visibility is active (the tick's tfp_camera_set_veil). Then the swap's first
// head-camera frame: its drawn eye, how far in front of the head, is returned in `first`.
static float held_ahead, held_up; // the native view's drawn eye in the swap's last native frame: what the hold keeps
static void leave(Game &g, bool lock, bool armed, float rig_ahead, float *first) {
  fresh(lock, false);
  g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
  g.eye[0] = 0; g.eye[1] = rig_ahead; g.eye[2] = .13f;
  CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
  tfp_camera_set_veil(armed);
  standing(g, 30, 33.3, true);
  standing(g, 4, 33.3, true, 2);           // the swap begins: the hold keeps the native view
  held_ahead = ahead_of_head(g); held_up = up_of_head(g);
  g.hip(2); g.frame(33.3);                 // the view leaves the native view: the head camera's first frame
  *first = ahead_of_head(g);
}

int main() {
  W.build(HERE); // the world, as camera_smooth_main sets it up
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  float first = 0;
  // ---- 1. Not armed (head visibility not hiding): the frame is as before, the held eye 0.22 m behind the head. ----
  {
    Game g; leave(g, true, false, -.22f, &first);
    CHECK(std::fabs(first - held_ahead) < .002f && held_ahead < -.2f);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 0 && s.frames == 0 && s.moved == 0 && s.behind == 0);
    g.end();
  }
  // ---- 2. Armed, the Scythe's eye: the first head-camera frame shows the eye TFP_CAMERA_VEIL_CLEAR in front of the
  // head, at the same height; the tick after head visibility switched the body off closes the veil, and the next frame
  // is the held eye again (what the swap glides from is untouched). ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    CHECK(std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f && std::fabs(up_of_head(g) - held_up) < .002f);
    TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 1 && s.frames == 1 && s.moved == 1 && s.behind == 0 && std::fabs(s.largest_m - (TFP_CAMERA_VEIL_CLEAR - held_ahead)) < .002f);
    tfp_camera_veil_tick(true);            // the next tick: the body found drawn again and switched off
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    s = veil();
    CHECK(s.leaves == 1 && s.frames == 1 && s.moved == 1 && s.behind == 0);
    g.end();
  }
  // ---- 3. The frame of the leave came before the tick that made it (the order the game can have it in): that tick
  // finds nothing drawn yet and the veil stays open; the next tick, with the body switched off, closes it. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    CHECK(std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(false);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(true);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 1 && s.frames == 2 && s.moved == 2 && s.behind == 0);
    g.end();
  }
  // ---- 4. Nothing was drawn again (one leave of soak 27's 138 had no re-show): the veil closes after two ticks
  // whatever head visibility found. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 1 && s.frames == 2 && s.moved == 2);
    g.end();
  }
  // ---- 5. An eye in front of the head already (the LAS-58 Talon's, 0.11 m): the veil opens and moves nothing. ----
  {
    Game g; leave(g, true, true, .14f, &first);
    CHECK(std::fabs(first - held_ahead) < .002f && held_ahead > .14f);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 1 && s.frames == 1 && s.moved == 0 && s.behind == 0);
    tfp_camera_veil_tick(true);
    g.end();
  }
  // ---- 6. Switched off by the tick while open (head visibility stopped): closed at once, the next frame is the held eye. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    tfp_camera_set_veil(false);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    CHECK(veil().frames == 1);
    g.end();
  }
  // ---- 7. A native frame while the veil is open (the view back in the native view before the body was switched
  // off): forwarded as it is, and counted as a frame not cleared. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    g.native(); g.frame(33.3);
    const TfpCameraVeilStats s = veil();
    CHECK(s.frames == 2 && s.moved == 1 && s.behind == 1);
    tfp_camera_veil_tick(true);
    g.end();
  }
  // ---- 8. Without the camera lock there is no veil (the lock is the view this mod holds). ----
  {
    Game g; leave(g, false, true, -.22f, &first);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 0 && s.frames == 0);
    g.end();
  }
  std::printf("camera veil through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
