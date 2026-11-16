// Differential test: the retail camera helper (native/first_person/native_ads_camera.cpp, build 26568081,
// compiled natively over tests/winshim with its NADSC_FIXTURE seams) against its C translation
// (src/camera_helper.c, build 25480438). One logical scene is written into two fake game memories, one per
// build's addresses (tests/diff_camera_helper_world.h); both helpers are armed and driven with the same
// calls, and after EVERY call these are compared: the return value; every memory read, write, compare-and-swap,
// lock operation and forwarded setter call, in order (addresses by name, forwarded position bit for bit); the
// caller's argument buffer; what the dispatch slot holds; the error code; the stats, handoff stats, entry
// trace and anchor stats structures. The first difference is printed and the test exits non-zero.
// Every script runs in its own child process, so both helpers start from their start-up state.
// The retail tree is read, never written. See docs/CAMERA-HELPER-TRANSLATION.md, "Differential test".
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
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
#define NADSC_FIXTURE
#include "native_ads_camera.cpp"
#define _Static_assert static_assert
extern "C" {
#include "camera_helper.h"
}
#include "diff_camera_helper_world.h"

static_assert(sizeof(NADSC_Config) == sizeof(TfpCameraConfig) && sizeof(NADSC_Stats) == sizeof(TfpCameraStats) &&
              sizeof(NADSC_Handoff49Data) == sizeof(TfpCameraHandoffData) && sizeof(NADSC_EntryTraceData) == sizeof(TfpCameraEntryTraceData) &&
              sizeof(NADSC_AnchorData) == sizeof(TfpCameraAnchorData), "the two helpers' structures have one size");
static_assert(offsetof(NADSC_Config, camera_unit) == offsetof(TfpCameraConfig, camera_unit) && offsetof(NADSC_Config, max_gap_ms) == offsetof(TfpCameraConfig, max_gap_ms) &&
              offsetof(NADSC_Stats, resets) == offsetof(TfpCameraStats, resets) && offsetof(NADSC_Handoff49Data, rotation_start_gap) == offsetof(TfpCameraHandoffData, rotation_start_gap),
              "the two helpers' structures have one layout");

namespace dt {

// ---- what the children report to the parent --------------------------------------------------------------
struct Shared { uint64_t calls, frames, events, skipped_frames, translation_only, probe_reads, probe_values; };
static Shared *shared;
static struct { uint64_t calls, frames, events, skipped_frames, translation_only; } counted;

// ---- the two worlds and the record of what each helper did ------------------------------------------------
static World W[2]; // 0: retail memory, read by the original; 1: this build's memory, read by the translation
enum { EV_READ, EV_WRITE, EV_CAS, EV_LOCK, EV_FORWARD };
struct Event { uint32_t kind, id; uint64_t v[4]; };
static std::vector<Event> events[2];
static int reads_in_call[2];
static int lock_depth[3];
static std::string script_name, call_text;
static uint64_t call_index, frame_index;
static bool discovering;    // this child counts the probed call's reads for the parent

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
static Value bytes_value(uint64_t raw, int length = 4) { Value v; v.raw = raw; v.length = length; return v; }
static Value address_value(int id, uint64_t offset = 0) { Value v; v.kind = 1; v.sym = {id, offset}; v.length = 8; return v; }
enum : uint64_t { SLOT_NATIVE = 1, SLOT_HELPER = 2, SLOT_FOREIGN = 0x7770000 };
static Value slot_value(uint64_t which) { Value v; v.kind = 2; v.raw = which; v.length = 8; return v; }
static uint64_t slot_class(int w, uint64_t value) { return value == W[w].native_value ? SLOT_NATIVE : value == W[w].hook_value ? SLOT_HELPER : value; }
static uint64_t slot_raw(int w, uint64_t which) { return which == SLOT_NATIVE ? W[w].native_value : which == SLOT_HELPER ? W[w].hook_value : which; }
static void store(int w, Sym at, const Value &v) {
  uint64_t raw = v.kind == 1 ? W[w].at(v.sym) : v.kind == 2 ? slot_raw(w, v.raw) : v.raw;
  std::memcpy(reinterpret_cast<void *>(W[w].at(at)), &raw, static_cast<size_t>(v.length));
}
static bool safe_write(uint64_t at, const void *data, size_t length) {
  struct iovec local = {const_cast<void *>(data), length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_writev(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static void protect(uint64_t at, size_t length, int protection) {
  const uint64_t begin = at & ~4095ull, end = (at + length + 4095) & ~4095ull;
  mprotect(reinterpret_cast<void *>(begin), end - begin, protection);
}

// ---- one injected event for the next call: a read that fails, or a value that changes just before a read --
enum { INJECT_NONE, INJECT_FAIL, INJECT_FAIL_STAY, INJECT_VALUE };
static struct {
  int mode = INJECT_NONE, index = -1; bool active = false;
  Sym at{0, 0}; Value value;
  int effective[2] = {0, 0}; uint64_t kept_at[2] = {0, 0}; size_t kept_length[2] = {0, 0};
} inject;

static void on_read(int w, uint64_t at, size_t size, int after, bool complete) {
  const bool chosen = inject.active && reads_in_call[w] == inject.index;
  if (!after) {
    if (!chosen) return;
    if (inject.mode == INJECT_VALUE) { store(w, inject.at, inject.value); return; }
    // A read of the caller's own argument that stays unreadable ends in a write to that page by the helper
    // itself (both helpers); only the failure that heals is comparable there.
    inject.effective[w] = inject.mode == INJECT_FAIL_STAY && W[w].name(at).id == A_NATIVE ? INJECT_FAIL : inject.mode;
    protect(at, size, PROT_NONE);
    return;
  }
  if (chosen && inject.mode != INJECT_VALUE) {
    if (inject.effective[w] == INJECT_FAIL) protect(at, size, PROT_READ | PROT_WRITE);
    else { inject.kept_at[w] = at; inject.kept_length[w] = size; }
  }
  const Sym name = W[w].name(at);
  events[w].push_back({EV_READ, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0}});
  ++reads_in_call[w];
}
static void on_write(int w, uint64_t at, size_t size, bool complete) {
  const Sym name = W[w].name(at);
  events[w].push_back({EV_WRITE, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0}});
}
static void on_cas(int w, uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) {
  const Sym name = W[w].name(target);
  events[w].push_back({EV_CAS, static_cast<uint32_t>(name.id), {name.offset, slot_class(w, exchange), slot_class(w, comparand), slot_class(w, observed)}});
}
static void on_lock(int w, int lock, int operation) { events[w].push_back({EV_LOCK, static_cast<uint32_t>(lock), {static_cast<uint64_t>(operation), 0, 0, 0}}); }
// The game's own setter: recorded, and the camera node takes the position (as the retail fixture's does).
static void on_forward(int w, void *camera, uint32_t unit, const float *point) {
  Event e{EV_FORWARD, unit, {reinterpret_cast<uint64_t>(camera) - W[w].a[A_CAMERA], 0, 0, 0}};
  e.v[1] = point == nullptr ? 2 : reinterpret_cast<uint64_t>(point) == W[w].native() ? 0 : 1; // the game's own argument, or the helper's buffer
  if (point) std::memcpy(&e.v[2], point, 16);
  events[w].push_back(e);
  if (point && reinterpret_cast<uint64_t>(camera) == W[w].a[A_CAMERA]) {
    safe_write(W[w].a[A_CAM_LOCALS] + 0x24, point, 12);
    safe_write(W[w].a[A_CAM_WORLDS] + 0x30, point, 12);
  }
}
static std::string describe(const Event &e) {
  char text[256];
  auto place = [&](char *out, size_t n, uint64_t offset) {
    if (e.id == 0xffff) std::snprintf(out, n, "address %#llx", static_cast<unsigned long long>(offset));
    else std::snprintf(out, n, "%s+%#llx", A_NAMES[e.id], static_cast<unsigned long long>(offset));
  };
  char where[96];
  switch (e.kind) {
  case EV_READ: place(where, sizeof where, e.v[0]); std::snprintf(text, sizeof text, "read %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_WRITE: place(where, sizeof where, e.v[0]); std::snprintf(text, sizeof text, "write %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_CAS: place(where, sizeof where, e.v[0]); std::snprintf(text, sizeof text, "compare-and-swap at %s: store %#llx if %#llx, found %#llx (1 = game's setter, 2 = helper)", where,
                                                                (unsigned long long)e.v[1], (unsigned long long)e.v[2], (unsigned long long)e.v[3]); break;
  case EV_LOCK: std::snprintf(text, sizeof text, "%s lock %s%s", e.id ? "policy" : "control", e.v[0] & 1 ? "released" : "taken", e.v[0] >= 2 ? " (shared)" : ""); break;
  default: {
    float p[4]; std::memcpy(p, &e.v[2], 16);
    std::snprintf(text, sizeof text, "forward to the game's setter: camera+%#llx, unit %#x, %s, position %.9g %.9g %.9g %.9g [%016llx %016llx]", (unsigned long long)e.v[0], e.id,
                  e.v[1] == 0 ? "the game's own argument" : e.v[1] == 1 ? "the helper's buffer" : "null", p[0], p[1], p[2], p[3], (unsigned long long)e.v[2], (unsigned long long)e.v[3]);
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
                                          "native6_forwarded", "native6_forwarded", "resets", "resets", "blend", "head_distance", "last_dt", "padding"};
static const char *const HANDOFF_NAMES[] = {"size", "rotation_proven", "entry_ms", "rotation_ms", "entry_started", "entry_started", "entry_frames", "entry_frames",
                                            "entry_rejected", "entry_rejected", "rotation_started", "rotation_started", "rotation_frames", "rotation_frames",
                                            "rotation_rejected", "rotation_rejected", "proof_mismatches", "proof_mismatches", "native_mismatches", "native_mismatches",
                                            "native_proven", "entry_last_delay", "entry_max_delay", "padding", "sample_reasons[0]", "sample_reasons[1]", "sample_reasons[2]",
                                            "sample_reasons[3]", "sample_reasons[4]", "sample_reasons[5]", "sample_reasons[6]", "sample_reasons[7]", "pending_paths[0]",
                                            "pending_paths[1]", "pending_paths[2]", "pending_paths[3]", "rotation_start_gap", "padding2"};
static const char *const TRACE_NAMES[] = {"size", "count", "exit_holds", "hold_exit", "row0.frame", "row0.linked", "row0.rig_known", "row0.padding", "row0.argument", "row0.argument",
                                          "row0.argument", "row0.camera_world", "row0.camera_world", "row0.camera_world", "row0.camera_local", "row0.camera_local",
                                          "row0.camera_local", "row0.rig_world", "row0.rig_world", "row0.rig_world", "row0.head_world", "row0.head_world", "row0.head_world", "row0.padding2"};
static const char *const ANCHOR_NAMES[] = {"size", "enabled", "known", "frames", "current[0]", "current[1]", "current[2]", "padding"};

// ---- the translation's host: the same facts the original gets from the stand-in Windows calls ------------
static uint32_t translation_cas_calls = 0;
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
static double host_now(void *) { return fixture_time; }
static bool host_writable_slot(void *, uint64_t at, uint64_t exe) {
  ++counted.translation_only;
  if (exe != W[1].a[A_EXE]) fail("the translation asked whether the slot is writable in image %#llx, not in the executable", static_cast<unsigned long long>(exe));
  return ::writable_slot(at, 0); // the original's fixture answer: committed, writable, private (and fixture_slot_readonly)
}
static bool host_executable(void *, uint64_t at) {
  if (at == W[1].a[A_EXE] + HERE.graph_getter) { ++counted.translation_only; return answer_getter_executable; }
  // The original (fixture build) asks this of the function standing in for the game's setter.
  if (at == W[1].a[A_EXE] + HERE.setter) return ::executable(W[0].native_value);
  fail("the translation asked whether an unexpected address is executable: %#llx", static_cast<unsigned long long>(at));
}
static bool host_cas(void *, uint64_t slot_address, uint64_t expected, uint64_t desired, uint64_t *observed) {
  if (slot_address != W[1].slot()) fail("the translation swaps at %#llx, not at the dispatch slot", static_cast<unsigned long long>(slot_address));
  // The original's fixture seams (cpp:454-460), on the translation's own call count.
  if (++translation_cas_calls == fixture_cas_fail_at) return false;
  uint64_t *target = reinterpret_cast<uint64_t *>(slot_address);
  if (translation_cas_calls == fixture_foreign_cas_at) __sync_lock_test_and_set(target, fixture_foreign_value);
  *observed = __sync_val_compare_and_swap(target, expected, desired);
  on_cas(1, slot_address, desired, expected, *observed);
  if (fixture_mutate_after_cas) std::memcpy(reinterpret_cast<void *>(W[1].at(W[0].name(fixture_mutate_after_cas))), &fixture_mutation_value, 4);
  return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) {
  const bool complete = safe_write(reinterpret_cast<uint64_t>(out), data, length);
  on_write(1, reinterpret_cast<uint64_t>(out), length, complete);
  return complete;
}
static bool host_pin(void *, uint64_t address) {
  if (address == reinterpret_cast<uint64_t>(&tfp_camera_begin)) return !shim_pin_fails;
  if (address == W[1].a[A_EXE]) { ++counted.translation_only; return answer_pin_executable; }
  fail("the translation pins an unexpected address: %#llx", static_cast<unsigned long long>(address));
}
static void host_lock(int lock, int operation, int slot, int change) {
  lock_depth[slot] += change;
  if (lock_depth[slot] < 0 || lock_depth[lock] > 1 || (lock_depth[2] && lock_depth[TFP_CAMERA_LOCK_POLICY]))
    fail("the translation's locks are unbalanced (control %d, policy %d, policy shared %d)", lock_depth[0], lock_depth[1], lock_depth[2]);
  on_lock(1, lock, operation);
}
static void host_lock_exclusive(void *, int lock) { host_lock(lock, 0, lock, 1); }
static void host_unlock_exclusive(void *, int lock) { host_lock(lock, 1, lock, -1); }
static void host_lock_shared(void *, int lock) { if (lock != TFP_CAMERA_LOCK_POLICY) fail("shared lock %d", lock); host_lock(lock, 2, 2, 1); }
static void host_unlock_shared(void *, int lock) { host_lock(lock, 3, 2, -1); }
static void host_forward(void *, uint64_t original_setter, void *camera, uint32_t unit, const float *native) {
  if (expect_null_original) {
    if (original_setter != 0) fail("the translation still knows an original setter (%#llx) after its reset", static_cast<unsigned long long>(original_setter));
    on_forward(1, camera, unit, native); return;
  }
  if (original_setter != W[1].native_value) fail("the translation forwards to %#llx, not to the game's setter", static_cast<unsigned long long>(original_setter));
  on_forward(1, camera, unit, native);
}
static void retail_setter(void *camera, uint32_t unit, const float *native) { on_forward(0, camera, unit, native); }

static void build() {
  W[0].build(RETAIL); W[1].build(HERE);
  W[0].native_value = reinterpret_cast<uint64_t>(&retail_setter); W[0].hook_value = reinterpret_cast<uint64_t>(&setter_hook);
  W[1].native_value = W[1].a[A_EXE] + HERE.setter; W[1].hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].slot(), W[w].native_value);
  fixture_target = retail_setter;
  shim_on_read = [](uint64_t at, size_t size, int after, bool complete) { on_read(0, at, size, after, complete); };
  shim_on_write = [](uint64_t at, size_t size, bool complete) { on_write(0, at, size, complete); };
  shim_on_cas = [](uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) { on_cas(0, target, exchange, comparand, observed); };
  shim_on_lock = [](void *lock, int operation) { on_lock(0, lock == &control_lock ? 0 : 1, operation); };
  TfpCameraHost host{};
  host.hook = W[1].hook_value; host.read = host_read; host.readable_span = host_readable_span; host.now = host_now;
  host.writable_slot = host_writable_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write;
  host.pin = host_pin; host.lock_exclusive = host_lock_exclusive; host.unlock_exclusive = host_unlock_exclusive;
  host.lock_shared = host_lock_shared; host.unlock_shared = host_unlock_shared; host.forward = host_forward;
  tfp_camera_reset_for_tests();
  tfp_camera_set_host(&host);
}

// ---- a run: the scene, the calls, and the comparison after each -------------------------------------------
struct Poke { Sym at; Value value; };
struct Run {
  Scene s;
  std::vector<Poke> pokes;
  // config (logical; the pointers are each world's own)
  uint32_t size = 88, abi = 1, index = 0, avatar = 0x333, body_unit = 0x400003, camera_unit = 0x400004, max_gap = 250, reserved_field = 0;
  float forward = .18f, up = .04f, smoothing = 100, blend = 120;
  int64_t context_delta = 0, camera_delta = 0, owner_delta = 0, game_delta = 0, exe_delta = 0;
  uint64_t context_absolute = 0, camera_absolute = 0; // nonzero: this value instead of the world's own address
  bool config_null = false, generation_null = false;
  // the hook's arguments
  bool caller_ours = true, argument_null = false; int64_t camera_argument_delta = 0; uint32_t unit_argument_xor = 0;
  uint64_t gen = 0, previous_gen = 0; uint32_t result = 0;
  // probes
  int probe_mode = INJECT_NONE, probe_index = -1, probe_value = 0;
  std::vector<Poke> probe_values;
  // posing
  float right[3] = {1, 0, 0}, ahead[3] = {0, 1, 0}, above[3] = {0, 0, 1};

  void sync() {
    for (int w = 0; w < 2; ++w) { W[w].apply(s, index); for (const Poke &p : pokes) store(w, p.at, p.value); }
  }
  void poke(int id, uint64_t offset, Value v) { pokes.push_back({{id, offset}, v}); }

  void observe() {
    const uint32_t retail_error = NADSC_Error(), translation_error = tfp_camera_error();
    if (retail_error != translation_error) fail("error code: retail %u, translation %u", retail_error, translation_error);
    const bool held = ::reserved.load() != 0;
    const uint64_t g = held ? ::owned_generation : 0;
    for (int w = 0; w < 2; ++w) { events[w].clear(); std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS), 0x5A, 0x600); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS, 80); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS + 4, 1); }
    uint32_t a = NADSC_ReadStats(g, reinterpret_cast<NADSC_Stats *>(W[0].a[A_IO] + IO_STATS));
    uint32_t b = tfp_camera_read_stats(g, reinterpret_cast<TfpCameraStats *>(W[1].a[A_IO] + IO_STATS));
    if (a != b) fail("read_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
    compare_bytes("stats", W[0].a[A_IO] + IO_STATS, W[1].a[A_IO] + IO_STATS, 80, STATS_NAMES, 20);
    if (held) {
      a = NADSC_HandoffStats49(g, reinterpret_cast<NADSC_Handoff49Data *>(W[0].a[A_IO] + IO_HANDOFF));
      b = tfp_camera_handoff_stats(g, reinterpret_cast<TfpCameraHandoffData *>(W[1].a[A_IO] + IO_HANDOFF));
      if (a != b) fail("handoff_stats returned %u in retail, %u in the translation", a, b);
      compare_bytes("handoff stats", W[0].a[A_IO] + IO_HANDOFF, W[1].a[A_IO] + IO_HANDOFF, 152, HANDOFF_NAMES, 38);
    }
    a = NADSC_EntryTrace50(reinterpret_cast<NADSC_EntryTraceData *>(W[0].a[A_IO] + IO_TRACE));
    b = tfp_camera_entry_trace(reinterpret_cast<TfpCameraEntryTraceData *>(W[1].a[A_IO] + IO_TRACE));
    if (a != b) fail("entry_trace returned %u in retail, %u in the translation", a, b);
    compare_bytes("entry trace", W[0].a[A_IO] + IO_TRACE, W[1].a[A_IO] + IO_TRACE, sizeof(NADSC_EntryTraceData), TRACE_NAMES, 24);
    a = NADSC_AnchorStats51(reinterpret_cast<NADSC_AnchorData *>(W[0].a[A_IO] + IO_ANCHOR));
    b = tfp_camera_anchor_stats(reinterpret_cast<TfpCameraAnchorData *>(W[1].a[A_IO] + IO_ANCHOR));
    if (a != b) fail("anchor_stats returned %u in retail, %u in the translation", a, b);
    compare_bytes("anchor stats", W[0].a[A_IO] + IO_ANCHOR, W[1].a[A_IO] + IO_ANCHOR, sizeof(NADSC_AnchorData), ANCHOR_NAMES, 8);
    compare_events("while reading the statistics");
    if (NADSC_Error() != retail_error || tfp_camera_error() != translation_error) fail("reading the statistics changed an error code");
  }
  void finish(int w) {
    if (inject.kept_length[w]) { protect(inject.kept_at[w], inject.kept_length[w], PROT_READ | PROT_WRITE); inject.kept_length[w] = 0; }
  }
  template <class Retail, class Translation> uint32_t both(const char *text, bool probe_here, Retail retail, Translation translation) {
    ++call_index; call_text = text;
    for (int w = 0; w < 2; ++w) { events[w].clear(); reads_in_call[w] = 0; }
    inject.active = probe_here && inject.mode != INJECT_NONE;
    const uint32_t a = retail(); finish(0);
    const uint32_t b = translation(); finish(1);
    inject.active = false;
    if (probe_here) { if (discovering) shared->probe_reads = static_cast<uint64_t>(reads_in_call[0]); inject.mode = INJECT_NONE; }
    if (a != b) fail("return value: retail %u, translation %u", a, b);
    compare_events("during the call");
    compare_bytes("the caller's argument buffer (position, then rotation at +64)", W[0].native(), W[1].native(), 96);
    uint64_t slots[2]; for (int w = 0; w < 2; ++w) std::memcpy(&slots[w], reinterpret_cast<void *>(W[w].slot()), 8);
    if (slot_class(0, slots[0]) != slot_class(1, slots[1]))
      fail("dispatch slot (1 = game's setter, 2 = helper): retail %#llx, translation %#llx", (unsigned long long)slot_class(0, slots[0]), (unsigned long long)slot_class(1, slots[1]));
    if (lock_depth[0] || lock_depth[1] || lock_depth[2]) fail("the translation returned holding a lock (control %d, policy %d, shared %d)", lock_depth[0], lock_depth[1], lock_depth[2]);
    observe();
    ++counted.calls;
    return result = a;
  }
  bool take_probe() { const bool here = probing; probing = false; return here; }
  bool probing = false;

  void write_config(int w) {
    NADSC_Config c{};
    c.size = size; c.abi = abi; c.game = W[w].a[A_GAME] + game_delta; c.exe = W[w].a[A_EXE] + exe_delta;
    c.context = W[w].a[A_CONTEXT] + context_delta; c.camera = W[w].a[A_CAMERA] + camera_delta; c.avatar_owner = W[w].a[A_OWNER] + owner_delta;
    if (context_absolute) c.context = context_absolute;
    if (camera_absolute) c.camera = camera_absolute;
    c.avatar_index = index; c.avatar = avatar; c.body = body_unit; c.camera_unit = camera_unit;
    c.forward = forward; c.up = up; c.smoothing_ms = smoothing; c.blend_ms = blend; c.max_gap_ms = max_gap; c.reserved = reserved_field;
    World::put(W[w].a[A_IO] + IO_CONFIG, c);
  }
  uint32_t begin() {
    sync();
    for (int w = 0; w < 2; ++w) { write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
    both("begin", take_probe(),
         [&] { return NADSC_Begin(config_null ? nullptr : reinterpret_cast<NADSC_Config *>(W[0].a[A_IO] + IO_CONFIG), generation_null ? nullptr : reinterpret_cast<uint64_t *>(W[0].a[A_IO] + IO_GENERATION)); },
         [&] { return tfp_camera_begin(config_null ? nullptr : reinterpret_cast<TfpCameraConfig *>(W[1].a[A_IO] + IO_CONFIG), generation_null ? nullptr : reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION)); });
    uint64_t g[2]; for (int w = 0; w < 2; ++w) std::memcpy(&g[w], reinterpret_cast<void *>(W[w].a[A_IO] + IO_GENERATION), 8);
    if (g[0] != g[1]) fail("generation written by begin: retail %llu, translation %llu", (unsigned long long)g[0], (unsigned long long)g[1]);
    if (result && g[0] && g[0] != UINT64_MAX) { previous_gen = gen; gen = g[0]; }
    return result;
  }
  uint32_t validate() {
    sync(); for (int w = 0; w < 2; ++w) write_config(w);
    return both("validate", take_probe(), [&] { return NADSC_Validate(config_null ? nullptr : reinterpret_cast<NADSC_Config *>(W[0].a[A_IO] + IO_CONFIG)); },
                [&] { return tfp_camera_validate(config_null ? nullptr : reinterpret_cast<TfpCameraConfig *>(W[1].a[A_IO] + IO_CONFIG)); });
  }
  uint32_t end(uint64_t g) { sync(); char t[64]; std::snprintf(t, sizeof t, "end(%llu)", (unsigned long long)g); return both(t, take_probe(), [&] { return NADSC_End(g); }, [&] { return tfp_camera_end(g); }); }
  uint32_t presentation(uint64_t g) { sync(); char t[64]; std::snprintf(t, sizeof t, "set_presentation(%llu)", (unsigned long long)g); return both(t, take_probe(), [&] { return NADSC_SetPresentation47(g); }, [&] { return tfp_camera_set_presentation(g); }); }
  uint32_t cut_hold(uint64_t g, uint32_t mode) { sync(); char t[64]; std::snprintf(t, sizeof t, "set_cut_hold(%llu, %u)", (unsigned long long)g, mode); return both(t, take_probe(), [&] { return NADSC_SetCutHold50(g, mode); }, [&] { return tfp_camera_set_cut_hold(g, mode); }); }
  uint32_t handoff(uint64_t g, float entry, float rotation) { sync(); char t[96]; std::snprintf(t, sizeof t, "set_handoff(%llu, %g, %g)", (unsigned long long)g, entry, rotation); return both(t, take_probe(), [&] { return NADSC_SetHandoff49(g, entry, rotation); }, [&] { return tfp_camera_set_handoff(g, entry, rotation); }); }
  uint32_t weapon_key(uint64_t g, uint32_t key) { sync(); char t[64]; std::snprintf(t, sizeof t, "set_weapon_key(%llu, %#x)", (unsigned long long)g, key); return both(t, take_probe(), [&] { return NADSC_SetWeaponKey50(g, key); }, [&] { return tfp_camera_set_weapon_key(g, key); }); }
  // The two statistics calls with a generation of the script's choosing (observe() uses the right one).
  uint32_t read_stats(uint64_t g, bool bad_header = false) {
    for (int w = 0; w < 2; ++w) { std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + 0x800), 0, 0x100); World::put<uint32_t>(W[w].a[A_IO] + 0x800, bad_header ? 79 : 80); World::put<uint32_t>(W[w].a[A_IO] + 0x804, 1); }
    char t[64]; std::snprintf(t, sizeof t, "read_stats(%llu)", (unsigned long long)g);
    both(t, take_probe(), [&] { return NADSC_ReadStats(g, reinterpret_cast<NADSC_Stats *>(W[0].a[A_IO] + 0x800)); }, [&] { return tfp_camera_read_stats(g, reinterpret_cast<TfpCameraStats *>(W[1].a[A_IO] + 0x800)); });
    compare_bytes("stats (script's call)", W[0].a[A_IO] + 0x800, W[1].a[A_IO] + 0x800, 80, STATS_NAMES, 20);
    return result;
  }
  uint32_t handoff_stats(uint64_t g) {
    for (int w = 0; w < 2; ++w) std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + 0x900), 0, 0x100);
    char t[64]; std::snprintf(t, sizeof t, "handoff_stats(%llu)", (unsigned long long)g);
    both(t, take_probe(), [&] { return NADSC_HandoffStats49(g, reinterpret_cast<NADSC_Handoff49Data *>(W[0].a[A_IO] + 0x900)); }, [&] { return tfp_camera_handoff_stats(g, reinterpret_cast<TfpCameraHandoffData *>(W[1].a[A_IO] + 0x900)); });
    compare_bytes("handoff stats (script's call)", W[0].a[A_IO] + 0x900, W[1].a[A_IO] + 0x900, 152, HANDOFF_NAMES, 38);
    return result;
  }
  // Statistics into a pointer that cannot be written.
  void stats_nowhere(uint64_t g) {
    both("read_stats(null)", false, [&] { return NADSC_ReadStats(g, nullptr); }, [&] { return tfp_camera_read_stats(g, nullptr); });
    both("handoff_stats(null)", false, [&] { return NADSC_HandoffStats49(g, nullptr); }, [&] { return tfp_camera_handoff_stats(g, nullptr); });
    both("entry_trace(null)", false, [&] { return NADSC_EntryTrace50(nullptr); }, [&] { return tfp_camera_entry_trace(nullptr); });
    both("anchor_stats(null)", false, [&] { return NADSC_AnchorStats51(nullptr); }, [&] { return tfp_camera_anchor_stats(nullptr); });
  }
  // One call of the hook, as the game's call site makes it.
  void frame(double dt) {
    fixture_time += dt;
    // Before any begin got as far as recording the game's setter, the original calls a null pointer.
    if (!::original.load()) { ++counted.skipped_frames; return; }
    sync(); ++frame_index;
    char t[200];
    std::snprintf(t, sizeof t, "frame at %.4f ms (step %.4f): request kind %u, mask %#x, aim %u, guard %u, linked %d, state %u", fixture_time, dt, s.request[0] & 0xffff, s.mask, s.aim, s.guard, s.linked ? 1 : 0, s.state);
    const int wrong = caller_ours ? 0 : 1;
    both(t, take_probe(),
         [&] { shim_return_address = reinterpret_cast<void *>(W[0].a[A_GAME] + RETAIL.call + 4 + wrong);
               setter_hook(reinterpret_cast<void *>(W[0].a[A_CAMERA] + camera_argument_delta), camera_unit ^ unit_argument_xor, argument_null ? nullptr : reinterpret_cast<const float *>(W[0].native())); return 0u; },
         [&] { tfp_camera_setter_from(W[1].a[A_GAME] + HERE.call + 4 + wrong, reinterpret_cast<void *>(W[1].a[A_CAMERA] + camera_argument_delta), camera_unit ^ unit_argument_xor,
                                      argument_null ? nullptr : reinterpret_cast<const float *>(W[1].native())); return 0u; });
    ++counted.frames;
    // The game's call pair goes on: the rotation at argument+64 (which the helper may have rewritten) goes to
    // the rotation setter, and the camera's world rotation becomes it (rows = the rotated axes).
    if (!argument_null && camera_argument_delta == 0) for (int w = 0; w < 2; ++w) {
      FreeAim::Q q{}; std::memcpy(&q, reinterpret_cast<void *>(W[w].native() + 64), 16);
      if (!FreeAim::valid(q)) continue;
      float rows[12] = {}; const float axes[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
      for (int axis = 0; axis < 3; ++axis) FreeAim::rotate(q, axes[axis], rows + axis * 4);
      World::put(W[w].a[A_CAM_WORLDS], rows);
    }
    reached();
  }
  // What the scripts reached, summed over leases (read from the original's state; for the scripts' self-checks only).
  struct Reached { uint64_t replaced = 0, native = 0, eye_hold = 0, glides = 0, anchor = 0, learned = 0, exit_holds = 0, seated = 0, grace = 0, correction = 0,
                   entry_frames = 0, turn_frames = 0, entry_started = 0, turn_started = 0, pending = 0, errors[19] = {}; } got;
  struct { uint64_t replaced, native; uint32_t eye_hold, glides, anchor, learned, exit_holds, seated, grace; uint64_t entry_frames, turn_frames, entry_started, turn_started; uint32_t pending; } before{};
  void reached() {
    auto step = [](uint64_t now_value, uint64_t &was, uint64_t &sum) { if (now_value > was) sum += now_value - was; was = now_value; };
    auto step32 = [](uint32_t now_value, uint32_t &was, uint64_t &sum) { if (now_value > was) sum += now_value - was; was = now_value; };
    step(::replaced.load(), before.replaced, got.replaced); step(::native6_forwarded.load(), before.native, got.native);
    step32(::policy.eye_hold_frames, before.eye_hold, got.eye_hold); step32(::policy.eye_glides, before.glides, got.glides);
    step32(::policy.ads_gap_frames, before.anchor, got.anchor); step32(::policy.ads_learned, before.learned, got.learned);
    step32(::policy.exit_holds, before.exit_holds, got.exit_holds); step32(::policy.seated_frames, before.seated, got.seated);
    step32(::policy.guard_grace_frames, before.grace, got.grace);
    step(::policy.entry_frames, before.entry_frames, got.entry_frames); step(::policy.turn_frames, before.turn_frames, got.turn_frames);
    step(::policy.entry_started, before.entry_started, got.entry_started); step(::policy.turn_started, before.turn_started, got.turn_started);
    step32(::policy.pending_paths[0] + ::policy.pending_paths[1], before.pending, got.pending);
    if (::policy.correction.active) ++got.correction;
    const uint32_t code = NADSC_Error(); if (code < 19) ++got.errors[code];
  }
  void frames(int count, double dt) { for (int i = 0; i < count; ++i) frame(dt); }
  // The next call is the probed one: in the discovery run its reads are counted; otherwise the chosen read
  // fails (and heals, or stays unreadable to the end of the call), or the chosen value changes just before it.
  void probe() {
    probing = true;
    if (discovering) shared->probe_values = probe_values.size();
    if (probe_mode == INJECT_NONE) return;
    inject.mode = probe_mode; inject.index = probe_index;
    if (probe_mode == INJECT_VALUE) { inject.at = probe_values[probe_value].at; inject.value = probe_values[probe_value].value; }
  }

  // ---- posing helpers ----
  // The body root at (x, y, z) turned by yaw; the head `head_height` above it and leaning (right, ahead) of it.
  void pose(float x, float y, float z, float yaw, float head_height = 1.5f, float lean_right = .03f, float lean_ahead = .06f) {
    const float c = std::cos(yaw), n = std::sin(yaw);
    const float m[16] = {c, n, 0, 0, -n, c, 0, 0, 0, 0, 1, 0, x, y, z, 1};
    std::memcpy(s.root, m, sizeof m);
    s.head[0] = x + lean_right * c - lean_ahead * n; s.head[1] = y + lean_right * n + lean_ahead * c; s.head[2] = z + head_height;
    const float local[3] = {.25f, .36f, 1.09f};
    for (int i = 0; i < 3; ++i) s.node42[i] = s.root[12 + i] + local[0] * s.root[i] + local[1] * s.root[4 + i] + local[2] * s.root[8 + i];
  }
  void look(float yaw, float pitch) {
    const float c = std::cos(yaw), n = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    right[0] = c; right[1] = n; right[2] = 0; ahead[0] = -n * cp; ahead[1] = c * cp; ahead[2] = sp; above[0] = n * sp; above[1] = -c * sp; above[2] = cp;
    for (int i = 0; i < 3; ++i) { s.context[i] = right[i]; s.context[4 + i] = ahead[i]; s.context[8 + i] = above[i]; s.camera_rotation[i] = right[i]; s.camera_rotation[4 + i] = ahead[i]; s.camera_rotation[8 + i] = above[i]; }
  }
  void rotation_from_look() {
    float m[16] = {}; for (int i = 0; i < 3; ++i) { m[i] = right[i]; m[4 + i] = ahead[i]; m[8 + i] = above[i]; } m[15] = 1;
    FreeAim::Q q{}; if (!FreeAim::matrix_q(m, q)) q = FreeAim::Q{};
    std::memcpy(s.argument_rotation, &q, 16);
  }
  // The normal (third-person) camera request: the game's camera sits behind and above the head.
  void hip(uint32_t mask = 0) {
    s.request[0] = 4; s.linked = false; s.mask = mask; s.aim = 0; s.camera_rotation_given = false;
    for (int i = 0; i < 3; ++i) s.argument[i] = s.head[i] - ahead[i] * 2.0f + above[i] * .4f;
    s.argument[3] = 0; rotation_from_look();
    for (int w = 0; w < 2; ++w) { s.weapon[w].parent = 1; for (int i = 0; i < 3; ++i) s.weapon[w].position[i] = s.node42[i]; }
  }
  // The game's own first-person request: camera linked to the rig, parent-local argument; the rig node sits
  // `eye` (right, ahead, above) from the head, and each weapon rigidly `grip[w]` from the rig node.
  float eye[3] = {0, .1f, .05f};
  float grip[2][3] = {{0, .22f, -.22f}, {.1f, .3f, -.15f}};
  void native(bool aim = false, bool linked = true) {
    s.request[0] = 6; s.linked = linked; s.mask = 0; s.aim = aim ? 1 : 0; s.camera_rotation_given = true;
    for (int i = 0; i < 3; ++i) { s.rig[i] = right[i]; s.rig[4 + i] = ahead[i]; s.rig[8 + i] = above[i]; s.rig[12 + i] = s.head[i] + right[i] * eye[0] + ahead[i] * eye[1] + above[i] * eye[2]; }
    s.rig[3] = s.rig[7] = s.rig[11] = 0; s.rig[15] = 1;
    s.argument[0] = .02f; s.argument[1] = .01f; s.argument[2] = .03f; s.argument[3] = 0; rotation_from_look();
    for (int w = 0; w < 2; ++w) { s.weapon[w].parent = 2; for (int i = 0; i < 3; ++i) s.weapon[w].position[i] = s.rig[12 + i] + right[i] * grip[w][0] + ahead[i] * grip[w][1] + above[i] * grip[w][2]; }
  }
  void arm(uint32_t mode = 15) {
    begin(); if (!result) fail("the script could not arm (begin returned 0, error %u)", NADSC_Error());
    presentation(gen); cut_hold(gen, mode);
  }
};

} // namespace dt
#include "diff_camera_helper_scripts.h"
