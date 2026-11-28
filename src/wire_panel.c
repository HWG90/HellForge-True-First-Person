/* See wire_panel.h. */
#include "wire_panel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ROWS = 64, QUEUE = 16 }; /* round 54: 49 controls with Aim-in time (main had 48, the old limit); room to grow */
typedef struct Row { TfpMenuValue value; bool disabled; char label[300]; } Row;
typedef struct Event { int index; bool activate; TfpMenuValue value, previous; } Event;
typedef struct Panel {
  TfpWire *w;
  const OvApi *ov;
  OvPanel *handle;
  TfpPanelPlatform platform;
  const TfpMenuControl *controls;
  int count;
  /* which menu, and the searches for MCM and for the host (game thread) */
  TfpMenuChoice choice;
  bool mcm_started, mcm_refused, started, gave_up;
  uint64_t mcm_first, mcm_next, first_try, next_try;
  TfpKeyEdge f10;            /* F10 while MCM holds the menu */
  bool f10_editing_seen;     /* round 50c: f10_editing is of the previous update (asked then) */
  bool f10_editing;          /* MCM was taking a key, a text or a colour at the previous update */
  /* shared, under the platform's lock */
  Row rows[ROWS];
  Event pending[QUEUE];
  int pending_count;
  bool dropped, retired;
  /* the render thread's own */
  Row shown[ROWS];
  int held, held_value;      /* the slider being dragged, -1: none; its value so far */
  int asking;                /* the key control waiting for a key, -1: none */
  int refused;               /* the key control whose last key was not taken (F8 for the editor, F11 for the toggle), -1: none; shown while it asks */
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
static void key_name(int key, char out[16]) { tfp_wire_menu_key_name(key, out); }   /* src/wire_menu.h */
/* What the user did, for the game thread; the shown value follows at once so the control does not jump back for a frame. */
static void push(Panel *p, int index, bool activate, TfpMenuValue value) {
  lock(p);
  if (p->pending_count == QUEUE) p->dropped = true;
  else {
    Event *e = &p->pending[p->pending_count++];
    *e = (Event){index, activate, value, {TFP_MENU_VALUE_NONE, false, 0}};
    if (!activate) e->previous = p->rows[index].value, p->rows[index].value = value;
  }
  unlock(p);
}
static void tip(Panel *p, const char *text) { if (text && p->ov->is_item_hovered()) p->ov->tooltip(text); }
/* The tooltip of an item that was drawn greyed, called after end_disabled. The host answers "not hovered" for a
 * greyed item (Dear ImGui's rule for a disabled one) and its tooltip asks the same again, so tip() alone never shows
 * anything there. An item that draws nothing, of the greyed item's own rectangle and not greyed, is laid over it and
 * carries the tooltip; the layout goes on from it as it did from the greyed item, which is alone on its line. */
static void tip_greyed(Panel *p, const char *text) {
  const OvApi *ov = p->ov;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  if (!text) return;
  ov->item_rect(&x0, &y0, &x1, &y1);
  if (!(x1 > x0) || !(y1 > y0)) return;
  ov->set_cursor_screen_pos(x0, y0);
  ov->dummy(x1 - x0, y1 - y0);
  tip(p, text);
}
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
  if (row->disabled && p->held == i) p->held = -1;   /* disabled in the middle of a drag (Bodycam switched off): let go, nothing stored */
  ov->begin_disabled(row->disabled);
  switch (c->kind) {
    case TFP_MK_BUTTON:
      /* The window's one main button (DESIGN.md: at most one) is the first: Enable True First Person. */
      if (i == 0 ? ov->button_primary(c->label) : ov->button(c->label)) push(p, i, true, (TfpMenuValue){TFP_MENU_VALUE_NONE, false, 0});
      break;
    case TFP_MK_TOGGLE: case TFP_MK_CHECK:
      v = row->value.boolean;
      if (ov->checkbox(c->label, &v)) push(p, i, false, (TfpMenuValue){TFP_MENU_VALUE_BOOL, v != 0, 0});
      break;
    case TFP_MK_SLIDER:
      if (slider(p, i, c->label, row->value.number, c->min, c->max, c->step, &v)) push(p, i, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, v});
      break;
    case TFP_MK_CHOICE:
      v = (int)row->value.number - 1;
      if (ov->combo(c->label, &v, c->choices, c->choice_count) && v >= 0 && v < c->choice_count) push(p, i, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, v + 1});
      break;
    case TFP_MK_KEY: {
      /* The host has no key-binding widget: the label, the key as a key cap, and a button that asks for the next key. */
      char name[16], change[64];
      int key = (int)row->value.number;
      snprintf(change, sizeof change, "Change##%s", c->id);
      ov->text(c->label), ov->same_line();
      if (key >= 1 && key <= 255) key_name(key, name), ov->key_cap(name); else ov->text_disabled("not bound");
      ov->same_line();
      /* A stored 119 (F8) for the editor means "the default", which here is another key (HellForge has F8): the row
       * would show F8 while that other key stayed the real one. So this row does not take F8. */
      const bool no_f8 = c->menu == TFP_MENU_editor_key && p->platform.editor_key != 119;
      /* Likewise a stored F11 for the toggle reads as U (src/wire_menu.h): HellForge's Armor LUT editor has F11. */
      const bool no_f11 = c->menu == TFP_MENU_toggle_key;
      if (ov->small_button(change)) p->asking = i, p->refused = -1, p->asked_at = frame->time;
      else if (p->asking == i && (row->disabled || frame->time - p->asked_at > 10)) p->asking = -1;
      else if (p->asking == i) {
        if (p->refused == i)
          ov->text_colored(ov->theme(OV_THEME_TEXT_DIM), no_f11 ? "F11 opens HellForge's Armor LUT editor, so it cannot toggle True First Person."
                                                                : "F8 is HellForge's own editor key, so it cannot open this one.");
        ov->text_colored(ov->theme(OV_THEME_TEXT_DIM), "Press a key. Esc keeps the current one, Delete clears it.");
        for (int pressed = 8; pressed < 255 && p->asking == i; pressed++) {
          if (pressed == TFP_PANEL_KEY || !ov->key_pressed(pressed)) continue;   /* the panel's own key closes the window */
          if ((pressed == 119 && no_f8) || (pressed == TFP_TOGGLE_KEY_OLD && no_f11)) { p->refused = i, p->asked_at = frame->time; continue; }   /* said in the row; ten more seconds */
          p->asking = -1;
          if (pressed != 27) push(p, i, false, (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, pressed == 46 ? 0 : pressed});
        }
        ov->end_disabled();
        return;   /* the last item is the hint: no tooltip on it */
      }
      break;
    }
    default: break;
  }
  ov->end_disabled();
  /* (a greyed key row ends in its small button on a line of several items: nothing is laid over that) */
  if (row->disabled && c->kind != TFP_MK_KEY) tip_greyed(p, c->description); else tip(p, c->description);
  /* the row's note, last (no tooltip on it): while the row is disabled, or while its choice shows note_when */
  if (c->note && (c->note_when ? (int)row->value.number == c->note_when : row->disabled))
    ov->text_colored(ov->theme(OV_THEME_TEXT_DIM), c->note);
}
static void draw(const OvFrame *frame, void *user) {
  Panel *p = user;
  const OvApi *ov = p->ov;
  lock(p);
  bool retired = p->retired;
  if (!retired) memcpy(p->shown, p->rows, sizeof p->shown);
  unlock(p);
  if (retired || !ov->begin_tab_bar("pages")) return;
  for (int tab = 0; tab < TFP_MENU_PAGES; tab++) {
    if (!ov->begin_tab_item(tfp_wire_menu_pages[tab])) continue;
    if (tfp_wire_menu_page_intro[tab]) ov->text_wrapped(tfp_wire_menu_page_intro[tab]), ov->spacing();
    for (int i = 0; i < p->count; i++) if (p->controls[i].page == tab) draw_control(p, i, frame);
    ov->end_tab_item();
  }
  ov->end_tab_bar();
}

/* ---- the game thread ---- */
static void say(TfpWire *w, const char *format, unsigned number) {
  char line[520];
  snprintf(line, sizeof line, format, number);
  tfp_wire_say(w, line);
}
static Panel *make(TfpWire *w) {
  Panel *p = state(w);
  if (p || !(p = calloc(1, sizeof *p))) return p;
  p->w = w, p->held = p->asking = p->refused = -1, p->choice = TFP_CHOICE_DECIDING;
  w->ext[TFP_WIRE_EXT_PANEL] = p;
  return p;
}
static bool host_fits(const OvApi *ov) {
  return ov->version >= TFP_PANEL_API && ov->size >= offsetof(OvApi, key_cap) + sizeof ov->key_cap && ov->register_panel && ov->set_open && ov->is_open &&
         ov->text && ov->text_wrapped && ov->text_disabled && ov->text_colored && ov->same_line && ov->spacing && ov->button && ov->small_button &&
         ov->checkbox && ov->slider_int && ov->combo && ov->is_item_hovered && ov->tooltip && ov->begin_tab_bar && ov->end_tab_bar &&
         ov->begin_tab_item && ov->end_tab_item && ov->key_pressed && ov->is_item_active && ov->is_item_deactivated_after_edit &&
         ov->begin_disabled && ov->end_disabled && ov->theme && ov->button_primary && ov->key_cap && ov->item_rect && ov->set_cursor_screen_pos && ov->dummy;
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
    p->pending_count = 0, p->dropped = p->retired = false, p->held = p->asking = p->refused = -1;
    memset(p->rows, 0, sizeof p->rows);
    for (int i = 0; i < count; i++) tfp_wire_menu_default(w, &controls[i], platform->editor_key, &p->rows[i].value);
    const TfpMenuHost host = {p, host_get, host_set, host_set_disabled, host_set_label, "the mod's own panel", platform->editor_key};
    if (!tfp_wire_menu_bind(w, &host, unavailable, edit)) why = "the menu could not be set up";
  }
  if (!why) {
    /* Last: from here the host may call draw. The title bar stays, so the window can be moved and closed with the mouse. */
    OvPanelDef def = {sizeof def, platform->panel_id, "True First Person", OV_PANEL_WINDOW, TFP_PANEL_KEY, 0, 620, 760, draw, p};
    p->handle = ov->register_panel(&def);
    if (!p->handle && platform->panel_id_reload) {
      /* Most likely a replaced build of this mod still holds the id (the host cannot take a panel out). */
      def.id = platform->panel_id_reload;
      p->handle = ov->register_panel(&def);
      if (p->handle)
        tfp_wire_say(w, "Settings panel: NEST (Goose Overlay Host) did not take the panel's usual id (a replaced build of this mod still holds "
                        "it), so this one has an id of its own until the game restarts: the window opens at its first place and size");
    }
    if (!p->handle) tfp_wire_menu_unbind(w), why = "NEST (Goose Overlay Host) did not take the panel";
  }
  if (why) {
    char line[300];
    snprintf(line, sizeof line, "Refused: settings panel: %s. The settings in settings.json apply", why);
    tfp_wire_say(w, line);
    return false;
  }
  say(w, "Ready: settings panel: \"True First Person\" is registered with NEST (Goose Overlay Host, API %u). F10 opens and closes it, and it is in "
         "the host's panel list (Page Down). Three tabs: First person, Bob and sway, Locked view, every option. DBF-MCM is not used (it is not "
         "loaded, did not take the menu, or panel_on.txt asks for this panel)", (unsigned)ov->version);
  return true;
}
/* One line when no menu can be had; `why` names what is missing. */
static void no_menu(TfpWire *w, const Panel *p, const TfpPanelPlatform *platform) {
  char line[700];
  snprintf(line, sizeof line, "Menu: no settings menu in this session: %sNEST (Goose Overlay Host) is not loaded, or is older than API %u. "
           "The settings can be edited by hand while the game is closed: settings.json and locked_view.json in "
           "%%LOCALAPPDATA%%\\Goose\\HellForge\\TrueFirstPerson (docs/guides/POSER-AND-MENU.md lists every option)",
           platform->panel_forced ? "panel_on.txt asks for the mod's own panel, and " : !platform->mcm_try ? "" :
           p->mcm_refused ? "DBF-MCM (goose.mcm) did not take the menu (its line above says why), and " : "DBF-MCM (goose.mcm) is not loaded, and ",
           (unsigned)TFP_PANEL_API);
  tfp_wire_say(w, line);
}
/* The panel's own search for the host, as before there was a choice: a try a second for 15 s, then no more. */
static void panel_search(TfpWire *w, Panel *p, const TfpPanelPlatform *platform, uint64_t now_ms, unsigned unavailable, TfpMenuEditFn edit) {
  if (p->handle || p->gave_up || !platform->connect || (p->started && now_ms < p->next_try)) return;
  if (!p->started) p->started = true, p->first_try = now_ms;
  p->next_try = now_ms + TFP_MENU_TRY_MS;
  const OvApi *ov = platform->connect(platform->user, TFP_PANEL_API);
  if (!ov) {
    if (now_ms - p->first_try < TFP_MENU_CONNECT_MS) return;   /* the host may load after this mod */
    p->gave_up = true, p->choice = TFP_CHOICE_NONE;
    no_menu(w, p, platform);
    return;
  }
  if (!tfp_wire_panel_bind(w, ov, platform, unavailable, edit)) p->gave_up = true, p->choice = TFP_CHOICE_NONE, no_menu(w, p, platform);
}
void tfp_wire_panel_tick(TfpWire *w, const TfpPanelPlatform *platform, uint64_t now_ms, unsigned unavailable, TfpMenuEditFn edit) {
  if (!platform) return;
  Panel *p = make(w);
  if (!p) return;
  if (p->choice == TFP_CHOICE_DECIDING) {
    if (platform->panel_forced || !platform->mcm_try) {
      p->choice = TFP_CHOICE_PANEL;
      if (platform->panel_forced) tfp_wire_say(w, "Test: panel_on.txt: the settings menu is the mod's own panel; DBF-MCM is not tried");
    } else {
      if (p->mcm_started && now_ms < p->mcm_next) return;
      if (!p->mcm_started) p->mcm_started = true, p->mcm_first = now_ms;
      p->mcm_next = now_ms + TFP_MENU_TRY_MS;
      const TfpMcmState state = platform->mcm_try(platform->user, w, unavailable, edit);
      if (state == TFP_MCM_READY) p->choice = TFP_CHOICE_MCM;
      else if (state == TFP_MCM_REFUSED) p->choice = TFP_CHOICE_PANEL, p->mcm_refused = true;   /* MCM's own line says why */
      else if (now_ms - p->mcm_first < TFP_MENU_CONNECT_MS) return;   /* NULL until MCM's first update: it may load after this mod */
      else {
        p->choice = TFP_CHOICE_PANEL;
        say(w, "Menu: DBF-MCM (goose.mcm) did not answer within %u seconds (it is not loaded); the settings menu is the mod's own panel", TFP_MENU_CONNECT_MS / 1000);
      }
    }
  }
  if (p->choice == TFP_CHOICE_MCM) {
    /* F10: MCM's window on this mod's page, or closed; one press, one change */
    if (platform->key_down && platform->mcm_show) {
      /* round 50: a tap between two updates is down and up within this one (src/key_tap.h): one press, the edge left up */
      const bool down = platform->key_down(platform->user, TFP_PANEL_KEY);
      const bool tapped = !down && platform->key_tapped && platform->key_tapped(platform->user, TFP_PANEL_KEY);
      const bool pressed = tfp_wire_menu_key_edge(&p->f10, TFP_PANEL_KEY, down || tapped);
      if (tapped) tfp_wire_menu_key_edge(&p->f10, TFP_PANEL_KEY, false);
      /* round 50c: a tap is judged by MCM's editing at the previous update too (mcm_show sees only now). Asked while
       * F10 is up: a tap is answered only for a key that was up at the update before */
      const bool was_editing = p->f10_editing_seen && p->f10_editing;
      p->f10_editing_seen = !down && platform->key_tapped && platform->mcm_editing;
      p->f10_editing = p->f10_editing_seen && platform->mcm_editing(platform->user);
      if (pressed) {
        bool opened = false;
        const char *refused = tapped && was_editing ? "DBF-MCM was taking a key, a text or a colour at the previous update (F10 belongs to that)"
                                                    : platform->mcm_show(platform->user, &opened);
        char line[280];
        if (refused) snprintf(line, sizeof line, "Menu: F10: refused: %s", refused), tfp_wire_say(w, line);
        else tfp_wire_say(w, opened ? "Menu: F10: opening DBF-MCM on this page" : "Menu: F10: closing DBF-MCM");
      }
    }
    if (now_ms < p->mcm_next) return;
    p->mcm_next = now_ms + TFP_MENU_TRY_MS;
    if (platform->mcm_try(platform->user, w, unavailable, edit) != TFP_MCM_REFUSED) return;   /* registered, or made again */
    p->choice = TFP_CHOICE_PANEL, p->mcm_refused = true;
    tfp_wire_say(w, "Menu: DBF-MCM no longer holds the settings menu and did not take it again; the settings menu is the mod's own panel");
  }
  if (p->choice == TFP_CHOICE_PANEL) panel_search(w, p, platform, now_ms, unavailable, edit);
}
TfpMenuChoice tfp_wire_panel_choice(TfpWire *w) { Panel *p = state(w); return p ? p->choice : TFP_CHOICE_DECIDING; }
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
    if (e->activate) tfp_wire_menu_activate(w, p->controls[e->index].id);   /* done by tfp_wire_menu_frame, which also logs a refusal */
    else tfp_wire_menu_changed(w, p->controls[e->index].id, &e->value, &e->previous);
  }
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
