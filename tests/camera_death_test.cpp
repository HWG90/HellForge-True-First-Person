// The camera lock on a body that physics has taken (thrown, or dead): src/camera_helper.c, beyond retail. On the
// fake game memory of the differential test (this build's layout); every frame is one call of the hook as the
// game's call site makes it. What the game did on the first death under the lock (2026-11-22, session b08c8327):
// the request stayed kind 4 and the player's own, the body unit and its node graph stayed the same, the unit root
// stayed where the character died and the skeleton left it (head 1.56 m from the root alive, 4.28 m at rest), and
// the body left the on-foot animation state. Judged here: the eye stays at the head, the direction is the last
// first-person one, nothing is followed without the lock, and a head that is not attached to its neck is refused.
// No retail source needed. Linux (process_vm_readv).
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

enum { PHYSICS = 0x547844, BODY = 0x400003 }; // the avatar's physics byte (bit 0x10: ragdoll), stride 0x78
struct Q { float x, y, z, w; };
static Q yaw_q(float yaw) { return {0, 0, std::sin(yaw / 2), std::cos(yaw / 2)}; }
static float degrees(Q a, Q b) {
  const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
  return 2 * std::acos(d > 1 ? 1 : d) * 57.29578f;
}

struct Game {
  Scene s;
  float right[3] = {1, 0, 0}, ahead[3] = {0, 1, 0}, above[3] = {0, 0, 1};
  float neck[3] = {0, 0, 1.4f};   // the head's parent node in the world
  float head_pitch = 0;           // the head node's own rotation about its right axis (for the kick)
  bool ragdoll = false, anim_gone = false;
  uint64_t gen = 0;
  Q out = {0, 0, 0, 1}; bool own = false; uint32_t error = 0;
  void stand(float x, float y, float z) {
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    std::memcpy(s.root, m, sizeof m);
    head(x + .03f, y + .06f, z + 1.56f);
  }
  void head(float x, float y, float z, float neck_away = .1f) { s.head[0] = x; s.head[1] = y; s.head[2] = z; neck[0] = x; neck[1] = y; neck[2] = z - neck_away; }
  void look(float yaw) {
    const float c = std::cos(yaw), n = std::sin(yaw);
    right[0] = c; right[1] = n; right[2] = 0; ahead[0] = -n; ahead[1] = c; ahead[2] = 0; above[0] = 0; above[1] = 0; above[2] = 1;
    for (int i = 0; i < 3; ++i) { s.context[i] = right[i]; s.context[4 + i] = ahead[i]; s.context[8 + i] = above[i]; s.camera_rotation[i] = right[i]; s.camera_rotation[4 + i] = ahead[i]; s.camera_rotation[8 + i] = above[i]; }
    looked = yaw_q(yaw); std::memcpy(s.argument_rotation, &looked, 16);
  }
  Q looked = {0, 0, 0, 1}; // the look as a rotation in the world: the ordinary camera's argument
  // The game's ordinary camera (kind 4), `back` metres behind the head.
  void hip(float back = 2.0f) {
    s.request[0] = 4; s.linked = false; s.mask = 0; s.aim = 0; s.camera_rotation_given = false;
    for (int i = 0; i < 3; ++i) s.argument[i] = s.head[i] - ahead[i] * back + above[i] * .4f;
    s.argument[3] = 0; std::memcpy(s.argument_rotation, &looked, 16);
  }
  void native() {
    s.request[0] = 6; s.linked = true; s.mask = 0; s.aim = 0; s.camera_rotation_given = true;
    for (int i = 0; i < 3; ++i) { s.rig[i] = right[i]; s.rig[4 + i] = ahead[i]; s.rig[8 + i] = above[i]; s.rig[12 + i] = s.head[i] + ahead[i] * .1f + above[i] * .05f; }
    s.rig[3] = s.rig[7] = s.rig[11] = 0; s.rig[15] = 1;
    s.argument[0] = .02f; s.argument[1] = .01f; s.argument[2] = .03f; s.argument[3] = 0;
    // Round 49: in the native view the rotation argument is local to the rig eye, whose axes are the look's here: none.
    const Q none = {0, 0, 0, 1}; std::memcpy(s.argument_rotation, &none, 16);
  }
  void begin_config(TfpCameraConfig &c) {
    c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA]; c.avatar_owner = W.a[A_OWNER];
    c.avatar_index = 0; c.avatar = 0x333; c.body = BODY; c.camera_unit = 0x400004; c.forward = .18f; c.up = .04f; c.smoothing_ms = 100; c.blend_ms = 120; c.max_gap_ms = 250;
  }
  void write() {
    W.apply(s, 0);
    World::put(W.a[A_BODY_WORLD] + HEAD_PARENT * 64 + 0x30, neck);
    const float c = std::cos(head_pitch), n = std::sin(head_pitch);
    const float rows[12] = {1, 0, 0, 0, 0, c, n, 0, 0, -n, c, 0};
    World::put(W.a[A_BODY_WORLD] + HEAD_NODE * 64, rows);
    World::put<uint8_t>(W.a[A_OWNER] + PHYSICS, ragdoll ? 0x14 : 0x04);
    World::put<uint64_t>(W.a[A_BODY] + 0x178, anim_gone ? 0 : W.a[A_ANIM_INSTANCE]);
  }
  bool try_begin() {
    TfpCameraConfig c{}; begin_config(c);
    World::put<uint64_t>(W.slot(), W.native_value);
    write();
    return tfp_camera_begin(&c, &gen) == 1;
  }
  void begin() {
    if (!try_begin() || tfp_camera_set_presentation(gen) != 1 || tfp_camera_set_cut_hold(gen, 15) != 1) {
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
  // How far the forwarded eye is from the head camera's place for a view along `yaw`: 0.18 m ahead (level) and 0.04 m above the head.
  float from_head(float yaw) const {
    const float want[3] = {s.head[0] - std::sin(yaw) * .18f, s.head[1] + std::cos(yaw) * .18f, s.head[2] + .04f};
    float d = 0; for (int i = 0; i < 3; ++i) d += (sent.position[i] - want[i]) * (sent.position[i] - want[i]);
    return std::sqrt(d);
  }
  // Alive: 12 frames of the ordinary camera, then 20 of the native view, looking along `yaw`.
  void alive(float yaw) {
    look(yaw); hip(); begin();
    for (int i = 0; i < 12; ++i) { hip(); frame(); }
    for (int i = 0; i < 20; ++i) { native(); frame(); }
  }
  // Death as the game showed it: the ordinary request again, the gun gone, the body out of the on-foot state.
  void die() { s.state = 50; ragdoll = true; }
};

// `bodycam`: the camera style (src/camera_damp.h); `gain`, `max`: the kick on foot. Every scenario but 6c is Bodycam with no kick on foot.
static void fresh(bool lock, bool smooth = false, float thrown_gain = 0, float thrown_max = 0, float damping_ms = 0, bool bodycam = true, float gain = 0, float max = 0) {
  tfp_camera_reset_for_tests();
  TfpCameraHost host{};
  host.hook = reinterpret_cast<uint64_t>(&hook_marker); host.read = host_read; host.readable_span = host_readable_span; host.now = host_now;
  host.writable_slot = host_true_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock; host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_camera_set_host(&host);
  tfp_camera_set_lock(lock, gain, max, 300, thrown_gain, thrown_max, damping_ms, bodycam);
  tfp_camera_set_continuity(smooth, false);
  clock_ms += 5000;
}

int main() {
  W.build(HERE);
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  const float first = .4f, corpse_camera = 2.0f; // the last first-person direction; where the game's camera on the corpse looks

  // ---- 1. Death under the lock: the body is thrown 4.3 m from where it died and lies there. Every frame is the
  // head camera at the head, along the last first-person direction, whatever the game's own camera does. ----
  for (int smooth = 0; smooth < 2; ++smooth) {
    fresh(true, smooth != 0);
    Game g; g.stand(3, -2, .5f); g.alive(first);
    TfpCameraLockStats before{}; tfp_camera_lock_stats(&before);
    TfpCameraDownStats down0{}; tfp_camera_down_stats(&down0);
    CHECK(down0.frames == 0 && down0.refused == 0 && tfp_camera_lock_off_foot_body() == 0);
    g.die();
    float worst_eye = 0, worst_turn = 0; int replaced = 0;
    for (int i = 0; i < 60; ++i) {
      const float t = i < 20 ? (i + 1) / 20.0f : 1; // 0.67 s of flight, then at rest
      g.head(3.03f + 4.2f * t, -1.94f + .5f * t, 2.06f - 1.36f * t);
      g.look(corpse_camera + .02f * i); g.hip(3.7f);
      g.frame();
      if (g.own && g.error == 0) ++replaced;
      if (g.from_head(first) > worst_eye) worst_eye = g.from_head(first);
      if (degrees(g.out, yaw_q(first)) > worst_turn) worst_turn = degrees(g.out, yaw_q(first));
    }
    CHECK(replaced == 60);
    CHECK(worst_eye < .45f);              // the held native eye and, with continuity on, the catch-up of the change of camera
    CHECK(g.from_head(first) < .07f);     // at rest: at the head
    CHECK(worst_turn < .5f);              // the last first-person direction, not the corpse camera's
    TfpCameraLockStats after{}; tfp_camera_lock_stats(&after);
    CHECK(after.missed == before.missed && after.frames == before.frames + 60);
    CHECK(after.off_foot == 1 && after.ragdoll == 1 && after.head_distance > 4.0f);
    TfpCameraDownStats down{}; tfp_camera_down_stats(&down);
    CHECK(down.frames == 60 && down.refused == 0 && down.kind == 4);
    CHECK(std::fabs(down.head[0] - g.s.head[0]) < 1e-4f && std::fabs(down.head[1] - g.s.head[1]) < 1e-4f && std::fabs(down.head[2] - g.s.head[2]) < 1e-4f);
    CHECK(tfp_camera_lock_off_foot_body() == BODY); // what head hiding asks: the lock holds the head camera on this body, off foot
    clock_ms += 400;
    CHECK(tfp_camera_lock_off_foot_body() == 0);    // not after 250 ms without such a frame
    g.end();
  }
  // ---- 2. The same death without the lock: nothing is followed, the game's camera is forwarded untouched
  // (retail: the head camera needs the on-foot state, and a head within 3 m of the unit root). ----
  {
    fresh(false);
    Game g; g.stand(3, -2, .5f); g.alive(first);
    g.die(); g.head(7.2f, -1.4f, .7f);
    for (int i = 0; i < 5; ++i) { g.look(corpse_camera); g.hip(3.7f); g.frame(); CHECK(!g.own && g.error == 18); }
    g.s.state = 36; // on foot by its animation state, the head still out of range
    for (int i = 0; i < 5; ++i) { g.look(corpse_camera); g.hip(3.7f); g.frame(); CHECK(!g.own && g.error == 16); }
    TfpCameraDownStats down{}; tfp_camera_down_stats(&down);
    CHECK(down.frames == 0 && down.refused == 0 && tfp_camera_lock_off_foot_body() == 0);
    g.end();
  }
  // ---- 3. A head that is nowhere near its neck (more than 3 m apart) is not followed, lock or
  // not: refused with error 16 and the reason, and counted as a refused frame of a body that is down. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, 0); g.alive(first);
    g.die();
    for (int i = 0; i < 4; ++i) { g.head(4.2f, 0, .3f); g.look(corpse_camera); g.hip(3.7f); g.frame(); CHECK(g.own && g.error == 0); }
    // Seen in the game 2026-11-23: a body torn by a grenade has its head off the neck (or exactly on it); it is this
    // unit's own head and is followed. Only a head nowhere near the body (more than 3 m from its neck) is refused.
    for (int i = 0; i < 6; ++i) { g.head(4.2f, 0, .3f, i < 3 ? 2.0f : 0.0f); g.look(corpse_camera); g.hip(3.7f); g.frame(); CHECK(g.own && g.error == 0); }
    for (int i = 0; i < 6; ++i) { g.head(4.2f, 0, .3f, 5.0f); g.look(corpse_camera); g.hip(3.7f); g.frame(); CHECK(!g.own && g.error == 16); }
    TfpCameraLockStats lock{}; tfp_camera_lock_stats(&lock);
    CHECK(lock.missed == 6 && lock.missed_kind == 4 && lock.missed_error == 16 && lock.missed_why == TFP_CAMERA_WHY_NECK);
    TfpCameraDownStats down{}; tfp_camera_down_stats(&down);
    CHECK(down.frames == 10 && down.refused == 6 && down.error == 16 && down.why == TFP_CAMERA_WHY_NECK);
    CHECK(std::strstr(tfp_camera_why_text(down.why), "neck") != nullptr);
    g.end();
  }
  // ---- 4. Off foot under the lock with the head in walking range (the first frames of a death; an action state
  // nobody has seen): held. The final proof no longer compares this frame with an on-foot sample of another time.
  // Not thrown: the direction stays the game's. And a body whose animation instance cannot be read is held too. ----
  {
    fresh(true);
    Game g; g.stand(1, 1, 0); g.alive(first);
    g.s.state = 50;
    // (Round 49: the game's direction is 92 degrees off the native view's here, under the 120 that is another view; the
    // difference is carried across the change of camera and eased out, 688 ms, src/camera_turn.h: not in one frame.)
    Q before = yaw_q(first); // the native view was shown along the rig eye
    for (int i = 0; i < 30; ++i) { g.look(corpse_camera); g.hip(); g.frame(); CHECK(g.own && g.error == 0);
      CHECK(degrees(before, g.out) < 200 * .0333f + .1f); before = g.out; }
    CHECK(degrees(g.out, yaw_q(corpse_camera)) < .5f);
    TfpCameraDownStats down{}; tfp_camera_down_stats(&down);
    CHECK(down.frames == 0 && tfp_camera_lock_off_foot_body() == BODY);
    g.anim_gone = true;
    for (int i = 0; i < 10; ++i) { g.look(corpse_camera); g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    g.anim_gone = false; g.write();
    g.end();
  }
  // ---- 5. A ragdoll that is survived: thrown, then on foot again. The direction goes back to the game's without
  // a step (eased over a few frames), and the eye is at the head throughout. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, 0); g.alive(first);
    g.die();
    for (int i = 0; i < 30; ++i) { g.head(.03f + .1f * i, .06f, 1.56f - .04f * i); g.look(corpse_camera); g.hip(); g.frame(); CHECK(g.own && degrees(g.out, yaw_q(first)) < .5f); }
    g.s.state = 36; g.ragdoll = false; g.stand(2.9f, 0, 0);
    Q last = g.out; float worst = 0;
    for (int i = 0; i < 30; ++i) {
      g.look(corpse_camera); g.hip(); g.frame();
      CHECK(g.own && g.error == 0);
      if (degrees(g.out, last) > worst) worst = degrees(g.out, last);
      last = g.out;
    }
    CHECK(worst < 30 && degrees(g.out, yaw_q(corpse_camera)) < .5f); // 92 degrees apart: never in one frame
    g.end();
  }
  // ---- 6. The kick on a thrown body uses the thrown tuning (70 % up to 35 degrees), about the held direction. ----
  {
    fresh(true, false, .7f, 35);
    Game g; g.stand(0, 0, 0); g.alive(first);
    g.die();
    for (int i = 0; i < 10; ++i) { g.head(.03f + .1f * i, .06f, 1.5f); g.look(corpse_camera); g.hip(); g.frame(); }
    CHECK(degrees(g.out, yaw_q(first)) < .5f);
    g.head_pitch = .5f; // the head thrown 28.6 degrees in one frame
    g.head(1.03f, .06f, 1.5f); g.look(corpse_camera); g.hip(); g.frame();
    const float kicked = degrees(g.out, yaw_q(first));
    CHECK(g.own && kicked > 15 && kicked < 21);
    for (int i = 0; i < 90; ++i) { g.look(corpse_camera); g.hip(); g.frame(); }
    CHECK(degrees(g.out, yaw_q(first)) < .5f); // settled: the held direction again
    g.end();
  }
  // ---- 6b. The same with camera damping at 60 % (180 ms): the kick comes into the view over several frames, never
  // by more in one frame than the time constant allows, stays well under its undamped size, and settles the same. ----
  {
    fresh(true, false, .7f, 35, 180);
    Game g; g.stand(0, 0, 0); g.alive(first);
    g.die();
    for (int i = 0; i < 10; ++i) { g.head(.03f + .1f * i, .06f, 1.5f); g.look(corpse_camera); g.hip(); g.frame(); }
    CHECK(degrees(g.out, yaw_q(first)) < .5f);
    g.head_pitch = .5f;
    float peak = 0, most = 0; Q last = g.out;
    for (int i = 0; i < 120; ++i) {
      g.head(1.03f, .06f, 1.5f); g.look(corpse_camera); g.hip(); g.frame();
      peak = std::fmax(peak, degrees(g.out, yaw_q(first))); most = std::fmax(most, degrees(g.out, last)); last = g.out;
    }
    CHECK(g.own && peak > 4 && peak < 12 && most < 21 * 33.3f / 180 * 1.05f);
    CHECK(degrees(g.out, yaw_q(first)) < .5f);
    g.end();
  }
  // ---- 6c. The camera style, at the options' defaults (35 % up to 8 degrees on foot, 70 % up to 35 thrown, camera
  // damping 60 %). The same thrown head as 6b: Stable still kicks, at half the thrown strength and on 250 ms, so
  // clearly less than Bodycam and never by more in one frame than 250 ms allows. And on foot, in the ordinary camera
  // and in the native view, a head that nods 28.6 degrees turns Bodycam's view and leaves Stable's exactly the game's. ----
  {
    float peak[2] = {}, most[2] = {};
    for (int stable = 0; stable < 2; ++stable) {
      fresh(true, false, .7f, 35, 180, !stable, .35f, 8);
      Game g; g.stand(0, 0, 0); g.alive(first);
      g.die();
      for (int i = 0; i < 10; ++i) { g.head(.03f + .1f * i, .06f, 1.5f); g.look(corpse_camera); g.hip(); g.frame(); }
      CHECK(degrees(g.out, yaw_q(first)) < .5f);
      g.head_pitch = .5f;
      Q last = g.out;
      for (int i = 0; i < 120; ++i) {
        g.head(1.03f, .06f, 1.5f); g.look(corpse_camera); g.hip(); g.frame();
        peak[stable] = std::fmax(peak[stable], degrees(g.out, yaw_q(first))); most[stable] = std::fmax(most[stable], degrees(g.out, last)); last = g.out;
      }
      CHECK(g.own && degrees(g.out, yaw_q(first)) < .5f);
      g.end();
    }
    std::printf("a thrown head, 28.6 degrees in one frame: Bodycam kicks %.2f degrees (%.2f in one frame), Stable %.2f (%.2f)\n", peak[0], most[0], peak[1], most[1]);
    CHECK(peak[0] > 4 && peak[1] > 1 && peak[1] < peak[0] * .6f && most[1] < most[0] * .6f && most[1] < 28.7f * .35f * 33.3f / 250 * 1.05f);
    for (int view = 0; view < 2; ++view) { // the ordinary camera, the native view
      float turned[2] = {};
      for (int stable = 0; stable < 2; ++stable) {
        fresh(true, false, .7f, 35, 180, !stable, .35f, 8);
        Game g; g.stand(0, 0, 0); g.alive(first);
        for (int i = 0; i < 10; ++i) { if (view) g.native(); else g.hip(); g.frame(); }
        const Q before = g.out;
        g.head_pitch = .5f;
        bool exact = true;
        for (int i = 0; i < 30; ++i) {
          if (view) g.native(); else g.hip();
          g.frame();
          turned[stable] = std::fmax(turned[stable], degrees(g.out, before));
          if (std::memcmp(&g.out, &before, 16)) exact = false;
        }
        CHECK((view || g.own) && exact == (stable != 0)); // (the native view is the game's own buffer)
        g.end();
      }
      std::printf("on foot, the head nods 28.6 degrees, %s: Bodycam turns the view %.2f degrees, Stable %.2f\n", view ? "the native view" : "the ordinary camera", turned[0], turned[1]);
      CHECK(turned[0] > 1 && turned[0] < 8.01f && turned[1] == 0);
    }
  }
  // ---- 7. Arming on a thrown body (the helper was ended and is begun again while the body is down): with the
  // lock the begin proof takes the head by its neck; without it the 3 m bound stands. ----
  for (int lock = 1; lock >= 0; --lock) {
    fresh(lock != 0);
    Game g; g.stand(0, 0, 0); g.head(4.2f, 0, .3f); g.look(first); g.hip();
    const bool armed = g.try_begin();
    CHECK(armed == (lock != 0));
    if (!armed) CHECK(tfp_camera_error() == 16);
    else g.end();
  }
  // ---- 8. A death that does not set the ragdoll flag: off foot, the head still in walking range (1.7 m) while the
  // game's camera circles the corpse, then beyond 3 m (4.3 m at rest). While the head is in range the direction is
  // the game's (a dive looks the same); once the body is down it is the last first-person one, not the direction
  // the game's corpse camera happened to have, and it gets there without a step. ----
  {
    fresh(true);
    Game g; g.stand(3, -2, .5f); g.alive(first);
    g.s.state = 50; // no ragdoll flag
    float orbit = first;
    for (int i = 0; i < 15; ++i) { // 0.5 s: the head 1.7 m from the root, the game's camera turning 4 degrees a frame
      orbit += .07f; g.head(3.4f, -1.6f, 2.0f); g.look(orbit); g.hip(3.7f); g.frame();
      CHECK(g.own && g.error == 0);
    }
    CHECK(degrees(g.out, yaw_q(orbit)) < .5f && degrees(yaw_q(orbit), yaw_q(first)) > 55);
    TfpCameraDownStats down0{}; tfp_camera_down_stats(&down0); CHECK(down0.frames == 0);
    Q last = g.out; float worst = 0;
    for (int i = 0; i < 45; ++i) {
      const float t = i < 10 ? (i + 1) / 10.0f : 1;
      orbit += .07f; g.head(3.4f + 3.8f * t, -1.6f + .2f * t, 2.0f - 1.3f * t); g.look(orbit); g.hip(3.7f); g.frame();
      CHECK(g.own && g.error == 0);
      if (degrees(g.out, last) > worst) worst = degrees(g.out, last);
      last = g.out;
    }
    CHECK(degrees(g.out, yaw_q(first)) < .5f); // the direction before death
    CHECK(g.from_head(first) < .07f);          // and the eye placed along it
    CHECK(worst < 30);                         // 88 degrees apart when the body is down: never in one frame
    TfpCameraLockStats lock{}; tfp_camera_lock_stats(&lock);
    CHECK(lock.ragdoll == 0 && lock.off_foot == 1 && lock.head_distance > 4.0f);
    TfpCameraDownStats down{}; tfp_camera_down_stats(&down); CHECK(down.frames > 30 && down.refused == 0);
    g.end();
  }
  // ---- 9. Camera damping and a body that is down. Down by the head alone (4 m from the root, the animation state
  // still on foot, no ragdoll flag): nothing of the head's motion is damped, the forwarded bytes are those without
  // damping. And a lag that is carried when the body goes down (the head dropped 0.5 m in a frame while alive:
  // 0.10 m behind) is let go over a few frames, not in one, and the head is followed to within 1 cm inside 150 ms. ----
  {
    static float record[2][120][4]; int frames[2] = {0, 0}; bool held = true;
    for (int damped = 0; damped < 2; ++damped) {
      fresh(true, false, 0, 0, damped ? 300.0f : 0.0f);
      clock_ms = 800000; // the same instants in both runs
      Game g; g.stand(0, 0, 0); g.look(first); g.hip(); g.begin();
      for (int i = 0; i < 20; ++i) { g.hip(); g.frame(10); std::memcpy(record[damped][frames[damped]++], sent.position, 16); }
      for (int i = 1; i <= 60; ++i) { // the head 4.2 m away, jerking 0.05 m down and back within 100 ms
        g.head(4.2f, 0, .3f - (i < 10 ? .05f * std::sin(i / 10.0f * 3.14159265f) : 0)); g.hip(3.7f); g.frame(10);
        if (!g.own || g.error != 0) held = false;
        std::memcpy(record[damped][frames[damped]++], sent.position, 16);
      }
      TfpCameraDownStats down{}; tfp_camera_down_stats(&down); CHECK(down.frames == 60);
      g.end();
    }
    CHECK(held && frames[0] == 80 && frames[1] == 80 && !std::memcmp(record[0], record[1], sizeof record[0][0] * 80));
  }
  {
    static float record[2][120][4]; int frames[2] = {0, 0};
    for (int damped = 0; damped < 2; ++damped) {
      fresh(true, false, 0, 0, damped ? 300.0f : 0.0f);
      clock_ms = 800000;
      Game g; g.stand(0, 0, 0); g.look(first); g.hip(); g.begin();
      for (int i = 0; i < 20; ++i) { g.hip(); g.frame(10); std::memcpy(record[damped][frames[damped]++], sent.position, 16); }
      g.head(.03f, .06f, 1.06f);
      for (int i = 0; i < 2; ++i) { g.hip(); g.frame(10); std::memcpy(record[damped][frames[damped]++], sent.position, 16); }
      g.die();
      for (int i = 1; i <= 60; ++i) { // thrown: 4 m in 0.3 s, then at rest
        const float t = i < 30 ? i / 30.0f : 1;
        g.head(.03f + 4.0f * t, .06f, 1.06f - .8f * t); g.hip(3.7f); g.frame(10);
        std::memcpy(record[damped][frames[damped]++], sent.position, 16);
      }
      g.end();
    }
    auto apart = [&](int i) { float d = 0; for (int k = 0; k < 3; ++k) d += (record[1][i][k] - record[0][i][k]) * (record[1][i][k] - record[0][i][k]); return std::sqrt(d); };
    CHECK(frames[0] == 82 && frames[1] == 82 && apart(21) > .09f && apart(21) < .1001f); // carried at death
    CHECK(apart(22) > .06f && apart(22) < apart(21));                                    // the first frame down: eased
    bool falling = true;
    for (int i = 23; i < 82; ++i) if (apart(i) > apart(i - 1) * .9f + 1e-6f) falling = false; // 10 ms of 40: 0.78 a frame
    CHECK(falling && apart(21 + 15) < .01f);                                              // 150 ms: within 1 cm of the head's eye
    CHECK(!std::memcmp(record[0][21 + 30], record[1][21 + 30], sizeof record[0][0] * 31)); // and gone
    std::printf("damping let go at death: %.3f m carried, %.3f m in the first frame down, %.4f m after 150 ms\n", apart(21), apart(22), apart(36));
  }
  std::printf("camera death: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
