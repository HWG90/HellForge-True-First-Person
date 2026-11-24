// The near helper (src/near_helper.c) armed on the fake game memory of its differential test (this build's layout), for
// a C test that drives it frame by frame beside the pose helper (tests/wire_pose_test.c, round 20: the gun and the near
// clip in the same frames). A C++ header builds that memory, so these few lines are C++; the test calls them as C.
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

static World W;
static Scene scene;
static uint64_t clock_us;
static struct { float meters; bool seen; } sent;
static uint64_t generation;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static uint64_t host_now(void *) { return clock_us; }
static bool host_true_slot(void *, uint64_t, uint64_t) { return true; }
static bool host_executable(void *, uint64_t) { return true; }
static bool host_cas(void *, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint64_t *>(slot), expected, desired); return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) { std::memcpy(out, data, length); return true; }
static bool host_pin(void *, uint64_t) { return true; }
static void host_lock(void *, int) {}
static void host_forward(void *, uint64_t, void *, float meters) { sent.seen = true; sent.meters = meters; }

extern "C" {
// Arms the near helper with `hip` m of hip clearance. 1: armed.
int near_world_begin(float hip, uint64_t now_us) {
  static bool built;
  if (!built) {
    W.build(HERE); built = true;
    W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker);
    World::put<uint64_t>(W.slot(), W.native_value);
  }
  TfpNearHost host{};
  host.hook = W.hook_value; host.read = host_read; host.now_us = host_now; host.writable_slot = host_true_slot; host.executable = host_executable;
  host.cas = host_cas; host.write = host_write; host.pin = host_pin; host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock;
  host.lock_shared = host_lock; host.unlock_shared = host_lock; host.forward = host_forward;
  tfp_near_reset_for_tests();
  tfp_near_set_host(&host);
  clock_us = now_us;
  scene = Scene{};
  W.apply(scene, 0);
  TfpNearConfig c{};
  c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA];
  c.avatar_owner = W.a[A_OWNER]; c.avatar_index = 0; c.hip = hip; c.aim = 0;
  return tfp_near_begin(&c, &generation) == 1;
}
// One call of the game's near setter from the pinned site on the armed camera, at `now_us`, in full aim (`full_aim` 1)
// or at the hip. Returns what the game's setter was handed.
float near_world_frame(float native, int full_aim, uint64_t now_us) {
  clock_us = now_us; sent.seen = false;
  scene.controls[4] = full_aim ? 1 : 0;
  W.apply(scene, 0);
  tfp_near_setter_from(W.caller(World::CALL_SITE), reinterpret_cast<void *>(W.a[A_CAMERA]), native);
  if (!sent.seen) { std::printf("near world: the hook did not forward\n"); std::exit(2); }
  return sent.meters;
}
void near_world_end(void) { tfp_near_end(generation); generation = 0; }
}
