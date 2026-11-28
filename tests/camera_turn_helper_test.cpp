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
  // ---- 6. In a dive (tfp_camera_set_dive): the Stable style's eye damping is let go on 40 ms and nothing new is damped,
  // as in a clamber; Bodycam is as before. The head camera, the head dropping 0.9 m in 0.3 s (a dive's launch) and then
  // going on level: the lag behind the head's own eye. [0] Stable, not told; [1] Stable, told at the launch; [2] and
  // [3] Bodycam with the camera damping option at 150 ms, not told and told. ----
  {
    float lag[4][60] = {}; float sent_at[4][60][4] = {}; int n[4] = {0, 0, 0, 0}; int dives[4] = {0, 0, 0, 0};
    for (int run = 0; run < 4; ++run) {
      fresh(true, true, true, run >= 2 ? 150.f : 0.f, run >= 2);
      clock_ms = 600000;
      Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
      standing(g, 30, 16.7, false);
      for (int i = 0; i < 50; ++i) {
        if (i == 12 && (run & 1)) tfp_camera_set_dive(true);
        if (i == 12) tfp_camera_trace_edge();
        const float drop = i < 12 ? 0 : std::fmin(.9f, .05f * (i - 11));
        g.pose(1, 2 + .1f * std::fmax(0, i - 11), 0, LOOK, 1.5f - drop); g.hip(0); g.frame(16.7);
        lag[run][n[run]] = g.from_head_eye(); std::memcpy(sent_at[run][n[run]], sent.position, 16); ++n[run];
      }
      TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
      for (uint32_t i = 0; i < count; ++i) dives[run] += rows[i].dive;
      tfp_camera_set_dive(false);
      g.end();
    }
    float most[4] = {0, 0, 0, 0}, late[4] = {0, 0, 0, 0};
    for (int run = 0; run < 4; ++run) for (int i = 12; i < 50; ++i) { most[run] = std::fmax(most[run], lag[run][i]); if (i >= 20) late[run] = std::fmax(late[run], lag[run][i]); }
    std::printf("  a dive in the head camera: Stable %.3f m behind the head at most (%.3f from 130 ms on), told %.3f (%.3f); Bodycam %.3f, told %.3f\n",
                most[0], late[0], most[1], late[1], most[2], most[3]);
    CHECK(late[0] > .15f);                    // Stable as it was: the eye up to its 0.25 m behind a head that drops
    // told: what was carried is let go on 40 ms and nothing new is damped. (What is left is not the eye damping's: the
    // head camera's own smoothing of the head, retail's, 0.06 m at most.)
    CHECK(most[1] < .061f && late[1] < .061f && late[0] - late[1] > .2f);
    CHECK(most[2] > .03f && std::memcmp(sent_at[2], sent_at[3], sizeof sent_at[2]) == 0); // Bodycam: the same bytes, told or not
    CHECK(dives[0] == 0 && dives[1] > 0 && dives[3] > 0); // and the rows say it
  }
  // ---- 7. Full aim keeps the sights on the eye: nothing is carried in a frame of full aim. Into full aim straight
  // from the head camera with the direction 39.5 degrees off: shown as the game gives it, at once; and a carry under
  // way when full aim begins ends there. Out of full aim into the head camera the carry runs as anywhere. ----
  {
    fresh(true, true, true);
    Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
    standing(g, 12, 16.7, false); standing(g, 30, 16.7, true);
    const TfpQ world = tfp_q_normalised(tfp_q_mul(yaw_q(LOOK), about_up(29))), local = about_up(-39.5f), plain = tfp_q_normalised(local);
    tfp_camera_trace_edge();
    for (int i = 0; i < 3; ++i) { g.hip(2); std::memcpy(g.s.argument_rotation, &world, 16); g.frame(16.7); } // a carry of 29 degrees under way
    { float now[3]; shown_forward(g, now); const float game[3] = {-std::sin(LOOK + 29 * TFP_PI / 180), std::cos(LOOK + 29 * TFP_PI / 180), 0}; CHECK(degrees_apart(now, game) > 20); }
    int different = 0;
    for (int i = 0; i < 10; ++i) { g.native(0, true); std::memcpy(g.s.argument_rotation, &local, 16); g.frame(16.7); const TfpQ got = handed(); different += std::memcmp(&plain, &got, 16) != 0; }
    CHECK(different == 0);
    TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
    int carried = 0; for (uint32_t i = 0; i < count; ++i) if (rows[i].kind == 6) carried += rows[i].turn_carry != 0 || rows[i].turn_edge != 0;
    CHECK(count > 5 && carried == 0);
    // and out of it: the head camera's first frame is shown the way full aim's last was
    float before[3], now[3]; shown_forward(g, before);
    g.hip(2); std::memcpy(g.s.argument_rotation, &world, 16); g.frame(16.7); shown_forward(g, now);
    CHECK(degrees_apart(before, now) < .05f);
    g.end();
  }
  // ---- 8. The check's blocker: a pan through a change out of the head camera into the native view. That change comes
  // a whole frame after the last head-camera frame, so the game's direction is off the one last shown by one frame of
  // the player's own turn. With the cameras agreeing nothing may be carried: every rotation handed on is the game's
  // own, bit for bit (main's), and the picture turns the same in every frame (it stood still for one: 1.67, 0.00,
  // 1.76, 1.90 degrees at 100 degrees a second). At 100 and 300 degrees a second, 16.7 and 33 ms a frame, in both
  // styles. ----
  for (float rate : {100.f, 300.f}) for (double dt : {16.7, 33.0}) for (bool bodycam : {true, false}) {
    fresh(true, true, true, 0, bodycam);
    float look = LOOK; const float step = rate * (float)dt / 1000;
    Game g; g.pose(1, 2, 0, look); g.look(look, 0); g.hip(); g.begin();
    standing(g, 12, dt, false);
    float last[3], now[3]; bool have = false; int different = 0, uneven = 0, carried = 0;
    for (int i = 0; i < 44; ++i) {
      look += step * TFP_PI / 180; g.look(look, 0);
      if (i < 20) g.hip(2); else g.native(0);
      if (i == 20) tfp_camera_trace_edge(); // the rows either side of the change
      TfpQ plain; std::memcpy(&plain, g.s.argument_rotation, 16); plain = tfp_q_normalised(plain);
      g.frame(dt);
      const TfpQ got = handed(); different += std::memcmp(&plain, &got, 16) != 0;
      shown_forward(g, now);
      if (have) uneven += std::fabs(degrees_apart(last, now) - step) > .01f;
      std::memcpy(last, now, 12); have = true;
    }
    TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
    for (uint32_t i = 0; i < count; ++i) carried += rows[i].turn_carry != 0 || rows[i].turn_edge != 0;
    CHECK(count >= 10 && different == 0 && uneven == 0 && carried == 0);
    g.end();
  }
  // ---- 9. The same pan with the cameras really 29 degrees apart at the change (the rig eye's direction 29 degrees
  // off the head camera's): the 29 degrees are carried and eased, not 29 and a frame of the pan; the frame of the
  // change turns by the pan's own step (no stand-still, no cut), what is still carried falls every frame, and the pan
  // goes on under it: when the ease is over the frames are the game's own again, bit for bit. ----
  for (float rate : {100.f, 300.f}) for (double dt : {16.7, 33.0}) {
    fresh(true, true, true);
    float look = LOOK; const float step = rate * (float)dt / 1000;
    Game g; g.pose(1, 2, 0, look); g.look(look, 0); g.hip(); g.begin();
    standing(g, 12, dt, false);
    float last[3], now[3], game[3], at_change = -1, left_over = 30; bool have = false, falling = true, slow = true; int different = 0;
    const int frames = 20 + (int)(400 / dt);
    for (int i = 0; i < frames; ++i) {
      look += step * TFP_PI / 180;
      if (i < 20) { g.look(look, 0); g.hip(2); }
      else { g.look(look - 29 * TFP_PI / 180, 0); g.native(0); } // the rig eye 29 degrees to the right of the pan
      if (i == 20) tfp_camera_trace_edge();
      const float a = look - (i < 20 ? 0 : 29 * TFP_PI / 180); game[0] = -std::sin(a); game[1] = std::cos(a); game[2] = 0;
      g.frame(dt);
      shown_forward(g, now);
      if (i == 20) at_change = degrees_apart(last, now);
      if (i >= 20) {
        const float off = degrees_apart(now, game);
        if (off > left_over + 1e-2f) falling = false;
        if (left_over - off > TFP_TURN_RATE_DEG_S * (float)dt / 1000 + .05f) slow = false;
        left_over = off;
        if ((i - 20) * dt >= 218 + dt) { const TfpQ plain = tfp_q_normalised(tfp_q_identity()), got = handed(); different += std::memcmp(&plain, &got, 16) != 0; }
      }
      std::memcpy(last, now, 12); have = true;
    }
    CHECK(have && std::fabs(at_change - step) < .05f); // the pan's own step in the frame of the change (was 0; main: 29 less the step)
    CHECK(falling && slow && left_over < 1e-3f && different == 0);
    TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
    int edges = 0; for (uint32_t i = 0; i < count; ++i) if (rows[i].turn_edge != 0) { ++edges; CHECK(std::fabs(rows[i].turn_edge - 29) < .05f); }
    CHECK(edges == 1);
    g.end();
  }
  // ---- 10. Round 55, THE DIRECTION SHOWN (soak 67 #29, 11:13:22.390: a leave with a carry of 62.8 degrees). In the
  // game the context's axes (basis(), what the head camera's eye is built along) are the game's own new direction in
  // the frame of the leave, and the direction shown the frame before in every frame after it. So the eye of the leave's
  // frame went 0.18 m along a direction 63 degrees off the one shown, and the next frame's along the one shown: a
  // sideways flick of about 0.49 m x 2 sin(31.5) in the log, there and back. Played here with the context set the way
  // the game sets it: the drawn eye's step may change across the view by 0.03 m at most around the leave, at 16.7 and
  // 33 ms a frame, in both styles, the veil and the push armed or not; and the push and the veil go along the view
  // shown (the head is behind the drawn eye along it). ----
  for (double dt : {16.7, 33.3}) for (bool bodycam : {true, false}) for (bool veiled : {false, true}) {
    fresh(true, true, true, 0, bodycam);
    if (veiled) { tfp_camera_set_veil(true); tfp_camera_set_clear(true); }
    Game g; g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    standing(g, 12, dt, false); standing(g, 30, dt, true); standing(g, 4, dt, true, 2);
    auto context_from = [&](TfpQ q) { // the context's axis rows (right, forward, up) from a rotation in the world
      const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1}; float r[3], f[3], u[3];
      tfp_q_rotate(q, x, r); tfp_q_rotate(q, y, f); tfp_q_rotate(q, z, u);
      for (int i = 0; i < 3; ++i) { g.s.context[i] = r[i]; g.s.context[4 + i] = f[i]; g.s.context[8 + i] = u[i]; }
    };
    const TfpQ game_q = tfp_q_normalised(tfp_q_mul(yaw_q(LOOK), about_up(63)));
    float eyes[40][3], views[40][3]; int n = 0; float worst = 0, behind = -1e9f;
    for (int i = 0; i < 30; ++i) {
      if (i == 0) context_from(game_q); else context_from(handed()); // the game's new one at the leave, then the one shown
      g.hip(2); std::memcpy(g.s.argument_rotation, &game_q, 16);
      g.frame(dt);
      if (veiled) tfp_camera_veil_tick(i == 1);
      std::memcpy(eyes[n], g.shown, 12); shown_forward(g, views[n]); ++n;
      // the head node from the drawn eye along the level view shown: behind it (the push and the veil go that way)
      const float h = std::sqrt(views[n - 1][0] * views[n - 1][0] + views[n - 1][1] * views[n - 1][1]);
      const float ahead = ((g.s.head[0] - g.shown[0]) * views[n - 1][0] + (g.s.head[1] - g.shown[1]) * views[n - 1][1]) / h;
      behind = std::fmax(behind, ahead);
    }
    for (int k = 1; k + 1 < n; ++k) {
      float change[3]; for (int i = 0; i < 3; ++i) change[i] = (eyes[k + 1][i] - eyes[k][i]) - (eyes[k][i] - eyes[k - 1][i]);
      const float h = std::sqrt(views[k][0] * views[k][0] + views[k][1] * views[k][1]);
      worst = std::fmax(worst, std::fabs(change[0] * views[k][1] / h - change[1] * views[k][0] / h));
    }
    std::printf("  a leave carrying 63 degrees at %.1f ms (%s%s): the step changed across the view by %.3f m at most; the head %.3f m in front of the eye at most\n", dt, bodycam ? "Bodycam" : "Stable", veiled ? ", veil and push" : "", worst, behind);
    CHECK(worst <= .03f);
    CHECK(behind <= -.10f); // the head stays behind the drawn eye along the view shown (d19fd57: 0.03 m behind)
    g.end();
  }
  // ---- 11. Round 55: with nothing carried (the direction the same at every change of camera) every eye handed on,
  // every rotation and every Trace row's eye, wanted eye, veil, push, head_shown and direction are what d19fd57 gave,
  // bit for bit: a digest of them, taken with the helper of d19fd57 (TFP_CAMERA_HELPER_C=<d19fd57's camera_helper.c>
  // tests/run-camera-turn-helper.sh prints it as "digest"). Swaps on foot through the native view and back, the
  // body moving in the head camera, the veil and the push armed, both styles, Stable with its damping. No veil is open at an entry
  // and none opens over 0.15 m: those are what round 55 changes. ----
  {
    uint64_t digest = 1469598103934665603ull; int carried = 0, veils = 0;
    auto mix = [&](const void *data, size_t length) { const uint8_t *b = static_cast<const uint8_t *>(data); for (size_t i = 0; i < length; ++i) { digest ^= b[i]; digest *= 1099511628211ull; } };
    auto rows_into = [&]() {
      TfpCameraTraceRow rows[64]; std::memset(rows, 0, sizeof rows); const uint32_t count = tfp_camera_trace_read(rows, 64);
      for (uint32_t i = 0; i < count; ++i) {
        const TfpCameraTraceRow &r = rows[i];
        mix(&r.kind, 4); mix(r.eye, 12); mix(r.wanted, 12); mix(&r.step, 4); mix(&r.correction, 4); mix(r.damp, 12); mix(r.view, 12);
        mix(r.veil, 12); mix(r.turn, 12); mix(r.veil_head, 8); mix(r.head_shown, 8); mix(&r.clear, 4); mix(&r.turn_carry, 4); mix(&r.turn_edge, 4); carried += r.turn_carry != 0 || r.turn_edge != 0; veils += r.veil[0] != 0 || r.veil[1] != 0 || r.veil[2] != 0;
      }
    };
    for (bool bodycam : {true, false}) for (double dt : {16.7, 33.3}) {
      fresh(true, true, true, bodycam ? 0.f : 350.f, bodycam);
      tfp_camera_set_veil(true); tfp_camera_set_clear(true);
      Game g; g.eye[1] = 0; g.eye[2] = .13f; // the weapon's eye level with the head: the veil moves it 0.09 m at a leave
      g.pose(1, 2, 0, LOOK); g.look(LOOK, 0); g.hip(); g.begin();
      CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
      tfp_camera_trace_edge();
      for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 24; ++i) { g.native(i > 18 ? 2 : 0); g.frame(dt); mix(sent.position, 16); const TfpQ q = handed(); mix(&q, 16); }
        rows_into(); tfp_camera_trace_edge();
        for (int i = 0; i < 30; ++i) {
          g.pose(1, 2 + .01f * i, 0, LOOK, 1.5f - .002f * i);
          g.hip(i < 6 ? 2 : 0); g.frame(dt); tfp_camera_veil_tick(i == 1);
          mix(sent.position, 16); const TfpQ q = handed(); mix(&q, 16);
        }
        rows_into(); tfp_camera_trace_edge();
      }
      g.end();
    }
    std::printf("  digest %016llx (%d rows carried, %d veiled)\n", static_cast<unsigned long long>(digest), carried, veils);
    CHECK(carried == 0 && veils > 0);
    CHECK(digest == 0x0e42c7547a15410cull); // d19fd57's
  }
  std::printf("camera turn through the helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
