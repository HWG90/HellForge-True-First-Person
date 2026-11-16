// Differential test: the retail scoped-blur helper (native/first_person/native_ads_blur.cpp, build 26568081,
// compiled natively over tests/winshim with its NADSB_FIXTURE seams) against its C translation
// (src/blur_helper.c, build 25480438). One logical state is written into two fake game memories, one per
// build's addresses (tests/diff_blur_helper_world.h); both helpers are driven with the same calls, and after
// EVERY call these are compared: the return value; every memory read, write, compare-and-swap, lock operation
// and forwarded setter call, in order (addresses by name; the forwarded provider, instance, hash and the 24
// variant bytes bit for bit, and whether the pointer forwarded was the game's own); the caller's variant
// buffer; what the dispatch slot holds; the generation; the error code; the stats structure.
// The first difference is printed and the test exits non-zero. Every script runs in its own child process,
// so both helpers start from their start-up state.
// The retail tree is read, never written. See docs/BLUR-HELPER-TRANSLATION.md, "Differential test".
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
#define NADSB_FIXTURE
#include "native_ads_blur.cpp"
#define _Static_assert static_assert
extern "C" {
#include "blur_helper.h"
}
#include "diff_blur_helper_world.h"

static_assert(sizeof(NADSB_Config) == sizeof(TfpBlurConfig) && sizeof(NADSB_Stats) == sizeof(TfpBlurStats), "the two helpers' structures have one size");
static_assert(offsetof(NADSB_Config, game) == offsetof(TfpBlurConfig, game) && offsetof(NADSB_Config, exe) == offsetof(TfpBlurConfig, exe) &&
              offsetof(NADSB_Config, context) == offsetof(TfpBlurConfig, context) && offsetof(NADSB_Config, camera) == offsetof(TfpBlurConfig, camera) &&
              offsetof(NADSB_Config, avatar_owner) == offsetof(TfpBlurConfig, avatar_owner) && offsetof(NADSB_Config, request) == offsetof(TfpBlurConfig, request) &&
              offsetof(NADSB_Config, avatar_index) == offsetof(TfpBlurConfig, avatar_index) && offsetof(NADSB_Config, avatar) == offsetof(TfpBlurConfig, avatar) &&
              offsetof(NADSB_Config, body) == offsetof(TfpBlurConfig, body) && offsetof(NADSB_Config, weapon) == offsetof(TfpBlurConfig, weapon) &&
              offsetof(NADSB_Config, reserved) == offsetof(TfpBlurConfig, reserved), "the two configs have one layout");
static_assert(offsetof(NADSB_Stats, installed) == offsetof(TfpBlurStats, installed) && offsetof(NADSB_Stats, armed) == offsetof(TfpBlurStats, armed) &&
              offsetof(NADSB_Stats, error) == offsetof(TfpBlurStats, error) && offsetof(NADSB_Stats, seen) == offsetof(TfpBlurStats, seen) &&
              offsetof(NADSB_Stats, forwarded) == offsetof(TfpBlurStats, forwarded) && offsetof(NADSB_Stats, replaced) == offsetof(TfpBlurStats, replaced) &&
              offsetof(NADSB_Stats, immediate_applies) == offsetof(TfpBlurStats, immediate_applies) && offsetof(NADSB_Stats, last_hash) == offsetof(TfpBlurStats, last_hash) &&
              offsetof(NADSB_Stats, last_instance) == offsetof(TfpBlurStats, last_instance) && offsetof(NADSB_Stats, last_native) == offsetof(TfpBlurStats, last_native) &&
              offsetof(NADSB_Stats, last_applied) == offsetof(TfpBlurStats, last_applied) && offsetof(NADSB_Stats, reserved_tail) == offsetof(TfpBlurStats, reserved_tail),
              "the two stats have one layout");
static_assert(OK == TFP_BLUR_OK && INVALID_CONFIG == TFP_BLUR_INVALID_CONFIG && BUSY == TFP_BLUR_BUSY && READ_FAILED == TFP_BLUR_READ_FAILED &&
              IMAGE_MISMATCH == TFP_BLUR_IMAGE_MISMATCH && CODE_MISMATCH == TFP_BLUR_CODE_MISMATCH && API_MISMATCH == TFP_BLUR_API_MISMATCH &&
              OWNER_MISMATCH == TFP_BLUR_OWNER_MISMATCH && UNIT_MISMATCH == TFP_BLUR_UNIT_MISMATCH && PIN_FAILED == TFP_BLUR_PIN_FAILED &&
              SLOT_NOT_WRITABLE == TFP_BLUR_SLOT_NOT_WRITABLE && CAS_MISMATCH == TFP_BLUR_CAS_MISMATCH && CAS_FAULT == TFP_BLUR_CAS_FAULT &&
              CLEANUP_PENDING == TFP_BLUR_CLEANUP_PENDING && SOURCE_INVALID == TFP_BLUR_SOURCE_INVALID && TOKEN_MISMATCH == TFP_BLUR_TOKEN_MISMATCH &&
              NO_SOURCE == TFP_BLUR_NO_SOURCE, "error numbers unchanged");

namespace dt {

// ---- what the children report to the parent --------------------------------------------------------------
struct Shared { uint64_t calls, frames, events, skipped_frames, translation_only, replaced, probe_reads, probe_values; };
static Shared *shared;
static struct { uint64_t calls, frames, events, skipped_frames, translation_only, replaced; } counted;

// ---- the two worlds and the record of what each helper did ------------------------------------------------
static World W[2]; // 0: retail memory, read by the original; 1: this build's memory, read by the translation
enum { EV_READ, EV_WRITE, EV_CAS, EV_LOCK, EV_FORWARD };
struct Event { uint32_t kind, id; uint64_t v[6]; };
static std::vector<Event> events[2];
static int reads_in_call[2], forwards_in_call[2];
static int lock_depth[2];
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
static bool safe_write(uint64_t at, const void *data, size_t length) {
  struct iovec local = {const_cast<void *>(data), length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_writev(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static bool safe_read(uint64_t at, void *data, size_t length) {
  struct iovec local = {data, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static void store(int w, Sym at, const Value &v) {
  uint64_t raw = v.kind == 1 ? W[w].at(v.sym) : v.kind == 2 ? slot_raw(w, v.raw) : v.raw;
  std::memcpy(reinterpret_cast<void *>(W[w].at(at)), &raw, static_cast<size_t>(v.length));
}
static void protect(uint64_t at, size_t length, int protection) {
  const uint64_t begin = at & ~4095ull, end = (at + length + 4095) & ~4095ull;
  mprotect(reinterpret_cast<void *>(begin), end - begin, protection);
}

// ---- trouble injected into the next call ------------------------------------------------------------------
// By read number (the probes): that read fails and heals, fails and stays unreadable to the end of the call,
// or a value changes just before it. By address (scripts): the next `remaining` reads of that place fail.
// On a forwarded setter call: a value changes, or an address failure is armed, as the game's setter returns.
enum { INJECT_NONE, INJECT_FAIL, INJECT_FAIL_STAY, INJECT_VALUE };
static struct {
  int mode = INJECT_NONE, index = -1; bool active = false;
  Sym at{0, 0}; Value value;
} inject;
static struct { Sym at{0, 0}; int remaining[2] = {0, 0}; bool stay = false; } fail_at;
static struct { int at_forward = -1; bool poke = false; Sym at{0, 0}; Value value; bool fail = false; Sym fail_sym{0, 0}; bool fail_stay = false; } on_forward_do;
static bool failing[2], staying[2];
static std::vector<std::pair<uint64_t, size_t>> kept[2];

static void on_read(int w, uint64_t at, size_t size, int after, bool complete) {
  if (!after) {
    const bool by_index = inject.active && reads_in_call[w] == inject.index;
    if (by_index && inject.mode == INJECT_VALUE) { store(w, inject.at, inject.value); return; }
    const bool by_address = fail_at.remaining[w] > 0 && at == W[w].at(fail_at.at);
    if (by_index || by_address) {
      failing[w] = true; staying[w] = by_index ? inject.mode == INJECT_FAIL_STAY : fail_at.stay;
      if (by_address && !by_index) --fail_at.remaining[w];
      protect(at, size, PROT_NONE);
    }
    return;
  }
  if (failing[w]) {
    failing[w] = false;
    if (staying[w]) kept[w].push_back({at, size}); else protect(at, size, PROT_READ | PROT_WRITE);
  }
  const Sym name = W[w].name(at);
  // A complete read outside every region would compare whatever the allocator put there: a fault of the test.
  if (complete && name.id == 0xffff) fail("test fault: %s read %zu bytes at %#llx, outside every region of its world", w ? "the translation" : "retail", size, static_cast<unsigned long long>(at));
  events[w].push_back({EV_READ, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0, 0, 0}});
  ++reads_in_call[w];
}
static void on_write(int w, uint64_t at, size_t size, bool complete) {
  const Sym name = W[w].name(at);
  events[w].push_back({EV_WRITE, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0, 0, 0}});
}
static void on_cas(int w, uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) {
  const Sym name = W[w].name(target);
  events[w].push_back({EV_CAS, static_cast<uint32_t>(name.id), {name.offset, slot_class(w, exchange), slot_class(w, comparand), slot_class(w, observed), 0, 0}});
}
static void on_lock(int w, int lock, int operation) { events[w].push_back({EV_LOCK, static_cast<uint32_t>(lock), {static_cast<uint64_t>(operation), 0, 0, 0, 0, 0}}); }
// The game's own setter: recorded. Pointer classes: 0 the game's own variant, 1 a buffer of the helper's
// (no region of the world), 2 null, 3 another place of the world; +8: the 24 bytes could not be read.
static void on_forward(int w, void *object, uint64_t packed, uint32_t hash, const void *variant) {
  const Sym provider = W[w].name(reinterpret_cast<uint64_t>(object));
  Event e{EV_FORWARD, static_cast<uint32_t>(provider.id), {provider.offset, packed, hash, 0, 0, 0}};
  const uint64_t pointer_value = reinterpret_cast<uint64_t>(variant);
  uint64_t cls = 1;
  if (!variant) cls = 2;
  else if (pointer_value == W[w].variant()) cls = 0;
  else { const Sym n = W[w].name(pointer_value); if (n.id != 0xffff) cls = 3 | (static_cast<uint64_t>(n.id) << 8) | (n.offset << 20); }
  uint8_t bytes24[24] = {};
  if (variant && !safe_read(pointer_value, bytes24, 24)) { cls |= 8; std::memset(bytes24, 0, 24); }
  e.v[2] |= cls << 32;
  std::memcpy(&e.v[3], bytes24, 24);
  events[w].push_back(e);
  if (forwards_in_call[w]++ == on_forward_do.at_forward) {
    if (on_forward_do.poke) store(w, on_forward_do.at, on_forward_do.value);
    if (on_forward_do.fail) { fail_at.at = on_forward_do.fail_sym; fail_at.remaining[w] = 1; fail_at.stay = on_forward_do.fail_stay; }
  }
}
static std::string describe(const Event &e) {
  char text[320];
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
  case EV_LOCK: std::snprintf(text, sizeof text, "%s lock %s", e.id ? "policy" : "control", e.v[0] & 1 ? "released" : "taken"); break;
  default: {
    place(where, sizeof where, e.v[0]);
    const uint64_t cls = e.v[2] >> 32; uint32_t tag; float f; std::memcpy(&tag, &e.v[3], 4); std::memcpy(&f, &e.v[4], 4);
    std::snprintf(text, sizeof text, "forward to the game's setter: provider %s, instance %#llx, hash %#x, variant pointer class %#llx (0 = the game's own, 1 = the helper's buffer, 2 = null), tag %u, float %.9g [%016llx %016llx %016llx]",
                  where, (unsigned long long)e.v[1], static_cast<uint32_t>(e.v[2]), (unsigned long long)cls, tag, f, (unsigned long long)e.v[3], (unsigned long long)e.v[4], (unsigned long long)e.v[5]);
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
                                          "immediate_applies", "immediate_applies", "last_hash", "last_instance", "last_native", "last_applied", "reserved_tail", "reserved_tail"};

// ---- the translation's host: the same facts the original gets from the stand-in Windows calls ------------
static uint32_t translation_cas_calls = 0;
static bool answer_executable[3] = {true, true, true}, answer_pin_executable = true; // asked of the translation only
static bool expect_null_original = false;
static void hook_marker() {}
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  on_read(1, at, length, 0, false);
  const bool complete = safe_read(at, out, length);
  on_read(1, at, length, 1, complete);
  return complete;
}
static bool host_readable_span(void *, uint64_t at, size_t length) { return ::readable_span(at, length); }
static bool host_writable_slot(void *, uint64_t at, uint64_t exe) {
  ++counted.translation_only;
  if (exe != W[1].a[A_EXE]) fail("the translation asked whether the slot is writable in image %#llx, not in the executable", static_cast<unsigned long long>(exe));
  return ::writable_slot(at, 0); // the original's fixture answer: committed, writable, private (and fixture_slot_readonly)
}
static bool host_executable(void *, uint64_t at) {
  const uint64_t exe = W[1].a[A_EXE];
  if (at == exe + HERE.pack) { ++counted.translation_only; return answer_executable[0]; }
  if (at == exe + HERE.lookup) { ++counted.translation_only; return answer_executable[1]; }
  if (at == exe + HERE.apply) { ++counted.translation_only; return answer_executable[2]; }
  // The original (fixture build) asks this of the function standing in for the game's setter.
  if (at == exe + HERE.setter) return ::executable(W[0].native_value);
  fail("the translation asked whether an unexpected address is executable: %#llx", static_cast<unsigned long long>(at));
}
static bool host_cas(void *, uint64_t slot_address, uint64_t expected, uint64_t desired, uint64_t *observed) {
  if (slot_address != W[1].slot()) fail("the translation swaps at %#llx, not at the dispatch slot", static_cast<unsigned long long>(slot_address));
  // The original's fixture seams (cpp:296-297), on the translation's own call count.
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
  if (address == reinterpret_cast<uint64_t>(&tfp_blur_begin)) return !shim_pin_fails;
  if (address == W[1].a[A_EXE]) { ++counted.translation_only; return answer_pin_executable; }
  fail("the translation pins an unexpected address: %#llx", static_cast<unsigned long long>(address));
}
static void host_lock(int lock, int operation, int change) {
  if (lock != TFP_BLUR_LOCK_CONTROL && lock != TFP_BLUR_LOCK_POLICY) fail("the translation uses an unknown lock %d", lock);
  lock_depth[lock] += change;
  if (lock_depth[lock] < 0 || lock_depth[lock] > 1)
    fail("the translation's locks are unbalanced (control %d, policy %d)", lock_depth[0], lock_depth[1]);
  on_lock(1, lock, operation);
}
static void host_lock_exclusive(void *, int lock) { host_lock(lock, 0, 1); }
static void host_unlock_exclusive(void *, int lock) { host_lock(lock, 1, -1); }
static void host_forward(void *, uint64_t original_setter, void *object, uint64_t packed, uint32_t hash, const void *variant) {
  if (expect_null_original) {
    if (original_setter != 0) fail("the translation still knows an original setter (%#llx) after its reset", static_cast<unsigned long long>(original_setter));
  } else if (original_setter != W[1].a[A_EXE] + HERE.setter) fail("the translation forwards to %#llx, not to the game's setter", static_cast<unsigned long long>(original_setter));
  on_forward(1, object, packed, hash, variant);
}
static void retail_setter(void *object, uint64_t packed, uint32_t hash, const void *variant) { on_forward(0, object, packed, hash, variant); }

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
  TfpBlurHost host{};
  host.hook = W[1].hook_value; host.read = host_read; host.readable_span = host_readable_span;
  host.writable_slot = host_writable_slot; host.executable = host_executable; host.cas = host_cas; host.write = host_write;
  host.pin = host_pin; host.lock_exclusive = host_lock_exclusive; host.unlock_exclusive = host_unlock_exclusive; host.forward = host_forward;
  tfp_blur_reset_for_tests();
  tfp_blur_set_host(&host);
}

// ---- a run: the state, the calls, and the comparison after each -------------------------------------------
struct Hook { bool caller_ours = true; int caller_delta = 1; int object_id = A_PROVIDER; uint64_t object_offset = 0; uint64_t packed = (static_cast<uint64_t>(DOF) << 32) | ENTITY_B;
              uint32_t hash = H_FAR; int variant = 0; }; // variant: 0 the game's own buffer, 1 null, 2 a copy in the spare page, 3 the unreadable page after it
struct Run {
  // config (logical; the pointers are each world's own)
  uint32_t size = 80, abi = 1, index = 0, avatar = AVATAR, body = BODY_UNIT, weapon = WEAPON; uint64_t reserved_field = 0;
  int64_t game_delta = 0, exe_delta = 0, context_delta = 0, camera_delta = 0, owner_delta = 0, request_delta = 0;
  int64_t context_absolute = -1, camera_absolute = -1, request_absolute = -1; // not -1: this value instead of the world's own address
  uint32_t head = 0, tail = 2;
  bool config_null = false, config_unreadable = false, generation_null = false, generation_unwritable = false;
  uint64_t gen = 0, previous_gen = 0; uint32_t result = 0;
  // probes
  int probe_mode = INJECT_NONE, probe_index = -1, probe_value = 0;
  struct Change { std::string name; Sym at; Value value; };
  std::vector<Change> probe_values;
  bool probing = false;
  // undo list: what a place held in each world before the script changed it
  struct Saved { Sym at; int length; uint8_t old[2][8]; };
  std::vector<Saved> undo;

  void snapshot(Sym at, int length) {
    Saved s{at, length, {}};
    for (int w = 0; w < 2; ++w) std::memcpy(s.old[w], reinterpret_cast<void *>(W[w].at(at)), static_cast<size_t>(length));
    undo.push_back(s);
  }
  void set(int id, uint64_t offset, const Value &v) { snapshot({id, offset}, v.length); for (int w = 0; w < 2; ++w) store(w, {id, offset}, v); }
  void set(const Change &c) { set(c.at.id, c.at.offset, c.value); }
  size_t mark() const { return undo.size(); }
  void restore(size_t to = 0) {
    while (undo.size() > to) {
      const Saved &s = undo.back();
      for (int w = 0; w < 2; ++w) std::memcpy(reinterpret_cast<void *>(W[w].at(s.at)), s.old[w], static_cast<size_t>(s.length));
      undo.pop_back();
    }
  }
  uint64_t request_offset() const { return head == tail ? 0x1F00 : 0x200 + ((tail + 31) % 32) * 0xE8; }
  // The newest camera request: kind 6 = the game's own first-person request, 4 = the normal one.
  void top(uint32_t kind = 6, uint32_t new_tail = 2, uint32_t new_head = 0) {
    head = new_head; tail = new_tail;
    set(A_CONTEXT, 0x1F8, bytes_value(head)); set(A_CONTEXT, 0x1FC, bytes_value(tail));
    const uint64_t at = request_offset();
    set(A_CONTEXT, at, bytes_value(kind)); set(A_CONTEXT, at + 4, bytes_value(avatar)); set(A_CONTEXT, at + 8, bytes_value(weapon)); set(A_CONTEXT, at + 12, bytes_value(body));
  }
  // The second eligible optic component: entity 107, row 6, blob1.
  void two_sources() { set(A_CLASSES, 6 * 4, bytes_value(DOF)); set(A_BLOBS, 6 * 8, address_value(A_BLOB1)); }

  void observe() {
    const uint32_t retail_error = NADSB_Error(), translation_error = tfp_blur_error();
    if (retail_error != translation_error) fail("error code: retail %u, translation %u", retail_error, translation_error);
    const bool held = ::reserved.load() != 0;
    const uint64_t g = held ? ::owned_generation : 0;
    for (int w = 0; w < 2; ++w) { events[w].clear(); std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS), 0x5A, 0x100); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS, 80); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS + 4, 1); }
    const uint32_t a = NADSB_ReadStats(g, reinterpret_cast<NADSB_Stats *>(W[0].a[A_IO] + IO_STATS));
    const uint32_t b = tfp_blur_read_stats(g, reinterpret_cast<TfpBlurStats *>(W[1].a[A_IO] + IO_STATS));
    if (a != b) fail("read_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
    if (a != 1) fail("read_stats(%llu) with the lease's own generation returned %u", (unsigned long long)g, a);
    compare_bytes("stats", W[0].a[A_IO] + IO_STATS, W[1].a[A_IO] + IO_STATS, 0x100, STATS_NAMES, 20);
    compare_events("while reading the statistics");
    if (NADSB_Error() != retail_error || tfp_blur_error() != translation_error) fail("reading the statistics changed an error code");
  }
  void finish(int w) {
    for (const auto &k : kept[w]) protect(k.first, k.second, PROT_READ | PROT_WRITE);
    kept[w].clear(); fail_at.remaining[w] = 0;
  }
  template <class Retail, class Translation> uint32_t both(const char *text, bool probe_here, Retail retail, Translation translation) {
    ++call_index; call_text = text;
    for (int w = 0; w < 2; ++w) { events[w].clear(); reads_in_call[w] = 0; forwards_in_call[w] = 0; }
    inject.active = probe_here && inject.mode != INJECT_NONE;
    const uint32_t a = retail(); finish(0);
    const uint32_t b = translation(); finish(1);
    inject.active = false; on_forward_do.at_forward = -1; on_forward_do.poke = on_forward_do.fail = false;
    if (probe_here) { if (discovering) shared->probe_reads = static_cast<uint64_t>(reads_in_call[0]); inject.mode = INJECT_NONE; }
    if (a != b) fail("return value: retail %u, translation %u", a, b);
    compare_events("during the call");
    compare_bytes("the caller's variant buffer", W[0].variant(), W[1].variant(), 64);
    uint64_t slots[2]; for (int w = 0; w < 2; ++w) std::memcpy(&slots[w], reinterpret_cast<void *>(W[w].slot()), 8);
    if (slot_class(0, slots[0]) != slot_class(1, slots[1]))
      fail("dispatch slot (1 = game's setter, 2 = helper): retail %#llx, translation %#llx", (unsigned long long)slot_class(0, slots[0]), (unsigned long long)slot_class(1, slots[1]));
    if (lock_depth[0] || lock_depth[1]) fail("the translation returned holding a lock (control %d, policy %d)", lock_depth[0], lock_depth[1]);
    if (fixture_cas_calls != translation_cas_calls) fail("compare-and-swap attempts: retail %u, translation %u", fixture_cas_calls, translation_cas_calls);
    observe();
    ++counted.calls;
    return result = a;
  }
  bool take_probe() { const bool here = probing; probing = false; return here; }

  // Trouble for the next call.
  void fail_reads(int id, uint64_t offset, bool stay = false, int count = 1) { fail_at.at = {id, offset}; fail_at.remaining[0] = fail_at.remaining[1] = count; fail_at.stay = stay; }
  void fail_read_number(int number, bool stay = false) { inject.mode = stay ? INJECT_FAIL_STAY : INJECT_FAIL; inject.index = number; probing = true; }
  void change_on_forward(int forward_number, const Change &c) { snapshot(c.at, c.value.length); on_forward_do.at_forward = forward_number; on_forward_do.poke = true; on_forward_do.at = c.at; on_forward_do.value = c.value; }
  void fail_after_forward(int forward_number, int id, uint64_t offset, bool stay = false) { on_forward_do.at_forward = forward_number; on_forward_do.fail = true; on_forward_do.fail_sym = {id, offset}; on_forward_do.fail_stay = stay; }

  void write_config(int w) {
    NADSB_Config c{};
    c.size = size; c.abi = abi; c.game = W[w].a[A_GAME] + game_delta; c.exe = W[w].a[A_EXE] + exe_delta;
    c.context = W[w].a[A_CONTEXT] + context_delta; c.camera = W[w].a[A_CAMERA] + camera_delta; c.avatar_owner = W[w].a[A_OWNER] + owner_delta;
    c.request = W[w].a[A_CONTEXT] + request_offset() + request_delta;
    if (context_absolute != -1) c.context = static_cast<uint64_t>(context_absolute);
    if (camera_absolute != -1) c.camera = static_cast<uint64_t>(camera_absolute);
    if (request_absolute != -1) c.request = static_cast<uint64_t>(request_absolute);
    c.avatar_index = index; c.avatar = avatar; c.body = body; c.weapon = weapon; c.reserved = reserved_field;
    World::put(W[w].a[A_IO] + IO_CONFIG, c);
  }
  uint64_t config_at(int w) const { return config_null ? 0 : config_unreadable ? W[w].a[A_IO] + 0x1000 : W[w].a[A_IO] + IO_CONFIG; }
  uint64_t generation_at(int w) const { return generation_null ? 0 : generation_unwritable ? W[w].a[A_IO] + 0x1000 : W[w].a[A_IO] + IO_GENERATION; }
  uint32_t begin() {
    for (int w = 0; w < 2; ++w) { write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
    both("begin", take_probe(),
         [&] { return NADSB_Begin(reinterpret_cast<NADSB_Config *>(config_at(0)), reinterpret_cast<uint64_t *>(generation_at(0))); },
         [&] { return tfp_blur_begin(reinterpret_cast<TfpBlurConfig *>(config_at(1)), reinterpret_cast<uint64_t *>(generation_at(1))); });
    uint64_t g[2]; for (int w = 0; w < 2; ++w) std::memcpy(&g[w], reinterpret_cast<void *>(W[w].a[A_IO] + IO_GENERATION), 8);
    if (g[0] != g[1]) fail("generation written by begin: retail %llu, translation %llu", (unsigned long long)g[0], (unsigned long long)g[1]);
    if (result && g[0] && g[0] != UINT64_MAX) { previous_gen = gen; gen = g[0]; }
    fixture_cas_fail_at = 0; fixture_foreign_cas_at = 0;
    return result;
  }
  uint32_t validate() {
    for (int w = 0; w < 2; ++w) write_config(w);
    return both("validate", take_probe(), [&] { return NADSB_Validate(reinterpret_cast<NADSB_Config *>(config_at(0))); },
                [&] { return tfp_blur_validate(reinterpret_cast<TfpBlurConfig *>(config_at(1))); });
  }
  uint32_t end(uint64_t g) {
    char t[64]; std::snprintf(t, sizeof t, "end(%llu)", (unsigned long long)g);
    both(t, take_probe(), [&] { return NADSB_End(g); }, [&] { return tfp_blur_end(g); });
    fixture_cas_fail_at = 0; fixture_foreign_cas_at = 0;
    return result;
  }
  // The statistics call with a generation and a header of the script's choosing (observe() uses the right ones).
  uint32_t read_stats(uint64_t g, uint32_t header_size = 80, uint32_t header_abi = 1, int where = 0) {
    for (int w = 0; w < 2; ++w) { std::memset(reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS2), 0xA5, 0x100); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS2, header_size); World::put<uint32_t>(W[w].a[A_IO] + IO_STATS2 + 4, header_abi); }
    char t[96]; std::snprintf(t, sizeof t, "read_stats(%llu), header %u/%u, pointer %d", (unsigned long long)g, header_size, header_abi, where);
    auto at = [&](int w) -> uint64_t { return where == 1 ? 0 : where == 2 ? W[w].a[A_IO] + 0x1000 : where == 3 ? W[w].a[A_SPARE] : W[w].a[A_IO] + IO_STATS2; };
    // where 3: a page that can be read but not written (the header is there, the result cannot be stored).
    if (where == 3) for (int w = 0; w < 2; ++w) { std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE]), reinterpret_cast<void *>(W[w].a[A_IO] + IO_STATS2), 80); mprotect(reinterpret_cast<void *>(W[w].a[A_SPARE]), 4096, PROT_READ); }
    both(t, take_probe(), [&] { return NADSB_ReadStats(g, reinterpret_cast<NADSB_Stats *>(at(0))); }, [&] { return tfp_blur_read_stats(g, reinterpret_cast<TfpBlurStats *>(at(1))); });
    if (where == 3) for (int w = 0; w < 2; ++w) { mprotect(reinterpret_cast<void *>(W[w].a[A_SPARE]), 4096, PROT_READ | PROT_WRITE); std::memset(reinterpret_cast<void *>(W[w].a[A_SPARE]), 0, 80); }
    compare_bytes("stats (script's call)", W[0].a[A_IO] + IO_STATS2, W[1].a[A_IO] + IO_STATS2, 0x100, STATS_NAMES, 20);
    return result;
  }
  // One call of the hook, as the game's call site (or another caller) makes it.
  void sample(const Hook &h = Hook()) {
    // Before any begin got as far as recording the game's setter, the original calls a null pointer.
    if (!::original.load()) { ++counted.skipped_frames; return; }
    ++frame_index;
    char t[240];
    std::snprintf(t, sizeof t, "hook: %s caller, provider %s+%#llx, instance %#llx, hash %#x, variant %d", h.caller_ours ? "the game's" : "another", A_NAMES[h.object_id],
                  (unsigned long long)h.object_offset, (unsigned long long)h.packed, h.hash, h.variant);
    const int wrong = h.caller_ours ? 0 : h.caller_delta;
    if (h.variant == 2) for (int w = 0; w < 2; ++w) std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE] + 0x800), reinterpret_cast<void *>(W[w].variant()), 24);
    auto variant = [&](int w) -> const void * { return h.variant == 1 ? nullptr : reinterpret_cast<const void *>(h.variant == 2 ? W[w].a[A_SPARE] + 0x800 : h.variant == 3 ? W[w].a[A_SPARE] + 0x2000 : W[w].variant()); };
    const uint64_t before = ::replaced.load();
    both(t, take_probe(),
         [&] { shim_return_address = reinterpret_cast<void *>(W[0].a[A_GAME] + RETAIL.call + 3 + wrong);
               setter_hook(reinterpret_cast<void *>(W[0].a[h.object_id] + h.object_offset), h.packed, h.hash, variant(0)); return 0u; },
         [&] { tfp_blur_setter_from(W[1].a[A_GAME] + HERE.call + 3 + wrong, reinterpret_cast<void *>(W[1].a[h.object_id] + h.object_offset), h.packed, h.hash, variant(1)); return 0u; });
    ++counted.frames; counted.replaced += ::replaced.load() - before;
  }
  void sample(uint32_t hash) { Hook h; h.hash = hash; sample(h); }
  // The game's own variant buffer: type tag and float.
  void variant(float f = 1.5f, uint32_t type = 2) { set(A_VARIANT, 0, bytes_value(type)); uint32_t bits; std::memcpy(&bits, &f, 4); set(A_VARIANT, 8, bytes_value(bits)); }
  // The next call is the probed one: in the discovery run its reads are counted; otherwise the chosen read
  // fails (and heals, or stays unreadable to the end of the call), or the chosen value changes just before it.
  void probe() {
    probing = true;
    if (discovering) shared->probe_values = probe_values.size();
    if (probe_mode == INJECT_NONE) return;
    inject.mode = probe_mode; inject.index = probe_index;
    if (probe_mode == INJECT_VALUE) { const Change &c = probe_values[static_cast<size_t>(probe_value)]; snapshot(c.at, c.value.length); inject.at = c.at; inject.value = c.value; }
  }
  // What the scripts check about themselves, from the original's state.
  void expect(bool condition, const char *what) { if (!condition) fail("the script did not reach what it is for: %s (retail error %u, result %u)", what, NADSB_Error(), result); }
  void arm() { top(6); begin(); expect(result == 1 && ::armed.load() == 1, "begin arms"); }
};

} // namespace dt
#include "diff_blur_helper_scripts.h"
