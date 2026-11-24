/* Retail src/first_person/placement_editor.lua in C: see placement_editor.h. Line numbers in the
 * comments are the Lua file's at 1d0d453. */
#include "placement_editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum Phase { IDLE, QUEUED, HANDOFF, ACTIVE, CLOSING } Phase;
static const char *const PHASE[] = {"idle", "queued", "handoff", "active", "closing"};
enum { B_PREVIOUS, B_NEXT, B_SAVE, B_SAVE_ALL, B_CANCEL, B_RESET, B_RESET_ALL, B_COUNT };
typedef struct Rect { bool has; double x, y, w, h; } Rect;

struct TfpEditor {
  TfpEdHost host;
  Phase phase;
  unsigned api, input;                 /* identities; 0 nil */
  bool view, view_draw, view_refresh;  /* `view` kept only when it has release (:293) */
  bool has_original, has_draft;
  TfpEdPlace original, draft;
  bool input_owned, placement_owned;
  TfpEdValue weapon_key, weapon_owner;
  int action_index;                    /* 1-based */
  int nactions;
  char action_id[TFP_ED_ACTIONS][48], action_label[TFP_ED_ACTIONS][64];
  bool restored, committed;
  char label[512];
  bool ready, click_held, enter_held, escape_held;
  Rect drag, panel, buttons[B_COUNT];
  bool has_buttons;
  bool right_held, middle_held, has_rotation_drag, has_middle_drag;
  double rotation_x, rotation_y, middle_x;
  bool has_kind;
  char kind[48];
};

static const double BASELINE[3] = {.15, 0, -.12};
static const double LIMIT = .5;

static void set_text(char *out, size_t size, const char *in) {
  size_t n = strlen(in);
  if (n >= size) n = size - 1;
  memcpy(out, in, n); out[n] = 0;
}
/* ---- values ---- */
static TfpEdValue nil(void) { TfpEdValue v; memset(&v, 0, sizeof v); return v; }
static TfpEdValue boolean(bool b) { TfpEdValue v = nil(); v.kind = b ? TFP_ED_TRUE : TFP_ED_FALSE; return v; }
static TfpEdValue text(const char *s) { TfpEdValue v = nil(); v.kind = TFP_ED_TEXT; set_text(v.s, sizeof v.s, s); return v; }
static bool truthy(const TfpEdValue *v) { return v->kind != TFP_ED_NIL && v->kind != TFP_ED_FALSE; }
static bool is_true(const TfpEdValue *v) { return v->kind == TFP_ED_TRUE; }
static bool same(const TfpEdValue *a, const TfpEdValue *b) {
  if (a->kind != b->kind) return false;
  if (a->kind == TFP_ED_NUMBER) return a->n == b->n;
  if (a->kind == TFP_ED_TEXT) return !strcmp(a->s, b->s);
  return a->kind != TFP_ED_TABLE;
}
/* tostring(v) */
static const char *tostr(const TfpEdValue *v, char out[224]) {
  switch (v->kind) {
    case TFP_ED_NIL: return "nil";
    case TFP_ED_FALSE: return "false";
    case TFP_ED_TRUE: return "true";
    case TFP_ED_NUMBER: snprintf(out, 224, "%.14g", v->n); return out;
    case TFP_ED_TEXT: return v->s;
    default: return "table";
  }
}
/* tostring(v or fallback) */
static const char *tostr_or(const TfpEdValue *v, const char *fallback, char out[224]) { return truthy(v) ? tostr(v, out) : fallback; }
static bool finite_number(double n) { return n == n && n > -INFINITY && n < INFINITY; }

/* ---- placements (:10-17) ---- */
static TfpEdPlace copy(const TfpEdPlace *p) {
  TfpEdPlace out = *p;
  for (int i = 3; i < 6; i++) if (!p->has[i]) { out.has[i] = true; out.v[i] = 0; }
  return out;
}
static bool valid(bool table, const TfpEdPlace *p) {
  if (!table) return false;
  for (int i = 0; i < 3; i++) if (!p->has[i] || !finite_number(p->v[i])) return false;
  for (int i = 0; i < 3; i++) if (!(fabs(p->v[i]) <= LIMIT)) return false;
  double yaw = p->has[3] ? p->v[3] : 0, pitch = p->has[4] ? p->v[4] : 0, roll = p->has[5] ? p->v[5] : 0;
  return finite_number(yaw) && finite_number(pitch) && fabs(yaw) <= 90 && fabs(pitch) <= 90 && finite_number(roll) && fabs(roll) <= 90;
}
static double clamp(double n) { return fmax(-LIMIT, fmin(LIMIT, n)); }
static bool inside(double x, double y, const Rect *r) {
  return finite_number(x) && finite_number(y) && x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}
static TfpEdPlace baseline_place(void) {
  TfpEdPlace p; memset(&p, 0, sizeof p);
  for (int i = 0; i < 6; i++) { p.has[i] = true; p.v[i] = i < 3 ? BASELINE[i] : 0; }
  return p;
}

/* ---- the host ---- */
static void look(const TfpEditor *e, TfpEdLook *out) { memset(out, 0, sizeof *out); e->host.look(e->host.user, out); }
/* plain(target, name, ...) (:42-47): a raise answers nil, text. */
static void plain(TfpEditor *e, TfpEdCall which, const TfpEdArgs *args, TfpEdReply *r) {
  TfpEdArgs none; memset(&none, 0, sizeof none);
  memset(r, 0, sizeof *r);
  if (!e->host.call(e->host.user, which, args ? args : &none, r)) { TfpEdValue why = r->a; memset(r, 0, sizeof *r); r->b = why; r->b.kind = TFP_ED_TEXT; }
}
/* invoke(target, name, ...) (:36-41): a raise answers false, text. */
static void invoke(TfpEditor *e, TfpEdCall which, const TfpEdArgs *args, TfpEdReply *r) {
  TfpEdArgs none; memset(&none, 0, sizeof none);
  memset(r, 0, sizeof *r);
  if (!e->host.call(e->host.user, which, args ? args : &none, r)) {
    TfpEdValue why = r->a; memset(r, 0, sizeof *r); r->a = boolean(false); r->b = why; r->b.kind = TFP_ED_TEXT;
  }
}
/* plain(input, name, ...) when `input` may be nil (the provider lost its camera_input): nil, 'Missing <name>' and no call (:43). */
static void plain_input(TfpEditor *e, TfpEdCall which, const char *name, const TfpEdArgs *args, TfpEdReply *r) {
  if (e->input) { plain(e, which, args, r); return; }
  char why[64];
  snprintf(why, sizeof why, "Missing %s", name);
  memset(r, 0, sizeof *r); r->b = text(why);
}
static void report(TfpEditor *e, const char *message) {   /* :35 */
  char line[560];
  set_text(e->label, sizeof e->label, message);
  snprintf(line, sizeof line, "Placement editor: %s", e->label);
  if (e->host.log) e->host.log(e->host.user, line);
}
static bool now(TfpEditor *e) {   /* :48-52: whether the clock answered a finite number */
  TfpEdArgs none; TfpEdReply r; memset(&none, 0, sizeof none); memset(&r, 0, sizeof r);
  return e->host.call(e->host.user, TFP_ED_CLOCK, &none, &r) && r.a.kind == TFP_ED_NUMBER && finite_number(r.a.n);
}
static bool clean_controller(TfpEditor *e) {   /* :53-56 */
  TfpEdArgs none; TfpEdReply r; memset(&none, 0, sizeof none); memset(&r, 0, sizeof r);
  return e->host.call(e->host.user, TFP_ED_C_IS_CLEAN, &none, &r) && is_true(&r.a);
}
static bool enabled(TfpEditor *e) { TfpEdReply r; invoke(e, TFP_ED_C_IS_ENABLED, NULL, &r); return is_true(&r.a); }   /* :57-59 */
static bool status_busy(const TfpEdReply *r) {
  return r->a.kind != TFP_ED_TABLE || truthy(&r->status.owner) || r->status.active || r->status.pending_restore;
}
static bool foreign_lut_active(TfpEditor *e) {   /* :60-73 */
  TfpEdLook l; look(e, &l);
  if (!l.lut || l.lut_closed) return false;
  if (l.lut_menu_visible) return true;
  if (l.lut_was_visible) return true;
  if (l.lut_capture) {
    if (l.lut_capture_active) return true;
    TfpEdReply r;
    if (l.lut_capture_status) plain(e, TFP_ED_LUT_STATUS, NULL, &r); else memset(&r, 0, sizeof r);
    if (r.a.kind == TFP_ED_TABLE && (truthy(&r.status.owner) || r.status.pending_restore)) return true;
  }
  return false;
}
static bool gameplay_cursor_ready(TfpEditor *e) {   /* :74-81 */
  TfpEdLook l; look(e, &l);
  TfpEdReply r;
  if (!l.window) return true;
  if (l.window_mouse_focus) { plain(e, TFP_ED_W_MOUSE_FOCUS, NULL, &r); if (!is_true(&r.a)) return false; }
  if (l.window_show_cursor) { plain(e, TFP_ED_W_SHOW_CURSOR, NULL, &r); if (r.a.kind != TFP_ED_FALSE) return false; }
  return true;
}
static bool controller_running(TfpEditor *e) {   /* :93-98 */
  TfpEdReply r;
  invoke(e, TFP_ED_C_RESULT, NULL, &r);
  if (!enabled(e) || r.a.kind != TFP_ED_TABLE) return false;
  return !strcmp(r.a.s, "running") || !strcmp(r.a.s, "reacquiring") || !strcmp(r.a.s, "waiting") || !strcmp(r.a.s, "suspended")
      || !strcmp(r.a.s, "suspend_pending");
}
static void take_actions(TfpEditor *e, const TfpEdLook *l, bool fallback) {
  e->nactions = l->nactions;
  if (l->nactions < 0 && fallback) { e->nactions = 1; snprintf(e->action_id[0], sizeof e->action_id[0], "idle"); snprintf(e->action_label[0], sizeof e->action_label[0], "Idle"); return; }
  for (int i = 0; i < l->nactions && i < TFP_ED_ACTIONS; i++) {
    snprintf(e->action_id[i], sizeof e->action_id[i], "%s", l->action_id[i]);
    snprintf(e->action_label[i], sizeof e->action_label[i], "%s", l->action_label[i]);
  }
}
static const char *action(const TfpEditor *e) { return e->action_id[e->action_index - 1]; }

/* apply(value) (:99-106). false: *problem is retail's second result. */
static bool apply(TfpEditor *e, const TfpEdPlace *value, TfpEdValue *problem) {
  if (!valid(true, value)) { *problem = text("Placement must be finite XYZ within 50 cm"); return false; }
  if (e->has_draft) {
    bool equal = value->v[0] == e->draft.v[0] && value->v[1] == e->draft.v[1] && value->v[2] == e->draft.v[2];
    for (int i = 3; i < 6 && equal; i++) equal = (value->has[i] ? value->v[i] : 0) == e->draft.v[i];
    if (equal) return true;
  }
  TfpEdArgs a; TfpEdReply r; memset(&a, 0, sizeof a);
  a.place = copy(value);
  invoke(e, TFP_ED_C_SET_PLACEMENT, &a, &r);
  if (!is_true(&r.a)) { *problem = truthy(&r.b) ? r.b : text("Live placement update refused"); return false; }
  e->draft = copy(value); e->has_draft = true;
  return true;
}

static void append(char *errors, size_t size, const char *one) {
  size_t used = strlen(errors);
  snprintf(errors + used, size - used, "%s%s", used ? "; " : "", one);
}
static TfpEdResult ok(void) { TfpEdResult r; memset(&r, 0, sizeof r); r.r1 = boolean(true); return r; }
static TfpEdResult refuse(const char *why) { TfpEdResult r; memset(&r, 0, sizeof r); r.r1 = boolean(false); if (why) r.r2 = text(why); return r; }

static TfpEdResult release_all(TfpEditor *e) {   /* :107-136 */
  char errors[1024] = "", scratch[224];
  bool failed = false;
  TfpEdLook l; TfpEdReply r;
  look(e, &l);
  if (truthy(&e->weapon_key) && l.has_target) {
    plain(e, TFP_ED_S_TARGET, NULL, &r);
    if (!same(&r.a, &e->weapon_key) || !same(&r.b, &e->weapon_owner)) e->placement_owned = false;
  }
  if (e->placement_owned && !e->restored && !e->committed && e->has_original) {
    TfpEdArgs a; memset(&a, 0, sizeof a);
    a.place = copy(&e->original);
    invoke(e, TFP_ED_C_SET_PLACEMENT, &a, &r);
    e->restored = is_true(&r.a);
    if (!e->restored) { append(errors, sizeof errors, tostr_or(&r.b, "Placement restoration pending", scratch)); failed = true; }
  }
  if (e->input_owned) {
    plain_input(e, TFP_ED_I_RELEASE, "release", NULL, &r);
    if (is_true(&r.a)) e->input_owned = false;
    else { append(errors, sizeof errors, tostr_or(&r.b, "Input restoration pending", scratch)); failed = true; }
  }
  if (e->view) {
    plain(e, TFP_ED_V_RELEASE, NULL, &r);
    if (r.a.kind != TFP_ED_FALSE && r.b.kind == TFP_ED_NIL) e->view = false;
    else { append(errors, sizeof errors, tostr_or(&r.b, "Editor view cleanup pending", scratch)); failed = true; }
  }
  if (failed) { e->phase = CLOSING; return refuse(errors); }
  e->api = e->input = 0; e->view = false; e->has_original = e->has_draft = false;
  e->weapon_key = e->weapon_owner = nil();
  e->input_owned = e->placement_owned = false;
  e->drag.has = e->panel.has = false; e->has_buttons = false;
  e->has_rotation_drag = false; e->right_held = false; e->has_middle_drag = false; e->middle_held = false;
  e->phase = IDLE;
  return ok();
}
/* close(reason, saved) (:137-143). reason NULL: nil. */
static TfpEdResult close_editor(TfpEditor *e, const char *reason, bool saved) {
  if (e->phase == IDLE) return ok();
  if (e->phase != CLOSING) report(e, reason ? reason : saved ? "Placement saved" : "Placement cancelled");
  e->committed = e->committed || saved;
  e->phase = CLOSING;
  return release_all(e);
}
static TfpEdResult closef(TfpEditor *e, const char *prefix, const char *detail) {
  char reason[480];
  snprintf(reason, sizeof reason, "%s%s", prefix, detail);
  return close_editor(e, reason, false);
}
static int button_at(const TfpEditor *e, double x, double y) {   /* :144-146 */
  if (!e->has_buttons) return -1;
  for (int i = 0; i < B_COUNT; i++) if (e->buttons[i].has && inside(x, y, &e->buttons[i])) return i;
  return -1;
}

/* draw(w, h) (:147-190). false: *why is retail's second result. */
static bool draw(TfpEditor *e, double w, double h, TfpEdValue *why) {
  TfpEdCommand commands[TFP_ED_COMMANDS];
  int n = 0;
  TfpEdLook l;
  char scratch[224];
  double scale = fmax(.6, fmin(1.5, h / 1080));
  memset(commands, 0, sizeof commands);
  e->panel = (Rect){true, 24 * scale, h - 280 * scale, fmin(w - 48 * scale, 530 * scale), 256 * scale};
#define RECT(r, c, pa) (commands[n++] = (TfpEdCommand){false, pa, (r).x, (r).y, (r).w, (r).h, 0, c, 300, ""})
#define TEXT(value, tx, ty, ts, c) do { commands[n] = (TfpEdCommand){true, false, tx, ty, 0, 0, (ts) * scale, c, 301, ""}; \
    set_text(commands[n].text, sizeof commands[n].text, value); n++; } while (0)
  RECT(e->panel, TFP_ED_COLOR_PANEL, true);
  double x = e->panel.x + 16 * scale, top = e->panel.y + e->panel.h - 28 * scale;
  char line[400];
  look(e, &l);
  if (truthy(&e->weapon_key)) snprintf(line, sizeof line, "%s  [%s]", l.title ? l.title : "WEAPON PLACEMENT", tostr(&e->weapon_key, scratch));
  else snprintf(line, sizeof line, "%s", l.title ? l.title : "WEAPON PLACEMENT");
  TEXT(line, x, top, 17, TFP_ED_COLOR_ACCENT);
  snprintf(line, sizeof line, "Editing: %s   (placement preview)", e->action_label[e->action_index - 1]);
  TEXT(line, x + 80 * scale, top - 30 * scale, 16, TFP_ED_COLOR_TEXT);
  snprintf(line, sizeof line, "Right %+.1f cm   Forward %+.1f cm   Up %+.1f cm", e->draft.v[0] * 100, e->draft.v[1] * 100, e->draft.v[2] * 100);
  TEXT(line, x, top - 66 * scale, 16, TFP_ED_COLOR_TEXT);
  snprintf(line, sizeof line, "Yaw %+.1f  Pitch %+.1f  Roll %+.1f deg", e->draft.v[3], e->draft.v[4], e->draft.v[5]);
  TEXT(line, x, top - 94 * scale, 15, TFP_ED_COLOR_TEXT);
  look(e, &l);
  TEXT(l.rotation_false ? "Left drag: move   Wheel: depth   Shift: fine" : "L: move  R: yaw/pitch  M: roll  Wheel: depth  Shift: fine", x,
       top - 118 * scale, 15, TFP_ED_COLOR_TEXT);
  bool supported = true;
  if (l.has_action_supported) {
    TfpEdArgs a; TfpEdReply r; memset(&a, 0, sizeof a);
    a.text = action(e);
    plain(e, TFP_ED_S_ACTION_SUPPORTED, &a, &r);
    supported = is_true(&r.a);
  }
  TEXT(supported ? "Wheel: depth   Arrows: move" : "Preset only: automatic action mapping needs a live trace", x,
       top - 142 * scale, 14, TFP_ED_COLOR_TEXT);
  snprintf(line, sizeof line, "%.72s", e->ready ? e->label : "Release the activation click/key to begin");
  TEXT(line, x, top - 174 * scale, 13, TFP_ED_COLOR_TEXT_DIM);
  for (int i = 0; i < B_COUNT; i++) e->buttons[i].has = false;
  e->has_buttons = true;
  for (int i = 1; i <= 2; i++) {
    Rect r = {true, x + (i - 1) * 40 * scale, top - 36 * scale, 32 * scale, 26 * scale};
    e->buttons[i == 1 ? B_PREVIOUS : B_NEXT] = r;
    RECT(r, TFP_ED_COLOR_FIELD, false);
    TEXT(i == 1 ? "<" : ">", r.x + 10 * scale, r.y + 6 * scale, 15, TFP_ED_COLOR_TEXT);
  }
  look(e, &l);
  int count = 4 + (l.reset_all ? 1 : 0) - (l.single_save ? 1 : 0);
  double bw = count == 4 ? 112 * scale : (e->panel.w - 32 * scale - (count - 1) * 10 * scale) / count, gap = 10 * scale, by = e->panel.y + 12 * scale;
  int names[5], items = 0;
  const char *labels[5];
  names[items] = B_SAVE; labels[items++] = l.save_label ? l.save_label : "Save weapon";
  if (!l.single_save) { names[items] = B_SAVE_ALL; labels[items++] = l.all_label ? l.all_label : "Save all"; }
  names[items] = B_CANCEL; labels[items++] = "Cancel [Esc]";
  names[items] = B_RESET; labels[items++] = "Reset";
  if (l.reset_all) { names[items] = B_RESET_ALL; labels[items++] = "Reset all"; }
  for (int index = 1; index <= items; index++) {
    Rect bounds = {true, x + (index - 1) * (bw + gap), by, bw, 28 * scale};
    e->buttons[names[index - 1]] = bounds;
    RECT(bounds, TFP_ED_COLOR_FIELD, false);
    TEXT(labels[index - 1], bounds.x + 10 * scale, bounds.y + 7 * scale, 14, TFP_ED_COLOR_TEXT);
  }
  TEXT(l.footer ? l.footer : "Save weapon / all applies to the selected action only", e->panel.x, e->panel.y - 20 * scale, 13, TFP_ED_COLOR_ACCENT);
#undef RECT
#undef TEXT
  TfpEdArgs a; TfpEdReply r; memset(&a, 0, sizeof a);
  a.commands = commands; a.ncommands = n;
  if (e->view_draw) plain(e, TFP_ED_V_DRAW, &a, &r);
  else { memset(&r, 0, sizeof r); r.b = text("Missing draw"); }
  if (r.a.kind == TFP_ED_FALSE || r.b.kind != TFP_ED_NIL) { *why = truthy(&r.b) ? r.b : text("Editor drawing refused"); return false; }
  return true;
}

/* ---- public (:192-226) ---- */
TfpEditor *tfp_editor_new(const TfpEdHost *host) {
  TfpEditor *e = calloc(1, sizeof *e);
  TfpEdLook l;
  e->host = *host;
  e->action_index = 1;
  look(e, &l);
  take_actions(e, &l, true);
  snprintf(e->label, sizeof e->label, "Placement editor closed");
  return e;
}
void tfp_editor_free(TfpEditor *e) { free(e); }
bool tfp_editor_is_active(const TfpEditor *e) { return e->phase != IDLE; }
bool tfp_editor_is_clean(const TfpEditor *e) { return e->phase == IDLE; }
const char *tfp_editor_status(const TfpEditor *e) { return e->label; }
const char *tfp_editor_phase(const TfpEditor *e) { return PHASE[e->phase]; }
const char *tfp_editor_kind(const TfpEditor *e) { return e->has_kind ? e->kind : NULL; }
const char *tfp_editor_preview_action(const TfpEditor *e) { return action(e); }
bool tfp_editor_preview_rotation(const TfpEditor *e, double out[3]) {
  if (e->phase != ACTIVE || !e->has_draft) return false;
  for (int i = 0; i < 3; i++) out[i] = e->draft.v[3 + i];
  return true;
}
bool tfp_editor_holds_draft(const TfpEditor *e) {
  if (!e->has_draft || !e->has_original || e->committed) return false;
  TfpEdPlace o = copy(&e->original);
  for (int i = 0; i < 6; i++) if (e->draft.v[i] != o.v[i]) return true;
  return false;
}

TfpEdResult tfp_editor_request_open(TfpEditor *e, const char *kind) {   /* :197-218 */
  TfpEdLook l; TfpEdReply r;
  look(e, &l);
  if (e->phase == IDLE && l.has_select_mode) {
    TfpEdArgs a; memset(&a, 0, sizeof a);
    a.text = kind ? kind : "weapon";
    plain(e, TFP_ED_S_SELECT_MODE, &a, &r);
    if (!truthy(&r.a)) { TfpEdResult out; memset(&out, 0, sizeof out); out.r1 = boolean(false); out.r2 = r.b; return out; }
    look(e, &l);
    take_actions(e, &l, false);
    e->has_kind = true; snprintf(e->kind, sizeof e->kind, "%s", kind ? kind : "weapon");
  }
  if (e->phase != IDLE) return refuse("Placement editor or its cleanup is already active");
  look(e, &l);
  if (l.has_can_start) {
    plain(e, TFP_ED_S_CAN_START, NULL, &r);
    if (!is_true(&r.a)) {
      TfpEdResult out = refuse("Placement camera cleanup remains pending");
      if (truthy(&r.b)) out.r2 = r.b;
      return out;
    }
  }
  if (foreign_lut_active(e)) return refuse("Close the Epic LUT editor before positioning arms and weapon");
  if (!enabled(e) && !clean_controller(e)) return refuse("Camera cleanup is still pending");
  look(e, &l);
  e->api = l.provider;
  if (!l.provider || !l.provider_supported) { e->api = 0; return refuse("DBF-MCM captured placement input is unavailable"); }
  e->phase = QUEUED;
  e->restored = e->committed = false;
  e->ready = e->click_held = e->enter_held = e->escape_held = false; e->drag.has = false;
  e->action_index = 1; e->right_held = false; e->has_rotation_drag = false; e->middle_held = false; e->has_middle_drag = false;
  report(e, "Opening placement editor");
  return ok();
}
TfpEdResult tfp_editor_cancel(TfpEditor *e, const char *reason) { return close_editor(e, reason ? reason : "Placement cancelled", false); }
TfpEdResult tfp_editor_finish(TfpEditor *e, bool all_weapons) {   /* :220-225 */
  if (e->phase != ACTIVE) return refuse("Placement editor is not ready to save");
  TfpEdArgs a; TfpEdReply r; memset(&a, 0, sizeof a);
  char scratch[224], line[300];
  a.place = copy(&e->draft); a.key = e->weapon_key; a.owner = e->weapon_owner; a.flag = all_weapons; a.text = action(e);
  plain(e, TFP_ED_S_SAVE, &a, &r);
  if (!is_true(&r.a)) {
    snprintf(line, sizeof line, "Save failed: %s", tostr_or(&r.b, "Settings write refused", scratch));
    report(e, line);
    TfpEdResult out; memset(&out, 0, sizeof out); out.r1 = boolean(false); out.r2 = r.b;
    return out;
  }
  return close_editor(e, all_weapons ? "Placement saved for all weapons" : "Placement saved for this weapon", true);
}
TfpEdResult tfp_editor_shutdown(TfpEditor *e) { return close_editor(e, "Placement editor disabled", false); }

static TfpEdResult answer(bool value) { TfpEdResult r; memset(&r, 0, sizeof r); r.r1 = boolean(value); return r; }
static TfpEdResult raise(const char *what) { TfpEdResult r; memset(&r, 0, sizeof r); r.raised = true; r.r1 = text(what); return r; }
static bool down(TfpEditor *e, double key) {
  TfpEdArgs a; TfpEdReply r; memset(&a, 0, sizeof a);
  a.number = key;
  plain(e, TFP_ED_I_DOWN, &a, &r);
  return is_true(&r.a);
}

TfpEdResult tfp_editor_update(TfpEditor *e) {   /* :227-394 */
  TfpEdLook l; TfpEdReply r; TfpEdArgs a;
  char scratch[224];
  if (e->phase == IDLE) return ok();
  if (e->phase == CLOSING) return release_all(e);
  if (foreign_lut_active(e)) return close_editor(e, "Epic LUT owns the cursor; placement editing cancelled", false);
  look(e, &l);
  if (l.provider != e->api) return close_editor(e, "MCM provider changed; placement cancelled", false);
  if (!now(e)) return close_editor(e, "Editor clock unavailable; placement cancelled", false);
  if (e->phase == QUEUED) {
    plain(e, TFP_ED_A_CLOSE, NULL, &r);
    if (!is_true(&r.a)) return closef(e, "MCM input handoff refused: ", tostr(&r.b, scratch));
    e->phase = HANDOFF;
    return ok();
  }
  if (e->phase == HANDOFF) {
    TfpEdReply capture;
    plain(e, TFP_ED_A_IS_OPEN, NULL, &r);
    plain(e, TFP_ED_A_INPUT_STATUS, NULL, &capture);
    if (r.a.kind != TFP_ED_FALSE || status_busy(&capture)) return close_editor(e, "Close other menus before positioning the weapon", false);
    if (!gameplay_cursor_ready(e)) return close_editor(e, "Game cursor is owned elsewhere; placement editing cancelled", false);
    if (enabled(e)) invoke(e, TFP_ED_C_GET_PLACEMENT, NULL, &r); else plain(e, TFP_ED_S_GET, NULL, &r);
    if (!valid(r.a.kind == TFP_ED_TABLE, &r.place)) return closef(e, "Saved placement unavailable: ", tostr_or(&r.b, "Invalid XYZ", scratch));
    TfpEdPlace saved = r.place;
    e->original = copy(&saved); e->has_original = true;
    memset(&a, 0, sizeof a); a.place = copy(&saved);
    invoke(e, TFP_ED_C_SET_PLACEMENT, &a, &r);
    if (!is_true(&r.a)) return closef(e, "Placement preparation refused: ", tostr(&r.b, scratch));
    e->draft = copy(&saved); e->has_draft = true; e->placement_owned = true;
    if (!enabled(e)) {
      invoke(e, TFP_ED_C_START, NULL, &r);
      if (!is_true(&r.a)) return closef(e, "First-person entry refused: ", tostr(&r.b, scratch));
    }
    look(e, &l);
    if (l.has_target) {
      plain(e, TFP_ED_S_TARGET, NULL, &r);
      e->weapon_key = r.a; e->weapon_owner = r.b;
      if (!truthy(&e->weapon_key)) return close_editor(e, "Equip a firearm and wait for first person before editing", false);
      invoke(e, TFP_ED_C_GET_PLACEMENT, NULL, &r);
      if (!valid(r.a.kind == TFP_ED_TABLE, &r.place)) return close_editor(e, "Weapon placement unavailable", false);
      saved = r.place;
      e->original = copy(&saved);
      memset(&a, 0, sizeof a); a.key = e->weapon_key; a.text = action(e);
      plain(e, TFP_ED_S_GET_ACTION, &a, &r);
      e->draft = valid(r.a.kind == TFP_ED_TABLE, &r.place) ? copy(&r.place) : copy(&saved);
      memset(&a, 0, sizeof a); a.place = e->draft;
      invoke(e, TFP_ED_C_SET_PLACEMENT, &a, &r);
      if (!is_true(&r.a)) return closef(e, "Action preview refused: ", tostr(&r.b, scratch));
    }
    plain(e, TFP_ED_A_INPUT_STATUS, NULL, &capture);
    if (status_busy(&capture)) return close_editor(e, "Editor input became owned by another frontend", false);
    if (foreign_lut_active(e) || !gameplay_cursor_ready(e)) return close_editor(e, "Another frontend took the cursor; placement editing cancelled", false);
    look(e, &l);
    e->input = l.provider_input; e->input_owned = false;
    plain_input(e, TFP_ED_I_ACQUIRE, "acquire", NULL, &r);
    if (truthy(&r.a)) e->input_owned = true;
    else {
      TfpEdValue problem = r.b;
      plain(e, TFP_ED_A_INPUT_STATUS, NULL, &capture);
      e->input_owned = capture.a.kind == TFP_ED_TABLE && capture.status.owner.kind == TFP_ED_TEXT && !strcmp(capture.status.owner.s, "goose_first_person_editor");
      return closef(e, "Editor input refused: ", tostr(&problem, scratch));
    }
    memset(&a, 0, sizeof a); a.flag = true;
    plain_input(e, TFP_ED_I_SET_FOCUS, "set_focus", &a, &r);
    if (!is_true(&r.a)) return closef(e, "Editor cursor refused: ", tostr(&r.b, scratch));
    plain(e, TFP_ED_A_CREATE_VIEW, NULL, &r);
    bool table = r.a.kind == TFP_ED_TABLE;
    e->view = table; e->view_draw = table && r.view.has_draw; e->view_refresh = table && r.view.has_refresh_font;
    if (!table || !r.view.has_draw || !r.view.has_release) {
      if (!table || !r.view.has_release) e->view = false;
      return closef(e, "Editor overlay unavailable: ", tostr_or(&r.b, "Unsupported view", scratch));
    }
    if (e->view_refresh) plain(e, TFP_ED_V_REFRESH_FONT, NULL, &r);
    e->phase = ACTIVE;
    report(e, "Drag to place arms and weapon; save to keep changes");
    return ok();
  }
  look(e, &l);
  if (l.has_target) {
    plain(e, TFP_ED_S_TARGET, NULL, &r);
    if (!same(&r.a, &e->weapon_key) || !same(&r.b, &e->weapon_owner)) {
      e->placement_owned = false;
      return close_editor(e, "Weapon changed; unsaved placement cancelled", false);
    }
  }
  if (!controller_running(e)) return close_editor(e, "Native camera stopped; unsaved placement cancelled", false);
  look(e, &l);
  if (l.provider_input != e->input) return close_editor(e, "MCM input provider changed; unsaved placement cancelled", false);
  plain(e, TFP_ED_A_IS_OPEN, NULL, &r);
  if (r.a.kind != TFP_ED_FALSE) return close_editor(e, "MCM opened; unsaved placement cancelled", false);
  plain(e, TFP_ED_I_FOCUSED, NULL, &r);
  if (!is_true(&r.a)) return close_editor(e, "Game focus lost; unsaved placement cancelled", false);
  TfpEdReply sample;
  plain(e, TFP_ED_I_SAMPLE, NULL, &sample);
  if (sample.a.kind != TFP_ED_TABLE) return closef(e, "Editor input lost: ", tostr_or(&sample.b, "Ownership changed", scratch));
  double sx = sample.sample.x, sy = sample.sample.y;
  memset(&a, 0, sizeof a); memset(&r, 0, sizeof r);
  bool resolved = e->host.call(e->host.user, TFP_ED_A_RESOLUTION, &a, &r);
  double w = NAN, h = NAN;
  if (resolved) { w = r.a.kind == TFP_ED_NUMBER ? r.a.n : NAN; h = r.b.kind == TFP_ED_NUMBER ? r.b.n : NAN; }
  if (!finite_number(w) || !finite_number(h) || w < 1 || h < 1) return close_editor(e, "Editor resolution unavailable", false);
  look(e, &l);
  bool right = !l.rotation_false && down(e, 2);
  look(e, &l);
  bool middle = !l.rotation_false && down(e, 4);
  bool click = sample.sample.click, enter = down(e, 13), escape = down(e, 27);
  if (!e->ready) {
    e->ready = !click && !right && !middle && !enter && !escape;
    e->right_held = right; e->middle_held = middle;
    e->click_held = click; e->enter_held = enter; e->escape_held = escape;
  } else {
    bool click_edge = click && !e->click_held, enter_edge = enter && !e->enter_held, escape_edge = escape && !e->escape_held;
    e->click_held = click; e->enter_held = enter; e->escape_held = escape;
    if (escape_edge) return close_editor(e, "Placement cancelled", false);
    if (enter_edge) { tfp_editor_finish(e, false); return answer(e->phase != CLOSING); }
    TfpEdPlace next = copy(&e->draft);
    if (click_edge) {
      int hit = button_at(e, sx, sy);
      if (hit == B_SAVE) { tfp_editor_finish(e, false); return answer(e->phase != CLOSING); }
      else if (hit == B_SAVE_ALL) { tfp_editor_finish(e, true); return answer(e->phase != CLOSING); }
      else if (hit == B_CANCEL) return close_editor(e, "Placement cancelled", false);
      else if (hit == B_NEXT || hit == B_PREVIOUS) {
        int index = e->action_index - 1 + (hit == B_NEXT ? 1 : -1);
        e->action_index = ((index % e->nactions) + e->nactions) % e->nactions + 1;
        memset(&a, 0, sizeof a); a.key = e->weapon_key; a.text = action(e);
        plain(e, TFP_ED_S_GET_ACTION, &a, &r);
        next = valid(r.a.kind == TFP_ED_TABLE, &r.place) ? copy(&r.place) : baseline_place();
        char line[200];
        snprintf(line, sizeof line, "Editing %s; switching actions discards unsaved edits", e->action_label[e->action_index - 1]);
        report(e, line);
        e->drag.has = false; e->has_rotation_drag = false;
      } else if (hit == B_RESET_ALL) {
        plain(e, TFP_ED_S_RESET_ALL, NULL, &r);
        if (!truthy(&r.a)) { report(e, tostr_or(&r.b, "Reset all refused", scratch)); return ok(); }
        memset(&a, 0, sizeof a); a.key = e->weapon_key; a.text = action(e);
        plain(e, TFP_ED_S_GET_ACTION, &a, &r);
        if (!valid(r.a.kind == TFP_ED_TABLE, &r.place)) return close_editor(e, "Reset value unavailable", false);
        e->original = copy(&r.place); e->has_original = true; next = copy(&r.place);
        report(e, "All saved placements in this editor reset");
      } else if (hit == B_RESET) {
        bool table = false;
        TfpEdPlace baseline; memset(&baseline, 0, sizeof baseline);
        look(e, &l);
        if (l.baseline == 2) {
          memset(&a, 0, sizeof a); a.key = e->weapon_key; a.text = action(e);
          plain(e, TFP_ED_S_BASELINE, &a, &r);
          table = r.a.kind == TFP_ED_TABLE; baseline = r.place;
        } else if (l.baseline == 1) { table = true; baseline = l.baseline_place; }
        if (valid(table, &baseline)) next = copy(&baseline);
        else if (!strcmp(action(e), "ads")) { memset(&next, 0, sizeof next); next.has[0] = next.has[1] = next.has[2] = true; }
        else next = baseline_place();
        report(e, "Default placement previewed; save to keep it");
      } else if (!e->panel.has || !inside(sx, sy, &e->panel)) {
        if (finite_number(sx) && finite_number(sy)) e->drag = (Rect){true, sx, sy, 0, 0};
      }
    }
    if (right && !middle && !e->right_held && !click && finite_number(sx) && finite_number(sy) && (!e->panel.has || !inside(sx, sy, &e->panel))) {
      e->has_rotation_drag = true; e->rotation_x = sx; e->rotation_y = sy; e->drag.has = false;
    }
    e->right_held = right;
    if (middle && !e->middle_held && !right && !click && finite_number(sx) && finite_number(sy) && (!e->panel.has || !inside(sx, sy, &e->panel))) {
      e->has_middle_drag = true; e->middle_x = sx; e->drag.has = false; e->has_rotation_drag = false;
    }
    e->middle_held = middle;
    double fine = down(e, 16) ? .1 : 1;
    if (middle && !right && !click && e->has_middle_drag && finite_number(sx)) {
      if (!next.has[5]) return raise("src/first_person/placement_editor.lua:368: attempt to perform arithmetic on field 'roll' (a nil value)");
      next.v[5] = fmax(-90, fmin(90, next.v[5] + (sx - e->middle_x) / w * 180 * fine));
      e->middle_x = sx;
    } else if (!middle) e->has_middle_drag = false;
    if (right && !middle && e->has_rotation_drag && finite_number(sx) && finite_number(sy)) {
      if (!next.has[3]) return raise("src/first_person/placement_editor.lua:372: attempt to perform arithmetic on field 'yaw' (a nil value)");
      next.v[3] = fmax(-90, fmin(90, next.v[3] - (sx - e->rotation_x) / w * 120 * fine));
      next.v[4] = fmax(-90, fmin(90, next.v[4] - (sy - e->rotation_y) / h * 90 * fine));
      e->rotation_x = sx; e->rotation_y = sy;
    } else if (!right) e->has_rotation_drag = false;
    if (!right && click && e->drag.has && finite_number(sx) && finite_number(sy)) {
      next.v[0] = next.v[0] + (sx - e->drag.x) / w * fine;
      next.v[2] = next.v[2] + (sy - e->drag.y) / h * .6 * fine;
      e->drag.x = sx; e->drag.y = sy;
    } else if (!click) e->drag.has = false;
    if (finite_number(sample.sample.wheel)) next.v[1] = next.v[1] + sample.sample.wheel / 120 * .01 * fine;
    double dt = finite_number(sample.sample.dt) ? fmax(0, fmin(.1, sample.sample.dt)) : 0;
    double step = .15 * dt * fine;
    /* Retail (:384) also bound Page Up (33) and Page Down (34) to depth. Not here (round 17): Page Up is the
     * editor's own open/close key and Page Down opens the overlay host's panel list; depth is the wheel's. */
    static const struct { double key; int axis; double sign; } BINDINGS[] = {{37, 0, -1}, {39, 0, 1}, {38, 2, 1}, {40, 2, -1}};
    for (size_t i = 0; i < sizeof BINDINGS / sizeof BINDINGS[0]; i++)
      if (down(e, BINDINGS[i].key)) next.v[BINDINGS[i].axis] = next.v[BINDINGS[i].axis] + step * BINDINGS[i].sign;
    for (int i = 0; i < 3; i++) next.v[i] = clamp(next.v[i]);
    TfpEdValue problem = nil();
    if (!apply(e, &next, &problem)) return closef(e, "Placement update failed: ", tostr(&problem, scratch));
  }
  TfpEdValue why = nil();
  if (!draw(e, w, h, &why)) return closef(e, "Editor overlay failed: ", tostr(&why, scratch));
  return ok();
}
