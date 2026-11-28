// Round 44: the head camera's eye at a throw, through the camera helper on the fake game memory of
// tests/camera_smooth_test.cpp (whose scene and helpers these are; its own checks are not run here).
//
// The user, on 0.6.0-feel.58: "Still possible for the camera to fall too far behind the body when throwing items
// causing your back and cape to be seen." In the head camera the third-person body is drawn with only the head, torso
// and shoulders hidden; the swap's hold keeps the native eye, 0.18 to 0.31 m behind the head node, for the throw and
// 900 ms of mask 0 after it, then lets it go over 300 to 450 ms; the veil cleared the leave's two frames and let its
// move out over 300 ms, back behind the head. src/camera_clear.h keeps the eye in front of the head instead.
//
//   camera_throw_eye_test [DIR]             the tests; DIR: tests/reference/throw (the log replays are skipped without it)
//   camera_throw_eye_test --rewrite IN OUT  IN's Trace lines with the eye as this build shows it (for the harness's
//                                           matrix judge: tests/test_harness_throw.py)
//
// Built against a camera helper without round 44 (no TFP_CAMERA_EYE_HELD) it still builds, and its checks of the eye
// and of Show torso fail: that is main's camera.
#define main camera_smooth_main
#include "camera_smooth_test.cpp"
#undef main
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
extern "C" {
#include "camera_math.h"
}

static const float FRONT = .05f; // the least in front of the head an eye shown on foot may be (the matrix's measure)
static const double SETTLE_MS = 600; // head visibility's TORSO_SETTLE

static uint32_t eye_held_now() {
#ifdef TFP_CAMERA_EYE_HELD
  return tfp_camera_lock_eye_held_body();
#else
  return 0; // main: Show torso does not wait for the hold
#endif
}
// Along the level direction the frame is shown with (the rotation the helper handed the game; the scripts' frames are
// the ordinary camera's, unparented: the rotation is the world's). Until round 55 along the context's axes (g.ahead), the
// direction the head camera's eye was built along: the two part where a change of camera carries a turn
// (src/camera_turn.h), and from round 55 the eye is built along the one shown (src/camera_helper.c lock_turn_preview).
static float level_ahead(const Game &g) {
  TfpQ q; std::memcpy(&q, reinterpret_cast<void *>(W.native() + 64), 16);
  const float y[3] = {0, 1, 0}; float f[3]; tfp_q_rotate(tfp_q_normalised(q), y, f);
  const float n = std::sqrt(f[0] * f[0] + f[1] * f[1]);
  if (g.s.linked || !(n > 1e-4f)) return g.ahead_of_head();
  return ((g.shown[0] - g.s.head[0]) * f[0] + (g.shown[1] - g.s.head[1]) * f[1]) / n;
}

// What head visibility would do with Show torso on: the torso drawn once kind 4 with mask 0 has been steady for
// TORSO_SETTLE, and (round 44) the camera holds no eye of the body's (TfpHeadIo.eye_held).
struct Torso {
  double steady_since = -1;
  bool step(bool head_camera, uint32_t mask, double now) {
    if (!head_camera || mask != 0) { steady_since = -1; return false; }
    if (steady_since < 0) steady_since = now;
    return now - steady_since >= SETTLE_MS && eye_held_now() == 0;
  }
};

// ---- the synthetic throws: a weapon's native eye behind the head, a throw that leaves the native view ----
struct Shape {
  const char *name; double dt; bool bodycam; float rig_ahead, rig_up, head_height, pitch, walk, turn;
};
// From the three windows of tests/reference/throw (the head node from the eye before the veil, veil_head):
// b9c6 the user's throw, Bodycam, 9 ms a frame, 0.210 m behind the head and 0.125 above it, the view turning 24 degrees;
// 7a3c Stable, 33 ms, 0.176 m and 0.083; 5608 prone, Bodycam, 33 ms, 0.307 m and 0.140, looking 24 degrees up.
// rig_ahead is the rig eye from the head along the view; the native eye drawn is 0.01 m further (the argument).
static const Shape B9C6 = {"b9c6 standing", 9.0, true, -.22f, .095f, 1.5f, -.057f, 1.2f, .42f};
static const Shape A7A3C = {"7a3c Stable", 33.3, false, -.186f, .053f, 1.5f, -.104f, .8f, 0};
static const Shape P5608 = {"5608 prone", 33.3, true, -.317f, .11f, .4f, .41f, 0, 0};

struct Run {
  float least = 1e9f; double least_at = 0; int head_frames = 0, behind = 0; float most_change = 0;
  int torso_in_hold = 0, torso_after = 0; bool held_seen = false, held_after = false;
};
// The timeline from the leave (ms): mask 2 350 (the throw), mask 0 1600 (the hold lapses at 900 and is let go by 1350),
// mask 2 300 (the next item drawn: the eye glides back to the weapon's remembered eye), mask 0 900, mask 2 300 (the
// weapon), then the native view again for 600.
// The game's action tuning (src/mod_view.c: TFP_ACTION_EYE_IDLE_MS, the eye return).
static void game_tuning() {
  tfp_camera_set_action_tuning(0x22u, 900u, true);
#ifdef TFP_CAMERA_EYE_HELD
  tfp_camera_set_clear(true);
#endif
}
static void throw_run(const Shape &sh, Run &r) {
  fresh(true, true, true, 60, sh.bodycam); game_tuning();
  Game g; g.pose(1, 2, 0, .2f, sh.head_height); g.look(.2f, sh.pitch); g.hip(); g.begin();
  g.eye[0] = 0; g.eye[1] = sh.rig_ahead; g.eye[2] = sh.rig_up;
  CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
  tfp_camera_set_veil(true);
  double t = -400;
  float x = 1, y = 2;
  auto place = [&](double at) {
    const float heading = .2f;
    const float s = sh.walk * (float)(std::max(0.0, at + 400) / 1000);
    x = 1 - std::sin(heading) * s; y = 2 + std::cos(heading) * s;
    g.pose(x, y, 0, heading, sh.head_height);
    const float turn = at < 0 ? 0 : sh.turn * (float)std::min(1.0, at / 300);
    g.look(heading + turn, sh.pitch);
  };
  standing(g, 20, sh.dt, true);
  for (; t < -120; t += sh.dt) { place(t); g.native(0); g.frame(sh.dt); }
  for (; t < 0; t += sh.dt) { place(t); g.native(2); g.frame(sh.dt); }
  Torso torso;
  std::vector<float> shown;
  int frame = 0; double mask0_at = -1;
  const struct { double until; uint32_t mask; bool native; } plan[] = {{350, 2, false}, {1950, 0, false}, {2250, 2, false}, {3150, 0, false}, {3450, 2, false}, {4050, 0, true}};
  double from = 0;
  for (const auto &p : plan) {
    for (t = from; t < p.until; t += sh.dt, ++frame) {
      place(t);
      if (p.native) g.native(p.mask); else g.hip(p.mask);
      g.frame(sh.dt);
      if (frame == 0) tfp_camera_veil_tick(false); else if (frame == 1) tfp_camera_veil_tick(true); else tfp_camera_veil_tick(false);
      for (int i = 0; i < 3; ++i) shown.push_back(g.shown[i]);
      if (!p.native) {
        const float a = level_ahead(g);
        ++r.head_frames; if (a < FRONT) ++r.behind;
        if (a < r.least) { r.least = a; r.least_at = t; }
      }
      if (p.mask == 0 && !p.native && mask0_at < 0) mask0_at = t;
      if (p.mask != 0) mask0_at = -1;
      const bool drawn = torso.step(!p.native, p.mask, t);
      if (eye_held_now()) r.held_seen = true;
      // the hold and its release: 900 ms of mask 0 and 300 ms of the glide at least
      if (mask0_at >= 0 && t - mask0_at < 900 + 300 && drawn) ++r.torso_in_hold;
      if (mask0_at >= 0 && t - mask0_at > 900 + 450 + 60 && drawn) ++r.torso_after;
      if (mask0_at >= 0 && t - mask0_at > 900 + 450 + 60 && eye_held_now()) r.held_after = true;
    }
    from = p.until;
  }
  // the judge's measure (tools/harness/matrix.py): the shown eye's change of step, from the third head-camera frame
  // (the leave's two are the veil's) to the end, the native view's return included
  const int n = (int)shown.size() / 3;
  for (int i = 3; i + 1 < n; ++i) {
    float d = 0; for (int k = 0; k < 3; ++k) { const float c = shown[3 * (i + 1) + k] - 2 * shown[3 * i + k] + shown[3 * (i - 1) + k]; d += c * c; }
    r.most_change = std::fmax(r.most_change, std::sqrt(d));
  }
  g.end();
}

// ---- the replay of a window of the log: the body, the head and the native eye as the Trace rows give them ----
struct Row {
  std::string line; double stamp = 0, tick = 0; int kind = 0; bool head = false;
  float eye[3] = {}, root[3] = {}, axes[9] = {}, view[3] = {}, vh[2] = {}; bool has_vh = false; int off_foot = 0; bool written = false;
};
struct MaskAt { double stamp; uint32_t mask; };
static double stamp_ms(const std::string &line) {
  int h, m, s, ms;
  if (line.size() < 12 || std::sscanf(line.c_str(), "%d:%d:%d.%d", &h, &m, &s, &ms) != 4) return -1;
  return ((h * 60.0 + m) * 60 + s) * 1000 + ms;
}
static bool vec(const std::string &line, const char *key, float *out, int n) {
  const std::string k = std::string(" ") + key + "=(";
  const size_t at = line.find(k);
  if (at == std::string::npos) return false;
  const char *p = line.c_str() + at + k.size();
  for (int i = 0; i < n; ++i) { char *end; out[i] = std::strtof(p, &end); if (end == p) return false; p = end; }
  return true;
}
static double number(const std::string &line, const char *key) {
  const std::string k = std::string(" ") + key + "=";
  const size_t at = line.find(k);
  return at == std::string::npos ? NAN : std::strtod(line.c_str() + at + k.size(), nullptr);
}
struct Window { std::vector<std::string> lines; std::vector<Row> rows; std::vector<MaskAt> masks; bool bodycam = true; };
static bool read_window(const char *path, Window &w) {
  std::ifstream in(path);
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    w.lines.push_back(line);
    if (line.find(" Trace: ") != std::string::npos) {
      Row r; r.line = line; r.stamp = stamp_ms(line); r.tick = number(line, "tick"); r.kind = (int)number(line, "kind");
      r.head = line.find("source=head") != std::string::npos;
      if (!vec(line, "eye_world", r.eye, 3) || !vec(line, "root", r.root, 3) || !vec(line, "axes", r.axes, 9) || !vec(line, "view", r.view, 3) || !std::isfinite(r.tick)) continue;
      r.has_vh = vec(line, "veil_head", r.vh, 2);
      r.off_foot = (int)number(line, "off_foot");
      w.rows.push_back(r);
    } else if (line.find(" Hold: mask=") != std::string::npos) {
      w.masks.push_back({stamp_ms(line), (uint32_t)std::strtoul(line.c_str() + line.find("mask=") + 5, nullptr, 16)});
    } else if (line.find("Camera style: Stable") != std::string::npos) w.bodycam = false;
  }
  std::stable_sort(w.rows.begin(), w.rows.end(), [](const Row &a, const Row &b) { return a.tick < b.tick; });
  std::vector<Row> unique;
  for (const Row &r : w.rows) if (unique.empty() || r.tick - unique.back().tick > .01) unique.push_back(r);
  w.rows = unique;
  return !w.rows.empty();
}
static float yaw_of(const float *forward) { return std::atan2(-forward[0], forward[1]); }

struct Replayed { double tick; int kind; uint32_t mask; float shown[3], head[3], h[2]; bool on_foot; const Row *from; TfpCameraTraceRow trace; bool traced; };
struct ReplayResult {
  std::vector<Replayed> frames; float least = 1e9f; double least_tick = 0; int head_frames = 0, behind = 0, torso_in_hold = 0; bool stable = false;
};
// One window through the helper. The head node is taken fixed in the body root's frame where the first veiled row puts
// it (veil_head: the head from the eye before the veil, along the level view and up); rows with no head-camera frame
// between them for more than 3 frames are filled with frames of the last body and view (the trace stops half a second
// after an edge); the control mask is the last Hold line's before the frame.
static bool replay(const Window &w, ReplayResult &out) {
  const Row *vrow = nullptr;
  for (const Row &r : w.rows) if (r.has_vh && r.kind == 4) { vrow = &r; break; }
  if (!vrow) return false;
  // the head in the (yaw-only) root frame of the fake
  const float hn = std::sqrt(vrow->view[0] * vrow->view[0] + vrow->view[1] * vrow->view[1]);
  const float hh[2] = {vrow->view[0] / hn, vrow->view[1] / hn};
  const float head_world[3] = {vrow->eye[0] + hh[0] * vrow->vh[0], vrow->eye[1] + hh[1] * vrow->vh[0], vrow->eye[2] + vrow->vh[1]};
  const float yaw0 = yaw_of(vrow->axes + 3), c0 = std::cos(yaw0), s0 = std::sin(yaw0);
  const float d0[3] = {head_world[0] - vrow->root[0], head_world[1] - vrow->root[1], head_world[2] - vrow->root[2]};
  const float head_local[3] = {d0[0] * c0 + d0[1] * s0, -d0[0] * s0 + d0[1] * c0, d0[2]}; // right, forward, up
  // the log's clock against the helper's: a live line is written at or after its frame
  double offset = 1e18;
  for (const Row &r : w.rows) offset = std::min(offset, r.stamp - r.tick);
  auto mask_at = [&](double tick) { uint32_t m = 0; for (const MaskAt &k : w.masks) if (k.stamp - offset <= tick + 1) m = k.mask; return m; };
  out.stable = !w.bodycam;
  fresh(true, true, true, 60, w.bodycam); game_tuning();
  Game g;
  auto put = [&](const Row &r) {
    const float yaw = yaw_of(r.axes + 3), c = std::cos(yaw), s = std::sin(yaw);
    g.pose(r.root[0], r.root[1], r.root[2], yaw);
    g.s.head[0] = r.root[0] + head_local[0] * c - head_local[1] * s; g.s.head[1] = r.root[1] + head_local[0] * s + head_local[1] * c; g.s.head[2] = r.root[2] + head_local[2];
    const float vn = std::sqrt(r.view[0] * r.view[0] + r.view[1] * r.view[1]);
    g.look(std::atan2(-r.view[0], r.view[1]), std::atan2(r.view[2], vn));
  };
  put(w.rows[0]); g.hip(); g.begin();
  CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
  tfp_camera_set_veil(true);
  // the native view before the window: steady at the first row's eye
  auto native_eye = [&](const Row &r) {
    const float arg[3] = {.02f, .01f, .03f}; const float *axes[3] = {g.right, g.ahead, g.above};
    for (int k = 0; k < 3; ++k) { float v = 0; for (int i = 0; i < 3; ++i) v += (r.eye[i] - g.s.head[i]) * axes[k][i]; g.eye[k] = v - arg[k]; }
  };
  const Row &first = w.rows[0];
  put(first); native_eye(first);
  for (int i = 0; i < 20; ++i) { g.native(0); g.frame(33.3); }
  double dt_typical = 33.3;
  { std::vector<double> d; for (size_t i = 1; i < w.rows.size(); ++i) d.push_back(w.rows[i].tick - w.rows[i - 1].tick);
    std::sort(d.begin(), d.end()); if (!d.empty()) dt_typical = std::max(4.0, d[d.size() / 2]); }
  clock_ms = first.tick - dt_typical;
  int kind_before = 6, head_frame = 0; Torso torso; double mask0_at = -1;
  auto one = [&](const Row &r, double tick, bool filled) {
    put(r);
    const uint32_t mask = mask_at(tick);
    const int kind = r.kind == 6 && !r.head ? 6 : 4;
    if (kind == 6) { native_eye(r); g.native(mask); } else g.hip(mask);
    const double dt = std::max(1.0, tick - clock_ms);
    g.frame(dt);
    tfp_camera_trace_edge();
    Replayed f{}; f.tick = tick; f.kind = kind; f.mask = mask; std::memcpy(f.shown, g.shown, 12); std::memcpy(f.head, g.s.head, 12);
    const float n = std::sqrt(g.ahead[0] * g.ahead[0] + g.ahead[1] * g.ahead[1]); f.h[0] = g.ahead[0] / n; f.h[1] = g.ahead[1] / n;
    f.on_foot = r.off_foot == 0; f.from = filled ? nullptr : &r;
    TfpCameraTraceRow rows[8]; const uint32_t got = tfp_camera_trace_read(rows, 8);
    f.traced = got > 0; if (got) f.trace = rows[got - 1];
    if (kind == 4) {
      if (kind_before == 6) head_frame = 0;
      tfp_camera_veil_tick(head_frame == 1); ++head_frame;
      if (f.on_foot) {
        const float a = level_ahead(g); ++out.head_frames; if (a < FRONT) ++out.behind;
        if (a < out.least) { out.least = a; out.least_tick = tick; }
      }
    } else tfp_camera_veil_tick(false);
    if (kind == 4 && mask == 0) { if (mask0_at < 0) mask0_at = tick; } else mask0_at = -1;
    const bool drawn = torso.step(kind == 4, mask, tick);
    if (drawn && mask0_at >= 0 && tick - mask0_at < 900 + 300) ++out.torso_in_hold;
    kind_before = kind;
    out.frames.push_back(f);
  };
  for (size_t i = 0; i < w.rows.size(); ++i) {
    const Row &r = w.rows[i];
    if (i > 0) {
      const Row &p = w.rows[i - 1];
      if (r.tick - p.tick > 3 * dt_typical && p.kind == 4 && p.head)
        for (double t = p.tick + dt_typical; t < r.tick - dt_typical / 2; t += dt_typical) one(p, t, true);
    }
    one(r, r.tick, false);
  }
  g.end();
  return true;
}

// ---- --rewrite: the window's Trace lines with the eye this build shows ----
static std::string format_row(const Row &like, const TfpCameraTraceRow &r, double stamp) {
  char head[96] = "0";
#ifdef TFP_CAMERA_EYE_HELD
  if (r.head_known) std::snprintf(head, sizeof head, "(%.4f %.4f)", (double)r.head_shown[0], (double)r.head_shown[1]);
  const double push = r.clear;
#else
  const double push = 0;
#endif
  char veil[96] = "0";
  if (r.veil[0] != 0 || r.veil[1] != 0 || r.veil[2] != 0) std::snprintf(veil, sizeof veil, "(%.5f %.5f %.5f)", (double)r.veil[0], (double)r.veil[1], (double)r.veil[2]);
  char veil_head[64] = "0";
  if (r.veil_head[0] != 0 || r.veil_head[1] != 0) std::snprintf(veil_head, sizeof veil_head, "(%.4f %.4f)", (double)r.veil_head[0], (double)r.veil_head[1]);
  const long ms = (long)std::llround(stamp) % 86400000;
  char text[2048];
  std::snprintf(text, sizeof text, "%02ld:%02ld:%02ld.%03ld Trace: t=%.1f kind=%u eye_world=(%.5f %.5f %.5f) step=%.4f wanted=(%.5f %.5f %.5f) correction=%.4f lease=0x0 pose_armed=0 entry_ease=%.2f source=%s"
                " root=(%.5f %.5f %.5f) axes=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) looked=0 taken=- quiet=0 tick=%.3f off_foot=%d damped=0 damp=(0.00000 0.00000 0.00000)"
                " view=(%.5f %.5f %.5f) sock=0.00000 arms=0 eye_edge=0 veil=%s turn=(0.0000 0.0000 0.0000) veil_head=%s head_shown=%s clear=%.4f",
                ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000, (double)r.ms, r.kind, (double)r.eye[0], (double)r.eye[1], (double)r.eye[2], (double)r.step,
                (double)r.wanted[0], (double)r.wanted[1], (double)r.wanted[2], (double)r.correction, (double)r.entry_ease, r.source == 1 ? "head" : "native",
                (double)like.root[0], (double)like.root[1], (double)like.root[2], (double)like.axes[0], (double)like.axes[1], (double)like.axes[2], (double)like.axes[3],
                (double)like.axes[4], (double)like.axes[5], (double)like.axes[6], (double)like.axes[7], (double)like.axes[8], r.tick, like.off_foot,
                (double)r.view[0], (double)r.view[1], (double)r.view[2], veil, veil_head, head, push);
  return text;
}
// The rows of the replay are the helper's, so its own clock; they are written with the log's (the row's tick turned
// back by the offset between the two). Rows of the fake's world are moved back into the log's: the replay put the
// body where the log had it, so only the eye and the veil differ. Filled frames are written before the row after them.
static int rewrite(const char *in, const char *out_path) {
  Window w;
  if (!read_window(in, w)) { std::printf("%s: no Trace rows\n", in); return 2; }
  ReplayResult r;
  if (!replay(w, r)) { std::printf("%s: no veiled head-camera row to place the head from\n", in); return 2; }
  double offset = 1e18; for (const Row &row : w.rows) offset = std::min(offset, row.stamp - row.tick);
  std::ofstream out(out_path);
  size_t next = 0;
  const Row *last = nullptr;
  for (const std::string &line : w.lines) {
    if (line.find(" Trace: ") == std::string::npos) { out << line << "\n"; continue; }
    const double tick = number(line, "tick");
    while (next < r.frames.size() && r.frames[next].tick <= tick + .01) {
      const Replayed &f = r.frames[next++];
      const Row *like = f.from ? f.from : last;
      if (f.from) last = f.from;
      if (like && f.traced) {
        TfpCameraTraceRow row = f.trace; row.tick = f.tick;
        out << format_row(*like, row, f.from ? f.from->stamp : f.tick + offset) << "\n";
      }
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  W.build(HERE); // the world, as camera_smooth_main sets it up
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  if (argc == 4 && std::string(argv[1]) == "--rewrite") return rewrite(argv[2], argv[3]);
  // ---- 1. Throws shaped as the three windows: every head-camera frame on foot shows the eye FRONT or more in front
  // of the head node, the leave's veiled frames included; nothing jumps over the judge's 0.05 m after the leave, the
  // native view's return included; Show torso waits for the hold and its release, and draws the torso after them. ----
  for (const Shape *sh : {&B9C6, &A7A3C, &P5608}) {
    Run r; throw_run(*sh, r);
    std::printf("%-14s head-camera frames %d, behind %d, least in front %+.3f m at %.0f ms; largest change %.3f m; torso in the hold %d, after %d\n",
                sh->name, r.head_frames, r.behind, r.least, r.least_at, r.most_change, r.torso_in_hold, r.torso_after);
    CHECK(r.head_frames > 100);
    CHECK(r.behind == 0 && r.least >= FRONT);
    CHECK(r.most_change < .05f);
    CHECK(r.torso_in_hold == 0);
#ifdef TFP_CAMERA_EYE_HELD
    CHECK(r.held_seen && !r.held_after && r.torso_after > 0);
#endif
  }
  // ---- 2. The windows of the log (tests/reference/throw), replayed through the helper. ----
  if (argc >= 2) {
    // the camera style of each session (its "Camera style:" lines are outside the windows)
    const struct { const char *name; bool bodycam; } names[] = {{"feel58-b9c6-L11795-12010.log", true}, {"soak-7a3c-L17118-17225.log", false}, {"soak-5608-L18433-18515.log", true}};
    for (const auto &named : names) {
      const char *name = named.name;
      const std::string path = std::string(argv[1]) + "/" + name;
      Window w; ReplayResult r;
      CHECK(read_window(path.c_str(), w));
      w.bodycam = named.bodycam;
      CHECK(replay(w, r));
      std::printf("%-30s %s: head-camera frames on foot %d, behind %d, least in front %+.3f m; torso in the hold %d\n", name, r.stable ? "Stable" : "Bodycam",
                  r.head_frames, r.behind, r.least, r.torso_in_hold);
      CHECK(r.head_frames >= 10);
      CHECK(r.behind == 0 && r.least >= FRONT);
      CHECK(r.torso_in_hold == 0);
    }
  } else std::printf("the log replays: skipped (no directory given)\n");
  // ---- 3. A swap in the middle of a dive (round 26's 0.43 to 0.45 m): the body has left the camera, not a held eye;
  // nothing is pushed, as the veil moves nothing over TFP_CAMERA_VEIL_MOST. ----
  {
    fresh(true, false); game_tuning();
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.47f; g.eye[2] = .05f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    standing(g, 30, 33.3, true); standing(g, 4, 33.3, true, 2);
    const float held = g.ahead_of_head();
    g.hip(2); g.frame(33.3); tfp_camera_veil_tick(false);
    CHECK(std::fabs(g.ahead_of_head() - held) < .002f);
    for (int i = 0; i < 10; ++i) { g.hip(2); g.frame(33.3); tfp_camera_veil_tick(i == 0); CHECK(std::fabs(g.ahead_of_head() - held) < .002f); }
    g.end();
  }
  // ---- 4. Off foot or down in a run (a dive or a body thrown after the throw; the ragdoll): nothing is pushed in
  // those frames but what was is eased out (off foot) or dropped (down); on foot again it is eased in. ----
  {
    fresh(true, false); game_tuning();
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .1f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    standing(g, 30, 33.3, true); standing(g, 4, 33.3, true, 2);
    for (int i = 0; i < 6; ++i) { g.hip(2); g.frame(33.3); tfp_camera_veil_tick(i == 1); }
    const float before = g.ahead_of_head();
#ifdef TFP_CAMERA_EYE_HELD
    CHECK(std::fabs(before - TFP_CAMERA_VEIL_CLEAR) < .003f);
#endif
    g.s.state = 50; // off foot
    float last = before, most = 0;
    for (int i = 0; i < 12; ++i) { g.hip(2); g.frame(33.3); most = std::fmax(most, std::fabs(g.ahead_of_head() - last)); last = g.ahead_of_head(); }
    CHECK(most <= .041f);                       // eased: at most TFP_CAMERA_CLEAR_STEP_M a frame
    CHECK(last < -.2f);                         // and then the held eye, as before round 44
    g.s.state = 36; // on foot again
    most = 0; float prev = last;
    for (int i = 0; i < 12; ++i) { g.hip(2); g.frame(33.3); most = std::fmax(most, std::fabs(g.ahead_of_head() - prev)); prev = g.ahead_of_head(); }
    CHECK(most <= .041f);
#ifdef TFP_CAMERA_EYE_HELD
    CHECK(std::fabs(prev - TFP_CAMERA_VEIL_CLEAR) < .003f);
#endif
    g.end();
  }
  // ---- 5. Head visibility not active (the veil not armed): nothing is pushed (the whole body is drawn anyway). ----
  {
    fresh(true, false); game_tuning();
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .1f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    standing(g, 30, 33.3, true); standing(g, 4, 33.3, true, 2);
    const float held = g.ahead_of_head();
    for (int i = 0; i < 8; ++i) { g.hip(2); g.frame(33.3); CHECK(std::fabs(g.ahead_of_head() - held) < .002f); }
    g.end();
  }
  // ---- 6. The veil and its let-out (round 26), the Scythe's eye 0.21 m behind the head: the leave's two veiled frames
  // and every frame after them TFP_CAMERA_VEIL_CLEAR in front; the let-out ends at the head plus the clearance, not at
  // the held eye. With the switch off (clear_off.txt), round 26's let-out back to the held eye. ----
  for (int on = 0; on < 2; ++on) {
    fresh(true, false); tfp_camera_set_action_tuning(0x22u, 900u, true);
#ifdef TFP_CAMERA_EYE_HELD
    tfp_camera_set_clear(on != 0);
#endif
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .13f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    standing(g, 30, 33.3, true); standing(g, 4, 33.3, true, 2);
    const float held = g.ahead_of_head();
    float least = 1, last = 0;
    for (int i = 0; i < 20; ++i) {
      g.hip(2); g.frame(33.3); tfp_camera_veil_tick(i == 1);
      if (i < 2) CHECK(std::fabs(g.ahead_of_head() - TFP_CAMERA_VEIL_CLEAR) < .002f);
      least = std::fmin(least, g.ahead_of_head()); last = g.ahead_of_head();
    }
    if (on) CHECK(least > TFP_CAMERA_VEIL_CLEAR - .002f && std::fabs(last - TFP_CAMERA_VEIL_CLEAR) < .002f);
    else CHECK(std::fabs(last - held) < .002f && held < -.2f);
    g.end();
  }
  // ---- 7. Down (the ragdoll flag with the body off foot): the push is dropped in the first such frame, as the veil's
  // let-out is; the frames of a body that is down are as before round 44. ----
  {
    fresh(true, false); game_tuning();
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    g.eye[0] = 0; g.eye[1] = -.22f; g.eye[2] = .1f;
    CHECK(tfp_camera_set_weapon_key(g.gen, 0x1111) == 1);
    tfp_camera_set_veil(true);
    standing(g, 30, 33.3, true); standing(g, 4, 33.3, true, 2);
    const float held = g.ahead_of_head();
    for (int i = 0; i < 6; ++i) { g.hip(2); g.frame(33.3); tfp_camera_veil_tick(i == 1); }
    g.s.state = 50; World::put<uint8_t>(W.a[A_OWNER] + 0x547844, 0x14);
    g.hip(2); g.frame(33.3);
    CHECK(std::fabs(g.ahead_of_head() - held) < .002f);
    World::put<uint8_t>(W.a[A_OWNER] + 0x547844, 0x04); g.s.state = 36;
    g.end();
  }
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
