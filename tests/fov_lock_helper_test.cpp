// The field-of-view lock through the helper itself (lock_field in src/fov_helper.c, beyond retail), on the fake
// game memory of the differential test (this build's layout). Every frame is one call of the hook as the game's
// call site makes it; what is judged is the field of view handed to the game's setter. tests/fov_lock_test.c has
// the arithmetic (src/fov_lock.h); here: which camera is touched, the clock the glide runs on, the glide in and
// out through tfp_fov_set_lock, and what tfp_fov_lock_stats reads. No retail source needed. Linux (process_vm_readv).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>
#define _Static_assert static_assert
extern "C" {
#include "fov_helper.h"
#include "fov_lock.h"
}
#include "diff_fov_helper_world.h"
using namespace dt;

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; std::printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

static World W;
static double seconds_now = 100; static bool clock_ok = true; static uint64_t clock_reads;
static struct { float radians; void *camera; bool seen; } sent;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static bool host_readable_span(void *, uint64_t, size_t) { return true; }
static bool host_seconds(void *, double *out) { ++clock_reads; if (clock_ok) *out = seconds_now; return clock_ok; }
static bool host_true_slot(void *, uint64_t, uint64_t) { return true; }
static bool host_true_field(void *, uint64_t) { return true; }
static bool host_executable(void *, uint64_t) { return true; }
static bool host_cas(void *, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint64_t *>(slot), expected, desired); return true;
}
static bool host_cas32(void *, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint32_t *>(at), expected, desired); return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) { std::memcpy(out, data, length); return true; }
static bool host_pin(void *, uint64_t) { return true; }
static void host_lock(void *, int) {}
static void host_forward(void *, uint64_t, void *camera, float radians) { sent.seen = true; sent.camera = camera; sent.radians = radians; }

// (the model of the report builds this file with the user's numbers: -DTFP_TEST_NATIVE=61.5f -DTFP_TEST_LOCKED=80.0f)
#ifndef TFP_TEST_NATIVE
#define TFP_TEST_NATIVE 70.0f
#endif
#ifndef TFP_TEST_LOCKED
#define TFP_TEST_LOCKED 110.0f
#endif
static const float RAD = 0.01745329251994329577f, NATIVE = TFP_TEST_NATIVE * RAD, LOCKED = TFP_TEST_LOCKED * RAD;
static uint64_t gen;
static void aim(bool held) { World::put<uint8_t>(W.a[A_CONTROLLER] + 4, held ? 1 : 0); }
// The weapon camera at the hip (kind 6, the newest request), and the helper armed on it.
static void fresh() {
  tfp_fov_reset_for_tests();
  TfpFovHost host{};
  host.hook = reinterpret_cast<uint64_t>(&hook_marker); host.read = host_read; host.readable_span = host_readable_span; host.seconds = host_seconds;
  host.writable_slot = host_true_slot; host.writable_request_field = host_true_field; host.executable = host_executable;
  host.cas = host_cas; host.cas32 = host_cas32; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock; host.forward = host_forward;
  tfp_fov_set_host(&host);
  World::put<uint64_t>(W.slot(), W.native_value);
  World::put<uint32_t>(W.a[A_CONTROLLER], 0); aim(false); World::put<uint8_t>(W.a[A_CONTROLLER] + 5, 0);
  World::put<uint32_t>(W.a[A_CONTEXT] + 0x1F8, 0); World::put<uint32_t>(W.a[A_CONTEXT] + 0x1FC, 2);
  const uint32_t rows[2][4] = {{4, AVATAR, WEAPON, BODY}, {6, AVATAR, WEAPON, BODY}};
  for (int i = 0; i < 2; ++i) { World::put(W.a[A_CONTEXT] + row_offset(i), rows[i]); World::put<float>(W.a[A_CONTEXT] + row_offset(i) + 0x3C, 90 * RAD); }
  TfpFovConfig c{};
  c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA]; c.avatar_owner = W.a[A_OWNER];
  c.request = W.a[A_CONTEXT] + row_offset(1); c.avatar_index = 0; c.avatar = AVATAR; c.body = BODY; c.weapon = WEAPON;
  gen = 0;
  if (tfp_fov_begin(&c, &gen) != 1) { std::printf("the helper did not arm (error %u)\n", tfp_fov_error()); std::exit(2); }
  seconds_now += 50; clock_ok = true;
}
// One call of the hook `ms` later; the armed camera, or another one of the same call site. Returns what was forwarded.
static float frame(double ms, bool other_camera = false) {
  seconds_now += ms / 1000; sent.seen = false;
  void *camera = reinterpret_cast<void *>(W.a[A_CAMERA] + (other_camera ? 0x800 : 0));
  tfp_fov_setter_from(W.a[A_GAME] + HERE.call + 6, camera, NATIVE);
  if (!sent.seen || sent.camera != camera) { std::printf("the hook did not forward\n"); std::exit(2); }
  return sent.radians;
}
static TfpFovLockStats stats() { TfpFovLockStats s{}; tfp_fov_lock_stats(&s); return s; }

// ---- Two calls a frame, as the game makes them in the first-person view (build 25480438). The queue's dispatcher
// (game+0x1279190) sets the composed lens through the hooked call site (return game+0x127a120); then, in the same
// frame and on the same thread, the post-camera rig update (game+0x127a420) sets the field of view of the same camera
// again at game+0x127aa90 (return game+0x127aa92), just before it sets the camera's position (game+0x127aaae, the
// camera helper's call site). The second call is another caller to this helper: forwarded, through the lock only.
// It is the later write of the frame, so it is the one on screen when the last write wins; both are judged. ----
static const uint64_t SECOND_CALL = 0x127aa92 - 0x127a120; // from the hooked call's return address
static float second(double ms, float native) {
  seconds_now += ms / 1000; sent.seen = false;
  void *camera = reinterpret_cast<void *>(W.a[A_CAMERA]);
  tfp_fov_setter_from(W.a[A_GAME] + HERE.call + 6 + SECOND_CALL, camera, native);
  if (!sent.seen || sent.camera != camera) { std::printf("the second call was not forwarded\n"); std::exit(2); }
  return sent.radians;
}
// One frame of `ms` (jittered by the caller) whose two calls are `gap` ms apart. `native` is the game's lens at both
// sites. Returns the second (later) value; *first gets the hooked call's.
struct Frames { double gap = 1.3; };
static Frames F;
static float frame2(double ms, double gap, float *first = nullptr, float native = NATIVE) {
  const double before = F.gap; F.gap = gap;
  seconds_now += (ms - before) / 1000; sent.seen = false;
  void *camera = reinterpret_cast<void *>(W.a[A_CAMERA]);
  tfp_fov_setter_from(W.a[A_GAME] + HERE.call + 6, camera, native);
  if (!sent.seen) { std::printf("the hook did not forward\n"); std::exit(2); }
  if (first) *first = sent.radians;
  return second(gap, native);
}
static uint32_t jitter_seed = 1;
static double jitter() { jitter_seed = jitter_seed * 1664525u + 1013904223u; return (jitter_seed >> 8) / 16777216.0; } // [0, 1)

int main() {
  W.build(HERE);
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  const double dt = 1000.0 / 144;

  // ---- 1. Without the lock nothing here changes anything, and the clock is not read for it. `base` is what the
  // helper shows at the hip without the lock (the game's setting, 90 degrees). ----
  float base = 0;
  {
    fresh();
    base = frame(dt);
    CHECK(base > .05f && std::fabs(base - LOCKED) > .2f);
    const uint64_t reads = clock_reads;
    for (int i = 0; i < 20; ++i) CHECK(frame(dt) == base);
    CHECK(clock_reads == reads); // (the helper's own policy reads no clock at the hip either)
    tfp_fov_set_lock(0, false, 5000);
    for (int i = 0; i < 20; ++i) CHECK(frame(dt) == base);
    CHECK(clock_reads == reads);
    CHECK(frame(dt, true) == NATIVE && frame(dt) == base); // another camera: the game's value
    CHECK(stats().applied == 0 && stats().shown_radians == 0 && stats().game_radians == NATIVE);
    tfp_fov_end(gen);
  }
  // ---- 2. The glide in runs on the frame's own clock: the binder gives its time once a tick (here it stands still
  // for the whole glide), and the field of view still moves in every rendered frame, by the 80 ms time constant. ----
  {
    fresh();
    for (int i = 0; i < 5; ++i) frame(dt);
    tfp_fov_set_lock(LOCKED, false, 5000);
    float last = frame(dt); // the first frame of the lock: from what was on screen
    CHECK(last == base && stats().shown_radians == base && stats().applied == 1);
    bool every = true, rate = true;
    for (int i = 0; i < 40; ++i) {
      const float now = frame(dt);
      if (!(now > last && now < LOCKED)) every = false;
      if (std::fabs((now - last) - (LOCKED - last) * (1 - std::exp(-static_cast<float>(dt) / TFP_FOV_LOCK_GLIDE_MS))) > 1e-4f) rate = false;
      last = now;
    }
    CHECK(every); CHECK(rate);
    for (int i = 0; i < 80; ++i) last = frame(dt);
    CHECK(std::fabs(last - LOCKED) < 1e-3f && stats().shown_radians == last && stats().applied == 121 && stats().game_radians == NATIVE);
    // ---- 3. A camera other than the armed one is passed through, and the state starts again: the armed camera's
    // next frame begins from what the game shows, not from the locked value. ----
    CHECK(frame(dt, true) == NATIVE);
    CHECK(stats().shown_radians == 0 && stats().applied == 121);
    // the game's field of view the "Aim:" line reports is the armed camera's, not another camera's (review N3)
    sent.seen = false;
    tfp_fov_setter_from(W.a[A_GAME] + HERE.call + 6, reinterpret_cast<void *>(W.a[A_CAMERA] + 0x800), .3f);
    CHECK(sent.seen && sent.radians == .3f && stats().game_radians == NATIVE);
    CHECK(frame(dt) == base && stats().shown_radians == base);
    float next = frame(dt);
    CHECK(next > base && next < base + (LOCKED - base) * .2f);
    for (int i = 0; i < 120; ++i) last = frame(dt);
    CHECK(std::fabs(last - LOCKED) < 1e-3f);
    // ---- 4. The glide out: the lock switched off, the value goes back to the game's in every frame, is the game's
    // within half a second, and then the statistics read 0 shown and nothing more is counted. ----
    tfp_fov_set_lock(0, false, 5000);
    bool back = true; int ended = -1;
    for (int i = 0; i < 72; ++i) {
      const float now = frame(dt);
      if (ended < 0) {
        if (now == base) { ended = i; CHECK(stats().shown_radians == 0); } // in the frame it ends, not one later
        else if (!(now < last && now > base && stats().shown_radians == now)) back = false;
      }
      else if (now != base) back = false;
      last = now;
    }
    CHECK(back && ended > 20 && ended < 72); // 0.35 rad to 0.003 rad on 80 ms: 0.38 s
    const uint64_t counted = stats().applied;
    CHECK(stats().shown_radians == 0);
    for (int i = 0; i < 10; ++i) CHECK(frame(dt) == base);
    CHECK(stats().applied == counted && stats().shown_radians == 0);
    tfp_fov_end(gen);
  }
  // ---- 5. Full aim: with 'zoom when aiming' the lock gives way to the lens the helper shows for the aim, without
  // it the locked value stays. Aiming is the helper's own reading of the controller. ----
  for (int zoom = 0; zoom < 2; ++zoom) {
    fresh();
    tfp_fov_set_lock(LOCKED, zoom != 0, 5000);
    float last = 0;
    for (int i = 0; i < 120; ++i) last = frame(dt);
    CHECK(std::fabs(last - LOCKED) < 1e-3f);
    aim(true);
    for (int i = 0; i < 120; ++i) last = frame(dt);
    if (zoom) CHECK(std::fabs(last - NATIVE) < 1e-3f && stats().shown_radians == last); // the game's own lens in full aim
    else CHECK(std::fabs(last - LOCKED) < 1e-3f);
    aim(false);
    tfp_fov_end(gen);
  }
  // ---- 6. A host clock that cannot be read: the glide falls back on the time the binder gives each tick. ----
  {
    fresh();
    for (int i = 0; i < 5; ++i) frame(dt);
    clock_ok = false;
    const float still = frame(dt); // (what the helper's own policy shows without its clock)
    tfp_fov_set_lock(LOCKED, false, 5000);
    float last = frame(dt);
    CHECK(last == still);
    for (int i = 1; i <= 60; ++i) { tfp_fov_set_lock(LOCKED, false, 5000 + i * 16.0); const float now = frame(dt); CHECK(now > last && now < LOCKED); last = now; }
    CHECK(std::fabs(last - LOCKED) < 1e-3f);
    clock_ok = true;
    tfp_fov_end(gen);
  }
  // ---- 7. Two calls a frame in full aim with 'zoom when aiming' (the user's report on feel.50: the view pulses in
  // ADS; the log read "the game asked for 61.5 degrees, shown 64.3 / 64.4 / 64.5" second to second, 232 frames
  // counted for 116). Both calls of a frame must hand the setter one value, and in steady aim that value must be the
  // game's lens, whatever the frame times and however a frame's time splits between the two calls. ----
  for (const double fps : {60.0, 116.0, 144.0}) {
    fresh(); F.gap = 1.3; jitter_seed = 7;
    const double period = 1000.0 / fps;
    auto ms = [&] { return period * (0.85 + 0.3 * jitter()); };   // frame times +-15 %
    auto gap = [&] { return 0.6 + 1.4 * jitter(); };              // 0.6 to 2.0 ms between the two calls
    tfp_fov_set_lock(LOCKED, true, 5000);
    float a = 0, b = 0;
    for (int i = 0; i < fps; ++i) b = frame2(ms(), gap(), &a);
    CHECK(std::fabs(b - LOCKED) < 1e-3f && std::fabs(a - LOCKED) < 1e-3f);
    const uint64_t counted = stats().applied;
    for (int i = 0; i < 10; ++i) frame2(ms(), gap());
    CHECK(stats().applied - counted == 10); // one locked frame counted per frame, not per call
    aim(true);
    for (int i = 0; i < fps; ++i) b = frame2(ms(), gap(), &a);
    float lo = 10, hi = 0, step = 0, last = b; bool same = true;
    for (int i = 0; i < 2 * fps; ++i) {
      b = frame2(ms(), gap(), &a);
      lo = std::fmin(lo, b); hi = std::fmax(hi, b); step = std::fmax(step, std::fabs(b - last)); last = b;
      if (i < 12 && std::getenv("TFP_FOV_MODEL_FRAMES")) std::printf("    frame %2d: calls %.3f / %.3f degrees\n", i, a / RAD, b / RAD);
      if (a != b) same = false;
    }
    std::printf("  %3.0f fps, full aim, two calls a frame: shown %.3f to %.3f degrees (the game's %.3f), largest change between frames %.4f degrees\n",
                fps, lo / RAD, hi / RAD, NATIVE / RAD, step / RAD);
    CHECK(same);                                     // both calls of a frame: one value
    CHECK(hi - lo < 1e-4f && std::fabs(hi - NATIVE) < 1e-3f);
    CHECK(stats().shown_radians == b);
    aim(false);
    tfp_fov_end(gen);
  }
  // ---- 8. The aim zoom itself still glides, on the 80 ms time constant, with two calls a frame: aim in and out,
  // quick taps, the game's lens changing under the aim (a swap), zoom when aiming off, the lock off while aiming,
  // the lock off at the hip. No frame is a pop: no change larger than one frame's glide of the whole distance. ----
  {
    fresh(); F.gap = 1.3; jitter_seed = 11;
    const double period = 1000.0 / 116;
    auto ms = [&] { return period * (0.85 + 0.3 * jitter()); };
    auto gap = [&] { return 0.6 + 1.4 * jitter(); };
    const float most = (LOCKED - NATIVE) * (1 - std::exp(-static_cast<float>(period * 1.15 + 2.0) / TFP_FOV_LOCK_GLIDE_MS)) + 1e-5f;
    tfp_fov_set_lock(LOCKED, true, 5000);
    float a = 0, b = 0, last = 0;
    for (int i = 0; i < 116; ++i) last = frame2(ms(), gap());
    // aim in: down to the game's lens, every frame a little, settled within 0.7 s (the 80 ms glide behind the helper's own 180 ms hand-off lens)
    aim(true);
    bool down = true, small = true, same = true; double t = 0;
    while (t < 700) { const double d = ms(); t += d; b = frame2(d, gap(), &a); if (b > last + 1e-6f) down = false; if (last - b > most) small = false; if (a != b) same = false; last = b; }
    CHECK(down); CHECK(small); CHECK(same); CHECK(std::fabs(b - NATIVE) < (.05f * RAD));
    // aim out: back up to the lock the same way
    aim(false);
    bool up = true; small = true; same = true; t = 0;
    while (t < 700) { const double d = ms(); t += d; b = frame2(d, gap(), &a); if (b < last - 1e-6f) up = false; if (b - last > most) small = false; if (a != b) same = false; last = b; }
    CHECK(up); CHECK(small); CHECK(same); CHECK(std::fabs(b - LOCKED) < (.05f * RAD));
    // quick taps: three frames in, three out, twenty times
    small = true; same = true;
    for (int tap = 0; tap < 40; ++tap) {
      aim(tap % 2 == 0);
      for (int i = 0; i < 3; ++i) { b = frame2(ms(), gap(), &a); if (std::fabs(b - last) > most) small = false; if (a != b) same = false; last = b; }
    }
    CHECK(small && same && b > NATIVE && b < LOCKED);
    // a swap while aiming: the game's lens moves from 70 to 50 degrees at both sites; the screen follows, gliding
    aim(true);
    for (int i = 0; i < 116; ++i) last = frame2(ms(), gap());
    const float swapped = 50 * RAD;
    small = true; same = true; float lo = 10, hi = 0;
    for (int i = 0; i < 232; ++i) {
      b = frame2(ms(), gap(), &a, swapped);
      if (std::fabs(b - last) > (NATIVE - swapped) * (1 - std::exp(-static_cast<float>(period * 1.15 + 2.0) / TFP_FOV_LOCK_GLIDE_MS)) + 1e-5f) small = false;
      if (a != b) same = false;
      if (i >= 116) { lo = std::fmin(lo, b); hi = std::fmax(hi, b); }
      last = b;
    }
    CHECK(small && same && hi - lo < 1e-4f && std::fabs(b - swapped) < 1e-3f);
    // zoom when aiming off, still aiming: back to the lock, and steady there
    tfp_fov_set_lock(LOCKED, false, 5000);
    for (int i = 0; i < 116; ++i) b = frame2(ms(), gap(), &a);
    lo = 10; hi = 0;
    for (int i = 0; i < 232; ++i) { b = frame2(ms(), gap(), &a); lo = std::fmin(lo, b); hi = std::fmax(hi, b); }
    CHECK(hi - lo < 1e-4f && std::fabs(b - LOCKED) < 1e-3f);
    tfp_fov_set_lock(LOCKED, true, 5000);
    for (int i = 0; i < 116; ++i) last = frame2(ms(), gap(), &a, swapped);
    // the lock off while aiming: nothing to glide (the screen already shows the game's lens), and the lock lets go
    tfp_fov_set_lock(0, true, 5000);
    small = true; int ended = -1;
    for (int i = 0; i < 116; ++i) {
      b = frame2(ms(), gap(), &a, swapped);
      if (std::fabs(b - last) > (.05f * RAD)) small = false;
      if (ended < 0 && stats().shown_radians == 0) ended = i;
      last = b;
    }
    CHECK(small && ended >= 0 && ended < 58 && b == swapped);
    aim(false);
    tfp_fov_end(gen);
  }
  // ---- 9. The lock off at the hip, two calls a frame. The helper's hip lens (the hooked call's value) and the game's
  // own lens (the second call's) differ; with the lock off the second call's lens is what the screen keeps when the
  // later write wins, so the glide out ends there, and the lock lets go: nothing is changed any more. ----
  {
    fresh(); F.gap = 1.3; jitter_seed = 13;
    const double period = 1000.0 / 116;
    auto ms = [&] { return period * (0.85 + 0.3 * jitter()); };
    auto gap = [&] { return 0.6 + 1.4 * jitter(); };
    tfp_fov_set_lock(LOCKED, true, 5000);
    float a = 0, b = 0, last = 0;
    for (int i = 0; i < 116; ++i) last = frame2(ms(), gap());
    CHECK(std::fabs(last - LOCKED) < 1e-3f);
    tfp_fov_set_lock(0, true, 5000);
    bool down = true, same = true; int ended = -1;
    for (int i = 0; i < 116; ++i) {
      b = frame2(ms(), gap(), &a);
      if (ended < 0) {
        if (stats().shown_radians == 0) ended = i;
        else { if (b > last + 1e-6f) down = false; if (a != b) same = false; }
      }
      last = b;
    }
    CHECK(down && same && ended > 5 && ended < 58);
    CHECK(b == NATIVE && a == base);              // let go: each call hands over the game's own value again
    const uint64_t counted = stats().applied;
    for (int i = 0; i < 10; ++i) frame2(ms(), gap());
    CHECK(stats().applied == counted && stats().shown_radians == 0);
    tfp_fov_end(gen);
  }
  // ---- 10. Round 20, "Aim field of view": a chosen value in full aim, through the real helper and two calls a frame.
  // With the lock on: the lock at the hip, the chosen value in full aim, the lock again after. With the lock off: the
  // game's own values at the hip, untouched, the chosen value in full aim, and let go afterwards. The two old answers
  // (the game's zoom, your locked view) are part 5's, through the new call. ----
  {
    const float CHOSEN = 40 * RAD;
    for (int locked = 1; locked >= 0; --locked) {
      fresh(); F.gap = 1.3; jitter_seed = 21;
      const double period = 1000.0 / 116;
      auto ms = [&] { return period * (0.85 + 0.3 * jitter()); };
      auto gap = [&] { return 0.6 + 1.4 * jitter(); };
      tfp_fov_set_lock_aim(locked ? LOCKED : 0, CHOSEN, 5000);
      float a = 0, b = 0;
      for (int i = 0; i < 116; ++i) b = frame2(ms(), gap(), &a);
      if (locked) CHECK(std::fabs(b - LOCKED) < 1e-3f && a == b);
      else CHECK(b == NATIVE && a == base && stats().shown_radians == 0 && stats().applied == 0);
      aim(true);
      bool same = true;
      for (int i = 0; i < 116; ++i) { b = frame2(ms(), gap(), &a); if (a != b) same = false; }
      CHECK(same && std::fabs(b - CHOSEN) < 1e-3f && stats().shown_radians == b); // one value a frame, the chosen one
      aim(false);
      for (int i = 0; i < 116; ++i) b = frame2(ms(), gap(), &a);
      if (locked) CHECK(std::fabs(b - LOCKED) < 1e-3f);
      else CHECK(b == NATIVE && a == base && stats().shown_radians == 0);          // let go again
      tfp_fov_end(gen);
    }
    for (int zoom = 0; zoom < 2; ++zoom) {
      fresh();
      tfp_fov_set_lock_aim(LOCKED, zoom ? TFP_FOV_AIM_ZOOM : TFP_FOV_AIM_LOCKED, 5000);
      float last = 0;
      aim(true);
      for (int i = 0; i < 120; ++i) last = frame(dt);
      CHECK(std::fabs(last - (zoom ? NATIVE : LOCKED)) < 1e-3f);
      aim(false);
      tfp_fov_end(gen);
    }
  }
  // ---- 10. Round 20: full aim with 'zoom when aiming', an optic zoomed in and the unzoom gate flipping (the tester's
  // report on feel.50: the screen shakes while aiming, more at a 50 or 100 m scope range). The game's side as the
  // listing has it (game+0x1286a46..0x1286d09): the lens target is the third-person field of view over the zoom level's
  // magnification, the wide one while the gate is set; the lens goes toward it at clamp(10 |way|, 0.1, 2) rad/s, times
  // the magnification while the gate is clear. The pose helper notes each evaluation before the frame's hooked call.
  // Both calls of a frame must hand the setter one value, and it must hold the optic's own zoom.
  // Round 20c: the hold is the test switch zoom_steady.txt (tfp_fov_set_aim_watch); part 12 has it off. ----
  for (const double fps : {60.0, 116.0, 144.0}) for (const float mag : {1.5f, 6.0f, 12.0f}) {
    fresh(); F.gap = 1.3; jitter_seed = 21; tfp_fov_set_aim_watch(true, true);
    const double period = 1000.0 / fps;
    auto ms = [&] { return period * (0.85 + 0.3 * jitter()); };
    auto gap = [&] { return 0.6 + 1.4 * jitter(); };
    const float wide = 90 * RAD, optic = wide / mag, k = std::tan(61.5f * RAD * .5f) / std::tan(60 * RAD * .5f);
    float lens = optic;
    auto evaluate = [&](bool gated, double dt) {
      const float target = gated ? wide : optic, way = target - lens;
      float rate = 10 * std::fabs(way); rate = rate < .1f ? .1f : rate > 2 ? 2 : rate; if (!gated) rate *= mag;
      const float step = (way < 0 ? -1.0f : 1.0f) * rate * static_cast<float>(dt * .001);
      lens = std::fabs(step) >= std::fabs(way) ? target : lens + step;
      tfp_fov_note_lens(gated, target, lens);
      return 2 * std::atan(std::tan(lens * .5f) * k);
    };
    const float rest = 2 * std::atan(std::tan(optic * .5f) * k);
    tfp_fov_set_lock(LOCKED, true, 5000);
    for (int i = 0; i < fps; ++i) frame2(ms(), gap());
    aim(true);
    TfpFovAimSecond took{}; tfp_fov_aim_take(&took);
    float a = 0, b = 0, last = 0, most = 0, least = 10, step = 0; bool same = true;
    for (int i = 0; i < 3 * fps; ++i) {
      const double d = ms();
      const float game = evaluate(i & 1, d);
      b = frame2(d, gap(), &a, game);
      if (a != b) same = false;
      if (i == static_cast<int>(fps)) tfp_fov_aim_take(&took); // the line's second from here: the aim in is over
      if (i > fps) {
        if (b > most) most = b;
        if (b < least) least = b;
        const float change = std::fabs(std::tan(last * .5f) / std::tan(b * .5f) - 1);
        if (change > step) step = change;
      }
      last = b;
    }
    std::printf("    %3.0f fps x%-4.1f gate flipping in full aim: shown %.3f..%.3f degrees (the optic at rest %.3f), largest change of magnification %.4f %%\n",
                fps, mag, least / RAD, most / RAD, rest / RAD, step * 100);
    CHECK(same && step < .005f && std::fabs(most - rest) < .01f * RAD && std::fabs(least - rest) < .01f * RAD);
    tfp_fov_aim_take(&took);
    CHECK(took.frames + 2 > 2 * fps && took.noted == took.frames && took.gated > took.frames / 3 && took.changes > took.frames / 2 && took.corrected > took.frames / 2);
    CHECK(std::fabs(took.clear_least - optic) < 1e-6f && std::fabs(took.clear_most - optic) < 1e-6f && took.lens_most > optic && took.game_most > rest);
    CHECK(took.shown_least > 0 && took.shown_most < rest + .2f * RAD && took.step_most < .005f);
    // The gate set and kept (a wall right in front): after TFP_FOV_LOCK_STALE_MS the game's own unzoom shows, gliding.
    float widest = 0, biggest = 0; last = b;
    for (int i = 0; i < fps; ++i) {
      const double d = ms();
      b = frame2(d, gap(), &a, evaluate(true, d));
      if (b > widest) widest = b;
      if (std::fabs(b - last) > biggest) biggest = std::fabs(b - last);
      last = b;
    }
    CHECK(widest > rest + 10 * RAD && biggest < (wide - optic) * .3f);
    // Clear again and steady: back on the optic, then the game's own value to the bit (no gate for 250 ms).
    for (int i = 0; i < fps; ++i) { const double d = ms(); b = frame2(d, gap(), &a, evaluate(false, d)); }
    CHECK(std::fabs(b - rest) < 1e-4f);
    // No note in a frame: the game's value, as before.
    const float plain = frame2(ms(), gap(), &a, rest + .1f * RAD);
    CHECK(std::fabs(plain - (rest + .1f * RAD)) < .1f * RAD);
    aim(false);
    tfp_fov_end(gen);
  }
  // ---- 11. Round 20, review 24: the hold is full aim's with 'zoom when aiming' only; it begins eased whatever the
  // gate did before; a third call of a frame keeps to the frame's goal; the "Aim:" line's factor. ----
  {
    const double fps = 60; const float mag = 12;
    const float wide = 90 * RAD, optic = wide / mag, k = std::tan(61.5f * RAD * .5f) / std::tan(60 * RAD * .5f);
    const float rest = 2 * std::atan(std::tan(optic * .5f) * k);
    float lens = optic;
    auto evaluate = [&](bool gated, double dt) {
      const float target = gated ? wide : optic, way = target - lens;
      float rate = 10 * std::fabs(way); rate = rate < .1f ? .1f : rate > 2 ? 2 : rate; if (!gated) rate *= mag;
      const float step = (way < 0 ? -1.0f : 1.0f) * rate * static_cast<float>(dt * .001);
      lens = std::fabs(step) >= std::fabs(way) ? target : lens + step;
      tfp_fov_note_lens(gated, target, lens);
      return 2 * std::atan(std::tan(lens * .5f) * k);
    };
    for (int where = 0; where < 2; ++where) { // 0: flipping at the hip, then aim; 1: flipping in aim with the option off, then on
      fresh(); F.gap = 1.3; lens = optic; tfp_fov_set_aim_watch(true, true); // round 20c: the switch on (zoom_steady.txt)
      tfp_fov_set_lock(LOCKED, where == 0, 5000);
      if (where == 1) aim(true);
      TfpFovAimSecond took{};
      for (int i = 0; i < fps; ++i) { const double d = 1000 / fps; frame2(d, 1.3, nullptr, evaluate(i & 1, d)); }
      tfp_fov_aim_take(&took);
      // nothing held outside full aim with the option; round 20c: full aim is counted whatever the option (the line's)
      CHECK(took.frames == (where == 1 ? static_cast<uint32_t>(fps) : 0u) && took.corrected == 0);
      if (where == 0) aim(true); else tfp_fov_set_lock(LOCKED, true, 6000);
      { const double d = 1000 / fps; frame2(d, 1.3, nullptr, evaluate(false, d)); }
      tfp_fov_aim_take(&took);
      CHECK(took.frames == 1 && took.corrected == 0); // the hold begins at nothing: it was not kept where it was not asked for
      for (int i = 0; i < fps; ++i) { const double d = 1000 / fps; frame2(d, 1.3, nullptr, evaluate(i & 1, d)); }
      tfp_fov_aim_take(&took);
      CHECK(took.corrected > took.frames / 2);
      // A third call in a frame (no hooked call before it): it steps toward the hooked call's goal, the optic's own.
      float a = 0, b = 0;
      for (int i = 0; i < 10; ++i) { const double d = 1000 / fps; b = frame2(d, 1.3, &a, evaluate(i & 1, d)); }
      const float third = second(1.3, wide);
      CHECK(std::fabs(b - rest) < .01f * RAD && std::fabs(third - rest) < .05f * RAD);
      // The line's factor: the game's value over the lens, in the tangent, constant here (the value is this frame's).
      tfp_fov_aim_take(&took);
      CHECK(took.factor_least > 0 && std::fabs(took.factor_least - k) < 1e-4f && std::fabs(took.factor_most - k) < 1e-4f);
      aim(false);
      tfp_fov_end(gen);
    }
  }
  // ---- 12. Round 20c: zoom_steady.txt absent (the start-up value). Full aim with the game's zoom shows the game's value
  // through the lock's glide as feel.51 did (nothing held while the gate flips: the pulse is there), and the "Aim:"
  // line still counts the gate, the optic's own target, the lens and the factor. With the lock off it counts too. ----
  for (int locked = 1; locked >= 0; --locked) {
    const double fps = 60; const float mag = 12;
    const float wide = 90 * RAD, optic = wide / mag, k = std::tan(61.5f * RAD * .5f) / std::tan(60 * RAD * .5f);
    float lens = optic;
    auto evaluate = [&](bool gated, double dt) {
      const float target = gated ? wide : optic, way = target - lens;
      float rate = 10 * std::fabs(way); rate = rate < .1f ? .1f : rate > 2 ? 2 : rate; if (!gated) rate *= mag;
      const float step = (way < 0 ? -1.0f : 1.0f) * rate * static_cast<float>(dt * .001);
      lens = std::fabs(step) >= std::fabs(way) ? target : lens + step;
      tfp_fov_note_lens(gated, target, lens);
      return 2 * std::atan(std::tan(lens * .5f) * k);
    };
    fresh(); F.gap = 1.3; tfp_fov_set_aim_watch(true, false);
    tfp_fov_set_lock(locked ? LOCKED : 0, true, 5000);
    aim(true);
    TfpFovAimSecond took{};
    for (int i = 0; i < fps; ++i) { const double d = 1000 / fps; frame2(d, 1.3, nullptr, evaluate(false, d)); }
    tfp_fov_aim_take(&took);
    float a = 0, b = 0, last = 0, step = 0, game = 0, game_last = 0, game_step = 0;
    for (int i = 0; i < 2 * fps; ++i) {
      const double d = 1000 / fps;
      game = evaluate(i & 1, d);
      b = frame2(d, 1.3, &a, game);
      if (i > 0) {
        const float change = std::fabs(std::tan(last * .5f) / std::tan(b * .5f) - 1), game_change = std::fabs(std::tan(game_last * .5f) / std::tan(game * .5f) - 1);
        if (change > step) step = change;
        if (game_change > game_step) game_step = game_change;
      }
      last = b; game_last = game;
    }
    tfp_fov_aim_take(&took);
    std::printf("    zoom_steady.txt absent, lock %s: x12 gate flipping in full aim, largest change of magnification %.3f %% (the game's own %.3f %%)\n",
                locked ? "on" : "off", step * 100, game_step * 100);
    if (locked) CHECK(step > .005f && step <= game_step + 1e-6f); // the lock's glide of the game's value: the pulse shows, softened
    else CHECK(b == game);                                        // the lock off: the game's own value, untouched
    CHECK(took.frames == static_cast<uint32_t>(2 * fps) && took.corrected == 0 && took.noted == took.frames && took.gated == took.frames / 2);
    CHECK(took.changes + 1 >= took.frames && std::fabs(took.clear_least - optic) < 1e-6f && took.factored > 0 && std::fabs(took.factor_least - k) < 1e-3f);
    aim(false);
    tfp_fov_end(gen);
  }
  // ---- Round 53 (tfp_fov_frame_pair): the two fields of view of the frame, as the game's setter was called on the
  // armed camera: what the game asked for and what the setter was handed, from the later call of the frame. ----
  {
    fresh();
    float game = -1, shown = -1; uint64_t calls = 9;
    CHECK(!tfp_fov_frame_pair(&game, &shown, &calls) && game == 0 && shown == 0 && calls == 0);   // no call yet
    CHECK(!tfp_fov_frame_pair(nullptr, nullptr, nullptr));
    float handed = frame(dt, true);                                                               // another camera: not the pair's
    CHECK(!tfp_fov_frame_pair(&game, &shown, &calls) && calls == 0);
    handed = frame(dt);                                                                           // the hip, no lock
    CHECK(tfp_fov_frame_pair(&game, &shown, &calls) && game == NATIVE && shown == handed && calls == 1);
    frame(dt, true);
    CHECK(tfp_fov_frame_pair(&game, &shown, &calls) && calls == 1 && shown == handed);            // the other camera changes nothing
    tfp_fov_set_lock(LOCKED, false, 5000);                                                        // the lock, 'your locked view' in aim
    float a = 0, b = 0;
    for (int i = 0; i < 144; ++i) b = frame2(dt, 1.3, &a);
    CHECK(tfp_fov_frame_pair(&game, &shown, &calls) && game == NATIVE && shown == b && std::fabs(shown - LOCKED) < 1e-3f && calls == 1 + 1 + 144);  // the first frame's two calls, then the later call of each
    { // between a frame's two calls the pair is the frame before's later call, not the hooked call's
      uint64_t before = 0, between = 0; float g0 = 0, s0 = 0, g1 = 0, s1 = 0;
      tfp_fov_frame_pair(&g0, &s0, &before);
      sent.seen = false; seconds_now += dt / 1000;
      tfp_fov_setter_from(W.a[A_GAME] + HERE.call + 6, reinterpret_cast<void *>(W.a[A_CAMERA]), 40 * RAD);
      CHECK(tfp_fov_frame_pair(&g1, &s1, &between) && between == before && g1 == g0 && s1 == s0);
      b = second(1.3, 40 * RAD);
      CHECK(tfp_fov_frame_pair(&g1, &s1, &between) && between == before + 1 && g1 == 40 * RAD && s1 == b);
      for (int i = 0; i < 3; ++i) frame(dt);                                                      // forwarded calls stop: the hooked call's, a frame later
      CHECK(tfp_fov_frame_pair(&g1, &s1, &between) && between == before + 3 && g1 == NATIVE && s1 == sent.radians);
      for (int i = 0; i < 144; ++i) b = frame2(dt, 1.3, &a);
    }
    aim(true);
    const float lens = 30 * RAD;                                                                  // the game's lens in full aim
    for (int i = 0; i < 144; ++i) {
      b = frame2(dt, 1.3, &a, lens);
      CHECK(tfp_fov_frame_pair(&game, &shown, nullptr) && game == lens && shown == b);            // every frame: this frame's
    }
    CHECK(std::fabs(shown - LOCKED) < 1e-3f);                                                     // full aim shown at the locked value
    tfp_fov_set_lock(LOCKED, true, 5000);                                                         // 'the game's zoom': glides to the lens
    float last = shown; bool down = true;
    for (int i = 0; i < 144; ++i) {
      b = frame2(dt, 1.3, &a, lens);
      CHECK(tfp_fov_frame_pair(&game, &shown, nullptr) && game == lens && shown == b);
      if (shown > last + 1e-6f) down = false;
      last = shown;
    }
    CHECK(down && std::fabs(shown - lens) < 1e-3f);
    tfp_fov_set_lock(0, true, 5000);                                                              // the lock off: let go, the game's value to the bit
    for (int i = 0; i < 144; ++i) b = frame2(dt, 1.3, &a, lens);
    CHECK(tfp_fov_frame_pair(&game, &shown, nullptr) && game == lens && shown == lens && b == lens);
    aim(false);
    tfp_fov_end(gen);
    tfp_fov_reset_for_tests();
    CHECK(!tfp_fov_frame_pair(&game, &shown, &calls) && calls == 0);
  }
  std::printf("fov lock helper: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
