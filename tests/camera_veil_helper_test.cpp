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
#include "head_log.h"

// How far the drawn eye is in front of the head, along the level view direction (the scripts look level).
static float ahead_of_head(const Game &g) {
  const float n = std::sqrt(g.ahead[0] * g.ahead[0] + g.ahead[1] * g.ahead[1]);
  return ((g.shown[0] - g.s.head[0]) * g.ahead[0] + (g.shown[1] - g.s.head[1]) * g.ahead[1]) / n;
}
static float up_of_head(const Game &g) { return g.shown[2] - g.s.head[2]; }
static TfpCameraVeilStats veil() { TfpCameraVeilStats s; std::memset(&s, 0, sizeof s); tfp_camera_veil_stats(&s); return s; }

// Standing, the weapon `rig_ahead` metres in front of the head (negative: behind it) and 0.13 m above it, in the
// native view for a second; `armed`: head visibility is active (the tick's tfp_camera_set_veil), or `head`: the tick
// handed to tfp_camera_veil_from_head instead. Then the swap's first head-camera frame: its drawn eye, how far in
// front of the head, is returned in `first`.
static float held_ahead, held_up; // the native view's drawn eye in the swap's last native frame: what the hold keeps
// Round 19: the trace kept (Trace rows read with tfp_camera_trace_read), and the veil opened by the tick that sees the
// leave (veil_second.txt), for the next leave() only.
static bool leave_trace, leave_second; static double leave_dt = 33.3; // and the time of a frame
static void leave(Game &g, bool lock, bool armed, float rig_ahead, float *first, const TfpHeadTick *head = nullptr) {
  fresh(lock, false, leave_trace);
  tfp_camera_set_veil_second(leave_second); leave_trace = leave_second = false;
  g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
  g.eye[0] = 0; g.eye[1] = rig_ahead; g.eye[2] = .13f;
  CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
  if (head) tfp_camera_veil_from_head(head); else tfp_camera_set_veil(armed);
  const double dt = leave_dt; leave_dt = 33.3;
  standing(g, 30, dt, true);
  standing(g, 4, dt, true, 2);             // the swap begins: the hold keeps the native view
  held_ahead = ahead_of_head(g); held_up = up_of_head(g);
  g.hip(2); g.frame(dt);                   // the view leaves the native view: the head camera's first frame
  *first = ahead_of_head(g);
}

int main() {
  W.build(HERE); // the world, as camera_smooth_main sets it up
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  float first = 0;
  // ---- 1. Not armed (the view off or head visibility not active): the frame is as before, the held eye 0.22 m behind the head. ----
  {
    Game g; leave(g, true, false, -.22f, &first);
    CHECK(std::fabs(first - held_ahead) < .002f && held_ahead < -.2f);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 0 && s.frames == 0 && s.moved == 0 && s.behind == 0);
    g.end();
  }
  // ---- 2. Armed, the Scythe's eye, in the order of every leave soak 27 traced (131 of 131): the first head-camera
  // frame is drawn before the tick that sees the leave, the body still hidden, and shows the eye TFP_CAMERA_VEIL_CLEAR
  // in front of the head at the same height; that tick finds nothing drawn yet and the veil stays open; the game draws
  // the body in the next frame, which is veiled too; the tick after it, with the body switched off, closes the veil and
  // the next frame is the held eye again (what the swap glides from is untouched). Two frames a leave, about 66 ms. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    CHECK(std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f && std::fabs(up_of_head(g) - held_up) < .002f);
    tfp_camera_veil_tick(false);           // the tick that sees the leave: nothing drawn again yet
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(true);            // the next tick: the body found drawn again and switched off
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 1 && s.frames == 2 && s.moved == 2 && s.behind == 0 && std::fabs(s.largest_m - (TFP_CAMERA_VEIL_CLEAR - held_ahead)) < .002f);
    g.end();
  }
  // ---- 3. The other order (not seen in soak 27): the tick that sees the leave runs before the first head-camera
  // frame, so that frame is the one with the body in it; the tick after it closes the veil: one frame. ----
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
  // ---- 7. A native frame while the veil is open (a leave that goes straight back to the native view within a tick:
  // soak 27, 12:42:10.736, 12:42:36.644, 12:45:24.412): not moved, counted among the frames but
  // not as left behind the head (that count is the head-camera frames the veil did not clear). A head-camera frame
  // after it, the veil still open, is moved again. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    g.native(); g.frame(33.3);
    TfpCameraVeilStats s = veil();
    CHECK(s.frames == 2 && s.moved == 1 && s.behind == 0);
    g.hip(2); g.frame(33.3);
    s = veil();
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f && s.frames == 3 && s.moved == 2 && s.behind == 0);
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
  // ---- 9. What the tick hands the veil (tfp_camera_veil_from_head, src/mod_view.c head_note). Armed while the view
  // is on and head visibility active, whatever it owns of the head: until the first leave of a session it owns no
  // head row (soak 27, 12:41:45.163: "kind=6 active: owns head 0, torso 0, shoulders 2"), and that first leave drew
  // 31 meshes again, 5 of them the head's (12:41:50.071), as every other did. ----
  {
    TfpHeadTick t; std::memset(&t, 0, sizeof t);
    t.bound = t.view = t.active = true; t.phase = "active"; t.kind = 6; t.shoulders = 2; t.rows = 5; t.full = true;
    Game g; leave(g, true, false, -.22f, &first, &t);
    CHECK(std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f && veil().leaves == 1 && veil().moved == 1);
    // The next tick (kind 4 now, the head rows taken): nothing found drawn yet, the veil stays open.
    t.kind = 4; t.head = 9; t.torso = 14; t.shoulders = 8; t.rows = 34;
    tfp_camera_veil_from_head(&t);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    // The tick that found the 31 meshes drawn again and switched all of them off closes it.
    t.on_again = 31; t.on_again_head = 5; t.turned_off = 31;
    tfp_camera_veil_from_head(&t);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().frames == 2);
    g.end();
  }
  {
    // The first tick after the leave already finds them and switches all off: closed by that tick, not by the count.
    TfpHeadTick t; std::memset(&t, 0, sizeof t);
    t.bound = t.view = t.active = true; t.phase = "active"; t.kind = 6; t.full = true;
    Game g; leave(g, true, false, -.22f, &first, &t);
    t.kind = 4; t.on_again = 31; t.on_again_head = 5; t.turned_off = 31;
    tfp_camera_veil_from_head(&t);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().frames == 1);
    g.end();
  }
  {
    // Found drawn again but some left drawn: that tick does not close it (the second tick does).
    TfpHeadTick t; std::memset(&t, 0, sizeof t);
    t.bound = t.view = t.active = true; t.phase = "active"; t.kind = 6; t.full = true;
    Game g; leave(g, true, false, -.22f, &first, &t);
    t.kind = 4; t.on_again = 31; t.turned_off = 29; t.left_on = 2;
    tfp_camera_veil_from_head(&t);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    g.end();
  }
  for (int which = 0; which < 3; ++which) {
    // The view off, head visibility not active, or no tick read: not armed, and the next frame is the held eye.
    TfpHeadTick t; std::memset(&t, 0, sizeof t);
    t.bound = t.view = t.active = true; t.phase = "active"; t.kind = 6; t.head = 9; t.full = true;
    if (which == 0) t.view = false;
    if (which == 1) { t.active = false; t.phase = "idle"; }
    Game g; leave(g, true, false, -.22f, &first, which == 2 ? nullptr : &t);
    if (which == 2) { tfp_camera_veil_from_head(nullptr); g.hip(2); g.frame(33.3); }
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 0 && veil().moved == 0);
    g.end();
  }
  {
    // An open veil is closed by a tick that cannot be read.
    TfpHeadTick t; std::memset(&t, 0, sizeof t);
    t.bound = t.view = t.active = true; t.phase = "active"; t.kind = 6; t.full = true;
    Game g; leave(g, true, false, -.22f, &first, &t);
    tfp_camera_veil_from_head(nullptr);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().frames == 1);
    g.end();
  }
  // ---- 10. Off foot at the leave (a dive, a fall): the veil opens but does not move the eye (a thrown body's head is
  // no place to judge "in front" from); the frame is counted as left behind the head. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(true);
    g.end();
    fresh(true, false);
    Game h; h.pose(1, 2, 0, .2f); h.look(.2f, 0); h.hip(); h.begin();
    h.eye[0] = 0; h.eye[1] = -.22f; h.eye[2] = .13f;
    CHECK(tfp_camera_set_weapon_key(h.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    standing(h, 30, 33.3, true);
    standing(h, 4, 33.3, true, 2);
    h.s.state = 50; h.hip(2); h.frame(33.3);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 1 && s.frames == 1 && s.moved == 0 && s.behind == 1 && ahead_of_head(h) < 0);
    tfp_camera_veil_tick(true);
    h.end();
  }
  // ---- 11. A new world (the helper ended and begun again): the last kind of the old one does not open a veil. The
  // old world's last frame was the native view; the new one's first is the head camera. ----
  {
    fresh(true, false);
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .13f;
    tfp_camera_set_veil(true);
    standing(g, 10, 33.3, true);
    g.end();
    Game h; h.pose(4, 5, 0, .2f); h.look(.2f, 0); h.hip(); h.begin();
    tfp_camera_set_veil(true);
    h.hip(); h.frame(33.3); h.hip(); h.frame(33.3);
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 0 && s.frames == 0 && s.moved == 0);
    h.end();
  }
  // ---- 12. Round 19: what is shown is logged. The Trace row of a frame the veil moved carries the move as a vector
  // (veil): the row's eye is the eye before it, as before, and eye + veil is the eye handed to the game. A frame it
  // did not move says nothing (zeros), the native frames before the leave and the held eye after it alike. At every
  // frame time. ----
  for (double dt : {8.0, 16.7, 33.3, 50.0}) {
    Game g; leave_trace = true; leave_dt = dt; leave(g, true, true, -.22f, &first);
    float drawn[3][3]; std::memcpy(drawn[0], g.shown, 12);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(dt); std::memcpy(drawn[1], g.shown, 12);
    tfp_camera_veil_tick(true); g.hip(2); g.frame(dt); std::memcpy(drawn[2], g.shown, 12);
    tfp_camera_trace_edge();
    TfpCameraTraceRow rows[16]; std::memset(rows, 0, sizeof rows);
    const uint32_t n = tfp_camera_trace_read(rows, 16);
    CHECK(n >= 4);
    if (n >= 4) {
      const float a = std::sqrt(g.ahead[0] * g.ahead[0] + g.ahead[1] * g.ahead[1]), way[2] = {g.ahead[0] / a, g.ahead[1] / a};
      for (int k = 0; k < 3; ++k) {
        const TfpCameraTraceRow &r = rows[n - 3 + k];
        float shown[3]; for (int i = 0; i < 3; ++i) shown[i] = r.eye[i] + r.veil[i];
        CHECK(r.kind == 4 && std::fabs(shown[0] - drawn[k][0]) < .002f && std::fabs(shown[1] - drawn[k][1]) < .002f && std::fabs(shown[2] - drawn[k][2]) < .002f);
        const float along = ((r.eye[0] - g.s.head[0]) * way[0] + (r.eye[1] - g.s.head[1]) * way[1]);
        CHECK(std::fabs(along - held_ahead) < .002f); // the row's eye: the held eye, before the veil
        const float moved = std::sqrt(r.veil[0] * r.veil[0] + r.veil[1] * r.veil[1] + r.veil[2] * r.veil[2]);
        if (k < 2) CHECK(std::fabs(moved - (TFP_CAMERA_VEIL_CLEAR - held_ahead)) < .002f && std::fabs(r.veil[0] * way[0] + r.veil[1] * way[1] - moved) < .002f && r.veil[2] == 0);
        else CHECK(moved == 0);
      }
      CHECK(rows[n - 4].kind == 6 && rows[n - 4].veil[0] == 0 && rows[n - 4].veil[1] == 0 && rows[n - 4].veil[2] == 0);
    }
    CHECK(!veil().second);
    g.end();
  }
  // ---- 13. Round 19, a test switch (veil_second.txt; off by default): the veil opens at the tick that sees the leave,
  // not at the first head-camera frame, so in the order soak 27 traced (the first head-camera frame before that tick,
  // the frame with the body after it) only the frame that draws the body is moved. The stats say which way it opens. ----
  for (double dt : {8.0, 16.7, 33.3, 50.0}) {
    Game g; leave_trace = true; leave_second = true; leave_dt = dt; leave(g, true, true, -.22f, &first);
    CHECK(std::fabs(first - held_ahead) < .002f); // the first head-camera frame: the held eye
    TfpCameraVeilStats s = veil();
    CHECK(s.second && s.leaves == 0 && s.frames == 0 && s.moved == 0);
    tfp_camera_veil_tick(false);           // the tick that sees the leave: it opens
    g.hip(2); g.frame(dt);                 // the frame with the body: moved
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(true);            // the body switched off: closed
    g.hip(2); g.frame(dt);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    s = veil();
    CHECK(s.second && s.leaves == 1 && s.frames == 1 && s.moved == 1 && s.behind == 0 && std::fabs(s.largest_m - (TFP_CAMERA_VEIL_CLEAR - held_ahead)) < .002f);
    tfp_camera_trace_edge();
    TfpCameraTraceRow rows[16]; std::memset(rows, 0, sizeof rows);
    const uint32_t n = tfp_camera_trace_read(rows, 16);
    CHECK(n >= 3);
    if (n >= 3) {
      CHECK(rows[n - 3].veil[0] == 0 && rows[n - 3].veil[1] == 0);   // the first head-camera frame: not moved
      CHECK(std::fabs(rows[n - 2].veil[0]) + std::fabs(rows[n - 2].veil[1]) > .2f);
      CHECK(rows[n - 1].veil[0] == 0 && rows[n - 1].veil[1] == 0);
    }
    g.end();
  }
  {
    // Nothing drawn again: closed by the tick after the one that opened it, so one frame.
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().frames == 1 && veil().moved == 1);
    g.end();
  }
  {
    // The tick that sees the leave already finds the body drawn again and switches it off: nothing opens.
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(true); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 0 && veil().frames == 0);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 0);
    g.end();
  }
  {
    // Not armed: the tick opens nothing (the switch alone is no veil).
    Game g; leave_second = true; leave(g, true, false, -.22f, &first);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 0);
    g.end();
  }
  {
    // The leave is seen once: the tick that found the body switched off took it, and later ticks open nothing.
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(true); g.hip(2); g.frame(33.3);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 0);
    g.end();
  }
  // ---- 14. A second leave while the veil is open (a leave that went straight back to the native view within a tick,
  // then left again: soak 27 had three). The tick that sees the second leave keeps the veil open for one more tick, so
  // the frame the game draws the body in after it is veiled too, in both ways of opening; the leave is counted. With
  // veil_second.txt the order is leave, tick (opens), native, leave, tick (keeps it), body frame, tick (closes). ----
  for (int second = 0; second < 2; ++second) for (double dt : {8.0, 16.7, 33.3, 50.0}) {
    Game g; leave_second = second != 0; leave_dt = dt; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(false);           // the tick that sees leave 1: open
    g.native(); g.frame(dt);               // straight back
    g.hip(2); g.frame(dt);                 // leave 2, the veil open: moved
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(false);           // the tick that sees leave 2: kept open
    g.hip(2); g.frame(dt);                 // the frame with the body: moved
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(false);           // closed: the count of two ticks from leave 2's tick
    g.hip(2); g.frame(dt);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    for (int k = 0; k < 3; ++k) {          // and it stays closed: no leave is left to open it again
      tfp_camera_veil_tick(false); g.hip(2); g.frame(dt);
      CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    }
    const TfpCameraVeilStats s = veil();
    CHECK(s.leaves == 2 && s.behind == 0);
    g.end();
  }
  {
    // A second leave whose tick finds the body drawn again and switched off: closed by that tick, as a first leave's.
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(false); g.native(); g.frame(33.3); g.hip(2); g.frame(33.3);
    tfp_camera_veil_tick(true); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 1);
    g.end();
  }
  // ---- 15. A leave noted for the tick (veil_second.txt) is dropped when the veil is switched off, and by the helper's
  // end and begin: the next tick opens nothing. ----
  {
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    tfp_camera_set_veil(false); tfp_camera_set_veil(true);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - held_ahead) < .002f && veil().leaves == 0 && veil().frames == 0);
    g.end();
  }
  {
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    g.end();
    Game h; h.pose(4, 5, 0, .2f); h.look(.2f, 0); h.hip(); h.begin();
    tfp_camera_veil_tick(false); h.hip(); h.frame(33.3);
    CHECK(veil().leaves == 0 && veil().frames == 0);
    h.end();
  }
  // ---- 16. The trace ring (128 rows) wraps after a veiled row: a later frame written into its place says no veil. ----
  {
    Game g; leave_trace = true; leave(g, true, true, -.22f, &first);      // the veiled rows: v
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);                 // v+1
    tfp_camera_veil_tick(true); g.hip(2); g.frame(33.3);                  // v+2: closed
    for (int k = 0; k < 127; ++k) { g.hip(2); g.frame(33.3); }          // up to v+129: v and v+1 written over
    tfp_camera_trace_edge();
    TfpCameraTraceRow rows[16]; std::memset(rows, 0, sizeof rows);
    const uint32_t n = tfp_camera_trace_read(rows, 16);
    CHECK(n == 6);                                                         // v+124 .. v+129
    for (uint32_t k = 0; k < n; ++k) CHECK(rows[k].kind == 4 && rows[k].veil[0] == 0 && rows[k].veil[1] == 0 && rows[k].veil[2] == 0);
    g.end();
  }
  std::printf("camera veil through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
