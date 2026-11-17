/* True First Person for HellForge, view build 0.6.0-view.1 (game build 25480438).
 *
 * F11 toggles the game's own first-person camera through the translated retail units: the runtime
 * order (tfp_runtime.c) with the controller, the backend, the camera / FOV / near / blur adapters
 * and helpers bound for real (wire_rt.c, wire_win.c), and every other module answering "off"
 * (wire_off.c). No arms, sleeves, motion or effects in this build. docs/WIRING-VIEW.md.
 *
 * One tick on the game thread; four dispatch-slot hooks that the helpers install while the view is
 * on and take out again when it goes off; nothing else is hooked, no code byte is written. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "hellforge_mod.h"
#include "profile.h"
#include "native_contract.h"
#include "wire_win.h"

#define TFP_VIEW_VERSION "0.6.0-view.1"
#define TFP_CONFLICT_ID "goose.first-person-controller"

static const HfApi *hf;
static HfMod *owner;
static TfpWire wire;
static enum { WAIT_MODULES, WAIT_PINS, RUNNING, REFUSED } stage;
static bool key_held, told_waiting, told_tick_thread, hook_told[TFP_WIN_HOOKS];
static uint64_t next_pin_check;

static void say(const char *format, ...) {
  char line[700];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  hf->log(owner, "%s", line);
}
static void refuse(const char *why) {
  stage = REFUSED;
  say("Refused: %s. True First Person stays off for this session.", why);
}

/* ---- load-time checks ---------------------------------------------------------------------------- */
static bool image_is(uint64_t base, uint32_t stamp, uint32_t size) {
  uint8_t dos[64], nt[0x58];
  uint32_t pe, found_stamp, found_size;
  if (!tfp_win_read(base, dos, sizeof dos) || dos[0] != 'M' || dos[1] != 'Z') return false;
  memcpy(&pe, dos + 0x3c, 4);
  if (pe > 0x1000 || !tfp_win_read(base + pe, nt, sizeof nt) || memcmp(nt, "PE\0\0", 4)) return false;
  memcpy(&found_stamp, nt + 8, 4);
  memcpy(&found_size, nt + 0x50, 4);
  return found_stamp == stamp && found_size == size;
}
static bool conflicting_mod(void) {
  if (hf->size < offsetof(HfApi, mods) + sizeof hf->mods || !hf->mods) return false;
  for (uint32_t i = 0; i < 4096; i++) {
    HfModInfo info;
    memset(&info, 0, sizeof info);
    info.size = sizeof info;
    if (!hf->mods(owner, i, &info)) break;
    if (info.id && !strcmp(info.id, TFP_CONFLICT_ID) && info.loaded) return true;
  }
  return false;
}
/* 1 equal, 0 different, -1 unreadable; *hooked: the code in memory now differs from the code before HellForge's hooks. */
static int pin_state(uint64_t at, const char *hex, bool *hooked) {
  uint8_t want[64], original[64], live[64];
  size_t n = strlen(hex) / 2;
  if (n > sizeof want) n = sizeof want;
  for (size_t i = 0; i < n; i++) {
    unsigned byte = 0;
    sscanf(hex + i * 2, "%2x", &byte);
    want[i] = (uint8_t)byte;
  }
  if (hf->read_code((uintptr_t)at, original, n) != 0) return -1;
  if (hf->read((uintptr_t)at, live, n) == 0 && memcmp(live, original, n)) *hooked = true;
  return !memcmp(original, want, n);
}
/* Every code pin of src/profile.h and src/native_contract.h, compared through hf->read_code.
 * 1: all equal and unhooked; 0: refused (said why); -1: some are not readable yet. */
static int check_pins(bool report) {
  unsigned ok = 0, bad = 0, unread = 0, foreign = 0;
  char first_bad[96] = "", first_hooked[96] = "";
  for (size_t i = 0; i < FP_WITNESS_COUNT + FP_NATIVE_SITE_COUNT; i++) {
    bool exe = i >= FP_WITNESS_COUNT && fp_native_sites[i - FP_WITNESS_COUNT].exe;
    const char *name = i < FP_WITNESS_COUNT ? fp_witnesses[i].name : fp_native_sites[i - FP_WITNESS_COUNT].name;
    uint32_t rva = i < FP_WITNESS_COUNT ? fp_witnesses[i].rva : fp_native_sites[i - FP_WITNESS_COUNT].rva;
    const char *hex = i < FP_WITNESS_COUNT ? fp_witnesses[i].hex : fp_native_sites[i - FP_WITNESS_COUNT].hex;
    bool hooked = false;
    int state = pin_state((exe ? tfp_win_exe() : tfp_win_game()) + rva, hex, &hooked);
    if (state < 0) unread++;
    else if (!state) { if (!bad++) snprintf(first_bad, sizeof first_bad, "%s (%s+%#x)", name, exe ? "exe" : "game", (unsigned)rva); }
    else ok++;
    if (hooked && !foreign++) snprintf(first_hooked, sizeof first_hooked, "%s (%s+%#x)", name, exe ? "exe" : "game", (unsigned)rva);
  }
  if (bad) {
    say("Pins: %u ok, %u mismatched, %u unreadable; first mismatch %s", ok, bad, unread, first_bad);
    refuse("a code pin of build 25480438 does not match this game");
    return 0;
  }
  if (foreign) {
    say("Pins: %u ok, %u of them hooked by another mod; first %s", ok, foreign, first_hooked);
    refuse("another mod hooks a function this mod pins (the session's hooks.tsv names it); the two cannot run together");
    return 0;
  }
  if (unread) return -1;
  if (report) say("Pins: %u ok, 0 mismatched, 0 unreadable (read as the code was before any hook)", ok);
  return 1;
}

static void measure_reads(void) { /* docs/WIRING-INVENTORY.md section 4: the helpers read many times per hook call */
  LARGE_INTEGER f, a, b;
  uint64_t value = 0;
  if (!QueryPerformanceFrequency(&f) || !QueryPerformanceCounter(&a)) return;
  for (int i = 0; i < 2000; i++) tfp_win_read(tfp_win_game() + FP_CAMERA, &value, sizeof value);
  QueryPerformanceCounter(&b);
  say("Reads: %.2f microseconds per guarded 8-byte read (2000 reads)", (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart / 2000.0);
}

static void start(void) {
  TfpOpsHost memory;
  TfpWirePlatform platform;
  TfpWireHelpers helpers;
  TfpRtValue r[2];
  tfp_win_hosts(&memory, &platform, &helpers);
  tfp_wire_init(&wire, &memory, &platform, &helpers);
  measure_reads();
  memset(r, 0, sizeof r);
  TfpRtStatus status = tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r);
  if (status != TFP_RT_OK) {
    TfpRtValue e = wire.rt.error;
    char why[300];
    snprintf(why, sizeof why, "the mode could not start: %.*s", e.kind == TFP_RT_TEXT ? (int)(e.length < 200 ? e.length : 200) : 7,
             e.kind == TFP_RT_TEXT ? e.text : "unknown");
    refuse(why);
    return;
  }
  stage = RUNNING;
  say("Ready. F11 toggles the view. Not in this build: arms and weapon placement, sleeves, hand grips, motion, head and body hiding, "
      "depth of field, vignette, aiming markers, movement policy, the F8 editor, the menu, Photo Mode camera sharing.");
}

static void tick(HfMod *mod, float dt, void *unused) {
  (void)mod; (void)dt; (void)unused;
  if (stage == REFUSED) return;
  if (!told_tick_thread) {
    told_tick_thread = true;
    say("Tick: game-thread callback on thread %lu (HellForge runs it before the game's own update)", (unsigned long)GetCurrentThreadId());
  }
  if (stage == WAIT_MODULES) {
    if (!tfp_win_setup(hf, owner)) return;
    if (conflicting_mod()) {
      refuse(TFP_CONFLICT_ID " is loaded. It hooks game.dll+0x12b2ae0 (the aim controller's exit), which this mod pins; switch it off "
             "on the Mods page");
      return;
    }
    if (!image_is(tfp_win_game(), FP_GAME_STAMP, FP_GAME_SIZE) || !image_is(tfp_win_exe(), FP_EXE_STAMP, FP_EXE_SIZE)) {
      refuse("game.dll or helldivers2.exe is not the image of build 25480438");
      return;
    }
    say("Image: game.dll and helldivers2.exe are build 25480438");
    stage = WAIT_PINS;
  }
  bool down = tfp_win_focused() && tfp_win_key_down(VK_F11);
  bool pressed = down && !key_held;
  key_held = down;
  if (stage == WAIT_PINS) {
    uint64_t now = GetTickCount64();
    if (pressed) say("Refused: F11 before the game's code pages could be read; try again once a mission or the ship is loaded");
    if (now < next_pin_check) return;
    next_pin_check = now + 500;
    int pins = check_pins(true);
    if (pins < 0 && !told_waiting) {
      told_waiting = true;
      say("Waiting: some pinned code pages are not readable yet (they are once a player exists)");
    }
    if (pins <= 0) return;
    start();
    if (stage != RUNNING) return;
  }
  if (pressed) wire.toggle_requested = true;

  char extra[160];
  snprintf(extra, sizeof extra, "calls=cam:%u,fov:%u,near:%u,blur:%u", tfp_win_hook(0)->calls, tfp_win_hook(1)->calls, tfp_win_hook(2)->calls,
           tfp_win_hook(3)->calls);
  tfp_wire_frame(&wire, extra);

  for (int i = 0; i < TFP_WIN_HOOKS; i++) {
    const TfpWinHook *hook = tfp_win_hook(i);
    if (hook->refused) {
      say("Refused: the %s slot was not swapped: the function it points at is not the pinned one (read with read_code)", tfp_win_hook_name(i));
      tfp_win_hook_refusal_seen(i);
    }
    if (hook->first_thread && !hook_told[i]) {
      uint64_t from = hook->first_return, game = tfp_win_game();
      hook_told[i] = true;
      say("Hook: %s first fired on thread %lu, called from game+%#llx (the tick is thread %lu)", tfp_win_hook_name(i),
          (unsigned long)hook->first_thread, (unsigned long long)(from - game), (unsigned long)GetCurrentThreadId());
    }
  }
}

/* The session is ending (possibly under the loader lock, other threads gone): no game call, no
 * lock, no helper state. Only put the dispatch slots back if they still hold this mod's hooks. */
static void session_end(HfMod *mod, HfEvent event, void *unused) {
  (void)event; (void)unused;
  unsigned restored = tfp_win_restore_slots();
  hf->log(mod, "Session end: %u dispatch slot(s) put back", restored);
}

__declspec(dllexport) int hellforge_mod_init(const HfApi *api, HfMod *mod) {
  if (!api || !mod || api->version < 16 || api->size < offsetof(HfApi, read_code) + sizeof api->read_code || !api->log || !api->read ||
      !api->write || !api->read_code || !api->module_base || !api->build_id || !api->on_game_thread || !api->on) return 1;
  hf = api;
  owner = mod;
  if (strcmp(api->build_id(), "25480438")) {
    api->log(mod, "Refused: this build of True First Person is for game build 25480438, not %s", api->build_id());
    return 1;
  }
  say("True First Person " TFP_VIEW_VERSION " (view only): retail 0.5.41-smooth.66 in C for build 25480438.");
  api->on(mod, HF_EVENT_SESSION_END, session_end, NULL);
  if (!api->on_game_thread(mod, tick, NULL)) {
    say("Refused: the game-thread callback could not be registered");
    return 1;
  }
  return 0;
}
