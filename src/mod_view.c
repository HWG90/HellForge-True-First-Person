/* True First Person for HellForge, view build 0.6.0-view.1 (game build 25480438).
 *
 * U (the menu's toggle key; F11 until round 36) toggles the game's own first-person camera through the translated retail units: the runtime
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
#include "perf_line.h"
#include "trace_row.h"
#include "wire_warp.h"
#include "engine.h"
#include "mod_menu.h"
#include "mod_panel.h"
#include "wire_panel.h"
#include "mod_editor.h"
#include "render_slot.h"
#include "arm_drive.h"
#include "wire_options.h"
#include "arm_weight.h"
#include "trace_edges.h"
#define TFP_VIEW_VERSION "0.6.0-feel.59"
#else
#include "wire_win.h"
#include "wire_action.h"
#include "camera_helper.h"
#define TFP_VIEW_VERSION "0.6.0-view.1"
#endif
#include "camera_damp.h"
#ifdef TFP_FEEL
#include "aim_log.h"
#include "near_helper.h"
#include "pose_helper.h"
#include "fov_helper.h"
#include "placement_editor.h"
#include "editor_motion.h"
#include "aim_bind.h"
#endif
#define TFP_CONFLICT_ID "goose.first-person-controller"

static const HfApi *hf;
static HfMod *owner;
static TfpWire wire;
static enum { WAIT_MODULES, WAIT_PINS, RUNNING, REFUSED } stage;
static bool told_waiting, told_fields_waiting, told_tick_thread, hook_told[TFP_WIN_HOOKS];
#ifdef TFP_FEEL
/* The two bound keys' presses. A key bound in the menu is still down at the next tick: it acts only once it has been let go. */
static TfpKeyEdge toggle_edge, editor_edge;
#else
static bool key_held;
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
/* Round 28 (src/perf_line.h, docs/design/specs/2026-11-25-performance.md): the tick in sections, for the Perf line.
 * Logging only: QueryPerformanceCounter at each section's end (about ten a tick), nothing allocated or formatted. */
static int64_t perf_tick[TFP_PERF_SECTIONS], perf_total[TFP_PERF_SECTIONS], perf_most[TFP_PERF_SECTIONS];
static int64_t perf_qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static void perf_lap(int section, int64_t *mark) { const int64_t now = perf_qpc(); perf_tick[section] += now - *mark; *mark = now; }
/* The depth of field's and the vignette's time since the last take (src/wire_effects.c), taken out of `from`. */
static void perf_effects(int from) {
  int64_t dof = 0, vignette = 0;
  tfp_wire_effects_take_spent(&wire, &dof, &vignette);
  perf_tick[TFP_PERF_DOF] += dof, perf_tick[TFP_PERF_VIGNETTE] += vignette;
  perf_tick[from] -= dof + vignette;
  if (perf_tick[from] < 0) perf_tick[from] = 0;
}
/* After the tick: what is left is rest; each section into the second's sums. */
/* Round 32: the frame's laps (src/perf_line.h TFP_LAP_*, timed by the wire), per tick into the second's sums. */
static int64_t lap_total[TFP_LAPS], lap_most[TFP_LAPS];
/* Round 35: on_update's parts (TFP_UPD_*) and the backend's snapshot calls (every caller), per tick into the second's sums. */
static int64_t part_total[TFP_UPDS], part_most[TFP_UPDS], snap_total, snap_most;
static uint64_t snap_told;
static void perf_laps_close(void) {
  int64_t lap[TFP_LAPS];
  memcpy(lap, wire.laps.tick, sizeof lap);
  memset(wire.laps.tick, 0, sizeof wire.laps.tick);
  tfp_perf_laps_close(lap);
  for (int i = 0; i < TFP_LAPS; i++) {
    lap_total[i] += lap[i];
    if (lap[i] > lap_most[i]) lap_most[i] = lap[i];
  }
  for (int i = 0; i < TFP_UPDS; i++) {
    part_total[i] += wire.laps.part[i];
    if (wire.laps.part[i] > part_most[i]) part_most[i] = wire.laps.part[i];
  }
  memset(wire.laps.part, 0, sizeof wire.laps.part);
  const uint64_t now = wire.ops.perf.snapshot_ticks; /* a backend made anew starts at 0 again */
  const int64_t snap = (int64_t)(now >= snap_told ? now - snap_told : now);
  snap_told = now;
  snap_total += snap;
  if (snap > snap_most) snap_most = snap;
}
/* Round 32: a running count's growth since the last line (a backend made anew starts its counts at 0 again). */
static uint32_t perf_delta(uint64_t now, uint64_t *told) {
  const uint64_t d = now >= *told ? now - *told : now;
  *told = now;
  return d > UINT32_MAX ? UINT32_MAX : (uint32_t)d;
}
static void perf_close(int64_t spent) {
  perf_laps_close();
  perf_effects(TFP_PERF_MENUS); /* a stop from the options or the menu, after the frame */
  int64_t rest = spent;
  for (int i = 0; i < TFP_PERF_REST; i++) rest -= perf_tick[i];
  perf_tick[TFP_PERF_REST] = rest > 0 ? rest : 0;
  for (int i = 0; i < TFP_PERF_SECTIONS; i++) {
    perf_total[i] += perf_tick[i];
    if (perf_tick[i] > perf_most[i]) perf_most[i] = perf_tick[i];
    perf_tick[i] = 0;
  }
}
/* Three features are switched by options of the settings panel's "Locked view" tab, live (src/wire_options.h): the
 * arm drive (Animated arms), the action hold (Weapon put-away) and the eye's continuity (Smooth eye). Test files
 * beside the DLL, read once at the start, override the option: arms_live.txt / hold_lease.txt force the feature on,
 * arms_off.txt / hold_off.txt / smooth_off.txt force it off. The three flags below are what is in effect now; only
 * the setters option_arms / option_hold / option_smooth write them, on the game thread. */
static TfpWireOptions options;
/* Under the camera lock: the eye's continuity (src/camera_continuity.h) and the per-frame "Trace:" lines after each
 * State edge. A file trace_off.txt beside the DLL switches the lines off; neither does anything without the lock. */
static bool smooth_wanted = true, trace_wanted = true;
/* Test switch panel_on.txt: the settings menu is the mod's own panel even when DBF-MCM is loaded (src/wire_panel.h).
 * mcm_on.txt, the switch of builds up to 0.6.0-feel.59, is still read and changes nothing: MCM is the default now. */
static bool panel_wanted;
/* The arm drive (src/arm_drive.h). arms_live: it is on (the Animated arms option, or its files). While it is on, the
 * game's joint values are put back after every primary draw (round 13, 2026-11-24: the game seats the first-person
 * eye by the rig's weapon socket, and a written socket left in the rig moved the camera; src/arm_drive.c). Test
 * switches, files beside the DLL read once at the start: arms_live_keep.txt leaves the written values in the rig until
 * the next frame, as every build before round 13 did, for the comparison in the game; arms_live_restore.txt, the
 * switch that proved put-back in 0.6.0-feel.59, is still read and changes nothing; arm_bend.txt makes the drive bend one
 * shoulder instead of following the body; rest_trace.txt makes the "Rest:" line (the resting weapon pose, once a
 * second) come every tick. */
static bool arms_live, arms_keep, arms_restore, arms_bend, arms_rest_trace, grip_off;
/* Test switch depth_off.txt, read once at the start: the pose helper's zoom-depth term adds nothing on any frame, so
 * neither the rig nor its root is shifted for the optic. It is there to find out in the game whether that shift is
 * part of the unzoom gate's flipping (docs/design/specs/2026-11-23-gate-flip.md). */
static bool depth_off;
/* Test switch depth_steady_off.txt, read once at the start: the rig's depth is not held steady against the game's
 * unzoom gate (src/depth_steady.h); it is still worked out and counted, so the "Steady:" line says what would have
 * been held. For comparing a game run with and without. */
static bool depth_steady_off;
/* Test switch zoom_steady.txt, read once at the start (round 20c, docs/guides/AIM-TEST.md): round20-zoom-shake's two
 * corrections in full aim, the field of view held at the optic's own zoom while the unzoom gate flips
 * (src/fov_lock.h tfp_aim_lens_goal) and the rig's depth held at full strength (src/depth_steady.h tfp_steady_aim and
 * tfp_steady_carry). Without it full aim is feel.59's; the "Aim:" line counts the same either way. */
#ifdef TFP_FEEL
static bool zoom_steady;
#endif
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
/* TfpSettingsDisk.same: <name><suffix> is there and holds the file's bytes as they are now. Anything that cannot be
 * opened or read answers false, and a further copy is then made as before. Game thread only, at a save. */
static bool same_user_file(void *user, TfpSettingsFile which, const char *suffix) {
  static char one[4096], other[4096];
  char path[MAX_PATH + 32], copy[MAX_PATH + 64];
  (void)user;
  if (!user_file_path(path, sizeof path, which, false)) return false;
  snprintf(copy, sizeof copy, "%s%s", path, suffix);
  /* Read while another program has either file open, for writing too: a sharing violation would answer "not the same". */
  const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  HANDLE a = CreateFileA(path, GENERIC_READ, share, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  HANDLE b = a == INVALID_HANDLE_VALUE ? a : CreateFileA(copy, GENERIC_READ, share, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  LARGE_INTEGER size_a, size_b;
  bool same = b != INVALID_HANDLE_VALUE && GetFileSizeEx(a, &size_a) && GetFileSizeEx(b, &size_b) && size_a.QuadPart == size_b.QuadPart;
  for (LONGLONG left = same ? size_a.QuadPart : 0; same && left > 0;) {
    DWORD got_a = 0, got_b = 0;
    same = ReadFile(a, one, sizeof one, &got_a, NULL) && ReadFile(b, other, sizeof other, &got_b, NULL) && got_a && got_a == got_b && !memcmp(one, other, got_a);
    left -= got_a;
  }
  if (b != INVALID_HANDLE_VALUE) CloseHandle(b);
  if (a != INVALID_HANDLE_VALUE) CloseHandle(a);
  return same;
}
#endif

static uint64_t say_lines; /* round 28: lines said, for the Perf line */
static void say(const char *format, ...) {
  char line[700]; /* TFP_LOG_LINE (src/trace_row.h): what is longer is cut; tests/trace_row_test.c holds the Trace row under it */
  say_lines++;
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
 *   source       head (the head camera), native (the game's first-person camera), repeat (its last argument repeated)
 *   root, axes, looked, taken, quiet, tick: what the continuity was given and what it did (below)
 *   off_foot     1: a head-camera frame of a body that was not on foot (a native frame never says it)
 *   damped       1: the eye damping ran in this frame (src/camera_damp.h)
 *   damp         what the damping added to the true eye, in the world: wanted - damp is the head camera's own eye.
 *                With damped=0 it is a lag being let go. (Round 10, 2026-11-24: the wanted eye is written after the
 *                damping, so the lines could not say whether its 0.25 m limit came into force in a frame, nor give
 *                the head's own path for a change to the damping to be tried on.)
 *   view         which way the view pointed, a unit vector in the world (the head camera's forward; in a native frame
 *                the forward axis of the rig eye the camera hangs on). With the root's axes it says whether the root
 *                turned under a still view (a backwards dive) or with it (the mouse): what is carried goes round with
 *                the root either way (W), and nothing the continuity is given tells the two apart.
 *   sock         the weapon socket as the arm drive drew it in this frame less the game's own, along the view (m):
 *                under put-back the gun's depth error on screen; with arms=2, how far the written socket threw the
 *                eye the other way (the eye the game would have had is eye + sock * view). 0 with arms=0.
 *   arms         what the arm drive did at this frame's draw: 0 not driven (off, another camera, refused) or not
 *                known for the frame, 1 driven and the game's joint values put back after the draw, 2 driven and the
 *                written values left (arms_live_keep.txt), 3 driven in a hold or with the gun away (put back)
 *   eye_edge     1: the presented eye's step changed by 0.15 m or more in this frame; the camera helper opened this
 *                window itself, or kept it open (src/trace_edges.h)
 *   turn         round 24: the direction the frame was shown with (the forward axis of the rotation handed to the game,
 *                in the world; in the native view through the rig eye's axes), (0 0 0) where it was not known
 *   veil         round 19: how far the helmet veil moved the eye handed to the game in this frame, in the world
 *                (src/camera_helper.h tfp_camera_set_veil), or 0 where it moved nothing. eye_world is the eye before
 *                it, so the eye shown is eye_world + veil (round 18: up to 0.32 m forward at a leave of the native
 *                view, which no row said); round 26: also the veil's move being let out in the frames after it closes
 *   veil_head    round 26: in a frame the veil was open for, the head node from the eye before the veil: (ahead up), m
 *                along the level view and up (the veil moves the eye less where the head is below the view); else 0
 *   head_shown   round 44: in every head-camera frame of the lock, the head node from the eye shown (eye_world + veil):
 *                (ahead up), m; a negative ahead is the eye that far in front of the head. 0 in any other frame
 *   clear        round 44: how far src/camera_clear.h pushed the eye the frame was built on along the level view (m)
 * Round 49's fields are not on the row (it has no room: src/trace_row.h). They are on two lines written straight after
 * the row they belong to, each with that row's t=, tick= and kind=:
 * "Turn:", in every frame a direction is carried in:
 *   turn_carry   how far the direction shown (the row's turn) is off the game's own in this frame, degrees: the
 *                difference carried across a change of camera and eased out (src/camera_turn.h)
 *   turn_edge    only on the frame a change of camera began a carry: the degrees it began with (the player's own turn
 *                of that frame taken out), then the probe: game_turn, the game's own direction in the world; game_cam,
 *                where the game put its ordinary (kind 4) camera, in this frame at a leave of the native view, in the
 *                last ordinary frame at an entry ("-": not known); heading, the body root's forward axis as a compass
 *                angle, degrees (atan2(x, y)). Why the game's two cameras point apart is not known; this is what a
 *                cause can be read from
 * "Held:", for a row that has either:
 *   hold         only in a head-camera frame whose eye a swap holds: the held difference, the eye from the head
 *                camera's own in the body root's frame (m): the held native eye, its glide to the incoming weapon's
 *                eye, or its release
 *   dive         only where it is 1: the body is in a dive (the motion runtime's action), as the camera helper was
 *                told (Stable's eye damping is let go there)
 * A window is also opened when the body's action changes (src/trace_edges.h); such a window has no State line before
 * it, and "t=" starts again in it. So does one the eye opens itself (eye_edge), unless a window was open. */
static void trace_note(void) {
  static TfpTraceEdges edges;
  /* The body's action, where the motion runtime samples it: a change of it opens a window too (src/trace_edges.h). */
  const TfpMotionRuntime *motion = tfp_wire_motion_runtime(&wire);
  const int action = motion && motion->state.phase == TFP_MOTION_PHASE_ACTIVE && motion->state.action != TFP_ACT_NONE ? (int)motion->state.action : -1;
  const unsigned edge = tfp_trace_edges_step(&edges, wire.log.has_lease, wire.controller.holding, action);
  if ((edge & TFP_TRACE_EDGE_EVENT) && lock_wanted) tfp_camera_continuity_event(); /* a lease or a hold begun or ended: the rig may be re-seated */
  tfp_camera_set_dive(lock_wanted && action == (int)TFP_ACT_DIVE); /* round 49: Stable's eye damping is let go in a dive (src/camera_helper.h) */
  /* Round 24: the native view left because its request or firearm changed (a backpack picked up): the next entry may
   * hold the eye until the rig has settled (THE SETTLE). On the leave only. */
  static bool had_lease;
  if (had_lease && !wire.log.has_lease && lock_wanted && strstr(wire.controller.wait_reason, "Native camera request or firearm changed")) tfp_camera_settle_arm();
  had_lease = wire.log.has_lease;
  if (!lock_wanted || !trace_wanted) return;
  { /* What the arm drive drew in the frame just presented: its draw came after that frame's camera call and before this
     * tick. Said before the rows are read, once for each draw the drive saw. */
    static uint64_t draws;
    TfpArmDriveStats arms;
    tfp_arm_drive_stats(&arms);
    if (arms.draw.count != draws) draws = arms.draw.count, tfp_camera_trace_draw(arms.draw.sock_m, arms.draw.mode);
  }
  if (edge & TFP_TRACE_EDGE_WINDOW) tfp_camera_trace_edge();
  TfpCameraTraceRow rows[48];
  const uint32_t count = tfp_camera_trace_read(rows, 48);
  if (!count) return;
  TfpPoseHoldProbe pose;
  memset(&pose, 0, sizeof pose);
  tfp_pose_hold_probe(&pose);
  const unsigned long long lease = wire.controller.lease ? (unsigned long long)wire.controller.lease->request : 0;
  for (uint32_t i = 0; i < count; i++) {
    const TfpCameraTraceRow *r = &rows[i];
    /* root: the body root the eye was measured against, axes: its three axis rows (the soak of 2026-11-22 on feel.59:
     * a root whose axes turned 0.26 rad in one frame carried the eye 0.18 m, and the lines could not say so); looked:
     * jumps within the camera were looked for; taken:
     * what the continuity did with the frame (jump, edge: taken up; root: the root alone re-seated, nothing taken
     * up; restart; start: it began from nothing in this frame). Without the root a jump of the root cannot be told
     * from a jump of the eye in these lines. quiet: full aim, nothing is taken up. tick: the frame's time on the
     * helper's clock (ms). With these the line holds all the continuity was given, and tools/harness/replay.py can
     * run it again frame for frame (the soaks of 2026-11-22 on feel.59: without the time across edges, full aim and
     * the starts, and with places to 0.1 mm, 29 of 3447 frames could not be replayed: two decisions that fell
     * within the rounding). */
    /* The text is src/trace_row.h's (its length is tested there). Round 49's fields are not on the row, which has no
     * room for them (701 to 706 characters on a dive + swap's edge row, of the 699 a line holds): Turn and Held follow
     * the row they belong to, with its t=, tick= and kind=. */
    char line[TFP_LOG_LINE];
    tfp_trace_row_text(line, sizeof line, r, lease, pose.armed != 0);
    say("%s", line);
    if (tfp_trace_turn_text(line, sizeof line, r) > 0) say("%s", line);
    if (tfp_trace_held_text(line, sizeof line, r) > 0) say("%s", line);
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
/* ---- the "Head:" line (src/head_log.h; docs/design/specs/2026-11-24-helmet-in-view.md) ----------------------------
 * Each tick, after the runtime's update: what head visibility owns of the head, torso and shoulders when that
 * changes, meshes it had seen hidden and found drawn again, meshes it leaves drawn, and its stopping or starting
 * while the view is on, and the veil's counts. Reads the module's own counts; the one thing it sets is the camera
 * helper's veil (round 14): armed while the module is active, closed by the update that switched the body off. */
static void head_note(void) {
  static TfpHeadLog seen;
  TfpHeadTick tick;
  char text[800];
  if (!tfp_wire_engine_head_tick(&wire, &tick)) { tfp_camera_veil_from_head(NULL); return; }
  tick.view = wire.controller.enabled;
  tick.now_ms = (double)GetTickCount64(); /* round 24: how long hiding was let go */
  /* Round 14, the veil (src/camera_helper.h tfp_camera_veil_from_head): this tick's update closes the one a leave
   * opened once it found the body drawn again and left none of it drawn; it stays armed while the module is active. */
  /* Round 26: the field of view the veil judges "the head below the view" with: the wider of the game's and the one
   * shown (the lock off: shown is 0), the widest of the last two seconds (a swap from full aim widens it after the leave). */
  static float fov_widest; static double fov_at;
  TfpFovLockStats fov; tfp_fov_lock_stats(&fov);
  const float fov_now = fmaxf(isfinite(fov.game_radians) ? fov.game_radians : 0, isfinite(fov.shown_radians) ? fov.shown_radians : 0);
  if (fov_now >= fov_widest || tick.now_ms-fov_at > 2000) fov_widest = fov_now, fov_at = tick.now_ms;
  tfp_camera_set_veil_fov(fov_widest);
  tfp_camera_veil_from_head(&tick);
  TfpCameraVeilStats veil;
  tfp_camera_veil_stats(&veil);
  tick.veil_leaves = veil.leaves, tick.veil_frames = veil.frames, tick.veil_moved = veil.moved, tick.veil_behind = veil.behind;
  tick.veil_largest_m = veil.largest_m, tick.veil_second = veil.second; /* round 19 */
  tick.veil_under = veil.under, tick.veil_far = veil.too_far; /* round 26 */
  if (tfp_head_log_step(&seen, &tick, text, sizeof text)) say("%s", text);
}
/* ---- the "Warp:" line (round 22; src/wire_warp.h, docs/design/specs/2026-11-24-warp-pack-glow.md) ----------------- */
static TfpWarp warp;
static bool warp_hide; /* warp_hide.txt beside the DLL: the stops are on (off by default until a game run has proven them) */
static void warp_say(const char *line) { say("%s", line); }
static void warp_note(void) {
  TfpHeadTick tick;
  const bool owns = wire.controller.enabled && tfp_wire_engine_head_tick(&wire, &tick) && tick.active;
  tfp_wire_warp_tick(&wire, &warp, owns, warp_hide, (double)GetTickCount64() / 1000.0, warp_say);
}
#endif

#ifdef TFP_FEEL
/* ---- the action hold (the Weapon put-away option): the binder's two hooks (src/wire.h TfpWireHold) ----------------
 * Each tick, after the controller decided. The pose helper tolerates an action from the moment a lease runs
 * (armed), not only once the controller holds: HellForge runs this tick before the game's update, so the game's
 * first rig write of an action comes before the tick that sees the action. The arm drive is told both: "ready"
 * for the same reason (the action's first drawn frame must not cost its binding and entry alignment when the gun
 * has already left the rig), and the hold itself (the game's joint values go back after every draw there, as they do
 * in every driven frame since round 13; with arms_live_keep.txt only in a hold and with the gun away). */
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
      arms_live ? "the arm drive is on: while a lease runs it keeps its binding when the gun leaves the rig; the game's joint "
                  "values go back after every draw, in a hold as outside one"
                : "the arm drive is off, so the rig's arms are the game's own during a hold");
}

/* ---- the three live features' setters (src/wire_options.h) ----------------------------------------------
 * Called on the game thread only: once from start(), then from the tick when the answer changed, and never while
 * the controller holds a lease through an action or waits for a lease's native exit (tfp_wire_options_busy).
 *
 * The arm drive: only the flag. arms_tick, later in the same tick, does the rest with the discipline it has for
 * The toggle key (U): on, the render hook goes in (retried every 500 ms, refusals said once); off, the drive is switched off,
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
  /* Said whether or not Animated arms is on now: the option is live, and the files are read once. */
  if (tfp_arm_drive_put_back(arms_keep, arms_restore))
    say("Arms: the rig's own joint values are put back after every primary draw, so the game always finds its own weapon socket (it seats the "
        "first-person eye by it); the written values are in the rig for the draw only. A file arms_live_keep.txt beside the DLL leaves them "
        "written until the next frame instead, as the builds before round 13 (2026-11-24) did%s", arms_restore ? "; arms_live_restore.txt is here and changes nothing" : "");
  else
    say("Test: arms_live_keep.txt: written joint values stay in the rig until the next frame, where the game's own are recovered (the way of "
        "builds before round 13, 2026-11-24: the written weapon socket moves the camera); in a hold and with the gun away they are put back as always%s",
        arms_restore ? "; arms_live_restore.txt is here too and is overruled" : "");
  if (arms_live && arms_rest_trace) say("Test: rest_trace.txt: the \"Rest:\" line of the resting weapon pose comes every tick instead of once a second");
  if (grip_off) say("Test: grip_off.txt: the left hand is not put on the weapon's grip; it stays where the body's arm puts it (as before round 23), "
                    "whatever First Person Arms says");
  else say("Grip: the left hand of the drawn arms holds the weapon's grip node (a foregrip's or vertical grip's own node when one is fitted) "
           "whenever the body's left hand is on it, while First Person Arms and Animated arms are on, also while aiming (on the game's own aim "
           "pose, the left arm only); not for a handgun. A file grip_off.txt beside the DLL leaves the hand as the body puts it, to compare");
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
    tfp_arm_drive_set(wanted, tfp_arm_drive_put_back(arms_keep, arms_restore), weight);
    /* Round 23: the left hand on the weapon's grip (src/offhand_grip.h): the option, the test switch, a handgun at the hip */
    tfp_arm_drive_set_grip(!grip_off && tfp_wire_settings_first_person_arms(&wire), tfp_wire_settings_sidearm(&wire));
    tfp_pose_set_sidearm(tfp_wire_settings_sidearm(&wire)); /* round 49e: for the "Reload rest:" line only */
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
    { /* the grip: once a second while it runs, and once whenever it lets go of the grip for another reason */
      static uint64_t next_grip;
      static uint32_t told_state = UINT32_MAX;
      TfpArmDriveGrip g;
      if (now >= next_grip) {
        next_grip = now + 1000;
        tfp_arm_drive_grip_take(&g);
        if (g.frames) say("Grip: %s", tfp_arm_drive_grip_line(&g));
        if (g.frames) say("Grip: gate: %s", tfp_arm_drive_grip_gate_line(&g)); /* round 38b: why it holds or lets go */
        const char *drawn = g.frames ? tfp_arm_drive_grip_drawn_line(&g) : NULL; /* round 36b: the hand as drawn, a line of its own */
        if (drawn) say("Grip: %s", drawn);
        const char *units = g.frames ? tfp_arm_drive_grip_units_line(&g, true) : NULL; /* round 36: whenever the gun's units change */
        if (units) say("Grip: %s", units);
        if (g.frames && g.state != told_state && g.state != TFP_ARM_GRIP_HELD && g.state != TFP_ARM_GRIP_EASING)
          say("Grip: left as the body holds it: %s", tfp_arm_drive_grip_state_name(g.state));
        if (g.frames) told_state = g.state;
      }
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
  if (s.frames != before.frames || s.refused != before.refused || s.idle != before.idle || s.restores != before.restores) {
    say("Arms: +%llu frames driven, %llu joints, %llu pieces refreshed, binds %llu, refused %llu (%s); +%llu frames in another camera, %u joints held",
        (unsigned long long)(s.frames - before.frames), (unsigned long long)(s.joints - before.joints), (unsigned long long)(s.pieces - before.pieces),
        (unsigned long long)s.binds, (unsigned long long)(s.refused - before.refused),
        s.refused != before.refused ? tfp_arm_drive_reason_name(s.reason) : "none", (unsigned long long)(s.idle - before.idle), (unsigned)s.owned);
    before = s;
  }
  /* Round 28: every second, whatever the arm stats did (they were the only trigger up to 0.6.0-feel.59, so seconds with
   * the drive idle were folded into the next line): what the mod costs, then the Perf line. */
  { /* what the mod costs: its tick and the arm drive's part of the draw, per frame, in the second just past */
    uint64_t drawn = 0; double mean = 0, most = 0;
    tfp_render_slot_cost(&drawn, &mean, &most);
    LARGE_INTEGER rate; QueryPerformanceFrequency(&rate);
    const double us = rate.QuadPart ? 1e6 / (double)rate.QuadPart : 0;
    say("Cost: tick %.0f us mean, %.0f us largest over %ld ticks; arms in the draw %.0f us mean, %.0f us largest over %llu frames",
        tick_cost_count ? (double)tick_cost_total * us / (double)tick_cost_count : 0, (double)tick_cost_max * us, (long)tick_cost_count, mean, most,
        (unsigned long long)drawn);
    /* the Perf line: the same ticks in sections, the vignette's entity copy, and what was counted in the second */
    static uint64_t told_counts[TFP_WIN_COUNTS], told_calls, told_logs, told_say;
    static uint32_t told_scans;
    TfpPerfSecond p;
    uint64_t counts[TFP_WIN_COUNTS], calls = 0, logs = 0;
    memset(&p, 0, sizeof p);
    p.ticks = (uint32_t)tick_cost_count;
    for (int i = 0; i < TFP_PERF_SECTIONS; i++) {
      p.mean_us[i] = tick_cost_count ? (double)perf_total[i] * us / (double)tick_cost_count : 0;
      p.most_us[i] = (double)perf_most[i] * us;
      perf_total[i] = perf_most[i] = 0;
    }
    tfp_win_effects_last_scan(&p.scan_entries, &p.scan_us, &p.scan_worst_us);
    const uint32_t scans = tfp_win_effects_scans();
    p.scans = scans - told_scans, told_scans = scans;
    tfp_win_counts(counts);
    eng_counts(&calls, &logs);
    p.host_writes = (uint32_t)(counts[TFP_WIN_COUNT_HOST_WRITE] - told_counts[TFP_WIN_COUNT_HOST_WRITE]);
    p.process_writes = (uint32_t)(counts[TFP_WIN_COUNT_PROCESS_WRITE] - told_counts[TFP_WIN_COUNT_PROCESS_WRITE]);
    p.queries = (uint32_t)(counts[TFP_WIN_COUNT_QUERY] - told_counts[TFP_WIN_COUNT_QUERY]);
    p.engine_calls = (uint32_t)(calls - told_calls);
    p.log_lines = (uint32_t)(counts[TFP_WIN_COUNT_LOG] - told_counts[TFP_WIN_COUNT_LOG] + (logs - told_logs) + (say_lines - told_say));
    memcpy(told_counts, counts, sizeof counts), told_calls = calls, told_logs = logs, told_say = say_lines;
    char line[700];
    tfp_perf_line(line, sizeof line, &p);
    say("%s", line);
    { /* round 32: the frame in laps, over the same ticks */
      double mean[TFP_LAPS], most[TFP_LAPS];
      for (int i = 0; i < TFP_LAPS; i++) {
        mean[i] = tick_cost_count ? (double)lap_total[i] * us / (double)tick_cost_count : 0;
        most[i] = (double)lap_most[i] * us;
        lap_total[i] = lap_most[i] = 0;
      }
      tfp_perf_frame_line(line, sizeof line, mean, most);
      say("%s", line);
    }
    { /* round 35: on_update in parts, and every snapshot call's time, over the same ticks */
      double mean[TFP_UPDS], most[TFP_UPDS];
      for (int i = 0; i < TFP_UPDS; i++) {
        mean[i] = tick_cost_count ? (double)part_total[i] * us / (double)tick_cost_count : 0;
        most[i] = (double)part_most[i] * us;
        part_total[i] = part_most[i] = 0;
      }
      tfp_perf_update_line(line, sizeof line, mean, most, tick_cost_count ? (double)snap_total * us / (double)tick_cost_count : 0,
                           (double)snap_most * us);
      snap_total = snap_most = 0;
      say("%s", line);
    }
    { /* round 32: what the backend did in the second, and the shadow memo (src/backend_ops.h TfpOpsPerf); round 35: the
       * memo live (the default since round 40; memo_off.txt: the shadow) */
      static uint64_t told_reads, told_takes[3], told_tables, told_invokes, told_bodies, told_memo[3][3], told_moved;
      static uint64_t told_served[3], told_checked[3], told_saved;
      static bool told_mismatch, told_off;
      const TfpOpsPerf *o = &wire.ops.perf;
      TfpPerfSnapshots n;
      memset(&n, 0, sizeof n);
      n.reads = perf_delta(o->reads, &told_reads);
      for (int m = 0; m < 3; m++) {
        n.takes[m] = perf_delta(o->takes[m], &told_takes[m]);
        n.calls[m] = perf_delta(o->memo_calls[m], &told_memo[0][m]);
        n.hits[m] = perf_delta(o->memo_hits[m], &told_memo[1][m]);
        n.mismatches[m] = perf_delta(o->memo_mismatches[m], &told_memo[2][m]);
      }
      n.tables = perf_delta(wire.snap_next, &told_tables);
      n.invokes = perf_delta(o->invokes, &told_invokes), n.invoke_bodies = perf_delta(o->invoke_bodies, &told_bodies);
      n.moved = perf_delta(o->memo_moved, &told_moved);
      n.memo = tfp_wire_memo_state(&wire);
      if (n.memo != TFP_PERF_MEMO_SHADOW) {
        for (int m = 0; m < 3; m++)
          n.served[m] = perf_delta(o->memo_served[m], &told_served[m]), n.checked[m] = perf_delta(o->memo_checked[m], &told_checked[m]);
        n.saved_reads = perf_delta(o->memo_saved_reads, &told_saved);
      }
      tfp_perf_snapshots_line(line, sizeof line, &n);
      say("%s", line);
      if (wire.memo_on && !told_off && (o->off.has || wire.memo_off)) { /* round 35: the check that turned it off, once */
        told_off = told_mismatch = true;
        if (o->off.has) tfp_perf_memo_off_line(line, sizeof line, o->off.mode, o->off.moved, o->off.field, o->off.offset, o->off.epoch);
        else
          tfp_perf_memo_off_line(line, sizeof line, wire.memo_off_cause.mode, wire.memo_off_cause.moved, wire.memo_off_cause.field,
                                 wire.memo_off_cause.offset, wire.memo_off_cause.epoch);
        say("%s", line);
      }
      if (o->first.has && !told_mismatch) {
        told_mismatch = true;
        tfp_perf_mismatch_line(line, sizeof line, o->first.mode, o->first.field, o->first.offset, o->first.epoch);
        say("%s", line);
      }
    }
    tick_cost_total = tick_cost_max = 0, tick_cost_count = 0;
  }
}
#endif

/* The entity records, the entity maps and the player fields, read once from the game's code before anything reads
 * them (src/entity_fields.h): HellForge 0.5 moves the records out of the EntityManager, a mod may move the player
 * lists. A cold page waits; an instruction that is not the reviewed one, or values that do not hold together, refuse
 * the session: nothing reads those fields at a guessed place. */
static bool fields_ready(void) {
  char why[200], line[400];
  TfpFieldsResult r = tfp_win_resolve_fields(why, sizeof why);
  if (r == TFP_FIELDS_UNREADABLE) {
    if (!told_fields_waiting) told_fields_waiting = true, say("Waiting: %s", why);
    return false;
  }
  if (r != TFP_FIELDS_OK) {
    char text[320];
    snprintf(text, sizeof text, "the entity and player fields could not be read from the game's code: %s", why);
    refuse(text);
    return false;
  }
  tfp_fields_describe(tfp_win_fields(), line, sizeof line);
  say("Ready: fields read from the game's code: %s", line);
#ifdef TFP_FEEL
  tfp_pose_set_entity_fields(tfp_win_fields());
#endif
  return true;
}
#ifdef TFP_FEEL
#include "seat_log.h"
/* Round 38 (src/seat_log.h): once a second while the lock shows first person from an exosuit's seat or glides out. */
static bool see_through_cockpit_now(void) { return tfp_locked_view_see_through_cockpit(&wire); }
static void seat_note(void) {
  static TfpCameraSeatStats told; static TfpArmDriveStats arms_told;
  TfpCameraSeatStats c; tfp_camera_seat_stats(&c);
  TfpArmDriveStats a; tfp_arm_drive_stats(&a);
  TfpWireSeat w;
  if (c.frames != told.frames && tfp_wire_engine_seat(&wire, &w)) {
    TfpSeatLine l = {c.frames - told.frames, c.refused - told.refused, c.glided - told.glided, c.settled, c.gliding, c.hands,
                     {c.offset[0], c.offset[1], c.offset[2]}, {c.left[0], c.left[1], c.left[2]}, {c.right[0], c.right[1], c.right[2]},
                     w.see_through, w.suit, w.how, w.searches, w.hidden, w.limbs, w.cockpit, a.frames - arms_told.frames, a.idle - arms_told.idle, w.gear};
    char line[700];
    tfp_seat_line(line, sizeof line, &l);
    say("%s", line);
  }
  told = c; arms_told = a;
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
  tfp_camera_set_clear(true); /* round 44: the head camera's eye kept in front of the head (src/camera_clear.h); clear_off.txt below */
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
        if (mcm_file) say("Test: mcm_on.txt is no longer needed and changes nothing: the settings menu is DBF-MCM's whenever it is loaded");
        strcpy(slash + 1, "panel_on.txt");
        FILE *panel_file = fopen(parts_path, "r");
        if (panel_file) fclose(panel_file);
        panel_wanted = panel_file != NULL;
        /* Round 35 (docs/design/specs/2026-11-25-performance.md section 6): the snapshot memo live. On by default since
         * round 40 (section 7, the A/B in the game); memo_off.txt switches it off (the memo then only compares, as in round
         * 32). memo_on.txt is still read and changes nothing. */
        strcpy(slash + 1, "memo_off.txt");
        FILE *memo_off_file = fopen(parts_path, "r");
        if (memo_off_file) fclose(memo_off_file);
        strcpy(slash + 1, "memo_on.txt");
        FILE *memo_on_file = fopen(parts_path, "r");
        if (memo_on_file) fclose(memo_on_file);
        tfp_wire_memo_start(&wire, memo_off_file != NULL);
        {
          char memo_line[512];
          tfp_perf_memo_start_line(memo_line, sizeof memo_line, memo_off_file != NULL, memo_on_file != NULL);
          say("%s", memo_line);
        }
#ifdef TFP_FEEL
        static const char *const arm_files[] = {"arms_live_keep.txt", "arms_live_restore.txt", "arm_bend.txt", "rest_trace.txt", "grip_off.txt"};
        bool *const arm_switches[] = {&arms_keep, &arms_restore, &arms_bend, &arms_rest_trace, &grip_off};
        for (int i = 0; i < 5; i++) {
          strcpy(slash + 1, arm_files[i]);
          FILE *arm_file = fopen(parts_path, "r");
          if (arm_file) fclose(arm_file);
          *arm_switches[i] = arm_file != NULL;
        }
        strcpy(slash + 1, "depth_off.txt");
        FILE *depth_file = fopen(parts_path, "r");
        if (depth_file) fclose(depth_file);
        depth_off = depth_file != NULL;
        strcpy(slash + 1, "depth_steady_off.txt");
        FILE *steady_file = fopen(parts_path, "r");
        if (steady_file) fclose(steady_file);
        depth_steady_off = steady_file != NULL;
        strcpy(slash + 1, "zoom_steady.txt");
        FILE *zoom_file = fopen(parts_path, "r");
        if (zoom_file) fclose(zoom_file);
        zoom_steady = zoom_file != NULL;
        /* Round 19: the helmet veil opens at the tick that sees a leave of the native view, not at the first head-camera
         * frame (src/camera_helper.h tfp_camera_set_veil_second). The "Head:" line says which way it opens. */
        strcpy(slash + 1, "veil_second.txt");
        FILE *veil_file = fopen(parts_path, "r");
        if (veil_file) fclose(veil_file);
        tfp_camera_set_veil_second(veil_file != NULL);
        if (veil_file) say("Test: veil_second.txt: the helmet veil opens at the tick that sees a leave of the native view, so only the frame that draws the body is moved");
        /* Round 44: the head camera's eye kept in front of the head (src/camera_clear.h), unless clear_off.txt. */
        strcpy(slash + 1, "clear_off.txt");
        FILE *clear_file = fopen(parts_path, "r");
        if (clear_file) fclose(clear_file), tfp_camera_set_clear(false);
        if (clear_file) say("Test: clear_off.txt: the head camera's eye is not kept in front of the head (a throw's held eye shows the cape, as before round 44)");
        /* Round 22: the Warp Pack's glow (src/warp_glow.h). Off by default: the "Warp:" line only says what plays. */
        strcpy(slash + 1, "warp_hide.txt");
        FILE *warp_file = fopen(parts_path, "r");
        if (warp_file) fclose(warp_file);
        warp_hide = warp_file != NULL;
        say(warp_hide ? "Test: warp_hide.txt: while the view is TFP's and full ADS is off, the Warp Pack's playing effects of its three stop lists are "
                        "stopped with the game's own stop (game+0x8abb20, no network message), as the game does in full ADS; leaving the view writes "
                        "0xffffffff to the pack's cached mode word so the game plays them again. \"Warp:\" lines say each step"
                      : "Warp: the Warp Pack's glow is left to the game (a file warp_hide.txt beside the DLL stops it while the view is TFP's); "
                        "\"Warp:\" lines say what the game plays");
        /* Round 24: THE SETTLE (src/camera_continuity.h), off by default: replayed, it made soak windows worse and
         * held back the rig's real motion; on only to compare in the game. */
        strcpy(slash + 1, "settle_on.txt");
        FILE *settle_file = fopen(parts_path, "r");
        if (settle_file) fclose(settle_file);
        tfp_camera_set_settle(settle_file != NULL);
        if (settle_file) say("Test: settle_on.txt: the native view coming back on mask 0 after it was left for a new request or firearm holds the "
                             "eye against the root until the rig is steady (3 frames, 350 ms at most), then eases once (THE SETTLE)");
        /* Round 49: the frame the head camera's eye is damped in (src/camera_damp.h tfp_damp_frame_step), off by default:
         * it takes the 0.31 m out of a get-up from lying on the back, and replayed over the soaks (tools/harness/replay.py
         * --damp-frame against --root-frame) it made other frames worse by more than 5 mm; on only to compare in the game. */
        strcpy(slash + 1, "damp_frame_on.txt");
        FILE *damp_frame_file = fopen(parts_path, "r");
        if (damp_frame_file) fclose(damp_frame_file);
        tfp_camera_set_damp_frame(damp_frame_file != NULL);
        if (damp_frame_file) say("Test: damp_frame_on.txt: the head camera's eye is damped in a level frame that turns with the body only as far as "
                                 "the view turns the same way (a body that turns under a still view does not swing the eye)");
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
  tfp_camera_set_action_tuning(TFP_ACTION_CLAMBER_MASK, TFP_ACTION_EYE_IDLE_MS, TFP_ACTION_EYE_RETURN);
  /* The camera lock (docs/design/specs/2026-11-22-locked-view-design.md): first person through every action and camera. */
  tfp_camera_set_lock(lock_wanted, .35f, 8.0f, 300.0f, .7f, 35.0f, tfp_camera_damping_ms(60), false); /* the options' defaults (the Stable camera); lock_note sets the saved ones */
#ifdef TFP_FEEL
  tfp_wire_engine_set_held_body(tfp_camera_lock_off_foot_body); /* head hiding stays on while the lock holds a body that is off foot */
  tfp_wire_engine_set_lock_view(lock_view_now);                 /* and while the game presents the lease's own first-person request (a stim) */
  tfp_wire_engine_set_seat(tfp_camera_lock_seat_body, see_through_cockpit_now); /* round 38: the whole diver hidden in an exosuit, the suit seen through */
  tfp_wire_engine_set_eye_held(tfp_camera_lock_eye_held_body); /* round 44: Show torso waits while a throw's held eye is kept or let go */
#endif
  say(lock_wanted ? "Ready: camera lock: the view stays first person through every action and camera of your own character, death included; "
                    "the camera style is Stable unless Bodycam is switched on in the panel: in Bodycam body movement kicks the view (35 %% up to 8 degrees "
                    "on foot, 70 %% up to 35 when thrown), in Stable only a thrown body does, at half that strength (a file lock_off.txt beside the DLL "
                    "switches the lock off)"
                  : "Test: lock_off.txt: the camera lock is off");
  tfp_camera_set_continuity(false, lock_wanted && trace_wanted); /* the continuity itself: option_smooth, once the settings are read */
  if (lock_wanted && !trace_wanted) say("Test: trace_off.txt: no \"Trace:\" lines after State edges");
  say("Ready: action camera: the head camera also takes a high clamber (mask 0x%x); the eye is held through %u ms without an action "
      "(a stim) and glides back before the weapon returns; the aim button is read from Windows, again as the script update begins",
      TFP_ACTION_CLAMBER_MASK, TFP_ACTION_EYE_IDLE_MS);
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
    tfp_wire_laps_start(&wire, perf_qpc); /* round 32: after every binder's pointers, before on_enable (the Perf frame line) */
    /* Where the two files are saved. A settings.json of 0.6.0-feel.20 to feel.24 is converted here, at once. */
    static const TfpSettingsDisk user_disk = {NULL, write_user_file, copy_user_file, same_user_file};
    tfp_wire_settings_disk(&wire, &user_disk, user_file);
    /* The three live features, now that the settings are read: each setter once, and a line each that says whether the
     * feature is on and whether the option or a file decided. Then what each one's start-up has to say. */
    options.set[TFP_FEATURE_ARMS] = option_arms, options.set[TFP_FEATURE_HOLD] = option_hold, options.set[TFP_FEATURE_SMOOTH] = option_smooth;
    tfp_wire_options_start(&wire, &options);
    arms_start();
    hold_start();
    /* The game's unzoom gate, watched for the log (src/gate_log.h), and the depth switch. */
    tfp_pose_set_gate_reads(1);
    tfp_pose_set_depth_off(depth_off);
    say("Ready: unzoom gate watched: a \"Gate:\" line in a second in which a gate byte changes or the zoom depth is added in some frames and "
        "passed in others, and once at each native entry; the avatar's main row is read for it (two guarded reads a frame); nothing is changed by it");
    if (depth_off) say("Test: depth_off.txt: the mod's zoom depth is forced to nothing on every frame (neither the rig nor its root is shifted for the optic)");
    /* The rig's depth held steady against that gate (src/depth_steady.h), or only counted. */
    tfp_pose_set_depth_steady(depth_steady_off ? 2 : 1);
    if (depth_steady_off) say("Test: depth_steady_off.txt: the rig's depth is not held steady; it follows the game's lens target as before, and a \"Steady:\" line "
                              "says what would have been held");
    else say("Ready: rig depth held steady: in the hip view the first-person rig and its root rest at the selected optic's distance instead of following "
             "the lens target the unzoom gate switches; a \"Steady:\" line in a second in which something was moved (depth_steady_off.txt switches it off)");
    /* Round 20c: the zoom-shake corrections in full aim, only with zoom_steady.txt */
    tfp_aim_bind_switches(zoom_steady); /* src/aim_bind.h */
    if (zoom_steady) say("Test: zoom_steady.txt: in full aim the rig's depth is held at full strength, and with Aim field of view the game's zoom the "
                         "field of view is held at the optic's own zoom while the unzoom gate flips (round 20 zoom-shake corrections)");
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
  char toggle_name[16];
  const int toggle_now = tfp_wire_menu_toggle_key(&wire);
  tfp_wire_menu_key_name(toggle_now, toggle_name);
  say("Keys: %s toggles True First Person%s", toggle_now ? toggle_name : "nothing (the menu's toggle key is not bound)",
      tfp_wire_menu_toggle_moved(&wire) ? " (the default; F11, the old default, now opens HellForge's Armor LUT editor, so a saved F11 "
                                          "reads as U; another key can be chosen in the menu)" : "");
  say("Ready. The toggle key switches True First Person (view, weapon and arm placement, motion; head hiding, sleeves, hand grips and the effects once "
      "their checks pass: see the Ready / Check / Refused lines). F10 opens the settings menu: DBF-MCM's page when DBF-MCM is loaded, "
      "else the mod's own panel through NEST (Goose Overlay Host; also in the host's panel list, Page Down); a file panel_on.txt beside "
      "the DLL asks for the panel. Settings are saved when they change. Page Up (the menu's editor key) opens the placement "
      "editor; F8 is HellForge's own editor. Not in this build: the eye-camera editor, Photo Mode camera sharing.");
#else
  say("Ready. U toggles the view (F11 is HellForge's Armor LUT editor). Not in this build: arms and weapon placement, sleeves, hand grips, motion, head and body hiding, "
      "depth of field, vignette, aiming markers, movement policy, the F8 editor, the menu, Photo Mode camera sharing.");
#endif
}

#ifdef TFP_FEEL
static unsigned menu_unavailable(void) {
  /* Round 23: First Person Arms is the left hand on the grip, done by the arm drive: available while the drive's render hook
   * contract holds, whatever the retail sleeves and hand grips (still held back) say. */
  bool arms = !arms_refusal[0] || tfp_wire_engine_state(&wire, TFP_WE_SLEEVES) != TFP_WE_REFUSED || tfp_wire_engine_state(&wire, TFP_WE_GRIP) != TFP_WE_REFUSED;
  return (arms ? 0 : TFP_MENU_NO_ARMS) | (tfp_mod_editor_available() ? 0 : TFP_MENU_NO_EDITOR);
}
/* A live replace asked for the mod's registrations with other mods to be taken out; this is the game
 * thread, where DBF-MCM allows it. Nothing is released while the mod still holds something in the game. */
static volatile LONG release_state; /* 0 not answered, 1 released, 2 refused (release_why) */
static const char *volatile release_why;
static void release_on_game_thread(void) {
  const char *why = stage == RUNNING ? tfp_wire_feel_held(&wire) : NULL;
  if (!why) tfp_wire_warp_release(&wire, &warp, warp_say); /* round 22: a restore still owed (the view is off by now, so normally none) */
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
  /* (frames the view is shown, not frames the camera lock held: the two are separate toggles in the panel) */
  const uint64_t now = GetTickCount64(), held = tfp_camera_view_frames();
  if (held != frames) { frames = held; shown_at = now; }
  const bool view = shown_at && now - shown_at < 250 && lock_wanted, shown = view && tfp_locked_view_fov_lock(&wire);
  /* "Aim field of view" (round 20): a chosen value holds in full aim with the lock off too, while the view is shown */
  const float aim = tfp_locked_view_aim_field(&wire);
  tfp_fov_set_lock_aim(shown ? (float)(tfp_locked_view_fov_degrees(&wire)*3.14159265358979/180) : 0, view || !(aim > 0) ? aim : TFP_FOV_AIM_ZOOM, (double)now);
}
static void lock_note(void) { /* once a second while the view is on: frames by camera, frames missed, the largest kick */
  static uint64_t next, told_frames, told_foreign, told_missed;
  const uint64_t now = GetTickCount64();
  if (now < next) return;
  next = now + 1000;
  /* The Locked view tab's options (src/wire_settings.h); lock_off.txt beside the DLL overrides the toggle. */
  const bool bodycam = tfp_locked_view_bodycam(&wire); /* the camera style (src/camera_damp.h): live, like the sliders */
  tfp_camera_set_lock(lock_wanted && tfp_locked_view_camera_lock(&wire), (float)(tfp_locked_view_kick_strength(&wire)/100),
                      (float)tfp_locked_view_kick_max_degrees(&wire), 300.0f, (float)(tfp_locked_view_thrown_kick_strength(&wire)/100), 35.0f,
                      tfp_camera_damping_ms(tfp_locked_view_camera_damping(&wire)), bodycam);
  static int told_style = -1;
  if (lock_wanted && told_style != (int)bodycam) {
    told_style = bodycam;
    say(bodycam ? "Camera style: Bodycam (the view moves with the head: the kick sliders and camera damping apply)"
                : "Camera style: Stable (no view kick on foot; a thrown body sways the view at half the thrown strength; the head camera follows the head slowly)");
  }
#ifdef TFP_FEEL
  seat_note();
#endif
  TfpCameraLockStats s; tfp_camera_lock_stats(&s);
  if (s.frames == told_frames && s.missed == told_missed) return;
  /* The kind and error are the last missed frame's: 0 in a second without one. tools/harness/matrix.py reads this line. */
  const bool missed = s.missed != told_missed;
  say("Lock: +%llu frames held (+%llu in another camera, kind now %u), +%llu missed (kind %u, helper error %u), largest kick %.1f degrees",
      (unsigned long long)(s.frames - told_frames), (unsigned long long)(s.foreign_frames - told_foreign), s.last_kind,
      (unsigned long long)(s.missed - told_missed), missed ? s.missed_kind : 0, missed ? s.missed_error : 0, (double)s.peak_kick_degrees);
  if (s.last_kind != 6 && (s.off_foot || s.ragdoll) && s.frames != told_frames)
    say("Body: off foot %u, ragdoll flag %u, head %.2f m from its root (the last head-camera frame the lock held)", s.off_foot, s.ragdoll, (double)s.head_distance);
  if (missed && s.missed_why == TFP_CAMERA_WHY_SPACE) say("Missed: the camera's local and world position were %.3f m apart (0: not read)", (double)s.missed_apart);
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
    static uint64_t told_jumps, told_edges, told_reseats, told_cuts;
    TfpCameraContinuityStats c; tfp_camera_continuity_stats(&c);
    if (c.jumps != told_jumps || c.edges != told_edges || c.reseats != told_reseats || c.cuts != told_cuts)
      say("Smooth: +%llu changes of camera and +%llu rig moves taken up, +%llu moves of the body root alone set aside, +%llu head-camera pose cuts taken up, "
          "largest correction %.3f m (%llu restarts so far)",
          (unsigned long long)(c.edges - told_edges), (unsigned long long)(c.jumps - told_jumps), (unsigned long long)(c.reseats - told_reseats),
          (unsigned long long)(c.cuts - told_cuts), (double)c.largest_m, (unsigned long long)c.restarts);
    told_jumps = c.jumps; told_edges = c.edges; told_reseats = c.reseats; told_cuts = c.cuts;
  }
  /* The eye going back and forth (src/camera_alternation.h): counted, nothing is done. A second without it writes nothing. */
  static TfpCameraAlternationStats told_alternation; TfpCameraAlternationStats a; tfp_camera_alternation_stats(&a);
  if (a.frames != told_alternation.frames)
    say("Alternation: %llu frames of the eye going back and forth (would be held; nothing is done), largest move %.3f m; jumps were looked for in "
        "%llu of them; the fire flag was set in %llu (flag byte 12 bit 0x40, not proven on this build); kind %u",
        (unsigned long long)(a.frames - told_alternation.frames), (double)a.largest_m, (unsigned long long)(a.looked - told_alternation.looked),
        (unsigned long long)(a.fire - told_alternation.fire), a.kind);
  told_alternation = a;
}
#ifdef TFP_FEEL
/* Round 20c: the "Aim:" line (src/aim_log.h), fed by steady_note and ads_seat_tick. */
static TfpAimLog aim_log;
static void gate_note(void) { /* once a second: what the pose helper counted of the game's unzoom gate, where it is worth a line (src/gate_log.h) */
  static uint64_t next, told_generation;
  const uint64_t now = GetTickCount64();
  if (now < next) return;
  next = now + 1000;
  TfpGateSecond g;
  if (!tfp_pose_gate_take(&g) || !g.frames) return;
  const bool entry = g.generation != told_generation; /* the first frames of a new lease: written whatever they hold */
  told_generation = g.generation;
  if (!entry && !tfp_gate_worth(&g)) return;
  char line[640];
  tfp_gate_line(&g, entry, line, sizeof line);
  say("%s", line);
}
static void steady_note(void) { /* once a second: the rig's depth held steady (src/depth_steady.h), where something was moved or passed for its values */
  static uint64_t next;
  const uint64_t now = GetTickCount64();
  if (now < next) return;
  next = now + 1000;
  TfpSteadySecond held;
  char line[640];
  if (!tfp_pose_steady_take(&held)) return;
  tfp_aim_log_add_rig(&aim_log, &held); /* round 20c: the rig's depth in full aim, for the "Aim:" line */
  if (tfp_steady_stop_line(&held, line, sizeof line)) say("%s", line); /* the root did not follow: said once, in the second it stopped */
  /* round 39: once a reload, in the second it ended (src/reload_hold.h) */
  for (uint32_t i = 0; i < held.reload.count && i < TFP_RELOAD_LOGGED; ++i) { tfp_reload_line(&held.reload.ends[i], line, sizeof line); say("%s", line); }
  if (held.reload.count > TFP_RELOAD_LOGGED) say("Reload: %u more reloads ended in the same second", held.reload.count - TFP_RELOAD_LOGGED);
  /* round 49e: the edges of each, looked at (the frame before it, its start and its end: src/reload_hold.h) */
  for (uint32_t i = 0; i < held.reload.edge_count && i < TFP_RELOAD_EDGES_LOGGED; ++i)
    for (uint32_t k = 0; tfp_reload_edges_line(&held.reload.edges[i], k, line, sizeof line); ++k) say("%s", line);
  if (!tfp_steady_worth(&held)) return;
  tfp_steady_line(&held, line, sizeof line);
  say("%s", line);
}
/* Round 20: the sight distance in aim (src/ads_seat.h) and the "Aim:" line (src/aim_log.h), every tick. The near
 * helper saw the game's sight (its near clip); the pose helper is handed it with the wanted distance and pulls the gun
 * in full aim. How much closer the gun was drawn the near helper reads from the pose helper itself, in its own calls
 * (src/wire_win_pose.c), not through this tick, which would leave the near clip a tick behind the gun. */
static void ads_seat_tick(void) {
  float sight = 0, native = 0, applied = 0;
  const bool near_on = tfp_near_ads_probe(&sight, &native, &applied);
  const TfpEditor *editor = tfp_wire_editor_of(&wire);
  tfp_aim_bind_tick(near_on, sight, tfp_locked_view_ads_sight_cm(&wire), tfp_editor_motion_editing_ads(editor)); /* src/aim_bind.h */
  TfpPoseAdsProbe ads;
  const bool pose_on = tfp_pose_ads_probe(&ads) && ads.armed;
  TfpFovLockStats f; tfp_fov_lock_stats(&f);
  TfpAimSample sample = {
    .full_ads = pose_on && ads.full_ads, .weapon = tfp_wire_settings_weapon_name(&wire),
    .game_fov = f.game_radians > 0 ? f.game_radians : NAN, .shown_fov = f.shown_radians > 0 ? f.shown_radians : NAN,
    .aim_field = tfp_locked_view_aim_field(&wire), .sight = near_on && sight > 0 ? sight : NAN,
    .wanted = (float)(tfp_locked_view_ads_sight_cm(&wire)/100), .pull = ads.pull, .closer = ads.closer, .ads_forward = ads.ads_forward,
    .near_clip = near_on ? applied : NAN, .depth = ads.depth, .editor = editor && tfp_editor_is_active(editor),
    .editor_action = editor ? tfp_editor_preview_action(editor) : NULL, .now_ms = (double)GetTickCount64()};
  TfpFovAimSecond frames; tfp_fov_aim_take(&frames); tfp_aim_log_add_fov(&aim_log, &frames); /* round 20c: the frames since the last tick */
  char line[TFP_AIM_LINE];
  if (tfp_aim_log_step(&aim_log, &sample, line, sizeof line)) say("%s", line);
}
#endif
static void fov_note(void) { /* once a second, on its own: the field-of-view lock also runs with the camera lock off, when lock_note has nothing to say */
  static uint64_t next, told_fov;
  const uint64_t now = GetTickCount64();
  if (now < next) return;
  next = now + 1000;
  TfpFovLockStats f; tfp_fov_lock_stats(&f);
  if (f.applied != told_fov) say("Fov: +%llu frames locked; the game asked for %.1f degrees, shown %.1f", (unsigned long long)(f.applied - told_fov),
                                (double)f.game_radians*57.29578, (double)f.shown_radians*57.29578);
  told_fov = f.applied;
}
/* Round 36b: the toggle key (a letter since round 36) is not taken while text is typed (src/key_guard.h). The chat
 * flag's place is read from the game's code once its pages read (tried every 5 s); until then one snapshot of the
 * keyboard is taken each tick for the typing fallback. The flag and the host are only asked on a press. Round 36c: the
 * whole toggle path (level, edge, sample, verdict) is tfp_key_guard_toggle, so the tick's press only comes out of it. */
#include "key_guard.h"
#include "goose_overlay.h"
static TfpKeyGuard key_guard;
static TfpChatLayout chat_layout;
static uint64_t chat_next_try;
static char chat_why[160];
static bool guard_read(void *u, uint64_t at, void *out, size_t size) { (void)u; return tfp_win_read(at, out, size); }
static bool guard_key(void *u, int vk) { (void)u; return tfp_win_key_down(vk); }
static bool guard_keyboard(void *u, uint8_t keys[256]) { (void)u; return tfp_win_keyboard(keys); }
static int guard_chat(void *u) { (void)u; return tfp_chat_open(guard_read, NULL, &chat_layout); }
static bool guard_overlay(void *u) { /* a host window open or look capture held: the host has the keyboard */
  (void)u;
  HMODULE host = GetModuleHandleA(OV_HOST_DLL);
  OvApiFn get = host ? (OvApiFn)(void *)GetProcAddress(host, "ov_api") : NULL;
  const OvApi *ov = get ? get(3) : NULL; /* every host since API 3 answers; input_captured from API 8 (host 0.6.0) */
  if (!ov) return false;
  if (ov->version >= 8 && ov->size >= offsetof(OvApi, input_captured) + sizeof ov->input_captured && ov->input_captured)
    return ov->input_captured() != 0;
  return ov->look_capture && ov->look_capture() != 0;
}
static bool guard_panel(void *u) {
  (void)u;
#ifdef TFP_FEEL
  return tfp_mod_panel_is_open() || tfp_wire_editor_active(&wire);
#else
  return false;
#endif
}
static void guard_say(void *u, const char *line) { (void)u; say("Keys: %s", line); }
static void guard_say_line(void *u, const char *line) { (void)u; say("%s", line); }
static bool guard_edge(void *u, int key, bool down) {
  (void)u;
#ifdef TFP_FEEL
  return tfp_wire_menu_key_edge(&toggle_edge, key, down);
#else
  (void)key;
  const bool pressed = down && !key_held;
  key_held = down;
  return pressed;
#endif
}
static const TfpKeyGuardIo guard_io = {NULL, guard_key, guard_edge, guard_keyboard, guard_chat, guard_overlay, guard_panel, guard_say};
/* Round 41: F11 (the old toggle key, HellForge's Armor LUT editor now) pressed in the game gets one notice naming the
 * toggle key (src/f11_hint.h): the host's HUD, the chat/overlay/panel answers of the guard above, F11 only read. */
#include "f11_hint.h"
static TfpF11Hint f11_hint;
static TfpF11HintHud f11_hud;
static bool f11_show(void *u, const char *notice, char *why, size_t why_size) {
  (void)u;
  HMODULE host = GetModuleHandleA(OV_HOST_DLL);
  OvApiFn get = host ? (OvApiFn)(void *)GetProcAddress(host, "ov_api") : NULL;
  return tfp_f11_hint_hud_show(&f11_hud, get ? get(3) : NULL, "goose.true-first-person/f11-notice", notice, why, why_size);
}
static void f11_hint_tick(int toggle_key, bool focused) {
  static const TfpF11HintIo io = {NULL, guard_key, guard_chat, guard_overlay, guard_panel, guard_say_line, f11_show};
  char name[16] = "";
#ifdef TFP_FEEL
  if (toggle_key) tfp_wire_menu_key_name(toggle_key, name);
#else
  if (toggle_key >= 'A' && toggle_key <= 'Z') snprintf(name, sizeof name, "%c", toggle_key);
#endif
  tfp_f11_hint_tick(&f11_hint, &io, GetTickCount64(), toggle_key, name, stage == RUNNING, focused);
}
#ifdef TFP_FEEL
/* Round 45: the cockpit frame (src/cockpit_frame.h). With See through the exosuit cockpit and Cockpit frame on, and
 * first person shown from an exosuit's seat (the camera lock's seat body and its seated flag, not gliding in or out;
 * the suit found and seen through, src/wire_engine.h TfpWireSeat), the Goose Overlay Host draws a static cockpit frame
 * as a HUD panel under an id of this load's own. The seat has no event: its body is an atomic the camera helper
 * publishes, read once a tick; the rest is read, and the host asked, only while seated or on leaving. */
#include "cockpit_frame.h"
static TfpCockpitFrame cockpit_frame;
static TfpCockpitHud cockpit_hud;
static char cockpit_panel_id[96];
static void cockpit_say(void *u, const char *line) { (void)u; say("%s", line); }
static const OvApi *cockpit_host(void) { /* the host as it is now (on the seat's edges only) */
  HMODULE host = GetModuleHandleA(OV_HOST_DLL);
  OvApiFn get = host ? (OvApiFn)(void *)GetProcAddress(host, "ov_api") : NULL;
  return get ? get(2) : NULL;
}
static bool cockpit_show(void *u, float opacity, char *why, size_t why_size) {
  (void)u;
  if (!cockpit_panel_id[0]) tfp_cockpit_panel_id(cockpit_panel_id, sizeof cockpit_panel_id, (unsigned long)GetCurrentProcessId(), (unsigned long long)GetTickCount64());
  return tfp_cockpit_hud_show(&cockpit_hud, cockpit_host(), cockpit_panel_id, opacity, why, why_size);
}
static void cockpit_hide(void *u) { (void)u; tfp_cockpit_hud_hide(&cockpit_hud, cockpit_host()); }
static void cockpit_opacity(void *u, float opacity) { (void)u; tfp_cockpit_hud_opacity(&cockpit_hud, opacity); }
static bool cockpit_drawn(void *u, char *description, size_t size) { (void)u; return tfp_cockpit_hud_drawn(&cockpit_hud, description, size); }
static void cockpit_frame_tick(void) {
  static const TfpCockpitFrameIo io = {NULL, cockpit_say, cockpit_show, cockpit_hide, cockpit_opacity, cockpit_drawn};
  TfpCockpitFrameIn in = {false, false, false, false, 0};
  const uint32_t body = stage == RUNNING ? tfp_camera_lock_seat_body() : 0;
  if (body) {
    TfpCameraSeatStats c; tfp_camera_seat_stats(&c);
    in.seated = c.seated && !c.gliding;
  }
  if (!in.seated && !cockpit_frame.seat) return; /* on foot: nothing more */
  if (in.seated) {
    TfpWireSeat seat;
    in.see_through = tfp_locked_view_see_through_cockpit(&wire);
    in.option = tfp_locked_view_cockpit_frame(&wire);
    in.suit = tfp_wire_engine_seat(&wire, &seat) && seat.suit && seat.body == body;
    in.opacity = (float)(tfp_locked_view_cockpit_frame_opacity(&wire) / 100);
  }
  tfp_cockpit_frame_tick(&cockpit_frame, &io, GetTickCount64(), &in);
}
#endif
/* end of round 45's cockpit frame */
static void guard_resolve(void) {
  const uint64_t now = GetTickCount64();
  if (chat_layout.resolved || now < chat_next_try || !tfp_win_game()) return;
  char why[160];
  chat_next_try = now + 5000;
  if (tfp_chat_resolve(guard_read, NULL, tfp_win_game(), FP_GAME_SIZE, &chat_layout, why, sizeof why))
    say("Keys: the toggle key waits while the game's chat is open (its flag at UI root game+0x%llx + 0x%x, from two of the game's "
        "own chat-gated handlers)", (unsigned long long)(chat_layout.root - tfp_win_game()), (unsigned)chat_layout.flag);
  else if (strcmp(why, chat_why)) {
    snprintf(chat_why, sizeof chat_why, "%s", why);
    say("Keys: the game's chat state is not readable yet (%s); until it is, a toggle press right after other typing is ignored", why);
  }
}
static void tick_body(void) {
#ifdef TFP_FEEL
  int64_t mark = perf_qpc(); /* round 28: the sections of the Perf line */
  if (stage == RUNNING) { gate_note(); steady_note(); ads_seat_tick(); }
  cockpit_frame_tick(); /* round 45: the cockpit frame over the see-through seat; hidden when not RUNNING */
#endif
  if (stage == RUNNING) { quit_guard(); fov_lock_tick(); lock_note(); fov_note(); }   /* not earlier: the game module and the guarded reader are set up in WAIT_MODULES */
#ifdef TFP_FEEL
  perf_lap(TFP_PERF_PRE, &mark);
#endif
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
  /* The toggle key is the menu's (default U, a stored F11 reads as U: src/wire_menu.h; 0: unbound). Polled as before: the SDK
   * has no key event. */
  const int toggle_key = stage == RUNNING ? tfp_wire_menu_toggle_key(&wire) : TFP_TOGGLE_KEY;
#else
  const int toggle_key = TFP_TOGGLE_KEY;
#endif
  const bool focused = tfp_win_focused();
  if (toggle_key && focused) guard_resolve();
  /* round 36b/36c: the press, refused while text is typed (src/key_guard.h tfp_key_guard_toggle) */
  const bool pressed = tfp_key_guard_toggle(&key_guard, &guard_io, GetTickCount64(), toggle_key, focused, !chat_layout.resolved);
  f11_hint_tick(toggle_key, focused); /* round 41: F11 pressed in the game, one notice naming the toggle key */
  if (stage == WAIT_PINS) {
    uint64_t now = GetTickCount64();
    if (pressed) say("Refused: the toggle key before the game's code pages could be read; try again once a mission or the ship is loaded");
    if (now < next_pin_check) return;
    next_pin_check = now + 500;
    int pins = check_pins(true);
    if (pins < 0 && !told_waiting) {
      told_waiting = true;
      say("Waiting: some pinned code pages are not readable yet (they are once a player exists)");
    }
    if (pins <= 0) return;
    if (!fields_ready()) return;
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
  mark = perf_qpc();
  /* The editor's key (the menu's; default Page Up), on its edge. The panel is registered before the menu, so
   * the menu knows whether to offer the editor. */
  int editor_key = tfp_wire_menu_editor_key(&wire, TFP_EDITOR_KEY);
  tfp_mod_editor_tick(hf, owner, editor_key);
  bool editor_down = editor_key && tfp_win_focused() && tfp_win_key_down(editor_key);
  if (tfp_wire_menu_key_edge(&editor_edge, editor_key, editor_down)) {
    const char *refused = tfp_wire_editor_toggle(&wire);
    if (refused) say("Placement editor: %s", refused);
  }
  if (tfp_mod_editor_settled()) tfp_mod_panel_tick(hf, owner, &wire, panel_wanted, menu_unavailable(), tfp_wire_editor_toggle);
  perf_lap(TFP_PERF_MENUS, &mark);
  tfp_wire_frame(&wire, extra);
  perf_lap(TFP_PERF_FRAME, &mark);
  perf_effects(TFP_PERF_FRAME);
  entry_note();
  head_note();   /* before the trace: a "Head:" line then stands above the rows of the frames it is about */
  warp_note();   /* round 22: the Warp Pack's glow */
  orphan_note(); /* before the trace: its edges open a trace window too */
  trace_note();
  perf_lap(TFP_PERF_NOTES, &mark);
  tfp_wire_panel_frame(&wire);         /* what the user did in the mod's own panel since the last tick */
  tfp_wire_menu_frame(&wire);          /* the menu's queued buttons and toggles, and the two status rows */
  tfp_wire_options_tick(&wire, &options); /* Animated arms, Weapon put-away, Smooth eye: a changed answer reaches its setter (not during a hold) */
  tfp_wire_settings_save_frame(&wire); /* writes only when the store changed since the last write */
  perf_lap(TFP_PERF_MENUS, &mark);
  eng_temp_end();
  perf_lap(TFP_PERF_FRAME, &mark); /* the frame's engine temporaries, released */
  arms_tick();
  perf_lap(TFP_PERF_ARMS, &mark);
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
#ifdef TFP_FEEL
    perf_close(spent);
#endif
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
 * the tick's own stop paths (toggled off) bring about; then the dispatch slots are put back as at the
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
  tfp_cockpit_hud_retire(&cockpit_hud, cockpit_host()); /* round 45: the cockpit frame's HUD panel likewise (closed already: no seat is held) */
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
