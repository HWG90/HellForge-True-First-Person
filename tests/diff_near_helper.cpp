// Differential test: the retail near-clip helper (native/near_clip/native_ads_near.cpp, build 26568081,
// compiled natively over tests/winshim with its NADSN_FIXTURE seams) against its C translation
// (src/near_helper.c, build 25480438). One logical state is written into two fake game memories, one per
// build's addresses (tests/diff_near_helper_world.h); both helpers are driven with the same calls, and after
// EVERY call these are compared: the return value; every memory read, write, compare-and-swap, lock operation
// and forwarded setter call, in order (addresses by name, the forwarded distance bit for bit); what the
// dispatch slot holds; the near distance of both cameras; the generation written; the error code; the stats
// structure and the caller table. The first difference is printed and the test exits non-zero.
// Every script runs in its own child process, so both helpers start from their start-up state.
// The retail tree is read, never written. See docs/translation/NEAR-HELPER-TRANSLATION.md, "Differential test".
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
#define NADSN_FIXTURE
#include "native_ads_near.cpp"
#define _Static_assert static_assert
extern "C" {
#include "near_helper.h"
}
#include "diff_near_helper_world.h"

static_assert(sizeof(NADSN_Config) == sizeof(TfpNearConfig) && sizeof(NADSN_Stats) == sizeof(TfpNearStats) && sizeof(NADSN_Site) == sizeof(TfpNearSite),
              "the two helpers' structures have one size");
static_assert(offsetof(NADSN_Config, avatar_index) == offsetof(TfpNearConfig, avatar_index) && offsetof(NADSN_Config, reserved2) == offsetof(TfpNearConfig, reserved2) &&
              offsetof(NADSN_Config, hip) == offsetof(TfpNearConfig, hip) && offsetof(NADSN_Stats, other_kind) == offsetof(TfpNearStats, other_kind) &&
              offsetof(NADSN_Stats, last_native) == offsetof(TfpNearStats, last_native) && offsetof(NADSN_Stats, aim) == offsetof(TfpNearStats, aim) &&
              offsetof(NADSN_Site, last_value) == offsetof(TfpNearSite, last_value), "the two helpers' structures have one layout");

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
static int reads_in_call[2], cas_in_call[2], writes_in_call[2];
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
enum : uint64_t { SLOT_NATIVE = 1, SLOT_HELPER = 2, SLOT_NATIVE2 = 3, SLOT_FOREIGN = 0x7770000 };
static Value slot_value(uint64_t which) { Value v; v.kind = 2; v.raw = which; v.length = 8; return v; }
static uint64_t slot_class(int w, uint64_t value) {
  return value == W[w].native_value ? SLOT_NATIVE : value == W[w].hook_value ? SLOT_HELPER : value == W[w].native2_value ? SLOT_NATIVE2 : value;
}
static uint64_t slot_raw(int w, uint64_t which) {
  return which == SLOT_NATIVE ? W[w].native_value : which == SLOT_HELPER ? W[w].hook_value : which == SLOT_NATIVE2 ? W[w].native2_value : which;
}
struct Poke { Sym at; Value value; };
static bool safe_write(uint64_t at, const void *data, size_t length) {
  struct iovec local = {const_cast<void *>(data), length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_writev(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static void store(int w, Sym at, const Value &v) {
  uint64_t raw = v.kind == 1 ? W[w].at(v.sym) : v.kind == 2 ? slot_raw(w, v.raw) : v.raw;
  safe_write(W[w].at(at), &raw, static_cast<size_t>(v.length));
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
  uint64_t kept_at[2] = {0, 0}; size_t kept_length[2] = {0, 0};
} inject;
// Seams of the test itself, armed for one call, applied identically to both helpers:
static std::vector<Poke> after_cas;       // written right after the call's first compare-and-swap
static bool generation_locks_after_write; // the generation page turns read-only after the call's first write to it
static bool readonly_after_policy_lock;     // ... from the call's first exclusive take of the policy lock on
static bool readonly_after_cas;            // the slot's page answers "not writable" from the call's first compare-and-swap on

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
  if (generation_locks_after_write && name.id == A_GEN && writes_in_call[w]++ == 0) protect(W[w].a[A_GEN], 8, PROT_READ);
}
static void on_cas(int w, uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) {
  const Sym name = W[w].name(target);
  events[w].push_back({EV_CAS, static_cast<uint32_t>(name.id), {name.offset, slot_class(w, exchange), slot_class(w, comparand), slot_class(w, observed)}});
  if (cas_in_call[w]++ == 0) { for (const Poke &p : after_cas) store(w, p.at, p.value); if (readonly_after_cas) fixture_slot_readonly = true; }
}
static void on_lock(int w, int lock, int operation) {
  events[w].push_back({EV_LOCK, static_cast<uint32_t>(lock), {static_cast<uint64_t>(operation), 0, 0, 0}});
  if (readonly_after_policy_lock && lock == 1 && operation == 0) fixture_slot_readonly = true;
}
// The game's own setter: recorded, and the camera takes the distance at +0x28 (as the game's does).
// It also looks at the caller table as it is at that moment (the table call takes no lock), so the order of
// "note the caller" and "forward" is compared too.
static float last_forwarded[2];
static void on_forward(int w, void *camera, float meters) {
  const Sym name = W[w].name(reinterpret_cast<uint64_t>(camera));
  Event e{EV_FORWARD, static_cast<uint32_t>(name.id), {name.offset, 0, 0, 0}};
  std::memcpy(&e.v[1], &meters, 4);
  const uint64_t table = W[w].a[A_IO] + IO_FORWARD_SITES;
  const uint32_t rows = w ? tfp_near_read_sites(reinterpret_cast<TfpNearSite *>(table), 16) : NADSN_ReadSites(reinterpret_cast<NADSN_Site *>(table), 16);
  e.v[2] = rows;
  for (uint32_t i = 0; i < rows && i < 16; ++i) {
    NADSN_Site row; std::memcpy(&row, reinterpret_cast<void *>(table + i * 32), 32);
    uint32_t bits; std::memcpy(&bits, &row.last_value, 4);
    e.v[3] = e.v[3] * 1099511628211ull + static_cast<uint64_t>(W[w].caller_id(row.return_address) + 2) * 1000003ull + row.calls * 8191 + row.owned_calls * 131 + bits;
  }
  events[w].push_back(e);
  last_forwarded[w] = meters;
  if (name.id == A_CAMERA || name.id == A_CAMERA2) safe_write(reinterpret_cast<uint64_t>(camera) + 0x28, &meters, 4);
}
static std::string describe(const Event &e) {
  char text[256], where[96];
  auto place = [&](uint64_t offset) {
    if (e.id == 0xffff) std::snprintf(where, sizeof where, "address %#llx", static_cast<unsigned long long>(offset));
    else std::snprintf(where, sizeof where, "%s+%#llx", A_NAMES[e.id], static_cast<unsigned long long>(offset));
  };
  switch (e.kind) {
  case EV_READ: place(e.v[0]); std::snprintf(text, sizeof text, "read %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_WRITE: place(e.v[0]); std::snprintf(text, sizeof text, "write %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_CAS: place(e.v[0]); std::snprintf(text, sizeof text, "compare-and-swap at %s: store %#llx if %#llx, found %#llx (1 = game's setter, 2 = helper)", where,
                                            (unsigned long long)e.v[1], (unsigned long long)e.v[2], (unsigned long long)e.v[3]); break;
  case EV_LOCK: std::snprintf(text, sizeof text, "%s lock %s%s", e.id ? "policy" : "control", e.v[0] & 1 ? "released" : "taken", e.v[0] >= 2 ? " (shared)" : ""); break;
  default: {
    float meters; std::memcpy(&meters, &e.v[1], 4); place(e.v[0]);
    std::snprintf(text, sizeof text, "forward to the game's setter: %s, distance %.9g [%08x]; caller table then: %llu rows, digest %016llx", where, meters, static_cast<uint32_t>(e.v[1]), (unsigned long long)e.v[2], (unsigned long long)e.v[3]);
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
static const char *const STATS_NAMES[] = {"size", "abi", "installed", "armed", "error", "reserved", "seen", "seen", "forwarded", "forwarded", "replaced", "replaced",
                                          "ads_forwarded", "ads_forwarded", "aim_applied", "aim_applied", "other_kind", "other_kind", "last_native", "last_applied", "hip", "aim"};
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
// The caller table: rows compared with the return address by its logical number.
static void compare_sites(const char *what, uint64_t retail, uint64_t translation, uint32_t rows) {
  for (uint32_t i = 0; i < rows; ++i) {
    NADSN_Site a, b; std::memcpy(&a, reinterpret_cast<void *>(retail + i * 32), 32); std::memcpy(&b, reinterpret_cast<void *>(translation + i * 32), 32);
    const int ia = W[0].caller_id(a.return_address), ib = W[1].caller_id(b.return_address);
    const bool same_address = ia >= 0 || ib >= 0 ? ia == ib : a.return_address == b.return_address;
    if (!same_address || a.calls != b.calls || a.owned_calls != b.owned_calls || std::memcmp(&a.last_value, &b.last_value, 4) || a.reserved != b.reserved)
      fail("%s, row %u: retail caller %d (%#llx) calls %llu owned %llu value %.9g reserved %u; translation caller %d (%#llx) calls %llu owned %llu value %.9g reserved %u", what, i,
           ia, (unsigned long long)a.return_address, (unsigned long long)a.calls, (unsigned long long)a.owned_calls, a.last_value, a.reserved,
           ib, (unsigned long long)b.return_address, (unsigned long long)b.calls, (unsigned long long)b.owned_calls, b.last_value, b.reserved);
  }
}

// ---- the translation's host: the same facts the original gets from the stand-in Windows calls ------------
static uint32_t translation_cas_calls = 0;
static bool answer_getter_executable = true, answer_pin_executable = true; // asked of the translation only
static bool expect_null_original = false; // one translation-only call of the hook before any begin
static void hook_marker() {}
static bool is_exe(uint64_t at) { return at == W[1].a[A_EXE] || at == W[1].a[A_EXE2]; }
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  on_read(1, at, length, 0, false);
  struct iovec local = {out, length}, remote = {reinterpret_cast<void *>(at), length};
  const bool complete = process_vm_readv(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
  on_read(1, at, length, 1, complete);
  return complete;
}
static uint64_t host_now_us(void *) { return fixture_now_us; }
static bool host_writable_slot(void *, uint64_t at, uint64_t exe) {
  ++counted.translation_only;
  if (!is_exe(exe)) fail("the translation asked whether the slot is writable in image %#llx, not in the executable", static_cast<unsigned long long>(exe));
  if (at & 7) fail("the translation asked the host about an unaligned slot");
  return ::writable_slot(at, 0); // the original's fixture answer: committed, writable, private (and fixture_slot_readonly)
}
static bool host_executable(void *, uint64_t at) {
  if (at == W[1].a[A_EXE] + HERE.getter || at == W[1].a[A_EXE2] + HERE.getter) { ++counted.translation_only; return answer_getter_executable; }
  // The original (fixture build) asks this of the function standing in for the game's setter.
  if (at == W[1].native_value) return ::executable(W[0].native_value);
  if (at == W[1].native2_value) return ::executable(W[0].native2_value);
  fail("the translation asked whether an unexpected address is executable: %#llx", static_cast<unsigned long long>(at));
}
static bool host_cas(void *, uint64_t slot_address, uint64_t expected, uint64_t desired, uint64_t *observed) {
  if (slot_address != W[1].slot() && slot_address != W[1].a[A_API2] + 0x68) fail("the translation swaps at %#llx, not at a dispatch slot", static_cast<unsigned long long>(slot_address));
  // The original's fixture seams (cpp:159-160), on the translation's own call count.
  if (++translation_cas_calls == fixture_cas_fail_at) return false;
  uint64_t *target = reinterpret_cast<uint64_t *>(slot_address);
  if (translation_cas_calls == fixture_foreign_cas_at) __sync_lock_test_and_set(target, fixture_foreign_value);
  *observed = __sync_val_compare_and_swap(target, expected, desired);
  on_cas(1, slot_address, desired, expected, *observed);
  return true;
}
static bool host_write(void *, void *out, const void *data, size_t length) {
  const bool complete = safe_write(reinterpret_cast<uint64_t>(out), data, length);
  on_write(1, reinterpret_cast<uint64_t>(out), length, complete);
  return complete;
}
static bool host_pin(void *, uint64_t address) {
  if (address == reinterpret_cast<uint64_t>(&tfp_near_begin)) return !shim_pin_fails;
  if (is_exe(address)) { ++counted.translation_only; return answer_pin_executable; }
  fail("the translation pins an unexpected address: %#llx", static_cast<unsigned long long>(address));
}
static void host_lock(int lock, int operation, int slot_index, int change) {
  lock_depth[slot_index] += change;
  if (lock_depth[slot_index] < 0 || lock_depth[lock] > 1 || (lock_depth[2] && lock_depth[TFP_NEAR_LOCK_POLICY]))
    fail("the translation's locks are unbalanced (control %d, policy %d, policy shared %d)", lock_depth[0], lock_depth[1], lock_depth[2]);
  on_lock(1, lock, operation);
}
static void host_lock_exclusive(void *, int lock) { host_lock(lock, 0, lock, 1); }
static void host_unlock_exclusive(void *, int lock) { host_lock(lock, 1, lock, -1); }
static void host_lock_shared(void *, int lock) { if (lock != TFP_NEAR_LOCK_POLICY) fail("shared lock %d", lock); host_lock(lock, 2, 2, 1); }
static void host_unlock_shared(void *, int lock) { host_lock(lock, 3, 2, -1); }
static void retail_setter(void *camera, float meters);
static void retail_setter2(void *camera, float meters);
static void host_forward(void *, uint64_t original_setter, void *camera, float meters) {
  if (expect_null_original) {
    if (original_setter != 0) fail("the translation still knows an original setter (%#llx) after its reset", static_cast<unsigned long long>(original_setter));
  } else if (original_setter != (::original.load() == &retail_setter2 ? W[1].native2_value : W[1].native_value)) fail("the translation forwards to %#llx, not to the game's setter", static_cast<unsigned long long>(original_setter));
  on_forward(1, camera, meters);
}
static void retail_setter(void *camera, float meters) { on_forward(0, camera, meters); }
static void retail_setter2(void *camera, float meters) { on_forward(0, camera, meters); } // the "other executable's" setter

static void build() {
  W[0].build(RETAIL); W[1].build(HERE);
  W[0].native_value = reinterpret_cast<uint64_t>(&retail_setter); W[0].native2_value = reinterpret_cast<uint64_t>(&retail_setter2);
  W[0].hook_value = reinterpret_cast<uint64_t>(&setter_hook);
  W[1].native_value = W[1].a[A_EXE] + HERE.setter; W[1].native2_value = W[1].a[A_EXE2] + HERE.setter;
  W[1].hook_value = reinterpret_cast<uint64_t>(&hook_marker);
  for (int w = 0; w < 2; ++w) { World::put<uint64_t>(W[w].slot(), W[w].native_value); World::put<uint64_t>(W[w].a[A_API2] + 0x68, W[w].native_value); }
  fixture_target = retail_setter;
  shim_on_read = [](uint64_t at, size_t size, int after, bool complete) { on_read(0, at, size, after, complete); };
  shim_on_write = [](uint64_t at, size_t size, bool complete) { on_write(0, at, size, complete); };
  shim_on_cas = [](uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) { on_cas(0, target, exchange, comparand, observed); };
  shim_on_lock = [](void *lock, int operation) { on_lock(0, lock == &control_lock ? 0 : 1, operation); };
  TfpNearHost host{};
  host.hook = W[1].hook_value; host.read = host_read; host.now_us = host_now_us;
  host.writable_slot = host_writable_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write;
  host.pin = host_pin; host.lock_exclusive = host_lock_exclusive; host.unlock_exclusive = host_unlock_exclusive;
  host.lock_shared = host_lock_shared; host.unlock_shared = host_unlock_shared; host.forward = host_forward;
  tfp_near_reset_for_tests();
  tfp_near_set_host(&host);
}

// ---- a run: the scene, the calls, and the comparison after each -------------------------------------------
enum { SEL_OWN = 0, SEL_SECOND = 1, SEL_SMALL = 2, SEL_UNMAPPED = 3, SEL_NULL = 4, SEL_HIGH = 5 };
static const uint64_t LOW_ADDRESS = 0x8000; // above the first page, below 64K: a helper must refuse it without a read
struct Run {
  Scene s;
  std::vector<Poke> pokes; // written into both worlds before every call, after the scene
  // config (logical; the pointers are each world's own)
  uint32_t size = 72, abi = 1, index = 0, reserved_field = 0; uint64_t reserved2 = 0;
  float hip = .15f, aim = 0;
  int64_t game_delta = 0, exe_delta = 0;
  int context_sel = SEL_OWN, camera_sel = SEL_OWN, owner_sel = SEL_OWN;
  bool second_exe = false;
  int config_at = 0, generation_at = 0; // 0: the io / generation page; 1: null; 2: the read-only page; 3: an address below 64K
  uint64_t gen = 0, previous_gen = 0; uint32_t result = 0;
  // probes
  int probe_mode = INJECT_NONE, probe_index = -1, probe_value = 0;
  std::vector<Poke> probe_values;
  bool probing = false;

  void sync() { for (int w = 0; w < 2; ++w) { W[w].apply(s, index); for (const Poke &p : pokes) store(w, p.at, p.value); } }
  void poke(int id, uint64_t offset, Value v) { pokes.push_back({{id, offset}, v}); }
  void heal() { pokes.clear(); for (int w = 0; w < 2; ++w) W[w].base(); }
  static void set_slot(uint64_t which) { for (int w = 0; w < 2; ++w) World::put<uint64_t>(W[w].slot(), slot_raw(w, which)); }
  static uint64_t pick(int w, int sel, int own, int second) {
    return sel == SEL_OWN ? W[w].a[own] : sel == SEL_SECOND ? W[w].a[second] : sel == SEL_SMALL ? 0x100 : sel == SEL_UNMAPPED ? 0x7770000 : sel == SEL_HIGH ? 0x800000000000ull : 0;
  }

  // After every call: the error code, the statistics and the caller table of both, byte for byte.
  void inspect() {
    const uint32_t retail_error = NADSN_Error(), translation_error = tfp_near_error();
    if (retail_error != translation_error) fail("error code: retail %u, translation %u", retail_error, translation_error);
    const bool held = ::reserved.load() != 0;
    const uint64_t g = held ? ::owned_generation : 0;
    for (int w = 0; w < 2; ++w) {
      events[w].clear(); std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS), 0x5A, 0x400);
      World::put<uint32_t>(W[w].a[A_IO] + IO_STATS, 88); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS + 4, 1);
    }
    uint32_t a = NADSN_ReadStats(g, reinterpret_cast<NADSN_Stats *>(W[0].a[A_IO] + IO_STATS));
    uint32_t b = tfp_near_read_stats(g, reinterpret_cast<TfpNearStats *>(W[1].a[A_IO] + IO_STATS));
    if (a != b) fail("read_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
    if (a != 1) fail("read_stats(%llu) with the right generation returned %u", (unsigned long long)g, a);
    compare_bytes("stats", W[0].a[A_IO] + IO_STATS, W[1].a[A_IO] + IO_STATS, 88, STATS_NAMES, 22);
    a = NADSN_ReadSites(reinterpret_cast<NADSN_Site *>(W[0].a[A_IO] + IO_SITES), 16);
    b = tfp_near_read_sites(reinterpret_cast<TfpNearSite *>(W[1].a[A_IO] + IO_SITES), 16);
    if (a != b) fail("read_sites returned %u in retail, %u in the translation", a, b);
    compare_sites("caller table", W[0].a[A_IO] + IO_SITES, W[1].a[A_IO] + IO_SITES, 16);
    compare_events("while reading the statistics");
    if (NADSN_Error() != retail_error || tfp_near_error() != translation_error) fail("reading the statistics changed an error code");
  }
  void finish(int w) {
    if (inject.kept_length[w]) { protect(inject.kept_at[w], inject.kept_length[w], PROT_READ | PROT_WRITE); inject.kept_length[w] = 0; }
    if (generation_locks_after_write) protect(W[w].a[A_GEN], 8, PROT_READ | PROT_WRITE);
  }
  template <class Retail, class Translation> uint32_t both(const char *text, bool probe_here, Retail retail, Translation translation) {
    ++call_index; call_text = text;
    for (int w = 0; w < 2; ++w) { events[w].clear(); reads_in_call[w] = cas_in_call[w] = writes_in_call[w] = 0; }
    inject.active = probe_here && inject.mode != INJECT_NONE;
    const bool readonly_before = fixture_slot_readonly;
    const uint32_t a = retail(); finish(0);
    const bool readonly_after = fixture_slot_readonly; fixture_slot_readonly = readonly_before; // the translation starts where the original did
    const uint32_t b = translation(); finish(1);
    if (fixture_slot_readonly != readonly_after) fail("a seam of the test fired for one helper only (slot read-only: retail %d, translation %d)", readonly_after, fixture_slot_readonly);
    inject.active = false; after_cas.clear(); generation_locks_after_write = false; readonly_after_cas = false; readonly_after_policy_lock = false;
    if (probe_here) { if (discovering) shared->probe_reads = static_cast<uint64_t>(reads_in_call[0]); inject.mode = INJECT_NONE; }
    if (a != b) fail("return value: retail %u, translation %u", a, b);
    compare_events("during the call");
    for (int api : {A_API, A_API2}) {
      uint64_t slots[2]; for (int w = 0; w < 2; ++w) std::memcpy(&slots[w], reinterpret_cast<void *>(W[w].a[api] + 0x68), 8);
      if (slot_class(0, slots[0]) != slot_class(1, slots[1]))
        fail("dispatch slot of %s (1 = game's setter, 2 = helper): retail %#llx, translation %#llx", A_NAMES[api], (unsigned long long)slot_class(0, slots[0]), (unsigned long long)slot_class(1, slots[1]));
    }
    compare_bytes("the owned camera's near distance", W[0].a[A_CAMERA] + 0x28, W[1].a[A_CAMERA] + 0x28, 4);
    compare_bytes("the second camera's near distance", W[0].a[A_CAMERA2] + 0x28, W[1].a[A_CAMERA2] + 0x28, 4);
    compare_bytes("the generation page", W[0].a[A_GEN], W[1].a[A_GEN], 8);
    if (lock_depth[0] || lock_depth[1] || lock_depth[2]) fail("the translation returned holding a lock (control %d, policy %d, shared %d)", lock_depth[0], lock_depth[1], lock_depth[2]);
    inspect();
    ++counted.calls;
    return result = a;
  }
  bool take_probe() { const bool here = probing; probing = false; return here; }

  void write_config(int w) {
    NADSN_Config c{};
    c.size = size; c.abi = abi; c.game = W[w].a[A_GAME] + game_delta; c.exe = W[w].a[second_exe ? A_EXE2 : A_EXE] + exe_delta;
    c.context = pick(w, context_sel, A_CONTEXT, A_CONTEXT2); c.camera = pick(w, camera_sel, A_CAMERA, A_CAMERA2);
    c.avatar_owner = pick(w, owner_sel, A_OWNER, A_OWNER2);
    c.avatar_index = index; c.hip = hip; c.aim = aim; c.reserved = reserved_field; c.reserved2 = reserved2;
    World::put(W[w].a[A_IO] + IO_CONFIG, c);
  }
  uint64_t config_pointer(int w) const { return config_at == 1 ? 0 : config_at == 3 ? LOW_ADDRESS : config_at == 2 ? W[w].a[A_RO] + 0x800 : W[w].a[A_IO] + IO_CONFIG; }
  uint64_t generation_pointer(int w) const { return generation_at == 1 ? 0 : generation_at == 3 ? LOW_ADDRESS : generation_at == 2 ? W[w].a[A_RO] + 0x100 : W[w].a[A_GEN]; }
  uint32_t begin() {
    sync();
    for (int w = 0; w < 2; ++w) { write_config(w); World::put<uint64_t>(W[w].a[A_GEN], UINT64_MAX); }
    fixture_target = reinterpret_cast<Setter>(second_exe ? W[0].native2_value : W[0].native_value);
    both("begin", take_probe(),
         [&] { return NADSN_Begin(reinterpret_cast<NADSN_Config *>(config_pointer(0)), reinterpret_cast<uint64_t *>(generation_pointer(0))); },
         [&] { return tfp_near_begin(reinterpret_cast<TfpNearConfig *>(config_pointer(1)), reinterpret_cast<uint64_t *>(generation_pointer(1))); });
    uint64_t g; std::memcpy(&g, reinterpret_cast<void *>(W[0].a[A_GEN]), 8);
    if (result && g && g != UINT64_MAX) { previous_gen = gen; gen = g; }
    was_armed = ::armed.load() != 0;
    return result;
  }
  uint32_t end(uint64_t g) { sync(); char t[64]; std::snprintf(t, sizeof t, "end(%llu)", (unsigned long long)g); return both(t, take_probe(), [&] { return NADSN_End(g); }, [&] { return tfp_near_end(g); }); }
  uint32_t set(uint64_t g, float new_hip, float new_aim) {
    sync(); char t[96]; std::snprintf(t, sizeof t, "set(%llu, %.9g, %.9g)", (unsigned long long)g, new_hip, new_aim);
    return both(t, take_probe(), [&] { return NADSN_Set(g, new_hip, new_aim); }, [&] { return tfp_near_set(g, new_hip, new_aim); });
  }
  // The statistics call with a generation and a header of the script's choosing (inspect() uses the right ones).
  // where: 0 the io page, 1 null, 2 the read-only page.
  uint32_t read_stats(uint64_t g, uint32_t header_size = 88, uint32_t header_abi = 1, int where = 0) {
    for (int w = 0; w < 2; ++w) { std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_SCRIPT_STATS), 0x33, 0x100); World::put<uint32_t>(W[w].a[A_IO] + IO_SCRIPT_STATS, header_size); World::put<uint32_t>(W[w].a[A_IO] + IO_SCRIPT_STATS + 4, header_abi); }
    auto at = [&](int w) { return where == 1 ? 0 : where == 3 ? LOW_ADDRESS : where == 2 ? W[w].a[A_RO] : W[w].a[A_IO] + IO_SCRIPT_STATS; };
    char t[96]; std::snprintf(t, sizeof t, "read_stats(%llu), header %u/%u, output %d", (unsigned long long)g, header_size, header_abi, where);
    both(t, take_probe(), [&] { return NADSN_ReadStats(g, reinterpret_cast<NADSN_Stats *>(at(0))); }, [&] { return tfp_near_read_stats(g, reinterpret_cast<TfpNearStats *>(at(1))); });
    compare_bytes("stats (script's call)", W[0].a[A_IO] + IO_SCRIPT_STATS, W[1].a[A_IO] + IO_SCRIPT_STATS, 88, STATS_NAMES, 22);
    return result;
  }
  uint32_t read_sites(uint32_t capacity, int where = 0) {
    for (int w = 0; w < 2; ++w) std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_SCRIPT_SITES), 0, 0x400);
    auto at = [&](int w) { return where == 1 ? 0 : where == 3 ? LOW_ADDRESS : where == 2 ? W[w].a[A_RO] : W[w].a[A_IO] + IO_SCRIPT_SITES; };
    char t[96]; std::snprintf(t, sizeof t, "read_sites(capacity %u), output %d", capacity, where);
    both(t, take_probe(), [&] { return NADSN_ReadSites(reinterpret_cast<NADSN_Site *>(at(0)), capacity); }, [&] { return tfp_near_read_sites(reinterpret_cast<TfpNearSite *>(at(1)), capacity); });
    compare_sites("caller table (script's call)", W[0].a[A_IO] + IO_SCRIPT_SITES, W[1].a[A_IO] + IO_SCRIPT_SITES, 20);
    return result;
  }
  // One call of the hook, `step` microseconds after the last (negative: the clock runs backwards).
  float frame(int64_t step, float meters = .05f, int caller = World::CALL_SITE, int camera = SEL_OWN) {
    fixture_now_us += static_cast<uint64_t>(step);
    return frame_now(meters, caller, camera);
  }
  float frame_now(float meters = .05f, int caller = World::CALL_SITE, int camera = SEL_OWN) {
    // Before any begin got as far as recording the game's setter, the original calls a null pointer.
    if (!::original.load()) { ++counted.skipped_frames; return meters; }
    sync(); ++frame_index;
    uint32_t bits; std::memcpy(&bits, &meters, 4);
    char t[200];
    std::snprintf(t, sizeof t, "frame at %llu us: distance %.9g [%08x], caller %d, camera %d; ring %u/%u kind %#x idle %#x, ads %u, flags %#x/%#x, rows %u", (unsigned long long)fixture_now_us,
                  meters, bits, caller, camera, s.head, s.tail, s.kind, s.idle, s.controls[4], s.flags[10], s.flags[15], s.count);
    both(t, take_probe(),
         [&] { shim_return_address = reinterpret_cast<void *>(W[0].caller(caller)); setter_hook(reinterpret_cast<void *>(pick(0, camera, A_CAMERA, A_CAMERA2)), meters); return 0u; },
         [&] { tfp_near_setter_from(W[1].caller(caller), reinterpret_cast<void *>(pick(1, camera, A_CAMERA, A_CAMERA2)), meters); return 0u; });
    ++counted.frames;
    reached(meters);
    return last_forwarded[0];
  }
  // What the scripts reached, summed over leases (read from the original's state; for the scripts' self-checks only).
  struct Reached { uint64_t replaced = 0, ads = 0, aim = 0, other = 0, gliding = 0, lost = 0, errors[18] = {}; } got;
  struct { uint64_t replaced, ads, aim, other; } before{};
  bool was_armed = false;
  bool code_lost() { const bool now_armed = ::armed.load() != 0, lost = was_armed && !now_armed && NADSN_Error() == OWNER_MISMATCH; was_armed = now_armed; return lost; }
  void reached(float meters) {
    auto step = [](uint64_t now_value, uint64_t &was, uint64_t &sum) { if (now_value > was) sum += now_value - was; was = now_value; };
    const uint64_t replaced_before = got.replaced;
    step(::replaced.load(), before.replaced, got.replaced); step(::ads_forwarded.load(), before.ads, got.ads);
    step(::aim_applied.load(), before.aim, got.aim); step(::other_kind.load(), before.other, got.other);
    // A frame in mid-glide: the distance handed on is the ease's own, and is none of the values it could rest at.
    const float out = last_forwarded[0], shown = ::shown_meters.load();
    if (got.replaced != replaced_before && ::shown_at.load() && out == shown && out != ::hip_meters.load() && out != ::aim_meters.load() && out != .05f && out != meters) ++got.gliding;
    if (code_lost()) ++got.lost;
    const uint32_t code = NADSN_Error(); if (code < 18) ++got.errors[code];
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
  void fp() { s.head = 0; s.tail = 2; s.kind = 6; s.idle = 4; s.controls[4] = 0; s.flags[10] = 0; s.flags[15] = 0; }
  void ads(bool held) { s.controls[4] = held ? 1 : 0; }
  void shoulder(bool held) { s.flags[10] = held ? 4 : 0; s.flags[15] = held ? 32 : 0; }
  void arm() { begin(); if (result != 1) fail("the script could not arm (begin returned %u, error %u)", result, NADSN_Error()); }
};

} // namespace dt
#include "diff_near_helper_scripts.h"
