/* See mod_editor.h. Two threads meet here: the game thread (the editor's update, through the platform
 * functions) and the overlay host's render thread (the draw callback). They share `shared` under one lock:
 * the render thread leaves the mouse, the keys and the frame size there and takes the list of items to draw. */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "mod_editor.h"
#include "mod_menu.h"
#include "goose_overlay.h"

static const OvApi *ov;
static OvPanel *panel;
static const TfpFxPlatform *window_platform;
static volatile LONG retired;
static int toggle_key;
static ULONGLONG first_try, next_try;
static bool gave_up;

static SRWLOCK lock = SRWLOCK_INIT;
static const int KEYS[] = {13, 16, 27, 37, 38, 39, 40};   /* Enter, Shift, Esc, the arrows (no Page Up / Page Down: round 17) */
static struct {
  bool have_frame, open;
  float width, height, mouse_x, mouse_y;
  bool button[3], key[256];
  double wheel, dt;
  TfpEdCommand commands[TFP_ED_COMMANDS];
  int count;
  /* round 53: the list of saved placements (src/editor_weapons.h), handed over by the binder */
  bool has_weapons, list_press;   /* list_press: the left button's press began on the list, so it is not the editor's */
  TfpEwWeapon weapons[TFP_EW_MAX];
  int weapon_count;
} shared;
static TfpEwView list_view;   /* the render thread's: what is folded and scrolled, for the session (until this DLL is loaded again) */

/* ---- the render thread ---- */
static uint32_t colour(TfpEdColor role) {
  switch (role) {
    case TFP_ED_COLOR_PANEL: return ov->theme(OV_THEME_PANEL);
    case TFP_ED_COLOR_FIELD: return ov->theme(OV_THEME_ROW);
    case TFP_ED_COLOR_ACCENT: return ov->theme(OV_THEME_ACCENT);
    case TFP_ED_COLOR_TEXT_DIM: return ov->theme(OV_THEME_TEXT_DIM);
    default: return ov->theme(OV_THEME_TEXT);
  }
}
/* Retail's items are in pixels from the bottom left; the host draws from the top left. Shapes follow
 * HD2-UI-Style/DESIGN.md: the panel a window (rounding 10, a 1 px line border), the buttons controls (rounding 6,
 * no border, the hover fill under the pointer), every colour a theme token. x, y: the pointer, from the top left. */
static void paint(const OvFrame *frame, const TfpEdCommand *local, int count, float x, float y) {
  float scale = frame->height / 1080.0f;
  if (scale < .6f) scale = .6f;
  if (scale > 1.5f) scale = 1.5f;
  for (int i = 0; i < count; i++) {
    const TfpEdCommand *c = &local[i];
    if (c->is_text) {
      float top = frame->height - (float)c->y - (float)c->size;
      ov->draw_text((float)c->x, top, colour(c->color), c->color == TFP_ED_COLOR_ACCENT ? 2 : 1, (float)c->size, c->text);
      continue;
    }
    float x0 = (float)c->x, y0 = frame->height - (float)(c->y + c->h), x1 = (float)(c->x + c->w), y1 = frame->height - (float)c->y;
    if (c->color == TFP_ED_COLOR_PANEL) {
      ov->draw_rect_filled(x0, y0, x1, y1, ov->theme(OV_THEME_PANEL), 10 * scale);
      ov->draw_rect(x0, y0, x1, y1, ov->theme(OV_THEME_LINE), 10 * scale, 1);
    } else {
      bool hover = x >= x0 && x <= x1 && y >= y0 && y <= y1;
      ov->draw_rect_filled(x0, y0, x1, y1, ov->theme(hover ? OV_THEME_ROW_HOVER : OV_THEME_ROW), 6 * scale);
    }
  }
}
static void draw(const OvFrame *frame, void *user) {
  static TfpEdCommand local[TFP_ED_COMMANDS], list_local[TFP_EW_COMMANDS];
  int count, list_count = 0;
  float x = 0, y = 0;
  (void)user;
  if (InterlockedCompareExchange(&retired, 0, 0)) return;
  ov->mouse_pos(&x, &y);
  double wheel = ov->mouse_wheel();
  const bool left = ov->mouse_down(0) != 0;
  AcquireSRWLockExclusive(&lock);
  shared.have_frame = true; shared.width = frame->width; shared.height = frame->height;
  shared.mouse_x = x; shared.mouse_y = y;
  shared.button[0] = left;
  for (int i = 1; i < 3; i++) shared.button[i] = ov->mouse_down(i) != 0;
  for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) shared.key[KEYS[i]] = ov->key_down(KEYS[i]) != 0;
  count = shared.count;
  memcpy(local, shared.commands, sizeof local[0] * (size_t)count);
  /* Round 53: the list of saved placements under the editor's panel, while the editor draws. A click on a header folds
   * it; a press that began on the list and the wheel over it stay the list's (the editor would move the weapon). */
  if (count && shared.has_weapons)
    list_count = tfp_ew_frame(&list_view, shared.weapons, shared.weapon_count, frame->width, frame->height, x, frame->height - y, left, &wheel, list_local,
                              TFP_EW_COMMANDS);
  else { list_view.was_down = left; list_view.press_in_list = false; }
  shared.list_press = list_view.press_in_list;
  shared.wheel += wheel;
  shared.dt += frame->delta;
  ReleaseSRWLockExclusive(&lock);
  if (!count) return;
  ov->draw_layer(OV_LAYER_FOREGROUND);
  paint(frame, local, count, x, y);
  paint(frame, list_local, list_count, x, y);
  ov->draw_layer(OV_LAYER_WINDOW);
}

/* ---- the game thread: the platform ---- */
static bool p_available(void *u) { (void)u; return panel != NULL && !InterlockedCompareExchange(&retired, 0, 0); }
static bool p_menu_close(void *u) { (void)u; return tfp_mod_menu_close(); }
static bool p_menu_is_open(void *u) { (void)u; return tfp_mod_menu_is_open(); }
static bool p_busy(void *u) { (void)u; return ov && ov->look_capture && ov->look_capture(); }
static bool p_acquire(void *u) {
  (void)u;
  if (!panel) return false;
  AcquireSRWLockExclusive(&lock);
  shared.have_frame = false; shared.wheel = shared.dt = 0; shared.count = 0; shared.open = true;
  shared.has_weapons = shared.list_press = false; shared.weapon_count = 0;   /* the binder hands the list over at the first drawn frame */
  memset(shared.button, 0, sizeof shared.button); memset(shared.key, 0, sizeof shared.key);
  ReleaseSRWLockExclusive(&lock);
  ov->set_open(panel, 1);   /* the host takes the game's input and shows its cursor while a window is open */
  return true;
}
static bool p_release(void *u) {
  (void)u;
  if (!panel) return true;
  ov->set_open(panel, 0);
  AcquireSRWLockExclusive(&lock);
  shared.open = false; shared.count = 0;
  ReleaseSRWLockExclusive(&lock);
  return true;
}
static bool p_focused(void *u) { (void)u; return ov && ov->is_focused ? ov->is_focused() != 0 : GetForegroundWindow() != NULL; }
static bool p_sample(void *u, TfpEditorSample *out) {
  (void)u;
  AcquireSRWLockExclusive(&lock);
  out->click = shared.button[0] && !shared.list_press;   /* round 53: a press on the list is not a move drag */
  out->x = shared.have_frame ? shared.mouse_x : NAN;
  out->y = shared.have_frame ? shared.height - shared.mouse_y : NAN;
  out->wheel = shared.wheel * 120; shared.wheel = 0;
  out->dt = shared.dt; shared.dt = 0;
  ReleaseSRWLockExclusive(&lock);
  return true;
}
static bool p_down(void *u, int key) {
  bool down;
  (void)u;
  if (key == toggle_key) return false;   /* the key that opens and closes the editor is never also an edit key */
  AcquireSRWLockExclusive(&lock);
  down = key == 2 ? shared.button[1] : key == 4 ? shared.button[2] : key > 0 && key < 256 && shared.key[key];
  ReleaseSRWLockExclusive(&lock);
  return down;
}
static bool p_resolution(void *u, double *width, double *height) {
  bool known;
  (void)u;
  AcquireSRWLockExclusive(&lock);
  known = shared.have_frame; *width = shared.width; *height = shared.height;
  ReleaseSRWLockExclusive(&lock);
  if (known) return true;
  RECT client;   /* until the panel's first frame: the game window's own size */
  HWND game = GetForegroundWindow();
  if (!game || !GetClientRect(game, &client) || client.right < 1 || client.bottom < 1) return false;
  *width = client.right; *height = client.bottom;
  return true;
}
static void p_draw(void *u, const TfpEdCommand *commands, int count) {
  (void)u;
  if (count > TFP_ED_COMMANDS) count = TFP_ED_COMMANDS;
  AcquireSRWLockExclusive(&lock);
  shared.count = commands ? count : 0;
  if (commands) memcpy(shared.commands, commands, sizeof commands[0] * (size_t)count);
  ReleaseSRWLockExclusive(&lock);
}
static void p_weapons(void *u, const TfpEwWeapon *list, int count) {   /* round 53 */
  (void)u;
  if (count > TFP_EW_MAX) count = TFP_EW_MAX;
  AcquireSRWLockExclusive(&lock);
  shared.has_weapons = list != NULL;
  shared.weapon_count = list ? count : 0;
  if (list) memcpy(shared.weapons, list, sizeof list[0] * (size_t)count);
  ReleaseSRWLockExclusive(&lock);
}
static bool p_window(void *u, bool *mouse_focus, bool *show_cursor) {
  const char *error = NULL;
  (void)u;
  return window_platform && window_platform->window_mouse_focus && window_platform->window_show_cursor &&
         window_platform->window_mouse_focus(window_platform->user, mouse_focus, &error) &&
         window_platform->window_show_cursor(window_platform->user, show_cursor, &error);
}

const TfpEditorPlatform *tfp_mod_editor_platform(const TfpFxPlatform *window) {
  static TfpEditorPlatform platform = {NULL, p_available, p_menu_close, p_menu_is_open, p_busy, p_acquire, p_release, p_focused, p_sample, p_down,
                                       p_resolution, p_draw, p_window, p_weapons};
  window_platform = window;
  if (!window || !window->window_mouse_focus || !window->window_show_cursor) platform.window = NULL;
  return &platform;
}
bool tfp_mod_editor_available(void) { return p_available(NULL); }
bool tfp_mod_editor_settled(void) { return panel != NULL || gave_up; }
void tfp_mod_editor_tick(const HfApi *hf, HfMod *mod, int editor_key) {
  toggle_key = editor_key;
  ULONGLONG now = GetTickCount64();
  if (panel || gave_up || now < next_try) return;
  if (!first_try) first_try = now;
  next_try = now + 1000;
  const OvApi *host = ov_connect();
  if (!host) {
    if (now - first_try < 15000) return;
    gave_up = true;
    hf->log(mod, "Placement editor: Goose Overlay Host is not loaded, so the editor cannot open (it needs the host's cursor and panel)");
    return;
  }
  if (host->version < 3 || !host->theme || !host->mouse_down || !host->draw_layer || !host->mouse_wheel) {
    gave_up = true;
    hf->log(mod, "Refused: placement editor: Goose Overlay Host is API %u; the editor needs API 3 or newer", (unsigned)host->version);
    return;
  }
  ov = host;
  /* The host cannot take a panel out and refuses a second one with the same id, and a replaced build of this
   * mod leaves its panel behind: every load registers under an id of its own. */
  char id[96];
  snprintf(id, sizeof id, "goose.true-first-person/placement@%lx-%llx", (unsigned long)GetCurrentProcessId(), (unsigned long long)now);
  OvPanelDef def = {sizeof def, id, "True First Person: placement", OV_PANEL_WINDOW, 0,
                    OV_WINDOW_NO_RESIZE | OV_WINDOW_AUTO_SIZE | OV_WINDOW_NO_TITLE | OV_WINDOW_NO_SCROLLBAR | OV_WINDOW_NO_MOVE, 0, 0, draw, NULL};
  panel = ov->register_panel(&def);
  if (!panel) { gave_up = true; hf->log(mod, "Refused: placement editor: Goose Overlay Host did not take the panel"); return; }
  if (ov->set_panel_alpha) ov->set_panel_alpha(panel, 0);   /* the host's own window stays empty: the editor draws its panel itself */
  hf->log(mod, "Ready: placement editor panel registered with Goose Overlay Host (API %u)", (unsigned)ov->version);
}
void tfp_mod_editor_retire(void) {
  InterlockedExchange(&retired, 1);
  if (panel) ov->set_open(panel, 0);
}
