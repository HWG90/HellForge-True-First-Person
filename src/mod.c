/* HellForge entry point. Production sources are C11; host owns all UI/input
 * surfaces and the game owns every camera/animation constructor. */
#define WIN32_LEAN_AND_MEAN
#include "hellforge_mod.h"
#include "profile.h"
#include "ui.h"
#include "sdk_inventory.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
static const HfApi *hf;
static HfMod *owner;
static const OvApi *ov;
static FpRuntime runtime;
static FpUi ui;
static wchar_t settings_path[1200];
static unsigned settings_revision, draft_revision;
static uint64_t next_engine, next_save;
static bool loaded, keys_ready, key_toggle, key_edit, pending_editor,
    save_pending;
static bool sdk_read(void *unused, uintptr_t at, void *out, size_t size) {
  (void)unused;
  return hf->read(at, out, size) == 0;
}
static bool focused(void) {
  DWORD process = 0;
  HWND window = GetForegroundWindow();
  return window && GetWindowThreadProcessId(window, &process) &&
         process == GetCurrentProcessId();
}
static void editor(bool open) {
  bool accepted = fp_runtime_editor(&runtime, open);
  if (open && !accepted) {
    pending_editor = true;
    return;
  }
  pending_editor = false;
  AcquireSRWLockExclusive(&ui.lock);
  ui.editing = runtime.editor;
  ui.action = FP_IDLE;
  ui.weapon = runtime.editor_weapon;
  ui.draft = runtime.draft;
  ui.draft_revision++;
  ReleaseSRWLockExclusive(&ui.lock);
  fp_ui_editor_open(&ui, open && accepted);
}
static void tick(HfMod *mod, float dt, void *unused) {
  (void)mod;
  (void)unused;
  if (!loaded)
    return;
  uint64_t now = GetTickCount64();
  if (now >= next_engine) {
    next_engine = now + 500;
    if (!eng_ready())
      eng_init(hf, owner);
  }
  bool focus = focused(), captured = ov && ov->input_captured(),
       own_editor = ov && ui.editor_panel && ov->is_open(ui.editor_panel);
  bool foreign = ov && ov->look_capture();
  unsigned commands = 0;
  bool binding_capture = false;
  int toggle_key = VK_F11, editor_key = VK_F8;
  AcquireSRWLockExclusive(&ui.lock);
  if (settings_revision != ui.revision) {
    runtime.settings = ui.settings;
    settings_revision = ui.revision;
    if (ui.config_dirty) {
      save_pending = true;
      ui.config_dirty = false;
      if (!next_save)
        next_save = now + 500;
    }
  }
  commands = ui.commands;
  ui.commands = 0;
  binding_capture = ui.binding != 0;
  toggle_key = ui.settings.toggle_key;
  editor_key = ui.settings.editor_key;
  if (runtime.editor && ui.draft_revision != draft_revision) {
    fp_runtime_draft(&runtime, (FpAction)ui.action, &ui.draft);
    draft_revision = ui.draft_revision;
  }
  ReleaseSRWLockExclusive(&ui.lock);
  bool toggle_down = toggle_key && (GetAsyncKeyState(toggle_key) & 0x8000),
       edit_down = editor_key && (GetAsyncKeyState(editor_key) & 0x8000);
  bool eligible =
      focus && !foreign && !binding_capture && (!captured || own_editor);
  if (eligible && keys_ready) {
    if (toggle_down && !key_toggle)
      commands |= FP_CMD_TOGGLE;
    else if (edit_down && !key_edit)
      commands |= FP_CMD_EDIT;
  }
  keys_ready = eligible;
  key_toggle = toggle_down;
  key_edit = edit_down;
  if (commands & FP_CMD_TOGGLE) {
    fp_runtime_enable(&runtime, !runtime.enabled);
    if (!runtime.enabled)
      editor(false);
  }
  if (commands & FP_CMD_EDIT) {
    if (runtime.editor || pending_editor)
      editor(false);
    else {
      if (!runtime.enabled)
        fp_runtime_enable(&runtime, true);
      editor(true);
    }
  }
  if (commands & FP_CMD_CLOSE_EDITOR)
    editor(false);
  if (commands & FP_CMD_FREE_AIM)
    fp_runtime_free_aim(&runtime);
  if (commands & FP_CMD_COMPARE)
    fp_runtime_compare(&runtime, now);
  own_editor = ov && ui.editor_panel && ov->is_open(ui.editor_panel);
  if (runtime.editor && !own_editor)
    editor(false);
  bool sdk_ready = eng_ready(),
       look_allowed = focus && !captured && !foreign && sdk_ready;
  if (look_allowed) {
    int focused_mouse = 1, shown_cursor = 0;
    if (eng_has("Window.mouse_focus") &&
        (!eng_call("Window.mouse_focus", ">b", &focused_mouse) ||
         !focused_mouse))
      look_allowed = false;
    if (eng_has("Window.show_cursor") &&
        (!eng_call("Window.show_cursor", ">b", &shown_cursor) || shown_cursor))
      look_allowed = false;
  }
  if(sdk_ready&&fp_sdk_inventory_pending()){FpSnapshot observed;if(fp_snapshot(&runtime.memory,false,&observed,NULL,0)&&observed.local_request)fp_sdk_inventory(hf,owner,&runtime.memory);}
  eng_temp_begin();
  if (runtime.enabled && !runtime.camera.token && !runtime.native.held &&
      !sdk_ready)
    snprintf(runtime.status, sizeof runtime.status,
             "Waiting for the game's native SDK state");
  else
    fp_runtime_tick(&runtime, dt, now, foreign, look_allowed,
                    (GetAsyncKeyState('S') & 0x8000) != 0);
  eng_temp_end();
  fp_sdk_observer(hf,owner,&runtime.memory,runtime.enabled,now);
  if (pending_editor && runtime.pose.token)
    editor(true);
  if (!runtime.editor && own_editor)
    fp_ui_editor_open(&ui, false);
  AcquireSRWLockExclusive(&ui.lock);
  ui.enabled = runtime.enabled;
  ui.editing = runtime.editor;
  ui.can_edit = runtime.enabled && runtime.pose.token && runtime.weapon_key;
  ui.weapon = runtime.weapon_key;
  snprintf(ui.status, sizeof ui.status, "%s", runtime.status);
  snprintf(ui.effects, sizeof ui.effects, "%s", runtime.effects.status);
  snprintf(ui.models, sizeof ui.models, "%s", runtime.models.status);
  snprintf(ui.issue, sizeof ui.issue, "%s", runtime.issue);
  ReleaseSRWLockExclusive(&ui.lock);
  if (save_pending && now >= next_save) {
    if (fp_settings_save(&runtime.settings, settings_path)) {
      save_pending = false;
      next_save = 0;
    } else {
      next_save = now + 2000;
      hf->log(owner, "Settings save is pending");
    }
  }
  static char previous[256];
  if (strcmp(previous, runtime.status)) {
    hf->log(owner, "%s", runtime.status);
    snprintf(previous, sizeof previous, "%s", runtime.status);
  }
}
static void session_end(HfMod *mod, HfEvent event, void *unused) {
  (void)mod;
  (void)event;
  (void)unused;
  if (save_pending)
    fp_settings_save(&runtime.settings, settings_path);
}
static void game_loaded(HfMod *mod, HfEvent event, void *unused) {
  (void)event;
  (void)unused;
  size_t size = 0;
  runtime.memory.game = hf->module_base("game.dll", &size);
  runtime.memory.exe = hf->module_base("helldivers2.exe", &size);
  if (!fp_image(&runtime.memory, runtime.memory.game, FP_GAME_STAMP,
                FP_GAME_SIZE) ||
      !fp_image(&runtime.memory, runtime.memory.exe, FP_EXE_STAMP,
                FP_EXE_SIZE)) {
    hf->log(mod, "Refused: target image identity differs");
    return;
  }
  ov = ov_connect();
  if (!ov) {
    hf->log(mod, "Goose Overlay Host API8 is required for the menu and "
                 "placement editor");
    return;
  }
  FpSettings saved = runtime.settings;
  if (!fp_ui_init(&ui, ov, &saved)) {
    hf->log(mod, "First Person panels could not register");
    return;
  }
  eng_init(hf, mod);
  loaded = true;
  if (!ov_on_game_thread(hf, mod, tick, NULL)) {
    loaded = false;
    hf->log(mod, "Game-thread callback unavailable");
    return;
  }
  hf->log(mod,
          "True First Person C runtime ready. F11 toggles the view; F8 opens "
          "arms/weapon placement. No Fird or inline optic-selection hook.");
}
__declspec(dllexport) int hellforge_mod_init(const HfApi *api, HfMod *mod) {
  if (!api || api->version < 16 ||
      api->size <
          offsetof(HfApi, on_game_thread) + sizeof api->on_game_thread ||
      strcmp(api->build_id(), "25480438"))
    return 1;
  hf = api;
  owner = mod;
  FpSettings settings;
  fp_settings_defaults(&settings);
  if (fp_settings_path(settings_path,
                       sizeof settings_path / sizeof *settings_path) &&
      !fp_settings_load(&settings, settings_path))
    hf->log(
        mod,
        "Settings file was invalid; valid values and defaults were retained");
  FpMemory memory = {sdk_read, NULL, 0, 0, NULL};
  fp_runtime_init(&runtime, &memory, &settings);
  hf->on(mod, HF_EVENT_SESSION_END, session_end, NULL);
  return hf->on(mod, HF_EVENT_GAME_LOADED, game_loaded, NULL);
}
