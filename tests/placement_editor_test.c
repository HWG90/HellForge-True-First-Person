/* src/placement_editor.c replayed against the retail placement_editor.lua's own answers
 * (tests/vectors/placement_editor_vectors.h, cut by tools/retail_ref/gen_placement_editor.py). The fakes
 * here answer from the same scripted world as tools/retail_ref/run_placement_editor.lua's. */
#include "effects_fx.h"
#include "placement_editor.h"
#include "vectors/placement_editor_vectors.h"

static TfpEditor *editor;

static TfpEdValue value_of(FxValue v) {
  TfpEdValue out; memset(&out, 0, sizeof out);
  switch (v.t) {
    case 'f': out.kind = TFP_ED_FALSE; break;
    case 't': out.kind = TFP_ED_TRUE; break;
    case 'd': out.kind = TFP_ED_NUMBER; out.n = v.n; break;
    case 's': out.kind = TFP_ED_TEXT; snprintf(out.s, sizeof out.s, "%.*s", v.len, v.s); break;
    default: break;
  }
  return out;
}
static const char *show_value(const TfpEdValue *v) {
  switch (v->kind) {
    case TFP_ED_NIL: return "~";
    case TFP_ED_FALSE: return "F";
    case TFP_ED_TRUE: return "T";
    case TFP_ED_NUMBER: return fx_num(v->n);
    case TFP_ED_TEXT: return fx_str(v->s);
    default: return "<table>";
  }
}
static bool has(int slot, char flag) { return fx_world[slot].t == 's' && memchr(fx_world[slot].s, flag, (size_t)fx_world[slot].len) != NULL; }
static const char *slot_text(int slot) {   /* a NUL-terminated copy of a text slot, or NULL */
  static char copies[8][160];
  static unsigned next;
  if (fx_world[slot].t != 's') return NULL;
  char *out = copies[next++ % 8];
  snprintf(out, sizeof copies[0], "%.*s", fx_world[slot].len, fx_world[slot].s);
  return out;
}
/* place(slot): true -> a table from <slot>_f, else that value. */
static void place(int slot, int fields, TfpEdValue *a, TfpEdPlace *p) {
  memset(p, 0, sizeof *p);
  if (fx_world[slot].t != 't') { *a = value_of(fx_world[slot]); return; }
  memset(a, 0, sizeof *a); a->kind = TFP_ED_TABLE;
  for (int i = 0; i < 6; i++) {
    FxValue v = fx_getk(fields, i + 1);
    if (v.t == 'd') { p->has[i] = true; p->v[i] = v.n; }
  }
}
static const char *place_text(const TfpEdPlace *p) {
  static char texts[2][256];
  static unsigned next;
  size_t size = sizeof texts[0], used = 0;
  char *out = texts[next++ % 2];
  for (int i = 0; i < 6; i++) used += (size_t)snprintf(out + used, size - used, "%s%s", i ? "," : "", p->has[i] ? fx_num(p->v[i]) : "~");
  return out;
}
static void status(int slot, TfpEdReply *r) {
  if (fx_world[slot].t != 's') { r->a = value_of(fx_world[slot]); return; }
  r->a.kind = TFP_ED_TABLE;
  if (has(slot, 'g')) { r->status.owner.kind = TFP_ED_TEXT; snprintf(r->status.owner.s, sizeof r->status.owner.s, "goose_first_person_editor"); }
  else if (has(slot, 'o')) { r->status.owner.kind = TFP_ED_TEXT; snprintf(r->status.owner.s, sizeof r->status.owner.s, "someone"); }
  r->status.active = has(slot, 'a'); r->status.pending_restore = has(slot, 'p');
}
static double sample_number(int key) { FxValue v = fx_getk(W_sample_f, key); return v.t == 'd' && isfinite(v.n) ? v.n : NAN; }

static const int RAISE[TFP_ED_CALLS] = {W_x_clock, W_x_c_is_clean, W_x_c_is_enabled, W_x_c_result, W_x_c_set_placement, W_x_c_get_placement,
  W_x_c_start, W_x_s_select_mode, W_x_s_can_start, W_x_s_target, W_x_s_get, W_x_s_get_action, W_x_s_save, W_x_s_reset_all, W_x_s_baseline,
  W_x_s_action_supported, W_x_lut_status, W_x_w_mouse_focus, W_x_w_show_cursor, W_x_a_close, W_x_a_is_open, W_x_a_input_status,
  W_x_a_resolution, W_x_a_create_view, W_x_i_acquire, W_x_i_release, W_x_i_set_focus, W_x_i_sample, W_x_i_down, W_x_i_focused, W_x_v_draw,
  W_x_v_release, W_x_v_refresh_font};
static const char *const NAME[TFP_ED_CALLS] = {"clock", "c_is_clean", "c_is_enabled", "c_result", "c_set_placement", "c_get_placement", "c_start",
  "s_select_mode", "s_can_start", "s_target", "s_get", "s_get_action", "s_save", "s_reset_all", "s_baseline", "s_action_supported", "lut_status",
  "w_mouse_focus", "w_show_cursor", "a_close", "a_is_open", "a_input_status", "a_resolution", "a_create_view", "i_acquire", "i_release",
  "i_set_focus", "i_sample", "i_down", "i_focused", "v_draw", "v_release", "v_refresh_font"};
static const char *const COLOR[] = {"panel", "field", "text", "accent", "text_dim"};

static char draw_text[1 << 16];
static const char *arguments(TfpEdCall which, const TfpEdArgs *a) {
  size_t size;
  char *out = fx_scratch(&size);
  out[0] = 0;
  switch (which) {
    case TFP_ED_C_SET_PLACEMENT: return place_text(&a->place);
    case TFP_ED_C_START: return "0,\"pose_offset\"";
    case TFP_ED_S_SELECT_MODE: case TFP_ED_S_ACTION_SUPPORTED: return fx_str(a->text);
    case TFP_ED_S_GET_ACTION: case TFP_ED_S_BASELINE: snprintf(out, size, "%s,%s", show_value(&a->key), fx_str(a->text)); return out;
    case TFP_ED_S_SAVE: {   /* the helpers share eight rotating buffers: take each text before the next is made */
      static char saved[512];
      size_t used = (size_t)snprintf(saved, sizeof saved, "%s,", place_text(&a->place));
      used += (size_t)snprintf(saved + used, sizeof saved - used, "%s,", show_value(&a->key));
      used += (size_t)snprintf(saved + used, sizeof saved - used, "%s,%s,", show_value(&a->owner), fx_bool(a->flag));
      snprintf(saved + used, sizeof saved - used, "%s", fx_str(a->text));
      return saved;
    }
    case TFP_ED_I_SET_FOCUS: return fx_bool(a->flag);
    case TFP_ED_I_DOWN: return fx_num(a->number);
    case TFP_ED_V_DRAW: {
      size_t used = 0;
      for (int i = 0; i < a->ncommands; i++) {
        const TfpEdCommand *c = &a->commands[i];
        const char *alpha = c->panel_alpha ? "panel" : "solid";
        if (i) draw_text[used++] = ';';
        if (c->is_text)
          used += (size_t)snprintf(draw_text + used, sizeof draw_text - used, "t %s %s %s %s %s %s %d", fx_str(c->text), fx_num(c->x), fx_num(c->y),
                                   fx_num(c->size), COLOR[c->color], alpha, c->layer);
        else
          used += (size_t)snprintf(draw_text + used, sizeof draw_text - used, "r %s %s %s %s %s %s %d", fx_num(c->x), fx_num(c->y), fx_num(c->w),
                                   fx_num(c->h), COLOR[c->color], alpha, c->layer);
      }
      draw_text[used] = 0;
      return draw_text;
    }
    default: return "";
  }
}
static bool host_call(void *user, TfpEdCall which, const TfpEdArgs *a, TfpEdReply *r) {
  (void)user;
  fx_tick();
  fx_note("%s(%s)", NAME[which], arguments(which, a));
  memset(r, 0, sizeof *r);
  const char *raised = fx_raise(RAISE[which]);
  if (raised) { r->a.kind = TFP_ED_TEXT; snprintf(r->a.s, sizeof r->a.s, "%s", raised); return false; }
#define PAIR(x, y) (r->a = value_of(fx_world[x]), r->b = value_of(fx_world[y]))
  switch (which) {
    case TFP_ED_CLOCK: r->a = value_of(fx_world[W_clock]); break;
    case TFP_ED_C_IS_CLEAN: r->a = value_of(fx_world[W_is_clean]); break;
    case TFP_ED_C_IS_ENABLED: r->a = value_of(fx_world[W_is_enabled]); break;
    case TFP_ED_C_RESULT:
      if (fx_world[W_result].t != 'n') { r->a.kind = TFP_ED_TABLE; if (fx_world[W_result].t == 's') snprintf(r->a.s, sizeof r->a.s, "%s", slot_text(W_result)); }
      break;
    case TFP_ED_C_SET_PLACEMENT: PAIR(W_set_ok, W_set_why); break;
    case TFP_ED_C_GET_PLACEMENT: place(W_get_place, W_get_place_f, &r->a, &r->place); r->b = value_of(fx_world[W_get_why]); break;
    case TFP_ED_C_START: PAIR(W_start_ok, W_start_why); break;
    case TFP_ED_S_SELECT_MODE: PAIR(W_select_ok, W_select_why); break;
    case TFP_ED_S_CAN_START: PAIR(W_can_ok, W_can_why); break;
    case TFP_ED_S_TARGET: PAIR(W_target_key, W_target_owner); break;
    case TFP_ED_S_GET: place(W_store_get, W_store_get_f, &r->a, &r->place); r->b = value_of(fx_world[W_store_get_why]); break;
    case TFP_ED_S_GET_ACTION: place(W_act, W_act_f, &r->a, &r->place); break;
    case TFP_ED_S_SAVE: PAIR(W_save_ok, W_save_why); break;
    case TFP_ED_S_RESET_ALL: PAIR(W_reset_ok, W_reset_why); break;
    case TFP_ED_S_BASELINE: place(W_basefn, W_base_f, &r->a, &r->place); break;
    case TFP_ED_S_ACTION_SUPPORTED: r->a = value_of(fx_world[W_supported]); break;
    case TFP_ED_LUT_STATUS: status(W_lut_status, r); break;
    case TFP_ED_W_MOUSE_FOCUS: r->a = value_of(fx_world[W_mouse_focus]); break;
    case TFP_ED_W_SHOW_CURSOR: r->a = value_of(fx_world[W_show_cursor]); break;
    case TFP_ED_A_CLOSE: PAIR(W_close_ok, W_close_why); break;
    case TFP_ED_A_IS_OPEN: r->a = value_of(fx_world[W_is_open]); break;
    case TFP_ED_A_INPUT_STATUS: status(W_status, r); break;
    case TFP_ED_A_RESOLUTION: PAIR(W_res_w, W_res_h); break;
    case TFP_ED_A_CREATE_VIEW:
      if (fx_world[W_view].t == 's') {
        r->a.kind = TFP_ED_TABLE; r->view.has_draw = has(W_view, 'd'); r->view.has_release = has(W_view, 'r'); r->view.has_refresh_font = has(W_view, 'f');
      } else r->a = value_of(fx_world[W_view]);
      r->b = value_of(fx_world[W_view_why]);
      break;
    case TFP_ED_I_ACQUIRE: PAIR(W_acquire, W_acquire_why); break;
    case TFP_ED_I_RELEASE: PAIR(W_release, W_release_why); break;
    case TFP_ED_I_SET_FOCUS: PAIR(W_focus_ok, W_focus_why); break;
    case TFP_ED_I_SAMPLE:
      if (fx_world[W_sample].t == 't') {
        r->a.kind = TFP_ED_TABLE; r->sample.click = fx_getk(W_sample_f, 1).t == 't';
        r->sample.x = sample_number(2); r->sample.y = sample_number(3); r->sample.wheel = sample_number(4); r->sample.dt = sample_number(5);
      } else r->a = value_of(fx_world[W_sample]);
      r->b = value_of(fx_world[W_sample_why]);
      break;
    case TFP_ED_I_DOWN: r->a = value_of(fx_getk(W_down, a->number)); break;
    case TFP_ED_I_FOCUSED: r->a = value_of(fx_world[W_focused]); break;
    case TFP_ED_V_DRAW: PAIR(W_draw_ok, W_draw_why); break;
    case TFP_ED_V_RELEASE: PAIR(W_vrelease_ok, W_vrelease_why); break;
    default: break;
  }
#undef PAIR
  return true;
}
static char action_text[256];
static void host_look(void *user, TfpEdLook *l) {
  (void)user;
  l->has_select_mode = fx_truthy(fx_world[W_has_select]); l->has_can_start = fx_truthy(fx_world[W_has_can]);
  l->has_target = fx_truthy(fx_world[W_has_target]); l->has_action_supported = fx_truthy(fx_world[W_has_supported]);
  l->reset_all = fx_truthy(fx_world[W_has_reset_all]); l->single_save = fx_truthy(fx_world[W_single_save]);
  l->rotation_false = fx_world[W_rotation].t == 'f';
  if (fx_world[W_base].t == 's' && fx_world[W_base].len == 2 && !memcmp(fx_world[W_base].s, "fn", 2)) l->baseline = 2;
  else if (fx_world[W_base].t == 't') { TfpEdValue unused; l->baseline = 1; place(W_base, W_base_f, &unused, &l->baseline_place); }
  l->title = slot_text(W_title); l->save_label = slot_text(W_save_label); l->all_label = slot_text(W_all_label); l->footer = slot_text(W_footer);
  l->nactions = -1;
  if (fx_world[W_actions].t == 's') {   /* "id:Label,id:Label" */
    snprintf(action_text, sizeof action_text, "%.*s", fx_world[W_actions].len, fx_world[W_actions].s);
    l->nactions = 0;
    for (char *item = strtok(action_text, ","); item; item = strtok(NULL, ",")) {
      char *colon = strchr(item, ':');
      *colon = 0;
      l->action_id[l->nactions] = item; l->action_label[l->nactions++] = colon + 1;
    }
  }
  l->lut = fx_world[W_lut].t == 's'; l->lut_closed = has(W_lut, 'c'); l->lut_menu_visible = has(W_lut, 'm'); l->lut_was_visible = has(W_lut, 'w');
  l->lut_capture = has(W_lut, 'a') || has(W_lut, 's') || has(W_lut, 'k'); l->lut_capture_active = has(W_lut, 'a'); l->lut_capture_status = has(W_lut, 's');
  l->window = fx_world[W_win].t == 's'; l->window_mouse_focus = has(W_win, 'f'); l->window_show_cursor = has(W_win, 'c');
  l->provider = fx_world[W_api].t == 'd' ? (unsigned)fx_world[W_api].n : 0;
  l->provider_supported = fx_truthy(fx_world[W_api_ok]) && fx_world[W_input_id].t == 'd';
  l->provider_input = l->provider_supported ? (unsigned)fx_world[W_input_id].n : 0;
}
static void host_log(void *user, const char *text) { (void)user; fx_log(text); }

static void fresh(void) { tfp_editor_free(editor); editor = NULL; }
static void create(void) {
  TfpEdHost host = {NULL, host_call, host_look, fx_truthy(fx_world[W_no_log]) ? NULL : host_log};
  tfp_editor_free(editor);
  editor = tfp_editor_new(&host);
}
static const char *arg_text(const FxValue *v) {
  static char copy[160];
  if (v->t != 's') return NULL;
  snprintf(copy, sizeof copy, "%.*s", v->len, v->s);
  return copy;
}
static void dispatch(const FxOp *op) {
  TfpEdResult r;
  double rotation[3];
  memset(&r, 0, sizeof r);
  if (!strcmp(op->call, "new")) { create(); fx_put("~ ~"); }
  else {
    if (!strcmp(op->call, "request_open")) r = tfp_editor_request_open(editor, arg_text(&op->a[0]));
    else if (!strcmp(op->call, "update")) r = tfp_editor_update(editor);
    else if (!strcmp(op->call, "cancel")) r = tfp_editor_cancel(editor, arg_text(&op->a[0]));
    else if (!strcmp(op->call, "finish")) r = tfp_editor_finish(editor, fx_truthy(op->a[0]));
    else r = tfp_editor_shutdown(editor);
    if (r.raised) fx_put("ERR %s", show_value(&r.r1)); else fx_put("%s %s", show_value(&r.r1), show_value(&r.r2));
  }
  fx_put(" %s %s %s %s %s %s ", tfp_editor_phase(editor), fx_str(tfp_editor_kind(editor)), fx_str(tfp_editor_status(editor)),
         fx_bool(tfp_editor_is_active(editor)), fx_bool(tfp_editor_is_clean(editor)), fx_str(tfp_editor_preview_action(editor)));
  if (tfp_editor_preview_rotation(editor, rotation)) fx_put("%s,%s,%s", fx_num(rotation[0]), fx_num(rotation[1]), fx_num(rotation[2]));
  else fx_put("~");
}

int main(void) {
  fx_unused();
  fx_any = W_x_any;
  return fx_main("placement_editor", PLACEMENT_EDITOR_VECTOR_COMMIT, placement_editor_runs,
                 (int)(sizeof placement_editor_runs / sizeof placement_editor_runs[0]), placement_editor_ops, placement_editor_events,
                 placement_editor_slots, W_COUNT, fresh, dispatch);
}
