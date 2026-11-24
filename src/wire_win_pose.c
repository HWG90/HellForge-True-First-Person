/* See wire_win_pose.h. "cpp:N" is native/first_person/native_ads_editor.cpp, "S:N"
 * native_ads_input.S, "memory:N" native_memory.h, all at 0.5.41-smooth.66.
 *
 * The helper (src/pose_helper.c) decides and orders every slot swap exactly as retail's DLL; this
 * file gives it Windows. As for the four slots of the view build (wire_win.c): before a swap that
 * installs one of the helper's values, the function the slot points at is identified by its code pin,
 * read through hf->read_code. No pin, no write. No code byte is ever written. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>
#include "wire_win_pose.h"
#include "slot_platform.h"
#include "near_helper.h"
#include "pose_helper.h"
#include "fov_helper.h"
#include "aim_bind.h"

/* The function each pose slot must hold before one of the helper's values goes in: exe RVA and the
 * bytes there. The same numbers as src/pose_helper.c B_EXE_POSITION / _ROTATION / _BRANCH_ROTATION /
 * _LOCAL_POSITION / _WORLD_MATRIX and their B_HEX_* (docs/translation/POSE-HELPER-TRANSLATION.md section 2:
 * build map and the exe image of build 25480438). The helper checks them too, through its host's
 * read; this second check is the binder's own rule and names the slot in the log. */
enum { SLOTS = 5 };
static const struct { const char *name; uint32_t api, rva; const char *hex; } TARGET[SLOTS] = {
    {"unit position setter", 0x38, 0x1fc720, "48895c24084889742410574883ec20498bf08bfa8bd9e88511eaff4c8b5018b801000000f0410fc182380017008bc04c"},
    {"unit rotation setter", 0x48, 0x1fc880, "48895c24084889742410574883ec20498bf08bfa8bd9e82510eaff4c8b5018b801000000f0410fc182380017008bc04c"},
    {"unit branch rotation setter", 0x40, 0x1fc7a0, "48895c24084889742410574881eca0000000440f29b42490000000498bd84863fae8fa10eaff488bc8488bf04c8b0841"},
    {"unit local position getter", 0x00, 0x1fc450,
     "40534883ec204863dae86214eaff488bc84c8b0041ff90e8000000488bcb48c1e1064883c124488b40204803c14883c4205bc3"},
    {"unit world matrix getter", 0x90, 0x1fd220, "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3"},
};
enum { API_ROOT = 0x3326308, API_UNIT = 0x18 }; /* pose_helper.c B_GAME_API_ROOT, L_API_UNIT: *(*(game+0x3326308)+0x18) is the Unit API table */
/* Which slot each of the helper's six values belongs in (TFP_POSE_HOOK_*): the two getter values share +0x00. */
static const int SLOT_OF[TFP_WIN_POSE_HOOKS] = {0, 1, 2, 3, 3, 4};
static const char *const HOOK_NAME[TFP_WIN_POSE_HOOKS] = {"pose position (UnitAPI+0x38)", "pose rotation (UnitAPI+0x48)",
                                                          "pose branch rotation (UnitAPI+0x40)", "pose getter (UnitAPI+0x00)",
                                                          "pose getter, input thunk (UnitAPI+0x00)", "pose input thunk (UnitAPI+0x90)"};
static TfpWinHook hooks[TFP_WIN_POSE_HOOKS];
static uint64_t hook_address[TFP_WIN_POSE_HOOKS];
static SRWLOCK locks[2]; /* control 0, policy 1 (cpp: control_lock, policy_lock) */

const TfpWinHook *tfp_win_pose_hook(int which) { return &hooks[which]; }
const char *tfp_win_pose_hook_name(int which) { return HOOK_NAME[which]; }
void tfp_win_pose_hook_refusal_seen(int which) { hooks[which].refused = 0; }

/* ---- the functions that sit in the slots ----------------------------------------------------------
 * cpp:1137-1141, 1234-1247, 1337-1340, 907-913: each hands the helper what retail's hook captured,
 * its own return address and (position, getter) the caller's stack pointer: the address of its own
 * return address + 8. They run on whatever thread calls the slot, for every unit of the game: they
 * touch their arguments, the helper, and one word that is written once (the first caller, for the
 * log). No counter, no lock, no log, no engine call. */
static void fired(int which, uint64_t return_address) {
  if (hooks[which].first_thread) return;
  hooks[which].first_return = return_address;
  hooks[which].calls = 1;
  hooks[which].first_thread = GetCurrentThreadId();
}
static __declspec(noinline) void position_hook(uint32_t unit, uint32_t node, const float *pose) {
  uint64_t from = (uint64_t)(uintptr_t)_ReturnAddress(), stack = (uint64_t)(uintptr_t)_AddressOfReturnAddress() + 8;
  fired(TFP_POSE_HOOK_POSITION, from);
  tfp_pose_position_from(from, unit, node, pose, stack);
}
static __declspec(noinline) void rotation_hook(uint32_t unit, uint32_t node, const float *pose) {
  uint64_t from = (uint64_t)(uintptr_t)_ReturnAddress();
  fired(TFP_POSE_HOOK_ROTATION, from);
  tfp_pose_rotation_from(from, unit, node, pose);
}
static __declspec(noinline) void branch_rotation_hook(uint32_t unit, uint32_t node, const float *pose) {
  uint64_t from = (uint64_t)(uintptr_t)_ReturnAddress();
  fired(TFP_POSE_HOOK_BRANCH_ROTATION, from);
  tfp_pose_branch_rotation_from(from, unit, node, pose);
}
static __declspec(noinline) const float *position_getter_hook(uint32_t unit, uint32_t node) {
  uint64_t from = (uint64_t)(uintptr_t)_ReturnAddress(), stack = (uint64_t)(uintptr_t)_AddressOfReturnAddress() + 8;
  fired(TFP_POSE_HOOK_GETTER, from);
  return tfp_pose_position_getter_from(from, unit, node, stack, 0);
}
/* src/pose_input_thunks.S (S:5-19, S:43-57): the two values of input mode. */
const float *tfp_pose_input90_thunk(uint32_t unit, uint32_t node);
const float *tfp_pose_input00_thunk(uint32_t unit, uint32_t node);

/* ---- the 32-bit compare-exchange of the look-gain path (S:24-41, cpp:31-44) ----------------------- */
uint32_t tfp_pose_input_compare_exchange(uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed);
void tfp_pose_input_cas_fault(void);
void tfp_pose_input_cas_recovery(void);
/* Named by the .seh_handler of tfp_pose_input_compare_exchange. Static unwind data covers exactly
 * this DLL's one atomic write; every other fault goes on through Windows' exception search. */
EXCEPTION_DISPOSITION tfp_pose_input_cas_exception(EXCEPTION_RECORD *exception, void *frame, CONTEXT *context, void *dispatcher) {
  (void)frame; (void)dispatcher;
  if (exception && context && exception->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && exception->ExceptionFlags == 0 &&
      exception->NumberParameters >= 2 && exception->ExceptionInformation[0] <= 1 &&
      exception->ExceptionAddress == (void *)(uintptr_t)tfp_pose_input_cas_fault &&
      context->Rip == (uint64_t)(uintptr_t)tfp_pose_input_cas_fault && exception->ExceptionInformation[1] >= context->Rcx &&
      exception->ExceptionInformation[1] < context->Rcx + 4) {
    context->Rip = (uint64_t)(uintptr_t)tfp_pose_input_cas_recovery;
    return ExceptionContinueExecution;
  }
  return ExceptionContinueSearch;
}

/* ---- the host --------------------------------------------------------------------------------------- */
static bool host_read(void *u, uint64_t at, void *out, size_t length) { (void)u; return tfp_win_read(at, out, length); } /* cpp:156 */
static uint64_t host_tick(void *u) { (void)u; return GetTickCount64(); }                                             /* cpp:265 */
static uint32_t host_thread(void *u) { (void)u; return GetCurrentThreadId(); }
static bool host_writable_slot(void *u, uint64_t at, uint64_t exe) { (void)u; return tfp_win_writable_slot(at, exe); } /* cpp:273-277 */
static bool host_executable(void *u, uint64_t at) { (void)u; return tfp_win_executable(at); }                          /* cpp:178-182 */
static bool host_write(void *u, void *out, const void *data, size_t length) { (void)u; return tfp_win_write(out, data, length); }
static bool host_pin(void *u, uint64_t address) { (void)u; return tfp_win_pin(address); }                              /* cpp:1341-1344 */
static bool host_writable32(void *u, uint64_t at) { (void)u; return tfp_win_writable32(at); }                          /* cpp:752-754 */
static bool host_cas32(void *u, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  (void)u;
  return tfp_pose_input_compare_exchange(at, expected, desired, observed) != 0;
}
static bool host_region(void *u, uint64_t at, uint64_t *base, uint64_t *size, bool *is_free) { /* cpp:726-730 */
  MEMORY_BASIC_INFORMATION r;
  (void)u;
  if (VirtualQuery((const void *)(uintptr_t)at, &r, sizeof r) != sizeof r) return false;
  *base = (uint64_t)(uintptr_t)r.BaseAddress, *size = (uint64_t)r.RegionSize, *is_free = r.State == MEM_FREE;
  return true;
}
static uint64_t host_allocate(void *u, uint64_t at, uint64_t size) { /* cpp:733 */
  (void)u;
  return (uint64_t)(uintptr_t)VirtualAlloc((void *)(uintptr_t)at, (SIZE_T)size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
static void host_lock_exclusive(void *u, int lock) { (void)u; AcquireSRWLockExclusive(&locks[lock]); }
static void host_unlock_exclusive(void *u, int lock) { (void)u; ReleaseSRWLockExclusive(&locks[lock]); }
static void host_lock_shared(void *u, int lock) { (void)u; AcquireSRWLockShared(&locks[lock]); }
static void host_unlock_shared(void *u, int lock) { (void)u; ReleaseSRWLockShared(&locks[lock]); }

/* InterlockedCompareExchangePointer inside __try (cpp:301-305): false only when it faulted. */
static bool host_cas(void *u, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  int installing = -1, removing = -1;
  (void)u;
  for (int i = 0; i < TFP_WIN_POSE_HOOKS; i++) {
    if (desired == hook_address[i]) installing = i;
    if (expected == hook_address[i]) removing = i;
  }
  if (installing >= 0) {
    const int target = SLOT_OF[installing];
    if (expected != tfp_win_exe() + TARGET[target].rva || !tfp_win_code_is(expected, TARGET[target].hex)) {
      /* Not the pinned function: answer as a swap that found another value and write nothing. The
       * helper rolls back and reports "slot ownership changed" (11); the tick logs why. */
      hooks[installing].refused = 1;
      *observed = 0;
      return true;
    }
  }
  uintptr_t found = 0;
  if (!fp_atomic_pointer((uintptr_t)slot, (uintptr_t)expected, (uintptr_t)desired, &found)) return false;
  *observed = found;
  if (found == expected) {
    if (installing >= 0) hooks[installing].slot = slot, hooks[installing].original = expected;
    if (removing >= 0) hooks[removing].slot = 0;
  }
  return true;
}
unsigned tfp_win_pose_restore_slots(void) {
  unsigned restored = 0;
  for (int i = 0; i < TFP_WIN_POSE_HOOKS; i++) {
    uintptr_t found = 0;
    if (!hooks[i].slot) continue;
    if (fp_atomic_pointer((uintptr_t)hooks[i].slot, (uintptr_t)hook_address[i], (uintptr_t)hooks[i].original, &found) &&
        found == hook_address[i]) restored++;
    hooks[i].slot = 0;
  }
  return restored;
}

int tfp_win_pose_check(char *text, size_t size) {
  const uint64_t exe = tfp_win_exe(), game = tfp_win_game();
  uint64_t root = 0, api = 0;
  for (int i = 0; i < SLOTS; i++) {
    uint8_t probe[8];
    if (!tfp_win_read(exe + TARGET[i].rva, probe, sizeof probe)) {
      snprintf(text, size, "the %s (exe+%#x) is not readable yet", TARGET[i].name, (unsigned)TARGET[i].rva);
      return -1;
    }
    if (!tfp_win_code_is(exe + TARGET[i].rva, TARGET[i].hex)) {
      snprintf(text, size, "the %s (exe+%#x) does not match its pin of build 25480438", TARGET[i].name, (unsigned)TARGET[i].rva);
      return 0;
    }
  }
  if (!tfp_win_read(game + API_ROOT, &root, 8) || root < 65536 || !tfp_win_read(root + API_UNIT, &api, 8) || api < 65536) {
    snprintf(text, size, "the Unit API table (game+%#x) is not readable yet", (unsigned)API_ROOT);
    return -1;
  }
  for (int i = 0; i < SLOTS; i++) {
    uint64_t now = 0;
    if (!tfp_win_read(api + TARGET[i].api, &now, 8)) {
      snprintf(text, size, "UnitAPI+%#x is not readable yet", (unsigned)TARGET[i].api);
      return -1;
    }
    if (now != exe + TARGET[i].rva) {
      bool ours = false;
      for (int k = 0; k < TFP_WIN_POSE_HOOKS; k++) ours = ours || now == hook_address[k];
      if (ours) continue; /* the helper's own value: a lease is running */
      snprintf(text, size, "UnitAPI+%#x holds %#llx, not the %s (exe+%#x): another mod owns the slot", (unsigned)TARGET[i].api,
               (unsigned long long)now, TARGET[i].name, (unsigned)TARGET[i].rva);
      return 0;
    }
  }
  snprintf(text, size, "UnitAPI %#llx: +0x38, +0x48, +0x40, +0x00 and +0x90 hold the five pinned functions", (unsigned long long)api);
  return 1;
}

const TfpPoseAdapterIo *tfp_win_pose_io(void) {
  static const TfpPoseAdapterIo io = {NULL, NULL, host_tick};
  return &io;
}
void tfp_win_pose_hosts(void) {
  hook_address[TFP_POSE_HOOK_POSITION] = (uint64_t)(uintptr_t)position_hook;
  hook_address[TFP_POSE_HOOK_ROTATION] = (uint64_t)(uintptr_t)rotation_hook;
  hook_address[TFP_POSE_HOOK_BRANCH_ROTATION] = (uint64_t)(uintptr_t)branch_rotation_hook;
  hook_address[TFP_POSE_HOOK_GETTER] = (uint64_t)(uintptr_t)position_getter_hook;
  hook_address[TFP_POSE_HOOK_GETTER_INPUT] = (uint64_t)(uintptr_t)tfp_pose_input00_thunk;
  hook_address[TFP_POSE_HOOK_INPUT90] = (uint64_t)(uintptr_t)tfp_pose_input90_thunk;
  TfpPoseHost host;
  memset(&host, 0, sizeof host); /* native_exp, forward_setter, forward_getter NULL: the game's functions are called directly, as retail does */
  memcpy(host.hooks, hook_address, sizeof host.hooks);
  host.read = host_read, host.tick = host_tick, host.thread = host_thread;
  host.writable_slot = host_writable_slot, host.executable = host_executable;
  host.cas = host_cas, host.write = host_write, host.pin = host_pin;
  host.lock_exclusive = host_lock_exclusive, host.unlock_exclusive = host_unlock_exclusive;
  host.lock_shared = host_lock_shared, host.unlock_shared = host_unlock_shared;
  host.writable32 = host_writable32, host.cas32 = host_cas32, host.region = host_region, host.allocate = host_allocate;
  /* Round 20: the frame's kind-6 lens to the field-of-view helper, and the near clip read from the pose helper in each
   * of the near helper's own calls (src/aim_bind.h, tested there). */
  tfp_aim_bind_pose(&host);
  tfp_pose_set_host(&host);
}
