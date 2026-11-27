// Round 49: the direction shown across a change of camera (src/camera_turn.h THE CARRY, THE DIRECTION) through the
// camera helper, on the fake game memory of tests/camera_smooth_test.cpp, whose scene and helpers these are (its own
// checks are not run here).
#define main camera_smooth_main
#include "camera_smooth_test.cpp"
#undef main
extern "C" {
#include "camera_turn.h"
}

static TfpQ about_up(float degrees) { const float h = degrees * TFP_PI / 360; return tfp_q(0, 0, std::sin(h), std::cos(h)); }
// The rotation the helper handed the game in the frame just played, as it left it in the argument.
static TfpQ handed() { TfpQ q; std::memcpy(&q, reinterpret_cast<void *>(W.native() + 64), 16); return q; }
// The direction the frame just played was shown with, in the world: the argument's forward, through the rig eye's
// axes in the native view.
static void shown_forward(const Game &g, float *out) {
  static const float world[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  float rig[9]; for (int a = 0; a < 3; ++a) for (int c = 0; c < 3; ++c) rig[a * 3 + c] = g.s.rig[a * 4 + c];
  tfp_turn_forward(g.s.linked ? rig : world, handed(), out);
}
static float degrees_apart(const float *a, const float *b) {
  const float c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  return std::atan2(std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]), a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) * 180 / TFP_PI;
}
static TfpQ yaw_q(float yaw) { return tfp_q(0, 0, std::sin(yaw / 2), std::cos(yaw / 2)); }

int main() {
  W.build(HERE); // the world, as camera_smooth_main sets it up
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  const float LOOK = .2f;
  // ---- 1. The dive-swap of soak 60 (19:34:53.811): out of the native view the game's direction is 29 degrees off the
  // one shown the frame before, and 39.5 off again on the way back in. At 8, 16.7 and 33 ms a frame. ----
  for (double dt : {8.0, 16.7, 33.0}) {
    fresh(true, true, true);
    Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    standing(g, 12, dt, false); standing(g, 30, dt, true); standing(g, 4, dt, true, 2);
    float before[3], now[3], last[3], game[3];
    shown_forward(g, before);
    CHECK(std::fabs(before[0] + std::sin(LOOK)) < 1e-4f && std::fabs(before[1] - std::cos(LOOK)) < 1e-4f); // the rig eye's forward
    tfp_camera_trace_edge();
    { TfpCameraTraceRow drop[64]; (void)tfp_camera_trace_read(drop, 64); }
    // the ordinary camera, the game's rotation 29 degrees to the left of what was shown
    const TfpQ left = tfp_q_normalised(tfp_q_mul(yaw_q(LOOK), about_up(29)));
    game[0] = -std::sin(LOOK + 29 * TFP_PI / 180); game[1] = std::cos(LOOK + 29 * TFP_PI / 180); game[2] = 0;
    std::memcpy(last, before, 12);
    float game_camera[3] = {};
    int frames = 0; bool monotone = true, slow = true; float left_over = 29, first_turn = -1;
    for (double t = 0; t < 400; t += dt, ++frames) {
      g.hip(2); std::memcpy(g.s.argument_rotation, &left, 16);
      if (frames == 0) std::memcpy(game_camera, g.s.argument, 12);
      g.frame(dt);
      shown_forward(g, now);
      const float off = degrees_apart(now, game), turned = degrees_apart(last, now);
      if (frames == 0) first_turn = turned;
      if (off > left_over + 1e-2f) monotone = false;
      if (turned > TFP_TURN_RATE_DEG_S * (float)dt / 1000 + .05f) slow = false;
      if (t >= 218 + dt) { const TfpQ plain = tfp_q_normalised(left), got = handed(); CHECK(std::memcmp(&plain, &got, 16) == 0); } // over: the game's own, bit for bit
      left_over = off; std::memcpy(last, now, 12);
    }
    CHECK(first_turn >= 0 && first_turn < .05f); // the frame of the change is shown the way the frame before it was (main: 29 degrees)
    CHECK(monotone && slow && left_over < 1e-3f);
    // the rows say it: the carry on every frame of the ease, and on the frame of the change the probe
    TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
    int edges = 0; float carried = 30; bool falling = true;
    for (uint32_t i = 0; i < count; ++i) {
      if (rows[i].kind != 4) continue;
      if (rows[i].turn_edge != 0) {
        ++edges;
        CHECK(std::fabs(rows[i].turn_edge - 29) < .05f && std::fabs(rows[i].turn_carry - 29) < .05f);
        CHECK(degrees_apart(rows[i].game_turn, game) < .05f && degrees_apart(rows[i].turn, before) < .05f);
        CHECK(rows[i].game_cam_known && std::memcmp(rows[i].game_cam, game_camera, 12) == 0); // where the game put its own camera
      }
      if (rows[i].turn_carry > carried + 1e-3f) falling = false;
      carried = rows[i].turn_carry;
    }
    CHECK(edges == 1 && falling && carried == 0);
    // back into the native view, the rig eye's direction 39.5 degrees off (the argument turned in the eye's frame)
    const TfpQ local = about_up(-39.5f);
    game[0] = -std::sin(LOOK - 39.5f * TFP_PI / 180); game[1] = std::cos(LOOK - 39.5f * TFP_PI / 180);
    frames = 0; monotone = slow = true; left_over = 68.5f; first_turn = -1;
    for (double t = 0; t < 500; t += dt, ++frames) {
      g.native(0); std::memcpy(g.s.argument_rotation, &local, 16);
      g.frame(dt);
      shown_forward(g, now);
      const float off = degrees_apart(now, game), turned = degrees_apart(last, now);
      if (frames == 0) first_turn = turned;
      if (off > left_over + 1e-2f) monotone = false;
      if (turned > TFP_TURN_RATE_DEG_S * (float)dt / 1000 + .05f) slow = false;
      if (t >= 514 + dt) { const TfpQ plain = tfp_q_normalised(local), got = handed(); CHECK(std::memcmp(&plain, &got, 16) == 0); }
      left_over = off; std::memcpy(last, now, 12);
    }
    CHECK(first_turn >= 0 && first_turn < .05f && monotone && slow); // 68.5 degrees: 514 ms
    for (double t = 0; t < 100; t += dt) { g.native(0); std::memcpy(g.s.argument_rotation, &local, 16); g.frame(dt); }
    shown_forward(g, now); CHECK(degrees_apart(now, game) < 1e-3f);
    { const TfpQ plain = tfp_q_normalised(local), got = handed(); CHECK(std::memcmp(&plain, &got, 16) == 0); }
    g.end();
  }
  // ---- 2. A mouse turn during the ease goes through: the game's direction turning 90 degrees a second from the
  // change on; what is shown is off it by what the still run carried at the same instant. ----
  {
    float off[2][40] = {}; int n[2] = {0, 0};
    for (int turning = 0; turning < 2; ++turning) {
      fresh(true, true);
      clock_ms = 800000;
      Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
      standing(g, 12, 16.7, false); standing(g, 30, 16.7, true);
      for (int i = 0; i < 30; ++i) {
        const float mouse = turning ? 90.f * (float)(i * 16.7) / 1000 : 0;
        const TfpQ q = tfp_q_normalised(tfp_q_mul(yaw_q(LOOK), about_up(29 + mouse)));
        const float a = LOOK + (29 + mouse) * TFP_PI / 180, game[3] = {-std::sin(a), std::cos(a), 0};
        g.hip(2); std::memcpy(g.s.argument_rotation, &q, 16); g.frame(16.7);
        float now[3]; shown_forward(g, now);
        off[turning][n[turning]++] = degrees_apart(now, game);
      }
      g.end();
    }
    CHECK(n[0] == n[1] && off[0][0] > 28.9f);
    for (int i = 0; i < n[0]; ++i) CHECK(std::fabs(off[0][i] - off[1][i]) < .02f);
  }
  // ---- 3. No difference at the change (a swap on foot): every rotation handed on is the game's own, normalised, as
  // before round 49, bit for bit; and a difference of 0.4 degrees is shown as it is. ----
  for (float apart : {0.f, .4f}) {
    fresh(true, true, true);
    Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
    standing(g, 12, 16.7, false);
    int different = 0, carried = 0;
    const TfpQ world = tfp_q_normalised(tfp_q_mul(yaw_q(LOOK), about_up(apart)));
    for (int round = 0; round < 3; ++round) {
      for (int i = 0; i < 20; ++i) { g.native(i > 15 ? 2 : 0); g.frame(16.7); const TfpQ plain = tfp_q_normalised(tfp_q_identity()), got = handed(); different += std::memcmp(&plain, &got, 16) != 0; }
      for (int i = 0; i < 20; ++i) { g.hip(2); std::memcpy(g.s.argument_rotation, &world, 16); g.frame(16.7); const TfpQ plain = tfp_q_normalised(world), got = handed(); different += std::memcmp(&plain, &got, 16) != 0; }
    }
    TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
    for (uint32_t i = 0; i < count; ++i) carried += rows[i].turn_carry != 0 || rows[i].turn_edge != 0;
    CHECK(different == 0 && carried == 0);
    g.end();
  }
  // ---- 4. Over 120 degrees is another view: shown as it is. ----
  {
    fresh(true, true);
    Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
    standing(g, 12, 16.7, false); standing(g, 30, 16.7, true);
    const TfpQ far = tfp_q_normalised(tfp_q_mul(yaw_q(LOOK), about_up(150)));
    g.hip(2); std::memcpy(g.s.argument_rotation, &far, 16); g.frame(16.7);
    const TfpQ got = handed(); CHECK(std::memcmp(&far, &got, 16) == 0);
    g.end();
  }
  // ---- 5. The orphan's eye (soak 62 #13 melee, 19:45:25.709; soak 60 19:38:41.799): in the orphaned native frames the
  // camera node's world matrix is not the view (117 and 90 degrees off the rows either side). The eye was built 0.18 m
  // along it: 0.255 m from the head camera's own at 90 degrees, so the first head-camera frame wanted another point (a
  // jump taken) and the veil saw the head in front of the eye and pushed. Now along the direction the frame is shown
  // with, the rig eye's axes times the argument: the same point, nothing taken, nothing veiled, and the rows say turn=. ----
  for (float off : {90.f, 117.f, 36.f}) {
    fresh(true, true, true);
    Game g; g.eye[1] = -.5f; g.eye[2] = .13f; // the weapon's eye: behind and above the head
    g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
    tfp_camera_set_veil(true);
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    g.native(0x40); g.frame(33.3);
    tfp_camera_set_orphan(true); tfp_camera_continuity_event(); tfp_camera_trace_edge();
    const float a = LOOK + off * TFP_PI / 180, c = std::cos(a), n = std::sin(a);
    const float turned[12] = {c, n, 0, 0, -n, c, 0, 0, 0, 0, 1, 0};
    for (int i = 0; i < 26; ++i) {
      if (i == 0) g.eye[1] += .28f;
      if (i == 1) { g.eye[1] += .19f; g.eye[2] -= .09f; }
      g.native(0x40); std::memcpy(g.s.camera_rotation, turned, sizeof turned); g.frame(33.3);
    }
    TfpCameraOrphanStats stats{}; tfp_camera_orphan_stats(&stats);
    CHECK(stats.frames == 26 && stats.refused == 0);
    CHECK(g.from_head_eye() < 1e-3f && std::fabs(g.ahead_of_head() - .18f) < 1e-3f); // the head camera's own point
    const float look[3] = {-std::sin(LOOK), std::cos(LOOK), 0};
    int orphans = 0, heads = 0, taken = 0, veiled = 0, said = 0;
    auto read_rows = [&]() {
      TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
      for (uint32_t i = 0; i < count; ++i) {
        const bool orphan = rows[i].kind == 6 && rows[i].source == 1, head = rows[i].kind == 4 && rows[i].source == 1;
        orphans += orphan; heads += head;
        if (head) { taken += (rows[i].what & (TFP_CAMERA_TRACE_JUMP | TFP_CAMERA_TRACE_CUT)) != 0; veiled += rows[i].veil[0] != 0 || rows[i].veil[1] != 0 || rows[i].veil[2] != 0; }
        if (orphan || head) said += degrees_apart(rows[i].turn, look) < .05f && degrees_apart(rows[i].view, look) < .05f;
      }
    };
    read_rows();
    CHECK(orphans >= 10 && said == orphans); // the orphan's rows say which way they were shown (zeros until round 49)
    const TfpCameraVeilStats before = []{ TfpCameraVeilStats v{}; tfp_camera_veil_stats(&v); return v; }();
    g.measure_from_here(); tfp_camera_trace_edge();
    float worst = 0;
    for (int i = 0; i < 8; ++i) { g.hip(0x40); g.frame(33.3); tfp_camera_veil_tick(i == 1); worst = std::fmax(worst, g.from_head_eye()); }
    CHECK(worst < 1e-3f && g.travel < 1e-3f); // the game's exit: the head camera wants the point the orphan had
    const TfpCameraVeilStats after = []{ TfpCameraVeilStats v{}; tfp_camera_veil_stats(&v); return v; }();
    CHECK(after.moved == before.moved && after.largest_m == before.largest_m); // the veil had nothing to move
    read_rows();
    CHECK(heads == 8 && taken == 0 && veiled == 0 && said == orphans + heads);
    g.end();
  }
  std::printf("camera turn through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
