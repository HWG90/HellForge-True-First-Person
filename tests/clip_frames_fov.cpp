// tests/clip_frames_test.cpp: the field-of-view helper's side, on its own fake game memory (the world of
// tests/fov_lock_helper_test.cpp, in a namespace of its own: the near helper's world has the same names).
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
#define dt dt_fov
#include "diff_fov_helper_world.h"
using namespace dt_fov;
#undef dt
#include "clip_frames.h"

static World W;
static bool built;
static double seconds_now = 100;
static struct { float radians; bool seen; } sent;
static uint64_t gen;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static bool host_readable_span(void *, uint64_t, size_t) { return true; }
static bool host_seconds(void *, double *out) { *out = seconds_now; return true; }
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
static void host_forward(void *, uint64_t, void *, float radians) { sent.seen = true; sent.radians = radians; }
static const float RAD = 0.01745329251994329577f;

// The weapon camera at the hip (kind 6, the newest request), and the helper armed on it.
void fovw_fresh() {
  if (!built) { W.build(HERE); W.native_value = W.a[A_EXE] + HERE.setter; W.hook_value = reinterpret_cast<uint64_t>(&hook_marker); built = true; }
  else tfp_fov_end(gen);
  tfp_fov_reset_for_tests();
  TfpFovHost host{};
  host.hook = reinterpret_cast<uint64_t>(&hook_marker); host.read = host_read; host.readable_span = host_readable_span; host.seconds = host_seconds;
  host.writable_slot = host_true_slot; host.writable_request_field = host_true_field; host.executable = host_executable;
  host.cas = host_cas; host.cas32 = host_cas32; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock; host.unlock_exclusive = host_lock; host.forward = host_forward;
  tfp_fov_set_host(&host);
  World::put<uint64_t>(W.slot(), W.native_value);
  World::put<uint32_t>(W.a[A_CONTROLLER], 0); fovw_aim(false); World::put<uint8_t>(W.a[A_CONTROLLER] + 5, 0);
  World::put<uint32_t>(W.a[A_CONTEXT] + 0x1F8, 0); World::put<uint32_t>(W.a[A_CONTEXT] + 0x1FC, 2);
  const uint32_t rows[2][4] = {{4, AVATAR, WEAPON, BODY}, {6, AVATAR, WEAPON, BODY}};
  for (int i = 0; i < 2; ++i) { World::put(W.a[A_CONTEXT] + row_offset(i), rows[i]); World::put<float>(W.a[A_CONTEXT] + row_offset(i) + 0x3C, 90 * RAD); }
  TfpFovConfig c{};
  c.size = sizeof c; c.abi = 1; c.game = W.a[A_GAME]; c.exe = W.a[A_EXE]; c.context = W.a[A_CONTEXT]; c.camera = W.a[A_CAMERA]; c.avatar_owner = W.a[A_OWNER];
  c.request = W.a[A_CONTEXT] + row_offset(1); c.avatar_index = 0; c.avatar = AVATAR; c.body = BODY; c.weapon = WEAPON;
  gen = 0;
  if (tfp_fov_begin(&c, &gen) != 1) { std::printf("the field-of-view helper did not arm (error %u)\n", tfp_fov_error()); std::exit(2); }
  seconds_now += 50;
}
void fovw_aim(bool held) { World::put<uint8_t>(W.a[A_CONTROLLER] + 4, held ? 1 : 0); }
void fovw_lock(float locked_degrees, float aim) { tfp_fov_set_lock_aim(locked_degrees * RAD, aim, 5000); }
// One call of the game's setter on the armed camera `ms` later: the hooked one (the queue's dispatcher) or the
// forwarded one that follows it in the frame (the post-camera rig update). Returns what the setter was handed.
float fovw_call(double ms, bool hooked, float native_degrees) {
  static const uint64_t SECOND_CALL = 0x127aa92 - 0x127a120;
  seconds_now += ms / 1000; sent.seen = false;
  tfp_fov_setter_from(W.a[A_GAME] + HERE.call + 6 + (hooked ? 0 : SECOND_CALL), reinterpret_cast<void *>(W.a[A_CAMERA]), native_degrees * RAD);
  if (!sent.seen) { std::printf("the field-of-view hook did not forward\n"); std::exit(2); }
  return sent.radians / RAD;
}
