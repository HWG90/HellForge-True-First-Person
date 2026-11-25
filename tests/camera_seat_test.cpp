// Round 38: first person kept in an exosuit or vehicle seat under the camera lock (src/camera_helper.c,
// src/camera_vehicle.h), on the fake game memory of the differential test; every frame is one call of the hook as
// the game's call site makes it. What the game did in an EXO-45 cockpit (2026-11-25, feel.55): the request stayed
// kind 4 and the player's own, the avatar's seated bit (flags[14] & 4) was set, layer 14 left the on-foot state, no
// ragdoll flag, the body root rose into the cockpit and the head sat 0.84 m above it, bobbing with the suit's gait.
// Judged here: entering glides from the last first-person view to the seat's eye; in the seat the eye is the root's
// frozen head anchor plus 0.18 m along the root's level forward and 0.04 m up, whatever the head bob and the game
// camera's yaw, and follows the suit wherever it walks; the direction is the game's on the same frame, with no
// kick; leaving glides back to the head camera, or into the native view, or through both in turn, never with a step
// and never back from the cockpit (the independent check of 2026-11-25 found a 2.52 m cut into the native view and a
// 2.46 m step, then 2.21 m back toward the cockpit, when native frames interrupted the glide); the seat body is
// published for head hiding until the exit glide ends and not after. A valve, a thrown body, a body killed in its
// seat and the lock switched off are what they were; an FRV's seat is a seat like the suit's. No retail source
// needed. Linux (process_vm_readv).
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
static struct { float position[4]; bool own_buffer, seen; } sent;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
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
  if (reinterpret_cast<uint64_t>(camera) == W.a[A_CAMERA]) {
    std::memcpy(reinterpret_cast<void *>(W.a[A_CAM_LOCALS] + 0x24), point, 12);
    std::memcpy(reinterpret_cast<void *>(W.a[A_CAM_WORLDS] + 0x30), point, 12);
  }
}

enum { PHYSICS = 0x547844, BODY = 0x400003, SEATED = 14, L_HAND = 13, R_HAND = 34 };
struct Q { float x, y, z, w; };
static Q yaw_q(float yaw) { return {0, 0, std::sin(yaw / 2), std::cos(yaw / 2)}; }
static float degrees(Q a, Q b) {
  const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return 2 * std::acos(d > 1 ? 1 : d) * 57.29578f;
}
static float apart(const float *a, const float *b) {
  float d = 0; for (int i = 0; i < 3; ++i) d += (a[i] - b[i]) * (a[i] - b[i]);
  return std::sqrt(d);
}

struct Game {
  Scene s;
  float right[3] = {1, 0, 0}, ahead[3] = {0, 1, 0}, above[3] = {0, 0, 1};
  float neck[3] = {0, 0, 1.4f};
  float root_yaw = 0, head_pitch = 0, hand_l[3] = {}, hand_r[3] = {}; // head_pitch: the head node's own nod
  bool ragdoll = false;
  uint64_t gen = 0;
  Q out = {0, 0, 0, 1}; bool own = false; uint32_t error = 0;
  // The body root at (x, y, z), turned `root_yaw` about the vertical; the head `head` metres from it in its frame.
  void place(float x, float y, float z, const float *head_local) {
    const float c = std::cos(root_yaw), n = std::sin(root_yaw);
    const float m[16] = {c, n, 0, 0, -n, c, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    std::memcpy(s.root, m, sizeof m);
    float h[3]; for (int k = 0; k < 3; ++k) h[k] = m[12 + k] + head_local[0] * m[k] + head_local[1] * m[4 + k] + head_local[2] * m[8 + k];
    head(h[0], h[1], h[2]);
    // the hands: raised in front of the chest, 0.45 m below the head and 0.35 m ahead, 0.2 m to each side
    for (int k = 0; k < 3; ++k) {
      hand_l[k] = m[12 + k] + (head_local[0] - .2f) * m[k] + (head_local[1] + .35f) * m[4 + k] + (head_local[2] - .45f) * m[8 + k];
      hand_r[k] = m[12 + k] + (head_local[0] + .2f) * m[k] + (head_local[1] + .35f) * m[4 + k] + (head_local[2] - .45f) * m[8 + k];
    }
  }
  void stand(float x, float y, float z, float head_up = 1.56f) { const float h[3] = {.03f, .06f, head_up}; place(x, y, z, h); }
  void head(float x, float y, float z) { s.head[0] = x; s.head[1] = y; s.head[2] = z; neck[0] = x; neck[1] = y; neck[2] = z - .1f; }
  void look(float yaw) {
    const float c = std::cos(yaw), n = std::sin(yaw);
    right[0] = c; right[1] = n; right[2] = 0; ahead[0] = -n; ahead[1] = c; ahead[2] = 0; above[0] = 0; above[1] = 0; above[2] = 1;
    for (int i = 0; i < 3; ++i) { s.context[i] = right[i]; s.context[4 + i] = ahead[i]; s.context[8 + i] = above[i]; s.camera_rotation[i] = right[i]; s.camera_rotation[4 + i] = ahead[i]; s.camera_rotation[8 + i] = above[i]; }
    const Q q = yaw_q(yaw); std::memcpy(s.argument_rotation, &q, 16);
  }
  // The game's ordinary camera (kind 4, or `kind`), `back` metres behind the head and `high` above it.
  void hip(float back = 2.0f, float high = .4f, uint32_t kind = 4) {
    s.request[0] = kind; s.linked = false; s.mask = 0; s.aim = 0; s.camera_rotation_given = false; s.camera_split = false;
    for (int i = 0; i < 3; ++i) s.argument[i] = s.head[i] - ahead[i] * back + above[i] * high;
    s.argument[3] = 0;
  }
  void native(float ahead_m = .1f) { // the rig eye ahead_m ahead of the head
    s.request[0] = 6; s.linked = true; s.mask = 0; s.aim = 0; s.camera_rotation_given = true;
    for (int i = 0; i < 3; ++i) { s.rig[i] = right[i]; s.rig[4 + i] = ahead[i]; s.rig[8 + i] = above[i]; s.rig[12 + i] = s.head[i] + ahead[i] * ahead_m + above[i] * .05f; }
    s.rig[3] = s.rig[7] = s.rig[11] = 0; s.rig[15] = 1;
    s.argument[0] = .02f; s.argument[1] = .01f; s.argument[2] = .03f; s.argument[3] = 0;
    s.camera_split = true; for (int i = 0; i < 3; ++i) { s.camera_local[i] = s.argument[i]; s.camera_world[i] = s.rig[12 + i]; }
  }
  void seat(bool on) { if (on) s.flags[SEATED] |= 4; else s.flags[SEATED] &= static_cast<uint8_t>(~4u); }
  void begin_config(TfpCameraConfig &c) {
    c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA]; c.avatar_owner = W.a[A_OWNER];
    c.avatar_index = 0; c.avatar = 0x333; c.body = BODY; c.camera_unit = 0x400004; c.forward = .18f; c.up = .04f; c.smoothing_ms = 100; c.blend_ms = 120; c.max_gap_ms = 250;
  }
  void write() {
    W.apply(s, 0);
    World::put(W.a[A_BODY_WORLD] + HEAD_PARENT * 64 + 0x30, neck);
    const float hc = std::cos(head_pitch), hn = std::sin(head_pitch), rows[12] = {1, 0, 0, 0, 0, hc, hn, 0, 0, -hn, hc, 0};
    World::put(W.a[A_BODY_WORLD] + HEAD_NODE * 64, rows);
    World::put<uint32_t>(W.a[A_HASHES] + L_HAND * 4, 0x5c0c68d3u); World::put<uint32_t>(W.a[A_HASHES] + R_HAND * 4, 0x248004f5u);
    World::put(W.a[A_BODY_WORLD] + L_HAND * 64 + 0x30, hand_l); World::put(W.a[A_BODY_WORLD] + R_HAND * 64 + 0x30, hand_r);
    World::put<uint8_t>(W.a[A_OWNER] + PHYSICS, ragdoll ? 0x14 : 0x04);
    World::put<uint64_t>(W.a[A_BODY] + 0x178, W.a[A_ANIM_INSTANCE]);
  }
  void begin() {
    TfpCameraConfig c{}; begin_config(c);
    World::put<uint64_t>(W.slot(), W.native_value);
    write();
    if (tfp_camera_begin(&c, &gen) != 1 || tfp_camera_set_presentation(gen) != 1 || tfp_camera_set_cut_hold(gen, 15) != 1) {
      std::printf("the helper did not arm (error %u)\n", tfp_camera_error()); std::exit(2);
    }
  }
  void end() { tfp_camera_end(gen); }
  void frame(double dt = 33.3) {
    clock_ms += dt; write(); sent.seen = false;
    tfp_camera_setter_from(W.a[A_GAME] + HERE.call + 4, reinterpret_cast<void *>(W.a[A_CAMERA]), 0x400004, reinterpret_cast<const float *>(W.native()));
    if (!sent.seen) { std::printf("the hook did not forward\n"); std::exit(2); }
    own = sent.own_buffer; error = tfp_camera_error();
    std::memcpy(&out, reinterpret_cast<void *>(W.native() + 64), 16);
  }
  Q given() const { Q q; std::memcpy(&q, s.argument_rotation, 16); return q; }
  // How far the forwarded eye is from the head camera's place for a view along `yaw`.
  float from_head(float yaw) const {
    const float want[3] = {s.head[0] - std::sin(yaw) * .18f, s.head[1] + std::cos(yaw) * .18f, s.head[2] + .04f};
    return apart(sent.position, want);
  }
  // The seat's eye for a head anchor `anchor` in the root frame: root + R anchor + 0.18 m along the root's level
  // forward + 0.04 m up.
  void seat_eye(const float *anchor, float *eye) const {
    const float *m = s.root;
    for (int k = 0; k < 3; ++k) eye[k] = m[12 + k] + anchor[0] * m[k] + anchor[1] * m[4 + k] + anchor[2] * m[8 + k];
    eye[0] += -std::sin(root_yaw) * .18f; eye[1] += std::cos(root_yaw) * .18f; eye[2] += .04f;
  }
  // The shown eye in the root's frame.
  void shown_local(float *local) const {
    const float *m = s.root;
    for (int a = 0; a < 3; ++a) { local[a] = 0; for (int k = 0; k < 3; ++k) local[a] += (sent.position[k] - m[12 + k]) * m[a * 4 + k]; }
  }
  bool forwarded_untouched() const {
    const Q q = given();
    return !own && apart(sent.position, s.argument) == 0 && !std::memcmp(&out, &q, 16);
  }
  // The diver climbs into the suit: the root rises into the cockpit, the head 0.84 m above it, off the on-foot state.
  void climb_in(float x, float y, const float *head_local = nullptr) {
    static const float sitting[3] = {.03f, .06f, .84f};
    seat(true); s.state = 25; place(x, y, 2.43f + .34f, head_local ? head_local : sitting);
  }
  void climb_out(float x, float y) { seat(false); s.state = 36; root_yaw = 0; stand(x, y, .34f); }
};

static void fresh(bool lock, bool smooth = false) {
  tfp_camera_reset_for_tests();
  TfpCameraHost host{};
  host.hook = reinterpret_cast<uint64_t>(&hook_marker); host.read = host_read; host.readable_span = host_readable_span; host.now = host_now;
  host.writable_slot = host_true_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock; host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_camera_set_host(&host);
  tfp_camera_set_lock(lock, .35f, 8.0f, 300, .7f, 35.0f, 0, true); // Bodycam, as the user has it: the kick sliders on
  tfp_camera_set_continuity(smooth, false);
  clock_ms += 5000;
}

static const float sitting[3] = {.03f, .06f, .84f};

// A native frame's argument is parent-local: the camera is drawn at the rig eye plus the argument along its axes.
static void rig_eye(const Game &g, float *out) {
  for (int k = 0; k < 3; ++k) out[k] = g.s.rig[12 + k] + g.s.argument[0] * g.s.rig[k] + g.s.argument[1] * g.s.rig[4 + k] + g.s.argument[2] * g.s.rig[8 + k];
}
// Where this frame put the camera in the world: the forwarded point, or in the native view that point on the rig eye.
static void track(const Game &g, float *out) {
  if (g.s.request[0] == 6 && g.s.linked) {
    for (int k = 0; k < 3; ++k) out[k] = g.s.rig[12 + k] + sent.position[0] * g.s.rig[k] + sent.position[1] * g.s.rig[4 + k] + sent.position[2] * g.s.rig[8 + k];
  } else std::memcpy(out, sent.position, 12);
}
// On foot, a moment of the native view, then into the suit and settled in it (the checker's probe, 2026-11-25).
static void board(Game &g) {
  g.stand(0, 0, .34f); g.look(.4f); g.hip(); g.begin();
  for (int i = 0; i < 20; ++i) { g.native(); g.frame(8.3); }
  for (int i = 0; i < 120; ++i) { g.climb_in(.1f, -.2f); g.look(.7f); g.hip(4.0f, 1.0f); g.frame(8.3); }
}

int main() {
  W.build(HERE);
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  const float first = .4f, suit = .7f; // the head camera's direction on foot; the suit camera's

  // ---- 1. Entering, staying and leaving, with continuity off and on, at 120 frames a second. ----
  for (int smooth = 0; smooth < 2; ++smooth) {
    fresh(true, smooth != 0);
    const double dt = 8.3;
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 40; ++i) { g.hip(); g.frame(dt); CHECK(g.own && g.error == 0); }
    g.s.state = 50; // the climb: off foot, not yet seated (held at the head, as a thrown body)
    for (int i = 0; i < 12; ++i) { g.hip(); g.frame(dt); CHECK(g.own && g.error == 0); }
    CHECK(tfp_camera_lock_seat_body() == 0);
    const float shown[3] = {sent.position[0], sent.position[1], sent.position[2]}; const Q shown_q = g.out;
    TfpCameraLockStats lock0{}; tfp_camera_lock_stats(&lock0);
    const uint64_t view0 = tfp_camera_view_frames();
    TfpCameraSeatStats seat0{}; tfp_camera_seat_stats(&seat0);
    CHECK(seat0.frames == 0 && seat0.entries == 0 && !seat0.seated);

    // Into the seat. The suit stands; the glide begins where the view was and ends on the seat's eye with the
    // game's direction, no step over 0.15 m, within 600 ms.
    float x = .1f, last[3] = {shown[0], shown[1], shown[2]}, most = 0; int glide_frames = 0; bool arrived = false;
    for (int i = 0; i < 100; ++i) {
      g.climb_in(x, -.2f); g.look(suit); g.hip(4.0f, 1.0f);
      g.frame(dt);
      CHECK(g.own && g.error == 0);
      if (i == 0) {
        CHECK(apart(sent.position, shown) < 1e-5f && degrees(g.out, shown_q) < .01f); // the glide's start
        TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat);
        CHECK(seat.entries == 1 && seat.seated && seat.in_kind == 4 && seat.in_ms >= 300 && seat.in_ms <= 600 && seat.in_metres > 1.0f && seat.gliding == 1);
        CHECK(tfp_camera_lock_seat_body() == BODY);  // the body is hidden from the first seat frame
      }
      const float step = apart(sent.position, last); if (step > most) most = step;
      std::memcpy(last, sent.position, 12);
      float eye[3]; g.seat_eye(sitting, eye);
      if (!arrived && apart(sent.position, eye) < 1e-4f && degrees(g.out, g.given()) < .01f) { arrived = true; glide_frames = i; }
    }
    CHECK(arrived && glide_frames <= 73); // 600 ms at 8.3 ms a frame
    CHECK(most < .15f);
    TfpCameraSeatStats in1{}; tfp_camera_seat_stats(&in1);
    CHECK(in1.frames == 100 && in1.refused == 0 && in1.cuts == 0 && in1.settled && in1.gliding == 0 && in1.glided >= 30);
    CHECK(std::fabs(in1.offset[0] - .03f) < 1e-3f && std::fabs(in1.offset[1] - .24f) < 1e-3f && std::fabs(in1.offset[2] - .88f) < 1e-3f);
    // The hands, from the eye along the root's axes: 0.2 m to each side, 0.17 m ahead, 0.49 m below.
    CHECK(in1.hands == 3 && std::fabs(in1.left[0] + .2f) < 1e-3f && std::fabs(in1.right[0] - .2f) < 1e-3f
          && std::fabs(in1.left[1] - .17f) < 1e-3f && std::fabs(in1.right[2] + .49f) < 1e-3f);
    std::printf("into the suit%s: %.2f m glided in %d frames (%u ms), largest step %.3f m\n", smooth ? " (continuity on)" : "",
                (double)in1.in_metres, glide_frames, in1.in_ms, (double)most);

    // Staying: the suit walks 3.6 m away and turns 90 degrees while the head bobs ±3 cm in the root frame and the
    // game camera yaws all round. The eye stays on the root's anchor; the direction is the game's on the same frame,
    // with no kick (the Bodycam kick sliders are on).
    TfpCameraLockStats lock1{}; tfp_camera_lock_stats(&lock1);
    const float rig_was[3] = {sent.position[0], sent.position[1], sent.position[2]};
    float worst = 0, worst_q = 0;
    for (int i = 0; i < 180; ++i) {
      x += .02f; g.root_yaw = (float)i / 180 * 1.5708f;
      const float bob[3] = {sitting[0] + .03f * std::sin((float)i * .7f), sitting[1] + .03f * std::cos((float)i * .5f), sitting[2] + .03f * std::sin((float)i * 1.3f)};
      g.climb_in(x, -.2f, bob); g.look(suit + (float)i * .02f); g.hip(4.0f, 1.0f);
      g.frame(dt);
      CHECK(g.own && g.error == 0);
      float eye[3]; g.seat_eye(sitting, eye);
      const float d = apart(sent.position, eye); if (d > worst) worst = d;
      const Q q = g.given(); // the game's own rotation, bit for bit: nothing eased or kicked
      const float a = std::fabs(g.out.x - q.x) + std::fabs(g.out.y - q.y) + std::fabs(g.out.z - q.z) + std::fabs(g.out.w - q.w); if (a > worst_q) worst_q = a;
    }
    CHECK(worst < 1e-3f && worst_q == 0);
    CHECK(apart(sent.position, rig_was) > 3.0f); // the view went with the suit
    TfpCameraLockStats lock2{}; tfp_camera_lock_stats(&lock2);
    CHECK(lock2.frames == lock1.frames + 180 && lock2.missed == lock1.missed && lock2.peak_kick_degrees == 0);
    CHECK(tfp_camera_view_frames() > view0 + 270);
    CHECK(lock2.frames > lock0.frames + 270);
    CHECK(tfp_camera_lock_seat_body() == BODY);
    std::printf("in the suit: the eye stayed within %.5f m of the anchor while the suit walked 3.6 m and turned 90 degrees\n", (double)worst);

    // Leaving, 2 m to the suit's side: the first frame is the seat's last eye, then the head camera within 600 ms with
    // no step over 0.15 m. The seat body stays published until the glide ends, then lapses within 250 ms.
    const float seat_last[3] = {sent.position[0], sent.position[1], sent.position[2]}; const Q seat_q = g.out;
    g.climb_out(x + 2, -.2f); g.look(first); g.hip();
    g.frame(dt);
    CHECK(g.own && g.error == 0 && apart(sent.position, seat_last) < 1e-5f && degrees(g.out, seat_q) < .01f);
    TfpCameraSeatStats out0{}; tfp_camera_seat_stats(&out0);
    CHECK(out0.exits == 1 && !out0.seated && out0.out_ms >= 300 && out0.out_ms <= 600 && out0.gliding == 2);
    float step_most = 0; std::memcpy(last, sent.position, 12); int back = -1;
    for (int i = 1; i < 120; ++i) {
      g.hip(); g.frame(dt); CHECK(g.own && g.error == 0);
      const float step = apart(sent.position, last); if (step > step_most) step_most = step;
      std::memcpy(last, sent.position, 12);
      if (back < 0 && g.from_head(first) < .07f && degrees(g.out, yaw_q(first)) < .5f) back = i;
      if (i == 30) CHECK(tfp_camera_lock_seat_body() == BODY); // 250 ms: still gliding
    }
    CHECK(back > 0 && back <= 73 && step_most < .15f);
    CHECK(g.from_head(first) < .07f && tfp_camera_lock_seat_body() == 0);
    TfpCameraSeatStats out1{}; tfp_camera_seat_stats(&out1);
    CHECK(out1.gliding == 0 && out1.exits == 1 && out1.entries == 1);
    std::printf("out of the suit%s: back in %d frames, largest step %.3f m\n", smooth ? " (continuity on)" : "", back, (double)step_most);
    g.end();
  }

  // ---- 2. Into the suit from the native view: the glide starts where the game drew the rig eye last. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 6; ++i) { g.hip(); g.frame(); }
    for (int i = 0; i < 10; ++i) { g.native(); g.frame(); }
    const float drawn[3] = {g.s.camera_world[0], g.s.camera_world[1], g.s.camera_world[2]};
    g.climb_in(.2f, -.2f); g.look(suit); g.hip(4.0f, 1.0f);
    g.frame();
    CHECK(g.own && apart(sent.position, drawn) < 1e-5f);
    for (int i = 0; i < 25; ++i) { g.hip(4.0f, 1.0f); g.frame(); }
    float eye[3]; g.seat_eye(sitting, eye);
    CHECK(g.own && apart(sent.position, eye) < 1e-4f && degrees(g.out, g.given()) < .01f);
    g.end();
  }

  // ---- 3. Nothing shown within 250 ms to start from: a cut, straight onto the seat's eye (never the game's camera). ----
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); }
    clock_ms += 400;
    g.climb_in(.1f, .5f); g.look(suit); g.hip(4.0f, 1.0f); g.frame();
    float eye[3]; g.seat_eye(sitting, eye);
    CHECK(g.own && apart(sent.position, eye) < 1e-4f && degrees(g.out, g.given()) < .01f);
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.entries == 1 && seat.in_ms == 0 && seat.cuts == 1 && seat.glided == 0);
    g.end();
  }

  // ---- 4. What the seat rule leaves as it was. A valve (seated, on foot): held at the head, the seated view easing to
  // the game's direction on 180 ms. A thrown body (off foot, not seated) and a body killed in its seat (seated, off
  // foot, ragdoll flag): held at the head. With the lock off, a seat is retail's: the game's frame, refused for not
  // being on foot (error 18). On foot, through the native view and back: no seat frame. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); CHECK(g.own); }
    for (int i = 0; i < 8; ++i) { g.native(); g.frame(); }
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); CHECK(g.own); }
    TfpCameraSeatStats foot{}; tfp_camera_seat_stats(&foot); CHECK(foot.frames == 0 && foot.entries == 0);
    g.seat(true); g.look(suit);
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0 && apart(sent.position, g.s.head) < .3f); }
    CHECK(degrees(g.out, yaw_q(suit)) > 1); // the valve's seated view: not there in one frame
    g.seat(false); g.s.state = 50;
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0 && apart(sent.position, g.s.head) < .3f); }
    g.seat(true); g.ragdoll = true;
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.frames == 0 && seat.entries == 0);
    CHECK(tfp_camera_lock_seat_body() == 0);
    g.end();
  }
  {
    fresh(false);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); }
    g.climb_in(.1f, .5f); g.look(suit);
    for (int i = 0; i < 5; ++i) { g.hip(4.0f, 1.0f); g.frame(); CHECK(g.forwarded_untouched() && g.error == 18); }
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.frames == 0 && seat.entries == 0);
    g.end();
  }

  // ---- 5. The lock switched off in the seat and on again out of it: no glide is left over from the seat. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); }
    g.climb_in(.1f, .5f); g.look(suit);
    for (int i = 0; i < 25; ++i) { g.hip(4.0f, 1.0f); g.frame(); }
    tfp_camera_set_lock(false, .35f, 8.0f, 300, .7f, 35.0f, 0, true);
    g.hip(4.0f, 1.0f); g.frame(); CHECK(g.forwarded_untouched());
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.exits == 1 && seat.out_ms == 0 && !seat.seated);
    g.climb_out(2, .5f); g.look(first);
    for (int i = 0; i < 5; ++i) { g.hip(); g.frame(); }
    tfp_camera_set_lock(true, .35f, 8.0f, 300, .7f, 35.0f, 0, true);
    for (int i = 0; i < 5; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    CHECK(g.from_head(first) < .07f);
    g.end();
  }

  // ---- 6. Leaving the seat straight into the native view (the checker's sequence A, 2026-11-25: a 2.52 m cut):
  // the glide out runs inside the native frames too. Its first frame is the seat's last eye, no step over 0.15 m, the
  // rig eye reached within 600 ms; the seat body stays published until the glide ends and not after. With the
  // continuity (Smooth eye) off and on. ----
  for (int smooth = 0; smooth < 2; ++smooth) {
    fresh(true, smooth != 0);
    Game g; board(g);
    const float seat_last[3] = {sent.position[0], sent.position[1], sent.position[2]};
    g.climb_out(2, -.2f); g.look(first); g.native(); g.frame(8.3);
    float last[3]; track(g, last);
    CHECK(g.error == 0 && g.own && apart(last, seat_last) < 1e-4f);
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat);
    CHECK(seat.exits == 1 && !seat.seated && seat.out_ms >= 300 && seat.out_ms <= 600 && seat.gliding == 2 && seat.out_metres > 1.5f);
    CHECK(tfp_camera_lock_seat_body() == BODY);
    float most = 0; int arrived = -1;
    for (int i = 1; i < 120; ++i) {
      g.native(); g.frame(8.3); CHECK(g.error == 0);
      float shown[3]; track(g, shown); const float step = apart(shown, last); if (step > most) most = step; std::memcpy(last, shown, 12);
      float rig[3]; rig_eye(g, rig);
      if (arrived < 0 && apart(shown, rig) < 1e-4f) arrived = i;
      if (i == 30) CHECK(tfp_camera_lock_seat_body() == BODY);
    }
    CHECK(arrived > 0 && arrived <= 73 && most < .15f);
    TfpCameraSeatStats out{}; tfp_camera_seat_stats(&out);
    CHECK(out.gliding == 0 && out.exits == 1 && tfp_camera_lock_seat_body() == 0);
    // The glide over, head-camera frames after it begin no glide from anywhere.
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(8.3); CHECK(g.own && g.error == 0); }
    TfpCameraSeatStats after{}; tfp_camera_seat_stats(&after); CHECK(after.gliding == 0 && after.exits == 1);
    std::printf("out of the suit into the native view%s: rig eye in %d frames, largest step %.3f m\n", smooth ? " (continuity on)" : "", arrived, (double)most);
    g.end();
  }

  // ---- 7. The glide out interrupted by native frames (the checker's sequence B: 2.46 m to the head, then 2.21 m
  // back toward the cockpit): 8 head-camera frames, 8 native ones, then the head camera. Every change of camera
  // starts the glide again from the view last shown; it never resumes from the cockpit. Continuity off and on. ----
  for (int smooth = 0; smooth < 2; ++smooth) {
    fresh(true, smooth != 0);
    Game g; board(g);
    float last[3] = {sent.position[0], sent.position[1], sent.position[2]}, most = 0; int at = -1, back = -1;
    g.climb_out(2, -.2f); g.look(first);
    for (int i = 0; i < 120; ++i) {
      if (i >= 8 && i < 16) g.native(); else g.hip();
      g.frame(8.3); CHECK(g.error == 0 && g.own);
      float shown[3]; track(g, shown); const float step = apart(shown, last); if (step > most) { most = step; at = i; } std::memcpy(last, shown, 12);
      if (i >= 16 && back < 0 && g.from_head(first) < .07f) back = i;
      if (i == 12 || i == 20) CHECK(tfp_camera_lock_seat_body() == BODY);
    }
    CHECK(most < .15f);
    CHECK(back > 16 && back <= 16 + 73 && g.from_head(first) < .07f);
    TfpCameraSeatStats out{}; tfp_camera_seat_stats(&out);
    CHECK(out.gliding == 0 && out.exits == 1 && tfp_camera_lock_seat_body() == 0);
    std::printf("out of the suit, head/native/head%s: largest step %.3f m (frame %d), head reached at frame %d\n", smooth ? " (continuity on)" : "", (double)most, at, back);
    g.end();
  }

  // ---- 7b. Late in the glide out, the native view with its rig eye 0.6 m from the head camera's eye, then the head
  // camera again: each change of camera begins the glide again from the view last shown, so neither change is a
  // step (a glide that only changed its target would show most of the 0.6 m at once). Continuity off and on. ----
  for (int smooth = 0; smooth < 2; ++smooth) {
    fresh(true, smooth != 0);
    Game g; board(g);
    float last[3] = {sent.position[0], sent.position[1], sent.position[2]}, most = 0;
    g.climb_out(2, -.2f); g.look(first);
    for (int i = 0; i < 170; ++i) {
      if (i >= 50 && i < 75) g.native(.7f); else g.hip();
      g.frame(8.3); CHECK(g.error == 0 && g.own);
      float shown[3]; track(g, shown); const float step = apart(shown, last); if (step > most) most = step; std::memcpy(last, shown, 12);
    }
    CHECK(most < .15f && g.from_head(first) < .07f);
    TfpCameraSeatStats out{}; tfp_camera_seat_stats(&out); CHECK(out.gliding == 0 && tfp_camera_lock_seat_body() == 0);
    std::printf("out of the suit, a far rig eye late in the glide%s: largest step %.3f m\n", smooth ? " (continuity on)" : "", (double)most);
    g.end();
  }

  // ---- 8. In the middle of the glide out: a camera that is neither the head camera nor the native view is held by
  // the lock as the head camera is, so the glide runs on through it, continuous; a frame the lock does not show (the
  // lock off for a frame) ends it, and the head-camera frames after it never glide back from the cockpit. ----
  {
    fresh(true);
    Game g; board(g);
    g.climb_out(2, -.2f); g.look(first);
    float last[3] = {sent.position[0], sent.position[1], sent.position[2]}, most = 0;
    for (int i = 0; i < 12; ++i) {
      if (i >= 5 && i < 8) g.hip(4.0f, 1.0f, 3); else g.hip();
      g.frame(8.3); CHECK(g.own && g.error == 0);
      const float step = apart(sent.position, last); if (step > most) most = step; std::memcpy(last, sent.position, 12);
    }
    TfpCameraSeatStats mid{}; tfp_camera_seat_stats(&mid);
    CHECK(most < .15f && mid.gliding == 2 && g.from_head(first) > .3f && tfp_camera_lock_seat_body() == BODY);
    // The camera still linked to the rig in a head-camera request, with no native view to repeat: the game's frame
    // is shown, not the lock's; the glide ends there and does not come back.
    g.hip(); g.s.linked = true; g.frame(8.3); CHECK(!g.own);
    g.hip(); g.frame(8.3); CHECK(g.own && g.error == 0 && g.from_head(first) < .07f);
    TfpCameraSeatStats gone{}; tfp_camera_seat_stats(&gone);
    CHECK(gone.gliding == 0 && tfp_camera_lock_seat_body() == 0);
    // Once more into the suit and out, then the lock off for a frame in the glide out: the glide ends there too.
    for (int i = 0; i < 120; ++i) { g.climb_in(.1f, -.2f); g.look(.7f); g.hip(4.0f, 1.0f); g.frame(8.3); }
    g.climb_out(2, -.2f); g.look(first);
    for (int i = 0; i < 5; ++i) { g.hip(); g.frame(8.3); }
    TfpCameraSeatStats again{}; tfp_camera_seat_stats(&again); CHECK(again.gliding == 2 && again.exits == 2);
    tfp_camera_set_lock(false, .35f, 8.0f, 300, .7f, 35.0f, 0, true);
    g.hip(); g.frame(8.3); // retail's head camera on foot: no seat glide in it
    TfpCameraSeatStats off{}; tfp_camera_seat_stats(&off);
    CHECK(off.gliding == 0 && tfp_camera_lock_seat_body() == 0);
    tfp_camera_set_lock(true, .35f, 8.0f, 300, .7f, 35.0f, 0, true);
    for (int i = 0; i < 3; ++i) { g.hip(); g.frame(8.3); CHECK(g.own && g.error == 0 && g.from_head(first) < .07f); }
    TfpCameraSeatStats out{}; tfp_camera_seat_stats(&out);
    CHECK(out.gliding == 0 && out.exits == 2 && tfp_camera_lock_seat_body() == 0);
    g.end();
  }

  // ---- 9. Any seated vehicle (an FRV's seat: no suit, the root driven at 20 m/s and turning, the head lower): first
  // person kept, the eye on the root's anchor, the game's direction; the diver hidden (the seat body). What is seen
  // through is the head visibility's business, and only an exosuit is (cockpit_find_test, head_visibility_test). ----
  {
    fresh(true);
    static const float frv[3] = {.35f, -.1f, .62f}; // a passenger's seat: to the side, lower
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(8.3); }
    float x = 0, worst = 0;
    for (int i = 0; i < 240; ++i) {
      x += .166f; g.root_yaw = (float)i / 240 * 1.0f;
      g.seat(true); g.s.state = 25; g.place(x, -.2f, .9f, frv); g.look(.3f + (float)i * .01f); g.hip(5.0f, 1.5f); g.frame(8.3);
      CHECK(g.own && g.error == 0);
      if (i >= 80) {
        float eye[3]; g.seat_eye(frv, eye); const float d = apart(sent.position, eye); if (d > worst) worst = d;
        const Q q = g.given(); CHECK(!std::memcmp(&g.out, &q, 16)); // the game's own rotation, bit for bit
      }
    }
    CHECK(worst < 1e-3f && tfp_camera_lock_seat_body() == BODY);
    std::printf("in an FRV's seat: the eye within %.5f m of the seat's anchor at 20 m/s\n", (double)worst);
    g.end();
  }

  // ---- 10. The continuity (Smooth eye) begins again from what is shown once the glide out has ended: nothing it
  // carried through the glide is eased in after it (camera_helper.c seat_out_end). The trace says so: the frame after
  // the glide's last starts the continuity from nothing (TFP_CAMERA_TRACE_START). ----
  {
    fresh(true, true); tfp_camera_set_continuity(true, true);
    Game g; board(g);
    g.climb_out(2, -.2f); g.look(first);
    double after_end = -1; bool was_gliding = true;
    for (int i = 0; i < 100; ++i) {
      if (i == 50) tfp_camera_trace_edge();
      g.hip(); g.frame(8.3); CHECK(g.own && g.error == 0);
      TfpCameraSeatStats st{}; tfp_camera_seat_stats(&st);
      if (!was_gliding && after_end < 0) after_end = clock_ms; // the first frame after the one that ended the glide
      if (was_gliding && st.gliding == 0) was_gliding = false;
    }
    static TfpCameraTraceRow rows[512]; const uint32_t count = tfp_camera_trace_read(rows, 512);
    bool started = false, found = false;
    for (uint32_t i = 0; i < count; ++i) if (rows[i].tick == after_end) { found = true; started = (rows[i].what & TFP_CAMERA_TRACE_START) != 0; }
    CHECK(after_end > 0 && found && started);
    g.end();
  }

  // ---- 11. No kick carried over a seat. The kick (the Bodycam kick sliders are on) follows the head node against
  // the view; the seat lets its memory go. Leaving with the view as it was before the seat and the head nodded 20
  // degrees since (the climb out): the first head-camera frames after the seat begin the kick from that head, and
  // kick nothing. Kept, the kick would take the 20 degrees as the head nodding in one frame. ----
  {
    fresh(true);
    Game g; board(g);
    TfpCameraLockStats before{}; tfp_camera_lock_stats(&before);
    g.climb_out(2, -.2f); g.look(first); g.head_pitch = .35f;
    float worst = 0;
    for (int i = 0; i < 120; ++i) {
      g.hip(); g.frame(8.3); CHECK(g.own && g.error == 0);
      TfpCameraSeatStats st{}; tfp_camera_seat_stats(&st);
      if (st.gliding == 0) { const float d = degrees(g.out, yaw_q(first)); if (d > worst) worst = d; }
    }
    TfpCameraLockStats after{}; tfp_camera_lock_stats(&after);
    CHECK(before.peak_kick_degrees < .5f && after.peak_kick_degrees < .5f && worst < .5f);
    std::printf("out of the suit with the head nodded 20 degrees: %.3f degrees from the game's, peak kick %.3f\n", (double)worst, (double)after.peak_kick_degrees);
    g.end();
  }

  std::printf("camera seat: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
