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
// Round 26: in front of the head `ms` after the last veiled frame of a leave of the Scythe's: the held eye and what of
// the veil's move (to TFP_CAMERA_VEIL_CLEAR) is not let out yet (tfp_camera_veil_let_out). Before round 26 the move was
// taken back in the first frame after the veil closed: the held eye at once.
static float out_ahead(double ms) { return held_ahead + (TFP_CAMERA_VEIL_CLEAR - held_ahead) * tfp_camera_veil_let_out(ms); }
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

// ---- Round 26: swaps in the middle of a dive or lunge (docs/design/specs/2026-11-24-helmet-in-view.md "Round 26"). ----
// A scene in the body root's frame along the heading (forward, up; no side), as a function of the time from the leave
// (ms): the root goes on at speed_before until the leave and speed_after from it; the head and the native eye go from
// standing (head 0.06 forward, 1.5 up; eye 0.10, 1.62) to the leave's over the 330 ms before it; after the leave the
// head goes on to head_after over 330 ms (the native eye is not drawn then: the swap holds it).
struct Dive {
  const char *name; float yaw, pitch, speed_before, speed_after;
  float head_fwd, head_up, eye_fwd, eye_up, head_after_fwd, head_after_up;
};
// 02:16:09.838 (soak 34, action 118; BR-14 Adjudicator to P/40-K Bolt Pistol): Trace rows. The view (0.0986 0.9863
// 0.1325): yaw -0.0997, pitch 7.6 degrees. Root 0.19 m a frame before the leave, 0.14 to 0.15 after (5.8, 4.4 m/s).
// The eye the swap holds: 0.289 m in front of the root and 1.065 m above it. The head node: 0.737 m in front (the
// veil's move 0.548 m to 0.10 m in front of it); its height is not logged: 0.51 m above the root, from the head
// camera's eye in the dives of soak 25 (10:17:37: 0.74 to 0.78 m forward, 0.53 to 0.56 up, less c.forward 0.18 and
// c.up 0.04), so 0.555 m below the held eye. Frame 2's head (.872) is 0.727 m in front.
static const Dive DIVE_118 = {"02:16:09.838 dive", -.0997f, .1329f, 5.8f, 4.4f, .737f, .51f, .289f, 1.065f, .80f, .45f};
// 02:18:33.429 (action 173): the view (-0.0576 0.997 0.03): yaw 0.0577, pitch 1.7 degrees; root 0.07 m a frame (2.1
// m/s); held eye 0.025 m in front of the root, 1.076 m above; the head 0.458 m in front (veil 0.534 m). Its height:
// the wanted eye rose 0.52 m and went 0.24 m back over the 330 ms after the leave with the held difference unchanged,
// so the head did; standing again by then (about 1.5 m), it was about 1.0 m above the root at the leave: level with
// the eye. A lunge, not a dive.
static const Dive LUNGE_173 = {"02:18:33.429 lunge", .0577f, .0300f, 2.1f, 2.1f, .458f, 1.0f, .025f, 1.076f, .23f, 1.52f};

static float lerpf(float a, float b, float t) { return a + (b - a) * std::fmin(1.f, std::fmax(0.f, t)); }
struct DiveFrame { double t; float shown[3], head[3], root[3]; };
struct DiveRun { DiveFrame f[260]; int n = 0, leave = -1; TfpCameraVeilStats s{}; float fwd[3], up[3], h[2]; };
// The scene at one frame time. `armed`: the veil (head visibility active); `fov`: tfp_camera_set_veil_fov.
static void dive_run(const Dive &d, double dt, bool armed, float fov, DiveRun &r) {
  fresh(true, false, true);
  tfp_camera_set_veil_fov(fov);
  Game g;
  auto place = [&](double t, bool native, uint32_t mask) {
    const float p = t < 0 ? d.speed_before * (float)t / 1000 : d.speed_after * (float)t / 1000;
    g.pose(0, 0, 0, d.yaw);
    const float fx = -std::sin(d.yaw), fy = std::cos(d.yaw), root[3] = {fx * p, fy * p, 0};
    g.pose(root[0], root[1], root[2], d.yaw);
    const float before = (float)(1 + t / 330.0), after = (float)(t / 330.0);
    const float hf = t < 0 ? lerpf(.06f, d.head_fwd, before) : lerpf(d.head_fwd, d.head_after_fwd, after);
    const float hu = t < 0 ? lerpf(1.5f, d.head_up, before) : lerpf(d.head_up, d.head_after_up, after);
    const float ef = t < 0 ? lerpf(.10f, d.eye_fwd, before) : d.eye_fwd, eu = t < 0 ? lerpf(1.62f, d.eye_up, before) : d.eye_up;
    g.s.head[0] = root[0] + fx * hf; g.s.head[1] = root[1] + fy * hf; g.s.head[2] = root[2] + hu;
    const float eye[3] = {root[0] + fx * ef, root[1] + fy * ef, root[2] + eu}, arg[3] = {.02f, .01f, .03f};
    const float *axes[3] = {g.right, g.ahead, g.above};
    for (int k = 0; k < 3; ++k) { float v = 0; for (int i = 0; i < 3; ++i) v += (eye[i] - g.s.head[i]) * axes[k][i]; g.eye[k] = v - arg[k]; }
    if (native) g.native(mask); else g.hip(mask);
    g.frame(dt);
    DiveFrame &f = r.f[r.n++]; f.t = t; std::memcpy(f.shown, g.shown, 12); std::memcpy(f.head, g.s.head, 12); std::memcpy(f.root, root, 12);
  };
  g.look(d.yaw, d.pitch); g.pose(0, 0, 0, d.yaw); g.hip(); g.begin();
  CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
  tfp_camera_set_veil(armed);
  int k = (int)std::ceil(600 / dt);
  for (; k > 0; --k) place(-k * dt, true, k * dt <= 140 ? 2 : 0);   // native; the swap from 140 ms before the leave
  r.leave = r.n;
  place(0, false, 2);                                                 // the leave: the first head-camera frame
  tfp_camera_veil_tick(false);                                        // the tick that sees it
  place(dt, false, 2);                                                // the frame with the body
  tfp_camera_veil_tick(true);                                         // the body switched off: closed
  for (k = 2; k * dt <= 700 && r.n < 259; ++k) { place(k * dt, false, 2); tfp_camera_veil_tick(false); }
  r.s = veil();
  std::memcpy(r.fwd, g.ahead, 12); std::memcpy(r.up, g.above, 12);
  const float n = std::sqrt(g.ahead[0] * g.ahead[0] + g.ahead[1] * g.ahead[1]); r.h[0] = g.ahead[0] / n; r.h[1] = g.ahead[1] / n;
  g.end();
}
// The shown eye against the head node: along the level view, sideways, up.
static void from_head(const DiveRun &r, int i, float *out) {
  const float *e = r.f[i].shown, *hd = r.f[i].head;
  out[0] = (e[0] - hd[0]) * r.h[0] + (e[1] - hd[1]) * r.h[1];
  out[1] = (e[0] - hd[0]) * r.h[1] - (e[1] - hd[1]) * r.h[0];
  out[2] = e[2] - hd[2];
}
// Inside the head: within the head as the veil takes it (TFP_CAMERA_VEIL_HEAD_M behind the node to TFP_CAMERA_VEIL_CLEAR
// in front, TFP_CAMERA_VEIL_HEAD_M to either side and up and down).
static bool inside_head(const DiveRun &r, int i) {
  float o[3]; from_head(r, i, o);
  return -o[0] > -TFP_CAMERA_VEIL_HEAD_M && -o[0] < TFP_CAMERA_VEIL_CLEAR && std::fabs(o[1]) < TFP_CAMERA_VEIL_HEAD_M && std::fabs(o[2]) < TFP_CAMERA_VEIL_HEAD_M;
}
// Clear of the head, worked out apart from the helper: TFP_CAMERA_VEIL_CLEAR or more in front of it, or every corner of
// that head below the bottom edge of a view of `half` (half the vertical field of view): the angle of each corner from
// the view axis, in the plane the view's up and forward span, under -half (or the corner behind the eye).
static bool clear_of_head(const DiveRun &r, int i, float half) {
  float o[3]; from_head(r, i, o);
  if (o[0] >= TFP_CAMERA_VEIL_CLEAR - .002f) return true;
  for (float a : {-TFP_CAMERA_VEIL_HEAD_M, TFP_CAMERA_VEIL_CLEAR}) for (float s : {-TFP_CAMERA_VEIL_HEAD_M, TFP_CAMERA_VEIL_HEAD_M})
    for (float u : {-TFP_CAMERA_VEIL_HEAD_M, TFP_CAMERA_VEIL_HEAD_M}) {
      const float *hd = r.f[i].head, *e = r.f[i].shown;
      const float c[3] = {hd[0] + r.h[0] * a + r.h[1] * s - e[0], hd[1] + r.h[1] * a - r.h[0] * s - e[1], hd[2] + u - e[2]};
      const float along = c[0] * r.fwd[0] + c[1] * r.fwd[1] + c[2] * r.fwd[2], high = c[0] * r.up[0] + c[1] * r.up[1] + c[2] * r.up[2];
      if (along > 0 && std::atan2(high, along) >= -half) return false;
    }
  return true;
}
// The judge's measure (tools/harness/matrix.py measure: the shown eye's change of step over three frames), its largest
// from the frame before the leave to the end; the body-lunge rule is left out (the root's own change here is at most
// (5.8 - 4.4) m/s x dt, 0.07 m at 50 ms, in the bare run as well).
static float largest_change(const DiveRun &r) {
  float most = 0;
  for (int i = r.leave; i + 1 < r.n; ++i) {
    float d = 0; for (int k = 0; k < 3; ++k) { const float c = r.f[i + 1].shown[k] - 2 * r.f[i].shown[k] + r.f[i - 1].shown[k]; d += c * c; }
    most = std::fmax(most, std::sqrt(d));
  }
  return most;
}
static float moved_by(const DiveRun &armed, const DiveRun &bare, int i) {
  float d = 0; for (int k = 0; k < 3; ++k) { const float c = armed.f[i].shown[k] - bare.f[i].shown[k]; d += c * c; }
  return std::sqrt(d);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f && veil().frames == 2);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f && veil().frames == 1);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f && veil().frames == 1);
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
        else CHECK(std::fabs(moved - (TFP_CAMERA_VEIL_CLEAR - held_ahead) * tfp_camera_veil_let_out(dt)) < .002f); // round 26: let out
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(dt)) < .002f);
    s = veil();
    CHECK(s.second && s.leaves == 1 && s.frames == 1 && s.moved == 1 && s.behind == 0 && std::fabs(s.largest_m - (TFP_CAMERA_VEIL_CLEAR - held_ahead)) < .002f);
    tfp_camera_trace_edge();
    TfpCameraTraceRow rows[16]; std::memset(rows, 0, sizeof rows);
    const uint32_t n = tfp_camera_trace_read(rows, 16);
    CHECK(n >= 3);
    if (n >= 3) {
      CHECK(rows[n - 3].veil[0] == 0 && rows[n - 3].veil[1] == 0);   // the first head-camera frame: not moved
      CHECK(std::fabs(rows[n - 2].veil[0]) + std::fabs(rows[n - 2].veil[1]) > .2f);
      CHECK(std::fabs(std::sqrt(rows[n - 1].veil[0] * rows[n - 1].veil[0] + rows[n - 1].veil[1] * rows[n - 1].veil[1]) - (TFP_CAMERA_VEIL_CLEAR - held_ahead) * tfp_camera_veil_let_out(dt)) < .002f); // round 26: let out
    }
    g.end();
  }
  {
    // Nothing drawn again: closed by the tick after the one that opened it, so one frame.
    Game g; leave_second = true; leave(g, true, true, -.22f, &first);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - TFP_CAMERA_VEIL_CLEAR) < .002f);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f && veil().frames == 1 && veil().moved == 1);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(dt)) < .002f);
    for (int k = 0; k < 3; ++k) {          // and it stays closed: no leave is left to open it again
      tfp_camera_veil_tick(false); g.hip(2); g.frame(dt);
      CHECK(std::fabs(ahead_of_head(g) - out_ahead((k + 2) * dt)) < .002f);
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
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(33.3)) < .002f);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3);
    CHECK(std::fabs(ahead_of_head(g) - out_ahead(66.6)) < .002f && veil().leaves == 1);
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
  // ---- 17. Round 26: a swap in the middle of a dive (soak 34, 02:16:09.838). The head is 0.45 m in front of the eye
  // the swap holds and 0.55 m below it; the old veil moved the eye 0.55 m forward for two frames and back. Now it moves
  // it only as far as puts the head below the bottom edge of the view (90 degrees: the user's field of view; not
  // known: the widest the settings allow, 120), the eye is never inside the head in a veiled frame, and the move is
  // let out after the veil closes instead of taken back in a frame. At every frame time. ----
  for (float fov : {1.5707964f, 0.0f}) for (double dt : {8.0, 16.7, 33.3, 50.0}) {
    static DiveRun armed, bare; armed = DiveRun(); bare = DiveRun();
    dive_run(DIVE_118, dt, true, fov, armed); dive_run(DIVE_118, dt, false, fov, bare);
    const float half = (fov > 0 ? fov : TFP_CAMERA_VEIL_FOV_MOST) / 2;
    const int L = armed.leave;
    const float m0 = moved_by(armed, bare, L), m1 = moved_by(armed, bare, L + 1);
    std::printf("  %s, %.1f ms, field of view %s: moved %.3f and %.3f m (largest %.3f), the judge's measure %.3f m (bare %.3f), under %llu, far %llu\n",
                DIVE_118.name, dt, fov > 0 ? "90" : "not known", m0, m1, armed.s.largest_m, largest_change(armed), largest_change(bare),
                (unsigned long long)armed.s.under, (unsigned long long)armed.s.too_far);
    for (int i = L; i <= L + 1; ++i) {
      CHECK(!inside_head(armed, i));             // never inside the head in a veiled frame
      CHECK(clear_of_head(armed, i, half));       // and the head out of the frame
    }
    // The held eye alone has the head in a frame of the widest field of view (at 90 degrees only its top front edge, by
    // about a degree): the veil is needed.
    CHECK(!inside_head(bare, L) && !clear_of_head(bare, L, TFP_CAMERA_VEIL_FOV_MOST / 2));
    CHECK(m0 > .02f && m0 < (fov > 0 ? .16f : .30f) && m1 < (fov > 0 ? .16f : .30f));
    CHECK(armed.s.largest_m < (fov > 0 ? .16f : .30f) && armed.s.under == 2 && armed.s.too_far == 0 && armed.s.behind == 0 && armed.s.moved == 2);
    CHECK(largest_change(armed) < armed.s.largest_m + largest_change(bare) + .01f && largest_change(armed) < (fov > 0 ? .20f : .35f));
    // Let out: after the veil closes the move shrinks every frame and is gone after TFP_CAMERA_VEIL_LET_OUT_MS.
    // The first frame after it still carries what tfp_camera_veil_let_out says of the move.
    CHECK(std::fabs(moved_by(armed, bare, L + 2) - m1 * tfp_camera_veil_let_out(armed.f[L + 2].t - armed.f[L + 1].t)) < .01f);
    float last = m1;
    for (int i = L + 2; i < armed.n; ++i) {
      const float now = moved_by(armed, bare, i);
      CHECK(now <= last + .002f); last = now;
      if (armed.f[i].t - armed.f[L + 1].t >= TFP_CAMERA_VEIL_LET_OUT_MS) CHECK(now < .002f);
    }
  }
  // ---- 18. Round 26: a lunge (soak 34, 02:18:33.429): the head 0.43 m in front of the held eye and level with it. No
  // move short of 0.53 m takes the helmet out of the frame, and that is over TFP_CAMERA_VEIL_MOST: the frames keep the
  // held eye (a frame of the helmet from 0.43 m behind it, accepted in this case only), counted as left behind the head
  // and as far. Never inside the head. ----
  for (double dt : {8.0, 16.7, 33.3, 50.0}) {
    static DiveRun armed, bare; armed = DiveRun(); bare = DiveRun();
    dive_run(LUNGE_173, dt, true, 1.5707964f, armed); dive_run(LUNGE_173, dt, false, 1.5707964f, bare);
    const int L = armed.leave;
    std::printf("  %s, %.1f ms: moved %.3f and %.3f m (largest %.3f), the judge's measure %.3f m (bare %.3f), far %llu, behind %llu\n",
                LUNGE_173.name, dt, moved_by(armed, bare, L), moved_by(armed, bare, L + 1), armed.s.largest_m, largest_change(armed), largest_change(bare),
                (unsigned long long)armed.s.too_far, (unsigned long long)armed.s.behind);
    for (int i = L; i <= L + 1; ++i) CHECK(!inside_head(armed, i) && moved_by(armed, bare, i) < .002f);
    CHECK(armed.s.moved == 0 && armed.s.too_far == 2 && armed.s.behind == 2 && armed.s.largest_m == 0);
    CHECK(largest_change(armed) < largest_change(bare) + .002f);
  }
  // ---- 19. Round 26, made-up dives: the head 0.30 to 0.70 m below the held eye and 0.30 to 0.55 m in front of it, at
  // fields of view of 80, 90 and 120 degrees and every frame time. In every veiled frame the eye is clear of the head (in
  // front of it, or the head below the view) or, where that would take more than TFP_CAMERA_VEIL_MOST, left where it was
  // and counted far; never inside the head; never moved more than TFP_CAMERA_VEIL_MOST. ----
  {
    int scenes = 0, moved_less = 0, kept = 0; float largest = 0;
    for (float below : {.30f, .45f, .55f, .70f}) for (float ahead : {.30f, .45f, .55f}) for (float fov_deg : {80.f, 90.f, 120.f})
      for (double dt : {8.0, 16.7, 33.3, 50.0}) {
        static DiveRun armed, bare; armed = DiveRun(); bare = DiveRun();
        Dive d = DIVE_118; d.head_fwd = d.eye_fwd + ahead; d.head_up = d.eye_up - below; d.head_after_fwd = d.head_fwd + .06f; d.head_after_up = d.head_up - .06f;
        const float fov = fov_deg * 3.14159265f / 180;
        dive_run(d, dt, true, fov, armed); dive_run(d, dt, false, fov, bare);
        const int L = armed.leave; ++scenes;
        for (int i = L; i <= L + 1; ++i) {
          const float m = moved_by(armed, bare, i);
          CHECK(!inside_head(armed, i));
          CHECK(m <= TFP_CAMERA_VEIL_MOST + .002f);
          CHECK(clear_of_head(armed, i, fov / 2) || (m < .002f && armed.s.too_far > 0));
          largest = std::fmax(largest, m);
        }
        if (armed.s.too_far) ++kept; else if (armed.s.under) ++moved_less;
      }
    std::printf("  made-up dives: %d scenes, %d moved less than the whole way, %d kept the held eye (far), largest move %.3f m\n", scenes, moved_less, kept, largest);
    CHECK(moved_less > 0);
  }
  // ---- 20. Round 26: the standing swap the veil was made for (the LAS-5 Scythe's eye 0.22 m behind the head, 0.13 m
  // above it) still moves the whole way to TFP_CAMERA_VEIL_CLEAR: the head is not below the view from there. ----
  {
    Game g; leave(g, true, true, -.22f, &first);
    CHECK(std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f && veil().under == 0 && veil().too_far == 0);
    tfp_camera_veil_tick(true);
    g.end();
  }
  // ---- 21. Round 30: the release of full aim (soak 36, 07:03:36.695): the game evaluates the camera a second time 0.35 ms
  // after the native frame with its ordinary camera on top and no action owning the view, and the next frame is the
  // native view again. The leave is kept pending: that frame is not moved (it is the eye the veil would not have
  // touched), the tick opens the veil for head-camera frames only, and the native frames after it are the game's own.
  // A swap in full aim (the gun leaving the hands, mask 2), any other action owning the view (mask 1, a stim's 0x40), a
  // same-frame leave out of the hip and a lone frame a whole frame later are veiled at once as before; a kept leave
  // whose view stays away is veiled from the next frame, its move let in (TFP_CAMERA_VEIL_LET_IN_M: 0.04 m, then
  // 0.12 m) instead of the whole 0.31 m in one frame, and let out from there after the veil closes. ----
  {
    struct Release { const char *name; bool aim; uint32_t mask; double gap; bool kept; };
    const Release cases[] = {{"aim released, nothing owns the view, 0.35 ms", true, 0, .35, true},
                             {"a swap in full aim (mask 2), 0.25 ms", true, 2, .25, false},
                             {"another action owning the view (mask 1), 0.3 ms", true, 1, .3, false},
                             {"a stim in full aim (mask 0x40), 0.3 ms", true, 0x40, .3, false},
                             {"out of the hip, 0.3 ms", false, 0, .3, false},
                             {"aim released, a whole frame later", true, 0, 33.3, false}};
    for (const Release &c : cases) for (int stay = 0; stay < 2; ++stay) {
      float seen[2] = {0, 0}, after[2] = {0, 0}, third[2] = {0, 0}, out[2] = {0, 0}; TfpCameraVeilStats s[2];
      for (int armed = 0; armed < 2; ++armed) {
        Game g; fresh(true, false, false); tfp_camera_set_veil_second(false);
        g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
        g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .13f;
        CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
        tfp_camera_set_veil(armed != 0);
        for (int i = 0; i < 30; ++i) { g.native(0, c.aim); g.frame(33.3); }
        g.hip(c.mask); g.frame(c.gap); seen[armed] = ahead_of_head(g);
        if (stay) {
          tfp_camera_veil_tick(false); g.hip(c.mask); g.frame(33.3 - c.gap); after[armed] = ahead_of_head(g);
          g.hip(c.mask); g.frame(33.3); third[armed] = ahead_of_head(g);   // a second frame before the tick
          tfp_camera_veil_tick(true); g.hip(c.mask); g.frame(33.3); out[armed] = ahead_of_head(g); // closed: let out
        }
        else {
          g.native(0, false); g.frame(33.3 - c.gap); after[armed] = ahead_of_head(g);
          tfp_camera_veil_tick(false); g.native(0, false); g.frame(33.3);
          tfp_camera_veil_tick(false); g.native(0, false); g.frame(33.3);
        }
        s[armed] = veil(); g.end();
      }
      std::printf("  %s, %s: first frame %.3f m in front of the head (bare %.3f), the next %.3f (bare %.3f); moved %llu, held back %llu\n",
                  c.name, stay ? "the view stays away" : "back to the native view", seen[1], seen[0], after[1], after[0],
                  (unsigned long long)s[1].moved, (unsigned long long)s[1].held_back);
      if (c.kept) {
        CHECK(std::fabs(seen[1] - seen[0]) < .002f && s[1].held_back == 1);
        if (stay) {
          // The whole move would be 0.31 m; frame 2 takes 0.04, frame 3 0.12 (the step changes by 0.04 m a frame), then the
          // let-out from 0.12. Short of TFP_CAMERA_VEIL_CLEAR: counted behind.
          std::printf("    let in: %.3f, %.3f m, then %.3f m let out\n", after[1] - after[0], third[1] - third[0], out[1] - out[0]);
          CHECK(TFP_CAMERA_VEIL_CLEAR - seen[0] > .3f);
          CHECK(std::fabs((after[1] - after[0]) - tfp_camera_veil_let_in(1)) < .002f);
          CHECK(std::fabs((third[1] - third[0]) - tfp_camera_veil_let_in(2)) < .002f);
          CHECK(std::fabs((out[1] - out[0]) - tfp_camera_veil_let_in(2) * tfp_camera_veil_let_out(33.3)) < .002f);
          CHECK(s[1].moved == 2 && s[1].behind == 2);
        }
        else CHECK(std::fabs(after[1] - after[0]) < .002f && s[1].moved == 0 && s[1].behind == 0);
      } else {
        CHECK(std::fabs(seen[1] - TFP_CAMERA_VEIL_CLEAR) < .002f && seen[0] < -.1f && s[1].held_back == 0 && s[1].leaves == 1);
        if (stay) CHECK(std::fabs(after[1] - TFP_CAMERA_VEIL_CLEAR) < .002f && std::fabs(third[1] - TFP_CAMERA_VEIL_CLEAR) < .002f);
      }
    }
  }
  // ---- 22. Round 30 (review): the let-in is a kept leave's only. A lone release of full aim (kept pending, the native
  // view straight back), then a second later a swap leaves out of the hip: that leave is veiled the whole way at once,
  // in its first frame and the next, as every leave before round 30. ----
  {
    Game g; fresh(true, false, false); tfp_camera_set_veil_second(false);
    g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .13f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    for (int i = 0; i < 30; ++i) { g.native(0, true); g.frame(33.3); }
    g.hip(0); g.frame(.35);                                               // the lone frame, kept pending
    g.native(0, false); g.frame(33.0); tfp_camera_veil_tick(false);
    for (int i = 0; i < 30; ++i) { g.native(0, false); g.frame(33.3); tfp_camera_veil_tick(false); }
    standing(g, 4, 33.3, true, 2);                                        // a swap out of the hip
    g.hip(2); g.frame(33.3); const float first = ahead_of_head(g);
    tfp_camera_veil_tick(false); g.hip(2); g.frame(33.3); const float next = ahead_of_head(g);
    std::printf("  a swap after a kept release: %.3f, %.3f m in front of the head; held back %llu\n", first, next, (unsigned long long)veil().held_back);
    CHECK(veil().held_back == 1 && std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f && std::fabs(next - TFP_CAMERA_VEIL_CLEAR) < .002f);
    g.end();
  }
  // ---- 23. Round 32: a kept release whose view really leaves (soak 37, 10:07:04.745, the P/40-K Bolt Pistol fired in
  // full aim). The game drops the aim (Drop: kind=4, mask 0) and evaluates the head camera 0.7 ms after the native frame
  // of full aim; nothing holds the native eye (the lease ended without a native exit) and the continuity takes nothing in
  // full aim, so main showed that frame 0.425 m from the native eye, unveiled, and the head camera from there on. Now
  // the kept frame's step from the eye last handed to the game is clamped to tfp_camera_veil_let_in(1) and what is left
  // is let out over TFP_CAMERA_VEIL_LET_OUT_MS; the Drop's tick (head visibility still active) opens the veil for the
  // frames after it, which are veiled; the Wait's tick that switched the body off closes it. Scene: the sight 0.13 m in
  // front of the head camera's eye and 0.27 m below it (the rows: 0.297 m along the view, 0.306 across). The same
  // script unarmed is main's frame (bare). Why nothing held the eye in the game the log does not say (no swap hold,
  // and retail's 150 ms correction was not running: the next rows go on at the head camera's own pace); here nothing
  // is remembered because the release's request names no firearm, with the eye hold and the sight anchor off. ----
  {
    float first[2] = {0, 0}, worst[2] = {0, 0}, last_gap[2] = {0, 0}; uint64_t veiled[2] = {0, 0}, clamped = 0, held = 0;
    for (int armed = 0; armed < 2; ++armed) {
      Game g; fresh(true, false, false); tfp_camera_set_veil_second(false);
      g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
      CHECK(tfp_camera_set_cut_hold(g.gen, 0) == 1);                     // no eye hold and no sight anchor: nothing kept the native eye
      g.eye[0] = -.02f; g.eye[1] = .30f; g.eye[2] = -.26f;                // the sight: 0.31 m in front of the head, 0.23 m below it
      CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
      TfpHeadTick t; std::memset(&t, 0, sizeof t);
      t.bound = t.view = t.active = true; t.phase = "active"; t.kind = 6; t.head = 5; t.torso = 11; t.shoulders = 12; t.rows = 29; t.full = true;
      if (armed) tfp_camera_veil_from_head(&t); else tfp_camera_set_veil(false);
      for (int i = 0; i < 30; ++i) { g.native(0, true); g.frame(33.3); }
      g.s.request[2] = 0; g.native(0, true); g.frame(33.3);             // fired: no firearm in the request, nothing remembered
      const float native_eye[3] = {g.shown[0], g.shown[1], g.shown[2]};
      g.hip(0); g.s.aim = 1; g.frame(.7);                                 // the kept frame, mask 0, still in full aim
      const float d[3] = {g.shown[0] - native_eye[0], g.shown[1] - native_eye[1], g.shown[2] - native_eye[2]};
      first[armed] = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      t.kind = 4;
      if (armed) tfp_camera_veil_from_head(&t);                           // the Drop's tick: head visibility active, nothing drawn yet
      float prev[3] = {g.shown[0], g.shown[1], g.shown[2]}, step[3] = {d[0], d[1], d[2]};
      const uint64_t frames_before = veil().frames;
      for (int i = 0; i < 14; ++i) {
        if (i == 1 && armed) { t.on_again = 26; t.on_again_head = 5; t.turned_off = 26; tfp_camera_veil_from_head(&t); } // the Wait's tick: the body off
        if (i == 2 && armed) { t.on_again = t.on_again_head = t.turned_off = 0; tfp_camera_veil_from_head(&t); }
        if (i == 1) veiled[armed] = veil().frames - frames_before;
        g.hip(0); g.frame(i == 0 ? 32.6 : 33.3);
        const float now[3] = {g.shown[0] - prev[0], g.shown[1] - prev[1], g.shown[2] - prev[2]};
        const float change = std::sqrt((now[0] - step[0]) * (now[0] - step[0]) + (now[1] - step[1]) * (now[1] - step[1]) + (now[2] - step[2]) * (now[2] - step[2]));
        if (change > worst[armed]) worst[armed] = change;
        std::memcpy(prev, g.shown, 12); std::memcpy(step, now, 12);
      }
      last_gap[armed] = g.from_head_eye();
      if (armed) { clamped = veil().clamped; held = veil().held_back; }
      g.end();
    }
    std::printf("  a kept release whose view leaves (10:07:04.745): first step %.3f m (bare %.3f), %llu frame(s) veiled after it, "
                "largest change of step %.3f m (bare %.3f), %.3f m from the head camera's eye at the end; held back %llu, clamped %llu\n",
                first[1], first[0], (unsigned long long)veiled[1], worst[1], worst[0], last_gap[1], (unsigned long long)held, (unsigned long long)clamped);
    CHECK(first[0] > .25f);                                               // main's frame: the whole way from the sight
    CHECK(first[1] <= tfp_camera_veil_let_in(1) + .002f);                 // at most the let-in cap
    CHECK(held == 1 && clamped == 1 && veiled[1] == 1);                   // kept, clamped, and the frame after the Drop veiled
    CHECK(worst[1] < .06f && worst[0] > .2f);                             // no change of step at the judge's 0.06 m
    CHECK(last_gap[1] < .002f && last_gap[0] < .002f);                    // let out: the head camera's own eye after 300 ms
  }
  // ---- 24. Round 32: the clamp is the kept frame's only, and it ends with the native view. A kept release with no eye
  // hold that goes straight back to the native view: the lone frame is clamped (0.04 m out instead of 0.30), the native
  // frame after it is the game's own, and the head-camera frames of a later leave are not carried. ----
  {
    Game g; fresh(true, false, false); tfp_camera_set_veil_second(false);
    g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    CHECK(tfp_camera_set_cut_hold(g.gen, 0) == 1);
    g.eye[0] = -.02f; g.eye[1] = .30f; g.eye[2] = -.26f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    for (int i = 0; i < 30; ++i) { g.native(0, true); g.frame(33.3); }
    g.s.request[2] = 0; g.native(0, true); g.frame(33.3);
    const float native_eye[3] = {g.shown[0], g.shown[1], g.shown[2]};
    g.hip(0); g.s.aim = 1; g.frame(.35);
    const float lone = std::sqrt((g.shown[0] - native_eye[0]) * (g.shown[0] - native_eye[0]) + (g.shown[1] - native_eye[1]) * (g.shown[1] - native_eye[1])
                                 + (g.shown[2] - native_eye[2]) * (g.shown[2] - native_eye[2]));
    g.native(0, false); g.frame(33.0); const float back = g.from_rig_eye();
    tfp_camera_veil_tick(false);
    g.s.request[2] = 0x444; g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .13f; CHECK(tfp_camera_set_cut_hold(g.gen, 15) == 1); // a firearm again, held as every swap is
    for (int i = 0; i < 30; ++i) { g.native(0, false); g.frame(33.3); tfp_camera_veil_tick(false); }
    standing(g, 4, 33.3, true, 2);                                        // a swap out of the hip, a second later
    g.hip(2); g.frame(33.3); const float swap = ahead_of_head(g);
    std::printf("  a kept release straight back, no hold: lone frame %.3f m from the native eye, then %.3f m from the rig's; a swap after: %.3f m in front of the head\n",
                lone, back, swap);
    CHECK(lone <= tfp_camera_veil_let_in(1) + .002f && back < .002f && veil().moved == 1 && veil().clamped == 1);
    CHECK(std::fabs(swap - TFP_CAMERA_VEIL_CLEAR) < .002f);
    g.end();
  }
  // ---- Round 49: a veiled frame that moves less than the frame before it (soak 60, 19:38:37.383: the veil 0.248 m in
  // the first head-camera frame and nothing in the second, the view having pitched 13.7 degrees; soak 62, 19:45:10.494:
  // 0.120 m, then 0.055 with the head below the view). It wrote its own move over the one kept for the let-out, so the
  // difference was dropped in one frame, the body still. Now the earlier move and its time are kept and let out from
  // that frame on. The view pitched up 63 degrees (the head half below it: 0.025 m moved) and 80 (nothing moved). ----
  for (float pitch : {1.1f, 1.4f}) {
    Game g; leave(g, true, true, -.22f, &first);
    CHECK(std::fabs(first - TFP_CAMERA_VEIL_CLEAR) < .002f);
    const float moved = first - held_ahead; // 0.31 m
    tfp_camera_veil_tick(false);           // the tick that sees the leave: the veil stays open
    const TfpCameraVeilStats before = veil();
    g.look(.2f, pitch); g.hip(2); g.frame(33.3);
    CHECK(veil().frames == before.frames + 1); // a veiled frame
    // short of the first frame's place by the let-out's share of one frame (3.4 percent: 0.011 m), not by the move
    const float now = ahead_of_head(g);
    std::printf("  a veiled frame that moves less, the view pitched %.0f degrees: the eye %.3f m back from the frame before (the let-out's share %.3f; the move %.3f)\n",
                pitch * 57.29578f, first - now, moved * (1 - tfp_camera_veil_let_out(33.3)), moved);
    CHECK(std::fabs(first - now - moved * (1 - tfp_camera_veil_let_out(33.3))) < .003f);
    // and it goes on from there, counted from the frame that moved: no step over the let-out's own
    tfp_camera_veil_tick(true);
    float last = now, steepest = 0;
    for (int i = 2; i <= 10; ++i) {
      g.hip(2); g.frame(33.3);
      const float at = ahead_of_head(g);
      CHECK(std::fabs(first - at - moved * (1 - tfp_camera_veil_let_out(33.3 * i))) < .004f);
      steepest = std::fmax(steepest, std::fabs(at - last)); last = at;
    }
    CHECK(steepest < 1.5f * moved * 33.3f / 300 + .003f && std::fabs(last - held_ahead) < .004f);
    g.end();
  }
  // ---- Round 55, the veil at a leave and an entry (soak 68, Bodycam #68 11:32:00.915, #82, #200; the matrix's
  // stand-up-prone-head, 11:09:12.265). As the game has it: the continuity and the push on, the swap holding the eye
  // `behind` metres behind the head at the leave (prone: the head well in front of the held eye), the veil closed by
  // the tick after the frame with the body. Every helper suite runs the shipped defaults (fresh() changes none). ----
  struct Played { float eye[40][3]; float veil[40]; float head_shown[40]; bool head_known[40]; int kinds[40]; int n = 0; };
  auto play_leave = [&](float behind, int head_frames, int native_frames, Played &p) {
    fresh(true, true, true);
    tfp_camera_set_veil(true); tfp_camera_set_clear(true);
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -behind; g.eye[2] = .13f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    standing(g, 30, 33.3, true); standing(g, 3, 33.3, true, 2);
    tfp_camera_trace_edge();
    auto note = [&](int kind) {
      TfpCameraTraceRow rows[8]; std::memset(rows, 0, sizeof rows); const uint32_t got = tfp_camera_trace_read(rows, 8);
      const TfpCameraTraceRow *r = got ? &rows[got - 1] : nullptr;
      const float *v = r ? r->veil : nullptr;
      std::memcpy(p.eye[p.n], g.shown, 12); p.veil[p.n] = v ? std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) : 0; p.kinds[p.n] = kind;
      p.head_known[p.n] = r && r->head_known; p.head_shown[p.n] = r ? r->head_shown[0] : 0; ++p.n;
      tfp_camera_trace_edge();
    };
    g.native(2); g.frame(33.3); note(6); // the swap's last native frame: p.eye[0]
    for (int i = 0; i < head_frames; ++i) { g.hip(2); g.frame(33.3); tfp_camera_veil_tick(i == 1); note(4); }
    for (int i = 0; i < native_frames; ++i) { g.native(2); g.frame(33.3); tfp_camera_veil_tick(false); note(6); }
    g.end();
  };
  auto change_of = [](const Played &p, int k) {
    float d = 0; for (int i = 0; i < 3; ++i) { const float c = (p.eye[k + 1][i] - p.eye[k][i]) - (p.eye[k][i] - p.eye[k - 1][i]); d += c * c; } return std::sqrt(d);
  };
  // THE OPEN is the round 44 clearance, whole in the leave's frame: an open of 0.4 m on foot (the head that far in front
  // of the held eye) puts the eye shown TFP_CAMERA_VEIL_CLEAR in front of the head in the leave's own frame and in every
  // head-camera frame after it, as the matrix's behind_head judge and the throw-eye judges want. (Round 55 tried
  // spreading it, 0.15 m a frame: those frames went 0.05 to 0.19 m behind the head on the throw-eye judges, so it was
  // dropped.)
  for (float behind : {.31f, .28f, .10f, .03f}) {
    Played p; play_leave(behind, 12, 0, p);
    float least = 1e9f; int known = 0;
    for (int k = 1; k < p.n; ++k) if (p.kinds[k] == 4 && p.head_known[k]) { ++known; least = std::fmin(least, -p.head_shown[k]); }
    std::printf("  round 55, a veil opening behind the head by %.2f m: the leave's veil %.3f m; the eye shown at least %.3f m in front of the head over %d head-camera frames\n",
                behind, p.veil[1], least, known);
    CHECK(known >= 10 && p.head_known[1]);
    CHECK(least >= TFP_CAMERA_VEIL_CLEAR - 2e-3f);
    if (behind > .25f) CHECK(p.veil[1] > .30f); // the whole move in the leave's frame
  }
  // (a) THE ENTRY: back into the native view while a veil of 0.26 m is still let out. The native view's first frame
  // took the eye without the veil (main: the eye stepped back the veil's whole 0.26 m in one frame and the step
  // changed by 0.30). Now the continuity is handed the eye shown, eye + veil, and lets the rest out.
  {
    Played p; play_leave(.28f, 4, 10, p);
    int entry = -1; for (int k = 2; k < p.n; ++k) if (p.kinds[k] == 6) { entry = k; break; } // p.eye[0] is the swap's native frame
    CHECK(entry > 0 && p.veil[entry - 1] > .15f);
    float worst = 0; for (int k = entry - 1; k + 1 < p.n && k < entry + 4; ++k) worst = std::fmax(worst, change_of(p, k));
    std::printf("  round 55, back into the native view with a veil of %.3f m let out: the step changed by %.3f m at most\n", entry > 0 ? p.veil[entry - 1] : 0, worst);
    CHECK(worst < .07f);
  }
  std::printf("camera veil through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
