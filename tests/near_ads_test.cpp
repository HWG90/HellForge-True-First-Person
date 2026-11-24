// Round 20: the near clip in full aim moves in with the gun (src/ads_seat.h tfp_ads_near), and the game's own sight
// distance is published for the pose helper (tfp_near_ads_probe), through the near helper itself on the differential
// test's fake game memory (this build's layout). Beyond retail: with nothing closer (the start-up value) full aim forwards
// the game's own value, as retail and tests/run-diff-near.sh have it. No retail source needed. Linux (process_vm_readv).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>
#define _Static_assert static_assert
extern "C" {
#include "near_helper.h"
}
#include "diff_near_helper_world.h"
using namespace dt;

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; std::printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool near(double a, double b, double e) { return std::fabs(a - b) <= e; }

static World W;
static Scene scene;
static uint64_t now_us = 1000000;
static struct { float meters; bool seen; } sent;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static uint64_t host_now(void *) { return now_us; }
static bool host_true_slot(void *, uint64_t, uint64_t) { return true; }
static bool host_executable(void *, uint64_t) { return true; }
static bool host_cas(void *, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint64_t *>(slot), expected, desired); return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) { std::memcpy(out, data, length); return true; }
static bool host_pin(void *, uint64_t) { return true; }
static void host_lock(void *, int) {}
static void host_forward(void *, uint64_t, void *, float meters) { sent.seen = true; sent.meters = meters; }
// The pose helper's side (tfp_pose_ads_near), scripted: how far the gun is drawn in.
static struct { float closer; bool on; unsigned asked; } pose = {0, true, 0};
static bool pose_source(float *closer) { ++pose.asked; *closer = pose.closer; return pose.on; }
static void set_closer(float m) { pose.closer = m; }

// One call of the hook from the pinned site on the armed camera, 8 ms later. Returns what the game's setter was handed.
static float frame(float native) {
  now_us += 8000; sent.seen = false;
  W.apply(scene, 0);
  tfp_near_setter_from(W.caller(World::CALL_SITE), reinterpret_cast<void *>(W.a[A_CAMERA]), native);
  if (!sent.seen) { std::printf("the hook did not forward\n"); std::exit(2); }
  return sent.meters;
}

int main() {
  W.build(HERE);
  W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  World::put<uint64_t>(W.slot(), W.native_value);
  TfpNearHost host{};
  host.hook = W.hook_value; host.read = host_read; host.now_us = host_now; host.writable_slot = host_true_slot; host.executable = host_executable;
  host.cas = host_cas; host.write = host_write; host.pin = host_pin; host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock;
  host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_near_reset_for_tests();
  tfp_near_set_host(&host);
  TfpNearConfig c{};
  c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA];
  c.avatar_owner = W.a[A_OWNER]; c.avatar_index = 0; c.hip = .10f; c.aim = 0;
  uint64_t gen = 0;
  W.apply(scene, 0);
  if (tfp_near_begin(&c, &gen) != 1) { std::printf("the near helper did not arm (error %u)\n", tfp_near_error()); return 2; }
  tfp_near_set_ads_source(pose_source);
  float sight = 0, native = 0, applied = 0;
  // At the hip: the game's sight (0.173 m, the Scythe's) is seen and published; the hip clearance is what is shown.
  float last = 0;
  for (int i = 0; i < 60; ++i) last = frame(.173f);
  CHECK(tfp_near_ads_probe(&sight, &native, &applied) && near(sight, .173, 1e-6) && near(native, .173, 1e-6));
  CHECK(near(last, .10, 1e-3));
  // At the hip the gun drawn in is nothing to the near clip: the hip clearance, and the pose helper is not even asked
  set_closer(.073f); pose.asked = 0;
  for (int i = 0; i < 20; ++i) last = frame(.173f);
  CHECK(near(last, .10, 1e-6) && pose.asked == 0);
  set_closer(0);
  // Full aim, nothing closer (the start-up value): the game's own value, as retail.
  scene.controls[4] = 1;
  for (int i = 0; i < 60; ++i) last = frame(.173f);
  CHECK(near(last, .173, 1e-6) && tfp_near_ads_probe(&sight, &native, &applied) && near(sight, .173, 1e-6) && near(applied, .173, 1e-6));
  // The gun 7.3 cm closer: the near clip comes in with it in the same call, never behind it (the review's B1)
  set_closer(.073f);
  last = frame(.173f);
  CHECK(near(last, .10, 1e-6));
  // round 20c (review 28, B1): the game's value less the gun's move, whatever the game's seat: a new zoom level's value
  // (0.22 m) less the same 7.3 cm, eased out to (a step away from the eye), and back in at once
  last = frame(.22f);
  CHECK(last > .10f && last < .147f);
  for (int i = 0; i < 60; ++i) last = frame(.22f);
  CHECK(near(last, .22 - .073, 1e-6));
  last = frame(.173f);
  CHECK(near(last, .10, 1e-6));
  // the gun going back out: the near clip eases out after it, never ahead of it
  set_closer(.02f);
  float before = last, worst = 0;
  for (int i = 0; i < 60; ++i) { last = frame(.173f); CHECK(last >= before - 1e-6f && last <= .173f - .02f + 1e-6f); if (last - before > worst) worst = last - before; before = last; }
  CHECK(near(last, .153, 1e-3) && worst < .02f && worst > 0);
  // the pose helper with no lease: nothing closer
  pose.on = false; set_closer(.073f);
  for (int i = 0; i < 80; ++i) last = frame(.173f);
  CHECK(near(last, .173, 1e-3));
  pose.on = true;
  set_closer(.073f);
  last = frame(.173f);
  CHECK(near(last, .10, 1e-6));
  // never under the game's own 0.05
  set_closer(.3f);
  for (int i = 0; i < 60; ++i) last = frame(.173f);
  CHECK(near(last, .05, 1e-6));
  // a pistol's iron sights: the game's 0.05 (its own constant) is no sight; nothing published, nothing changed
  set_closer(.073f);
  for (int i = 0; i < 60; ++i) last = frame(.05f);
  CHECK(near(last, .05, 1e-6) && tfp_near_ads_probe(&sight, &native, &applied) && sight == 0);
  // nothing closer again: back to the game's value
  set_closer(0);
  for (int i = 0; i < 80; ++i) last = frame(.173f);
  CHECK(near(last, .173, 1e-3));
  // a bad value is nothing
  set_closer(NAN);
  for (int i = 0; i < 80; ++i) last = frame(.173f);
  CHECK(near(last, .173, 1e-6));
  scene.controls[4] = 0;
  tfp_near_set_ads_source(nullptr);
  tfp_near_end(gen);
  CHECK(!tfp_near_ads_probe(&sight, &native, &applied) && sight == 0);      // no lease: nothing known
  std::printf("near ads: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
