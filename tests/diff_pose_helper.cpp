// Differential test: the retail editor/pose helper (native/first_person/native_ads_editor.cpp, build 26568081,
// compiled natively over tests/winshim with its NADSE_FIXTURE seams) against its C translation
// (src/pose_helper.c, build 25480438). One logical scene is written into two fake game memories, one per
// build's addresses (tests/diff_pose_helper_world.h); both helpers are armed and driven with the same calls,
// and after EVERY call these are compared: the return value; every memory read, write, compare-and-swap (64
// and 32 bit), lock operation, allocation, call of the game's exp and forwarded setter or getter call, in
// order (addresses by name, forwarded positions and quaternions bit for bit); what the five dispatch slots
// hold; the request's preset selector and the private preset records; the error code; and the statistics,
// depth statistics, input statistics, rotation trace and entry position structures. The first difference is
// printed and the test exits non-zero. Every script runs in its own child process, so both helpers start
// from their start-up state. The retail tree is read, never written.
// Built twice by tests/run-diff-pose.sh: against the production object of the translation, and (with
// DIFF_POSE_INPUT) against an object built with tests/diff_pose_helper_open.h, in which the look-gain
// normalisation is not refused. See docs/POSE-HELPER-TRANSLATION.md, "Differential test".
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
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <xmmintrin.h>
#include "pose_extra.h"
#define NADSE_FIXTURE
#include "native_ads_editor.cpp"
#define _Static_assert static_assert
extern "C" {
#include "pose_helper.h"
}
#include "diff_pose_helper_world.h"

static_assert(sizeof(NADSE_Config) == sizeof(TfpPoseConfig) && sizeof(NADSE_Stats) == sizeof(TfpPoseStats) && sizeof(NADSE_DepthStats) == sizeof(TfpPoseDepthStats) &&
              sizeof(NADSE_InputStats) == sizeof(TfpPoseInputStats) && sizeof(NADSE_RotationTraceData) == sizeof(TfpPoseRotationTraceData) &&
              sizeof(NADSE_EntryPositionData) == sizeof(TfpPoseEntryPositionData), "the two helpers' structures have one size");
static_assert(offsetof(NADSE_Config, weapon_descriptor) == offsetof(TfpPoseConfig, weapon_descriptor) && offsetof(NADSE_Config, offset_z) == offsetof(TfpPoseConfig, offset_z) &&
              offsetof(NADSE_Stats, zero_restores) == offsetof(TfpPoseStats, zero_restores) && offsetof(NADSE_DepthStats, gain) == offsetof(TfpPoseDepthStats, gain) &&
              offsetof(NADSE_InputStats, pitch_gain) == offsetof(TfpPoseInputStats, pitch_gain), "the two helpers' structures have one layout");

namespace dp {
#if defined(DIFF_POSE_INPUT)
static const bool input_build = true;  // the translation was built with the test's values for its open constants
#else
static const bool input_build = false; // the production translation: the look-gain normalisation refuses
#endif

// ---- what the children report to the parent --------------------------------------------------------------
struct Shared { uint64_t calls, hooks, events, skipped, translation_only, retail_only_reads, probe_reads, probe_values; };
static Shared *shared;
static struct { uint64_t calls, hooks, events, skipped, translation_only, retail_only_reads; } counted;

// ---- the two worlds and the record of what each helper did ------------------------------------------------
static World W[2]; // 0: retail memory, read by the original; 1: this build's memory, read by the translation
enum { EV_READ, EV_WRITE, EV_CAS, EV_LOCK, EV_FORWARD, EV_GETTER, EV_CAS32, EV_ALLOCATE, EV_EXP };
struct Event { uint32_t kind, id; uint64_t v[5]; };
static std::vector<Event> events[2];
static int reads_in_call[2];
static int lock_depth[3];
static int side = 0;               // which helper is running
static std::string script_name, call_text;
static uint64_t call_index;
static bool discovering;           // this child counts the probed call's reads for the parent
static uint64_t game_pose[2];      // the pointer the game passed to the hook being called (0: none)
static unsigned getter_entry = 3;  // which dispatch slot the call being made came through (the original's fixture has one function behind both getters)
static bool setter_applies = true; // the game's position setter writes node 0's local position
static bool expect_null_original = false;
static std::vector<uint64_t> blocks[2]; // the 64 KiB blocks each helper allocated for private preset records

[[noreturn]] static void fail(const char *format, ...) {
  char text[2048];
  va_list arguments; va_start(arguments, format); std::vsnprintf(text, sizeof text, format, arguments); va_end(arguments);
  std::fprintf(stderr, "DIFFERENCE (%s) script \"%s\", call %llu: %s\n  %s\n", input_build ? "look-gain build" : "production build", script_name.c_str(),
               static_cast<unsigned long long>(call_index), call_text.c_str(), text);
  std::fflush(stderr);
  std::_Exit(1);
}

// A value written into both worlds: plain bytes, the address of a named place, or a dispatch slot value.
struct Value { int kind = 0; uint64_t raw = 0; Sym sym{0, 0}; int length = 4; int which = 0; };
static Value bytes_value(uint64_t raw, int length = 4) { Value v; v.raw = raw; v.length = length; return v; }
static Value address_value(int id, uint64_t offset = 0) { Value v; v.kind = 1; v.sym = {id, offset}; v.length = 8; return v; }
static Value float_value(float f) { uint32_t bits; std::memcpy(&bits, &f, 4); return bytes_value(bits, 4); }
// Dispatch slot values by class: the game's own function, one of the helper's six values, or a raw foreign value.
enum : uint64_t { SLOT_NATIVE = 1, SLOT_HOOK = 0x10, SLOT_FOREIGN = 0x7770000 };
static uint64_t slot_class(int w, int which, uint64_t value) {
  if (value == W[w].native[which]) return SLOT_NATIVE;
  for (int k = 0; k < 6; ++k) if (value == W[w].hooks[k]) return SLOT_HOOK + static_cast<uint64_t>(k);
  return value;
}
static uint64_t slot_raw(int w, int which, uint64_t cls) {
  return cls == SLOT_NATIVE ? W[w].native[which] : cls >= SLOT_HOOK && cls < SLOT_HOOK + 6 ? W[w].hooks[cls - SLOT_HOOK] : cls;
}
static Value slot_value(int which, uint64_t cls) { Value v; v.kind = 2; v.raw = cls; v.length = 8; v.which = which; return v; }
static void store(int w, Sym at, const Value &v) {
  uint64_t raw = v.kind == 1 ? W[w].at(v.sym) : v.kind == 2 ? slot_raw(w, v.which, v.raw) : v.raw;
  std::memcpy(reinterpret_cast<void *>(W[w].at(at)), &raw, static_cast<size_t>(v.length));
}
static bool safe_write(uint64_t at, const void *data, size_t length) {
  struct iovec local = {const_cast<void *>(data), length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_writev(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static bool safe_read(uint64_t at, void *data, size_t length) {
  struct iovec local = {data, length}, remote = {reinterpret_cast<void *>(at), length};
  return process_vm_readv(shim_pid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
}
static void protect(uint64_t at, size_t length, int protection) {
  const uint64_t begin = at & ~4095ull, end = (at + length + 4095) & ~4095ull;
  mprotect(reinterpret_cast<void *>(begin), end - begin, protection);
}
// The private preset records: the index a helper stores in the request is (record - table) / 144, a number that
// depends on where the block lies. It is compared as "private record k".
static uint64_t record_of(int w, size_t k) { const uint64_t table = W[w].a[A_G_PRESETS], start = blocks[w][k]; return start + (144 - (start - table) % 144) % 144; }
static uint64_t preset_class(int w, uint32_t value) {
  for (size_t k = 0; k < blocks[w].size(); ++k) if ((record_of(w, k) - W[w].a[A_G_PRESETS]) / 144 == value) return 0x80000000ull + k;
  return value;
}

// ---- one injected event for the next call: a read that fails, or a value that changes just before a read --
enum { INJECT_NONE, INJECT_FAIL, INJECT_FAIL_STAY, INJECT_VALUE };
static struct {
  int mode = INJECT_NONE, index = -1; bool active = false;
  Sym at{0, 0}; Value value;
  uint64_t kept_at[2] = {0, 0}; size_t kept_length[2] = {0, 0};
} inject;
static uint64_t fault_at[2]; // a page made unwritable by the "fault after the query" seam, to be put back after the call
static uint32_t translation_fault_after_query = 0;

static bool retail_only_read(int w, uint64_t at, size_t size) {
  // The production translation refuses the code proof before reading (B_INPUT_ESTABLISHED 0); the original
  // hashes its first pin (the evaluator, in 512-byte blocks) and then fails. Those reads have no counterpart.
  return !input_build && w == 0 && size >= 256 && at >= W[0].a[A_G_PIN0] && at < W[0].a[A_G_PIN0] + RETAIL.pins[0].length;
}
static void on_read(int w, uint64_t at, size_t size, int after, bool complete) {
  if (retail_only_read(w, at, size)) { if (after) ++counted.retail_only_reads; return; }
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
  // The nine depth pins are whole instruction sequences, and one of them (the division by the number of resource
  // buckets) is 24 bytes in retail and 36 in this build: such a read is recorded as "the whole pin".
  uint64_t logical = size;
  if (name.id >= A_G_DEPTH1 && name.id <= A_G_DEPTH9 && name.offset == 0 && size == std::strlen(W[w].L->depth_hex[name.id - A_G_DEPTH1]) / 2) logical = 0x10000;
  events[w].push_back({EV_READ, static_cast<uint32_t>(name.id), {name.offset, logical, complete ? 1u : 0u, 0, 0}});
  ++reads_in_call[w];
}
static void on_write(int w, uint64_t at, size_t size, bool complete) {
  const Sym name = W[w].name(at);
  events[w].push_back({EV_WRITE, static_cast<uint32_t>(name.id), {name.offset, size, complete ? 1u : 0u, 0, 0}});
}
static int slot_index(int w, uint64_t target) { for (int i = 0; i < 5; ++i) if (W[w].slot(i) == target) return i; return -1; }
static void on_cas(int w, uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) {
  const int which = slot_index(w, target);
  if (which < 0) fail("%s swaps at %#llx, which is none of the five dispatch slots", w ? "the translation" : "the original", static_cast<unsigned long long>(target));
  events[w].push_back({EV_CAS, static_cast<uint32_t>(which), {slot_class(w, which, exchange), slot_class(w, which, comparand), slot_class(w, which, observed), 0, 0}});
}
static void on_lock(int w, int lock, int operation) { events[w].push_back({EV_LOCK, static_cast<uint32_t>(lock), {static_cast<uint64_t>(operation), 0, 0, 0, 0}}); }
// The game's own setters: recorded; the position setter on the rig's node 0 writes that node's local position
// (which the helper reads back before it restores the rig root).
static void on_forward(int w, unsigned which, uint32_t unit, uint32_t node, const float *pose) {
  Event e{EV_FORWARD, which, {unit | static_cast<uint64_t>(node) << 32, 0, 0, 0, 0}};
  const uint64_t address = reinterpret_cast<uint64_t>(pose);
  e.v[1] = !pose ? 2 : address == game_pose[w] ? 0 : 1; // the game's own argument, the helper's buffer, or null
  if (pose) e.v[4] = safe_read(address, &e.v[2], 16) ? 1 : 2;
  events[w].push_back(e);
  if (which == 0 && unit == VIEWMODEL && node == 0 && setter_applies && e.v[4] == 1) {
    uint64_t poses = 0;
    if (safe_read(W[w].a[A_RIG] + 0x80, &poses, 8)) safe_write(poses + 0x24, &e.v[2], 12);
  }
}
static const float *on_getter(int w, unsigned which, uint32_t unit, uint32_t node) {
  events[w].push_back({EV_GETTER, which, {unit | static_cast<uint64_t>(node) << 32, 0, 0, 0, 0}});
  return reinterpret_cast<const float *>(W[w].a[A_ARG] + ARG_GETTER_RESULT);
}
static void on_allocate(uint64_t wanted, uint64_t size, uint64_t got) {
  events[side].push_back({EV_ALLOCATE, 0, {size, got ? 1u : 0u, 0, 0, 0}});
  if (got) blocks[side].push_back(got);
}
// The 32-bit compare-exchange of native_ads_input.S (NADSE_InputCompareExchange with its fault handler): the
// swap, the value found, or 0 when the write would fault. The same routine serves the translation's host.
static uint32_t cas32(int w, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  uint32_t current = 0;
  const bool writable = safe_read(at, &current, 4) && safe_write(at, &current, 4);
  const Sym name = W[w].name(at);
  Event e{EV_CAS32, static_cast<uint32_t>(name.id), {name.offset, preset_class(w, desired), preset_class(w, expected), 0, writable ? 1u : 0u}};
  if (writable) { *observed = __sync_val_compare_and_swap(reinterpret_cast<uint32_t *>(at), expected, desired); e.v[3] = preset_class(w, *observed); }
  events[w].push_back(e);
  return writable ? 1 : 0;
}
// The game's exp, in the ways it can answer.
static int exp_mode = 0;
static double the_exp(int w, double x) {
  Event e{EV_EXP, 0, {0, 0, 0, 0, 0}}; std::memcpy(&e.v[0], &x, 8); events[w].push_back(e);
  switch (exp_mode) {
  case 1: return NAN;
  case 2: return 1.5;
  case 3: _mm_setcsr(_mm_getcsr() | 0x8000u); return std::exp(x); // leaves flush-to-zero set
  default: return std::exp(x);
  }
}

static std::string describe(const Event &e) {
  char text[320], where[96];
  auto place = [&](uint64_t offset) {
    if (e.id == 0xffff) std::snprintf(where, sizeof where, "memory outside the worlds");
    else std::snprintf(where, sizeof where, "%s+%#llx", A_NAMES[e.id], static_cast<unsigned long long>(offset));
  };
  switch (e.kind) {
  case EV_READ: place(e.v[0]);
    if (e.v[1] == 0x10000) std::snprintf(text, sizeof text, "read %s, the whole pin, %s", where, e.v[2] ? "complete" : "FAILED");
    else std::snprintf(text, sizeof text, "read %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED");
    break;
  case EV_WRITE: place(e.v[0]); std::snprintf(text, sizeof text, "write %s, %llu bytes, %s", where, (unsigned long long)e.v[1], e.v[2] ? "complete" : "FAILED"); break;
  case EV_CAS: std::snprintf(text, sizeof text, "compare-and-swap at dispatch slot %u: store %#llx if %#llx, found %#llx (1 = the game's function, 0x10.. = the helper's)", e.id,
                             (unsigned long long)e.v[0], (unsigned long long)e.v[1], (unsigned long long)e.v[2]); break;
  case EV_LOCK: std::snprintf(text, sizeof text, "%s lock %s%s", e.id ? "policy" : "control", e.v[0] & 1 ? "released" : "taken", e.v[0] >= 2 ? " (shared)" : ""); break;
  case EV_GETTER: std::snprintf(text, sizeof text, "forward to the game's getter behind slot %u: unit %#x, node %u", e.id, (unsigned)e.v[0], (unsigned)(e.v[0] >> 32)); break;
  case EV_CAS32: place(e.v[0]); std::snprintf(text, sizeof text, "32-bit compare-and-swap at %s: store %#llx if %#llx, found %#llx, %s (0x8000000k = private record k)", where,
                                              (unsigned long long)e.v[1], (unsigned long long)e.v[2], (unsigned long long)e.v[3], e.v[4] ? "done" : "FAULTED"); break;
  case EV_ALLOCATE: std::snprintf(text, sizeof text, "allocate %llu bytes near the preset table: %s", (unsigned long long)e.v[0], e.v[1] ? "obtained" : "refused"); break;
  case EV_EXP: { double x; std::memcpy(&x, &e.v[0], 8); std::snprintf(text, sizeof text, "call the game's exp(%.17g)", x); break; }
  default: {
    float p[4]; std::memcpy(p, &e.v[2], 16);
    std::snprintf(text, sizeof text, "forward to the game's setter behind slot %u: unit %#x, node %u, %s%s, values %.9g %.9g %.9g %.9g [%016llx %016llx]", e.id, (unsigned)e.v[0],
                  (unsigned)(e.v[0] >> 32), e.v[1] == 0 ? "the game's own argument" : e.v[1] == 1 ? "the helper's buffer" : "null", e.v[4] == 2 ? " (unreadable)" : "",
                  p[0], p[1], p[2], p[3], (unsigned long long)e.v[2], (unsigned long long)e.v[3]);
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
static const char *const STATS_NAMES[] = {"size", "abi", "installed", "armed", "expired", "error", "seen_position", "seen_position", "seen_rotation", "seen_rotation",
                                          "forwarded_position", "forwarded_position", "forwarded_rotation", "forwarded_rotation", "weapon_updates", "weapon_updates",
                                          "zero_restores", "zero_restores", "blend", "target_weight"};
static const char *const DEPTH_NAMES[] = {"size", "abi", "installed", "armed", "supported", "reason", "zoom_index", "reserved", "applied", "applied", "unsupported", "unsupported",
                                          "cache_refreshes", "cache_refreshes", "cache_checks", "cache_checks", "ads_bypassed", "ads_bypassed", "native_distance",
                                          "reference_distance", "correction", "zoom", "target_fov", "current_fov", "gain", "padding"};
static const char *const INPUT_NAMES[] = {"size", "abi", "installed", "active", "reason", "pending", "cache_entries", "reserved", "observed", "observed", "normalized", "normalized",
                                          "unsupported", "unsupported", "restored", "restored", "foreign_wins", "foreign_wins", "quarantined", "quarantined", "ratio", "yaw_gain",
                                          "pitch_gain", "padding"};
static const char *const ENTRY_NAMES[] = {"size", "enabled", "writes", "remembered"};
// The rotation trace holds each write's caller as an offset into the game image or the executable: compared by name.
static void compare_trace(const char *what, uint64_t retail, uint64_t translation) {
  const NADSE_RotationTraceData *a = reinterpret_cast<const NADSE_RotationTraceData *>(retail), *b = reinterpret_cast<const NADSE_RotationTraceData *>(translation);
  static const char *const header[] = {"size", "count", "guarded", "enabled"};
  compare_bytes(what, retail, translation, 16, header, 4);
  for (unsigned i = 0; i < NADSE_ROTATION_TRACE_ROWS; ++i) {
    const NADSE_RotationTraceRow &x = a->rows[i], &y = b->rows[i];
    if (x.slot != y.slot || x.ms != y.ms || x.replaced != y.replaced || std::memcmp(x.q, y.q, 16))
      fail("%s row %u differs: retail slot %u at %u ms, replaced %u, q %.9g %.9g %.9g %.9g; translation slot %u at %u ms, replaced %u, q %.9g %.9g %.9g %.9g", what, i,
           x.slot, x.ms, x.replaced, x.q[0], x.q[1], x.q[2], x.q[3], y.slot, y.ms, y.replaced, y.q[0], y.q[1], y.q[2], y.q[3]);
    auto caller = [](int w, uint32_t value) -> Sym {
      if (value == 0xffffffffu) return {0xfffe, 0};
      return value & 0x80000000u ? W[w].name(W[w].a[A_EXE] + (value & 0x7fffffffu)) : W[w].name(W[w].a[A_GAME] + value);
    };
    const Sym p = caller(0, x.caller), q = caller(1, y.caller);
    if (p.id != q.id || p.offset != q.offset)
      fail("%s row %u: the caller differs: retail %#x (%s+%#llx), translation %#x (%s+%#llx)", what, i, x.caller, p.id < A_NAMES_COUNT ? A_NAMES[p.id] : "neither image",
           (unsigned long long)p.offset, y.caller, q.id < A_NAMES_COUNT ? A_NAMES[q.id] : "neither image", (unsigned long long)q.offset);
  }
}

// ---- the game's functions behind the original's slots (its fixture targets) ------------------------------
static void retail_position(uint32_t unit, uint32_t node, const float *pose) { on_forward(0, 0, unit, node, pose); }
static void retail_rotation(uint32_t unit, uint32_t node, const float *pose) { on_forward(0, 1, unit, node, pose); }
static void retail_branch_rotation(uint32_t unit, uint32_t node, const float *pose) { on_forward(0, 2, unit, node, pose); }
static const float *retail_getter(uint32_t unit, uint32_t node) { return on_getter(0, getter_entry, unit, node); }
static double retail_exp(double x) { return the_exp(0, x); }

// ---- the translation's host: the same facts the original gets from the stand-in Windows calls ------------
static uint32_t translation_cas_calls = 0;
static bool answer_getter_executable = true, answer_pin_exe = true, answer_pin_game = true; // asked of the translation only
static uint8_t hook_markers[6];
static bool host_read(void *, uint64_t at, void *out, size_t length) {
  on_read(1, at, length, 0, false);
  const bool complete = safe_read(at, out, length);
  on_read(1, at, length, 1, complete);
  return complete;
}
static uint64_t host_tick(void *) { return ::fixture_now; }
static uint32_t host_thread(void *) { return shim_thread_id; }
static bool host_writable_slot(void *, uint64_t at, uint64_t exe) {
  ++counted.translation_only;
  if (exe != W[1].a[A_EXE]) fail("the translation asked whether the slot is writable in image %#llx, not in the executable", static_cast<unsigned long long>(exe));
  return ::writable_slot(at, 0); // the original's fixture answer: committed, writable, private (and fixture_slot_readonly)
}
static bool host_executable(void *, uint64_t at) {
  if (at == W[1].a[A_E_GRAPH_GETTER]) { ++counted.translation_only; return answer_getter_executable; }
  // The original (fixture build) asks this of the functions standing in for the game's.
  for (int i = 0; i < 4; ++i) if (at == W[1].native[i]) return ::executable(W[0].native[i]);
  if (at == W[1].native[4]) return ::executable(W[0].native[4]);
  fail("the translation asked whether an unexpected address is executable: %#llx", static_cast<unsigned long long>(at));
}
static bool host_cas(void *, uint64_t slot_address, uint64_t expected, uint64_t desired, uint64_t *observed) {
  const int which = slot_index(1, slot_address);
  if (which < 0) fail("the translation swaps at %#llx, which is none of the five dispatch slots", static_cast<unsigned long long>(slot_address));
  // The original's fixture seams (cpp:296-300), on the translation's own call count.
  if (++translation_cas_calls == ::fixture_cas_fail_at) return false;
  if (::fixture_fail_rollback && desired == W[1].native[0]) return false;
  uint64_t *target = reinterpret_cast<uint64_t *>(slot_address);
  if (translation_cas_calls == ::fixture_foreign_cas_at) __sync_lock_test_and_set(target, ::fixture_foreign_value);
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
  if (address == reinterpret_cast<uint64_t>(&tfp_pose_begin)) return !shim_pin_fails;
  if (address == W[1].a[A_EXE]) { ++counted.translation_only; return answer_pin_exe; }
  if (address == W[1].a[A_GAME]) { ++counted.translation_only; return answer_pin_game; }
  fail("the translation pins an unexpected address: %#llx", static_cast<unsigned long long>(address));
}
static void host_lock(int lock, int operation, int slot, int change) {
  lock_depth[slot] += change;
  if (lock_depth[slot] < 0 || lock_depth[lock] > 1 || (lock_depth[2] && lock_depth[TFP_POSE_LOCK_POLICY]))
    fail("the translation's locks are unbalanced (control %d, policy %d, policy shared %d)", lock_depth[0], lock_depth[1], lock_depth[2]);
  on_lock(1, lock, operation);
}
static void host_lock_exclusive(void *, int lock) { host_lock(lock, 0, lock, 1); }
static void host_unlock_exclusive(void *, int lock) { host_lock(lock, 1, lock, -1); }
static void host_lock_shared(void *, int lock) { if (lock != TFP_POSE_LOCK_POLICY) fail("shared lock %d", lock); host_lock(lock, 2, 2, 1); }
static void host_unlock_shared(void *, int lock) { host_lock(lock, 3, 2, -1); }
// The VirtualQuery test of input_cas32 (cpp:752-754), then the original's "fault after the query" seam (cpp:755-761).
static bool writable32(uint64_t at) {
  MEMORY_BASIC_INFORMATION region{};
  return VirtualQuery(reinterpret_cast<void *>(at), &region, sizeof region) == sizeof region && region.State == MEM_COMMIT
      && writable_pe_protection(region.Protect) && at + 4 <= reinterpret_cast<uint64_t>(region.BaseAddress) + region.RegionSize;
}
static bool host_writable32(void *, uint64_t at) {
  if (!writable32(at)) return false;
  if (translation_fault_after_query) {
    const uint32_t mode = translation_fault_after_query; translation_fault_after_query = 0;
    protect(at & ~0xfffull, 0x1000, mode == 2 ? PROT_NONE : PROT_READ); fault_at[1] = at & ~0xfffull;
  }
  return true;
}
static bool host_cas32(void *, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) { return cas32(1, at, expected, desired, observed) != 0; }
static bool host_region(void *, uint64_t at, uint64_t *base, uint64_t *size, bool *is_free) { return shim_span(at, base, size, is_free); }
static uint64_t host_allocate(void *, uint64_t at, uint64_t size) { return shim_place(at, size); }
static double host_exp(void *, uint64_t address, double x) {
  if (address != W[1].a[A_G_PIN3]) fail("the translation calls exp at %#llx, not at the game's exp", static_cast<unsigned long long>(address));
  return the_exp(1, x);
}
static void host_forward_setter(void *, unsigned which, uint64_t original, uint32_t unit, uint32_t node, const float *pose) {
  if (which > 2) fail("the translation forwards a setter call through slot %u", which);
  if (expect_null_original ? original != 0 : original != W[1].native[which])
    fail("the translation forwards slot %u's call to %#llx, not to the game's function", which, static_cast<unsigned long long>(original));
  on_forward(1, which, unit, node, pose);
}
static const float *host_forward_getter(void *, unsigned which, uint64_t original, uint32_t unit, uint32_t node) {
  if (which != 3 && which != 4) fail("the translation forwards a getter call through slot %u", which);
  if (expect_null_original ? original != 0 : original != W[1].native[which])
    fail("the translation forwards slot %u's getter call to %#llx, not to the game's function", which, static_cast<unsigned long long>(original));
  return on_getter(1, which, unit, node);
}

static bool load(const std::string &path, uint64_t offset, uint64_t length, uint64_t to) {
  const int file = open(path.c_str(), O_RDONLY);
  if (file < 0) return false;
  const ssize_t n = pread(file, reinterpret_cast<void *>(to), length, static_cast<off_t>(offset));
  close(file);
  return n == static_cast<ssize_t>(length);
}
static uint64_t fnv(uint64_t at, uint32_t length) {
  uint64_t hash = 0xcbf29ce484222325ull;
  for (uint32_t i = 0; i < length; ++i) hash = (hash ^ *reinterpret_cast<const uint8_t *>(at + i)) * 0x100000001b3ull;
  return hash;
}
// The code the look-gain normalisation pins. Retail world: retail's bytes, from a capture of that build (the
// same files the retail input fixture reads). This build's world: its own bytes for the seven established
// pins, and retail's bytes at the test's places for the nine open ones. False: a file is missing.
static bool load_pins() {
  const char *home = std::getenv("HOME");
  const char *captures = std::getenv("TFP_INPUT_CAPTURE_DIR"), *images = std::getenv("TFP_HERE_IMAGE_DIR");
  const std::string retail_dir = captures ? captures : std::string(home ? home : "") + "/Projects/Linux HD2 Mods/builds/26568081/mods/gadget-kit/capture-links";
  const std::string here_dir = images ? images : std::string(home ? home : "") + "/Documents/ChatGPT/Linux HD2/native-work/evidence";
  const std::string retail_file[2] = {retail_dir + "/game-6af322c1-180000000.bin", retail_dir + "/helldivers2-6af2d5e6-140000000.bin"};
  const std::string here_file[2] = {here_dir + "/game-pre1020.dll", here_dir + "/exe-25480438.bin"};
  for (int i = 0; i < 16; ++i) {
    const Pin &r = RETAIL.pins[i], &h = HERE.pins[i];
    if (!load(retail_file[r.exe], r.offset, r.length, W[0].a[r.exe ? A_EXE : A_GAME] + r.offset)) return false;
    if (!(h.real ? load(here_file[h.exe], h.offset, h.length, W[1].a[h.exe ? A_EXE : A_GAME] + h.offset)
                 : load(retail_file[r.exe], r.offset, h.length, W[1].a[h.exe ? A_EXE : A_GAME] + h.offset))) return false;
  }
  // What the worlds' own literals say must still be there: the images agree with the constants of both builds.
  for (int w = 0; w < 2; ++w) {
    const Layout &L = *W[w].L; const uint64_t game = W[w].a[A_GAME], exe = W[w].a[A_EXE];
    auto check = [&](uint64_t at, const char *hex, const char *what) {
      if (!::bytes(at, hex)) { std::fprintf(stderr, "Pose helper: the %s image does not hold the expected bytes at %s\n", w ? "25480438" : "retail", what); std::_Exit(2); }
    };
    check(game + L.carrier_call, "41ff5138", "the carrier call"); check(game + L.carrier_pair, "41ff5148", "the carrier pair"); check(game + L.rig_call, "41ff5138", "the rig call");
    check(game + L.observe_call, "41ffd0", "the observe call"); check(game + L.input_return - 2, "ffd0", "the input call");
    for (int i = 4; i < 9; ++i) check(game + L.depth_pin[i], L.depth_hex[i], "a depth pin");
    for (int i = 0; i < 3; ++i) check(game + L.carrier_pin[i], CARRIER_HEX[i], "a carrier pin");
    check(exe + L.wrappers[3], L.hex_wrappers[3], "the local position getter"); check(exe + L.world_matrix, L.hex_world_matrix, "the world matrix getter");
    check(exe + L.graph_getter, "488d4160c3", "the graph getter");
  }
  return true;
}

static void build() {
  W[0].build(RETAIL); W[1].build(HERE);
  W[0].native[0] = reinterpret_cast<uint64_t>(&retail_position); W[0].native[1] = reinterpret_cast<uint64_t>(&retail_rotation);
  W[0].native[2] = reinterpret_cast<uint64_t>(&retail_branch_rotation); W[0].native[3] = W[0].native[4] = reinterpret_cast<uint64_t>(&retail_getter);
  W[0].hooks[0] = reinterpret_cast<uint64_t>(&::position_hook); W[0].hooks[1] = reinterpret_cast<uint64_t>(&::rotation_hook);
  W[0].hooks[2] = reinterpret_cast<uint64_t>(&::branch_rotation_hook); W[0].hooks[3] = reinterpret_cast<uint64_t>(&::position_getter_hook);
  W[0].hooks[4] = reinterpret_cast<uint64_t>(&NADSE_Input00Thunk); W[0].hooks[5] = reinterpret_cast<uint64_t>(&NADSE_Input90Thunk);
  for (int i = 0; i < 4; ++i) W[1].native[i] = W[1].a[A_EXE] + HERE.wrappers[i];
  W[1].native[4] = W[1].a[A_EXE] + HERE.world_matrix;
  for (int k = 0; k < 6; ++k) W[1].hooks[k] = reinterpret_cast<uint64_t>(&hook_markers[k]);
  for (int w = 0; w < 2; ++w) for (int i = 0; i < 5; ++i) World::put<uint64_t>(W[w].slot(i), W[w].native[i]);
  ::fixture_targets[0] = retail_position; ::fixture_targets[1] = retail_rotation; ::fixture_targets[2] = retail_branch_rotation;
  ::fixture_targets[3] = reinterpret_cast<Setter>(retail_getter);
  ::fixture_input_exp = retail_exp;
  ::fixture_now = 1000;
  // The fixture build of the original starts with the draw animation and the entry position off (cpp:1016, 1178);
  // the shipped one, which the translation follows, with 220 ms and on (cpp:1018, 1180). The translation keeps
  // its own start-up values: this is where they are compared with the shipped ones.
  ::entry_animation_ms.store(220); ::entry_position_enabled.store(1);
  shim_on_read = [](uint64_t at, size_t size, int after, bool complete) { on_read(0, at, size, after, complete); };
  shim_on_write = [](uint64_t at, size_t size, bool complete) { on_write(0, at, size, complete); };
  shim_on_cas = [](uint64_t target, uint64_t exchange, uint64_t comparand, uint64_t observed) { on_cas(0, target, exchange, comparand, observed); };
  shim_on_lock = [](void *lock, int operation) { on_lock(0, lock == &::control_lock ? 0 : 1, operation); };
  shim_on_allocate = on_allocate;
  TfpPoseHost host{};
  for (int k = 0; k < 6; ++k) host.hooks[k] = W[1].hooks[k];
  host.read = host_read; host.tick = host_tick; host.thread = host_thread; host.writable_slot = host_writable_slot; host.executable = host_executable;
  host.cas = host_cas; host.write = host_write; host.pin = host_pin; host.lock_exclusive = host_lock_exclusive; host.unlock_exclusive = host_unlock_exclusive;
  host.lock_shared = host_lock_shared; host.unlock_shared = host_unlock_shared; host.writable32 = host_writable32; host.cas32 = host_cas32;
  host.region = host_region; host.allocate = host_allocate; host.native_exp = host_exp; host.forward_setter = host_forward_setter; host.forward_getter = host_forward_getter;
  tfp_pose_set_host(&host);
}

// ---- a run: the scene, the calls, and the comparison after each -------------------------------------------
struct Poke { Sym at; Value value; uint64_t old[2] = {0, 0}; };
static const Sym NOWHERE{-1, 0};
struct Run {
  Scene s;
  std::vector<Poke> pokes;
  // config (logical; the pointers are each world's own)
  uint32_t size = 104, abi = 2, index = 0, avatar = AVATAR, weapon = WEAPON, body = BODY, viewmodel = VIEWMODEL, duration = 0, weapon_unit = WEAPON_UNIT, blend = 150, reserved_field = 0;
  float offset[3] = {.15f, 0, -.12f};
  int64_t game_delta = 0, exe_delta = 0, context_delta = 0, owner_delta = 0, request_delta = 0, wdesc_delta = 0;
  uint64_t wdesc_absolute = 0;
  bool config_null = false, generation_null = false;
  uint64_t gen = 0, previous_gen = 0; uint32_t result = 0;
  bool poll = true;        // read the lease statistics after every call (that call can itself end a timed lease)
  bool stack_on = true;    // the hooks are given the evaluator's stack frame (off: no frame, as a caller without one)
  bool inner_frame = false;
  bool thunks = false;     // the getter is entered as the input-mode thunk enters it (with the caller's R15)
  float optic_target = 0;  // the lens the current zoom level is heading for (the scripts' depth helpers)
  // probes
  int probe_mode = INJECT_NONE, probe_index = -1, probe_value = 0;
  std::vector<Poke> probe_values;
  bool probing = false;

  Run() {
    for (uint32_t i = 0; i < RIG_NODES; ++i) { s.hashes[i] = 0x1000 + i; s.links[i] = 0x10001; }
    for (const GraphRow &row : GRAPH_ROWS) { s.hashes[row.node] = row.hash; s.links[row.node] = row.link; }
    for (unsigned i = 0; i < 144; ++i) s.preset[i] = static_cast<uint8_t>(i * 7 + 1);
    const float gains[2] = {.3f, .4f}; std::memcpy(s.preset + 4, gains, 8);
    const uint32_t tail = 0x76543210; std::memcpy(s.preset + 0x8c, &tail, 4);
    normal();
  }
  // ---- the request record ----
  template <class V> void req(unsigned at, const V &v) { std::memcpy(s.request + at, &v, sizeof v); if (at == 0x80) s.write_preset = true; }
  template <class V> V req_get(unsigned at) const { V v; std::memcpy(&v, s.request + at, sizeof v); return v; }
  // Third person: the newest record is an ordinary one; the owned row is the next to be written (fixture: normal()).
  void normal() {
    s.ring[0] = static_cast<uint32_t>((s.row + 31) % 32); s.ring[1] = static_cast<uint32_t>(s.row);
    s.under[0] = 4; s.under[1] = avatar; s.under[2] = s.under[3] = 0;
    std::memset(s.request, 0, sizeof s.request); s.write_preset = true;
    std::memset(s.controller, 0, 6); s.flags[6] = 0; s.flags[10] = 0; s.flags[14] = 0; s.flags[15] = 0;
    s.wdesc[0] = weapon; s.wdesc[1] = weapon_unit; s.descriptor[2] = avatar; s.descriptor[3] = body; s.stack_index = index; s.rig_reference = viewmodel;
  }
  // The game's own first-person request in the owned row, alone in the ring (fixture: fpv() and sole()).
  void fpv() {
    s.ring[0] = static_cast<uint32_t>(s.row); s.ring[1] = static_cast<uint32_t>((s.row + 1) % 32);
    req<uint32_t>(0, 6); req<uint32_t>(4, avatar); req<uint32_t>(8, weapon); req<uint32_t>(12, body); req<uint32_t>(0x18, 3);
    const float pose[4] = {1, 2, 3, 0}; req(0x48, pose); req<uint32_t>(0x54, 0x80000000u); req<float>(0x98, 0);
  }
  void pose(float x, float y, float z) { const float p[3] = {x, y, z}; req(0x48, p); }

  void sync() {
    for (int w = 0; w < 2; ++w) { W[w].apply(s, index); for (const Poke &p : pokes) store(w, p.at, p.value); }
    s.write_preset = false; s.write_root_local = false;
  }
  // A change to one place of both worlds that stays (re-applied after every scene write) until unpoke().
  void poke(int id, uint64_t offset, Value v) {
    Poke p{{id, offset}, v};
    for (int w = 0; w < 2; ++w) std::memcpy(&p.old[w], reinterpret_cast<void *>(W[w].at(p.at)), static_cast<size_t>(v.length));
    pokes.push_back(p);
  }
  void unpoke(size_t keep = 0) {
    while (pokes.size() > keep) {
      const Poke &p = pokes.back();
      for (int w = 0; w < 2; ++w) std::memcpy(reinterpret_cast<void *>(W[w].at(p.at)), &p.old[w], static_cast<size_t>(p.value.length));
      pokes.pop_back();
    }
  }
  // Places that are unreadable during the next call only (after the scene has been written).
  std::vector<std::pair<Sym, size_t>> blocked;
  Sym request_sym(unsigned offset = 0) const { return {A_CONTEXT, 0x200 + static_cast<uint64_t>(s.row) * 0xE8 + offset}; }

  void compare_slots() {
    for (int i = 0; i < 5; ++i) {
      uint64_t v[2]; for (int w = 0; w < 2; ++w) std::memcpy(&v[w], reinterpret_cast<void *>(W[w].slot(i)), 8);
      if (slot_class(0, i, v[0]) != slot_class(1, i, v[1]))
        fail("dispatch slot %d (1 = the game's function, 0x10.. = the helper's): retail %#llx, translation %#llx", i, (unsigned long long)slot_class(0, i, v[0]), (unsigned long long)slot_class(1, i, v[1]));
    }
  }
  void compare_presets() {
    uint32_t v[2] = {0, 0}; for (int w = 0; w < 2; ++w) safe_read(W[w].request(s.row) + 0x80, &v[w], 4);
    if (preset_class(0, v[0]) != preset_class(1, v[1]))
      fail("the request's preset selector (0x8000000k = private record k): retail %#llx, translation %#llx", (unsigned long long)preset_class(0, v[0]), (unsigned long long)preset_class(1, v[1]));
    if (blocks[0].size() != blocks[1].size()) fail("private preset blocks: retail allocated %zu, the translation %zu", blocks[0].size(), blocks[1].size());
    for (size_t k = 0; k < blocks[0].size(); ++k) { char what[64]; std::snprintf(what, sizeof what, "private preset record %zu", k); compare_bytes(what, record_of(0, k), record_of(1, k), 144); }
  }
  void observe() {
    const uint32_t retail_error = NADSE_Error(), translation_error = tfp_pose_error();
    if (retail_error != translation_error) fail("error code: retail %u, translation %u", retail_error, translation_error);
    const bool held = ::reserved.load() != 0;
    const uint64_t g = held ? ::owned_generation : 0;
    uint32_t a = 0, b = 0;
    for (int w = 0; w < 2; ++w) {
      events[w].clear(); const uint64_t io = W[w].a[A_IO];
      std::memset(reinterpret_cast<void *>(io + IO_STATS), 0x5A, 0xE00);
      World::put<uint32_t>(io + IO_STATS, 80); World::put<uint32_t>(io + IO_STATS + 4, 2);
      World::put<uint32_t>(io + IO_DEPTH, 104); World::put<uint32_t>(io + IO_DEPTH + 4, 1); World::put<uint32_t>(io + IO_DEPTH + 28, 0);
      World::put<uint32_t>(io + IO_INPUT, 96); World::put<uint32_t>(io + IO_INPUT + 4, 1); World::put<uint32_t>(io + IO_INPUT + 28, 0);
    }
    if (poll) {
      side = 0; a = NADSE_ReadStats(g, reinterpret_cast<NADSE_Stats *>(W[0].a[A_IO] + IO_STATS));
      side = 1; b = tfp_pose_read_stats(g, reinterpret_cast<TfpPoseStats *>(W[1].a[A_IO] + IO_STATS));
      if (a != b) fail("read_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
      compare_bytes("stats", W[0].a[A_IO] + IO_STATS, W[1].a[A_IO] + IO_STATS, 80, STATS_NAMES, 20);
    }
    side = 0; a = NADSE_ReadDepthStats(g, reinterpret_cast<NADSE_DepthStats *>(W[0].a[A_IO] + IO_DEPTH));
    side = 1; b = tfp_pose_read_depth_stats(g, reinterpret_cast<TfpPoseDepthStats *>(W[1].a[A_IO] + IO_DEPTH));
    if (a != b) fail("read_depth_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
    compare_bytes("depth stats", W[0].a[A_IO] + IO_DEPTH, W[1].a[A_IO] + IO_DEPTH, 104, DEPTH_NAMES, 26);
    side = 0; a = NADSE_ReadInputStats(g, reinterpret_cast<NADSE_InputStats *>(W[0].a[A_IO] + IO_INPUT));
    side = 1; b = tfp_pose_read_input_stats(g, reinterpret_cast<TfpPoseInputStats *>(W[1].a[A_IO] + IO_INPUT));
    if (a != b) fail("read_input_stats(%llu) returned %u in retail, %u in the translation", (unsigned long long)g, a, b);
    compare_bytes("input stats", W[0].a[A_IO] + IO_INPUT, W[1].a[A_IO] + IO_INPUT, 96, INPUT_NAMES, 24);
    side = 0; a = NADSE_RotationTrace49(reinterpret_cast<NADSE_RotationTraceData *>(W[0].a[A_IO] + IO_TRACE));
    side = 1; b = tfp_pose_rotation_trace(reinterpret_cast<TfpPoseRotationTraceData *>(W[1].a[A_IO] + IO_TRACE));
    if (a != b) fail("rotation_trace returned %u in retail, %u in the translation", a, b);
    compare_trace("rotation trace", W[0].a[A_IO] + IO_TRACE, W[1].a[A_IO] + IO_TRACE);
    side = 0; a = NADSE_EntryPositionStats50(reinterpret_cast<NADSE_EntryPositionData *>(W[0].a[A_IO] + IO_ENTRY));
    side = 1; b = tfp_pose_entry_position_stats(reinterpret_cast<TfpPoseEntryPositionData *>(W[1].a[A_IO] + IO_ENTRY));
    if (a != b) fail("entry_position_stats returned %u in retail, %u in the translation", a, b);
    compare_bytes("entry position stats", W[0].a[A_IO] + IO_ENTRY, W[1].a[A_IO] + IO_ENTRY, 16, ENTRY_NAMES, 4);
    compare_events("while reading the statistics");
    if (NADSE_Error() != retail_error || tfp_pose_error() != translation_error) fail("reading the statistics changed an error code");
  }
  void finish(int w) {
    if (inject.kept_length[w]) { protect(inject.kept_at[w], inject.kept_length[w], PROT_READ | PROT_WRITE); inject.kept_length[w] = 0; }
    // The original's seam changes the protection of the request's page itself (cpp:755-761); put it back.
    if (w == 0 && fault_pending && !::fixture_input_fault_after_query) fault_at[0] = (W[0].request(s.row) + 0x80) & ~0xfffull;
    if (fault_at[w]) { protect(fault_at[w], 0x1000, PROT_READ | PROT_WRITE); fault_at[w] = 0; }
  }
  bool fault_pending = false;
  unsigned allocation_failures = 0; // this many of the next call's allocations are refused (for each helper)
  template <class Retail, class Translation> uint32_t both(const char *text, bool probe_here, Retail retail, Translation translation) {
    ++call_index; call_text = text;
    for (int w = 0; w < 2; ++w) { events[w].clear(); reads_in_call[w] = 0; }
    inject.active = probe_here && inject.mode != INJECT_NONE;
    const uint32_t csr = _mm_getcsr();
    for (int w = 0; w < 2; ++w) for (const auto &b : blocked) protect(W[w].at(b.first), b.second, PROT_NONE);
    side = 0; shim_allocate_failures = allocation_failures; const uint32_t a = retail(); const uint32_t csr_a = _mm_getcsr(); _mm_setcsr(csr); finish(0);
    side = 1; shim_allocate_failures = allocation_failures; const uint32_t b = translation(); const uint32_t csr_b = _mm_getcsr(); _mm_setcsr(csr); finish(1);
    shim_allocate_failures = 0; allocation_failures = 0;
    for (int w = 0; w < 2; ++w) for (const auto &b : blocked) protect(W[w].at(b.first), b.second, PROT_READ | PROT_WRITE);
    blocked.clear();
    if (!::fixture_input_fault_after_query && !translation_fault_after_query) fault_pending = false;
    inject.active = false;
    if (probe_here) { if (discovering) shared->probe_reads = static_cast<uint64_t>(reads_in_call[0]); inject.mode = INJECT_NONE; }
    if (a != b) fail("return value: retail %u, translation %u", a, b);
    if (csr_a != csr_b) fail("the floating-point control word after the call: retail %#x, translation %#x", csr_a, csr_b);
    compare_events("during the call");
    if (translation_cas_calls != ::fixture_cas_calls) fail("dispatch slot swaps attempted so far: retail %u, translation %u", ::fixture_cas_calls, translation_cas_calls);
    compare_slots(); compare_presets();
    if (lock_depth[0] || lock_depth[1] || lock_depth[2]) fail("the translation returned holding a lock (control %d, policy %d, shared %d)", lock_depth[0], lock_depth[1], lock_depth[2]);
    observe();
    ++counted.calls;
    // A refused control call, by its error code (hook calls answer nothing).
    const uint32_t code = NADSE_Error(); if (!hook_call && a != 1 && code < 19) ++errors[code];
    hook_call = false;
    return result = a;
  }
  uint64_t errors[19] = {};
  bool hook_call = false;
  bool take_probe() { const bool here = probing; probing = false; return here; }
  // The next call is the probed one: in the discovery run its reads are counted; otherwise the chosen read
  // fails (and heals, or stays unreadable to the end of the call), or the chosen value changes just before it.
  void probe() {
    probing = true;
    if (discovering) shared->probe_values = probe_values.size();
    if (probe_mode == INJECT_NONE) return;
    inject.mode = probe_mode; inject.index = probe_index;
    if (probe_mode == INJECT_VALUE) { inject.at = probe_values[probe_value].at; inject.value = probe_values[probe_value].value; }
  }

  // ---- the exports ----
  void write_config(int w) {
    NADSE_Config c{};
    c.size = size; c.abi = abi; c.game = W[w].a[A_GAME] + game_delta; c.exe = W[w].a[A_EXE] + exe_delta;
    c.context = W[w].a[A_CONTEXT] + context_delta; c.request = W[w].request(s.row) + request_delta; c.avatar_owner = W[w].a[A_OWNER] + owner_delta;
    c.avatar_index = index; c.avatar = avatar; c.weapon = weapon; c.body = body; c.viewmodel = viewmodel; c.duration_ms = duration;
    c.weapon_descriptor = wdesc_absolute ? wdesc_absolute : W[w].a[A_WDESC] + wdesc_delta; c.weapon_unit = weapon_unit; c.blend_ms = blend;
    c.offset_x = offset[0]; c.offset_y = offset[1]; c.offset_z = offset[2]; c.reserved = reserved_field;
    World::put(W[w].a[A_IO] + IO_CONFIG, c);
  }
  template <class C> C *config(int w) { return config_null ? nullptr : reinterpret_cast<C *>(W[w].a[A_IO] + IO_CONFIG); }
  uint64_t *generation(int w) { return generation_null ? nullptr : reinterpret_cast<uint64_t *>(W[w].a[A_IO] + IO_GENERATION); }
  // kind: 0 begin, 1 begin_steady, 2 begin_input, 3 begin_steady_input
  uint32_t begin(int kind = 0) {
    sync();
    for (int w = 0; w < 2; ++w) { write_config(w); World::put<uint64_t>(W[w].a[A_IO] + IO_GENERATION, UINT64_MAX); }
    static const char *const names[] = {"begin", "begin_steady", "begin_input", "begin_steady_input"};
    both(names[kind], take_probe(),
         [&] { NADSE_Config *c = config<NADSE_Config>(0); uint64_t *g = generation(0);
               return kind == 0 ? NADSE_Begin(c, g) : kind == 1 ? NADSE_BeginSteady(c, g) : kind == 2 ? NADSE_BeginInput(c, g) : NADSE_BeginSteadyInput(c, g); },
         [&] { TfpPoseConfig *c = config<TfpPoseConfig>(1); uint64_t *g = generation(1);
               return kind == 0 ? tfp_pose_begin(c, g) : kind == 1 ? tfp_pose_begin_steady(c, g) : kind == 2 ? tfp_pose_begin_input(c, g) : tfp_pose_begin_steady_input(c, g); });
    uint64_t g[2]; for (int w = 0; w < 2; ++w) std::memcpy(&g[w], reinterpret_cast<void *>(W[w].a[A_IO] + IO_GENERATION), 8);
    if (g[0] != g[1]) fail("generation written by begin: retail %llu, translation %llu", (unsigned long long)g[0], (unsigned long long)g[1]);
    if (result && g[0] && g[0] != UINT64_MAX) { previous_gen = gen; gen = g[0]; }
    return result;
  }
  uint32_t validate(bool input = false) {
    sync(); for (int w = 0; w < 2; ++w) write_config(w);
    return both(input ? "validate_input" : "validate", take_probe(),
                [&] { return input ? NADSE_ValidateInput(config<NADSE_Config>(0)) : NADSE_Validate(config<NADSE_Config>(0)); },
                [&] { return input ? tfp_pose_validate_input(config<TfpPoseConfig>(1)) : tfp_pose_validate(config<TfpPoseConfig>(1)); });
  }
#define DP_TEXT(...) char t[200]; std::snprintf(t, sizeof t, __VA_ARGS__)
  uint32_t end(uint64_t g) { sync(); DP_TEXT("end(%llu)", (unsigned long long)g); return both(t, take_probe(), [&] { return NADSE_End(g); }, [&] { return tfp_pose_end(g); }); }
  uint32_t placement(uint64_t g, float x, float y, float z) { sync(); DP_TEXT("set_placement(%llu, %g, %g, %g)", (unsigned long long)g, x, y, z);
    return both(t, take_probe(), [&] { return NADSE_SetPlacement(g, x, y, z); }, [&] { return tfp_pose_set_placement(g, x, y, z); }); }
  uint32_t ads_placement(uint64_t g, float x, float y, float z) { sync(); DP_TEXT("set_ads_placement(%llu, %g, %g, %g)", (unsigned long long)g, x, y, z);
    return both(t, take_probe(), [&] { return NADSE_SetADSPlacement(g, x, y, z); }, [&] { return tfp_pose_set_ads_placement(g, x, y, z); }); }
  uint32_t motion(uint64_t g, float x, float y, float z) { sync(); DP_TEXT("set_motion(%llu, %g, %g, %g)", (unsigned long long)g, x, y, z);
    return both(t, take_probe(), [&] { return NADSE_SetMotion(g, x, y, z); }, [&] { return tfp_pose_set_motion(g, x, y, z); }); }
  uint32_t transform(uint64_t g, float x, float y, float z, float qx, float qy, float qz, float qw, bool authored = false) {
    sync(); DP_TEXT("set_%stransform(%llu, %g, %g, %g, q %.9g %.9g %.9g %.9g)", authored ? "authored_" : "", (unsigned long long)g, x, y, z, qx, qy, qz, qw);
    return both(t, take_probe(), [&] { return authored ? NADSE_SetAuthoredTransform(g, x, y, z, qx, qy, qz, qw) : NADSE_SetTransform(g, x, y, z, qx, qy, qz, qw); },
                [&] { return authored ? tfp_pose_set_authored_transform(g, x, y, z, qx, qy, qz, qw) : tfp_pose_set_transform(g, x, y, z, qx, qy, qz, qw); }); }
  uint32_t zoom_policy(uint64_t g, uint32_t active) { sync(); DP_TEXT("set_zoom_policy(%llu, %u)", (unsigned long long)g, active);
    return both(t, take_probe(), [&] { return NADSE_SetZoomPolicy(g, active); }, [&] { return tfp_pose_set_zoom_policy(g, active); }); }
  uint32_t transition(uint64_t g, uint64_t epoch, float progress, uint32_t ads) { sync(); DP_TEXT("set_transition(%llu, epoch %llu, progress %.9g, ads %u)", (unsigned long long)g, (unsigned long long)epoch, progress, ads);
    return both(t, take_probe(), [&] { return NADSE_SetTransition(g, epoch, progress, ads); }, [&] { return tfp_pose_set_transition(g, epoch, progress, ads); }); }
  uint32_t rotation_guard(uint32_t enabled) { DP_TEXT("set_rotation_guard(%u)", enabled); return both(t, false, [&] { return NADSE_SetRotationGuard49(enabled); }, [&] { return tfp_pose_set_rotation_guard(enabled); }); }
  uint32_t entry_position(uint32_t enabled) { DP_TEXT("set_entry_position(%u)", enabled); return both(t, false, [&] { return NADSE_SetEntryPosition50(enabled); }, [&] { return tfp_pose_set_entry_position(enabled); }); }
  uint32_t entry_animation(uint32_t ms, float drop, float back) { DP_TEXT("set_entry_animation(%u, %g, %g)", ms, drop, back);
    return both(t, false, [&] { return NADSE_SetEntryAnimation50(ms, drop, back); }, [&] { return tfp_pose_set_entry_animation(ms, drop, back); }); }
  // The statistics calls with a generation and a header of the script's choosing (observe() uses the right ones).
  // header: 0 good, 1 wrong size, 2 wrong abi, 3 reserved field set, 4 null pointer, 5 a page that cannot be written
  uint32_t stats_call(int which, uint64_t g, int header = 0) {
    static const uint32_t sizes[3] = {80, 104, 96}, abis[3] = {2, 1, 1};
    static const char *const names[3] = {"read_stats", "read_depth_stats", "read_input_stats"};
    sync();
    for (int w = 0; w < 2; ++w) {
      const uint64_t io = W[w].a[A_IO] + IO_SCRIPT; std::memset(reinterpret_cast<void *>(io), 0, 0x100);
      World::put<uint32_t>(io, header == 1 ? sizes[which] - 1 : sizes[which]); World::put<uint32_t>(io + 4, header == 2 ? abis[which] + 1 : abis[which]);
      if (header == 3) World::put<uint32_t>(io + 28, 7);
      if (header == 5) protect(io, 0x100, PROT_READ);
    }
    DP_TEXT("%s(%llu), header case %d", names[which], (unsigned long long)g, header);
    auto out = [&](int w) { return header == 4 ? 0 : W[w].a[A_IO] + IO_SCRIPT; };
    both(t, take_probe(),
         [&] { return which == 0 ? NADSE_ReadStats(g, reinterpret_cast<NADSE_Stats *>(out(0))) : which == 1 ? NADSE_ReadDepthStats(g, reinterpret_cast<NADSE_DepthStats *>(out(0)))
                                                                                                             : NADSE_ReadInputStats(g, reinterpret_cast<NADSE_InputStats *>(out(0))); },
         [&] { return which == 0 ? tfp_pose_read_stats(g, reinterpret_cast<TfpPoseStats *>(out(1))) : which == 1 ? tfp_pose_read_depth_stats(g, reinterpret_cast<TfpPoseDepthStats *>(out(1)))
                                                                                                                : tfp_pose_read_input_stats(g, reinterpret_cast<TfpPoseInputStats *>(out(1))); });
    for (int w = 0; w < 2; ++w) protect(W[w].a[A_IO] + IO_SCRIPT, 0x100, PROT_READ | PROT_WRITE);
    compare_bytes("statistics (script's call)", W[0].a[A_IO] + IO_SCRIPT, W[1].a[A_IO] + IO_SCRIPT, sizes[which]);
    return result;
  }
  // The two calls without a generation, into a null pointer or a page that cannot be written.
  void diagnostics_nowhere(bool read_only) {
    for (int w = 0; w < 2; ++w) if (read_only) protect(W[w].a[A_IO] + IO_SCRIPT, 0x1000, PROT_READ);
    auto out = [&](int w) { return read_only ? W[w].a[A_IO] + IO_SCRIPT : 0; };
    both(read_only ? "rotation_trace(read-only page)" : "rotation_trace(null)", false, [&] { return NADSE_RotationTrace49(reinterpret_cast<NADSE_RotationTraceData *>(out(0))); },
         [&] { return tfp_pose_rotation_trace(reinterpret_cast<TfpPoseRotationTraceData *>(out(1))); });
    both(read_only ? "entry_position_stats(read-only page)" : "entry_position_stats(null)", false, [&] { return NADSE_EntryPositionStats50(reinterpret_cast<NADSE_EntryPositionData *>(out(0))); },
         [&] { return tfp_pose_entry_position_stats(reinterpret_cast<TfpPoseEntryPositionData *>(out(1))); });
    for (int w = 0; w < 2; ++w) protect(W[w].a[A_IO] + IO_SCRIPT, 0x1000, PROT_READ | PROT_WRITE);
  }

  // ---- the five hooks ----
  // Before any begin recorded the game's functions, both helpers call a null pointer: such calls are skipped.
  bool hook_possible(int which) { if (::originals[which].load()) return true; ++counted.skipped; return false; }
  uint64_t stack(int w) const { return stack_on ? W[w].stack(inner_frame) : 0; }
  static uint64_t at(int w, Sym s) { return s.id < 0 ? 0 : s.id == 0xffff ? s.offset : W[w].at(s); }
  static std::string where(Sym s) {
    char t[96];
    if (s.id < 0) return "null"; if (s.id == 0xffff) { std::snprintf(t, sizeof t, "%#llx", (unsigned long long)s.offset); return t; }
    std::snprintf(t, sizeof t, "%s+%#llx", A_NAMES[s.id], (unsigned long long)s.offset); return t;
  }
  void position(Sym caller, uint32_t unit, uint32_t node, Sym pose) {
    if (!hook_possible(0)) return;
    sync(); DP_TEXT("position setter from %s: unit %#x, node %u, pose at %s, time %llu", where(caller).c_str(), unit, node, where(pose).c_str(), (unsigned long long)::fixture_now);
    for (int w = 0; w < 2; ++w) game_pose[w] = at(w, pose);
    hook_call = true;
    both(t, take_probe(),
         [&] { shim_return_address = reinterpret_cast<void *>(at(0, caller)); ::fixture_depth_frame = stack(0);
               ::position_hook(unit, node, reinterpret_cast<const float *>(game_pose[0])); return 0u; },
         [&] { tfp_pose_position_from(at(1, caller), unit, node, reinterpret_cast<const float *>(game_pose[1]), stack(1)); return 0u; });
    ++counted.hooks;
  }
  void rotation(Sym caller, uint32_t unit, uint32_t node, Sym pose) {
    if (!hook_possible(1)) return;
    sync(); DP_TEXT("rotation setter from %s: unit %#x, node %u, pose at %s, time %llu", where(caller).c_str(), unit, node, where(pose).c_str(), (unsigned long long)::fixture_now);
    for (int w = 0; w < 2; ++w) game_pose[w] = at(w, pose);
    hook_call = true;
    both(t, take_probe(), [&] { shim_return_address = reinterpret_cast<void *>(at(0, caller)); ::rotation_hook(unit, node, reinterpret_cast<const float *>(game_pose[0])); return 0u; },
         [&] { tfp_pose_rotation_from(at(1, caller), unit, node, reinterpret_cast<const float *>(game_pose[1])); return 0u; });
    ++counted.hooks;
  }
  void branch(Sym caller, uint32_t unit, uint32_t node, Sym pose) {
    if (!hook_possible(2)) return;
    sync(); DP_TEXT("branch rotation setter from %s: unit %#x, node %u, pose at %s, time %llu", where(caller).c_str(), unit, node, where(pose).c_str(), (unsigned long long)::fixture_now);
    for (int w = 0; w < 2; ++w) game_pose[w] = at(w, pose);
    hook_call = true;
    both(t, take_probe(), [&] { shim_return_address = reinterpret_cast<void *>(at(0, caller)); ::branch_rotation_hook(unit, node, reinterpret_cast<const float *>(game_pose[0])); return 0u; },
         [&] { tfp_pose_branch_rotation_from(at(1, caller), unit, node, reinterpret_cast<const float *>(game_pose[1])); return 0u; });
    ++counted.hooks;
  }
  static uint32_t getter_class(int w, const float *result) { return reinterpret_cast<uint64_t>(result) == W[w].a[A_ARG] + ARG_GETTER_RESULT ? 1u : 0u; }
  // The local position getter: through the plain hook (request 0), or as the input-mode thunk enters it (with the caller's R15).
  void getter(Sym caller, uint32_t unit, uint32_t node, bool thunk = false, Sym request = NOWHERE) {
    if (!hook_possible(3)) return;
    sync(); DP_TEXT("position getter%s from %s: unit %#x, node %u, request %s, time %llu", thunk ? " (input thunk)" : "", where(caller).c_str(), unit, node, where(request).c_str(), (unsigned long long)::fixture_now);
    for (int w = 0; w < 2; ++w) game_pose[w] = 0;
    hook_call = true;
    getter_entry = 3;
    const uint32_t got = both(t, take_probe(),
         [&] { ::fixture_depth_frame = stack(0);
               if (thunk) return getter_class(0, NADSE_Input00Call(unit, node, at(0, request), stack(0), at(0, caller)));
               shim_return_address = reinterpret_cast<void *>(at(0, caller)); return getter_class(0, ::position_getter_hook(unit, node)); },
         [&] { return getter_class(1, tfp_pose_position_getter_from(at(1, caller), unit, node, stack(1), thunk ? at(1, request) : 0)); });
    if (got != 1) fail("the getter hook did not return the game's own result");
    ++counted.hooks;
  }
  // The world-matrix getter at UnitAPI+0x90, as the input-mode thunk enters it.
  void input90(Sym caller, uint32_t unit, uint32_t node, Sym request) {
    if (!::input_original.load()) { ++counted.skipped; return; }
    sync(); DP_TEXT("world-matrix getter (input thunk) from %s: unit %#x, node %u, request %s, time %llu", where(caller).c_str(), unit, node, where(request).c_str(), (unsigned long long)::fixture_now);
    for (int w = 0; w < 2; ++w) game_pose[w] = 0;
    hook_call = true;
    getter_entry = 4;
    const uint32_t got = both(t, take_probe(), [&] { return getter_class(0, NADSE_Input90Call(unit, node, at(0, request), stack(0), at(0, caller))); },
                              [&] { return getter_class(1, tfp_pose_input90_from(at(1, caller), unit, node, at(1, request), stack(1))); });
    getter_entry = 3;
    if (got != 1) fail("the world-matrix hook did not return the game's own result");
    ++counted.hooks;
  }
  void tick(int64_t ms) { ::fixture_now += static_cast<uint64_t>(ms); }
  // The original's "the page stops being writable between the query and the swap" seam, for the next 32-bit swap.
  void fault_after_query(uint32_t mode) { ::fixture_input_fault_after_query = mode; translation_fault_after_query = mode; fault_pending = true; }
};

} // namespace dp

// The three entries of native_ads_input.S, which is Windows-convention assembly and is not assembled here.
// The two thunks only capture the caller's R15, stack pointer and return address and call NADSE_Input90Call /
// NADSE_Input00Call; the test calls those two directly with the captured values, as the retail input fixture
// does, so only the thunks' addresses are used (they are what the original stores in a slot).
extern "C" const float *NADSE_Input90Thunk(uint32_t, uint32_t) { std::fprintf(stderr, "the 0x90 thunk was called\n"); std::abort(); }
extern "C" const float *NADSE_Input00Thunk(uint32_t, uint32_t) { std::fprintf(stderr, "the 0x00 thunk was called\n"); std::_Exit(5); }
extern "C" void NADSE_InputCASFault() {}
extern "C" void NADSE_InputCASRecovery() { std::fflush(stdout); }
extern "C" uint32_t NADSE_InputCompareExchange(uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  return dp::cas32(0, at, expected, desired, observed);
}

#include "diff_pose_helper_scripts.h"
