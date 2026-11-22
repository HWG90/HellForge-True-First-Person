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
#include <math.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "quit_guard.h"
void tfp_head_set_parts(unsigned parts); /* head_visibility.c: beyond-retail test switch */
void tfp_wire_engine_allow_arms(bool allowed); /* wire_engine.c: beyond-retail switch */
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
#include "mod_panel.h"
#include "wire_panel.h"
#include "mod_editor.h"
#include "render_slot.h"
#include "arm_drive.h"
#include "wire_options.h"
#include "arm_weight.h"
#define TFP_VIEW_VERSION "0.6.0-feel.30"
#else
#include "wire_win.h"
#include "wire_action.h"
#include "camera_helper.h"
#define TFP_VIEW_VERSION "0.6.0-view.1"
#endif
#define TFP_CONFLICT_ID "goose.first-person-controller"

static const HfApi *hf;
static HfMod *owner;
static TfpWire wire;
static enum { WAIT_MODULES, WAIT_PINS, RUNNING, REFUSED } stage;
static bool key_held, told_waiting, told_tick_thread, hook_told[TFP_WIN_HOOKS];
#ifdef TFP_FEEL
static bool editor_key_held;
#endif
static uint64_t next_pin_check;
/* Live replace (docs/guides/LIVE-REPLACE.md). closing: hellforge_mod_unload has taken over, the tick does
 * nothing. in_tick: a tick is between its first and last line. Each side sets its own flag and then
 * reads the other's, both with interlocked operations, so one of the two always sees the other. */
static volatile LONG closing, in_tick;
static DWORD tick_thread; /* the game thread, as the tick last saw it */
#ifdef TFP_FEEL
static TfpWireFeel feel;
static TfpFxPlatform fx_platform;
static bool lock_wanted = true;
static LONG64 tick_cost_total, tick_cost_max, tick_cost_count; /* game thread: the tick's own time, for the Cost line */
/* Three features are switched by options of the settings panel's "Locked view" tab, live (src/wire_options.h): the
 * arm drive (Animated arms), the action hold (Weapon put-away) and the eye's continuity (Smooth eye). Test files
 * beside the DLL, read once at the start, override the option: arms_live.txt / hold_lease.txt force the feature on,
 * arms_off.txt / hold_off.txt / smooth_off.txt force it off. The three flags below are what is in effect now; only
 * the setters option_arms / option_hold / option_smooth write them, on the game thread. */
static TfpWireOptions options;
/* Under the camera lock: the eye's continuity (src/camera_continuity.h) and the per-frame "Trace:" lines after each
 * State edge. A file trace_off.txt beside the DLL switches the lines off; neither does anything without the lock. */
static bool smooth_wanted = true, trace_wanted = true;
static bool mcm_wanted; /* test switch mcm_on.txt: the settings menu in DBF-MCM instead of the mod's own panel */
/* The arm drive (src/arm_drive.h). arms_live: it is on (the Animated arms option, or its files). Test switches, files
 * beside the DLL read once at the start: arms_live_restore.txt makes it put the game's joint values back after every
 * draw instead of leaving them; arm_bend.txt makes it bend one shoulder instead of following the body; rest_trace.txt
 * makes the "Rest:" line (the resting weapon pose, once a second) come every tick. */
static bool arms_live, arms_restore, arms_bend, arms_rest_trace;
/* The action hold (docs/design/ACTION-CAMERA.md "Action hold") is on: the Weapon put-away option, or its files.
 * While it is off none of the hold's code is reached. */
static bool hold_lease;
static char arms_refusal[200]; /* why the render hook may not go in, as last logged: one line per reason */
static bool engine_hooked, pose_hook_told[TFP_WIN_POSE_HOOKS];
static char settings_text[TFP_SETTINGS_MAX], locked_text[1 << 16];
/* Two places hold settings.json. The one beside this DLL is made at build time (tools/convert_settings.py)
 * and is the first run's; HellForge holds a session's mod files read-only, so what the menu and the poser
 * change is saved to the user's own file, which is read first from then on:
 *   %LOCALAPPDATA%\Goose\HellForge\TrueFirstPerson\settings.json
 * The locked view's options are in locked_view.json in that folder, and only there (src/wire_settings.h says why
 * they are not in settings.json, and what is copied aside before a file is replaced). */
#define TFP_USER_FOLDERS {"Goose", "Goose\\HellForge", "Goose\\HellForge\\TrueFirstPerson"}
static const char *const user_file_names[2] = {"settings.json", "locked_view.json"}; /* by TfpSettingsFile */
static bool user_file_path(char *path, size_t size, TfpSettingsFile file, bool make_folders) {
  char base[MAX_PATH];
  static const char *const folders[] = TFP_USER_FOLDERS;
  DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
  if (!n || n >= sizeof base) return false;
  for (size_t i = 0; make_folders && i < sizeof folders / sizeof *folders; i++) {
    snprintf(path, size, "%s\\%s", base, folders[i]);
    CreateDirectoryA(path, NULL); /* an existing folder is fine; a real failure shows when the file is made */
  }
  return (size_t)snprintf(path, size, "%s\\%s\\%s", base, folders[sizeof folders / sizeof *folders - 1], user_file_names[file]) < size;
}
static size_t read_file(const char *path, char *text, size_t size) {
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE) return 0;
  DWORD got = 0, total = 0;
  while (total < size && ReadFile(file, text + total, (DWORD)(size - total), &got, NULL) && got) total += got;
  CloseHandle(file);
  return total;
}
/* 0: no file in either place. *user_file: the text is the user's own file (the one the saver replaces). When it is
 * false the user's file is either not there or gave no bytes (it did not open: another program holds it, or the read
 * failed); the saver does not know which, so copy_user_file is asked before the first write (src/wire_settings.h:
 * <name>.unread). The same holds for read_locked answering 0. */
static size_t read_settings(char *where, size_t where_size, bool *user_file) {
  HMODULE self = NULL;
  char path[MAX_PATH + 32];
  where[0] = 0, *user_file = false;
  if (user_file_path(path, sizeof path, TFP_SETTINGS_FILE, false)) {
    size_t total = read_file(path, settings_text, sizeof settings_text);
    if (total) { snprintf(where, where_size, "%s", path); *user_file = true; return total; }
  }
  if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void *)read_settings, &self))
    return 0;
  DWORD n = GetModuleFileNameA(self, path, MAX_PATH);
  if (!n || n >= MAX_PATH) return 0;
  char *slash = strrchr(path, '\\');
  if (!slash) return 0;
  strcpy(slash + 1, "settings.json");
  snprintf(where, where_size, "%s", path);
  return read_file(path, settings_text, sizeof settings_text);
}
static size_t read_locked(char *where, size_t where_size) { /* 0: no locked_view.json */
  where[0] = 0;
  if (!user_file_path(where, where_size, TFP_LOCKED_FILE, false)) { where[0] = 0; return 0; }
  return read_file(where, locked_text, sizeof locked_text);
}
/* TfpSettingsDisk.write: a temporary file beside the target, flushed, then renamed over it. Game thread only
 * (tfp_wire_settings_disk is called from the first tick's start-up, tfp_wire_settings_save_frame from the tick). */
static bool write_user_file(void *user, TfpSettingsFile which, const char *text, size_t length, char *why, size_t why_size) {
  char path[MAX_PATH + 32], temp[MAX_PATH + 40];
  (void)user;
  if (!user_file_path(path, sizeof path, which, true)) { snprintf(why, why_size, "LOCALAPPDATA is not set"); return false; }
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
/* TfpSettingsDisk.copy: the file as it is on disk to <name><suffix>, never over a file that is there. */
static TfpSettingsCopy copy_user_file(void *user, TfpSettingsFile which, const char *suffix, char *why, size_t why_size) {
  char path[MAX_PATH + 32], copy[MAX_PATH + 64];
  (void)user;
  if (!user_file_path(path, sizeof path, which, false)) { snprintf(why, why_size, "LOCALAPPDATA is not set"); return TFP_COPY_FAILED; }
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
    DWORD code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return TFP_COPY_NONE;
    snprintf(why, why_size, "%s could not be looked at (Windows error %lu)", path, (unsigned long)code);
    return TFP_COPY_FAILED;
  }
  snprintf(copy, sizeof copy, "%s%s", path, suffix);
  if (CopyFileA(path, copy, TRUE)) return TFP_COPY_DONE; /* TRUE: fail when the copy's name is taken */
  DWORD code = GetLastError();
  if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) return TFP_COPY_EXISTS;
  snprintf(why, why_size, "Windows error %lu", (unsigned long)code);
  return TFP_COPY_FAILED;
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

#ifdef TFP_FEEL
/* The aim button once more as the engine's script update begins (src/wire_action.h): the game's own input is
 * newer there than in the tick. Runs inside lua_pcall's hook, so: the tick's thread only, never inside the tick
 * (its own engine calls pass through the same hook), Windows' level only, and at most a few times a frame. */
static void aim_script_probe(void) {
  if (stage != RUNNING || GetCurrentThreadId() != tick_thread || InterlockedCompareExchange(&in_tick, 0, 0) ||
      InterlockedCompareExchange(&closing, 0, 0)) return;
  if (!tfp_wire_aim_due(&wire)) return;
  int right, middle;
  tfp_win_buttons(&right, &middle);
  tfp_wire_aim_probe(&wire, right, middle);
}
#endif

#ifdef TFP_FEEL
/* How far the eye moves when the native view comes back, and in which direction: the helper's own record of
 * the first native frames (read-only), a few frames after each return. Level and up are world axes. */
static void entry_note(void) {
  static bool was_leased;
  static int countdown;
  static float told[3];
  const bool leased = wire.log.has_lease;
  if (leased && !was_leased) countdown = 8;
  was_leased = leased;
  if (!countdown || --countdown) return;
  TfpCameraEntryTraceData trace;
  memset(&trace, 0, sizeof trace);
  if (!tfp_camera_entry_trace(&trace) || trace.count < 1 || trace.rows[0].frame != 0) return;
  /* Both points are in the world: the head camera's last eye, and the rig eye the camera becomes a child of on the
   * native view's first frame (the game's own argument there is within a few centimetres of that node). The camera
   * unit's own world matrix is not used: on the frames after the link it still holds a world-valued local position
   * under the rig's transform, which printed the player's distance from the world origin (9 to 36 m, until feel.22). */
  const TfpCameraEntryTraceRow *first = &trace.rows[0];
  if (!first->rig_known || !memcmp(told, first->head_world, sizeof told)) return; /* this return came from no head-camera frame: an old record */
  memcpy(told, first->head_world, sizeof told);
  const double x = first->rig_world[0] - first->head_world[0], y = first->rig_world[1] - first->head_world[1],
               z = first->rig_world[2] - first->head_world[2];
  say("Entry: the native view's eye was %.3f m (%.3f level, %+.3f up) from the head camera's last eye on its first frame; "
      "camera %s the rig on that frame",
      sqrt(x * x + y * y + z * z), sqrt(x * x + y * y), z, first->linked ? "linked to" : "not yet linked to");
}
#endif

#ifdef TFP_FEEL
/* The per-frame trace (docs/design/ACTION-CAMERA.md "Trace"): written for the 0.5 s after each State edge (the native
 * view entered or left, a hold begun or ended) and the six frames before it, from the camera helper's own record of
 * what it presented. Nothing outside those windows, nothing with trace_off.txt or without the lock.
 *   t            ms from the edge to the frame's camera call (negative: before it)
 *   kind         the game's top camera request on that frame
 *   eye_world    the eye as presented; step: its distance from the frame before
 *   wanted       the eye before the continuity correction; correction: the length of what is carried
 *   lease        the controller's native request record (0: none) when the line is written, pose_armed likewise
 *   entry_ease   the eased entry's progress (1: not easing)
 *   source       head (the head camera), native (the game's first-person camera), repeat (its last argument repeated) */
static void trace_note(void) {
  static bool was_leased, was_holding;
  const bool leased = wire.log.has_lease, holding = wire.controller.holding, edge = leased != was_leased || holding != was_holding;
  was_leased = leased, was_holding = holding;
  if (edge && lock_wanted) tfp_camera_continuity_event(); /* a lease or a hold begun or ended: the rig may be re-seated */
  if (!lock_wanted || !trace_wanted) return;
  if (edge) tfp_camera_trace_edge();
  TfpCameraTraceRow rows[48];
  const uint32_t count = tfp_camera_trace_read(rows, 48);
  if (!count) return;
  TfpPoseHoldProbe pose;
  memset(&pose, 0, sizeof pose);
  tfp_pose_hold_probe(&pose);
  const unsigned long long lease = wire.controller.lease ? (unsigned long long)wire.controller.lease->request : 0;
  static const char *const sources[] = {"-", "head", "native", "repeat"};
  for (uint32_t i = 0; i < count; i++) {
    const TfpCameraTraceRow *r = &rows[i];
    say("Trace: t=%.1f kind=%u eye_world=(%.4f %.4f %.4f) step=%.4f wanted=(%.4f %.4f %.4f) correction=%.4f lease=0x%llx pose_armed=%d entry_ease=%.2f source=%s",
        (double)r->ms, r->kind, (double)r->eye[0], (double)r->eye[1], (double)r->eye[2], (double)r->step, (double)r->wanted[0], (double)r->wanted[1],
        (double)r->wanted[2], (double)r->correction, lease, (int)(pose.armed != 0), (double)r->entry_ease, sources[r->source < 4 ? r->source : 0]);
  }
}
#endif

#ifdef TFP_FEEL
/* ---- the orphaned native frames (0.6.0-feel.26; src/wire_action.h, src/camera_helper.h tfp_camera_set_orphan) ----
 * A stim: the game keeps its first-person request for about 0.85 s after the exit was asked for, with the gun on
 * the body and the rig's eye at the body's head. Each tick, after the controller decided: the camera helper is told
 * while that lasts (it presents the head camera's eye in those frames), and one line is written when it is over.
 * Nothing is read and nothing is said with the camera lock off, or outside the controller's wait for an exit.
 *   frames       frames presented at the head's eye (camera calls, not ticks)
 *   rig eye      how far behind that eye the game's own eye was at most, along the level view direction
 *   head hidden  1: head visibility was hiding rows on every tick of the window
 *   why          what the exit waited for; then, when there were any, the frames that could not be presented so */
static bool lock_view_now(void) { return lock_wanted && tfp_locked_view_camera_lock(&wire); }
static void orphan_note(void) {
  static bool was;
  static unsigned ticks, hidden;
  static TfpCameraOrphanStats start;
  static float behind;
  static char why[200];
  const bool on = lock_view_now() && tfp_wire_orphan(&wire);
  if (on != was) {
    tfp_camera_set_orphan(on);
    tfp_camera_continuity_event();
    if (trace_wanted) tfp_camera_trace_edge();
  }
  TfpCameraOrphanStats now;
  if (on || was) tfp_camera_orphan_stats(&now);
  if (on && !was) {
    start = now, ticks = hidden = 0, behind = 0;
    const char *label = wire.controller.label, *reason = strstr(label, "Waiting for the held native request: ");
    snprintf(why, sizeof why, "%s", reason ? reason + strlen("Waiting for the held native request: ") : label);
  }
  if (on) {
    ticks++;
    if (tfp_wire_engine_head_hidden(&wire) > 0) hidden++;
    if (now.frames != start.frames && now.behind_m > behind) behind = now.behind_m;
  }
  if (!on && was) {
    char extra[160] = "";
    if (now.frames != start.frames && now.behind_m > behind) behind = now.behind_m;
    if (now.refused != start.refused)
      snprintf(extra, sizeof extra, "; +%llu frames followed the rig's eye (the head or the rig's eye was not proven)", (unsigned long long)(now.refused - start.refused));
    say("Orphan: kind=6 frames=+%llu eye held at the head (rig eye %.3f m behind), head hidden=%d, why=%s%s%s", (unsigned long long)(now.frames - start.frames),
        (double)behind, (int)(ticks && hidden == ticks), why[0] ? why : "-", extra,
        ticks && hidden != ticks ? hidden ? "; head visibility was not hiding on every tick" : "; head visibility hid nothing" : "");
  }
  was = on;
}
#endif

#ifdef TFP_FEEL
/* ---- the action hold (the Weapon put-away option): the binder's two hooks (src/wire.h TfpWireHold) ----------------
 * Each tick, after the controller decided. The pose helper tolerates an action from the moment a lease runs
 * (armed), not only once the controller holds: HellForge runs this tick before the game's update, so the game's
 * first rig write of an action comes before the tick that sees the action. The arm drive is told both: "ready"
 * for the same reason (the action's first drawn frame must not cost its binding and entry alignment when the gun
 * has already left the rig), and the hold itself, during which the game's joint values go back after every draw. */
static void hold_apply(TfpWire *w, bool armed, bool holding) {
  (void)w;
  tfp_pose_set_hold(armed || holding);
  tfp_arm_drive_set_hold_ready(armed || holding);
  tfp_arm_drive_set_hold(holding);
}
static void hold_probe(TfpWire *w, const TfpOpsHold *view, uint32_t weapon_unit, TfpWireHoldProbe *out) {
  TfpPoseHoldProbe pose;
  TfpArmDriveProbe rig;
  (void)w; (void)view;
  if (tfp_pose_hold_probe(&pose)) out->has_pose = true, out->pose_armed = pose.armed != 0, out->pose_error = pose.error, out->node5_calls = pose.node5_calls;
  tfp_arm_drive_probe(weapon_unit, &rig);
  if (!rig.proven) return;
  out->gun_parent = rig.gun_parent;
  out->has_camera = rig.has_camera, out->camera_parent_is_rig_eye = rig.camera_parent_is_rig_eye;
  out->has_eye = rig.has_eye, out->has_rig_eye = rig.has_rig_eye;
  for (int i = 0; i < 3; i++) out->eye[i] = rig.eye[i], out->rig_eye[i] = rig.rig_eye[i];
}
/* The two hooks are there from the start (nothing calls them while the hold is off); the switch itself is option_hold's. */
static void hold_start(void) { /* after tfp_wire_init and tfp_wire_options_start, before on_enable */
  wire.hold.apply = hold_apply, wire.hold.probe = hold_probe;
  if (!hold_lease) { say("Hold: off: an action (weapon swap, grenade, stim) ends the native first-person view, as before the hold"); return; }
  say("Hold: on: while a lease runs, an action (weapon swap, grenade, stim) no longer ends the game's "
      "first-person camera request as long as that request is still the lease's own (kind 6, same record, same player, not a ragdoll, "
      "not seated, not full aim, no other camera owner); at most 4000 ms, then the exit as without the hold");
  say("Hold: the pose helper keeps its placement through an action; \"Hold:\" lines say what the rig and the camera do; %s",
      arms_live ? "the arm drive is on: while a lease runs it keeps its binding when the gun leaves the rig, and puts the game's joint "
                  "values back after every draw during a hold and in any frame whose gun is away"
                : "the arm drive is off, so the rig's arms are the game's own during a hold");
}

/* ---- the three live features' setters (src/wire_options.h) ----------------------------------------------
 * Called on the game thread only: once from start(), then from the tick when the answer changed, and never while
 * the controller holds a lease through an action or waits for a lease's native exit (tfp_wire_options_busy).
 *
 * The arm drive: only the flag. arms_tick, later in the same tick, does the rest with the discipline it has for
 * F11: on, the render hook goes in (retried every 500 ms, refusals said once); off, the drive is switched off,
 * the game's joint values are put back here on the game thread, and only then the hook comes out. */
static void option_arms(void *user, bool on) { (void)user; arms_live = on; }
/* The action hold: the controller's switch (an exit an action causes is held back, or taken as in retail), the
 * backend's (how a lease is released after a hold) and the binder's per-tick part. Off, the pose helper's tolerance
 * and the arm drive's two hold flags are cleared once, since the binder's tick no longer reaches them. No lease is
 * being held or released when this runs, so no native request changes owner here: with a lease running and no action
 * the next action is simply held (on) or ends the native view as in retail (off). */
static void option_hold(void *user, bool on) {
  const bool was_on = hold_lease; /* never on: the pose helper and the arm drive were never told of a hold, and are not now */
  (void)user;
  hold_lease = on;
  tfp_wire_hold_set(&wire, on);
  if (!on && was_on) hold_apply(&wire, false, false);
}
/* The eye's continuity: one atomic flag the camera hook reads each frame. Off, a correction still being carried is
 * dropped with the next frame (at most one small step, never during a hold); on, it starts from the next change of
 * camera. Without the camera lock (lock_off.txt) it stays off. */
static void option_smooth(void *user, bool on) {
  (void)user;
  smooth_wanted = on;
  tfp_camera_set_continuity(lock_wanted && on, lock_wanted && trace_wanted);
}

/* ---- the arm drive (src/arm_drive.c, src/render_slot.c) ----------------------------------------------
 * Start-up: the switches, and whether the render hook's contract holds (reads only). */
static void arms_start(void) {
  tfp_render_slot_setup(hf);
  tfp_arm_drive_set_bend(arms_bend);
  if (arms_bend) say("Test: arm_bend.txt: while the native first-person view is shown, the first-person rig's l_shoulder (node 10) is turned "
                     "70 degrees about its own Z at the primary draw; the body is ignored");
  else if (arms_live) say("Arms: on: the body's arm animation is written onto the first-person arm rig at the primary draw while the "
                          "native first-person view is shown (42 joints)");
  else say("Arms: off: the first-person rig's arms are the game's own");
  if ((arms_live || arms_bend) && arms_restore) say("Test: arms_live_restore.txt: the rig's own joint values are put back after every primary draw");
  else if (arms_live || arms_bend) say("Arms: written joint values stay until the next frame, where the game's own are recovered (a file "
                                       "arms_live_restore.txt beside the DLL puts them back after every draw instead)");
  if (arms_live && arms_rest_trace) say("Test: rest_trace.txt: the \"Rest:\" line of the resting weapon pose comes every tick instead of once a second");
  const char *why = tfp_render_slot_contract();
  if (why) {
    snprintf(arms_refusal, sizeof arms_refusal, "%s", why);
    say("Arms: render hook contract refused: %s. The arm drive stays off while that holds.", why);
  }
  else say("Arms: render hook contract passed: the primary draw (game+0xab7ac7) calls the world renderer (exe+0x318150) through the "
           "application table's slot +0x28, which holds it; %s", arms_live || arms_bend ? "the hook goes in when the view is switched on"
           : "the hook is not put in while Animated arms is off (it goes in when the option is switched on with the view on)");
}
/* Every tick while running. The hook is in only while the drive is switched on (Animated arms, or arm_bend.txt, and the view on);
 * when it goes off the game's joint values are put back first, here on the game thread, then the hook comes out. */
static void arms_tick(void) {
  static uint64_t next_try, next_note;
  static bool hook_fired_told;
  static TfpArmDriveStats before;
  const uint64_t now = GetTickCount64();
  const bool wanted = (arms_live || arms_bend) && wire.controller.enabled;
  tfp_render_slot_tick();
  { /* While aiming the game's own aim pose shows (src/arm_weight.h): the copy fades out and back in 120 ms. */
    static float weight = 1; static uint64_t weighed;
    static bool aiming; /* the last tick's answer: kept when no witness can be asked (tfp_arm_aiming) */
    const TfpMotionRuntime *aim_motion = tfp_wire_motion_runtime(&wire);
    const bool active = aim_motion && aim_motion->state.phase == TFP_MOTION_PHASE_ACTIVE;
    const bool holding = wire.controller.holding;
    bool flags_read = false, flags_aim = false;
    if (wanted && !active && !holding) { /* no running lease, or a refused sample: the player's own flags, read here on the game thread */
      TfpOpsSnapshot player;
      TfpOpsText why;
      if (tfp_ops_player_snapshot(&wire.ops, &player, &why) == TFP_OPS_TRUE)
        flags_read = true, flags_aim = player.controller_active || player.shoulder_aim || player.aiming;
    }
    aiming = tfp_arm_aiming(active, active && (aim_motion->state.action == TFP_ACT_AIM || aim_motion->state.action == TFP_ACT_ADS), holding,
                            flags_read, flags_aim, wanted && aiming);
    static bool was_wanted;
    if (wanted && !was_wanted) weight = aiming ? 0 : 1; /* the drive starts: nothing of it was shown, so there is nothing to fade from */
    was_wanted = wanted;
    weight = tfp_arm_weight_step(weight, aiming, weighed ? (float)(now - weighed) : 0); weighed = now;
    tfp_arm_drive_set(wanted, arms_restore, weight);
  }
  { /* The resting weapon pose (src/arm_drive.h): the clock, whether this tick is a rest and in which stance, and the body's layer states.
     * The action alone is not a rest: a walk, hip fire, an ammo check and a melee are "idle" to it (src/action_select.c), and in the game
     * a reload was too. So also: the speed over the ground, the look pitch and the fire button (the left mouse button: the game's key
     * bindings are not read, so fire on another key is not seen). An ammo check, a melee and a reload that the game does not flag are
     * told from a rest by nothing here; the drive's own rule is for them (a measurement is kept only when a second one confirms it). */
    const TfpMotionRuntime *motion = tfp_wire_motion_runtime(&wire);
    const bool live = motion && motion->state.phase == TFP_MOTION_PHASE_ACTIVE && wire.log.has_lease && !wire.controller.holding;
    const TfpAct act = live ? motion->state.action : TFP_ACT_NONE;
    TfpArmRestSample sample = {.stance = act == TFP_ACT_IDLE ? TFP_ARM_REST_STAND : act == TFP_ACT_CROUCH ? TFP_ARM_REST_CROUCH
                                         : act == TFP_ACT_PRONE ? TFP_ARM_REST_PRONE : TFP_ARM_REST_NONE,
                               .fire = tfp_win_focused() && tfp_win_key_down(VK_LBUTTON)};
    if (live) {
      sample.has_speed = motion->state.has_speed, sample.speed = motion->state.speed;
      sample.has_pitch = motion->model.has_previous, sample.pitch = motion->model.previous.pitch; /* of the model's last sample: the camera's forward */
      sample.has_layers = motion->state.has_l1 && motion->state.has_l3 && motion->state.has_l14;
      sample.l1 = motion->state.l1, sample.l3 = motion->state.l3, sample.l14 = motion->state.l14;
    }
    static uint64_t told, next_rest;
    uint32_t key = 0;
    unsigned why = 0;
    const unsigned rest = tfp_arm_drive_rest_of(&sample, &key, &why);
    TfpArmDriveStats a;
    tfp_arm_drive_set_rest(now, rest, key);
    tfp_arm_drive_set_rest_why(why);
    tfp_arm_drive_stats(&a);
    if (a.align.count != told) told = a.align.count, say("Arms: %s", tfp_arm_drive_align_line(&a.align));
    if (arms_rest_trace || now >= next_rest) { /* once a second; with rest_trace.txt every tick that had a driven frame */
      TfpArmDriveRest seen;
      next_rest = now + 1000;
      tfp_arm_drive_rest_take(&seen);
      if (seen.frames) say("Rest: %s", tfp_arm_drive_rest_line(&seen));
    }
  }
  if (wanted) {
    if (!tfp_render_slot_installed() && now >= next_try) {
      next_try = now + 500;
      const char *why = tfp_render_slot_install();
      if (!why) say("Arms: render hook in (application table +0x28, was the world renderer exe+0x318150)");
      else if (strcmp(why, arms_refusal)) { /* one line per reason, not one per try */
        snprintf(arms_refusal, sizeof arms_refusal, "%s", why);
        say("Refused: arms: the render hook was not put in: %s", why);
      }
    }
  } else if (tfp_render_slot_installed() && tfp_arm_drive_release()) {
    tfp_render_slot_remove();
    say("Arms: render hook out; the first-person rig holds the game's own joint values");
  }
  const TfpWinHook *hook = tfp_render_slot_hook();
  if (hook->first_thread && !hook_fired_told) {
    hook_fired_told = true;
    say("Hook: primary draw first passed on thread %lu, called from game+%#llx (the tick is thread %lu)", (unsigned long)hook->first_thread,
        (unsigned long long)(hook->first_return - tfp_win_game()), (unsigned long)GetCurrentThreadId());
  }
  if (now < next_note) return;
  next_note = now + 1000;
  TfpArmDriveStats s;
  tfp_arm_drive_stats(&s);
  if (s.frames == before.frames && s.refused == before.refused && s.idle == before.idle && s.restores == before.restores) return;
  say("Arms: +%llu frames driven, %llu joints, %llu pieces refreshed, binds %llu, refused %llu (%s); +%llu frames in another camera, %u joints held",
      (unsigned long long)(s.frames - before.frames), (unsigned long long)(s.joints - before.joints), (unsigned long long)(s.pieces - before.pieces),
      (unsigned long long)s.binds, (unsigned long long)(s.refused - before.refused),
      s.refused != before.refused ? tfp_arm_drive_reason_name(s.reason) : "none", (unsigned long long)(s.idle - before.idle), (unsigned)s.owned);
  before = s;
  { /* what the mod costs: its tick and the arm drive's part of the draw, per frame, in the second just past */
    uint64_t drawn = 0; double mean = 0, most = 0;
    tfp_render_slot_cost(&drawn, &mean, &most);
    LARGE_INTEGER rate; QueryPerformanceFrequency(&rate);
    const double us = rate.QuadPart ? 1e6 / (double)rate.QuadPart : 0;
    say("Cost: tick %.0f us mean, %.0f us largest over %ld ticks; arms in the draw %.0f us mean, %.0f us largest over %llu frames",
        tick_cost_count ? (double)tick_cost_total * us / (double)tick_cost_count : 0, (double)tick_cost_max * us, (long)tick_cost_count, mean, most,
        (unsigned long long)drawn);
    tick_cost_total = tick_cost_max = 0, tick_cost_count = 0;
  }
}
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
  {
    /* Test switch: a file "head_parts.txt" beside the DLL holding one digit 1-7 (bit 0 head, 1 torso, 2 shoulders). */
    char parts_path[MAX_PATH];
    HMODULE self_module;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(void *)read_settings, &self_module)) {
      DWORD length = GetModuleFileNameA(self_module, parts_path, MAX_PATH);
      char *slash = length && length < MAX_PATH - 20 ? strrchr(parts_path, '\\') : NULL;
      if (slash) {
        strcpy(slash + 1, "head_parts.txt");
        FILE *parts_file = fopen(parts_path, "r");
        int digit = parts_file ? fgetc(parts_file) : EOF;
        if (parts_file) fclose(parts_file);
        strcpy(slash + 1, "arms_on.txt");
        FILE *arms_file = fopen(parts_path, "r");
        if (arms_file) fclose(arms_file);
        tfp_wire_engine_allow_arms(arms_file != NULL);
        if (arms_file) say("Test: arms_on.txt: sleeves and hand grips are allowed to run");
        strcpy(slash + 1, "lock_off.txt");
        FILE *lock_file = fopen(parts_path, "r");
        if (lock_file) fclose(lock_file);
        lock_wanted = lock_file == NULL;
        strcpy(slash + 1, "smooth_off.txt");
        FILE *smooth_file = fopen(parts_path, "r");
        if (smooth_file) fclose(smooth_file);
        if (smooth_file) options.force_off[TFP_FEATURE_SMOOTH] = "smooth_off.txt";
        strcpy(slash + 1, "trace_off.txt");
        FILE *trace_file = fopen(parts_path, "r");
        if (trace_file) fclose(trace_file);
        trace_wanted = trace_file == NULL;
        strcpy(slash + 1, "mcm_on.txt");
        FILE *mcm_file = fopen(parts_path, "r");
        if (mcm_file) fclose(mcm_file);
        mcm_wanted = mcm_file != NULL;
        if (mcm_wanted) say("Test: mcm_on.txt: the settings menu is registered with DBF-MCM (goose.mcm) instead of the mod's own panel");
#ifdef TFP_FEEL
        static const char *const arm_files[] = {"arms_live_restore.txt", "arm_bend.txt", "rest_trace.txt"};
        bool *const arm_switches[] = {&arms_restore, &arms_bend, &arms_rest_trace};
        for (int i = 0; i < 3; i++) {
          strcpy(slash + 1, arm_files[i]);
          FILE *arm_file = fopen(parts_path, "r");
          if (arm_file) fclose(arm_file);
          *arm_switches[i] = arm_file != NULL;
        }
        /* The overrides of the three live features (src/wire_options.h): for testing only. */
        static const struct { const char *name; TfpFeature feature; bool on; } forced[] = {
          {"arms_live.txt", TFP_FEATURE_ARMS, true}, {"arms_off.txt", TFP_FEATURE_ARMS, false},
          {"hold_lease.txt", TFP_FEATURE_HOLD, true}, {"hold_off.txt", TFP_FEATURE_HOLD, false}};
        for (size_t i = 0; i < sizeof forced / sizeof *forced; i++) {
          strcpy(slash + 1, forced[i].name);
          FILE *forced_file = fopen(parts_path, "r");
          if (!forced_file) continue;
          fclose(forced_file);
          *(forced[i].on ? &options.force_on[forced[i].feature] : &options.force_off[forced[i].feature]) = forced[i].name;
        }
#endif
        if (digit >= '1' && digit <= '7') {
          tfp_head_set_parts((unsigned)(digit - '0'));
          say("Test: head_parts.txt: hiding parts mask %d (1 head, 2 torso, 4 shoulders)", digit - '0');
        }
      }
    }
  }
  /* What build 25480438 does differently during actions (src/wire_action.h). */
  tfp_camera_set_action_tuning(TFP_ACTION_PICKUP_MASK, TFP_ACTION_EYE_IDLE_MS, TFP_ACTION_EYE_RETURN);
  /* The camera lock (docs/design/specs/2026-11-22-locked-view-design.md): first person through every action and camera. */
  tfp_camera_set_lock(lock_wanted, .35f, 8.0f, 300.0f, .7f, 35.0f);
#ifdef TFP_FEEL
  tfp_wire_engine_set_held_body(tfp_camera_lock_off_foot_body); /* head hiding stays on while the lock holds a body that is off foot */
  tfp_wire_engine_set_lock_view(lock_view_now);                 /* and while the game presents the lease's own first-person request (a stim) */
#endif
  say(lock_wanted ? "Ready: camera lock: the view stays first person through every action and camera of your own character, death included; "
                    "body movement kicks the view (35 %% up to 8 degrees on foot, 70 %% up to 35 when thrown; a file lock_off.txt beside the DLL switches it off)"
                  : "Test: lock_off.txt: the camera lock is off");
  tfp_camera_set_continuity(false, lock_wanted && trace_wanted); /* the continuity itself: option_smooth, once the settings are read */
  if (lock_wanted && !trace_wanted) say("Test: trace_off.txt: no \"Trace:\" lines after State edges");
  say("Ready: action camera: the head camera also takes the pick-up action (mask 0x%x); the eye is held through %u ms without an action "
      "(a stim) and glides back before the weapon returns; the aim button is read from Windows, again as the script update begins",
      TFP_ACTION_PICKUP_MASK, TFP_ACTION_EYE_IDLE_MS);
#ifdef TFP_FEEL
  tfp_win_mouse_engine(&mouse_engine);
  eng_set_script_probe(aim_script_probe);
#endif
  measure_reads();
#ifdef TFP_FEEL
  {
    char where[MAX_PATH + 32], where_locked[MAX_PATH + 32], text[360];
    bool user_file = false;
    TfpWireFeelParts parts;
    memset(&parts, 0, sizeof parts);
    parts.settings_length = read_settings(where, sizeof where, &user_file);
    parts.settings_json = parts.settings_length ? settings_text : NULL;
    parts.settings_origin = where[0] ? where : "settings.json";
    parts.locked_length = read_locked(where_locked, sizeof where_locked);
    parts.locked_json = parts.locked_length ? locked_text : NULL;
    parts.locked_origin = where_locked[0] ? where_locked : "locked_view.json";
    parts.pose = tfp_wire_pose_linked(), parts.pose_io = tfp_win_pose_io();
    parts.engine = engine_hooked ? tfp_win_engine_api() : NULL;
    if (!engine_hooked) say("Refused: engine bridge: %s", eng_status());
    tfp_win_effects_platform(&fx_platform, tfp_win_exe());
    parts.effects = &fx_platform;
    parts.editor = tfp_mod_editor_platform(&fx_platform);
    tfp_wire_feel_bind(&wire, &feel, &parts);
    /* Where the two files are saved. A settings.json of 0.6.0-feel.20 to feel.24 is converted here, at once. */
    static const TfpSettingsDisk user_disk = {NULL, write_user_file, copy_user_file};
    tfp_wire_settings_disk(&wire, &user_disk, user_file);
    /* The three live features, now that the settings are read: each setter once, and a line each that says whether the
     * feature is on and whether the option or a file decided. Then what each one's start-up has to say. */
    options.set[TFP_FEATURE_ARMS] = option_arms, options.set[TFP_FEATURE_HOLD] = option_hold, options.set[TFP_FEATURE_SMOOTH] = option_smooth;
    tfp_wire_options_start(&wire, &options);
    arms_start();
    hold_start();
    if (!lock_wanted) say("Smooth: eye continuity stays off whatever the option says: the camera lock is off (lock_off.txt)");
    else say(smooth_wanted ? "Smooth: on: while the lock holds the view, a change of camera (head camera, native view) and a one-frame move of the "
                             "first-person rig are carried as a correction of at most 1 m and eased out over 100 to 400 ms; farther than 1.5 m is "
                             "shown at once; nothing is taken up in full aim"
                           : "Smooth: off: changes of camera and rig moves are shown as they are");
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
      "their checks pass: see the Ready / Check / Refused lines). F10 opens the mod's own settings panel when NEST (Goose Overlay "
      "Host) is loaded; it is also in the host's panel list (Page Down). DBF-MCM is not needed and not used (a file mcm_on.txt beside "
      "the DLL puts the menu there instead). Settings are saved when they change. Page Up (the menu's editor key) opens the placement "
      "editor; F8 is HellForge's own editor. Not in this build: the eye-camera editor, Photo Mode camera sharing.");
#else
  say("Ready. F11 toggles the view. Not in this build: arms and weapon placement, sleeves, hand grips, motion, head and body hiding, "
      "depth of field, vignette, aiming markers, movement policy, the F8 editor, the menu, Photo Mode camera sharing.");
#endif
}

#ifdef TFP_FEEL
static unsigned menu_unavailable(void) {
  bool arms = tfp_wire_engine_state(&wire, TFP_WE_SLEEVES) != TFP_WE_REFUSED || tfp_wire_engine_state(&wire, TFP_WE_GRIP) != TFP_WE_REFUSED;
  return (arms ? 0 : TFP_MENU_NO_ARMS) | (tfp_mod_editor_available() ? 0 : TFP_MENU_NO_EDITOR);
}
/* A live replace asked for the mod's registrations with other mods to be taken out; this is the game
 * thread, where DBF-MCM allows it. Nothing is released while the mod still holds something in the game. */
static volatile LONG release_state; /* 0 not answered, 1 released, 2 refused (release_why) */
static const char *volatile release_why;
static void release_on_game_thread(void) {
  const char *why = stage == RUNNING ? tfp_wire_feel_held(&wire) : NULL;
  if (!why) { /* the arm drive: its joint values go back here, on the game thread, then its hook comes out */
    tfp_arm_drive_set(false, false, 1.0f);
    if (tfp_arm_drive_release()) tfp_render_slot_remove();
    else why = tfp_arm_drive_held();
  }
  if (!why && !tfp_mod_menu_release(hf, owner, &wire)) why = "DBF-MCM did not release the settings menu";
  release_why = why;
  InterlockedExchange(&release_state, why ? 2 : 1);
}
#endif
/* docs/design/QUIT-CRASH.md. The engine's own release routine for a {pointer, count} pair is hooked once; when it is
 * called for the one object whose release faults, each of its three pairs that the block allocator cannot own is
 * emptied first, so the routine skips it. Those few pages are then simply left to the process exit. Every other call passes through untouched. */
static void (*quit_release_original)(uint64_t *pair);
static void quit_release_hook(uint64_t *pair) {
  uint64_t state = 0;
  if (pair && tfp_win_read(tfp_win_game() + TFP_QUIT_GUARD_STATE, &state, sizeof state) && state
      && (uint64_t)(uintptr_t)pair == state + TFP_QUIT_GUARD_TABLE) {
    for (int i = 0; i < TFP_QUIT_GUARD_PAIRS; i++)
      if (tfp_quit_guard_faulty(pair[i * 2])) pair[i * 2] = 0, *(uint32_t *)&pair[i * 2 + 1] = 0;
  }
  quit_release_original(pair);
}
static void quit_guard(void) {
  static bool tried;
  static const uint8_t expected[20] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20};
  uint8_t code[20];
  if (tried) return;
  tried = true;
  uint64_t at = tfp_win_game() + TFP_QUIT_GUARD_RELEASE;
  if (!tfp_win_read(at, code, sizeof code) || memcmp(code, expected, sizeof code)) {
    say("Quit guard: off: game.dll+0x%x is not the release routine of build 25480438 (or another mod already hooks it)", TFP_QUIT_GUARD_RELEASE);
    return;
  }
  if (hf->hook(owner, (uintptr_t)at, (void *)quit_release_hook, (void **)&quit_release_original) != 0 || !quit_release_original) {
    quit_release_original = NULL;
    say("Quit guard: off: HellForge refused the hook at game.dll+0x%x", TFP_QUIT_GUARD_RELEASE);
    return;
  }
  say("Quit guard: on: the game's faulty release of its table at state+0x%x is skipped while closing (game.dll+0x%x)", TFP_QUIT_GUARD_TABLE, TFP_QUIT_GUARD_RELEASE);
}

static void fov_lock_tick(void) { /* every tick: the field of view is locked while the view is being shown */
  static uint64_t frames, shown_at;
  const uint64_t now = GetTickCount64(), held = tfp_camera_lock_frames();
  if (held != frames) { frames = held; shown_at = now; }
  const bool shown = shown_at && now - shown_at < 250 && lock_wanted && tfp_locked_view_fov_lock(&wire);
  tfp_fov_set_lock(shown ? (float)(tfp_locked_view_fov_degrees(&wire)*3.14159265358979/180) : 0, tfp_locked_view_fov_zoom_when_aiming(&wire), (double)now);
}
static void lock_note(void) { /* once a second while the view is on: frames by camera, frames missed, the largest kick */
  static uint64_t next, told_frames, told_foreign, told_missed;
  const uint64_t now = GetTickCount64();
  if (now < next) return;
  next = now + 1000;
  /* The Locked view tab's options (src/wire_settings.h); lock_off.txt beside the DLL overrides the toggle. */
  tfp_camera_set_lock(lock_wanted && tfp_locked_view_camera_lock(&wire), (float)(tfp_locked_view_kick_strength(&wire)/100),
                      (float)tfp_locked_view_kick_max_degrees(&wire), 300.0f, (float)(tfp_locked_view_thrown_kick_strength(&wire)/100), 35.0f);
  TfpCameraLockStats s; tfp_camera_lock_stats(&s);
  if (s.frames == told_frames && s.missed == told_missed) return;
  /* The kind and error are the last missed frame's: 0 in a second without one. tools/harness/matrix.py reads this line. */
  const bool missed = s.missed != told_missed;
  say("Lock: +%llu frames held (+%llu in another camera, kind now %u), +%llu missed (kind %u, helper error %u), largest kick %.1f degrees",
      (unsigned long long)(s.frames - told_frames), (unsigned long long)(s.foreign_frames - told_foreign), s.last_kind,
      (unsigned long long)(s.missed - told_missed), missed ? s.missed_kind : 0, missed ? s.missed_error : 0, (double)s.peak_kick_degrees);
  if (s.last_kind != 6 && (s.off_foot || s.ragdoll) && s.frames != told_frames)
    say("Body: off foot %u, ragdoll flag %u, head %.2f m from its root (the last head-camera frame the lock held)", s.off_foot, s.ragdoll, (double)s.head_distance);
  if (missed) say("Missed: kind %u, helper error %u (%s); the body on the last frame held: off foot %u, ragdoll flag %u, head %.2f m from its root",
                  s.missed_kind, s.missed_error, tfp_camera_why_text(s.missed_why), s.off_foot, s.ragdoll, (double)s.head_distance);
  told_frames = s.frames; told_foreign = s.foreign_frames; told_missed = s.missed;
  /* While the body is down under the lock (physics has it: thrown or dead; camera_helper.h TfpCameraDownStats). */
  static uint64_t told_down, told_refused; TfpCameraDownStats d; tfp_camera_down_stats(&d);
  if (d.frames != told_down || d.refused != told_refused) {
    char reason[200];
    if (d.refused != told_refused) snprintf(reason, sizeof reason, "helper error %u: %s", d.error,
                                            d.kind == 0xffff ? "the camera record is not the player's" : tfp_camera_why_text(d.why));
    else snprintf(reason, sizeof reason, "none; ragdoll flag %u, off foot %u, head %.2f m from its root", d.ragdoll, d.off_foot, (double)d.head_distance);
    say("Dead: kind=%u head=(%.3f %.3f %.3f) frames=+%llu refused=+%llu (%s)", d.kind, (double)d.head[0], (double)d.head[1], (double)d.head[2],
        (unsigned long long)(d.frames - told_down), (unsigned long long)(d.refused - told_refused), reason);
    told_down = d.frames; told_refused = d.refused;
  }
  if (smooth_wanted) {
    static uint64_t told_jumps, told_edges;
    TfpCameraContinuityStats c; tfp_camera_continuity_stats(&c);
    if (c.jumps != told_jumps || c.edges != told_edges)
      say("Smooth: +%llu changes of camera and +%llu rig moves taken up, largest correction %.3f m (%llu restarts so far)",
          (unsigned long long)(c.edges - told_edges), (unsigned long long)(c.jumps - told_jumps), (double)c.largest_m, (unsigned long long)c.restarts);
    told_jumps = c.jumps; told_edges = c.edges;
  }
  static uint64_t told_fov; TfpFovLockStats f; tfp_fov_lock_stats(&f);
  if (f.applied != told_fov) say("Fov: +%llu frames locked; the game asked for %.1f degrees, shown %.1f", (unsigned long long)(f.applied - told_fov),
                                (double)f.game_radians*57.29578, (double)f.shown_radians*57.29578);
  told_fov = f.applied;
}
static void tick_body(void) {
  if (stage == RUNNING) { quit_guard(); fov_lock_tick(); lock_note(); }   /* not earlier: the game module and the guarded reader are set up in WAIT_MODULES */
  if (stage == REFUSED) return;
  tick_thread = GetCurrentThreadId();
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
  /* The editor's key (the menu's; default Page Up), on its edge. The panel is registered before the menu, so
   * the menu knows whether to offer the editor. */
  int editor_key = tfp_wire_menu_editor_key(&wire, TFP_EDITOR_KEY);
  tfp_mod_editor_tick(hf, owner, editor_key);
  bool editor_down = editor_key && tfp_win_focused() && tfp_win_key_down(editor_key);
  if (editor_down && !editor_key_held) {
    const char *refused = tfp_wire_editor_toggle(&wire);
    if (refused) say("Placement editor: %s", refused);
  }
  editor_key_held = editor_down;
  if (tfp_mod_editor_settled()) tfp_mod_panel_tick(hf, owner, &wire, mcm_wanted, menu_unavailable(), tfp_wire_editor_toggle);
  tfp_wire_frame(&wire, extra);
  entry_note();
  orphan_note(); /* before the trace: its edges open a trace window too */
  trace_note();
  tfp_wire_panel_frame(&wire);         /* what the user did in the mod's own panel since the last tick */
  tfp_wire_menu_frame(&wire);          /* the menu's queued buttons and toggles, and the two status rows */
  tfp_wire_options_tick(&wire, &options); /* Animated arms, Weapon put-away, Smooth eye: a changed answer reaches its setter (not during a hold) */
  tfp_wire_settings_save_frame(&wire); /* writes only when the store changed since the last write */
  eng_temp_end();
  arms_tick();
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
  if (!InterlockedCompareExchange(&closing, 0, 0)) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    tick_body();
    QueryPerformanceCounter(&t1);
    const LONG64 spent = t1.QuadPart - t0.QuadPart;
    tick_cost_total += spent, tick_cost_count++;
    if (spent > tick_cost_max) tick_cost_max = spent;
  }
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
  restored += tfp_render_slot_restore();
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
    if (!why) why = tfp_arm_drive_held(); /* joint values can only be put back from the game thread */
  }
  unsigned render_restored = 0;
  if (!why) {
    /* The render hook comes out first; a world render that is inside it now (it stays inside for a whole draw)
     * must have returned before this build may be taken out of memory. */
    render_restored = tfp_render_slot_restore();
    for (int waited = 0; !tfp_render_slot_idle(); waited++) {
      if (waited >= 2000) { why = "a world render did not leave this mod's render hook within 2 seconds"; break; }
      Sleep(1);
    }
  }
  if (why) {
    InterlockedExchange(&release_state, 0);
    InterlockedExchange(&closing, 0);
    hf->log(mod, "Refused: live %s: %s. This build stays for the session.", reason == HF_UNLOAD_REPLACE ? "replace" : "removal", why);
    return 1;
  }
  eng_set_script_probe(NULL);
  tfp_mod_editor_retire();   /* its panel stays registered with the overlay host: closed, and drawing nothing */
  tfp_mod_panel_retire();    /* the settings panel likewise */
  unsigned restored = tfp_win_restore_slots() + tfp_win_pose_restore_slots() + render_restored;
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
