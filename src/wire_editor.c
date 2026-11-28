/* The placement editor's binder: see wire_editor.h. */
#include "wire_editor.h"
#include "wire_settings.h"
#include "editor_motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef TfpRtValue V;
typedef struct Ed {
  TfpWire *w;
  TfpEditorPlatform p;
  TfpEditor *editor;
  TfpOpsPlacement argument;   /* the placement of the call in progress: handed to the runtime by this identity */
  bool (*previous_decode)(TfpWire *w, V value, TfpOpsPlacement *out);
  char title[64], save_label[64], all_label[64], footer[96], actions[256], why[224];
  /* round 53: the list of saved placements (src/editor_weapons.h) */
  char held[TFP_EW_KEY];      /* store.target()'s last key; "": none, or not text */
  bool listed;                /* the platform has the list of this opening */
  TfpEwWeapon list[TFP_EW_MAX];
} Ed;
static Ed *state(const TfpWire *w) { return w ? w->ext[TFP_WIRE_EXT_EDITOR] : NULL; }
#define W(h) ((TfpWire *)(h)->user)

static V nil(void) { V v; memset(&v, 0, sizeof v); return v; }
static V boolean(bool b) { V v = nil(); v.kind = b ? TFP_RT_TRUE : TFP_RT_FALSE; return v; }
static V number(double n) { V v = nil(); v.kind = TFP_RT_NUMBER; v.number = n; return v; }
static V text_n(const char *s) { V v = nil(); v.kind = TFP_RT_TEXT; v.text = s; v.length = strlen(s); return v; }
static V reference(const void *p) { V v = nil(); v.kind = TFP_RT_REF; v.ref = p; return v; }

static void ed_text(TfpEdValue *out, const char *s, size_t length) {
  memset(out, 0, sizeof *out);
  out->kind = TFP_ED_TEXT;
  if (length >= sizeof out->s) length = sizeof out->s - 1;
  memcpy(out->s, s, length);
}
static TfpEdValue from_rt(V v) {
  TfpEdValue out; memset(&out, 0, sizeof out);
  switch (v.kind) {
    case TFP_RT_FALSE: out.kind = TFP_ED_FALSE; break;
    case TFP_RT_TRUE: out.kind = TFP_ED_TRUE; break;
    case TFP_RT_NUMBER: out.kind = TFP_ED_NUMBER; out.n = v.number; break;
    case TFP_RT_TEXT: ed_text(&out, v.text, v.length); break;
    case TFP_RT_REF: out.kind = TFP_ED_TABLE; break;
    default: break;
  }
  return out;
}
static V to_rt(const TfpEdValue *v) {
  switch (v->kind) {
    case TFP_ED_FALSE: return boolean(false);
    case TFP_ED_TRUE: return boolean(true);
    case TFP_ED_NUMBER: return number(v->n);
    case TFP_ED_TEXT: return text_n(v->s);
    default: return nil();
  }
}
static void place_from_ops(const TfpOpsPlacement *o, TfpEdPlace *p) {
  const double v[6] = {o->x, o->y, o->z, o->yaw, o->pitch, o->roll};
  memset(p, 0, sizeof *p);
  for (int i = 0; i < 6; i++) if (o->present & (1u << i)) { p->has[i] = true; p->v[i] = v[i]; }
}
static void ops_from_place(const TfpEdPlace *p, TfpOpsPlacement *o) {
  double *v[6] = {&o->x, &o->y, &o->z, &o->yaw, &o->pitch, &o->roll};
  memset(o, 0, sizeof *o);
  for (int i = 0; i < 6; i++) if (p->has[i]) { *v[i] = p->v[i]; o->present |= 1u << i; }
}
/* A placement table the runtime answered: decoded when it is one this binder can read; else a table
 * with no fields (the editor finds it invalid, as retail finds a table without x, y, z). */
static void place_of(Ed *st, V value, TfpEdReply *r) {
  TfpOpsPlacement ops;
  r->a = from_rt(value);
  memset(&r->place, 0, sizeof r->place);
  if (value.kind != TFP_RT_REF) return;
  if (value.ref == &st->w->ops.pose_placement ? tfp_wire_controller_placement(st->w, &ops) : tfp_wire_placement_of(st->w, value, &ops))
    place_from_ops(&ops, &r->place);
}
/* w->placement_decode: the editor's own argument, then whoever was there before (the settings store). */
static bool decode(TfpWire *w, V value, TfpOpsPlacement *out) {
  Ed *st = state(w);
  if (!st) return false;
  if (value.kind == TFP_RT_REF && value.ref == &st->argument) { *out = st->argument; return true; }
  return st->previous_decode && st->previous_decode(w, value, out);
}

/* ---- round 53: the list of saved placements ---- */
static void list_add(TfpWire *w, const char *key, bool saved, TfpEwWeapon *out, int *count, int capacity) {
  const TfpActionCatalogue *catalogue = tfp_wire_settings_catalogue(w);
  const TfpActionWeapon *info = catalogue ? tfp_action_weapon_info(catalogue, key) : NULL;
  const char *name = info && info->name ? info->name : !strcmp(key, "__all") ? "All weapons (shared)" : key;
  tfp_ew_add(out, count, capacity, key, name, tfp_ew_group_of_kind(info ? info->kind : 0), saved);
}
int tfp_wire_editor_weapon_list(TfpWire *w, const char *held, TfpEwWeapon *out, int capacity) {
  TfpSettings *store = tfp_wire_settings(w);
  int count = 0;
  if (!store) return 0;
  const TfpSettingsValue *profiles = store->field[TFP_SET_action_placements], *older = store->field[TFP_SET_weapon_placements];
  for (size_t i = 0; profiles && profiles->kind == TFP_SETTINGS_PROFILES && i < profiles->count; i++)
    if (profiles->profiles[i].has) list_add(w, profiles->ids[i], true, out, &count, capacity);
  for (size_t i = 0; older && older->kind == TFP_SETTINGS_PLACEMENTS && i < older->count; i++) list_add(w, older->ids[i], true, out, &count, capacity);
  if (held && *held) list_add(w, held, false, out, &count, capacity);   /* already there: stays as saved */
  tfp_ew_sort(out, count, held);
  return count;
}
/* The editor draws: the platform gets the list once an opening, and again after "Reset all" emptied it. */
static void list_show(Ed *st) {
  if (st->listed || !st->p.weapons) return;
  st->listed = true;
  st->p.weapons(st->p.user, st->list, tfp_wire_editor_weapon_list(st->w, st->held, st->list, TFP_EW_MAX));
}
static void list_hide(Ed *st) {
  st->listed = false;
  if (st->p.weapons) st->p.weapons(st->p.user, NULL, 0);
}

/* One of the runtime's own functions. false: it raised (reply->a is the text) or does not exist. */
static bool entry(Ed *st, TfpRtEntry which, const TfpRtArgs *args, V *r, int count, TfpEdReply *reply) {
  TfpRtArgs none; memset(&none, 0, sizeof none);
  for (int i = 0; i < count; i++) r[i] = nil();
  TfpRtStatus s = tfp_rt_call(&st->w->rt, which, args ? args : &none, r);
  if (s == TFP_RT_OK) return true;
  if (s == TFP_RT_RAISED && st->w->rt.error.kind == TFP_RT_TEXT) ed_text(&reply->a, st->w->rt.error.text, st->w->rt.error.length);
  else { char why[96]; snprintf(why, sizeof why, s == TFP_RT_RAISED ? "error" : "Missing %s", tfp_rt_entry_name(which)); ed_text(&reply->a, why, strlen(why)); }
  return false;
}
#define PAIR(e, argp) do { V r[8]; if (!entry(st, e, argp, r, 8, reply)) return false; reply->a = from_rt(r[0]); reply->b = from_rt(r[1]); return true; } while (0)

static bool host_call(void *user, TfpEdCall which, const TfpEdArgs *a, TfpEdReply *reply) {
  Ed *st = user;
  TfpWire *w = st->w;
  TfpRtArgs args;
  V r[8];
  memset(&args, 0, sizeof args);
  memset(reply, 0, sizeof *reply);
  switch (which) {
    case TFP_ED_CLOCK: { double now = tfp_wire_clock(w); if (now == now) { reply->a.kind = TFP_ED_NUMBER; reply->a.n = now; } return true; }
    case TFP_ED_C_IS_CLEAN: PAIR(TFP_RT_E_pc_is_clean, NULL);
    case TFP_ED_C_IS_ENABLED: PAIR(TFP_RT_E_pc_is_enabled, NULL);
    case TFP_ED_C_RESULT:
      if (!entry(st, TFP_RT_E_pc_result, NULL, r, 8, reply)) return false;
      reply->a = from_rt(r[0]);
      if (r[0].kind == TFP_RT_REF) snprintf(reply->a.s, sizeof reply->a.s, "%s", tfp_wire_phase_text(w->controller.phase));
      return true;
    case TFP_ED_C_SET_PLACEMENT: {
      /* round 20: while the ADS action is edited the hip placement keeps the one the editor opened with; the draft goes
       * to the ADS placement through the motion runtime (src/editor_motion.h) */
      const TfpEdPlace value = tfp_editor_motion_controller_value(st->editor, &a->place);
      ops_from_place(&value, &st->argument);
      args.v[0] = reference(&st->argument);
      PAIR(TFP_RT_E_pc_set_placement, &args);
    }
    case TFP_ED_C_GET_PLACEMENT:
      if (!entry(st, TFP_RT_E_pc_get_placement, NULL, r, 8, reply)) return false;
      place_of(st, r[0], reply); reply->b = from_rt(r[1]);
      return true;
    case TFP_ED_C_START:
      args.v[0] = number(0); args.v[1] = text_n("pose_offset");
      PAIR(TFP_RT_E_pc_start, &args);
    case TFP_ED_S_SELECT_MODE: args.v[0] = text_n(a->text); PAIR(TFP_RT_E_store_select_mode, &args);
    case TFP_ED_S_CAN_START: PAIR(TFP_RT_E_store_can_start, NULL);
    case TFP_ED_S_TARGET:
      if (!entry(st, TFP_RT_E_store_target, NULL, r, 8, reply)) return false;
      reply->a = from_rt(r[0]); reply->b = from_rt(r[1]);
      snprintf(st->held, sizeof st->held, "%.*s", TFP_EW_KEY - 1, reply->a.kind == TFP_ED_TEXT ? reply->a.s : "");   /* round 53: the list marks it */
      return true;
    case TFP_ED_S_GET:
      if (!entry(st, TFP_RT_E_store_get, NULL, r, 8, reply)) return false;
      place_of(st, r[0], reply); reply->b = from_rt(r[1]);
      return true;
    case TFP_ED_S_GET_ACTION:
      args.v[0] = to_rt(&a->key); args.v[1] = text_n(a->text);
      if (!entry(st, TFP_RT_E_store_get_action, &args, r, 8, reply)) return false;
      place_of(st, r[0], reply);
      return true;
    case TFP_ED_S_SAVE:
      ops_from_place(&a->place, &st->argument);
      args.v[0] = reference(&st->argument); args.v[1] = to_rt(&a->key); args.v[2] = to_rt(&a->owner); args.v[3] = boolean(a->flag);
      args.v[4] = text_n(a->text);
      PAIR(TFP_RT_E_store_save, &args);
    case TFP_ED_S_RESET_ALL:
      st->listed = false;   /* round 53: the next drawn frame lists what is left */
      PAIR(TFP_RT_E_store_reset_all, NULL);
    case TFP_ED_S_BASELINE:
      args.v[0] = to_rt(&a->key); args.v[1] = text_n(a->text);
      if (!entry(st, TFP_RT_E_store_baseline, &args, r, 8, reply)) return false;
      place_of(st, r[0], reply);
      return true;
    case TFP_ED_S_ACTION_SUPPORTED: args.v[0] = text_n(a->text); PAIR(TFP_RT_E_store_action_supported, &args);
    case TFP_ED_W_MOUSE_FOCUS: case TFP_ED_W_SHOW_CURSOR: {
      bool focus = false, cursor = true;
      if (!st->p.window || !st->p.window(st->p.user, &focus, &cursor)) { ed_text(&reply->a, "Window state unavailable", 24); return false; }
      reply->a.kind = (which == TFP_ED_W_MOUSE_FOCUS ? focus : cursor) ? TFP_ED_TRUE : TFP_ED_FALSE;
      return true;
    }
    case TFP_ED_A_CLOSE:
      reply->a.kind = st->p.menu_close(st->p.user) ? TFP_ED_TRUE : TFP_ED_FALSE;
      if (reply->a.kind == TFP_ED_FALSE) ed_text(&reply->b, "the settings menu did not close", 31);
      return true;
    case TFP_ED_A_IS_OPEN: reply->a.kind = st->p.menu_is_open(st->p.user) ? TFP_ED_TRUE : TFP_ED_FALSE; return true;
    case TFP_ED_A_INPUT_STATUS:
      reply->a.kind = TFP_ED_TABLE;
      if (st->p.capture_busy(st->p.user)) ed_text(&reply->status.owner, "another frontend", 16);
      return true;
    case TFP_ED_A_RESOLUTION: {
      double width = 0, height = 0;
      if (st->p.resolution(st->p.user, &width, &height)) { reply->a.kind = reply->b.kind = TFP_ED_NUMBER; reply->a.n = width; reply->b.n = height; }
      return true;
    }
    case TFP_ED_A_CREATE_VIEW: reply->a.kind = TFP_ED_TABLE; reply->view.has_draw = reply->view.has_release = true; return true;
    case TFP_ED_I_ACQUIRE:
      st->listed = false;   /* round 53: a new opening lists anew */
      if (st->p.acquire(st->p.user)) ed_text(&reply->a, "goose_first_person_editor", 25);
      else ed_text(&reply->b, "the overlay host did not open the editor's panel", 48);
      return true;
    case TFP_ED_I_RELEASE:
      reply->a.kind = st->p.release(st->p.user) ? TFP_ED_TRUE : TFP_ED_FALSE;
      if (reply->a.kind == TFP_ED_FALSE) ed_text(&reply->b, "the overlay host did not close the editor's panel", 49);
      return true;
    case TFP_ED_I_SET_FOCUS: reply->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_I_SAMPLE: {
      TfpEditorSample s;
      if (!st->p.sample(st->p.user, &s)) { ed_text(&reply->b, "the overlay host gave no input", 30); return true; }
      reply->a.kind = TFP_ED_TABLE;
      reply->sample.click = s.click; reply->sample.x = s.x; reply->sample.y = s.y; reply->sample.wheel = s.wheel; reply->sample.dt = s.dt;
      return true;
    }
    case TFP_ED_I_DOWN: reply->a.kind = st->p.down(st->p.user, (int)a->number) ? TFP_ED_TRUE : TFP_ED_FALSE; return true;
    case TFP_ED_I_FOCUSED: reply->a.kind = st->p.focused(st->p.user) ? TFP_ED_TRUE : TFP_ED_FALSE; return true;
    case TFP_ED_V_DRAW: list_show(st); st->p.draw(st->p.user, a->commands, a->ncommands); return true;
    case TFP_ED_V_RELEASE: list_hide(st); st->p.draw(st->p.user, NULL, 0); return true;   /* nothing left to draw */
    default: return true;   /* LUT_STATUS and V_REFRESH_FONT are never asked: look() offers neither */
  }
}
static const char *kept(char *buffer, size_t size, V v) {
  if (v.kind != TFP_RT_TEXT) return NULL;
  snprintf(buffer, size, "%.*s", (int)v.length, v.text);
  return buffer;
}
static void host_look(void *user, TfpEdLook *l) {
  Ed *st = user;
  V r[8];
  TfpEdReply unused;
  memset(&unused, 0, sizeof unused);
  l->has_select_mode = l->has_can_start = l->has_target = l->has_action_supported = l->reset_all = true;
  l->nactions = -1;
  if (entry(st, TFP_RT_E_store_fields, NULL, r, 8, &unused)) {
    if (r[0].kind == TFP_RT_REF && r[0].ref == tfp_wire_settings_actions) {
      l->nactions = (int)TFP_ACT_COUNT < (int)TFP_ED_ACTIONS ? (int)TFP_ACT_COUNT : (int)TFP_ED_ACTIONS;
      for (int i = 0; i < l->nactions; i++) { l->action_id[i] = tfp_wire_settings_actions[i][0]; l->action_label[i] = tfp_wire_settings_actions[i][1]; }
    } else if (kept(st->actions, sizeof st->actions, r[0])) {   /* "id/Label/1": the camera's one action */
      char *slash = strchr(st->actions, '/');
      if (slash) {
        *slash = 0;
        char *end = strchr(slash + 1, '/');
        if (end) *end = 0;
        l->nactions = 1; l->action_id[0] = st->actions; l->action_label[0] = slash + 1;
      }
    }
    l->title = kept(st->title, sizeof st->title, r[1]);
    l->rotation_false = r[2].kind == TFP_RT_FALSE;
    if (r[3].kind == TFP_RT_TEXT && r[3].length == 8) l->baseline = 2;
    else if (r[3].kind == TFP_RT_TEXT) {
      l->baseline = 1;
      for (int i = 0; i < 3; i++) { l->baseline_place.has[i] = true; l->baseline_place.v[i] = tfp_rt_camera_baseline[i]; }
    }
    l->single_save = r[4].kind == TFP_RT_TRUE;
    l->save_label = kept(st->save_label, sizeof st->save_label, r[5]);
    l->all_label = kept(st->all_label, sizeof st->all_label, r[6]);
    l->footer = kept(st->footer, sizeof st->footer, r[7]);
  }
  /* stingray.Window has both functions in the game; without a way to ask them, retail's check is skipped as when Window is absent. */
  l->window = l->window_mouse_focus = l->window_show_cursor = st->p.window != NULL;
  if (st->p.available(st->p.user)) { l->provider = l->provider_input = 1; l->provider_supported = true; }
}
static void host_log(void *user, const char *line) { tfp_wire_say(((Ed *)user)->w, line); }

/* ---- the runtime's editor_* host pointers ---- */
static V value_out(TfpWire *w, const TfpEdValue *v) {
  if (v->kind == TFP_ED_TEXT) return text_n(tfp_wire_keep(w, v->s));
  return to_rt(v);
}
static bool answer(TfpRtHost *h, TfpEdResult r, V *o1, V *o2) {
  if (r.raised) return tfp_wire_raise(h, r.r1.s);
  if (o1) *o1 = value_out(W(h), &r.r1);
  if (o2) *o2 = value_out(W(h), &r.r2);
  return true;
}
static bool rt_new(TfpRtHost *h) {
  Ed *st = state(W(h));
  TfpEdHost host = {st, host_call, host_look, host_log};
  tfp_editor_free(st->editor);
  st->editor = tfp_editor_new(&host);
  return st->editor != NULL || tfp_wire_raise(h, "not enough memory");
}
static bool rt_update(TfpRtHost *h) { return answer(h, tfp_editor_update(state(W(h))->editor), NULL, NULL); }
static bool rt_is_active(TfpRtHost *h, V *o1) { *o1 = boolean(tfp_editor_is_active(state(W(h))->editor)); return true; }
static bool rt_cancel(TfpRtHost *h, const char *reason, V *o1, V *o2) { return answer(h, tfp_editor_cancel(state(W(h))->editor, reason), o1, o2); }
static bool rt_shutdown(TfpRtHost *h, V *o1, V *o2) { return answer(h, tfp_editor_shutdown(state(W(h))->editor), o1, o2); }
/* Round 20: opened in full aim, the editor begins on its ADS action (src/editor_motion.h), and says so. */
static TfpEdResult open_editor(Ed *st, const char *kind) {
  TfpOpsSnapshot s;
  TfpOpsText why;
  const bool aiming = tfp_editor_motion_full_aim(tfp_ops_player_snapshot(&st->w->ops, &s, &why), &s);
  bool on_ads = false;
  const TfpEdResult r = tfp_editor_motion_open(st->editor, kind, aiming, &on_ads);
  if (on_ads)
    tfp_wire_say(st->w, "Placement editor: opened in full aim: editing the ADS placement (what full aim draws), live; the wheel moves the sight toward you or away");
  return r;
}
static bool rt_request_open(TfpRtHost *h, V *o1, V *o2) { return answer(h, open_editor(state(W(h)), NULL), o1, o2); }
static bool rt_status(TfpRtHost *h, V *o1) { *o1 = text_n(tfp_wire_keep(W(h), tfp_editor_status(state(W(h))->editor))); return true; }

bool tfp_wire_editor_bind(TfpWire *w, const TfpEditorPlatform *platform) {
  Ed *st = calloc(1, sizeof *st);
  if (!st) return false;
  st->w = w; st->p = *platform;
  w->ext[TFP_WIRE_EXT_EDITOR] = st;
  st->previous_decode = w->placement_decode;
  w->placement_decode = decode;
  TfpRtHost *h = &w->host;
  h->editor_new = rt_new; h->editor_update = rt_update; h->editor_is_active = rt_is_active; h->editor_cancel = rt_cancel;
  h->editor_shutdown = rt_shutdown; h->editor_request_open = rt_request_open; h->editor_status = rt_status;
  return true;
}
void tfp_wire_editor_unbind(TfpWire *w) {
  Ed *st = state(w);
  if (!st) return;
  if (w->placement_decode == decode) w->placement_decode = st->previous_decode;
  tfp_editor_free(st->editor);
  free(st);
  w->ext[TFP_WIRE_EXT_EDITOR] = NULL;
}
bool tfp_wire_editor_active(const TfpWire *w) { Ed *st = state(w); return st && st->editor && tfp_editor_is_active(st->editor); }
bool tfp_wire_editor_holds_draft(const TfpWire *w) { Ed *st = state(w); return st && st->editor && tfp_editor_holds_draft(st->editor); }
const TfpEditor *tfp_wire_editor_of(const TfpWire *w) { Ed *st = state(w); return st ? st->editor : NULL; }
const char *tfp_wire_editor_status(const TfpWire *w) { Ed *st = state(w); return st && st->editor ? tfp_editor_status(st->editor) : "Placement editor is unavailable"; }
const char *tfp_wire_editor_toggle(TfpWire *w) {
  Ed *st = state(w);
  if (!st || !st->editor) return "Placement editor is unavailable until first person has been set up";
  TfpEdResult r = tfp_editor_is_active(st->editor) ? tfp_editor_cancel(st->editor, "Placement editing cancelled; first person stays on")
                                                   : open_editor(st, "weapon");
  if (!r.raised && r.r1.kind == TFP_ED_TRUE) return NULL;
  const TfpEdValue *why = r.raised ? &r.r1 : &r.r2;
  snprintf(st->why, sizeof st->why, "%s", why->kind == TFP_ED_TEXT ? why->s : "Placement editor refused");
  return st->why;
}
