/* See render_slot.h. Build 25480438.
 *
 * Lifted from the retired First Person Controller's host (true-first-person-controller-body14-20261118
 * src/native_host.c; "host:N" is a line of it): the game draws a world by calling the engine's
 * application table, slot +0x28, which holds the world renderer, exe+0x318150. The primary draw of a
 * frame is the call at game+0xab7ac7; its return address is game+0xab7b00 (host:232, native_bindings.h
 * FPC_NATIVE_PRIMARY_RETURN). This mod's function goes into that slot while the arm drive is switched
 * on; it lets the arm drive write the first-person rig's joints just before the primary draw is forwarded
 * (src/arm_drive.c) and forwards every call unchanged, with the caller's own arguments.
 *
 * The contract (host:141-164 read_render_contract), all proven before the slot is written (and once at
 * start-up, for the log):
 *   - game.dll and helldivers2.exe are the images of build 25480438 (time stamp and size);
 *   - the primary draw's call bytes at game+0xab7ac7, and the renderer's first bytes at exe+0x318150 in
 *     executable memory: both as they were before any HellForge hook AND as they are now (a function
 *     another mod hooked is refused, as the retired host's plain read refused it);
 *   - *(game+0x3326308) is the engine's root, exe+0x27c8d80, and *(root+0x10) the application table,
 *     exe+0x27c9e60;
 *   - the slot, application+0x28, is 8 writable bytes of the exe's image and holds the renderer or this
 *     mod's own hook. Anything else is another mod's: refused, nothing written.
 * The slot is written with one compare-and-swap from the renderer to the hook, and put back with one
 * from the hook to the renderer: a value another mod put there afterwards is never overwritten
 * (host:123-134), the same rule as src/wire_win.c and src/wire_win_pose.c. No code byte is written.
 * On every primary draw, before the arm drive is called, the root, the application table and the slot
 * are read again and must be what the hook was put on (host:114-122 slot_owned).
 *
 * Threads (host:388-393): the world render is called on the game thread, the thread of HellForge's
 * game-thread tick; the retired host required exactly that (`same_thread`, and a tick at most 500 ms
 * old) before it let its controller call the engine's setters from here, and the accepted versions did.
 * The hook passes the same condition to the arm drive, which does nothing without it. A call on another
 * thread, a nested call, or a call that is not the primary draw's is only forwarded. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdio.h>
#include <string.h>
#include "render_slot.h"
#include "arm_drive.h"
#include "profile.h"
#include "slot_platform.h"

#define B_GAME_PRIMARY_CALL   UINT32_C(0xab7ac7)  /* host:149 */
#define B_HEX_PRIMARY_CALL    "488b053ae886024c8bc583bb78c40a0002" /* host:143 */
#define B_GAME_PRIMARY_RETURN UINT32_C(0xab7b00)  /* host:232 */
#define B_EXE_RENDER          UINT32_C(0x318150)  /* host:150 */
#define B_HEX_RENDER          "48895c240848896c2410488974241848897c242041564883ec60" /* host:142 */
#define B_EXE_ROOT            UINT32_C(0x27c8d80) /* host:152 */
#define B_EXE_APPLICATION     UINT32_C(0x27c9e60) /* host:153 */
#define L_ROOT_APPLICATION    0x10
#define L_APPLICATION_RENDER  0x28                /* host:154 */
enum { TICK_MAX_AGE_MS = 500 };                   /* host:392 */

typedef void (*RenderWorld)(void *, void *, void *, const float *, void *, const void *, uint32_t, uint32_t); /* host:14 */
static LONG64 cost_total, cost_max, cost_count; /* game thread only: the wrapper's measured branch and the tick */
static const HfApi *hf;
static TfpWinHook hook;                  /* slot, original: where the hook is in now */
static volatile LONG armed, inflight, calls;
static volatile LONG tick_thread;
static volatile ULONGLONG tick_ms;
static unsigned depth;                   /* game thread only */
static char contract_text[200];

static void render_thunk(void *world, void *camera, void *viewport, const float *optional4, void *opaque5, const void *opaque6, uint32_t opaque7,
                         uint32_t opaque8);
static uint64_t thunk_address(void) { return (uint64_t)(uintptr_t)render_thunk; }

/* ---- the contract ---- */
static bool image_is(uint64_t base, uint32_t stamp, uint32_t size) {
  uint8_t dos[64], nt[0x58];
  uint32_t pe, found_stamp, found_size;
  if (!tfp_win_read(base, dos, sizeof dos) || dos[0] != 'M' || dos[1] != 'Z') return false;
  memcpy(&pe, dos + 0x3c, 4);
  if (pe > 0x1000 || !tfp_win_read(base + pe, nt, sizeof nt) || memcmp(nt, "PE\0\0", 4)) return false;
  memcpy(&found_stamp, nt + 8, 4), memcpy(&found_size, nt + 0x50, 4);
  return found_stamp == stamp && found_size == size;
}
/* 1: the pinned bytes, before any hook and now; 0: not the pinned bytes; -1: the pinned bytes with a hook on them now. */
static int code_state(uint64_t at, const char *hex) {
  uint8_t now[64], want[64];
  const size_t n = strlen(hex) / 2;
  if (!tfp_win_code_is(at, hex) || n > sizeof now) return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned byte = 0;
    sscanf(hex + i * 2, "%2x", &byte);
    want[i] = (uint8_t)byte;
  }
  return hf && hf->read((uintptr_t)at, now, n) == 0 && !memcmp(now, want, n) ? 1 : -1;
}
typedef struct Contract { uint64_t slot, original, current; } Contract;
static const char *contract(Contract *out) {
  const uint64_t game = tfp_win_game(), exe = tfp_win_exe();
  uint64_t root = 0, application = 0, current = 0;
  int state;
  if (!game || !exe) return "game.dll or helldivers2.exe is not loaded";
  if (!image_is(game, FP_GAME_STAMP, FP_GAME_SIZE)) return "game.dll is not the image of build 25480438";
  if (!image_is(exe, FP_EXE_STAMP, FP_EXE_SIZE)) return "helldivers2.exe is not the image of build 25480438";
  if ((state = code_state(game + B_GAME_PRIMARY_CALL, B_HEX_PRIMARY_CALL)) != 1)
    return state ? "the primary draw's call site (game+0xab7ac7) is hooked by another mod" : "the primary draw's call site (game+0xab7ac7) differs";
  if ((state = code_state(exe + B_EXE_RENDER, B_HEX_RENDER)) != 1)
    return state ? "the world renderer (exe+0x318150) is hooked by another mod" : "the world renderer's entry (exe+0x318150) differs";
  if (!tfp_win_executable(exe + B_EXE_RENDER)) return "the world renderer's entry (exe+0x318150) is not executable code";
  if (!tfp_win_read(game + FP_API_ROOT, &root, 8) || root != exe + B_EXE_ROOT || !tfp_win_read(root + L_ROOT_APPLICATION, &application, 8) ||
      application != exe + B_EXE_APPLICATION)
    return "the engine's root or application table differs";
  const uint64_t slot = application + L_APPLICATION_RENDER;
  if ((slot & 7) || !tfp_win_writable_slot(slot, exe) || !tfp_win_read(slot, &current, 8)) return "the render slot (application table +0x28) is not accessible";
  if (current != exe + B_EXE_RENDER && current != thunk_address()) {
    snprintf(contract_text, sizeof contract_text, "the render slot (application table +0x28) holds %#llx, not the world renderer (exe+0x318150): another mod owns it",
             (unsigned long long)current);
    return contract_text;
  }
  *out = (Contract){slot, exe + B_EXE_RENDER, current};
  return NULL;
}
const char *tfp_render_slot_contract(void) {
  Contract c;
  return contract(&c);
}
/* The hook is where it was put, on the same tables (host:114-122 slot_owned). */
static bool slot_owned(void) {
  uint64_t root = 0, application = 0, current = 0;
  return hook.slot && tfp_win_read(tfp_win_game() + FP_API_ROOT, &root, 8) && root == tfp_win_exe() + B_EXE_ROOT &&
         tfp_win_read(root + L_ROOT_APPLICATION, &application, 8) && application + L_APPLICATION_RENDER == hook.slot &&
         tfp_win_read(hook.slot, &current, 8) && current == thunk_address();
}

/* ---- the slot ---- */
const char *tfp_render_slot_install(void) {
  Contract c;
  uintptr_t found = 0;
  const char *why = contract(&c);
  if (why) return why;
  if (c.current == thunk_address()) { /* already in (this build put it there: nothing else knows the address) */
    hook.slot = c.slot, hook.original = c.original;
    InterlockedExchange(&armed, 1);
    return NULL;
  }
  hook.original = c.original; /* before the swap: a call can come through the hook at once */
  if (!fp_atomic_pointer((uintptr_t)c.slot, (uintptr_t)c.original, (uintptr_t)thunk_address(), &found) || found != c.original) {
    hook.refused = 1;
    return "the render slot changed hands while the hook was going in";
  }
  hook.slot = c.slot;
  InterlockedExchange(&armed, 1);
  return NULL;
}
unsigned tfp_render_slot_restore(void) {
  uintptr_t found = 0;
  unsigned restored = 0;
  InterlockedExchange(&armed, 0);
  if (!hook.slot) return 0;
  if (fp_atomic_pointer((uintptr_t)hook.slot, (uintptr_t)thunk_address(), (uintptr_t)hook.original, &found) && found == thunk_address()) restored = 1;
  hook.slot = 0; /* whatever is there now is not this mod's */
  return restored;
}
bool tfp_render_slot_remove(void) {
  tfp_render_slot_restore();
  return true;
}
bool tfp_render_slot_installed(void) { return hook.slot != 0; }
bool tfp_render_slot_idle(void) { return InterlockedCompareExchange(&inflight, 0, 0) == 0; }
const TfpWinHook *tfp_render_slot_hook(void) {
  hook.calls = (uint32_t)InterlockedCompareExchange(&calls, 0, 0);
  return &hook;
}
void tfp_render_slot_tick(void) {
  tick_ms = GetTickCount64();
  InterlockedExchange(&tick_thread, (LONG)GetCurrentThreadId());
}

/* ---- the function in the slot (host:364-429) ---- */
static __declspec(noinline) void render_thunk(void *world, void *camera, void *viewport, const float *optional4, void *opaque5, const void *opaque6,
                                              uint32_t opaque7, uint32_t opaque8) {
  const uint64_t from = (uint64_t)(uintptr_t)_ReturnAddress();
  const RenderWorld original = (RenderWorld)(uintptr_t)hook.original; /* set before the slot ever held this function */
  bool acted = false, after = false;
  InterlockedIncrement(&inflight);
  InterlockedIncrement(&calls);
  if (from == tfp_win_game() + B_GAME_PRIMARY_RETURN && InterlockedCompareExchange(&armed, 0, 0)) {
    const ULONGLONG now = GetTickCount64(), ticked = tick_ms;
    const bool game_thread = (LONG)GetCurrentThreadId() == InterlockedCompareExchange(&tick_thread, 0, 0) && ticked && now >= ticked &&
                             now - ticked <= TICK_MAX_AGE_MS;
    if (!hook.first_thread) hook.first_return = from, hook.first_thread = GetCurrentThreadId();
    if (!game_thread) tfp_arm_drive_render((uint64_t)(uintptr_t)world, false); /* counted as a refusal; nothing read or written */
    else if (!depth && slot_owned()) {
      depth = 1, acted = true;
      LARGE_INTEGER t0, t1;
      QueryPerformanceCounter(&t0);
      after = tfp_arm_drive_render((uint64_t)(uintptr_t)world, true);
      QueryPerformanceCounter(&t1);
      const LONG64 spent = t1.QuadPart - t0.QuadPart; /* what the arm drive costs this frame, for the Arms line */
      cost_total += spent, cost_count++;
      if (spent > cost_max) cost_max = spent;
    }
  }
  /* An exception of the engine's own passes through; the game's values still go back and the count still comes down. */
  __try {
    original(world, camera, viewport, optional4, opaque5, opaque6, opaque7, opaque8);
  } __finally {
    if (after) tfp_arm_drive_after_render();
    if (acted) depth = 0;
    InterlockedDecrement(&inflight);
  }
}

/* The arm drive's own time inside the primary draw since the last call: frames measured, mean and largest in
 * microseconds. Read and reset by the tick (the game thread, the same thread the wrapper measures on). */
void tfp_render_slot_cost(uint64_t *frames, double *mean_us, double *max_us) {
  LARGE_INTEGER rate;
  QueryPerformanceFrequency(&rate);
  const double us = rate.QuadPart ? 1e6 / (double)rate.QuadPart : 0;
  *frames = (uint64_t)cost_count, *mean_us = cost_count ? (double)cost_total * us / (double)cost_count : 0, *max_us = (double)cost_max * us;
  cost_total = cost_max = 0, cost_count = 0;
}

/* ---- the arm drive's host ---- */
typedef void (*SetLocal)(uint32_t, uint32_t, const TfpGraftLocal *); /* the retired FpcSetUnitPose */
typedef void (*UpdateUnit)(void *, uint32_t);                        /* the retired FpcUpdateUnit */
static __declspec(noinline) void call_set_local(uint64_t function, uint32_t unit, uint32_t node, const TfpGraftLocal *local) {
  ((SetLocal)(uintptr_t)function)(unit, node, local);
}
static __declspec(noinline) void call_update_unit(uint64_t function, uint64_t world, uint32_t unit) {
  ((UpdateUnit)(uintptr_t)function)((void *)(uintptr_t)world, unit);
}
/* Called through volatile pointers so the compiler keeps the calls inside their __try (src/slot_atomic.c). */
static void (*volatile set_local_dispatch)(uint64_t, uint32_t, uint32_t, const TfpGraftLocal *) = call_set_local;
static void (*volatile update_unit_dispatch)(uint64_t, uint64_t, uint32_t) = call_update_unit;
static bool host_read(void *user, uintptr_t at, void *out, size_t size) { (void)user; return tfp_win_read(at, out, size); }
static bool host_executable(void *user, uint64_t at) { (void)user; return tfp_win_executable(at); }
static bool host_set_local(void *user, uint64_t function, uint32_t unit, uint32_t node, const TfpGraftLocal *local) {
  (void)user;
  __try { set_local_dispatch(function, unit, node, local); return true; }
  __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool host_update_unit(void *user, uint64_t function, uint64_t world, uint32_t unit) {
  (void)user;
  __try { update_unit_dispatch(function, world, unit); return true; }
  __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void tfp_render_slot_setup(const HfApi *api) {
  TfpArmDriveHost host;
  hf = api;
  memset(&host, 0, sizeof host);
  host.memory.read = host_read, host.memory.game = (uintptr_t)tfp_win_game(), host.memory.exe = (uintptr_t)tfp_win_exe();
  host.executable = host_executable, host.set_local = host_set_local, host.update_unit = host_update_unit;
  tfp_arm_drive_set_host(&host);
}
