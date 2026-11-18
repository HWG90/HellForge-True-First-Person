/* True First Person for HellForge, view build 0.6.0-view.1 (game build 25480438).
 *
 * F11 toggles the game's own first-person camera through the translated retail units: the runtime
 * order (tfp_runtime.c) with the controller, the backend, the camera / FOV / near / blur adapters
 * and helpers bound for real (wire_rt.c, wire_win.c), and every other module answering "off"
 * (wire_off.c). No arms, sleeves, motion or effects in this build. docs/wiring/WIRING-VIEW.md.
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
#ifdef TFP_FEEL
/* The feel build: the same entry with the module binders joined on (src/wire_feel.h, docs/wiring/WIRING-FEEL.md). */
#include "wire_win_pose.h" /* first: it settles the two TfpPoseStats names */
#include "wire_feel.h"
#include "wire_win_engine.h"
#include "wire_win_effects.h"
#include "engine.h"
#include "mod_menu.h"
#define TFP_VIEW_VERSION "0.6.0-actioncam.1"
#else
#include "wire_win.h"
#define TFP_VIEW_VERSION "0.6.0-view.1"
#endif
#define TFP_CONFLICT_ID "goose.first-person-controller"

static const HfApi *hf;
static HfMod *owner;
static TfpWire wire;
static enum { WAIT_MODULES, WAIT_PINS, RUNNING, REFUSED } stage;
static bool key_held, told_waiting, told_tick_thread, hook_told[TFP_WIN_HOOKS];
static uint64_t next_pin_check;
/* Live replace (docs/guides/LIVE-REPLACE.md). closing: hellforge_mod_unload has taken over, the tick does
 * nothing. in_tick: a tick is between its first and last line. Each side sets its own flag and then
 * reads the other's, both with interlocked operations, so one of the two always sees the other. */
static volatile LONG closing, in_tick;
#ifdef TFP_FEEL
static TfpWireFeel feel;
static TfpFxPlatform fx_platform;
static bool engine_hooked, pose_hook_told[TFP_WIN_POSE_HOOKS];
static char settings_text[TFP_SETTINGS_MAX];
/* Two places hold settings.json. The one beside this DLL is made at build time (tools/convert_settings.py)
 * and is the first run's; HellForge holds a session's mod files read-only, so what the menu and the poser
 * change is saved to the user's own file, which is read first from then on:
 *   %LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson\settings.json */
#define TFP_USER_FOLDERS {"Goose", "Goose\\HellForge", "Goose\\HellForge\\TrueFirstPerson"}
static bool user_settings_path(char *path, size_t size, bool make_folders) {
  char base[MAX_PATH];
  static const char *const folders[] = TFP_USER_FOLDERS;
  DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
  if (!n || n >= sizeof base) return false;
  for (size_t i = 0; make_folders && i < sizeof folders / sizeof *folders; i++) {
    snprintf(path, size, "%s\\%s", base, folders[i]);
    CreateDirectoryA(path, NULL); /* an existing folder is fine; a real failure shows when the file is made */
  }
  return (size_t)snprintf(path, size, "%s\\%s\\settings.json", base, folders[sizeof folders / sizeof *folders - 1]) < size;
}
static size_t read_file(const char *path) {
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) return 0;
  DWORD got = 0, total = 0;
  while (total < sizeof settings_text && ReadFile(file, settings_text + total, (DWORD)(sizeof settings_text - total), &got, NULL) && got) total += got;
  CloseHandle(file);
  return total;
}
/* 0: no file in either place. */
static size_t read_settings(char *where, size_t where_size) {
  HMODULE self = NULL;
  char path[MAX_PATH + 16];
  where[0] = 0;
  if (user_settings_path(path, sizeof path, false)) {
    size_t total = read_file(path);
    if (total) { snprintf(where, where_size, "%s", path); return total; }
  }
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void *)read_settings, &self))
    return 0;
  DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
  if (!n || n >= MAX_PATH) return 0;
  char *slash = strrchr(path, '\\');
  if (!slash) return 0;
  strcpy(slash + 1, "settings.json");
  snprintf(where, where_size, "%s", path);
  return read_file(path);
}
/* TfpSettingsSaveFn: a temporary file beside the target, flushed, then renamed over it. Game thread only
 * (tfp_wire_settings_save_frame is called from the tick). */
static bool save_settings(void *user, const char *text, size_t length, char *why, size_t why_size) {
  char path[MAX_PATH + 16], temp[MAX_PATH + 24];
  (void)user;
  if (!user_settings_path(path, sizeof path, true)) { snprintf(why, why_size, "LOCALAPPDATA is not set"); return false; }
  snprintf(temp, sizeof temp, "%s.new", path);
  HANDLE file = CreateFileA(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) { snprintf(why, why_size, "%s could not be made (Windows error %lu)", temp, (unsigned long)GetLastError()); return false; }
  DWORD put = 0;
  bool ok = WriteFile(file, text, (DWORD)length, &put, NULL) && put == length && FlushFileBuffers(file);
  DWORD code = GetLastError();
  CloseHandle(file);
  if (ok && !MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) ok = false, code = GetLastError();
  if (!ok) { DeleteFileA(temp); snprintf(why, why_size, "%s could not be written (Windows error %lu)", path, (unsigned long)code); }
  return ok;
}
#endif

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

static void measure_reads(void) { /* docs/wiring/WIRING-INVENTORY.md section 4: the helpers read many times per hook call */
  LARGE_INTEGER f, a, b;
  uint64_t value = 0;
  if (!QueryPerformanceFrequency(&f) || !QueryPerformanceCounter(&a)) return;
  for (int i = 0; i < 2000; i++) tfp_win_read(tfp_win_game() + FP_CAMERA, &value, sizeof value);
  QueryPerformanceCounter(&b);
  say("Reads: %.2f microseconds per guarded 8-byte read (2000 reads)", (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)f.QuadPart / 2000.0);
}

#ifdef TFP_FEEL
/* The aim button from the engine, as retail reads it (src/wire_mouse.h). Called inside the tick's eng_temp scope. */
#include "wire_mouse.h"
static bool mouse_engine_ready(void *u) { (void)u; return eng_ready(); }
static uint64_t mouse_engine_function(void *u, const char *path) { (void)u; return (uint64_t)eng_cfunction(path); }
static uint64_t mouse_exe_base(void *u) { (void)u; return tfp_win_exe(); }
static uint64_t mouse_engine_generation(void *u) { (void)u; return eng_state_generation(); }
static bool mouse_number(EngRef *value, bool *has, double *out) {
  *has = eng_type(*value) == 3 /* LUA_TNUMBER */ && eng_tonumber(*value, out);
  eng_unref(value);
  return true;
}
static bool mouse_engine_index(void *u, const char *path, const char *name, bool *has, double *index) {
  EngRef value = 0;
  (void)u;
  return eng_call(path, "s>r", name, &value) && mouse_number(&value, has, index);
}
static bool mouse_engine_button(void *u, double index, bool *has, double *level) {
  EngRef value = 0;
  (void)u;
  return eng_call("Mouse.button", "n>r", index, &value) && mouse_number(&value, has, level);
}
static const TfpWireMouseHost mouse_engine = {NULL, mouse_engine_ready, mouse_engine_function, mouse_exe_base, mouse_engine_index,
                                              mouse_engine_button, mouse_engine_generation, NULL};
#endif

static void start(void) {
  TfpOpsHost memory;
  TfpWirePlatform platform;
  TfpWireHelpers helpers;
  TfpRtValue r[2];
  tfp_win_hosts(&memory, &platform, &helpers);
#ifdef TFP_FEEL
  tfp_win_pose_hosts();
#endif
  tfp_wire_init(&wire, &memory, &platform, &helpers);
  wire.mouse_source = tfp_win_mouse_source;
#ifdef TFP_FEEL
  tfp_win_mouse_engine(&mouse_engine);
#endif
  measure_reads();
#ifdef TFP_FEEL
  {
    char where[MAX_PATH + 16], text[360];
    TfpWireFeelParts parts;
    memset(&parts, 0, sizeof parts);
    parts.settings_length = read_settings(where, sizeof where);
    parts.settings_json = parts.settings_length ? settings_text : NULL;
    parts.settings_origin = where[0] ? where : "settings.json";
    parts.pose = tfp_wire_pose_linked(), parts.pose_io = tfp_win_pose_io();
    parts.engine = engine_hooked ? tfp_win_engine_api() : NULL;
    if (!engine_hooked) say("Refused: engine bridge: %s", eng_status());
    tfp_win_effects_platform(&fx_platform, tfp_win_exe());
    parts.effects = &fx_platform;
    tfp_wire_feel_bind(&wire, &feel, &parts);
    tfp_wire_settings_saver(&wire, save_settings, NULL);
    int slots = tfp_win_pose_check(text, sizeof text);
    say(slots == 1 ? "Ready: pose slots: %s" : slots == 0 ? "Refused: pose: %s" : "Waiting: pose slots are not readable yet: %s", text);
  }
#endif
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
#ifdef TFP_FEEL
  say("Ready. F11 toggles True First Person (view, weapon and arm placement, motion; head hiding, sleeves, hand grips and the effects once "
      "their checks pass: see the Ready / Check / Refused lines). The settings menu is in DBF-MCM when goose.mcm is loaded; "
      "settings are saved when they change. Not in this build: a placement editor (F8 opens HellForge's own editor), Photo Mode camera "
      "sharing.");
#else
  say("Ready. F11 toggles the view. Not in this build: arms and weapon placement, sleeves, hand grips, motion, head and body hiding, "
      "depth of field, vignette, aiming markers, movement policy, the F8 editor, the menu, Photo Mode camera sharing.");
#endif
}

#ifdef TFP_FEEL
static unsigned menu_unavailable(void) {
  bool arms = tfp_wire_engine_state(&wire, TFP_WE_SLEEVES) != TFP_WE_REFUSED || tfp_wire_engine_state(&wire, TFP_WE_GRIP) != TFP_WE_REFUSED;
  return (arms ? 0 : TFP_MENU_NO_ARMS) | TFP_MENU_NO_EDITOR;
}
/* A live replace asked for the mod's registrations with other mods to be taken out; this is the game
 * thread, where DBF-MCM allows it. Nothing is released while the mod still holds something in the game. */
static volatile LONG release_state; /* 0 not answered, 1 released, 2 refused (release_why) */
static const char *volatile release_why;
static void release_on_game_thread(void) {
  const char *why = stage == RUNNING ? tfp_wire_feel_held(&wire) : NULL;
  if (!why && !tfp_mod_menu_release(hf, owner, &wire)) why = "DBF-MCM did not release the settings menu";
  release_why = why;
  InterlockedExchange(&release_state, why ? 2 : 1);
}
#endif
static void tick_body(void) {
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
#ifdef TFP_FEEL
  /* The toggle key is the menu's (default F11; 0: unbound). Polled as before: the SDK has no key event. */
  int toggle_key = stage == RUNNING ? tfp_wire_menu_toggle_key(&wire) : VK_F11;
  bool down = toggle_key && tfp_win_focused() && tfp_win_key_down(toggle_key);
#else
  bool down = tfp_win_focused() && tfp_win_key_down(VK_F11);
#endif
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
#ifdef TFP_FEEL
  /* Engine values made in the tick live in the engine's per-frame buffer (src/engine.h). */
  eng_temp_begin();
  tfp_wire_feel_begin(&wire);
  tfp_mod_menu_tick(hf, owner, &wire, menu_unavailable(), NULL);
  tfp_wire_frame(&wire, extra);
  tfp_wire_menu_frame(&wire);          /* what the user did in MCM since the last tick, and the two status rows */
  tfp_wire_settings_save_frame(&wire); /* writes only when the store changed since the last write */
  eng_temp_end();
  for (int i = 0; i < TFP_WIN_POSE_HOOKS; i++) {
    const TfpWinHook *hook = tfp_win_pose_hook(i);
    if (hook->refused) {
      say("Refused: the pose %s slot was not swapped: the function it points at is not the pinned one (read with read_code)", tfp_win_pose_hook_name(i));
      tfp_win_pose_hook_refusal_seen(i);
    }
    if (hook->first_thread && !pose_hook_told[i]) {
      pose_hook_told[i] = true;
      say("Hook: pose %s first fired on thread %lu, called from game+%#llx (the tick is thread %lu)", tfp_win_pose_hook_name(i),
          (unsigned long)hook->first_thread, (unsigned long long)(hook->first_return - tfp_win_game()), (unsigned long)GetCurrentThreadId());
    }
  }
#else
  tfp_wire_frame(&wire, extra);
#endif

  {
    char why[160];
    if (tfp_win_mouse_note(why, sizeof why))
      say(*why ? "Aim button: %s" : "Aim button: read from the engine (Mouse.button at exe+0x482480, proven), as retail reads it%s", *why ? why : "");
  }
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

static void tick(HfMod *mod, float dt, void *unused) {
  (void)mod; (void)dt; (void)unused;
  InterlockedExchange(&in_tick, 1);
  if (!InterlockedCompareExchange(&closing, 0, 0)) tick_body();
#ifdef TFP_FEEL
  else if (!InterlockedCompareExchange(&release_state, 0, 0)) release_on_game_thread();
#endif
  InterlockedExchange(&in_tick, 0);
}

/* The session is ending (possibly under the loader lock, other threads gone): no game call, no
 * lock, no helper state. Only put the dispatch slots back if they still hold this mod's hooks. */
static void session_end(HfMod *mod, HfEvent event, void *unused) {
  (void)event; (void)unused;
  unsigned restored = tfp_win_restore_slots();
#ifdef TFP_FEEL
  restored += tfp_win_pose_restore_slots();
#endif
  hf->log(mod, "Session end: %u dispatch slot(s) put back", restored);
}

#ifdef TFP_FEEL
/* SDK 16, "live": true. HellForge is taking this build out of the running game (reason: removed, or
 * replaced by a new build). Called on the loader's change thread: no game call, no engine call and
 * no helper call from here. It only succeeds when the mod already holds nothing in the game, which
 * the tick's own stop paths (F11 off) bring about; then the dispatch slots are put back as at the
 * session's end. Anything else: 1, and HellForge keeps this build for the session. */
__declspec(dllexport) int hellforge_mod_unload(const HfApi *api, HfMod *mod, uint32_t reason) {
  const char *why = NULL;
  (void)api;
  /* The game thread answers: it checks that nothing is held and takes the menu out of DBF-MCM, which
   * only that thread may do. */
  InterlockedExchange(&release_state, 0);
  InterlockedExchange(&closing, 1);
  for (int waited = 0; !InterlockedCompareExchange(&release_state, 0, 0) && waited < 2000; waited++) Sleep(1);
  LONG answer = InterlockedCompareExchange(&release_state, 0, 0);
  if (answer == 2) why = release_why;
  else if (!answer) {
    /* No game update came in two seconds. Without a menu registration this thread can still judge it, once no update is in flight. */
    for (int waited = 0; InterlockedCompareExchange(&in_tick, 0, 0); waited++) {
      if (waited >= 2000) { why = "a game update of this mod did not finish within 2 seconds"; break; }
      Sleep(1);
    }
    if (!why && tfp_mod_menu_registered())
      why = "the game thread did not run during the change, and the settings menu can only be taken out of DBF-MCM from it";
    if (!why && stage == RUNNING) why = tfp_wire_feel_held(&wire);
  }
  if (why) {
    InterlockedExchange(&release_state, 0);
    InterlockedExchange(&closing, 0);
    hf->log(mod, "Refused: live %s: %s. This build stays for the session.", reason == HF_UNLOAD_REPLACE ? "replace" : "removal", why);
    return 1;
  }
  unsigned restored = tfp_win_restore_slots() + tfp_win_pose_restore_slots();
  hf->log(mod, "Live %s: the mod held nothing in the game; %u dispatch slot(s) put back; this build does nothing from here on.",
          reason == HF_UNLOAD_REPLACE ? "replace" : "removal", restored);
  return 0;
}
#endif

__declspec(dllexport) int hellforge_mod_init(const HfApi *api, HfMod *mod) {
  if (!api || !mod || api->version < 16 || api->size < offsetof(HfApi, read_code) + sizeof api->read_code || !api->log || !api->read ||
      !api->write || !api->read_code || !api->module_base || !api->build_id || !api->on_game_thread || !api->on) return 1;
  hf = api;
  owner = mod;
  if (strcmp(api->build_id(), "25480438")) {
    api->log(mod, "Refused: this build of True First Person is for game build 25480438, not %s", api->build_id());
    return 1;
  }
#ifdef TFP_FEEL
  bool live = api->size >= offsetof(HfApi, load_phase) + sizeof api->load_phase && api->load_phase && api->load_phase(mod) == HF_LOAD_LIVE;
  say("True First Person " TFP_VIEW_VERSION " (feel build): retail 0.5.41-smooth.66 in C for build 25480438. %s", live
      ? "Set up live, while the game runs: the same set-up as at the game's start." : "Set up as the game started.");
  engine_hooked = tfp_win_engine_init(api, mod); /* hooks lua51.dll's lua_pcall / lua_close, from the loader's thread as the donor did */
#else
  say("True First Person " TFP_VIEW_VERSION " (view only): retail 0.5.41-smooth.66 in C for build 25480438.");
#endif
  api->on(mod, HF_EVENT_SESSION_END, session_end, NULL);
  if (!api->on_game_thread(mod, tick, NULL)) {
    say("Refused: the game-thread callback could not be registered");
    return 1;
  }
  return 0;
}
