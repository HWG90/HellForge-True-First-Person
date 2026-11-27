// Continuity of the presented eye through the camera helper itself (src/camera_helper.c, beyond retail, only in
// frames the camera lock holds), on the fake game memory of the differential test (this build's layout). Every
// frame is one call of the hook as the game's call site makes it; what is judged is the eye the game would draw:
// the forwarded position for the ordinary camera, and the rig eye's transform applied to the forwarded
// parent-local position for the native view. Also: the edge trace, and that nothing is read or changed without
// the lock. No retail source needed. Linux (process_vm_readv).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>
#define _Static_assert static_assert
extern "C" {
#include "camera_helper.h"
}
#include "diff_camera_helper_world.h"
using namespace dt;

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; std::printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

static World W;
static double clock_ms = 1000;
static uint64_t reads;
static struct { float position[4]; bool own_buffer, seen; } sent;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  ++reads;
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static bool host_readable_span(void *, uint64_t, size_t) { return true; }
static double host_now(void *) { return clock_ms; }
static bool host_true_slot(void *, uint64_t, uint64_t) { return true; }
static bool host_executable(void *, uint64_t) { return true; }
static bool host_cas(void *, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint64_t *>(slot), expected, desired); return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) { std::memcpy(out, data, length); return true; }
static bool host_pin(void *, uint64_t) { return true; }
static void host_lock(void *, int) {}
static void host_forward(void *, uint64_t, void *camera, uint32_t, const float *point) {
  sent.seen = true; sent.own_buffer = reinterpret_cast<uint64_t>(point) != W.native();
  std::memcpy(sent.position, point, 16);
  if (reinterpret_cast<uint64_t>(camera) == W.a[A_CAMERA]) { // the camera node takes the position
    std::memcpy(reinterpret_cast<void *>(W.a[A_CAM_LOCALS] + 0x24), point, 12);
    std::memcpy(reinterpret_cast<void *>(W.a[A_CAM_WORLDS] + 0x30), point, 12);
  }
}

struct Game {
  Scene s;
  float right[3] = {1, 0, 0}, ahead[3] = {0, 1, 0}, above[3] = {0, 0, 1};
  float eye[3] = {0, .1f, .05f};      // the rig eye from the head: right, ahead, above
  uint64_t gen = 0;
  float shown[3] = {}, body[3] = {}, step = 0, change = 0, travel = 0; bool have = false; // the drawn eye, in the body's frame
  float world_step = 0, world_change = 0; // the drawn eye's step in the world, and the most it changed from one frame to the next
  bool forwarded_own = false;
  void pose(float x, float y, float z, float yaw, float head_height = 1.5f) {
    const float c = std::cos(yaw), n = std::sin(yaw);
    const float m[16] = {c, n, 0, 0, -n, c, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    std::memcpy(s.root, m, sizeof m);
    s.head[0] = x + .03f * c - .06f * n; s.head[1] = y + .03f * n + .06f * c; s.head[2] = z + head_height;
  }
  // The body root alone, the head and the rig left where they are (the game re-seating the root by itself).
  void root_only(float x, float y, float z) { s.root[12] = x; s.root[13] = y; s.root[14] = z; }
  void look(float yaw, float pitch) {
    const float c = std::cos(yaw), n = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    right[0] = c; right[1] = n; right[2] = 0; ahead[0] = -n * cp; ahead[1] = c * cp; ahead[2] = sp; above[0] = n * sp; above[1] = -c * sp; above[2] = cp;
    for (int i = 0; i < 3; ++i) { s.context[i] = right[i]; s.context[4 + i] = ahead[i]; s.context[8 + i] = above[i]; s.camera_rotation[i] = right[i]; s.camera_rotation[4 + i] = ahead[i]; s.camera_rotation[8 + i] = above[i]; }
  }
  void rotation_from_look() { // yaw only in these scripts
    const float yaw = std::atan2(right[1], right[0]);
    s.argument_rotation[0] = 0; s.argument_rotation[1] = 0; s.argument_rotation[2] = std::sin(yaw / 2); s.argument_rotation[3] = std::cos(yaw / 2);
  }
  void hip(uint32_t mask = 0) {
    s.request[0] = 4; s.linked = false; s.mask = mask; s.aim = 0; s.camera_rotation_given = false;
    for (int i = 0; i < 3; ++i) s.argument[i] = s.head[i] - ahead[i] * 2.0f + above[i] * .4f;
    s.argument[3] = 0; rotation_from_look();
  }
  void rig_at_head() {
    for (int i = 0; i < 3; ++i) { s.rig[i] = right[i]; s.rig[4 + i] = ahead[i]; s.rig[8 + i] = above[i]; s.rig[12 + i] = s.head[i] + right[i] * eye[0] + ahead[i] * eye[1] + above[i] * eye[2]; }
    s.rig[3] = s.rig[7] = s.rig[11] = 0; s.rig[15] = 1;
  }
  void native(uint32_t mask = 0, bool aim = false) {
    s.request[0] = 6; s.linked = true; s.mask = mask; s.aim = aim ? 1 : 0; s.camera_rotation_given = true;
    rig_at_head();
    s.argument[0] = .02f; s.argument[1] = .01f; s.argument[2] = .03f; s.argument[3] = 0;
    // Round 49: in the native view the rotation argument is local to the rig eye, whose axes are the look's here: none.
    // (Until then this wrote the world's yaw, as hip() does; nothing read a native frame's rotation against the rig.)
    s.argument_rotation[0] = s.argument_rotation[1] = s.argument_rotation[2] = 0; s.argument_rotation[3] = 1;
  }
  // The request is the ordinary one again but the camera is still a child of the rig, which the game did not move.
  void gap() { const bool given = s.camera_rotation_given; hip(); s.linked = true; s.camera_rotation_given = given; }
  void begin() {
    TfpCameraConfig c{};
    c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA]; c.avatar_owner = W.a[A_OWNER];
    c.avatar_index = 0; c.avatar = 0x333; c.body = 0x400003; c.camera_unit = 0x400004; c.forward = .18f; c.up = .04f; c.smoothing_ms = 100; c.blend_ms = 120; c.max_gap_ms = 250;
    World::put<uint64_t>(W.slot(), W.native_value);
    W.apply(s, 0);
    if (tfp_camera_begin(&c, &gen) != 1 || tfp_camera_set_presentation(gen) != 1 || tfp_camera_set_cut_hold(gen, 15) != 1) {
      std::printf("the helper did not arm (error %u)\n", tfp_camera_error()); std::exit(2);
    }
  }
  void end() { tfp_camera_end(gen); have = false; step = change = travel = 0; }
  void measure_from_here() { change = travel = world_change = 0; }
  void frame(double dt) {
    clock_ms += dt; W.apply(s, 0); sent.seen = false;
    tfp_camera_setter_from(W.a[A_GAME] + HERE.call + 4, reinterpret_cast<void *>(W.a[A_CAMERA]), 0x400004, reinterpret_cast<const float *>(W.native()));
    if (!sent.seen) { std::printf("the hook did not forward\n"); std::exit(2); }
    forwarded_own = sent.own_buffer;
    float world[3];
    if (s.linked) for (int i = 0; i < 3; ++i) world[i] = s.rig[12 + i] + sent.position[0] * s.rig[i] + sent.position[1] * s.rig[4 + i] + sent.position[2] * s.rig[8 + i];
    else std::memcpy(world, sent.position, 12);
    float relative[3] = {};
    for (int axis = 0; axis < 3; ++axis) for (int i = 0; i < 3; ++i) relative[axis] += (world[i] - s.root[12 + i]) * s.root[axis * 4 + i];
    if (have) {
      const float d[3] = {relative[0] - body[0], relative[1] - body[1], relative[2] - body[2]};
      const float now_step = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      if (std::fabs(now_step - step) > change) change = std::fabs(now_step - step);
      if (now_step > travel) travel = now_step;
      step = now_step;
      const float w[3] = {world[0] - shown[0], world[1] - shown[1], world[2] - shown[2]};
      const float now_world = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
      if (std::fabs(now_world - world_step) > world_change) world_change = std::fabs(now_world - world_step);
      world_step = now_world;
    }
    have = true; std::memcpy(shown, world, 12); std::memcpy(body, relative, 12);
  }
  // The head camera's eye (c.forward 0.18 m along the level view direction, c.up 0.04 m), and the drawn eye against it.
  void head_eye(float *out) const {
    const float n = std::sqrt(ahead[0] * ahead[0] + ahead[1] * ahead[1]);
    out[0] = s.head[0] + ahead[0] / n * .18f; out[1] = s.head[1] + ahead[1] / n * .18f; out[2] = s.head[2] + .04f;
  }
  float from_head_eye() const { float t[3]; head_eye(t); return std::sqrt((shown[0] - t[0]) * (shown[0] - t[0]) + (shown[1] - t[1]) * (shown[1] - t[1]) + (shown[2] - t[2]) * (shown[2] - t[2])); }
  float ahead_of_head() const { // metres in front of the head node along the level view direction (negative: behind it)
    const float n = std::sqrt(ahead[0] * ahead[0] + ahead[1] * ahead[1]);
    return ((shown[0] - s.head[0]) * ahead[0] + (shown[1] - s.head[1]) * ahead[1]) / n;
  }
  float from_rig_eye() const { // how far the drawn eye is from where the game put it
    float d = 0;
    for (int i = 0; i < 3; ++i) { const float wanted = s.rig[12 + i] + .02f * s.rig[i] + .01f * s.rig[4 + i] + .03f * s.rig[8 + i]; d += (shown[i] - wanted) * (shown[i] - wanted); }
    return std::sqrt(d);
  }
};

// `bodycam`: the camera style (src/camera_damp.h). Every scenario before 19 is Bodycam, which is the camera before the style existed.
static void fresh(bool lock, bool smooth, bool trace = false, float damping_ms = 0, bool bodycam = true) {
  tfp_camera_reset_for_tests();
  TfpCameraHost host{};
  host.hook = reinterpret_cast<uint64_t>(&hook_marker); host.read = host_read; host.readable_span = host_readable_span; host.now = host_now;
  host.writable_slot = host_true_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock; host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_camera_set_host(&host);
  tfp_camera_set_lock(lock, 0, 0, 300, 0, 0, damping_ms, bodycam); // no kick: the rotation is not the subject
  tfp_camera_set_continuity(smooth, trace);
  clock_ms += 5000;
}
static void standing(Game &g, int frames, double dt, bool native_view, uint32_t mask = 0) {
  for (int i = 0; i < frames; ++i) { if (native_view) g.native(mask); else g.hip(mask); g.frame(dt); }
}

// One script played twice at the same instants, without camera damping and with it: what was forwarded each frame.
struct Twice {
  float at[2][400][4]; int frames[2];
  float apart(int i) const {
    float d = 0; for (int k = 0; k < 3; ++k) d += (at[1][i][k] - at[0][i][k]) * (at[1][i][k] - at[0][i][k]);
    return std::sqrt(d);
  }
  bool same(int from, int to) const { return frames[0] == frames[1] && to <= frames[0] && !std::memcmp(at[0][from], at[1][from], sizeof(float) * 4 * (to - from)); }
};
// `frame`: round 49's test switch (tfp_camera_set_damp_frame: the eye damped in the frame of tfp_damp_frame_step).
template <class Script> static void twice(Twice &t, bool smooth, float damping_ms, Script script, bool frame = false) {
  for (int damped = 0; damped < 2; ++damped) {
    fresh(true, smooth, false, damped ? damping_ms : 0.0f);
    tfp_camera_set_damp_frame(frame);
    clock_ms = 900000;
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    t.frames[damped] = 0;
    auto keep = [&]() { std::memcpy(t.at[damped][t.frames[damped]++], sent.position, 16); };
    script(g, keep);
    g.end();
  }
}

// Scenario 14: the farthest the presented eye went with damping 0, measured on the build before the option existed.
static const float RAW_PEAK = 0.0211488f, RAW_TRAVEL = 0.00265f; // farthest, and the most in one frame

int main() {
  W.build(HERE);
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);

  // ---- 1. The rig re-seated in one frame during a swap (mask 2): 0.218 m back, as in the game. ----
  for (int smooth = 1; smooth >= 0; --smooth) {
    fresh(true, smooth != 0);
    // The view looks down and across the body (the rig follows the view, the body root does not): the argument is
    // local to the rig, not to the body, and a conversion through the wrong one of the two shows here.
    Game g; g.pose(3, -2, .5f, .2f); g.look(.9f, -.5f); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 20, 33.3, true);
    CHECK(g.from_rig_eye() < 1e-4f); // the entry is over: the drawn eye is the game's
    g.measure_from_here();
    g.eye[1] -= .218f; g.native(2); g.frame(33.3);
    if (smooth) {
      CHECK(g.step < .01f && g.forwarded_own && std::fabs(g.from_rig_eye() - .218f) < .01f);
      standing(g, 14, 33.3, true, 2);
      CHECK(g.from_rig_eye() < 1e-4f && g.change < .03f && g.travel < .06f);
      TfpCameraContinuityStats stats{}; tfp_camera_continuity_stats(&stats);
      CHECK(stats.jumps == 1 && stats.largest_m > .2f && stats.largest_m < .23f);
    } else {
      CHECK(std::fabs(g.step - .218f) < .001f && !g.forwarded_own && g.from_rig_eye() < 1e-4f); // the switch off: the jump is shown
    }
    g.end();
  }
  // ---- 2. A stim (mask 0x40, which the retail sample refuses): 0.1 m then 0.216 m in two frames. ----
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    standing(g, 12, 16.7, false); standing(g, 30, 16.7, true);
    g.measure_from_here();
    g.eye[2] -= .1f; g.native(0x40); g.frame(16.7);
    CHECK(g.step < .01f);
    g.eye[1] -= .216f; g.native(0x40); g.frame(16.7);
    CHECK(g.step < .01f);
    standing(g, 30, 16.7, true, 0x40);
    CHECK(g.from_rig_eye() < 1e-4f && g.change < .008f);
    // ---- 3. Its end: the ordinary camera for one frame (no held eye: the native eye was not recorded under
    // the mask), then the native view again with the rig back at the weapon. One glide, no jump. ----
    g.measure_from_here();
    g.hip(); g.frame(16.7);
    CHECK(g.step < .01f && g.forwarded_own);
    g.eye[2] += .1f; g.eye[1] += .216f; g.native(); g.frame(16.7);
    CHECK(g.step < .02f);
    standing(g, 40, 16.7, true);
    CHECK(g.from_rig_eye() < 1e-4f && g.change < .01f);
    g.end();
  }
  // ---- 4. Leaving the native view and coming back (head camera for 12 frames): continuous both ways, with camera
  // damping too (scenario 14; the damped eye starts from the true one and is handed on as it was shown). ----
  for (int damped = 0; damped < 2; ++damped) {
    fresh(true, true, false, damped ? 180.0f : 0.0f);
    Game g; g.pose(1, 1, 0, .4f); g.look(.4f, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 20, 33.3, true);
    g.measure_from_here();
    standing(g, 12, 33.3, false, 2);
    g.eye[0] += .05f; standing(g, 20, 33.3, true);
    CHECK(g.from_rig_eye() < 1e-4f && g.change < .03f);
    g.end();
  }
  // ---- 5. The game's request replaced for one frame while the body moves at 6 m/s: the rig stands still for
  // that frame and comes back 0.3 m further on. Judged in the body's frame. ----
  for (int smooth = 1; smooth >= 0; --smooth) {
    fresh(true, smooth != 0);
    Game g; float x = 0; g.pose(x, 0, 0, 0); g.look(.7f, .4f); g.hip(); g.begin(); // looking up and to the side of the way
    for (int i = 0; i < 12; ++i) { x += .2f; g.pose(x, 0, 0, 0); g.hip(); g.frame(33.3); }
    for (int i = 0; i < 20; ++i) { x += .2f; g.pose(x, 0, 0, 0); g.native(); g.frame(33.3); }
    g.measure_from_here();
    x += .2f; g.pose(x, 0, 0, 0); g.gap(); g.frame(33.3);       // the rig is where it was
    const float gap_step = g.step;
    x += .2f; g.pose(x, 0, 0, 0); g.eye[1] += .3f; g.native(); g.frame(33.3);
    const float back_step = g.step;
    for (int i = 0; i < 16; ++i) { x += .2f; g.pose(x, 0, 0, 0); g.native(); g.frame(33.3); }
    if (smooth) CHECK(gap_step < .01f && back_step < .02f && g.change < .03f && g.from_rig_eye() < 1e-4f);
    else CHECK(std::fabs(gap_step - .2f) < .001f && back_step > .2f); // the switch off: 0.2 m behind, then 0.24 m in one frame
    g.end();
  }
  // ---- 6. A real teleport (5 m in a frame) and full aim: nothing is carried. ----
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 20, 33.3, true);
    g.eye[1] += 5; g.native(); g.frame(33.3);
    CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own);
    g.eye[1] -= 5; standing(g, 3, 33.3, true);
    g.eye[1] += .3f; g.native(0, true); g.frame(33.3);
    CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own);
    g.end();
  }
  // ---- 7. The trace: the frames before an edge and the 500 ms after it, each once. ----
  {
    fresh(true, true, true);
    Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    TfpCameraTraceRow rows[64];
    standing(g, 12, 33.3, false);
    CHECK(tfp_camera_trace_read(rows, 64) == 0); // no edge: nothing
    standing(g, 10, 33.3, true);
    tfp_camera_trace_edge();
    uint32_t count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 6 && rows[0].ms < -160 && rows[5].ms <= 0 && rows[5].kind == 6 && rows[5].source == 2);
    g.eye[1] -= .2f; g.native(2); g.frame(33.3);
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 1 && rows[0].ms > 30 && rows[0].ms < 36 && std::fabs(rows[0].correction - .2f) < .01f && rows[0].step < .01f);
    // the body root the eye was measured against, and what was done with the frame: looked for a jump, took one up
    CHECK(rows[0].root[0] == g.s.root[12] && rows[0].root[1] == g.s.root[13] && rows[0].root[2] == g.s.root[14]);
    CHECK(rows[0].what == (TFP_CAMERA_TRACE_LOOKED | TFP_CAMERA_TRACE_JUMP));
    // and that root's axes: without them a turn of the root cannot be told from a move of the eye
    for (int axis = 0; axis < 3; ++axis) for (int k = 0; k < 3; ++k) CHECK(rows[0].axes[axis * 3 + k] == g.s.root[axis * 4 + k]);
    CHECK(std::fabs(rows[0].axes[0] * rows[0].axes[0] + rows[0].axes[1] * rows[0].axes[1] + rows[0].axes[2] * rows[0].axes[2] - 1) < 1e-4f);
    float eye_distance = 0; for (int i = 0; i < 3; ++i) eye_distance += (rows[0].eye[i] - g.shown[i]) * (rows[0].eye[i] - g.shown[i]);
    CHECK(std::sqrt(eye_distance) < 1e-4f && std::fabs(rows[0].wanted[1] - rows[0].eye[1]) > .15f && rows[0].entry_ease == 1);
    standing(g, 30, 33.3, true, 2);
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 14 && rows[count - 1].ms <= 500 && rows[count - 1].ms > 466); // 500 ms after the edge, no more
    CHECK(tfp_camera_trace_read(rows, 64) == 0);
    standing(g, 3, 33.3, false, 2); // a change of camera: its first frame says kind 4, source 1, and a correction
    tfp_camera_trace_edge();
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 6 && rows[5].kind == 4 && rows[5].source == 1 && rows[2].kind == 6);
    CHECK(rows[2].what == 0 && rows[3].what == TFP_CAMERA_TRACE_LOOKED); // the change of kind opens the time jumps are looked for
    // tick: the frame's time on the helper's clock, which `ms` counts from the edge
    CHECK(rows[5].tick - rows[4].tick > 30 && rows[5].tick - rows[4].tick < 36 && std::fabs((rows[5].tick - rows[4].tick) - (rows[5].ms - rows[4].ms)) < .01);
    g.native(0, true); g.frame(33.3); // full aim: the row says the continuity was told to take nothing up
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 1 && (rows[0].what & TFP_CAMERA_TRACE_QUIET) && !(rows[0].what & TFP_CAMERA_TRACE_START));
    g.end();
    fresh(true, true, true); // the continuity's first frame says so, and only that one
    Game first; first.pose(0, 0, 0, 0); first.look(0, 0); first.hip(); first.begin();
    standing(first, 3, 33.3, true);
    tfp_camera_trace_edge();
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 3 && (rows[0].what & TFP_CAMERA_TRACE_START) && !(rows[1].what & TFP_CAMERA_TRACE_START) && !(rows[2].what & TFP_CAMERA_TRACE_START));
    first.end();
    fresh(true, true, false); // the trace switched off: nothing kept
    Game h; h.pose(0, 0, 0, 0); h.look(0, 0); h.hip(); h.begin();
    standing(h, 5, 33.3, false); tfp_camera_trace_edge(); standing(h, 5, 33.3, false);
    CHECK(tfp_camera_trace_read(rows, 64) == 0);
    h.end();
  }
  // ---- 9. Away from any event the rig eye's own motion is not a jump: a crouch (the eye 2.5 m/s down from one
  // frame to the next), a fast turn of the rig about the body, a one-frame move. Shown as the game put it. After
  // the binder's word (a lease or a hold begun or ended) a one-frame move is taken up, for 600 ms. ----
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, .3f); g.look(.3f, -.2f); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true); // the entry and its 600 ms are over
    for (int i = 0; i < 8; ++i) { g.eye[2] -= 2.5f * .0333f; g.native(); g.frame(33.3); CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own); }
    for (int i = 0; i < 8; ++i) { g.look(.3f + .5f * (i + 1), -.2f); g.native(); g.frame(33.3); CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own); }
    g.eye[1] -= .2f; g.native(); g.frame(33.3);
    CHECK(g.from_rig_eye() < 1e-4f && std::fabs(g.step - .2f) < .03f);
    TfpCameraContinuityStats stats{}; tfp_camera_continuity_stats(&stats);
    CHECK(stats.jumps == 0);
    standing(g, 15, 33.3, true);
    tfp_camera_continuity_event();
    standing(g, 5, 33.3, true);
    g.eye[1] += .2f; g.native(); g.frame(33.3); // 200 ms after the word: taken up
    CHECK(g.step < .01f && g.forwarded_own && std::fabs(g.from_rig_eye() - .2f) < .01f);
    standing(g, 15, 33.3, true);
    CHECK(g.from_rig_eye() < 1e-4f);
    tfp_camera_continuity_event();
    standing(g, 20, 33.3, true);
    g.eye[1] -= .2f; g.native(); g.frame(33.3); // 700 ms after the word: the game's own again
    CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own);
    tfp_camera_continuity_stats(&stats);
    CHECK(stats.jumps == 1);
    g.end();
  }
  // ---- 9b. The eye going back and forth every frame, away from any event (the game's unzoom gate flipping,
  // docs/design/specs/2026-11-23-gate-flip.md): counted for the "Alternation:" line and shown as the game put it;
  // nothing is held. Counted from the second return on and for 70 ms after the last move; with the binder's word the
  // frames count as looked at; in full aim nothing counts. ----
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, .3f); g.look(.3f, -.2f); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    TfpCameraAlternationStats seen{}; tfp_camera_alternation_stats(&seen);
    CHECK(seen.frames == 0 && seen.largest_m == 0);
    for (int i = 0; i < 12; ++i) { g.eye[1] += (i & 1) ? .2f : -.2f; g.native(); g.frame(33.3); CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own); }
    standing(g, 10, 33.3, true);
    tfp_camera_alternation_stats(&seen);
    CHECK(seen.frames == 12 && seen.looked == 0 && seen.fire == 0 && seen.kind == 6 && std::fabs(seen.largest_m - .2f) < .01f);
    tfp_camera_alternation_stats(&seen);
    CHECK(seen.frames == 12 && seen.largest_m == 0); // the largest move is the one since the last call
    tfp_camera_continuity_event();
    for (int i = 0; i < 12; ++i) { g.eye[1] += (i & 1) ? .2f : -.2f; g.native(); g.frame(33.3); }
    standing(g, 30, 33.3, true);
    tfp_camera_alternation_stats(&seen);
    CHECK(seen.frames == 24 && seen.looked == 12);
    g.end();
  }
  // ---- 9a. 500 ms after the word a one-frame move is still taken up (seen in the game 2026-11-22: walking out of
  // an emote, the rig re-seated 0.10 m in one frame half a second after the view came back). ----
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, .3f); g.look(.3f, -.2f); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    tfp_camera_continuity_event();
    standing(g, 15, 33.3, true);
    g.eye[1] += .1f; g.native(); g.frame(33.3);
    CHECK(g.step < .01f && g.forwarded_own);
    g.end();
  }
  // ---- 9b. A re-seat that takes three frames and begins on the last frame of the 600 ms (seen in the game
  // 2026-11-22: closing the stratagem list or a swap with the view pitched down, 0.08 m a frame for three frames,
  // 270 ms after the entry). The run that began inside the time is taken up to its end, not shown from its second
  // frame on. ----
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, .3f); g.look(.3f, -.2f); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    tfp_camera_continuity_event();
    standing(g, 18, 33.3, true);
    float worst = 0;
    for (int i = 0; i < 3; ++i) { g.eye[1] -= .05f; g.eye[2] += .065f; g.native(); g.frame(33.3); if (g.step > worst) worst = g.step; }
    CHECK(worst < .04f);
    TfpCameraContinuityStats stats{}; tfp_camera_continuity_stats(&stats);
    CHECK(stats.jumps == 3);
    for (int i = 0; i < 15; ++i) { g.native(); g.frame(33.3); if (g.step > worst) worst = g.step; }
    CHECK(worst < .045f && g.from_rig_eye() < 1e-4f); // 0.245 m caught up in 230 ms, 0.040 m a frame at most
    // and the time is not held open by it: a one-frame move after the run is the game's own again
    g.eye[1] += .2f; g.native(); g.frame(33.3);
    CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own);
    g.end();
  }
  // ---- 9c. Frames the view is shown are counted with the camera lock off too: the field-of-view lock is its own
  // toggle and follows "the view is shown", not "the camera lock held a frame". ----
  for (int lock = 0; lock < 2; ++lock) {
    fresh(lock != 0, true);
    Game g; g.pose(0, 0, 0, .3f); g.look(.3f, -.2f); g.hip(); g.begin();
    const uint64_t before = tfp_camera_view_frames(), held = tfp_camera_lock_frames();
    standing(g, 12, 33.3, false); standing(g, 12, 33.3, true);
    CHECK(tfp_camera_view_frames() - before == 24);
    CHECK(lock ? tfp_camera_lock_frames() - held == 24 : tfp_camera_lock_frames() == held);
    g.end();
  }
  // ---- 10. The entry under the lock is one mechanism. With continuity on the 140 ms eased entry does not run and
  // the change of camera is carried whole: 0.3 m and 0.6 m at 30 frames a second change no step by 0.035 m, and no
  // frame of it is read as a jump. With continuity off the eased entry runs as before (and is the rougher one). ----
  for (int far = 0; far < 2; ++far) {
    float change[2] = {0, 0};
    for (int smooth = 1; smooth >= 0; --smooth) {
      fresh(true, smooth != 0, true);
      Game g; g.pose(2, 1, 0, .1f); g.look(.5f, -.3f); g.hip(); g.begin();
      standing(g, 12, 33.3, false); standing(g, 20, 33.3, true); standing(g, 12, 33.3, false, 2);
      g.measure_from_here();
      g.eye[1] += far ? .6f : .3f; standing(g, 20, 33.3, true);
      change[smooth] = g.change;
      CHECK(g.from_rig_eye() < 1e-4f);
      if (smooth) {
        TfpCameraContinuityStats stats{}; tfp_camera_continuity_stats(&stats);
        CHECK(stats.jumps == 0 && stats.edges == 2 && g.change < .035f); // (the way out is held by the eye hold: nothing to take up)
        TfpCameraTraceRow rows[64]; tfp_camera_trace_edge();
        const uint32_t count = tfp_camera_trace_read(rows, 64);
        for (uint32_t i = 0; i < count; ++i) CHECK(rows[i].entry_ease == 1); // the eased entry never ran
      }
      g.end();
    }
    CHECK(change[1] < change[0]);
  }
  // The rig's own draw motion in the first frames of the native view (the logged rig eye after the entry of
  // 08:56:33.459: 0.038, 0.112, 0.067 m back, then 0.007 and 0.043 m forward, the head camera already in place).
  {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 20, 33.3, true); standing(g, 12, 33.3, false, 2);
    g.measure_from_here();
    const float moves[7] = {0, 0, -.038f, -.112f, -.067f, .007f, .043f};
    for (float move : moves) { g.eye[1] += move; g.native(); g.frame(33.3); }
    standing(g, 20, 33.3, true);
    CHECK(g.from_rig_eye() < 1e-4f && g.change <= .05f);
    g.end();
  }
  // ---- 11. The remembered eye. A native eye that moved 5 cm in a frame is being re-seated by the game; if the view
  // leaves within 200 ms, that eye is not what is remembered for its weapon. Seen through the swap glide: with the
  // other weapon in the hands and the first one coming up, the head camera glides to the first one's remembered eye.
  // late = 0: the view leaves 100 ms after the move, the eye from before the move is the one remembered.
  // late = 1: the eye stood still for 330 ms after the move: that is the weapon's eye now. Continuity off: the
  // glide is judged raw. ----
  for (int late = 0; late < 2; ++late) {
    fresh(true, false);
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    const uint32_t first = 0x1111, second = 0x2222;
    CHECK(tfp_camera_set_weapon_key(g.gen, first) == 1);
    standing(g, 12, 33.3, false); standing(g, 20, 33.3, true);
    float settled[3]; std::memcpy(settled, g.body, sizeof settled); // the first weapon's eye, in the body's frame
    standing(g, 6, 33.3, false, 2); standing(g, 20, 33.3, true);      // out and back: remembered
    g.eye[1] -= .3f; standing(g, late ? 10 : 3, 33.3, true, 2);      // the rig goes back 0.3 m in one frame
    float moved[3]; std::memcpy(moved, g.body, sizeof moved);
    CHECK(std::fabs(moved[1] - settled[1] + .3f) < .01f);
    standing(g, 3, 33.3, false, 2);
    CHECK(tfp_camera_set_weapon_key(g.gen, second) == 1);
    g.eye[1] += .3f; g.eye[0] += .08f; standing(g, 20, 33.3, true);  // the second weapon in the hands
    CHECK(tfp_camera_set_weapon_key(g.gen, first) == 1);
    standing(g, 30, 33.3, false, 2);                                 // the first one coming up: the glide, 1 s
    const float *wanted = late ? moved : settled, *other = late ? settled : moved;
    float to_wanted = 0, to_other = 0;
    for (int i = 0; i < 3; ++i) { to_wanted += (g.body[i] - wanted[i]) * (g.body[i] - wanted[i]); to_other += (g.body[i] - other[i]) * (g.body[i] - other[i]); }
    CHECK(std::sqrt(to_wanted) < .01f && std::sqrt(to_other) > .25f);
    g.end();
  }
  // ---- 12. A stim (seen in the game 2026-11-23, 0.6.0-feel.25). Mask 0x40: the game keeps its first-person request
  // for about 0.85 s after the mod asked for the exit, the gun goes to the body, and the rig's eye goes to the head in
  // two frames (0.28 m, then 0.19 m: to 0.21 m behind the head camera's eye, inside the helmet). The binder says so
  // (tfp_camera_set_orphan) on the tick the gun has left. Then: the eye presented goes to the head camera's point and
  // never back behind the head, no frame's step changes by more than the continuity allows, the game's exit (the
  // ordinary camera) finds the eye already there, and the native view's return is taken up as before.
  // stand_in 0: the binder never says so (0.6.0-feel.25): the eye follows the rig behind the head. ----
  for (int stand_in = 1; stand_in >= 0; --stand_in) {
    fresh(true, true, true);
    Game g; g.eye[1] = -.5f; g.eye[2] = .13f; // the weapon's eye: behind and above the head, as logged
    g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    g.native(0x40); g.frame(33.3); // the put-away's frame: the gun on the rig, the game's own view
    CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own);
    TfpCameraOrphanStats before{}; tfp_camera_orphan_stats(&before);
    CHECK(before.frames == 0 && before.refused == 0);
    if (stand_in) tfp_camera_set_orphan(true);
    tfp_camera_continuity_event(); tfp_camera_trace_edge();
    g.measure_from_here();
    float nearest = g.from_head_eye(), least_ahead = g.ahead_of_head(); bool retreat = false;
    CHECK(nearest > .6f && least_ahead < -.4f);
    for (int i = 0; i < 26; ++i) {
      if (i == 0) g.eye[1] += .28f;
      if (i == 1) { g.eye[1] += .19f; g.eye[2] -= .09f; }
      g.native(0x40); g.frame(33.3);
      if (g.from_head_eye() > nearest + 1e-4f || g.ahead_of_head() < least_ahead - 1e-4f) retreat = true;
      nearest = std::fmin(nearest, g.from_head_eye()); least_ahead = std::fmax(least_ahead, g.ahead_of_head());
      if (stand_in) CHECK(g.forwarded_own);
      if (stand_in && i >= 13) CHECK(g.from_head_eye() < 1e-3f); // there within 0.45 s
    }
    TfpCameraOrphanStats stats{}; tfp_camera_orphan_stats(&stats);
    TfpCameraTraceRow rows[64]; const uint32_t count = tfp_camera_trace_read(rows, 64);
    if (stand_in) {
      CHECK(!retreat && g.change < .035f && g.from_head_eye() < 1e-4f && std::fabs(g.ahead_of_head() - .18f) < 1e-3f);
      CHECK(stats.frames == 26 && stats.refused == 0 && std::fabs(stats.behind_m - .20f) < .01f);
      CHECK(count >= 10 && rows[count - 1].kind == 6 && rows[count - 1].source == 1); // the native request, the head's eye
    } else {
      CHECK(g.from_rig_eye() < 1e-4f && g.ahead_of_head() < 0 && stats.frames == 0 && stats.refused == 0); // in the helmet
      CHECK(count >= 10 && rows[count - 1].kind == 6 && rows[count - 1].source == 2);
    }
    if (stand_in) {
      g.measure_from_here();
      standing(g, 10, 33.3, false); // the game's exit: the ordinary camera, the head camera's own frames: the eye is there
      CHECK(g.travel < 1e-3f && g.from_head_eye() < 1e-3f);
      g.measure_from_here();
      tfp_camera_set_orphan(false);
      g.eye[1] = -.25f; g.eye[2] = .12f; standing(g, 20, 33.3, true); // the weapon is back
      CHECK(g.change < .035f && g.from_rig_eye() < 1e-4f);
    }
    g.end();
  }
  // The binder's word alone does not move a view that is the weapon's own: full aim is left to the game.
  {
    fresh(true, true);
    Game g; g.eye[1] = -.5f; g.eye[2] = .13f; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    tfp_camera_set_orphan(true);
    for (int i = 0; i < 20; ++i) { g.native(0, true); g.frame(33.3); }
    TfpCameraOrphanStats stats{}; tfp_camera_orphan_stats(&stats);
    CHECK(stats.frames == 0 && g.from_rig_eye() < 1e-4f);
    // A head that is not where a head can be (beyond 3 m from the unit root): refused, the frame is the native one.
    for (int i = 0; i < 20; ++i) { g.native(0x40); g.s.head[2] = 5; g.frame(33.3); }
    tfp_camera_orphan_stats(&stats);
    CHECK(stats.frames == 0 && stats.refused == 20 && g.from_rig_eye() < 1e-4f);
    g.end();
  }
  // Smooth eye off: the head's point at once (a change of camera is then shown as it is, as everywhere else).
  {
    fresh(true, false);
    Game g; g.eye[1] = -.5f; g.eye[2] = .13f; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 40, 33.3, true);
    tfp_camera_set_orphan(true);
    g.native(0x40); g.frame(33.3);
    CHECK(g.from_head_eye() < 1e-4f);
    g.end();
  }
  // Without the lock the binder's word changes nothing: the same forwarded bytes and the same number of reads.
  {
    struct Record { float position[4]; bool own; uint64_t reads; } record[2][80];
    int frames[2] = {0, 0};
    for (int on = 0; on < 2; ++on) {
      fresh(false, true, true);
      clock_ms = 700000;
      Game g; g.eye[1] = -.5f; g.eye[2] = .13f; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
      tfp_camera_set_orphan(on != 0);
      auto run = [&](int count, bool native_view, uint32_t mask) { for (int i = 0; i < count; ++i) { if (native_view) g.native(mask); else g.hip(mask); const uint64_t before = reads; g.frame(16.7);
        Record &r = record[on][frames[on]++]; std::memcpy(r.position, sent.position, 16); r.own = sent.own_buffer; r.reads = reads - before; } };
      run(15, false, 0); run(20, true, 0); g.eye[1] += .47f; run(25, true, 0x40); run(10, false, 0);
      TfpCameraOrphanStats stats{}; tfp_camera_orphan_stats(&stats);
      CHECK(stats.frames == 0 && stats.refused == 0);
      g.end();
    }
    CHECK(frames[0] == frames[1] && frames[0] == 70);
    int different = 0;
    for (int i = 0; i < frames[0]; ++i) if (std::memcmp(record[0][i].position, record[1][i].position, 16) || record[0][i].own != record[1][i].own || record[0][i].reads != record[1][i].reads) ++different;
    CHECK(different == 0);
  }
  // ---- 8. Without the lock the switches change nothing: the same forwarded bytes and the same number of
  // memory reads, frame for frame, with continuity and trace on or off. ----
  {
    struct Record { float position[4]; bool own; uint64_t reads; } record[2][120];
    int frames[2] = {0, 0};
    for (int on = 0; on < 2; ++on) {
      fresh(false, on != 0, on != 0);
      clock_ms = 500000; // the same instants in both runs
      Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
      auto keep = [&](uint64_t before) { Record &r = record[on][frames[on]++]; std::memcpy(r.position, sent.position, 16); r.own = sent.own_buffer; r.reads = reads - before; };
      auto run = [&](int count, bool native_view, uint32_t mask) { for (int i = 0; i < count; ++i) { if (native_view) g.native(mask); else g.hip(mask); const uint64_t before = reads; g.frame(16.7); keep(before); } };
      run(20, false, 0); run(20, true, 0); g.eye[1] -= .218f; run(20, true, 2); run(10, false, 2); g.eye[1] += .218f; run(20, true, 0);
      { g.gap(); const uint64_t before = reads; g.frame(16.7); keep(before); }
      run(10, true, 0);
      g.end();
    }
    CHECK(frames[0] == frames[1] && frames[0] == 101);
    int different = 0;
    for (int i = 0; i < frames[0]; ++i) if (std::memcmp(record[0][i].position, record[1][i].position, 16) || record[0][i].own != record[1][i].own || record[0][i].reads != record[1][i].reads) ++different;
    CHECK(different == 0);
  }
  // ---- 13. Head-camera frames inside the window after a change (mask 2, as in a swap). On foot: the head-frame jump
  // the detection was added for (0.15 m in one frame) is taken up. Off foot and not (yet) known to be down, as in
  // the first frames of a death that never sets the ragdoll flag: the head is thrown against the unit root, which
  // stays behind (0.1 m more every frame: 3 m/s, the head still within 3 m), and that is the body's own motion, not
  // a re-seated rig: the eye stays at the head. ----
  for (int thrown = 0; thrown < 2; ++thrown) {
    fresh(true, true);
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false); standing(g, 20, 33.3, true); standing(g, 20, 33.3, false);
    CHECK(g.from_head_eye() < 1e-3f);
    g.measure_from_here();
    float worst = 0;
    if (thrown) {
      g.s.state = 50; // off foot; no ragdoll flag
      for (int i = 0; i < 10; ++i) {
        g.pose(1, 2, 0, .2f, 1.5f - .1f * (i + 1)); g.hip(2); g.frame(33.3);
        worst = std::fmax(worst, g.from_head_eye());
      }
      CHECK(worst < .065f); // never left behind the head (0.06 m: the most the head camera's own smoothing lags)
    } else {
      g.pose(1, 2, 0, .2f, 1.35f);
      for (int i = 0; i < 24; ++i) { g.hip(2); g.frame(33.3); worst = std::fmax(worst, g.from_head_eye()); }
      CHECK(g.change < .05f && worst > .1f && g.from_head_eye() < 1e-3f); // 0.15 m in one frame: eased, and there in 0.8 s
    }
    std::printf("head frames, %s: step change %.3f m, farthest from the head's eye %.3f m\n", thrown ? "thrown off foot" : "a jump on foot", g.change, worst);
    g.end();
  }
  // ---- 14. Camera damping (the Locked view option, here as its time constant: 60 % is 180 ms). In head-camera
  // frames on foot the head jerks 0.05 m down and back within 100 ms (a half sine, 144 frames a second): the eye
  // presented moves clearly less with damping than without, and comes back to the head's eye. Without damping
  // every frame is what it was before the option existed (the numbers are those of the build before it). Nothing
  // else is damped: the native view, a body that is off foot, and anything without the lock give the same forwarded
  // bytes with damping as without. ----
  {
    const double dt = 1000.0 / 144;
    auto jerk = [&](Game &g, int i) { g.pose(1, 2, 0, .2f, 1.5f - (i < 15 ? .05f * std::sin(i * static_cast<float>(dt) / 100 * 3.14159265f) : 0)); };
    float peak[2][2] = {}, travel[2][2] = {};
    for (int smooth = 0; smooth < 2; ++smooth) for (int damped = 0; damped < 2; ++damped) {
      fresh(true, smooth != 0, false, damped ? 180.0f : 0.0f);
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
      standing(g, 12, dt, false); standing(g, 40, dt, true); standing(g, 300, dt, false); // 2 s: the way in from the native view is over
      CHECK(g.from_head_eye() < 1e-3f);
      float start[3]; std::memcpy(start, g.body, 12);
      g.measure_from_here();
      for (int i = 1; i <= 120; ++i) {
        jerk(g, i); g.hip(); g.frame(dt);
        const float moved = std::sqrt((g.body[0] - start[0]) * (g.body[0] - start[0]) + (g.body[1] - start[1]) * (g.body[1] - start[1]) + (g.body[2] - start[2]) * (g.body[2] - start[2]));
        peak[smooth][damped] = std::fmax(peak[smooth][damped], moved);
      }
      CHECK(g.from_head_eye() < 2e-3f); // back at the head's eye
      travel[smooth][damped] = g.travel;
      std::printf("head jerk 0.05 m, smooth eye %s, damping %s: the eye moved %.4f m at most, %.5f m in one frame\n", smooth ? "on" : "off", damped ? "60 %" : "0", peak[smooth][damped], g.travel);
      g.end();
    }
    for (int smooth = 0; smooth < 2; ++smooth) {
      CHECK(peak[smooth][0] > .02f && peak[smooth][1] > .001f && peak[smooth][1] < peak[smooth][0] * .5f);
      CHECK(travel[smooth][1] < travel[smooth][0] * .5f); // and no frame moves it half as far
      CHECK(std::fabs(peak[smooth][0] - RAW_PEAK) < 2e-6f && std::fabs(travel[smooth][0] - RAW_TRAVEL) < 1e-5f); // damping 0: as before the option
    }
    // What is not damped: the same bytes with and without.
    for (int scene = 0; scene < 3; ++scene) {
      static float record[2][160][4]; int frames[2] = {0, 0};
      for (int damped = 0; damped < 2; ++damped) {
        fresh(scene != 2, true, false, damped ? 300.0f : 0.0f);
        clock_ms = 700000; // the same instants in both runs
        Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
        auto keep = [&]() { std::memcpy(record[damped][frames[damped]++], sent.position, 16); };
        for (int i = 0; i < 12; ++i) { g.hip(); g.frame(dt); keep(); }
        if (scene == 0) { // the native view: the rig eye jerks
          for (int i = 0; i < 20; ++i) { g.native(); g.frame(dt); keep(); }
          for (int i = 1; i <= 60; ++i) { jerk(g, i); g.native(); g.frame(dt); keep(); }
        } else if (scene == 1) { // off foot (a dive, the first frames of a death): the head is followed exactly
          for (int i = 0; i < 20; ++i) { g.hip(); g.frame(dt); keep(); }
          g.s.state = 50;
          for (int i = 1; i <= 60; ++i) { jerk(g, i); g.hip(); g.frame(dt); keep(); }
        } else { // no lock
          for (int i = 1; i <= 60; ++i) { jerk(g, i); g.hip(); g.frame(dt); keep(); }
        }
        g.end();
      }
      CHECK(frames[0] == frames[1] && frames[0] > 70 && !std::memcmp(record[0], record[1], sizeof(float) * 4 * frames[0]));
    }
    // And on foot under the lock the same script does differ (the comparison above can tell).
    {
      static float record[2][160][4]; int frames[2] = {0, 0};
      for (int damped = 0; damped < 2; ++damped) {
        fresh(true, true, false, damped ? 300.0f : 0.0f);
        clock_ms = 700000;
        Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
        for (int i = 0; i < 32; ++i) { g.hip(); g.frame(dt); std::memcpy(record[damped][frames[damped]++], sent.position, 16); }
        for (int i = 1; i <= 60; ++i) { jerk(g, i); g.hip(); g.frame(dt); std::memcpy(record[damped][frames[damped]++], sent.position, 16); }
        g.end();
      }
      CHECK(!std::memcmp(record[0], record[1], sizeof(float) * 4 * 32) && std::memcmp(record[0], record[1], sizeof(float) * 4 * frames[0]));
    }
  }
  // ---- 15. What is damped is the head against the body root, not the eye's place in front of it. With a head that
  // is still, turning the view (the eye turns about the head with it, 0.18 m out) moves the presented eye exactly as
  // without damping; so does the body turning under a still view (a strafe start, walking backwards). ----
  {
    const double dt = 1000.0 / 144;
    static Twice t;
    twice(t, false, 180, [&](Game &g, auto keep) {
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
      for (int i = 1; i <= 40; ++i) { g.look(.2f + .04f * i, 0); g.hip(); g.frame(dt); keep(); } // 92 degrees in 0.28 s
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
    });
    CHECK(t.frames[0] == 120 && t.same(0, 120));
    float worst = 0;
    twice(t, false, 180, [&](Game &g, auto keep) {
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
      for (int i = 1; i <= 40; ++i) { g.pose(1, 2, 0, .2f + .04f * i); g.hip(); g.frame(dt); keep(); } // the body, the view still
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
    });
    for (int i = 0; i < t.frames[0]; ++i) worst = std::fmax(worst, t.apart(i));
    CHECK(t.frames[0] == 120 && t.frames[1] == 120 && worst < 1e-4f);
    std::printf("damping, the body turning 92 degrees under a still view: the eye at most %.5f m from the undamped one\n", worst);
  }
  // ---- 15b. Round 49, a test switch (tfp_camera_set_damp_frame, damp_frame_on.txt; off by default, and off everything
  // above and below is as it was). The frame the eye is damped in turns with the root only as far as the view does
  // (src/camera_damp.h tfp_damp_frame_step). The root alone turns 16.5 degrees a frame at 30 frames a second (the
  // get-up of soak 60) under a still view, and the head, 0.54 m from it, stands still in the world. Without damping
  // the eye moves only by the head camera's own smoothing, which works in the root's axes. Damped in the root's
  // axes the eye is swung further, by the lag those axes make of a head that did not move; damped in the frame it goes
  // no further than the undamped one. And a head that the turning root carries round with it does move in the world:
  // in the frame that is damped like any other move of the head (in the root's axes it is none: scenario 15), by no
  // more than the head went, and the eye comes to it. A mouse turn over a still head is exactly the undamped one. ----
  {
    const double slow = 1000.0 / 30, dt = 1000.0 / 144;
    static Twice t;
    float went[2][2] = {{0, 0}, {0, 0}}; // [frame switch][damped]: the farthest the eye went from where it stood
    for (int frame = 0; frame < 2; ++frame) {
      twice(t, false, 180, [&](Game &g, auto keep) {
        const float head[3] = {1.5f, 2.2f, 1.5f};
        std::memcpy(g.s.head, head, 12);
        for (int i = 0; i < 10; ++i) { g.hip(); g.frame(slow); keep(); }
        for (int i = 1; i <= 8; ++i) { g.pose(1, 2, 0, .2f + .288f * i); std::memcpy(g.s.head, head, 12); g.hip(); g.frame(slow); keep(); }
        for (int i = 0; i < 10; ++i) { g.hip(); g.frame(slow); keep(); }
      }, frame != 0);
      CHECK(t.frames[0] == 28 && t.frames[1] == 28);
      for (int damped = 0; damped < 2; ++damped) for (int i = 10; i < 28; ++i) {
        float d = 0; for (int k = 0; k < 3; ++k) d += (t.at[damped][i][k] - t.at[damped][9][k]) * (t.at[damped][i][k] - t.at[damped][9][k]);
        went[frame][damped] = std::fmax(went[frame][damped], std::sqrt(d));
      }
      std::printf("the root alone turning 132 degrees under a still view and a still head, the damping's frame %s: the eye went %.4f m at most, undamped %.4f m\n",
                  frame ? "on" : "off", went[frame][1], went[frame][0]);
    }
    CHECK(std::fabs(went[0][0] - went[1][0]) < 1e-6f);  // the switch changes nothing without damping
    CHECK(went[0][1] > went[0][0] + .03f);              // the root's axes: swung further than the undamped eye
    CHECK(went[1][1] <= went[1][0] + 1e-4f);            // the frame: no further
    twice(t, false, 180, [&](Game &g, auto keep) {
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
      for (int i = 1; i <= 40; ++i) { g.pose(1, 2, 0, .2f + .04f * i); g.hip(); g.frame(dt); keep(); } // the body, the view still
      for (int i = 0; i < 200; ++i) { g.hip(); g.frame(dt); keep(); }
    }, true);
    float worst = 0;
    for (int i = 0; i < t.frames[0]; ++i) worst = std::fmax(worst, t.apart(i));
    CHECK(t.frames[0] == 280 && t.frames[1] == 280 && worst > .01f && worst < .0671f * 1.6f && t.apart(279) < 1e-3f); // the head's arc: 0.067 m out, 92 degrees
    std::printf("the damping's frame on, the body turning 92 degrees under a still view, the head carried round: the eye at most %.5f m from the undamped one, %.5f m at the end\n", worst, t.apart(279));
    twice(t, false, 180, [&](Game &g, auto keep) {
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
      for (int i = 1; i <= 40; ++i) { g.look(.2f + .04f * i, 0); g.hip(); g.frame(dt); keep(); }
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
    }, true);
    CHECK(t.frames[0] == 120 && t.same(0, 120));
    tfp_camera_set_damp_frame(false);
  }
  // ---- 16. The way out of damping. A lag is carried (the head dropped 0.5 m in a frame: 0.10 m, the limit), then
  // the body leaves 'on foot' or full aim begins. The lag is not dropped in that frame: it is let go on 40 ms, is
  // under 1 cm within 150 ms and gone after that, and the head's own motion meanwhile is not damped again. ----
  for (int way = 0; way < 2; ++way) {
    const double dt = 1000.0 / 144;
    static Twice t;
    twice(t, false, 300, [&](Game &g, auto keep) {
      for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
      g.pose(1, 2, 0, .2f, 1.0f);
      for (int i = 0; i < 3; ++i) { g.hip(); g.frame(dt); keep(); }
      if (way == 0) g.s.state = 50; // off foot
      for (int i = 1; i <= 80; ++i) {
        g.pose(1, 2, 0, .2f, 1.0f - .004f * (i < 30 ? i : 30)); // and the head goes on down
        g.hip(); if (way == 1) g.s.aim = 1;
        g.frame(dt); keep();
      }
    });
    CHECK(t.frames[0] == 123 && t.frames[1] == 123 && t.same(0, 40));
    CHECK(std::fabs(t.apart(40) - .10f) < 1e-3f && t.apart(42) > .09f); // carried, at the limit
    CHECK(t.apart(43) > .07f && t.apart(43) < t.apart(42));              // the first frame out: eased, not dropped
    bool falling = true;
    for (int i = 44; i < 123; ++i) if (t.apart(i) > t.apart(i - 1) * .9f + 1e-6f) falling = false; // 6.9 ms of 40: 0.84 a frame
    CHECK(falling);
    CHECK(t.apart(42 + 21) < .01f); // 146 ms
    CHECK(t.same(42 + 40, 123));    // gone: every frame after is the undamped one
    std::printf("damping let go (%s): %.3f m carried, %.3f m in the first frame out, %.4f m after 146 ms\n", way ? "full aim" : "off foot", t.apart(42), t.apart(43), t.apart(63));
  }
  // ---- 17. Where the damping stands back, each gate on its own: the same forwarded bytes with damping as without
  // while the head jerks, in full aim, and in a camera of the lock that is not the ordinary one (kind 5). ----
  {
    const double dt = 1000.0 / 144;
    auto jerk = [&](Game &g, int i) { g.pose(1, 2, 0, .2f, 1.5f - (i < 15 ? .05f * std::sin(i * static_cast<float>(dt) / 100 * 3.14159265f) : 0)); };
    for (int gate = 0; gate < 2; ++gate) {
      static Twice t; bool head_camera = true;
      twice(t, false, 300, [&](Game &g, auto keep) {
        for (int i = 0; i < 32; ++i) { g.hip(); g.frame(dt); keep(); }
        for (int i = 1; i <= 60; ++i) {
          jerk(g, i); g.hip();
          if (gate == 0) g.s.aim = 1; else g.s.request[0] = 5;
          g.frame(dt); keep();
          if (!g.forwarded_own || tfp_camera_error() != 0) head_camera = false;
        }
      });
      CHECK(head_camera && t.frames[0] == 92 && t.same(0, 92));
    }
    // The damped eye is handed on as it was shown: the way into the native view starts from it. The head has
    // dropped 0.5 m (the eye is 0.10 m behind) when the native view begins; with the lag handed on the first native
    // frame is the eye where it was, without it the eye would be shown 0.10 m off for that frame. The drop in two
    // frames (0.24 m and 0.26 m), and in one. In one frame from rest it is a pose cut for the smooth eye (round 19, THE
    // CUT): the trace says so, the damping still runs under it (0.10 m, its limit), the eye is not moved in that frame
    // (the cut is taken up whole) and comes down on the curve, and the native view after it starts from where it is.
    for (int frames = 2; frames >= 1; --frames) for (int smooth = 0; smooth < 2; ++smooth) {
      fresh(true, smooth != 0, true, 300);
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
      TfpCameraTraceRow rows[128];
      standing(g, 12, dt, false); standing(g, 40, dt, true); standing(g, 300, dt, false);
      if (frames == 2) { g.pose(1, 2, 0, .2f, 1.26f); g.hip(); g.frame(dt); }
      tfp_camera_trace_edge(); tfp_camera_trace_read(rows, 128);
      g.measure_from_here();
      g.pose(1, 2, 0, .2f, 1.0f); g.hip(); g.frame(dt);
      tfp_camera_trace_edge();
      const uint32_t count = tfp_camera_trace_read(rows, 128);
      const bool said = count >= 1 && (rows[count - 1].what & TFP_CAMERA_TRACE_CUT) != 0;
      const float damp = count >= 1 ? std::sqrt(rows[count - 1].damp[0] * rows[count - 1].damp[0] + rows[count - 1].damp[1] * rows[count - 1].damp[1]
                                                + rows[count - 1].damp[2] * rows[count - 1].damp[2]) : 0;
      const float in_drop = g.step, off = g.from_head_eye();
      if (frames == 1 && smooth) {
        CHECK(said && std::fabs(damp - .10f) < 1e-3f);
        CHECK(in_drop < .01f && off > .4f);  // the cut not shown: the eye where it was, 0.5 m above the damped one
      } else {
        CHECK(!said);
        CHECK(std::fabs(off - .16f) < .005f); // 0.10 m of damping and the 0.06 m of the head camera's own smoothing
      }
      g.pose(1, 2, 0, .2f, 1.0f); g.hip(); g.frame(dt);
      g.measure_from_here();
      g.native(); g.frame(dt);
      CHECK(g.step < .02f);
      std::printf("damping handed on to the native view, the head down 0.5 m in %d frame%s, smooth eye %s: %.4f m in the frame of the drop%s, "
                  "%.3f m off the head camera's eye; %.4f m in the first native frame\n", frames, frames > 1 ? "s" : "", smooth ? "on" : "off", in_drop,
                  said ? " (cut)" : "", off, g.step);
      g.end();
    }
    // A frame that was not damped forgets the damping: after three native frames (21 ms, inside the pause the
    // low-pass would bridge) in which the head moved 0.05 m, the head camera's eye starts from the true one.
    {
      static Twice t;
      twice(t, false, 300, [&](Game &g, auto keep) {
        for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
        g.pose(1, 2, 0, .2f, 1.45f);
        for (int i = 0; i < 3; ++i) { g.native(); g.frame(dt); keep(); }
        for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
      });
      CHECK(t.frames[0] == 83 && t.same(0, 83));
    }
    // The limit, through the helper: the head 0.5 m away in one frame, the eye never more than 0.10 m behind it.
    {
      static Twice t; float worst = 0;
      twice(t, false, 300, [&](Game &g, auto keep) {
        for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); keep(); }
        g.pose(1, 2, 0, .2f, 1.0f);
        for (int i = 0; i < 20; ++i) { g.hip(); g.frame(dt); keep(); }
      });
      for (int i = 0; i < t.frames[0]; ++i) worst = std::fmax(worst, t.apart(i));
      CHECK(t.frames[0] == 60 && t.frames[1] == 60 && std::fabs(worst - .10f) < 1e-4f && std::fabs(t.apart(40) - .10f) < 1e-4f);
    }
  }
  // ---- 18. The body root re-seated by itself (seen in the soak of 2026-11-22, 0.6.0-feel.37, 19:45:12.826: a
  // backwards dive begun 170 ms after the view came back). The body walks backwards at 3 m/s in the native view; in
  // one frame the game puts the root 0.6 m further on while the head and the rig eye go on as they went, and one
  // frame later the rig eye follows by 0.44 m. Judged in the world, where the eye is drawn: the first of the two
  // frames is an ordinary frame (in feel.37 the drawn eye went the root's 0.6 m with it), the second is a re-seated
  // rig, taken up and eased. Smooth eye off: the game's own, the 0.44 m in one frame. ----
  for (int smooth = 1; smooth >= 0; --smooth) {
    fresh(true, smooth != 0, true);
    Game g; float y = 0, root_y = 0; g.pose(0, y, 0, 0); g.look(0, 0); g.hip(); g.begin();
    for (int i = 0; i < 12; ++i) { y -= .1f; root_y -= .1f; g.pose(0, y, 0, 0); g.hip(); g.frame(33.3); }
    for (int i = 0; i < 5; ++i) { y -= .1f; root_y -= .1f; g.pose(0, y, 0, 0); g.native(); g.frame(33.3); }
    const float entry = g.from_rig_eye(); // what the entry still carries, 170 ms in
    g.measure_from_here();
    TfpCameraTraceRow rows[64]; tfp_camera_trace_edge();
    CHECK(tfp_camera_trace_read(rows, 64) == 6 && rows[0].kind == 4 && rows[1].kind == 6
          && ((rows[1].what & TFP_CAMERA_TRACE_EDGE) != 0) == (smooth != 0)); // the entry: a change of camera taken up
    y -= .1f; root_y -= .7f; g.pose(0, y, 0, 0); g.root_only(0, root_y, 0); g.native(); g.frame(33.3);
    const float root_step = g.world_step, root_carried = g.from_rig_eye();
    TfpCameraContinuityStats stats{}; tfp_camera_continuity_stats(&stats);
    // the log says so: the Trace row of the frame names the root's place and that it alone was re-seated
    CHECK(tfp_camera_trace_read(rows, 64) == 1 && rows[0].root[1] == root_y
          && (smooth ? stats.reseats == 1 && (rows[0].what & TFP_CAMERA_TRACE_ROOT) && !(rows[0].what & TFP_CAMERA_TRACE_JUMP) : stats.reseats == 0 && rows[0].what == 0));
    y -= .54f; root_y -= .1f; g.pose(0, y, 0, 0); g.root_only(0, root_y, 0); g.native(); g.frame(33.3);
    const float eye_step = g.world_step, eye_carried = g.from_rig_eye();
    for (int i = 0; i < 20; ++i) { y -= .1f; root_y -= .1f; g.pose(0, y, 0, 0); g.root_only(0, root_y, 0); g.native(); g.frame(33.3); }
    if (smooth) {
      CHECK(std::fabs(root_step - .1f) < .03f && root_carried <= entry + 1e-4f); // nothing taken up for the root
      CHECK(std::fabs(eye_step - .1f) < .03f && eye_carried > .4f && eye_carried < .48f + entry); // the rig's 0.44 m is
      CHECK(g.from_rig_eye() < 1e-4f && g.world_change < .04f);
    } else {
      CHECK(std::fabs(root_step - .1f) < 1e-3f && std::fabs(eye_step - .54f) < 1e-3f && g.from_rig_eye() < 1e-4f);
    }
    g.end();
  }
  // ---- 19. A frame the lock could not hold for the camera's space (helper error 15; seen in the soak of
  // 2026-11-22, 19:54:43, one frame, logged as "no proof named"). The reason names which of the two it was: the
  // ordinary camera's request with the camera linked to a parent and no native view of the last frames to repeat, or
  // a camera without a parent whose local and world position are not the same point (and how far apart). ----
  for (int apart = 0; apart < 2; ++apart) {
    fresh(true, true);
    Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    standing(g, 12, 33.3, false);
    TfpCameraLockStats before{}; tfp_camera_lock_stats(&before);
    CHECK(before.missed == 0);
    if (apart) {
      g.hip(); g.s.camera_split = true;
      for (int i = 0; i < 3; ++i) { g.s.camera_local[i] = g.shown[i]; g.s.camera_world[i] = g.shown[i]; }
      g.s.camera_world[2] += 2.0f; /* further than a camera's world matrix can be behind its local in a frame or two */
    } else g.gap();
    g.frame(33.3);
    g.s.camera_split = false;
    TfpCameraLockStats after{}; tfp_camera_lock_stats(&after);
    CHECK(after.missed == 1 && after.missed_kind == 4 && after.missed_error == TFP_CAMERA_SPACE_INVALID);
    CHECK(after.missed_why == (apart ? TFP_CAMERA_WHY_SPACE : TFP_CAMERA_WHY_LINKED));
    CHECK(apart ? std::fabs(after.missed_apart - 2.0f) < 1e-4f : after.missed_apart == 0);
    const char *text = tfp_camera_why_text(after.missed_why);
    CHECK(std::strstr(text, apart ? "local and world" : "linked to a parent") != nullptr);
    standing(g, 3, 33.3, false); // the next frame is the head camera's again
    tfp_camera_lock_stats(&after);
    CHECK(after.missed == 1);
    g.end();
  }
  // ---- 20. A change of camera style while a lag is carried never moves the eye by itself. Stable, the head drops
  // 0.5 m in one frame (0.25 m carried), and in the next the style is Bodycam at the default 60 % (180 ms, 0.10 m).
  // Against the same frames without any damping, the eye moves no more than 0.02 m in any one frame at 60 and 144
  // frames a second; at 30 it moves what the camera left in Stable moves in its own first frame from 0.25 m
  // (0.0227 m), and no more. It is within Bodycam's 0.10 m 300 ms after the change. With Bodycam's camera damping
  // at 0 no damping is left: the lag is let go by 0.55 m a second at most (0.02 m in no frame), and is gone in 600 ms.
  // Runs: [0] no damping, [1] the change, [2] Stable kept. (Round 19: with the smooth eye on the drop is two frames,
  // 0.24 m and 0.26 m; 0.5 m in one frame from rest is a pose cut for it, THE CUT, taken up whatever the damping did,
  // and scenario 17 says what is then shown. With it off, the drop is one frame, as before.) ----
  for (int rate = 0; rate < 3; ++rate) for (int smooth = 0; smooth < 2; ++smooth) for (int to = 0; to < 2; ++to) {
    static const double rates[] = {30, 60, 144};
    const double dt = 1000.0 / rates[rate];
    const int still = static_cast<int>(rates[rate] / 3), after = static_cast<int>(rates[rate]); // a second
    static float out[3][260][4]; int frames[3] = {0, 0, 0};
    for (int run = 0; run < 3; ++run) {
      fresh(true, smooth != 0, false, run ? 180.0f : 0.0f, !run);
      clock_ms = 900000;
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
      auto keep = [&]() { std::memcpy(out[run][frames[run]++], sent.position, 16); };
      for (int i = 0; i < still; ++i) { g.hip(); g.frame(dt); keep(); }
      if (smooth) { g.pose(1, 2, 0, .2f, 1.26f); g.hip(); g.frame(dt); keep(); }
      g.pose(1, 2, 0, .2f, 1.0f);
      g.hip(); g.frame(dt); keep();
      if (run == 1) tfp_camera_set_lock(true, 0, 0, 300, 0, 0, to ? 0.0f : 180.0f, true); // what lock_note does when the option changes
      for (int i = 0; i < after; ++i) { g.hip(); g.frame(dt); keep(); }
      CHECK(tfp_camera_error() == 0);
      g.end();
    }
    const int at = still + (smooth ? 2 : 1); // the first frame of the new style
    auto lag = [&](int i) { float d = 0; for (int k = 0; k < 3; ++k) d += (out[1][i][k] - out[0][i][k]) * (out[1][i][k] - out[0][i][k]); return std::sqrt(d); };
    auto moved = [&](int run, int i) { // how far the eye moved against the undamped one in frame i
      float d = 0;
      for (int k = 0; k < 3; ++k) { const float now = out[run][i][k] - out[0][i][k], before = out[run][i - 1][k] - out[0][i - 1][k]; d += (now - before) * (now - before); }
      return std::sqrt(d);
    };
    CHECK(frames[0] == at + after && frames[1] == at + after && frames[2] == at + after && std::fabs(lag(at - 1) - .25f) < 1e-3f); // carried, at Stable's limit
    float most = 0, own = 0;
    for (int i = at; i < at + after; ++i) { most = std::fmax(most, moved(1, i)); own = std::fmax(own, moved(2, i)); }
    const int at300 = at + static_cast<int>(std::ceil(300 / dt - 1e-6)) - 1; // the frame 300 ms after the change
    if (!to) {
      std::printf("Stable to Bodycam with 0.25 m carried, %g frames a second, smooth eye %s: %.4f m in the first frame, at most %.4f m in one frame "
                  "(Stable kept: %.4f m), %.4f m behind after 300 ms\n", rates[rate], smooth ? "on" : "off", moved(1, at), most, own, lag(at300));
      CHECK(most <= std::fmax(.02f, own) + 1e-5f && own < .023f);
      CHECK(lag(at300) <= .10f + 1e-4f && lag(at + after - 1) < .01f);
    } else {
      std::printf("Stable to Bodycam at damping 0 with 0.25 m carried, %g frames a second, smooth eye %s: %.4f m in the first frame, at most %.4f m in one frame, %.4f m behind after 300 ms\n",
                  rates[rate], smooth ? "on" : "off", moved(1, at), most, lag(at300));
      const int at600 = at + static_cast<int>(std::ceil(600 / dt - 1e-6)) - 1;
      CHECK(most <= .02f && moved(1, at) > .003f && lag(at300) < .10f); // let go at 0.55 m a second at most, not dropped
      CHECK(!std::memcmp(out[0][at600], out[1][at600], sizeof(float) * 4 * (at + after - at600))); // gone: the undamped camera
    }
  }
  // ---- 21. The trace says what the eye damping did to the wanted eye, and whether the body was on foot. The
  // wanted eye of a head-camera frame is written after the damping, so a log could not say whether the damping ran
  // in a frame, how far behind the head the eye was, or where the head itself was (round 10, 2026-11-24: prone and a
  // crouch out of an emote, the eye setting off at 0.02, 0.12, 0.25 m a frame; the arithmetic says the 0.25 m limit
  // came into force, the lines could not). damp: what was added to the true eye, in the world; nothing: zeros. ----
  {
    const double dt = 33.3;
    fresh(true, true, true, 300.0f);
    Game g; g.pose(1, 2, 0, .2f); g.look(.3f, -.2f); g.hip(); g.begin();
    TfpCameraTraceRow rows[64];
    standing(g, 12, dt, false);
    tfp_camera_trace_edge();
    uint32_t count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 6);
    for (uint32_t i = 0; i < count; ++i) // on foot, damped, the head still: nothing added
      CHECK((rows[i].what & TFP_CAMERA_TRACE_DAMPED) && !(rows[i].what & TFP_CAMERA_TRACE_OFF_FOOT) && rows[i].damp[0] == 0 && rows[i].damp[1] == 0 && rows[i].damp[2] == 0);
    g.pose(1, 2, 0, .2f, 1.0f); g.hip(); g.frame(dt); // the head 0.5 m down in a frame: the eye is held 0.10 m above it, the limit
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 1 && (rows[0].what & TFP_CAMERA_TRACE_DAMPED) && !(rows[0].what & TFP_CAMERA_TRACE_OFF_FOOT));
    CHECK(std::fabs(rows[0].damp[2] - .10f) < 1e-3f && std::fabs(rows[0].damp[0]) < 1e-3f && std::fabs(rows[0].damp[1]) < 1e-3f);
    const float true_eye = rows[0].wanted[2] - rows[0].damp[2];
    g.s.state = 50; g.hip(); g.frame(dt); // off foot: the row says so, the damping no longer runs, and what is let go is still told
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 1 && (rows[0].what & TFP_CAMERA_TRACE_OFF_FOOT) && !(rows[0].what & TFP_CAMERA_TRACE_DAMPED));
    CHECK(rows[0].damp[2] > .02f && rows[0].damp[2] < .09f && std::fabs(rows[0].wanted[2] - rows[0].damp[2] - true_eye) < 1e-3f);
    standing(g, 10, dt, false);
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 10 && (rows[9].what & TFP_CAMERA_TRACE_OFF_FOOT) && rows[9].damp[2] == 0); // let go and gone
    g.s.state = 36; standing(g, 2, dt, true); tfp_camera_trace_edge(); // on foot again, the native view: never damped
    count = tfp_camera_trace_read(rows, 64);
    CHECK(count >= 1 && rows[count - 1].kind == 6 && !(rows[count - 1].what & (TFP_CAMERA_TRACE_DAMPED | TFP_CAMERA_TRACE_OFF_FOOT)) && rows[count - 1].damp[2] == 0);
    g.end();
    std::printf("trace: the damping's share of the wanted eye and the body off foot are in the rows\n");
  }
  // ---- 22. The trace says which way the view pointed. What is carried is kept against the body root, and goes
  // round with the root's axes; that is right for a body that turns with its view and wrong for a root that turns
  // under a still view (W; round 10: a backwards dive, the root's axes going round 156 degrees in nine frames, the rig
  // eye and the view staying, and 0.6 m carried swept round on an arc, 0.8 m of path that was not the eye's). The
  // filter cannot tell the two apart from what it is given, and the lines could not either. view: the head camera's
  // forward; in a native frame the forward axis of the rig eye the camera hangs on. Nothing is done with it yet. ----
  for (int native_view = 0; native_view < 2; ++native_view) {
    const double dt = 33.3;
    fresh(true, true, true);
    Game g; g.pose(1, 2, 0, .2f); g.look(.3f, 0); g.hip(); g.begin();
    TfpCameraTraceRow rows[64];
    standing(g, 8, dt, native_view != 0);
    g.look(.8f, 0); standing(g, 1, dt, native_view != 0);   // the view turns by 0.5 rad, the body does not
    g.pose(1, 2, 0, .9f); standing(g, 1, dt, native_view != 0); // the body turns by 0.7 rad, the view does not
    tfp_camera_trace_edge();
    const uint32_t count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 6);
    auto angle = [](const float *a, const float *b) { return std::acos(std::fmin(1.0f, a[0] * b[0] + a[1] * b[1] + a[2] * b[2])); };
    const TfpCameraTraceRow &before = rows[3], &turned = rows[4], &body = rows[5];
    CHECK(std::fabs(before.view[0] * before.view[0] + before.view[1] * before.view[1] + before.view[2] * before.view[2] - 1) < 1e-4f);
    CHECK(std::fabs(before.view[0] + std::sin(.3f)) < 1e-3f && std::fabs(before.view[1] - std::cos(.3f)) < 1e-3f);
    CHECK(std::fabs(angle(before.view, turned.view) - .5f) < 2e-3f && angle(before.axes + 3, turned.axes + 3) < 1e-3f);
    CHECK(angle(turned.view, body.view) < 2e-3f && std::fabs(angle(turned.axes + 3, body.axes + 3) - .7f) < 2e-3f);
    g.end();
  }
  // ---- 23. The trace is opened by the eye itself (round 13). A melee: the action does not change, no State line is
  // written, and the eye went 0.7 m in a frame with no traced frame at all (16 of 17 melees in the soak of 2026-11-24,
  // and both 1.6 m back-and-forth seconds). When the presented eye's step changes by 0.15 m or
  // more in a frame, a window opens there: the six frames before it, that frame, 500 ms after. Nothing shown differs. ----
  for (int at = 0; at < 4; ++at) {
    const double dt = at == 0 ? 8 : at == 1 ? 16.7 : at == 2 ? 33.3 : 50;
    fresh(true, true, true);
    Game g; g.pose(0, 0, 0, .3f); g.look(.3f, -.2f); g.hip(); g.begin();
    TfpCameraTraceRow rows[128];
    standing(g, 12, dt, false); standing(g, static_cast<int>(700 / dt) + 2, dt, true); // the entry and its 600 ms are over
    CHECK(tfp_camera_trace_read(rows, 128) == 0); // nothing opened a window: the change of camera was taken up
    g.eye[1] += .7f; g.native(); g.frame(dt);      // not looked for: shown as the game put it
    CHECK(g.from_rig_eye() < 1e-4f && !g.forwarded_own);
    uint32_t count = tfp_camera_trace_read(rows, 128);
    CHECK(count == 7 && rows[6].ms == 0 && (rows[6].what & TFP_CAMERA_TRACE_SELF) && !(rows[5].what & TFP_CAMERA_TRACE_SELF));
    CHECK(rows[0].ms < -5.5 * dt && std::fabs(rows[6].step - .7f) < .01f && rows[5].step < .001f);
    g.eye[1] -= .7f; g.native(); g.frame(dt);      // and back: the window is open, "t=" goes on from the first
    count = tfp_camera_trace_read(rows, 128);
    CHECK(count == 1 && (rows[0].what & TFP_CAMERA_TRACE_SELF) && rows[0].ms > dt - 1 && rows[0].ms < dt + 1);
    standing(g, static_cast<int>(700 / dt), dt, true);
    count = tfp_camera_trace_read(rows, 128);       // the stop is the third such frame; 500 ms after it, no more
    CHECK(count > 2 && (rows[0].what & TFP_CAMERA_TRACE_SELF) && rows[count - 1].ms <= 2 * dt + 500 + .01 && rows[count - 1].ms > dt + 500);
    for (uint32_t i = 1; i < count; ++i) CHECK(!(rows[i].what & TFP_CAMERA_TRACE_SELF) && rows[i].ms - rows[i - 1].ms > dt - .01 && rows[i].ms - rows[i - 1].ms < dt + .01);
    CHECK(tfp_camera_trace_read(rows, 128) == 0);
    // under 0.15 m: a crouch, a turn, a 0.1 m re-seat. Nothing opens.
    g.eye[1] += .1f; g.native(); g.frame(dt); standing(g, 5, dt, true);
    for (int i = 0; i < 8; ++i) { g.look(.3f + .2f * (i + 1), -.2f); g.native(); g.frame(dt); }
    CHECK(tfp_camera_trace_read(rows, 128) == 0);
    g.end();
    // with the trace switched off nothing is kept and nothing opens
    fresh(true, true, false);
    Game h; h.pose(0, 0, 0, .3f); h.look(.3f, -.2f); h.hip(); h.begin();
    standing(h, 12, dt, false); standing(h, 30, dt, true); h.eye[1] += .7f; h.native(); h.frame(dt);
    CHECK(tfp_camera_trace_read(rows, 128) == 0);
    h.end();
  }
  // ---- 24. The trace says what the arm drive drew in the frame (round 13): the binder hands the helper the drive's
  // figures of the last primary draw at the next tick, for the newest frame presented. sock: the socket as drawn less
  // the game's own, along the view; arms: TFP_ARM_DRAW_* (0: not driven, or not known for the frame). ----
  {
    fresh(true, true, true);
    Game g; g.pose(0, 0, 0, 0); g.look(0, 0); g.hip(); g.begin();
    TfpCameraTraceRow rows[64];
    tfp_camera_trace_draw(.5f, 1); // nothing presented yet: nothing to say it of
    standing(g, 3, 33.3, true); tfp_camera_trace_draw(.25f, 1);
    standing(g, 1, 33.3, true); tfp_camera_trace_draw(-.125f, 3);
    standing(g, 1, 33.3, true); // this frame's draw has not been told yet
    tfp_camera_trace_edge();
    const uint32_t count = tfp_camera_trace_read(rows, 64);
    CHECK(count == 5 && rows[0].arms == 0 && rows[0].sock == 0 && rows[1].arms == 0);
    CHECK(rows[2].arms == 1 && rows[2].sock == .25f && rows[3].arms == 3 && rows[3].sock == -.125f && rows[4].arms == 0 && rows[4].sock == 0);
    g.end();
  }
  // ---- 25. THE CUT (round 19). An emote: the head camera holds the view, nothing has changed for 1.3 s, so nothing
  // is looked for. The diver dives out of it: in one frame the game cuts the pose, the head 0.4 m lower against a root
  // that lunges 0.08 m and turns its yaw by 2.8 rad (a dive-back), then the root goes on at 5 m/s. On foot the cut is
  // taken up as a change of camera and eased (the trace says cut); off foot (thrown) the head is shown as it is, as
  // in scenario 13. At 8, 16.7, 33.3 and 50 ms a frame, Bodycam (no damping) and Stable (350 ms, 0.25 m).
  // What is measured is the head node (round 19 review), which the head camera's own smoothing keeps within 0.06 m
  // of the head: it moves 0.34 m against the root in that frame. At 50 ms a frame that is under the cut's bound there
  // (150 m/s^2 times the frame's time squared, 0.375 m: prone out of an emote moves the head 0.30 m in such a frame
  // from rest), and it is shown as it was before round 19; a cut of the head of 0.55 m (the most seen, 0.553 m of the
  // node) is a cut at 50 ms too (`big`). ----
  {
    static const double times[] = {8, 16.7, 33.3, 50, 50};
    for (int stable = 0; stable < 2; ++stable) for (int thrown = 0; thrown < 2; ++thrown) for (int at = 0; at < 5; ++at) {
      const double dt = times[at]; const bool big = at == 4, under = dt == 50 && !big;
      fresh(true, true, true, 0.0f, !stable);
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
      TfpCameraTraceRow rows[128];
      standing(g, (int)(1300 / dt), dt, false);
      tfp_camera_trace_read(rows, 128);
      CHECK(g.from_head_eye() < 1e-3f);
      g.measure_from_here();
      if (thrown) g.s.state = 50; // off foot
      // the presented eye against the root's place in the world (a root that turns does not move it), its step's change as a vector
      float against[3][3] = {};
      auto note = [&]() {
        for (int n = 0; n < 2; ++n) std::memcpy(against[n], against[n + 1], 12);
        for (int k = 0; k < 3; ++k) against[2][k] = g.shown[k] - g.s.root[12 + k];
      };
      auto changed = [&]() { float d = 0; for (int k = 0; k < 3; ++k) { const float e = against[2][k] - 2 * against[1][k] + against[0][k]; d += e * e; } return std::sqrt(d); };
      g.hip(); g.frame(dt); note(); g.hip(); g.frame(dt); note();
      const float height = big ? .95f : 1.1f;
      float x = 1.08f; g.pose(x, 2, 0, .2f + 2.8f, height); g.hip(); g.frame(dt); note();
      tfp_camera_trace_edge();
      uint32_t count = tfp_camera_trace_read(rows, 128);
      const bool said = count >= 1 && (rows[count - 1].what & TFP_CAMERA_TRACE_CUT) != 0;
      const float in_cut = changed(); // in the frame of the cut
      float worst = 0;
      for (int i = 0; i < (int)(800 / dt); ++i) { x += 5 * (float)dt / 1000; g.pose(x, 2, 0, .2f + 2.8f, height); g.hip(); g.frame(dt); worst = std::fmax(worst, g.from_head_eye()); }
      std::printf("a pose cut of %.2f m out of an emote, %s, %s, %g ms a frame: the step against the root's place changes by %.3f m in its frame%s, the eye %.3f m from the "
                  "head's at most and %.4f m after 0.8 s\n", 1.5f - height, stable ? "Stable" : "Bodycam", thrown ? "thrown off foot" : "on foot", dt, in_cut,
                  said ? " (cut)" : "", worst, g.from_head_eye());
      if (thrown) CHECK(!said && in_cut > .35f && worst < .065f); // the body's own motion, shown as it is
      else if (under) CHECK(!said); // under the bound at 50 ms: shown as before round 19
      else {
        CHECK(said);
        CHECK(in_cut < .04f);                       // not shown in its frame
        CHECK(worst > .2f);                         // eased: the eye comes after the head
        CHECK(g.from_head_eye() < (stable ? .045f : 1e-3f)); // and is there by 0.8 s (Stable: what its own damping leaves, 0.037 to 0.041 m without the cut too)
      }
      g.end();
    }
  }
  // ---- 26. What is no cut (round 19 review), in the head camera on foot with nothing looked for (1.3 s of nothing
  // changing), at 8, 16.7, 33.3 and 50 ms a frame, Bodycam and Stable:
  //  - a flick of the mouse, 95 and 180 degrees in one frame after a calm one. The head camera's eye is 0.18 m ahead
  //    of the head along the level view and swings round it by up to 0.36 m; the root does not turn with the view.
  //    The head does not move, and the eye is shown where the head camera puts it, every frame (as before round 19).
  //  - the body root moving by itself 0.3 and 0.5 m, the head, the rig and the eye still in the world (the game
  //    re-seating the root): read as before round 19, the eye no further off the head camera's eye than the damping
  //    and the head camera's own smoothing leave it (Bodycam 0.060 m, Stable 0.310 m at most). ----
  {
    static const double times[] = {8, 16.7, 33.3, 50};
    for (int stable = 0; stable < 2; ++stable) for (int what = 0; what < 4; ++what) for (double dt : times) {
      fresh(true, true, true, 0.0f, !stable);
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
      TfpCameraTraceRow rows[128];
      standing(g, (int)(1300 / dt), dt, false);
      tfp_camera_trace_read(rows, 128);
      g.hip(); g.frame(dt); g.hip(); g.frame(dt);
      if (what < 2) g.look(.2f + (what == 0 ? 95.f : 180.f) * 3.14159265f / 180, 0);
      else g.root_only(1 - (what == 2 ? .3f : .5f), 2, 0);
      g.hip(); g.frame(dt);
      tfp_camera_trace_edge();
      bool said = false;
      for (uint32_t i = 0, count = tfp_camera_trace_read(rows, 128); i < count; ++i) said = said || (rows[i].what & TFP_CAMERA_TRACE_CUT) != 0;
      const float first = g.from_head_eye(); float worst = first;
      for (int i = 0; i < (int)(400 / dt); ++i) { g.hip(); g.frame(dt); worst = std::fmax(worst, g.from_head_eye()); }
      tfp_camera_trace_edge();
      for (uint32_t i = 0, count = tfp_camera_trace_read(rows, 128); i < count; ++i) said = said || (rows[i].what & TFP_CAMERA_TRACE_CUT) != 0;
      std::printf("no cut, %s, %s, %g ms a frame: the eye %.3f m off the head camera's eye in the frame, %.3f m at most in 400 ms%s\n", stable ? "Stable" : "Bodycam",
                  what == 0 ? "a flick of 95 degrees" : what == 1 ? "a flick of 180 degrees" : what == 2 ? "the root alone 0.3 m" : "the root alone 0.5 m",
                  dt, first, worst, said ? " (CUT)" : "");
      CHECK(!said);
      if (what < 2) CHECK(worst < 1e-3f);
      else CHECK(worst < (stable ? .315f : .065f));
      g.end();
    }
  }
  // ---- 27. Where the head is not given, each on its own: a cut of the head (0.4 m lower in one frame, from rest, after
  // 1.3 s of nothing changing) is no cut in a camera of the lock that is not the ordinary one (kind 5), in a body that
  // is down (its head over 3 m from the root, the root left behind), nor in the first frame out of full aim (the head
  // is not given in full aim, so its place the frame before is not known). [0] is the same in the ordinary camera on
  // foot: a cut. (The ADS gap is not here: it begins with a change of camera and lasts under 250 ms, inside the 600 ms
  // in which jumps are looked for after one, and a frame looked at is no cut either.) ----
  for (int way = 0; way < 4; ++way) {
    const double dt = 16.7;
    fresh(true, true, true, 0.0f, true);
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    TfpCameraTraceRow rows[128];
    TfpCameraDownStats down_before{}; tfp_camera_down_stats(&down_before);
    auto still = [&](int frames) {
      for (int i = 0; i < frames; ++i) {
        g.hip(); if (way == 1) g.s.request[0] = 5; if (way == 3) g.s.aim = 1;
        if (way == 2) g.root_only(1 - 3.4f, 2, 0); // the root left 3.4 m behind the head
        g.frame(dt);
      }
    };
    still((int)(1300 / dt));
    tfp_camera_trace_read(rows, 128);
    g.pose(1, 2, 0, .2f, 1.1f); if (way == 2) g.root_only(1 - 3.4f, 2, 0);
    g.hip(); if (way == 1) g.s.request[0] = 5; // way 3: the aim let go in this frame
    g.frame(dt);
    tfp_camera_trace_edge();
    bool said = false;
    for (uint32_t i = 0, count = tfp_camera_trace_read(rows, 128); i < count; ++i) said = said || (rows[i].what & TFP_CAMERA_TRACE_CUT) != 0;
    TfpCameraDownStats down{}; tfp_camera_down_stats(&down);
    static const char *const ways[] = {"the ordinary camera on foot", "kind 5", "a body that is down", "the first frame out of full aim"};
    std::printf("a cut of the head in %s: %s\n", ways[way], said ? "a cut" : "no cut");
    CHECK(said == (way == 0));
    if (way == 2) CHECK(down.frames > down_before.frames + 50); // the scene is a body that is down
    CHECK(tfp_camera_error() == 0);
    g.end();
  }
  // ---- 28. A high clamber in the head camera (round 24; the user's log of 2026-11-24 on feel.51, 20:27:42, Stable).
  // The native view, then the head camera as the gun is stowed (the binder's event, mask 0x22), and the body root
  // climbs 1.8 m in 0.3 s at 6 m/s. The head goes up with it for 80 ms and then crouches onto the ledge: it rises at
  // 1.2 m/s while the root goes on (a step change of 0.08 to 0.24 m a frame against the root, at 16.7 to 50 ms a frame);
  // the mask is 0x20 from 0.15 s on. The root stops; 0.15 s later the body stands up (the head 1.06 m up against the
  // root at 2.5 m/s, inside the window the event opened), the mask 2 from 0.55 s (the gun being drawn, as at
  // 20:28:14.0, where Stable's damping held the eye 0.25 m below the rising head); then the native view on mask 0.
  // In the log the crouch was taken up as four jumps, the run's speed handed to the ease-out, and the shown eye rose
  // 0.5 to 0.6 m past the head and sank back over 0.3 s, with Stable's damping 0.25 m behind on top. Now: the shown
  // eye stays within 0.25 m of the head camera's eye throughout (the change of camera at the hand-off is eased, 0.08 m
  // here), and within 0.1 m of it from 100 ms after the root stops. ----
  {
    static const float times[] = {8, 16.7f, 33.3f, 50};
    for (unsigned d = 0; d < 4; ++d) for (int bodycam = 0; bodycam < 2; ++bodycam) {
      const double dt = times[d];
      fresh(true, true, true, bodycam ? 180.0f : 0.0f, bodycam != 0);
      tfp_camera_set_action_tuning(0x22, 900, true); // the binder's: the head camera takes the clamber's mask
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.native(); g.begin();
      standing(g, static_cast<int>(500 / dt), dt, true);
      tfp_camera_continuity_event(); // "hold ended: the gun has left the hands"
      { TfpCameraTraceRow drop[128]; while (tfp_camera_trace_read(drop, 128)) {} }
      tfp_camera_trace_edge();
      unsigned turned = 0, rows_read = 0; float worst_turn = 0;
      float climbing = 0, after = 0, risen = 0; double t = 0;
      const float c = std::cos(.2f), n = std::sin(.2f);
      for (; t < 1.3; t += dt / 1000) {
        const float tc = static_cast<float>(std::fmin(t, .3));
        const float root_z = 6 * tc, forward = 3.3f * tc;
        const float head_z = 1.5f + 6 * std::fmin(tc, .08f) + 1.2f * std::fmax(0.0f, tc - .08f); // in the world
        float height = head_z - root_z;
        if (t > .45) height = std::fmin(1.5f, height + 2.5f * static_cast<float>(t - .45));
        g.pose(1 - n * forward, 2 + c * forward, root_z, .2f, height);
        const uint32_t mask = t < .15 ? 0x22u : t < .55 ? 0x20u : 2u; // the gun drawn in the stand-up, as at 20:28:14
        if (t < .9) g.hip(mask); else g.native(0);
        g.frame(dt);
        { TfpCameraTraceRow rows[128]; // round 24: every head-camera row says the direction shown, a unit vector along the look (yaw 0.2, level)
          for (uint32_t i = 0, count = tfp_camera_trace_read(rows, 128); i < count; ++i) {
            if (rows[i].kind != 4) continue; // the fake's native argument is not a rig-local direction
            ++rows_read;
            const float *v = rows[i].turn, length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (length > .99f && length < 1.01f) ++turned;
            worst_turn = std::fmax(worst_turn, std::sqrt((v[0] + n) * (v[0] + n) + (v[1] - c) * (v[1] - c) + v[2] * v[2]));
          } }
        if (t < .9) {
          climbing = std::fmax(climbing, g.from_head_eye());
          if (t >= .4) after = std::fmax(after, g.from_head_eye());
        }
        if (t >= .45 && t < .9) risen = std::fmax(risen, g.from_head_eye());
      }
      std::printf("a high clamber in the head camera, %s, %g ms a frame: the shown eye at most %.3f m from the head camera's eye, "
                  "%.3f m once the root stopped (%.3f m in the stand-up), the change of its step %.3f m at most\n",
                  bodycam ? "Bodycam" : "Stable", dt, climbing, after, risen, g.world_change);
      CHECK(climbing <= .25f);
      CHECK(after <= .10f);
      if (!(rows_read > 0 && turned == rows_read && worst_turn < .01f)) std::printf("  turn: %u rows, %u unit, %.4f off\n", rows_read, turned, worst_turn);
      CHECK(rows_read > 0 && turned == rows_read && worst_turn < .01f);
      CHECK(tfp_camera_error() == 0);
      g.end();
      tfp_camera_set_action_tuning(0, 300, false); // the defaults again for what runs after
    }
  }
  // ---- 29. The window and a clamber's masks, in the helper (round 24 review: putting the old rule back in
  // smooth_detect, any change of mask opening the window, survived every test). The head camera on a vault's mask
  // 0x20 with the root still, long after any event (no jump is looked for), then the clamber's 0x22: nothing opens.
  // The same with a swap's 2 then 0: that change of mask opens the window, as it always did. ----
  for (int traversal = 0; traversal < 2; ++traversal) {
    fresh(true, true, true);
    tfp_camera_set_action_tuning(0x22, 900, true);
    Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.hip(); g.begin();
    standing(g, 40, 33.3, false, traversal ? 0x20u : 2u); // 1.3 s: any window is over
    { TfpCameraTraceRow drop[128]; while (tfp_camera_trace_read(drop, 128)) {} }
    standing(g, 5, 33.3, false, traversal ? 0x22u : 0u);
    tfp_camera_trace_edge();
    TfpCameraTraceRow rows[64];
    const uint32_t count = tfp_camera_trace_read(rows, 64);
    unsigned before = 0, after = 0;
    for (uint32_t i = 0; i < count; ++i) if (rows[i].what & TFP_CAMERA_TRACE_LOOKED) { if (i == 0) ++before; else ++after; }
    std::printf("the window at a change of mask %s, the head camera, the root still: %u of %u frames after it looked at\n",
                traversal ? "0x20 -> 0x22" : "2 -> 0", after, count - 1);
    CHECK(count == 6 && before == 0);
    if (traversal) CHECK(after == 0); else CHECK(after == 5);
    CHECK(tfp_camera_error() == 0);
    g.end();
    tfp_camera_set_action_tuning(0, 300, false);
  }
  // ---- 30. THE SETTLE's switch and gate, in the helper (round 24 review: dropping its mask-0 condition survived).
  // The native view, then the head camera on a swap's mask 2, then the binder's arm (the native view was left for a
  // new firearm) and the native view again with the rig 0.3 m under the head camera's eye. Only with the switch on
  // (settle_on.txt), the arm given and the entry on mask 0 is the eye held (a Trace row says +settle). ----
  {
    struct Case { bool on, arm; uint32_t mask; bool held; const char *name; } cases[] = {
      {false, true, 0, false, "the switch off (the default)"}, {true, true, 0, true, "the switch on, armed, mask 0"},
      {true, true, 2, false, "the switch on, armed, mask 2"}, {true, false, 0, false, "the switch on, not armed"}};
    for (const Case &k : cases) {
      fresh(true, true, true);
      tfp_camera_set_settle(k.on);
      Game g; g.pose(1, 2, 0, .2f); g.look(.2f, 0); g.native(); g.begin();
      standing(g, 20, 33.3, true);
      standing(g, 20, 33.3, false, 2);
      if (k.arm) tfp_camera_settle_arm();
      g.eye[2] = -.25f;
      standing(g, 4, 33.3, true, k.mask);
      tfp_camera_trace_edge();
      TfpCameraTraceRow rows[64];
      const uint32_t count = tfp_camera_trace_read(rows, 64);
      unsigned held = 0, native = 0;
      for (uint32_t i = 0; i < count; ++i) { if (rows[i].kind == 6) ++native; if (rows[i].what & TFP_CAMERA_TRACE_SETTLE) ++held; }
      std::printf("THE SETTLE, %s: %u of %u native frames held\n", k.name, held, native);
      CHECK(native == 4);
      CHECK(k.held ? held > 0 : held == 0);
      CHECK(tfp_camera_error() == 0);
      g.end();
    }
    tfp_camera_set_settle(false);
  }
  std::printf("camera smooth: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
