/* See wire_win.h. Each function says which retail lines it stands for ("camera:N" is
 * native/first_person/native_ads_camera.cpp, "memory:N" native_memory.h, "backend:N" backend.lua).
 *
 * Engine API slot swaps: the four helpers decide and order them exactly as retail (their C is
 * retail's, proven); this file only gives them the compare-and-swap. Before a swap that installs a
 * hook, the function the slot points at is identified by its code pin, read through hf->read_code
 * (the bytes from before any HellForge hook): no pin, no write. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "wire_win.h"
#include "camera_helper.h"
#include "fov_helper.h"
#include "near_helper.h"
#include "blur_helper.h"
#include "slot_platform.h"

static const HfApi *hf;
static HfMod *owner;
static uint64_t game_base, exe_base;
static size_t game_size, exe_size;
static bool wine, has_read_code;
static struct { uint64_t from, to; } code[32];
static unsigned code_ranges;
static SRWLOCK locks[TFP_WIN_HOOKS][2];
static TfpWinHook hooks[TFP_WIN_HOOKS];
static uint64_t hook_address[TFP_WIN_HOOKS];

/* The function each dispatch slot must point at before this mod's hook goes in: exe RVA and the
 * bytes there. camera, fov, near: src/native_contract.h (verified in the game by the probe); the
 * weapon parameter setter: src/blur_helper.c B_EXE_SETTER / B_HEX_SETTER. All four compared with the
 * exe image of build 25480438 on 2026-11-16 (docs/WIRING-VIEW.md). */
static const struct { const char *name; uint32_t rva; const char *hex; } TARGET[TFP_WIN_HOOKS] = {
    {"camera position setter", 0x30deb0, "48895c2408574883ec60488bd9498bf88bcae8f9f9d8ff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920"},
    {"camera FOV setter", 0x30d990, "f30f114934c7415000000000e90f211c00"},
    {"camera near setter", 0x30d960, "f30f114928e9a6201c00"},
    {"weapon parameter setter", 0x310050, "48895c24084889742410574883ec404c8b11498bd94c8bca418bf8448bc249c1e920488d542460488bf141ff5238"},
};
static const char *const HOOK_NAME[TFP_WIN_HOOKS] = {"camera", "fov", "near", "blur"};

uint64_t tfp_win_game(void) { return game_base; }
uint64_t tfp_win_exe(void) { return exe_base; }
const TfpWinHook *tfp_win_hook(int which) { return &hooks[which]; }
const char *tfp_win_hook_name(int which) { return HOOK_NAME[which]; }
void tfp_win_hook_refusal_seen(int which) { hooks[which].refused = 0; }

/* ---- memory ---------------------------------------------------------------------------------- */
static bool is_code(uint64_t at, size_t size) {
  for (unsigned i = 0; i < code_ranges; i++)
    if (at >= code[i].from && at + size <= code[i].to) return true;
  return false;
}
bool tfp_win_read(uint64_t at, void *out, size_t size) {
  if (at < 65536 || at >= UINT64_C(0x800000000000) || !size || at + size >= UINT64_C(0x800000000000)) return false;
  if (has_read_code && is_code(at, size)) return hf->read_code((uintptr_t)at, out, size) == 0;
  return hf->read((uintptr_t)at, out, size) == 0;
}
static bool host_read(void *u, uint64_t at, void *out, size_t size) { (void)u; return tfp_win_read(at, out, size); }
static void add_code(uint64_t base) { /* the executable sections of a loaded image */
  uint8_t dos[64], nt[24], section[40];
  if (!tfp_win_read(base, dos, sizeof dos) || dos[0] != 'M' || dos[1] != 'Z') return;
  uint32_t pe;
  memcpy(&pe, dos + 0x3c, 4);
  if (pe > 0x1000 || !tfp_win_read(base + pe, nt, sizeof nt)) return;
  uint16_t count, optional;
  memcpy(&count, nt + 6, 2);
  memcpy(&optional, nt + 20, 2);
  for (uint16_t i = 0; i < count && i < 64 && code_ranges < sizeof code / sizeof *code; i++) {
    uint32_t size, rva, flags;
    if (!tfp_win_read(base + pe + 24 + optional + (uint64_t)i * 40, section, sizeof section)) return;
    memcpy(&size, section + 8, 4);
    memcpy(&rva, section + 12, 4);
    memcpy(&flags, section + 36, 4);
    if (!(flags & 0x20000000u)) continue; /* IMAGE_SCN_MEM_EXECUTE */
    code[code_ranges].from = base + rva;
    code[code_ranges++].to = base + rva + size;
  }
}
bool tfp_win_setup(const HfApi *api, HfMod *mod) {
  hf = api;
  owner = mod;
  has_read_code = api->size >= offsetof(HfApi, read_code) + sizeof api->read_code && api->read_code;
  game_base = api->module_base("game.dll", &game_size);
  exe_base = api->module_base("helldivers2.exe", &exe_size);
  if (!game_base || !exe_base) return false;
  wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != NULL; /* memory:5, backend:62 */
  code_ranges = 0;
  add_code(game_base);
  add_code(exe_base);
  return code_ranges > 0;
}

/* memory:2-13 */
static bool section_mapping(const MEMORY_BASIC_INFORMATION *r) { return r->Type == MEM_IMAGE || (wine && r->Type == MEM_MAPPED); }
/* camera:164-178 */
static bool host_readable_span(void *u, uint64_t at, size_t length) {
  const uint64_t end = at + length;
  uint64_t cursor = at;
  (void)u;
  for (unsigned regions = 0; cursor < end && regions < 4; regions++) {
    MEMORY_BASIC_INFORMATION r;
    if (VirtualQuery((const void *)(uintptr_t)cursor, &r, sizeof r) != sizeof r || r.State != MEM_COMMIT ||
        (r.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD p = r.Protect & 0xff;
    if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READ && p != PAGE_EXECUTE_READWRITE &&
        p != PAGE_EXECUTE_WRITECOPY) return false;
    const uint64_t base = (uint64_t)(uintptr_t)r.BaseAddress, next = base + r.RegionSize;
    if (base > cursor || next <= cursor || next > UINT64_C(0x800000000000)) return false;
    cursor = next < end ? next : end;
  }
  return cursor == end;
}
/* camera:432-445 (the alignment test is the helper's) */
static bool host_writable_slot(void *u, uint64_t at, uint64_t exe) {
  MEMORY_BASIC_INFORMATION r;
  (void)u;
  if (VirtualQuery((const void *)(uintptr_t)at, &r, sizeof r) != sizeof r || r.State != MEM_COMMIT ||
      (r.Protect != PAGE_READWRITE && r.Protect != PAGE_WRITECOPY) || at + 8 > (uint64_t)(uintptr_t)r.BaseAddress + r.RegionSize) return false;
  return section_mapping(&r) && (uint64_t)(uintptr_t)r.AllocationBase == exe;
}
/* native_ads_fov.cpp:355-361 */
static bool host_writable_request_field(void *u, uint64_t at) {
  MEMORY_BASIC_INFORMATION r;
  (void)u;
  return !(at & 3) && VirtualQuery((const void *)(uintptr_t)at, &r, sizeof r) == sizeof r && r.State == MEM_COMMIT &&
         (r.Protect == PAGE_READWRITE || r.Protect == PAGE_WRITECOPY) && at + 4 <= (uint64_t)(uintptr_t)r.BaseAddress + r.RegionSize;
}
/* camera:228-232 */
static bool host_executable(void *u, uint64_t at) {
  MEMORY_BASIC_INFORMATION r;
  (void)u;
  if (VirtualQuery((const void *)(uintptr_t)at, &r, sizeof r) != sizeof r || r.State != MEM_COMMIT || !section_mapping(&r)) return false;
  return r.Protect == PAGE_EXECUTE_READ || r.Protect == PAGE_EXECUTE_READWRITE || r.Protect == PAGE_EXECUTE_WRITECOPY;
}
static bool host_write(void *u, void *out, const void *data, size_t length) { /* WriteProcessMemory on this process */
  SIZE_T written = 0;
  (void)u;
  return WriteProcessMemory(GetCurrentProcess(), out, data, length, &written) && written == length;
}
static bool host_pin(void *u, uint64_t address) { /* camera:1059-1062 */
  HMODULE module = NULL;
  (void)u;
  return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)(uintptr_t)address, &module) != 0;
}

/* ---- clocks (camera:139-151, fov:81-90, near) -------------------------------------------------- */
static double frequency(void) {
  static double value;
  if (value <= 0) {
    LARGE_INTEGER f;
    value = QueryPerformanceFrequency(&f) && f.QuadPart > 0 ? (double)f.QuadPart : -1;
  }
  return value;
}
static bool qpc_seconds(double *out) {
  LARGE_INTEGER ticks;
  if (frequency() <= 0 || !QueryPerformanceCounter(&ticks)) return false;
  *out = (double)ticks.QuadPart / frequency();
  return true;
}
static double camera_now(void *u) {
  double seconds;
  (void)u;
  return qpc_seconds(&seconds) ? seconds * 1000.0 : (double)GetTickCount64();
}
static bool fov_seconds(void *u, double *out) { (void)u; return qpc_seconds(out); }
static uint64_t near_now_us(void *u) {
  double seconds;
  (void)u;
  return qpc_seconds(&seconds) ? (uint64_t)(seconds * 1000000.0) : GetTickCount64() * 1000u;
}
static double platform_clock(void *u) {
  double seconds;
  (void)u;
  return qpc_seconds(&seconds) ? seconds : (double)NAN;
}

/* ---- the dispatch slots ------------------------------------------------------------------------ */
static bool pinned_target(int which, uint64_t target) {
  uint8_t now[64], want[64];
  size_t n = strlen(TARGET[which].hex) / 2;
  if (target != exe_base + TARGET[which].rva || n > sizeof now || !has_read_code) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned byte = 0;
    sscanf(TARGET[which].hex + i * 2, "%2x", &byte);
    want[i] = (uint8_t)byte;
  }
  return hf->read_code((uintptr_t)target, now, n) == 0 && !memcmp(now, want, n);
}
/* InterlockedCompareExchangePointer inside __try (camera:452-464): false only when it faulted. */
static bool host_cas(void *u, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  int installing = -1, removing = -1;
  (void)u;
  for (int i = 0; i < TFP_WIN_HOOKS; i++) {
    if (desired == hook_address[i]) installing = i;
    if (expected == hook_address[i]) removing = i;
  }
  if (installing >= 0 && !pinned_target(installing, expected)) {
    /* The function behind the slot is not the pinned one: answer as a swap that found another
     * value, write nothing. The helper reports its "slot changed" error; the tick logs why. */
    hooks[installing].refused = 1;
    *observed = 0;
    return true;
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
typedef void (*Cas32)(uint64_t, uint32_t, uint32_t, uint32_t *);
static __declspec(noinline) void cas32_raw(uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  *observed = (uint32_t)InterlockedCompareExchange((LONG volatile *)(uintptr_t)at, (LONG)desired, (LONG)expected);
}
static Cas32 volatile cas32_dispatch = cas32_raw; /* as slot_atomic.c: keeps the SEH scope around the intrinsic */
static bool host_cas32(void *u, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  (void)u;
  __try { cas32_dispatch(at, expected, desired, observed); return true; }
  __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned tfp_win_restore_slots(void) {
  unsigned restored = 0;
  for (int i = 0; i < TFP_WIN_HOOKS; i++) {
    uintptr_t found = 0;
    if (!hooks[i].slot) continue;
    if (fp_atomic_pointer((uintptr_t)hooks[i].slot, (uintptr_t)hook_address[i], (uintptr_t)hooks[i].original, &found) &&
        found == hook_address[i]) restored++;
    hooks[i].slot = 0;
  }
  return restored;
}

/* ---- locks: retail's two SRW locks per helper (control 0, policy 1) ---------------------------- */
#define LOCKS(name, index) \
  static void name##_lock_exclusive(void *u, int lock) { (void)u; AcquireSRWLockExclusive(&locks[index][lock]); } \
  static void name##_unlock_exclusive(void *u, int lock) { (void)u; ReleaseSRWLockExclusive(&locks[index][lock]); } \
  static void name##_lock_shared(void *u, int lock) { (void)u; AcquireSRWLockShared(&locks[index][lock]); } \
  static void name##_unlock_shared(void *u, int lock) { (void)u; ReleaseSRWLockShared(&locks[index][lock]); }
LOCKS(camera, TFP_WIN_CAMERA)
LOCKS(fov, TFP_WIN_FOV)
LOCKS(near_clip, TFP_WIN_NEAR)
LOCKS(blur, TFP_WIN_BLUR)

/* ---- the functions that sit in the slots -------------------------------------------------------
 * Each hands its own return address to the helper (retail: _ReturnAddress() inside the hook) and
 * touches nothing else. They run on whatever thread the game calls the slot from; the helper's
 * locks are the protection, as in retail. The first call's thread and caller are kept for the log. */
static void fired(int which, uint64_t return_address) {
  if (!hooks[which].first_thread) {
    hooks[which].first_return = return_address;
    hooks[which].first_thread = GetCurrentThreadId();
  }
  hooks[which].calls++;
}
static __declspec(noinline) void camera_hook(void *camera, uint32_t unit, const float *native) {
  uint64_t from = (uint64_t)(uintptr_t)__builtin_return_address(0);
  fired(TFP_WIN_CAMERA, from);
  tfp_camera_setter_from(from, camera, unit, native);
}
static __declspec(noinline) void fov_hook(void *camera, float radians) {
  uint64_t from = (uint64_t)(uintptr_t)__builtin_return_address(0);
  fired(TFP_WIN_FOV, from);
  tfp_fov_setter_from(from, camera, radians);
}
static __declspec(noinline) void near_hook(void *camera, float meters) {
  uint64_t from = (uint64_t)(uintptr_t)__builtin_return_address(0);
  fired(TFP_WIN_NEAR, from);
  tfp_near_setter_from(from, camera, meters);
}
static __declspec(noinline) void blur_hook(void *provider, uint64_t instance, uint32_t hash, const void *variant) {
  uint64_t from = (uint64_t)(uintptr_t)__builtin_return_address(0);
  fired(TFP_WIN_BLUR, from);
  tfp_blur_setter_from(from, provider, instance, hash, variant);
}

/* ---- the backend's host (backend.lua _windows, :43-64, :438-456) -------------------------------- */
static uint64_t ops_module(void *u, TfpOpsModule which) { (void)u; return which == TFP_OPS_GAME ? game_base : exe_base; }
static bool ops_write(void *u, uint64_t at, const void *bytes, size_t size) { (void)u; return hf->write((uintptr_t)at, bytes, size) == 0; }
static void ops_query(void *u, uint64_t at, TfpOpsRegion *out) {
  MEMORY_BASIC_INFORMATION r;
  (void)u;
  memset(out, 0, sizeof *out);
  if (VirtualQuery((const void *)(uintptr_t)at, &r, sizeof r) != sizeof r) return;
  out->answered = true;
  out->state = r.State, out->kind = r.Type, out->protect = r.Protect;
  out->allocation = (uint64_t)(uintptr_t)r.AllocationBase;
}
typedef void (*NativeCall)(TfpOpsNative, uint64_t, const uint64_t *, uint64_t *);
static __declspec(noinline) void native_raw(TfpOpsNative which, uint64_t address, const uint64_t *a, uint64_t *result) {
  void *f = (void *)(uintptr_t)address;
  switch (which) {
    case TFP_OPS_ALLOWED: *result = ((uint8_t (*)(void *, uint32_t))f)((void *)(uintptr_t)a[0], (uint32_t)a[1]); break;
    case TFP_OPS_ENTER: ((void (*)(void *, uint32_t, uint32_t))f)((void *)(uintptr_t)a[0], (uint32_t)a[1], (uint32_t)a[2]); break;
    case TFP_OPS_EXIT: ((void (*)(void *))f)((void *)(uintptr_t)a[0]); break;
    case TFP_OPS_BODY_EXISTS: *result = (uint64_t)((int (*)(uint32_t, uint32_t))f)((uint32_t)a[0], (uint32_t)a[1]); break;
    case TFP_OPS_BODY_TRIGGER: ((void (*)(uint32_t, uint32_t))f)((uint32_t)a[0], (uint32_t)a[1]); break;
    case TFP_OPS_DETACH: ((void (*)(void *, uint32_t))f)((void *)(uintptr_t)a[0], (uint32_t)a[1]); break;
    default: break;
  }
}
static NativeCall volatile native_dispatch = native_raw;
static bool ops_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  uint64_t value = 0;
  bool done = false;
  (void)u;
  if (which == TFP_OPS_PRESENTATION) { /* open on this build (docs/BACKEND-OPS-TRANSLATION.md); the backend refuses before it gets here */
    raised->present = true;
    snprintf(raised->text, sizeof raised->text, "FP presentation is not established on build 25480438");
    return false;
  }
  __try { native_dispatch(which, address, args, &value); done = true; }
  __except (EXCEPTION_EXECUTE_HANDLER) { done = false; }
  if (!done) {
    raised->present = true;
    snprintf(raised->text, sizeof raised->text, "Native call %d at %#llx faulted", (int)which, (unsigned long long)address);
    return false;
  }
  if (result) *result = value;
  return true;
}
static void ops_log(void *u, const char *text) { (void)u; hf->log(owner, "%s", text); }

/* ---- keys and the mouse ------------------------------------------------------------------------- */
bool tfp_win_focused(void) {
  DWORD process = 0;
  HWND window = GetForegroundWindow();
  return window && GetWindowThreadProcessId(window, &process) && process == GetCurrentProcessId();
}
bool tfp_win_key_down(int virtual_key) { return (GetAsyncKeyState(virtual_key) & 0x8000) != 0; }
/* Retail reads stingray.Mouse.button('right' / 'middle') (runtime.lua:395-421); the level of the
 * same two buttons from Windows, 0 while the game is not the foreground window. */
static int platform_mouse(void *u, int button) {
  (void)u;
  if (button != 1 && button != 2) return -1;
  return tfp_win_focused() && tfp_win_key_down(button == 1 ? VK_RBUTTON : VK_MBUTTON) ? 1 : 0;
}

void tfp_win_hosts(TfpOpsHost *memory, TfpWirePlatform *platform, TfpWireHelpers *helpers) {
  static const TfpCameraHelperApi camera_api = TFP_CAMERA_HELPER_LINKED;
  static const TfpFovHelperApi fov_api = {tfp_fov_validate, tfp_fov_begin, tfp_fov_bind, tfp_fov_set_transition, tfp_fov_end,
                                          tfp_fov_read_stats, tfp_fov_error};
  static const TfpNearHelperApi near_api = {tfp_near_begin, tfp_near_set, tfp_near_end, tfp_near_read_stats, tfp_near_error};
  static const TfpBlurHelperApi blur_api = {tfp_blur_validate, tfp_blur_begin, tfp_blur_end, tfp_blur_read_stats, tfp_blur_error};
  hook_address[TFP_WIN_CAMERA] = (uint64_t)(uintptr_t)camera_hook;
  hook_address[TFP_WIN_FOV] = (uint64_t)(uintptr_t)fov_hook;
  hook_address[TFP_WIN_NEAR] = (uint64_t)(uintptr_t)near_hook;
  hook_address[TFP_WIN_BLUR] = (uint64_t)(uintptr_t)blur_hook;

  TfpCameraHost camera = {NULL, hook_address[TFP_WIN_CAMERA], host_read, host_readable_span, camera_now, host_writable_slot, host_executable,
                          host_cas, host_write, host_pin, camera_lock_exclusive, camera_unlock_exclusive, camera_lock_shared,
                          camera_unlock_shared, NULL};
  tfp_camera_set_host(&camera);
  TfpFovHost fov = {NULL, hook_address[TFP_WIN_FOV], host_read, host_readable_span, fov_seconds, host_writable_slot,
                    host_writable_request_field, host_executable, host_cas, host_cas32, host_write, host_pin, fov_lock_exclusive,
                    fov_unlock_exclusive, NULL};
  tfp_fov_set_host(&fov);
  TfpNearHost near_host = {NULL, hook_address[TFP_WIN_NEAR], host_read, near_now_us, host_writable_slot, host_executable, host_cas,
                           host_write, host_pin, near_clip_lock_exclusive, near_clip_unlock_exclusive, near_clip_lock_shared,
                           near_clip_unlock_shared, NULL};
  tfp_near_set_host(&near_host);
  TfpBlurHost blur = {NULL, hook_address[TFP_WIN_BLUR], host_read, host_readable_span, host_writable_slot, host_executable, host_cas,
                      host_write, host_pin, blur_lock_exclusive, blur_unlock_exclusive, NULL};
  tfp_blur_set_host(&blur);
  (void)fov_lock_shared; (void)fov_unlock_shared; (void)blur_lock_shared; (void)blur_unlock_shared;

  memset(memory, 0, sizeof *memory);
  memory->wine = wine;
  memory->module = ops_module, memory->read = host_read, memory->write = ops_write, memory->query = ops_query, memory->call = ops_call;
  memory->log = ops_log;
  platform->user = NULL, platform->clock = platform_clock, platform->mouse = platform_mouse, platform->log = ops_log;
  helpers->camera = &camera_api, helpers->fov = &fov_api, helpers->near_clip = &near_api, helpers->blur = &blur_api;
}

/* ---- for src/wire_win_pose.c (the feel build's pose helper host): the same rules, shared ---------- */
bool tfp_win_code_is(uint64_t at, const char *hex) { /* the bytes at `at` as they were before any HellForge hook */
  uint8_t now[128], want[128];
  size_t n = strlen(hex) / 2;
  if (!n || n > sizeof now || !has_read_code) return false;
  for (size_t i = 0; i < n; i++) {
    unsigned byte = 0;
    sscanf(hex + i * 2, "%2x", &byte);
    want[i] = (uint8_t)byte;
  }
  return hf->read_code((uintptr_t)at, now, n) == 0 && !memcmp(now, want, n);
}
bool tfp_win_writable_slot(uint64_t at, uint64_t exe) { return host_writable_slot(NULL, at, exe); }
bool tfp_win_writable32(uint64_t at) { return host_writable_request_field(NULL, at); }
bool tfp_win_executable(uint64_t at) { return host_executable(NULL, at); }
bool tfp_win_write(void *out, const void *data, size_t length) { return host_write(NULL, out, data, length); }
bool tfp_win_pin(uint64_t address) { return host_pin(NULL, address); }
