// Differential test: the retail field-of-view helper (native/first_person/native_ads_fov.cpp, build 26568081,
// compiled natively over tests/winshim with its NADSF_FIXTURE seams) against its C translation
// (src/fov_helper.c, build 25480438). The same logical state is written into two fake game memories, one per
// build's addresses (tests/diff_fov_helper_world.h); both helpers are driven with the same calls, and after
// EVERY call these are compared: the return value; every memory read, write, 64-bit and 32-bit
// compare-and-swap, lock operation and forwarded setter call, in order (addresses by name, the forwarded lens
// bit for bit); how often the clock was read; how many slot swaps were attempted; what the dispatch slot
// holds; the request records of the camera context (where the FirdPerson policy writes); the generation
// written; the error code; the statistics structure. The first difference is printed and the test exits
// non-zero. Every script runs in its own child process, so both helpers start from their start-up state.
// The retail tree is read, never written. See docs/FOV-HELPER-TRANSLATION.md, "Differential test".
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include "fov_extra.h"
#define NADSF_FIXTURE
#include "native_ads_fov.cpp"
#define _Static_assert static_assert
extern "C" {
#include "fov_helper.h"
}
#include "diff_fov_helper_world.h"

static_assert(sizeof(NADSF_Config) == sizeof(TfpFovConfig) && sizeof(NADSF_Stats) == sizeof(TfpFovStats), "the two helpers' structures have one size");
static_assert(offsetof(NADSF_Config, request) == offsetof(TfpFovConfig, request) && offsetof(NADSF_Config, avatar_index) == offsetof(TfpFovConfig, avatar_index) &&
              offsetof(NADSF_Config, weapon) == offsetof(TfpFovConfig, weapon) && offsetof(NADSF_Config, reserved) == offsetof(TfpFovConfig, reserved) &&
              offsetof(NADSF_Stats, seen) == offsetof(TfpFovStats, seen) && offsetof(NADSF_Stats, ads_forwarded) == offsetof(TfpFovStats, ads_forwarded) &&
              offsetof(NADSF_Stats, last_native) == offsetof(TfpFovStats, last_native) && offsetof(NADSF_Stats, padding) == offsetof(TfpFovStats, padding),
              "the two helpers' structures have one layout");
static_assert(static_cast<int>(TFP_FOV_REQUEST_FIELD_INVALID) == static_cast<int>(REQUEST_FIELD_INVALID) && static_cast<int>(TFP_FOV_TOKEN_MISMATCH) == static_cast<int>(TOKEN_MISMATCH) &&
              static_cast<int>(TFP_FOV_SOURCE_INVALID) == static_cast<int>(SOURCE_INVALID) && static_cast<int>(TFP_FOV_CLEANUP_PENDING) == static_cast<int>(CLEANUP_PENDING), "error numbers");

namespace dt {

// ---- what the children report to the parent --------------------------------------------------------------
struct Shared { uint64_t calls, frames, events, skipped_frames, translation_only, probe_reads, probe_values; };
static Shared *shared;
static struct { uint64_t calls, frames, events, skipped_frames, translation_only; } counted;

// ---- the two worlds and the record of what each helper did ------------------------------------------------
static World W[2]; // 0: retail memory, read by the original; 1: this build's memory, read by the translation
enum { EV_READ, EV_WRITE, EV_CAS, EV_CAS32, EV_LOCK, EV_FORWARD };
struct Event { uint32_t kind, id; uint64_t v[4]; };
static std::vector<Event> events[2];
static int reads_in_call[2], writes_in_call[2], forwards_in_call[2];
static uint64_t translation_clock_reads = 0;
static uint32_t translation_cas_calls = 0;
static int lock_depth[2];
static std::string script_name, call_text;
static uint64_t call_index, frame_index;
static bool discovering; // this child counts the probed call's reads for the parent

[[noreturn]] static void fail(const char *format, ...) {
  char text[1024];
  va_list arguments; va_start(arguments, format); std::vsnprintf(text, sizeof text, format, arguments); va_end(arguments);
  std::fprintf(stderr, "DIFFERENCE script \"%s\", call %llu (frame %llu): %s\n  %s\n", script_name.c_str(),
               static_cast<unsigned long long>(call_index), static_cast<unsigned long long>(frame_index), call_text.c_str(), text);
  std::fflush(stderr);
  std::_Exit(1);
}

// A value written into both worlds: plain bytes, the address of a named place, or a dispatch slot value.
struct Value { int kind = 0; uint64_t raw = 0; Sym sym{0, 0}; int length = 4; };
static Value raw_value(uint64_t raw, int length = 4) { Value v; v.raw = raw; v.length = length; return v; }
static Value float_value(float f) { uint32_t u; std::memcpy(&u, &f, 4); return raw_value(u, 4); }
static Value address_value(int id, uint64_t offset = 0) { Value v; v.kind = 1; v.sym = {id, offset}; v.length = 8; return v; }
enum : uint64_t { SLOT_NATIVE = 1, SLOT_HELPER = 2, SLOT_FOREIGN = 0x7770000 };
static Value slot_value(uint64_t which) { Value v; v.kind = 2; v.raw = which; v.length = 8; return v; }
static uint64_t slot_class(int w, uint64_t value) { return value == W[w].native_value ? SLOT_NATIVE : value == W[w].hook_value ? SLOT_HELPER : value; }
static uint64_t slot_raw(int w, uint64_t which) { return which == SLOT_NATIVE ? W[w].native_value : which == SLOT_HELPER ? W[w].hook_value : which; }
static void store(int w, Sym at, const Value &v) {
  uint64_t raw = v.kind == 1 ? W[w].at(v.sym) : v.kind == 2 ? slot_raw(w, v.raw) : v.raw;
  std::memcpy(reinterpret_cast<void *>(W[w].at(at)), &raw, static_cast<size_t>(v.length));
}
static void set(Sym at, const Value &v) { for (int w = 0; w < 2; ++w) store(w, at, v); }
static void set_u8(int id, uint64_t offset, uint8_t v) { set({id, offset}, raw_value(v, 1)); }
static void set_u32(int id, uint64_t offset, uint32_t v) { set({id, offset}, raw_value(v, 4)); }
static void set_f32(int id, uint64_t offset, float v) { set({id, offset}, float_value(v)); }
static bool safe_write(uint64_t at, const void *data, size_t length) {
  struct iovec local = {const_cast<void *>(data), length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_writev(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static bool peek(uint64_t at, uint64_t *out) {
  struct iovec local = {out, 8}, remote = {reinterpret_cast<void *>(at), 8};
  return process_vm_readv(shim_pid(), &local, 1, &remote, 1, 0) == 8;
}
static void protect(uint64_t at, size_t length, int protection) {
  const uint64_t begin = at & ~4095ull, end = (at + length + 4095) & ~4095ull;
  mprotect(reinterpret_cast<void *>(begin), end - begin, protection);
}
static void protect_region(int id, int protection) { for (int w = 0; w < 2; ++w) mprotect(reinterpret_cast<void *>(W[w].a[id]), W[w].size[id], protection); }

// ---- things a script can arrange to happen inside the next call --------------------------------------------
// One read that fails (and heals, or stays unreadable to the end of the call), or a value that changes just
// before a read.
enum { INJECT_NONE, INJECT_FAIL, INJECT_FAIL_STAY, INJECT_VALUE };
static struct {
  int mode = INJECT_NONE, index = -1; bool active = false;
  Sym at{0, 0}; Value value;
  uint64_t kept_at[2] = {0, 0}; size_t kept_length[2] = {0, 0};
} inject;
// A value that changes while the game's setter runs (the n-th forwarded call of the next call).
struct Poke { Sym at; Value value; };
struct ForwardPoke { int nth; Poke poke; };
static std::vector<ForwardPoke> forward_pokes;
// The caller's output page becomes read-only after the n-th write of the next call.
static int write_fail_after = 0; static bool io_locked[2];

static void on_read(int w, uint64_t at, size_t size, int after, bool complete) {
  const bool chosen = inject.active && reads_in_call[w] == inject.index;
  if (!after) {
    if (!chosen) return;
    if (inject.mode == INJECT_VALUE) { store(w, inject.at, inject.value); return; }
    protect(at, size, PROT_NONE);
    return;
  }
  if (chosen && inject.mode != INJECT_VALUE) {
    if (inject.mode == INJECT_FAIL) protect(at, size, PROT_READ | PROT_WRITE);
    else { inject.kept_at[w] = at; inject.kept_length[w] = size; }
  }
  const Sym name = W[w].name(at);
  events[w].push_back({EV_READ, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0}});
  ++reads_in_call[w];
}
static void on_write(int w, uint64_t at, size_t size, bool complete) {
  const Sym name = W[w].name(at);
  events[w].push_back({EV_WRITE, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0}});
  if (++writes_in_call[w] == write_fail_after) { mprotect(reinterpret_cast<void *>(W[w].a[A_IO]), 4096, PROT_READ); io_locked[w] = true; }
}
static void on_cas(int w, uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) {
  const Sym name = W[w].name(target);
  events[w].push_back({EV_CAS, static_cast<uint32_t>(name.id), {name.offset, slot_class(w, exchange), slot_class(w, comparand), slot_class(w, observed)}});
}
static void on_cas32(int w, uint64_t target, uint32_t exchange, uint32_t comparand, uint32_t observed) {
  const Sym name = W[w].name(target);
  events[w].push_back({EV_CAS32, static_cast<uint32_t>(name.id), {name.offset, exchange, comparand, observed}});
}
static void on_lock(int w, int lock, int operation) { events[w].push_back({EV_LOCK, static_cast<uint32_t>(lock), {static_cast<uint64_t>(operation), 0, 0, 0}}); }
// The game's own setter: recorded.
static void on_forward(int w, void *camera, float radians) {
  const Sym name = W[w].name(reinterpret_cast<uint64_t>(camera));
  uint32_t bits = 0; std::memcpy(&bits, &radians, 4);
  events[w].push_back({EV_FORWARD, static_cast<uint32_t>(name.id), {name.offset, bits, 0, 0}});
  ++forwards_in_call[w];
  for (const ForwardPoke &p : forward_pokes) if (p.nth == forwards_in_call[w]) store(w, p.poke.at, p.poke.value);
}
static std::string describe(const Event &e) {
  char text[256], where[96];
  if (e.id == 0xffff) std::snprintf(where, sizeof where, "address %#llx", static_cast<unsigned long long>(e.v[0]));
  else if (e.kind != EV_LOCK) std::snprintf(where, sizeof where, "%s+%#llx", A_NAMES[e.id], static_cast<unsigned long long>(e.v[0]));
  switch (e.kind) {
  case EV_READ: std::snprintf(text, sizeof text, "read %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_WRITE: std::snprintf(text, sizeof text, "write %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_CAS: std::snprintf(text, sizeof text, "slot compare-and-swap at %s: store %#llx if %#llx, found %#llx (1 = game's setter, 2 = helper)", where,
                             (unsigned long long)e.v[1], (unsigned long long)e.v[2], (unsigned long long)e.v[3]); break;
  case EV_CAS32: std::snprintf(text, sizeof text, "lens compare-and-swap at %s: store %#llx if %#llx, found %#llx", where,
                               (unsigned long long)e.v[1], (unsigned long long)e.v[2], (unsigned long long)e.v[3]); break;
  case EV_LOCK: std::snprintf(text, sizeof text, "%s lock %s", e.id ? "policy" : "control", e.v[0] & 1 ? "released" : "taken"); break;
  default: {
    float f; uint32_t bits = static_cast<uint32_t>(e.v[1]); std::memcpy(&f, &bits, 4);
    std::snprintf(text, sizeof text, "forward to the game's setter: %s, lens %.9g [%08x]", where, f, bits);
  }
  }
  return text;
}
static void compare_events(const char *stage) {
  const size_t n = std::min(events[0].size(), events[1].size());
  for (size_t i = 0; i < n; ++i)
    if (std::memcmp(&events[0][i], &events[1][i], sizeof(Event)))
      fail("%s: step %zu differs\n  retail:      %s\n  translation: %s", stage, i, describe(events[0][i]).c_str(), describe(events[1][i]).c_str());
  if (events[0].size() != events[1].size()) {
    const int longer = events[0].size() > events[1].size() ? 0 : 1;
    fail("%s: retail made %zu steps, the translation %zu; first extra step (%s): %s", stage, events[0].size(), events[1].size(),
         longer ? "translation" : "retail", describe(events[longer][n]).c_str());
  }
  counted.events += n;
}
static void compare_bytes(const char *what, uint64_t retail, uint64_t translation, size_t length, const char *const *names = nullptr, size_t name_count = 0) {
  const uint8_t *a = reinterpret_cast<const uint8_t *>(retail), *b = reinterpret_cast<const uint8_t *>(translation);
  if (!std::memcmp(a, b, length)) return;
  for (size_t i = 0; i < length; i += 4) {
    const size_t n = std::min<size_t>(4, length - i);
    if (!std::memcmp(a + i, b + i, n)) continue;
    uint32_t x = 0, y = 0; float fx = 0, fy = 0;
    std::memcpy(&x, a + i, n); std::memcpy(&y, b + i, n); std::memcpy(&fx, &x, 4); std::memcpy(&fy, &y, 4);
    fail("%s differs at byte %zu (%s): retail %#010x (as float %.9g), translation %#010x (as float %.9g)", what, i,
         names && i / 4 < name_count ? names[i / 4] : "", x, fx, y, fy);
  }
}
static const char *const STATS_NAMES[] = {"size", "abi", "installed", "armed", "error", "reserved", "seen", "seen", "forwarded", "forwarded", "replaced", "replaced",
                                          "immediate_applies", "immediate_applies", "binds", "binds", "ads_forwarded", "ads_forwarded",
                                          "last_native", "last_applied", "source_degrees", "padding"};

// ---- the translation's host: the same facts the original gets from the stand-in Windows calls ------------
static bool answer_getter_executable = true, answer_pin_executable = true; // asked of the translation only
static bool expect_null_original = false; // one translation-only call of the hook before any begin
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  on_read(1, at, length, 0, false);
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  const bool complete = process_vm_readv(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
  on_read(1, at, length, 1, complete);
  return complete;
}
static bool host_readable_span(void *, uint64_t at, size_t length) { return ::readable_span(at, length); }
// The original's fixture clock (cpp:83): the value, and whether the clock works.
static bool host_seconds(void *, double *out) { ++translation_clock_reads; *out = fixture_now; return fixture_clock_ok; }
static bool host_writable_slot(void *, uint64_t at, uint64_t exe) {
  ++counted.translation_only;
  if (exe != W[1].a[A_EXE]) fail("the translation asked whether the slot is writable in image %#llx, not in the executable", static_cast<unsigned long long>(exe));
  return ::writable_slot(at, 0); // the original's fixture answer: committed, writable, private (and fixture_slot_readonly)
}
static bool host_writable_request_field(void *, uint64_t at) { return ::writable_request_field(at); } // the original's own page question
static bool host_executable(void *, uint64_t at) {
  if (at == W[1].a[A_EXE] + HERE.getter) { ++counted.translation_only; return answer_getter_executable; }
  // The original (fixture build) asks this of the function standing in for the game's setter.
  if (at == W[1].a[A_EXE] + HERE.setter) return ::executable(W[0].native_value);
  fail("the translation asked whether an unexpected address is executable: %#llx", static_cast<unsigned long long>(at));
}
static bool host_cas(void *, uint64_t slot_address, uint64_t expected, uint64_t desired, uint64_t *observed) {
  // The original's fixture seams (cpp:458-459), on the translation's own call count.
  if (++translation_cas_calls == fixture_cas_fail_at) return false;
  uint64_t *target = reinterpret_cast<uint64_t *>(slot_address);
  if (translation_cas_calls == fixture_foreign_cas_at) __sync_lock_test_and_set(target, fixture_foreign_value);
  *observed = __sync_val_compare_and_swap(target, expected, desired);
  on_cas(1, slot_address, desired, expected, *observed);
  return true;
}
static bool host_cas32(void *, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  *observed = __sync_val_compare_and_swap(reinterpret_cast<uint32_t *>(at), expected, desired);
  on_cas32(1, at, desired, expected, *observed);
  return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) {
  const bool complete = safe_write(reinterpret_cast<uint64_t>(out), data, length);
  on_write(1, reinterpret_cast<uint64_t>(out), length, complete);
  return complete;
}
static bool host_pin(void *, uint64_t address) {
  if (address == reinterpret_cast<uint64_t>(&tfp_fov_begin)) return !shim_pin_fails;
  if (address == W[1].a[A_EXE]) { ++counted.translation_only; return answer_pin_executable; }
  fail("the translation pins an unexpected address: %#llx", static_cast<unsigned long long>(address));
}
static void host_lock(int lock, int operation, int change) {
  lock_depth[lock] += change;
  if (lock_depth[lock] < 0 || lock_depth[lock] > 1)
    fail("the translation's locks are unbalanced (control %d, policy %d)", lock_depth[0], lock_depth[1]);
  on_lock(1, lock, operation);
}
static void host_lock_exclusive(void *, int lock) { host_lock(lock, 0, 1); }
static void host_unlock_exclusive(void *, int lock) { host_lock(lock, 1, -1); }
static void host_forward(void *, uint64_t original_setter, void *camera, float radians) {
  if (expect_null_original) {
    if (original_setter != 0) fail("the translation still knows an original setter (%#llx) after its reset", static_cast<unsigned long long>(original_setter));
    on_forward(1, camera, radians); return;
  }
  if (original_setter != W[1].native_value) fail("the translation forwards to %#llx, not to the game's setter", static_cast<unsigned long long>(original_setter));
  on_forward(1, camera, radians);
}
static void retail_setter(void *camera, float radians) { on_forward(0, camera, radians); }

static void build() {
  W[0].build(RETAIL); W[1].build(HERE);
  W[0].native_value = reinterpret_cast<uint64_t>(&retail_setter); W[0].hook_value = reinterpret_cast<uint64_t>(&setter_hook);
  W[1].native_value = W[1].a[A_EXE] + HERE.setter; W[1].hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].slot(), W[w].native_value); World::put<uint64_t>(W[w].a[A_API_OTHER] + 0x80, W[w].native_value); }
  fixture_target = retail_setter;
  shim_on_read = [](uint64_t at, size_t size, int after, bool complete) { on_read(0, at, size, after, complete); };
  shim_on_write = [](uint64_t at, size_t size, bool complete) { on_write(0, at, size, complete); };
  shim_on_cas = [](uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) { on_cas(0, target, exchange, comparand, observed); };
  shim_on_cas32 = [](uint64_t target, uint32_t exchange, uint32_t comparand, uint32_t observed) { on_cas32(0, target, exchange, comparand, observed); };
  shim_on_lock = [](void *lock, int operation) { on_lock(0, lock == &control_lock ? 0 : 1, operation); };
  TfpFovHost host{};
  host.hook = W[1].hook_value; host.read = host_read; host.readable_span = host_readable_span; host.seconds = host_seconds;
  host.writable_slot = host_writable_slot; host.writable_request_field = host_writable_request_field; host.executable = host_executable;
  host.cas = host_cas; host.cas32 = host_cas32; host.write = host_write; host.pin = host_pin;
  host.lock_exclusive = host_lock_exclusive; host.unlock_exclusive = host_unlock_exclusive; host.forward = host_forward;
  tfp_fov_reset_for_tests();
  tfp_fov_set_host(&host);
}

// ---- a run: the state, the calls, and the comparison after each -------------------------------------------
static const float RAD = 0.01745329251994329577f;
struct Run {
  // config (logical; the pointers are each world's own)
  uint32_t size = 80, abi = 1, index = 0, avatar = AVATAR, body = BODY, weapon = WEAPON; uint64_t reserved_ms = 0;
  int request_row = 1; uint64_t request_absolute = 0; bool request_is_absolute = false; int64_t request_delta = 0;
  int64_t context_delta = 0, camera_delta = 0, owner_delta = 0, game_delta = 0, exe_delta = 0;
  uint64_t context_absolute = 0, camera_absolute = 0; // nonzero: this value instead of the world's own address
  bool config_null = false, generation_null = false;
  // the hook's arguments
  bool caller_ours = true; int64_t camera_argument_delta = 0;
  uint64_t gen = 0; uint32_t result = 0;
  // probes
  int probe_mode = INJECT_NONE, probe_index = -1, probe_value = 0;
  std::vector<Poke> probe_values;
  bool probing = false;

  // ---- the state of the game, written into both worlds ----
  int flags_id() const { return index ? A_O_FLAGS1 : A_O_FLAGS0; }
  void controls(uint32_t mask = 0, bool aim = false, bool guard = false) { set_u32(A_CONTROLLER, 0, mask); set_u8(A_CONTROLLER, 4, aim ? 1 : 0); set_u8(A_CONTROLLER, 5, guard ? 1 : 0); }
  void aim(bool held) { set_u8(A_CONTROLLER, 4, held ? 1 : 0); }
  void shoulder(bool held) { set_u8(flags_id(), 10, held ? 4 : 0); set_u8(flags_id(), 15, held ? 32 : 0); }
  void seated(bool held) { set_u8(flags_id(), 14, held ? 4 : 0); }
  void ring(uint32_t head, uint32_t tail) { set_u32(A_CONTEXT, 0x1F8, head); set_u32(A_CONTEXT, 0x1FC, tail); }
  void row(int i, uint32_t kind, uint32_t row_avatar, uint32_t row_weapon, uint32_t row_body) {
    const uint64_t at = row_offset(i);
    set_u32(A_CONTEXT, at, kind); set_u32(A_CONTEXT, at + 4, row_avatar); set_u32(A_CONTEXT, at + 8, row_weapon); set_u32(A_CONTEXT, at + 12, row_body);
  }
  void lens(int i, float radians) { set_f32(A_CONTEXT, row_offset(i) + 0x3C, radians); }
  void lens_bits(int i, uint32_t bits) { set_u32(A_CONTEXT, row_offset(i) + 0x3C, bits); }
  // The newest request: the ring from 0 to `tail`, its last row of this kind for this avatar, body and weapon.
  void top(uint32_t kind, uint32_t tail, uint32_t row_weapon) {
    ring(0, tail); request_row = static_cast<int>((tail + 31) % 32);
    row(request_row, kind, avatar, row_weapon, body); lens(request_row, 90 * RAD);
  }
  void normal() { controls(); shoulder(false); seated(false); top(4, 1, weapon); }  // the third-person camera
  void fp() { normal(); top(6, 2, weapon); }                                        // the weapon camera, at the hip
  void degrees(float f) { set_f32(A_S_FOV, 0, f); }
  void slot_holds(uint64_t which) { set({A_API, 0x80}, slot_value(which)); }

  void observe() {
    const uint32_t retail_error = NADSF_Error(), translation_error = tfp_fov_error();
    if (retail_error != translation_error) fail("error code: retail %u, translation %u", retail_error, translation_error);
    const bool held = ::reserved.load() != 0;
    const uint64_t g = held ? ::owned_generation : 0;
    for (int w = 0; w < 2; ++w) { events[w].clear(); std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS), 0x5A, 0x100); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS, 88); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS + 4, 1); }
    const uint32_t a = NADSF_ReadStats(g, reinterpret_cast<NADSF_Stats *>(W[0].a[A_IO] + IO_STATS));
    const uint32_t b = tfp_fov_read_stats(g, reinterpret_cast<TfpFovStats *>(W[1].a[A_IO] + IO_STATS));
    if (a != b) fail("read_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
    compare_bytes("stats", W[0].a[A_IO] + IO_STATS, W[1].a[A_IO] + IO_STATS, 0x100, STATS_NAMES, 22);
    compare_events("while reading the statistics");
    if (NADSF_Error() != retail_error || tfp_fov_error() != translation_error) fail("reading the statistics changed an error code");
  }
  void finish(int w) {
    if (inject.kept_length[w]) { protect(inject.kept_at[w], inject.kept_length[w], PROT_READ | PROT_WRITE); inject.kept_length[w] = 0; }
    if (io_locked[w]) { mprotect(reinterpret_cast<void *>(W[w].a[A_IO]), 4096, PROT_READ | PROT_WRITE); io_locked[w] = false; }
  }
  template <class Retail, class Translation> uint32_t both(const char *text, bool probe_here, Retail retail, Translation translation) {
    ++call_index; call_text = text;
    for (int w = 0; w < 2; ++w) { events[w].clear(); reads_in_call[w] = writes_in_call[w] = forwards_in_call[w] = 0; }
    inject.active = probe_here && inject.mode != INJECT_NONE;
    const uint64_t retail_clock = fixture_clock_reads, translation_clock = translation_clock_reads;
    const uint32_t a = retail(); finish(0);
    const uint32_t b = translation(); finish(1);
    inject.active = false; forward_pokes.clear(); write_fail_after = 0;
    if (probe_here) { if (discovering) shared->probe_reads = static_cast<uint64_t>(reads_in_call[0]); inject.mode = INJECT_NONE; }
    if (a != b) fail("return value: retail %u, translation %u", a, b);
    compare_events("during the call");
    if (fixture_clock_reads - retail_clock != translation_clock_reads - translation_clock)
      fail("clock reads: retail %llu, translation %llu", (unsigned long long)(fixture_clock_reads - retail_clock), (unsigned long long)(translation_clock_reads - translation_clock));
    if (fixture_cas_calls != translation_cas_calls) fail("slot swaps attempted so far: retail %u, translation %u", fixture_cas_calls, translation_cas_calls);
    uint64_t slots[2]; for (int w = 0; w < 2; ++w) if (!peek(W[w].slot(), &slots[w])) slots[w] = 0xdead; // unreadable
    if (slot_class(0, slots[0]) != slot_class(1, slots[1]))
      fail("dispatch slot (1 = game's setter, 2 = helper): retail %#llx, translation %#llx", (unsigned long long)slot_class(0, slots[0]), (unsigned long long)slot_class(1, slots[1]));
    compare_bytes("the camera context's request records (ring at +0x1f8)", W[0].a[A_CONTEXT] + 0x1F8, W[1].a[A_CONTEXT] + 0x1F8, 0x2000 - 0x1F8);
    if (lock_depth[0] || lock_depth[1]) fail("the translation returned holding a lock (control %d, policy %d)", lock_depth[0], lock_depth[1]);
    observe();
    ++counted.calls;
    result = a; reached();
    return a;
  }
  bool take_probe() { const bool here = probing; probing = false; return here; }

  void write_config(int w) {
    NADSF_Config c{};
    c.size = size; c.abi = abi; c.game = W[w].a[A_GAME] + game_delta; c.exe = W[w].a[A_EXE] + exe_delta;
    c.context = W[w].a[A_CONTEXT] + context_delta; c.camera = W[w].a[A_CAMERA] + camera_delta; c.avatar_owner = W[w].a[A_OWNER] + owner_delta;
    if (context_absolute) c.context = context_absolute;
    if (camera_absolute) c.camera = camera_absolute;
    c.request = request_is_absolute ? request_absolute : W[w].a[A_CONTEXT] + row_offset(request_row) + request_delta;
    c.avatar_index = index; c.avatar = avatar; c.body = body; c.weapon = weapon; c.reserved = reserved_ms;
    World::put(W[w].a[A_IO] + IO_CONFIG, c);
  }
  template <class Config> Config *config_pointer(int w) { return config_null ? nullptr : reinterpret_cast<Config *>(W[w].a[A_IO] + IO_CONFIG); }
  uint64_t *generation_pointer(int w) { return generation_null ? nullptr : reinterpret_cast<uint64_t *>(W[w].a[A_IO] + IO_GENERATION); }
  void after_begin() {
    uint64_t g[2]; for (int w = 0; w < 2; ++w) std::memcpy(&g[w], reinterpret_cast<void *>(W[w].a[A_IO] + IO_GENERATION), 8);
    if (g[0] != g[1]) fail("generation written by begin: retail %llu, translation %llu", (unsigned long long)g[0], (unsigned long long)g[1]);
    if (result && g[0] && g[0] != UINT64_MAX) gen = g[0];
    if (result == 2) ++got.cleanup_begins;
  }
  uint32_t begin() {
    for (int w = 0; w < 2; ++w) { write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
    both("begin", take_probe(), [&] { return NADSF_Begin(config_pointer<NADSF_Config>(0), generation_pointer(0)); },
         [&] { return tfp_fov_begin(config_pointer<TfpFovConfig>(1), generation_pointer(1)); });
    after_begin(); return result;
  }
  uint32_t begin_fird(float captured) {
    for (int w = 0; w < 2; ++w) { write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
    char t[64]; std::snprintf(t, sizeof t, "begin_fird(%.9g)", captured);
    both(t, take_probe(), [&] { return NADSF_BeginFird(config_pointer<NADSF_Config>(0), captured, generation_pointer(0)); },
         [&] { return tfp_fov_begin_fird(config_pointer<TfpFovConfig>(1), captured, generation_pointer(1)); });
    after_begin(); return result;
  }
  uint32_t validate() {
    for (int w = 0; w < 2; ++w) write_config(w);
    return both("validate", take_probe(), [&] { return NADSF_Validate(config_pointer<NADSF_Config>(0)); }, [&] { return tfp_fov_validate(config_pointer<TfpFovConfig>(1)); });
  }
  uint32_t validate_fird() {
    for (int w = 0; w < 2; ++w) write_config(w);
    return both("validate_fird", take_probe(), [&] { return NADSF_ValidateFird(config_pointer<NADSF_Config>(0)); }, [&] { return tfp_fov_validate_fird(config_pointer<TfpFovConfig>(1)); });
  }
  // Bind to the request record `to_row` (plus `delta` bytes), or to a raw value when `absolute`.
  uint32_t bind(uint64_t g, int to_row, uint32_t to_weapon, int64_t delta = 0, bool absolute = false, uint64_t raw = 0) {
    char t[96]; std::snprintf(t, sizeof t, "bind(%llu, row %d%+lld, weapon %#x)", (unsigned long long)g, to_row, (long long)delta, to_weapon);
    auto address = [&](int w) { return absolute ? raw : W[w].a[A_CONTEXT] + row_offset(to_row) + delta; };
    both(t, take_probe(), [&] { return NADSF_Bind(g, address(0), to_weapon); }, [&] { return tfp_fov_bind(g, address(1), to_weapon); });
    if (result) { request_row = to_row; weapon = to_weapon; }
    return result;
  }
  uint32_t bind_fird(uint64_t g, int to_row, int64_t delta = 0, bool absolute = false, uint64_t raw = 0) {
    char t[96]; std::snprintf(t, sizeof t, "bind_fird(%llu, row %d%+lld)", (unsigned long long)g, to_row, (long long)delta);
    auto address = [&](int w) { return absolute ? raw : W[w].a[A_CONTEXT] + row_offset(to_row) + delta; };
    both(t, take_probe(), [&] { return NADSF_BindFird(g, address(0)); }, [&] { return tfp_fov_bind_fird(g, address(1)); });
    if (result) request_row = to_row;
    return result;
  }
  uint32_t transition(uint64_t g, uint64_t epoch, float progress, uint32_t ads) {
    char t[96]; std::snprintf(t, sizeof t, "set_transition(%llu, epoch %llu, progress %.9g, ads %u)", (unsigned long long)g, (unsigned long long)epoch, progress, ads);
    return both(t, take_probe(), [&] { return NADSF_SetTransition(g, epoch, progress, ads); }, [&] { return tfp_fov_set_transition(g, epoch, progress, ads); });
  }
  uint32_t end(uint64_t g) {
    char t[64]; std::snprintf(t, sizeof t, "end(%llu)", (unsigned long long)g);
    return both(t, take_probe(), [&] { return NADSF_End(g); }, [&] { return tfp_fov_end(g); });
  }
  // The statistics call with a generation and a header of the script's choosing (observe() uses the right ones).
  uint32_t read_stats(uint64_t g, uint32_t header_size = 88, uint32_t header_abi = 1) {
    for (int w = 0; w < 2; ++w) { std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS_SCRIPT), 0xA5, 0x100); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS_SCRIPT, header_size); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS_SCRIPT + 4, header_abi); }
    char t[64]; std::snprintf(t, sizeof t, "read_stats(%llu)", (unsigned long long)g);
    both(t, take_probe(), [&] { return NADSF_ReadStats(g, reinterpret_cast<NADSF_Stats *>(W[0].a[A_IO] + IO_STATS_SCRIPT)); },
         [&] { return tfp_fov_read_stats(g, reinterpret_cast<TfpFovStats *>(W[1].a[A_IO] + IO_STATS_SCRIPT)); });
    compare_bytes("stats (script's call)", W[0].a[A_IO] + IO_STATS_SCRIPT, W[1].a[A_IO] + IO_STATS_SCRIPT, 0x100, STATS_NAMES, 22);
    return result;
  }
  void stats_nowhere(uint64_t g) { both("read_stats(null)", false, [&] { return NADSF_ReadStats(g, nullptr); }, [&] { return tfp_fov_read_stats(g, nullptr); }); }
  // One call of the hook, as the game's call site makes it, with the lens the game computed.
  void frame(float native) {
    // Before any begin got as far as recording the game's setter, the original calls a null pointer.
    if (!::original.load()) { ++counted.skipped_frames; return; }
    ++frame_index;
    char t[200]; uint32_t bits; std::memcpy(&bits, &native, 4);
    std::snprintf(t, sizeof t, "frame at %.9g s, native lens %.9g [%08x]%s%s", fixture_now, native, bits, caller_ours ? "" : ", foreign caller", camera_argument_delta ? ", foreign camera" : "");
    const int wrong = caller_ours ? 0 : 1;
    both(t, take_probe(),
         [&] { shim_return_address = reinterpret_cast<void *>(W[0].a[A_GAME] + RETAIL.call + 6 + wrong);
               setter_hook(reinterpret_cast<void *>(W[0].a[A_CAMERA] + camera_argument_delta), native); return 0u; },
         [&] { tfp_fov_setter_from(W[1].a[A_GAME] + HERE.call + 6 + wrong, reinterpret_cast<void *>(W[1].a[A_CAMERA] + camera_argument_delta), native); return 0u; });
    ++counted.frames;
  }
  // What the scripts reached, read from the original's state after every call (for the scripts' self-checks only).
  struct Reached { uint64_t replaced = 0, ads = 0, immediate = 0, binds = 0, blending = 0, shared = 0, tween = 0, ledger = 0, cleanup_begins = 0, errors[20] = {}; } got;
  struct { uint64_t replaced, ads, immediate, binds; } before{};
  void reached() {
    auto step = [](uint64_t now_value, uint64_t &was, uint64_t &sum) { if (now_value > was) sum += now_value - was; was = now_value; };
    step(::replaced.load(), before.replaced, got.replaced); step(::ads_forwarded.load(), before.ads, got.ads);
    step(::immediate_applies.load(), before.immediate, got.immediate); step(::binds.load(), before.binds, got.binds);
    if (::policy.lens_blending) ++got.blending;
    if (::policy.shared_phase) ++got.shared;
    if (::policy.shoulder_weight > 0 && ::policy.shoulder_weight < 1) ++got.tween;
    if (::policy.input_lens.active) ++got.ledger;
    const uint32_t code = NADSF_Error(); if (code < 20) ++got.errors[code];
  }
  // The next call is the probed one: in the discovery run its reads are counted; otherwise the chosen read
  // fails (and heals, or stays unreadable to the end of the call), or the chosen value changes just before it.
  void probe() {
    probing = true;
    if (discovering) shared->probe_values = probe_values.size();
    if (probe_mode == INJECT_NONE) return;
    inject.mode = probe_mode; inject.index = probe_index;
    if (probe_mode == INJECT_VALUE) { inject.at = probe_values[probe_value].at; inject.value = probe_values[probe_value].value; }
  }
};

} // namespace dt
#include "diff_fov_helper_scripts.h"
