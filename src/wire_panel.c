/* See wire_panel.h. */
#include "wire_panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROWS = 40, QUEUE = 16, TABS = 3 };
static const char *const LOCKED_TAB = "Locked view";
/* The options of TFP_SETTINGS_LOCKED, in its order: what the panel calls them. `note`: the option sets nothing;
 * it is drawn greyed (the host's begin_disabled) with the note under it, since a greyed item may show no tooltip. */
static const struct { const char *label, *description, *note; } LOCKED[TFP_LOCKED_COUNT] = {
  {"Camera lock", "Keep the view first person through every action and camera of your own character.", NULL},
  {"Field of view lock", "Hold one field of view whatever the weapon or the camera asks for.", NULL},
  {"Field of view (degrees)", "The field of view the lock holds. Default 80.", NULL},
  {"Zoom when aiming", "Let aiming zoom in from the locked field of view. Off keeps the locked value while aiming too.", NULL},
  {"View kick strength (%)", "How much of your body's movement reaches the view while on foot. Default 35.", NULL},
  {"View kick limit (degrees)", "The most the view is turned by a kick while on foot. Default 8.", NULL},
  {"View kick when thrown (%)", "How much of your body's movement reaches the view while you are thrown or ragdolled. Default 70.", NULL},
  {"Animated arms", "The first-person arms follow your character's own animation.", NULL},
  {"One-handed pistols", "One-handed weapons are held as your character holds them. This comes from the character's own animation while "
                         "Animated arms is on; there is nothing to set here.",
   "Comes from your character's own animation while Animated arms is on."},
  {"Weapon put-away", "Keep first person while the weapon is put away for a swap, grenade or stim.", NULL},
  {"Smooth eye", "No camera pops when the game changes its camera.", NULL},
};
_Static_assert(TFP_LOCKED_COUNT == 11 && TFP_LOCKED_animated_arms == 7 && TFP_LOCKED_one_handed_pistols == 8 && TFP_LOCKED_put_away == 9 &&
               TFP_LOCKED_smooth_eye == 10, "LOCKED names the options of TFP_SETTINGS_LOCKED in its order");

typedef struct Row { TfpMenuValue value; bool disabled; char label[300]; } Row;
typedef struct Event { int index; bool locked, activate; TfpMenuValue value, previous; } Event;
typedef struct Panel {
  TfpWire *w;
  const OvApi *ov;
  OvPanel *handle;
  TfpPanelPlatform platform;
  const TfpMenuControl *controls;
  int count;
  /* the search for the host (game thread) */
  bool started, gave_up;
  uint64_t first_try, next_try;
  /* shared, under the platform's lock */
  Row rows[ROWS];
  TfpSettingsMenuValue locked[TFP_LOCKED_COUNT];
  Event pending[QUEUE];
  int pending_count;
  bool dropped, retired;
  /* the render thread's own */
  Row shown[ROWS];
  TfpSettingsMenuValue shown_locked[TFP_LOCKED_COUNT];
  int held, held_value;      /* the slider being dragged (locked options count on from `count`), -1: none; its value so far */
  int asking;                /* the key control waiting for a key, -1: none */
  double asked_at;
} Panel;

static Panel *state(TfpWire *w) { return w->ext[TFP_WIRE_EXT_PANEL]; }
static void lock(Panel *p) { p->platform.lock(p->platform.user); }
static void unlock(Panel *p) { p->platform.unlock(p->platform.user); }
static int row_of(const Panel *p, const char *id) {
  for (int i = 0; id && i < p->count; i++) if (!strcmp(p->controls[i].id, id)) return i;
  return -1;
}

/* ---- the menu's host (game thread): what src/mod_menu.c is for DBF-MCM ---- */
static bool host_get(void *user, const char *id, TfpMenuValue *out) {
  Panel *p = user;
  int i = row_of(p, id);
  if (i < 0) return false;
  lock(p), *out = p->rows[i].value, unlock(p);
  return out->kind != TFP_MENU_VALUE_NONE;
}
static bool host_set(void *user, const char *id, const TfpMenuValue *value) {
  Panel *p = user;
  int i = row_of(p, id);
  if (i < 0 || !value) return false;
  lock(p);
  TfpMenuValue previous = p->rows[i].value;
  if (previous.kind != value->kind) { unlock(p); return false; }
  p->rows[i].value = *value;
  unlock(p);
  /* As MCM does after a set: the menu hears of the change (it ignores the echo of its own put-back). */
  if (previous.boolean != value->boolean || previous.number != value->number) tfp_wire_menu_changed(p->w, id, value, &previous);
  return true;
}
static bool host_set_disabled(void *user, const char *id, bool disabled) {
  Panel *p = user;
  int i = row_of(p, id);
  if (i < 0) return false;
  lock(p), p->rows[i].disabled = disabled, unlock(p);
  return true;
}
static bool host_set_label(void *user, const char *id, const char *label) {
  Panel *p = user;
  int i = row_of(p, id);
  if (i < 0 || !label) return false;
  lock(p), snprintf(p->rows[i].label, sizeof p->rows[i].label, "%s", label), unlock(p);
  return true;
}

/* ---- the render thread ---- */
static void key_name(int key, char out[16]) {
  static const struct { int key; const char *name; } named[] = {
    {8, "BACKSPACE"}, {9, "TAB"}, {13, "ENTER"}, {16, "SHIFT"}, {17, "CTRL"}, {18, "ALT"}, {19, "PAUSE"}, {20, "CAPS LOCK"}, {27, "ESC"},
    {32, "SPACE"}, {33, "PAGE UP"}, {34, "PAGE DOWN"}, {35, "END"}, {36, "HOME"}, {37, "LEFT"}, {38, "UP"}, {39, "RIGHT"}, {40, "DOWN"},
    {45, "INSERT"}, {46, "DELETE"}, {1, "MOUSE 1"}, {2, "MOUSE 2"}, {4, "MOUSE 3"}, {5, "MOUSE 4"}, {6, "MOUSE 5"}};
  for (size_t i = 0; i < sizeof named / sizeof *named; i++) if (named[i].key == key) { snprintf(out, 16, "%s", named[i].name); return; }
  if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) snprintf(out, 16, "%c", key);
  else if (key >= 0x60 && key <= 0x69) snprintf(out, 16, "NUM %d", key - 0x60);
  else if (key >= 0x70 && key <= 0x87) snprintf(out, 16, "F%d", key - 0x6f);
  else snprintf(out, 16, "KEY %d", key);
}
/* What the user did, for the game thread; the shown value follows at once so the control does not jump back for a frame. */
static void push(Panel *p, int index, bool locked, bool activate, TfpMenuValue value) {
  lock(p);
  if (p->pending_count == QUEUE) p->dropped = true;
  else {
    Event *e = &p->pending[p->pending_count++];
    *e = (Event){index, locked, activate, value, {TFP_MENU_VALUE_NONE, false, 0}};
    if (locked) p->locked[index].boolean = value.boolean, p->locked[index].number = value.number;
    else if (!activate) e->previous = p->rows[index].value, p->rows[index].value = value;
  }
  unlock(p);
}
static void tip(Panel *p, const char *text) { if (text && p->ov->is_item_hovered()) p->ov->tooltip(text); }
/* A whole-number slider with a step. `slot` names it among all sliders. true: let go on a new value (*value). */
static bool slider(Panel *p, int slot, const char *label, double now, double min, double max, double step, int *value) {
  const OvApi *ov = p->ov;
  int stored = (int)(now + (now < 0 ? -.5 : .5)), v = p->held == slot ? p->held_value : stored;
  if (ov->slider_int(label, &v, (int)min, (int)max)) {
    if (step > 1) v = (int)min + (int)(((double)(v - (int)min) + step / 2) / step) * (int)step;
    v = v < (int)min ? (int)min : v > (int)max ? (int)max : v;
    p->held = slot, p->held_value = v;
  }
  if (p->held != slot || (!ov->is_item_deactivated_after_edit() && ov->is_item_active())) return false;
  p->held = -1;   /* let go; also when the host never said so (the window was closed mid-drag) */
  *value = p->held_value;
  return *value != stored;
}
static void draw_control(Panel *p, int i, const OvFrame *frame) {
  const OvApi *ov = p->ov;
  const TfpMenuControl *c = &p->controls[i];
  const Row *row = &p->shown[i];
  int v;
  if (c->kind == TFP_MK_STATUS) { ov->text_wrapped(row->label[0] ? row->label : c->label); return; }
  ov->begin_disabled(row->disabled);
  switch (c->kind) {
    case TFP_MK_BUTTON:
      /* The window's one main button (DESIGN.md: at most one) is the first: Enable True First Person. */
      if (i == 0 ? ov->button_primary(c->label) : ov->button(c->label)) push(p, i, false, true, (TfpMenuValue){TFP_MENU_VALUE_NONE, false, 0});
      break;
    case TFP_MK_TOGGLE: case TFP_MK_CHECK:
      v = row->value.boolean;
      if (ov->checkbox(c->label, &v)) push(p, i, false, false, (TfpMenuValue){TFP_MENU_VALUE_BOOL, v != 0, 0});
      break;
    case TFP_MK_SLIDER:
      if (slider(p, i, c->label, row->value.number, c->min, c->max, c->step, &v)) push(p, i, false, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, v});
      break;
    case TFP_MK_CHOICE:
      v = (int)row->value.number - 1;
      if (ov->combo(c->label, &v, tfp_wire_menu_presets, 3) && v >= 0 && v < 3) push(p, i, false, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, v + 1});
      break;
    case TFP_MK_KEY: {
      /* The host has no key-binding widget: the label, the key as a key cap, and a button that asks for the next key. */
      char name[16], change[64];
      int key = (int)row->value.number;
      snprintf(change, sizeof change, "Change##%s", c->id);
      ov->text(c->label), ov->same_line();
      if (key >= 1 && key <= 255) key_name(key, name), ov->key_cap(name); else ov->text_disabled("not bound");
      ov->same_line();
      if (ov->small_button(change)) p->asking = i, p->asked_at = frame->time;
      else if (p->asking == i && (row->disabled || frame->time - p->asked_at > 10)) p->asking = -1;
      else if (p->asking == i) {
        ov->text_colored(ov->theme(OV_THEME_TEXT_DIM), "Press a key. Esc keeps the current one, Delete clears it.");
        for (int pressed = 8; pressed < 255 && p->asking == i; pressed++) {
          if (pressed == TFP_PANEL_KEY || !ov->key_pressed(pressed)) continue;   /* the panel's own key closes the window */
          p->asking = -1;
          if (pressed != 27) push(p, i, false, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, pressed == 46 ? 0 : pressed});
        }
        ov->end_disabled();
        return;   /* the last item is the hint: no tooltip on it */
      }
      break;
    }
    default: break;
  }
  tip(p, c->description);
  ov->end_disabled();
}
static void draw_locked(Panel *p) {
  const OvApi *ov = p->ov;
  ov->text_wrapped("First person through every action, one field of view, and how much your body's movement moves the view.");
  ov->spacing();
  for (int i = 0; i < TFP_LOCKED_COUNT; i++) {
    const TfpSettingsLockedInfo *info = tfp_settings_locked_info((TfpSettingsLockedId)i);
    int v = p->shown_locked[i].boolean;
    if (LOCKED[i].note) {
      ov->begin_disabled(1);
      ov->checkbox(LOCKED[i].label, &v);   /* shown as stored; the host blocks it */
      ov->end_disabled();
      tip(p, LOCKED[i].description);
      ov->text_colored(ov->theme(OV_THEME_TEXT_DIM), LOCKED[i].note);
      continue;
    }
    if (info->is_bool) {
      if (ov->checkbox(LOCKED[i].label, &v)) push(p, i, true, false, (TfpMenuValue){TFP_MENU_VALUE_BOOL, v != 0, 0});
    } else if (slider(p, p->count + i, LOCKED[i].label, p->shown_locked[i].number, info->min, info->max, info->step, &v))
      push(p, i, true, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, v});
    tip(p, LOCKED[i].description);
  }
}
static void draw(const OvFrame *frame, void *user) {
  Panel *p = user;
  const OvApi *ov = p->ov;
  lock(p);
  bool retired = p->retired;
  if (!retired) memcpy(p->shown, p->rows, sizeof p->shown), memcpy(p->shown_locked, p->locked, sizeof p->shown_locked);
  unlock(p);
  if (retired || !ov->begin_tab_bar("pages")) return;
  for (int tab = 0; tab < TABS; tab++) {
    if (!ov->begin_tab_item(tab < 2 ? tfp_wire_menu_pages[tab] : LOCKED_TAB)) continue;
    if (tab == 2) draw_locked(p);
    else for (int i = 0; i < p->count; i++) if (p->controls[i].page == tab) draw_control(p, i, frame);
    ov->end_tab_item();
  }
  ov->end_tab_bar();
}

/* ---- the game thread ---- */
static void say(TfpWire *w, const char *format, unsigned number) {
  char line[420];
  snprintf(line, sizeof line, format, number);
  tfp_wire_say(w, line);
}
static Panel *make(TfpWire *w) {
  Panel *p = state(w);
  if (p || !(p = calloc(1, sizeof *p))) return p;
  p->w = w, p->held = p->asking = -1;
  w->ext[TFP_WIRE_EXT_PANEL] = p;
  return p;
}
static bool host_fits(const OvApi *ov) {
  return ov->version >= TFP_PANEL_API && ov->size >= offsetof(OvApi, key_cap) + sizeof ov->key_cap && ov->register_panel && ov->set_open && ov->is_open &&
         ov->text && ov->text_wrapped && ov->text_disabled && ov->text_colored && ov->same_line && ov->spacing && ov->button && ov->small_button &&
         ov->checkbox && ov->slider_int && ov->combo && ov->is_item_hovered && ov->tooltip && ov->begin_tab_bar && ov->end_tab_bar &&
         ov->begin_tab_item && ov->end_tab_item && ov->key_pressed && ov->is_item_active && ov->is_item_deactivated_after_edit &&
         ov->begin_disabled && ov->end_disabled && ov->theme && ov->button_primary && ov->key_cap;
}
bool tfp_wire_panel_bound(TfpWire *w) { Panel *p = state(w); return p && p->handle; }
bool tfp_wire_panel_bind(TfpWire *w, const OvApi *ov, const TfpPanelPlatform *platform, unsigned unavailable, TfpMenuEditFn edit) {
  int count = 0;
  const TfpMenuControl *controls = tfp_wire_menu_controls(&count);
  if (!ov || !platform || !platform->lock || !platform->unlock || !platform->panel_id) return false;
  if (!host_fits(ov)) {
    say(w, "Refused: settings panel: NEST (Goose Overlay Host) is API %u or lacks a function the panel draws with; it needs API 6 or newer "
           "(host 0.4.0). The settings in settings.json apply", (unsigned)ov->version);
    return false;
  }
  const char *why = !tfp_wire_settings(w) ? "the settings store is not bound" : tfp_wire_menu_bound(w) ? "the menu is already registered elsewhere" :
                    count > ROWS ? "the menu has more controls than the panel holds" : NULL;
  Panel *p = why ? NULL : make(w);
  if (!why && !p) why = "out of memory";
  if (!why && p->handle) return true;
  if (!why) {
    p->ov = ov, p->platform = *platform, p->controls = controls, p->count = count;
    p->pending_count = 0, p->dropped = p->retired = false, p->held = p->asking = -1;
    memset(p->rows, 0, sizeof p->rows);
    for (int i = 0; i < count; i++) tfp_wire_menu_default(w, &controls[i], platform->editor_key, &p->rows[i].value);
    memcpy(p->locked, tfp_wire_settings(w)->locked, sizeof p->locked);
    const TfpMenuHost host = {p, host_get, host_set, host_set_disabled, host_set_label, "the mod's own panel"};
    if (!tfp_wire_menu_bind(w, &host, unavailable, edit)) why = "the menu could not be set up";
  }
  if (!why) {
    /* Last: from here the host may call draw. The title bar stays, so the window can be moved and closed with the mouse. */
    OvPanelDef def = {sizeof def, platform->panel_id, "True First Person", OV_PANEL_WINDOW, TFP_PANEL_KEY, 0, 620, 760, draw, p};
    p->handle = ov->register_panel(&def);
    if (!p->handle) tfp_wire_menu_unbind(w), why = "NEST (Goose Overlay Host) did not take the panel";
  }
  if (why) {
    char line[300];
    snprintf(line, sizeof line, "Refused: settings panel: %s. The settings in settings.json apply", why);
    tfp_wire_say(w, line);
    return false;
  }
  say(w, "Ready: settings panel: \"True First Person\" is registered with NEST (Goose Overlay Host, API %u). F10 opens and closes it, and it is in "
         "the host's panel list (Page Down). Three tabs: First person, Bob and sway, Locked view. DBF-MCM is not used", (unsigned)ov->version);
  return true;
}
void tfp_wire_panel_tick(TfpWire *w, const TfpPanelPlatform *platform, uint64_t now_ms, unsigned unavailable, TfpMenuEditFn edit) {
  if (!platform) return;
  if (platform->mcm_wanted) { if (platform->mcm_tick) platform->mcm_tick(platform->user, w, unavailable, edit); return; }
  Panel *p = make(w);
  if (!p || p->handle || p->gave_up || !platform->connect || (p->started && now_ms < p->next_try)) return;
  if (!p->started) p->started = true, p->first_try = now_ms;
  p->next_try = now_ms + 1000;
  const OvApi *ov = platform->connect(platform->user);
  if (!ov) {
    if (now_ms - p->first_try < 15000) return;   /* the host may load after this mod */
    p->gave_up = true;
    say(w, "Settings panel: NEST (Goose Overlay Host) is not loaded, or is older than API %u, so there is no settings panel; the settings in "
           "settings.json apply", (unsigned)OV_API_VERSION);
    return;
  }
  if (!tfp_wire_panel_bind(w, ov, platform, unavailable, edit)) p->gave_up = true;
}
void tfp_wire_panel_frame(TfpWire *w) {
  Panel *p = state(w);
  Event events[QUEUE];
  if (!p || !p->handle) return;
  lock(p);
  int count = p->pending_count;
  bool dropped = p->dropped;
  memcpy(events, p->pending, sizeof events[0] * (size_t)count);
  p->pending_count = 0, p->dropped = false;
  unlock(p);
  if (dropped) tfp_wire_say(w, "Settings panel: more than 16 changes in one update; the rest were dropped");
  for (int i = 0; i < count; i++) {
    const Event *e = &events[i];
    if (e->locked) tfp_wire_settings_locked_set(w, (TfpSettingsLockedId)e->index, e->value.kind == TFP_MENU_VALUE_BOOL, e->value.boolean, e->value.number);
    else if (e->activate) tfp_wire_menu_activate(w, p->controls[e->index].id);   /* done by tfp_wire_menu_frame, which also logs a refusal */
    else tfp_wire_menu_changed(w, p->controls[e->index].id, &e->value, &e->previous);
  }
  /* The locked-view options show what the store holds (it keeps a number inside its range). Only after a change. */
  if (!count || !tfp_wire_settings(w)) return;
  lock(p);
  if (!p->pending_count) memcpy(p->locked, tfp_wire_settings(w)->locked, sizeof p->locked);
  unlock(p);
}
bool tfp_wire_panel_is_open(TfpWire *w) { Panel *p = state(w); return p && p->handle && p->ov->is_open(p->handle) != 0; }
void tfp_wire_panel_close(TfpWire *w) { Panel *p = state(w); if (p && p->handle && p->ov->is_open(p->handle)) p->ov->set_open(p->handle, 0); }
void tfp_wire_panel_retire(TfpWire *w) {
  Panel *p = state(w);
  if (!p || !p->handle) return;
  lock(p), p->retired = true, p->pending_count = 0, unlock(p);
  p->ov->set_open(p->handle, 0);
}
void tfp_wire_panel_free(TfpWire *w) { free(state(w)); w->ext[TFP_WIRE_EXT_PANEL] = NULL; }
