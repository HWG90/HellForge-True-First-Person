// Round 37: an exosuit or vehicle seat under the camera lock (src/camera_helper.c, src/camera_vehicle.h), on the fake
// game memory of the differential test; every frame is one call of the hook as the game's call site makes it. What
// the game did in an EXO-45 cockpit (2026-11-25, feel.55, session 0410a21a): the request stayed kind 4 and the
// player's own, the avatar's seated bit (flags[14] & 4) was set, layer 14 left the on-foot state, no ragdoll flag,
// the body root rose into the cockpit and the head sat 0.84 m above it. Judged here: the camera glides to the game's
// own and is then the game's untouched; leaving the seat glides back to the head camera; a valve (seated on foot),
// a thrown body (off foot, not seated), a body killed in its seat and the lock switched off are what they were.
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

enum { PHYSICS = 0x547844, BODY = 0x400003, SEATED = 14 };
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
  bool ragdoll = false;
  uint64_t gen = 0;
  Q out = {0, 0, 0, 1}; bool own = false; uint32_t error = 0;
  void stand(float x, float y, float z, float head_up = 1.56f) {
    const float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    std::memcpy(s.root, m, sizeof m);
    head(x + .03f, y + .06f, z + head_up);
  }
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
  // The native view. `seen`: the camera's world position the game drew last (the rig eye), as the engine has it.
  void native() {
    s.request[0] = 6; s.linked = true; s.mask = 0; s.aim = 0; s.camera_rotation_given = true;
    for (int i = 0; i < 3; ++i) { s.rig[i] = right[i]; s.rig[4 + i] = ahead[i]; s.rig[8 + i] = above[i]; s.rig[12 + i] = s.head[i] + ahead[i] * .1f + above[i] * .05f; }
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
    const float rows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    World::put(W.a[A_BODY_WORLD] + HEAD_NODE * 64, rows);
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
  // How far the forwarded eye is from the head camera's place for a view along `yaw`.
  float from_head(float yaw) const {
    const float want[3] = {s.head[0] - std::sin(yaw) * .18f, s.head[1] + std::cos(yaw) * .18f, s.head[2] + .04f};
    return apart(sent.position, want);
  }
  bool forwarded_untouched() const { // the game's own frame: its buffer, its position, its rotation
    Q given; std::memcpy(&given, s.argument_rotation, 16);
    return !own && apart(sent.position, s.argument) == 0 && !std::memcmp(&out, &given, 16);
  }
  // The diver climbs into the suit: the root rises 2.43 m, the head 0.84 m above it, layer 14 off the on-foot state.
  void climb_in(float x, float y) { seat(true); s.state = 25; stand(x, y, 2.43f + .34f, .84f); }
  void climb_out(float x, float y) { seat(false); s.state = 36; stand(x, y, .34f); }
};

static void fresh(bool lock, bool smooth = false) {
  tfp_camera_reset_for_tests();
  TfpCameraHost host{};
  host.hook = reinterpret_cast<uint64_t>(&hook_marker); host.read = host_read; host.readable_span = host_readable_span; host.now = host_now;
  host.writable_slot = host_true_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock; host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_camera_set_host(&host);
  tfp_camera_set_lock(lock, 0, 0, 300, 0, 0, 0, true);
  tfp_camera_set_continuity(smooth, false);
  clock_ms += 5000;
}

int main() {
  W.build(HERE);
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  const float first = .4f, suit = .7f; // the head camera's direction on foot; the exosuit camera's

  // ---- 1. Into the exosuit from the head camera, with continuity off and on. The diver stumbles off foot first (the
  // climb), then is seated. The first seated frame is where the view was; the view glides to the game's suit camera
  // (4.1 m behind and above the cockpit) in at most 600 ms with no frame's step much over the smoothstep's peak, while
  // the suit walks on; then every frame is the game's own, untouched. Head hiding lets go within 250 ms; the lock
  // counts the seat frames as handed back, not as held or missed, and the field-of-view lock stops following. Then out
  // again: the first frame is the game's last, and the head camera is back within 600 ms. ----
  for (int smooth = 0; smooth < 2; ++smooth) {
    fresh(true, smooth != 0);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 12; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    g.s.state = 50; // the climb: off foot, not yet seated (held at the head, as a thrown body)
    for (int i = 0; i < 4; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    CHECK(tfp_camera_lock_off_foot_body() == BODY);
    const float shown[3] = {sent.position[0], sent.position[1], sent.position[2]}; const Q shown_q = g.out;
    TfpCameraLockStats lock0{}; tfp_camera_lock_stats(&lock0);
    const uint64_t view0 = tfp_camera_view_frames();
    TfpCameraSeatStats seat0{}; tfp_camera_seat_stats(&seat0);
    CHECK(seat0.frames == 0 && seat0.entries == 0 && !seat0.seated);

    float x = -.1f, last[3] = {}, walk_last[3] = {}, way = 0; int glided = 0; float most = 0; bool forwarded = false, untouched_after = true;
    for (int i = 0; i < 40; ++i) {
      x += .08f; g.climb_in(x, -.2f); g.look(suit); g.hip(4.0f, 1.0f); // the suit walks on at 2.4 m/s
      g.frame();
      if (i == 0) {
        CHECK(g.own && apart(sent.position, shown) < 1e-5f && degrees(g.out, shown_q) < .01f); // the glide's start
        TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat);
        CHECK(seat.entries == 1 && seat.seated && seat.in_kind == 4 && seat.in_ms == 600 && seat.in_metres > 3.5f && seat.in_weapon == 0x444);
        way = seat.in_metres;
      } else if (g.own) {
        CHECK(!forwarded); ++glided;
        float step[3]; for (int k = 0; k < 3; ++k) step[k] = (sent.position[k] - last[k]) - (g.s.argument[k] - walk_last[k]);
        const float s = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]); if (s > most) most = s;
      } else {
        if (!forwarded) CHECK(i <= 19); // 600 ms at 33.3 ms a frame
        forwarded = true;
        if (!g.forwarded_untouched()) untouched_after = false;
      }
      std::memcpy(last, sent.position, 12); std::memcpy(walk_last, g.s.argument, 12);
      CHECK(g.error == 0);
    }
    CHECK(forwarded && untouched_after && glided >= 15);
    CHECK(most < 1.5f * way * 33.3f / 600 * 1.02f + .08f); // the smoothstep's peak step on the way, beside the walk (0.08 m a frame, not yet all of it taken on)
    CHECK(tfp_camera_lock_off_foot_body() == 0);      // 1.3 s later: head hiding is the game's again
    TfpCameraLockStats lock1{}; tfp_camera_lock_stats(&lock1);
    CHECK(lock1.frames == lock0.frames && lock1.missed == lock0.missed && lock1.foreign_frames == lock0.foreign_frames);
    CHECK(tfp_camera_view_frames() == view0);
    TfpCameraSeatStats seat1{}; tfp_camera_seat_stats(&seat1);
    CHECK(seat1.frames == 40 && seat1.glided == static_cast<uint64_t>(glided) + 1 && seat1.entries == 1 && seat1.exits == 0 && seat1.cuts == 0 && seat1.seated);
    std::printf("into the suit%s: %.2f m glided in %d frames, largest step beside the walk %.3f m\n", smooth ? " (continuity on)" : "", way, glided + 1, most);

    // Out of the suit, 2 m to its side: the first frame is the game's last suit frame, then the head camera within 600 ms.
    const float game_last[3] = {sent.position[0], sent.position[1], sent.position[2]}; const Q game_q = g.out;
    g.climb_out(x + 2, -.2f); g.look(first); g.hip();
    g.frame();
    CHECK(g.own && g.error == 0 && apart(sent.position, game_last) < 1e-5f && degrees(g.out, game_q) < .01f);
    TfpCameraSeatStats out0{}; tfp_camera_seat_stats(&out0);
    CHECK(out0.exits == 1 && !out0.seated && out0.out_ms >= 300 && out0.out_ms <= 600);
    float worst = 0;
    for (int i = 1; i < 30; ++i) {
      g.hip(); g.frame(); CHECK(g.own && g.error == 0);
      if (i == 18) CHECK(g.from_head(first) < .07f && degrees(g.out, yaw_q(first)) < .5f); // 600 ms
      if (i >= 18 && g.from_head(first) > worst) worst = g.from_head(first);
    }
    CHECK(worst < .07f && degrees(g.out, yaw_q(first)) < .5f);
    TfpCameraLockStats lock2{}; tfp_camera_lock_stats(&lock2);
    CHECK(lock2.frames == lock1.frames + 30 && lock2.missed == lock1.missed);
    CHECK(tfp_camera_view_frames() == view0 + 30);
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
    CHECK(g.own && apart(sent.position, drawn) < 1e-5f && degrees(g.out, yaw_q(first)) < .01f);
    int n = 1; while (g.own && n < 40) { g.hip(4.0f, 1.0f); g.frame(); ++n; }
    CHECK(n <= 20 && g.forwarded_untouched());
    g.end();
  }

  // ---- 3. A seat whose camera is not kind 4 (another vehicle's, a passenger's: an FRV was not seen yet), the same:
  // handed to the game's. And a game camera more than 8 m from the view: a cut, the game's frame from the first. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); }
    g.climb_in(.1f, .5f); g.look(suit); g.hip(3.0f, 1.2f, 9);
    int n = 0; do { g.hip(3.0f, 1.2f, 9); g.frame(); ++n; } while (g.own && n < 40);
    CHECK(n >= 10 && n <= 20 && g.forwarded_untouched());
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.entries == 1 && seat.in_kind == 9 && seat.cuts == 0);
    g.end();
  }
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); }
    g.climb_in(.1f, .5f); g.look(suit);
    for (int i = 0; i < 5; ++i) { g.hip(10.0f, 1.0f); g.frame(); CHECK(g.forwarded_untouched() && g.error == 0); }
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.entries == 1 && seat.in_ms == 0 && seat.cuts == 1 && seat.glided == 0);
    g.end();
  }

  // ---- 4. What the seat rule leaves as it was. A valve (seated, on foot): held at the head with the seated view.
  // A thrown body (off foot, not seated) and a body killed in its seat (seated, off foot, ragdoll flag): held. With the
  // lock off, a seat is retail's: the game's frame, refused for not being on foot (error 18). On foot nothing counts. ----
  {
    fresh(true);
    Game g; g.stand(0, 0, .34f); g.look(first); g.hip(); g.begin();
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); CHECK(g.own); }
    for (int i = 0; i < 8; ++i) { g.native(); g.frame(); }
    for (int i = 0; i < 8; ++i) { g.hip(); g.frame(); CHECK(g.own); }
    TfpCameraSeatStats foot{}; tfp_camera_seat_stats(&foot); CHECK(foot.frames == 0 && foot.entries == 0);
    g.seat(true); g.look(suit);
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0 && apart(sent.position, g.s.head) < .3f); }
    CHECK(degrees(g.out, yaw_q(suit)) > 1); // the seated view glides to the game's direction on 180 ms: not there in one frame
    g.seat(false); g.s.state = 50;
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    g.seat(true); g.ragdoll = true;
    for (int i = 0; i < 10; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.frames == 0 && seat.entries == 0);
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
    tfp_camera_set_lock(false, 0, 0, 300, 0, 0, 0, true);
    g.hip(4.0f, 1.0f); g.frame(); CHECK(g.forwarded_untouched());
    TfpCameraSeatStats seat{}; tfp_camera_seat_stats(&seat); CHECK(seat.exits == 1 && seat.out_ms == 0 && !seat.seated);
    g.climb_out(2, .5f); g.look(first);
    for (int i = 0; i < 5; ++i) { g.hip(); g.frame(); }
    tfp_camera_set_lock(true, 0, 0, 300, 0, 0, 0, true);
    for (int i = 0; i < 5; ++i) { g.hip(); g.frame(); CHECK(g.own && g.error == 0); }
    CHECK(g.from_head(first) < .07f);
    g.end();
  }

  std::printf("camera vehicle lock: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
