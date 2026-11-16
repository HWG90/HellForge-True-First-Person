/* Replays tests/vectors/aim_markers_vectors.h (the retail aim_markers.lua's own answers) against
 * src/aim_markers.c. The fakes are those of tools/retail_ref/run_aim_markers.lua. */
#include "aim_markers.h"
#include "effects_fx.h"
#include "vectors/aim_markers_vectors.h"

static TfpAimMarkersHost host;
static TfpAimMarkers lease;
static TfpAimMarkersHud huds[4];
static bool have_hud[4];

static bool mem(const char *name, uint64_t address, int size, unsigned char *out, const char **error, int raise_slot, bool with_size) {
  fx_tick();
  if (with_size) fx_note("%s(%s %d)", name, fx_num((double)address), size);
  else fx_note("%s(%s)", name, fx_num((double)address));
  if ((*error = fx_raise(raise_slot))) return false;
  if (!fx_read((double)address, size, out)) { *error = fx_truthy(fx_world[W_nil_fault]) ? NULL : "Memory unavailable"; return false; }
  return true;
}
static bool f_module(void *u, uint64_t *game, const char **error) {
  (void)u;
  fx_tick();
  fx_note("module(\"identity\")");
  if ((*error = fx_raise(W_x_module))) return false;
  *game = (uint64_t)fx_n(W_game);
  return true;
}
static bool f_read(void *u, uint64_t address, uint32_t size, uint8_t *out, const char **error) {
  (void)u;
  return mem("read", address, (int)size, out, error, W_x_read, true);
}
static bool f_ptr(void *u, uint64_t address, uint64_t *out, const char **error) {
  unsigned char b[8];
  (void)u;
  if (!mem("ptr", address, 8, b, error, W_x_ptr, false)) return false;
  *out = 0;
  for (int i = 7; i >= 0; i--) *out = *out << 8 | b[i];
  if (*out < 65536) { *error = "Pointer unavailable"; return false; }
  return true;
}
static uint32_t word(const unsigned char *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static bool f_u32(void *u, uint64_t address, uint32_t *out, const char **error) {
  unsigned char b[4];
  (void)u;
  if (!mem("u32", address, 4, b, error, W_x_u32, false)) return false;
  *out = word(b);
  return true;
}
static bool f_u8(void *u, uint64_t address, uint8_t *out, const char **error) {
  (void)u;
  return mem("u8", address, 1, out, error, W_x_u8, false);
}
static bool f_windows(void *u, bool *wine, const char **error) {
  (void)u;
  fx_tick();
  fx_note("windows()");
  if ((*error = fx_raise(W_x_windows))) return false;
  *wine = fx_is(W_wine, 't');
  return true;
}
static double number_or(int slot, double otherwise) { return fx_is(slot, 'd') ? fx_n(slot) : otherwise; }
static size_t f_query(void *u, uint64_t address, TfpAimMarkersRegion *r, size_t size) {
  double ret;
  (void)u;
  fx_tick();
  fx_note("virtual_query(%s %s)", fx_num((double)address), fx_num((double)size));
  ret = number_or(W_q_ret, (double)size);
  if (ret != 0) {
    r->state = (uint32_t)number_or(W_q_state, 0); r->kind = (uint32_t)number_or(W_q_kind, 0);
    r->protect = (uint32_t)number_or(W_q_protect, 0); r->allocation = (uint64_t)number_or(W_q_alloc, 0);
  }
  return (size_t)ret;
}
/* What the game's show/hide function (or the test setter) does to the widget's flag word. */
static bool set_shown(uint64_t widget, bool value, const char **error) {
  FxValue own = fx_getk(W_set_fx, (double)widget);
  int effect = own.t == 'd' ? (int)own.n : (int)number_or(W_set_effect, 0);
  unsigned char b[4];
  if (fx_read((double)widget, 4, b) && effect != 1) {
    uint32_t now = word(b), bit16 = now >> 4 & 1, want = value ? 1 : 0;
    if (effect == 3) want = 1 - want;
    now = now + (want - bit16) * 16;
    for (int i = 0; i < 4; i++) b[i] = (unsigned char)(now >> (8 * i));
    fx_write((double)widget, b, 4);
  }
  if (effect == 2) { *error = "failed after write"; return false; }
  return true;
}
static bool f_set_shown(void *u, uint64_t function, uint64_t widget, uint8_t shown, const char **error) {
  (void)u;
  fx_tick();
  fx_note("set_shown(%s %s %u)", fx_num((double)function), fx_num((double)widget), shown);
  if ((*error = fx_raise(W_x_set_shown))) return false;
  return set_shown(widget, shown == 1, error);
}
static bool f_setter(void *u, uint64_t widget, bool shown, const char **error) {
  (void)u;
  fx_tick();
  fx_note("setter(%s %s)", fx_num((double)widget), fx_bool(shown));
  if ((*error = fx_raise(W_x_setter))) return false;
  return set_shown(widget, shown, error);
}

static void text(char *out, size_t *len, bool *has, int slot) {
  *has = fx_is(slot, 's');
  *len = *has ? (size_t)fx_world[slot].len : 0;
  if (*has) memcpy(out, fx_world[slot].s, *len);
}
static bool f_foreign(void *u) { (void)u; fx_tick(); fx_note("foreign()"); return fx_truthy(fx_world[W_foreign]); }
static bool f_snapshot(void *u, TfpAimMarkersSnapshot *s) {
  (void)u;
  fx_tick();
  fx_note("snapshot()");
  memset(s, 0, sizeof *s);
  if (!fx_is(W_s_ok, 't')) return false;
  s->safe = fx_truthy(fx_world[W_s_safe]); s->camera_kind = (int)number_or(W_s_kind, 0);
  text(s->identity, &s->identity_len, &s->has_identity, W_s_id);
  text(s->player_identity, &s->player_identity_len, &s->has_player_identity, W_s_pid);
  s->shoulder_aim = fx_is(W_s_sa, 't'); s->controller_active = fx_is(W_s_ca, 't'); s->aiming = fx_is(W_s_aim, 't');
  return true;
}
static bool f_pin(void *u, TfpAimMarkersSnapshot *s) {
  (void)u;
  fx_tick();
  fx_note("pin_player_snapshot()");
  memset(s, 0, sizeof *s);
  if (!fx_is(W_p_ok, 't')) return false;
  s->foreign_camera = fx_truthy(fx_world[W_p_foreign]);
  text(s->player_identity, &s->player_identity_len, &s->has_player_identity, W_p_pid);
  s->shoulder_aim = fx_is(W_p_sa, 't'); s->controller_active = fx_is(W_p_ca, 't'); s->aiming = fx_is(W_p_aim, 't');
  return true;
}
static bool f_pinned_ready(void *u, const TfpAimMarkersSnapshot *s) {
  (void)u;
  fx_tick();
  fx_note("pinned_ready(%s)", s->has_player_identity ? fx_strn(s->player_identity, s->player_identity_len) : "~");
  return fx_is(W_pinned_ready, 't');
}
static bool f_fallback_ready(void *u) { (void)u; fx_tick(); fx_note("fallback_ready()"); return fx_truthy(fx_world[W_fallback_ready]); }
static bool f_is_enabled(void *u) { (void)u; fx_tick(); fx_note("is_enabled()"); return fx_truthy(fx_world[W_c_enabled]); }
static bool f_pose_offset(void *u) { (void)u; fx_tick(); fx_note("result()"); return fx_truthy(fx_world[W_c_pose]); }
static bool f_pose_context(void *u, const TfpAimMarkersSnapshot *s, uint64_t *weapon_object, const char **error) {
  (void)u;
  fx_tick();
  fx_note("pose_context(%s)", s->has_identity ? fx_strn(s->identity, s->identity_len) : "~");
  if ((*error = fx_raise(W_x_pose_context))) return false;
  *weapon_object = (uint64_t)fx_n(W_s_wobj);
  return true;
}
/* The saved table lives in the world as a text: `key` (true) or `key=f` (false), separated by commas. */
static bool f_settings(void *u, TfpAimMarkersSaved *answer) {
  static TfpAimMarkersSaved kept;       /* as a host that reuses one buffer: entries past `count` are stale */
  TfpAimMarkersSaved *out = &kept;
  const char *s;
  int len, from = 0;
  (void)u;
  fx_tick();
  fx_note("settings()");
  out->count = 0;
  if (!fx_is(W_saved, 's')) { *answer = kept; return false; }
  s = fx_world[W_saved].s; len = fx_world[W_saved].len;
  for (int i = 0; i <= len; i++) {
    if (i < len && s[i] != ',') continue;
    if (i > from && !(i - from >= 2 && s[i - 2] == '=' && s[i - 1] == 'f') && out->count < TFP_AIM_MARKERS_SAVED) {
      int n = i - from < TFP_AIM_MARKERS_SAVED_KEY - 1 ? i - from : TFP_AIM_MARKERS_SAVED_KEY - 1;
      memcpy(out->keys[out->count], s + from, (size_t)n);
      out->keys[out->count++][n] = 0;
    }
    from = i + 1;
  }
  *answer = kept;
  return true;
}
static int by_text(const void *a, const void *b) { return strcmp(a, b); }
/* Retail hands over a table built with pairs(); both fakes print and store the keys sorted. */
static bool f_save(void *u, const char (*keys)[TFP_AIM_MARKERS_KEY], size_t count, const char **why) {
  char sorted[TFP_AIM_MARKERS_SAVED + 1][TFP_AIM_MARKERS_KEY];
  char *joined = malloc((TFP_AIM_MARKERS_SAVED + 1) * TFP_AIM_MARKERS_KEY + 1);
  size_t used = 0;
  (void)u;
  if (count > TFP_AIM_MARKERS_SAVED + 1) { fprintf(stderr, "save: %zu keys\n", count); exit(2); }
  memcpy(sorted, keys, count * sizeof sorted[0]);
  qsort(sorted, count, sizeof sorted[0], by_text);
  joined[0] = 0;
  for (size_t i = 0; i < count; i++) used += (size_t)sprintf(joined + used, "%s%s", i ? "," : "", sorted[i]);
  fx_tick();
  fx_note("save(%s)", joined);
  if (!fx_is(W_save_ok, 't')) { if (fx_s(W_save_why)) *why = fx_s(W_save_why); return false; }   /* no second result: left alone */
  fx_world[W_saved] = (FxValue){'s', 0, joined, (int)used};
  return true;
}
static TfpAimMarkersTri f_global_setting(void *u) {
  (void)u;
  fx_tick();
  fx_note("global_setting()");
  return fx_is(W_global, 't') ? TFP_AIM_MARKERS_SHOW : fx_is(W_global, 'f') ? TFP_AIM_MARKERS_HIDE : TFP_AIM_MARKERS_UNSET;
}
static TfpAimMarkersSave f_save_global(void *u, bool value, const char **why) {
  (void)u;
  fx_tick();
  fx_note("save_global(%s)", fx_bool(value));
  if (fx_raise(W_x_save_global)) { *why = fx_raise(W_x_save_global); return TFP_AIM_MARKERS_SAVE_RAISED; }
  if (fx_is(W_sg_ok, 'd')) return TFP_AIM_MARKERS_SAVE_RAISED;      /* the error value nil */
  if (!fx_is(W_sg_ok, 't')) { if (fx_s(W_sg_why)) *why = fx_s(W_sg_why); return TFP_AIM_MARKERS_SAVE_REFUSED; }
  fx_world[W_global] = (FxValue){value ? 't' : 'f', 0, NULL, 0};
  return TFP_AIM_MARKERS_SAVE_DONE;
}
static void f_log(void *u, const char *message) { (void)u; fx_log(message); }

static void build(bool use_setter, bool no_log) {
  memset(&host, 0, sizeof host);
  host.backend_module_identity = f_module; host.backend_read = f_read; host.backend_ptr = f_ptr; host.backend_u32 = f_u32; host.backend_u8 = f_u8;
  host.backend_windows = f_windows; host.windows_virtual_query = f_query; host.game_set_widget_shown = f_set_shown;
  host.setter = use_setter ? f_setter : NULL;
  host.foreign = f_foreign; host.backend_snapshot = f_snapshot; host.fallback_ready = f_fallback_ready;
  host.controller_is_enabled = f_is_enabled; host.controller_pose_offset = f_pose_offset; host.backend_pose_context = f_pose_context;
  host.settings = f_settings; host.save = f_save; host.log = no_log ? NULL : f_log;
  memset(have_hud, 0, sizeof have_hud);
  tfp_aim_markers_init(&lease, &host);
}
static void fresh(void) { build(false, false); }
static void put_hud(const TfpAimMarkersHud *h) {
  if (!h) { fx_put("~"); return; }
  fx_put("%s %s %s %s %s %s %s %s", fx_num((double)h->game), fx_num((double)h->root), fx_num((double)h->state), fx_num((double)h->parent),
         fx_num((double)h->context), fx_num((double)h->world), fx_num((double)h->widgets[0]), fx_num((double)h->widgets[1]));
}
static void put_state(void) {
  TfpAimMarkersResult r;
  fx_put(" | %s %s | ", fx_bool(lease.closing), lease.has_fault ? fx_str(lease.fault) : "~");
  if (lease.has_lease) {
    const TfpAimMarkersLease *l = &lease.lease;
    put_hud(&l->hud);
    fx_put(" %s %s %s [", fx_strn(l->weapon, l->weapon_len), fx_strn(l->player, l->player_len), l->releasing ? "T" : "~");
    for (size_t i = 0; i < l->count; i++)
      fx_put("%s%s:%s:%s", i ? " " : "", fx_num((double)l->rows[i].widget), fx_bool(l->rows[i].before), l->rows[i].done ? "T" : "~");
    fx_put("]");
  } else fx_put("~");
  tfp_aim_markers_result(&lease, &r);
  fx_put(" | %s %s %s %s | %s %s | ", fx_bool(r.hidden), fx_str(r.error), fx_bool(r.cleanup_pending), fx_bool(tfp_aim_markers_is_clean(&lease)),
         lease.source.has_cached_identity ? fx_strn(lease.source.cached_identity, lease.source.cached_identity_len) : "~",
         lease.source.has_cached_key ? fx_str(lease.source.cached_key) : "~");
  put_hud(have_hud[1] ? &huds[1] : NULL);
  fx_put(" | ");
  put_hud(have_hud[2] ? &huds[2] : NULL);
}
static void dispatch(const FxOp *op) {
  const char *why = "(unset)", *call = op->call;
  if (!strcmp(call, "new")) build(fx_truthy(op->a[0]), fx_truthy(op->a[1]));
  host.backend_pin_first_person = fx_is(W_pin_first, 't');
  host.backend_pin_player_snapshot = fx_truthy(fx_world[W_pin_fn]) ? f_pin : NULL;
  host.pinned_ready = fx_truthy(fx_world[W_has_pinned_ready]) ? f_pinned_ready : NULL;
  host.global_setting = fx_truthy(fx_world[W_has_global_fn]) ? f_global_setting : NULL;
  host.save_global = fx_truthy(fx_world[W_has_sg]) ? f_save_global : NULL;
  if (!strcmp(call, "new")) fx_put("new");
  else if (!strcmp(call, "update")) { bool r = tfp_aim_markers_update(&lease, &why); fx_put("%s %s", fx_bool(r), fx_str(why)); }
  else if (!strcmp(call, "stop")) { bool r = tfp_aim_markers_stop(&lease, &why); fx_put("%s %s", fx_bool(r), fx_str(why)); }
  else if (!strcmp(call, "shutdown")) { bool r = tfp_aim_markers_shutdown(&lease, &why); fx_put("%s %s", fx_bool(r), fx_str(why)); }
  else if (!strcmp(call, "set_setting")) {
    bool r = tfp_aim_markers_set_setting(&lease, op->a[0].t == 't', &why);
    fx_put("%s %s", fx_bool(r), fx_str(why));
  } else if (!strcmp(call, "toggle")) {
    bool r = tfp_aim_markers_toggle(&lease, op->a[0].s, op->a[1].s, (size_t)op->a[1].len, &why);
    fx_put("%s %s", fx_bool(r), fx_str(why));
  } else if (!strcmp(call, "setting")) fx_put("%s", fx_bool(tfp_aim_markers_setting(&lease)));
  else if (!strcmp(call, "target")) {
    TfpAimMarkersMenuTarget t;
    TfpAimMarkersRead got = tfp_aim_markers_target(&lease, &t, &why);
    if (got == TFP_AIM_MARKERS_READ_RAISED) fx_put("raised %s", fx_str(why));
    else if (got == TFP_AIM_MARKERS_READ_TARGET) fx_put("target %s %s %s", fx_str(t.key), fx_strn(t.identity, t.identity_len), fx_bool(t.allowed));
    else fx_put("none");
  } else if (!strcmp(call, "current")) {
    TfpAimMarkersTarget t;
    TfpAimMarkersRead got = tfp_aim_markers_source_current(&lease.source, fx_truthy(op->a[0]), &t, &why);
    if (got == TFP_AIM_MARKERS_READ_RAISED) fx_put("raised %s", fx_str(why));
    else if (got == TFP_AIM_MARKERS_READ_TARGET)
      fx_put("target %s %s %s %s %s", t.view_only ? "T" : "~", t.has_key ? fx_str(t.key) : "~", t.has_identity ? fx_strn(t.identity, t.identity_len) : "~",
             t.has_player_identity ? fx_strn(t.player_identity, t.player_identity_len) : "~", fx_bool(t.aiming));
    else fx_put("none");
  } else if (!strcmp(call, "probe")) {
    int slot = (int)op->a[1].n;
    TfpAimMarkersHud found;
    TfpAimMarkersProbe got = tfp_aim_markers_probe(&host, fx_truthy(op->a[0]), &found, &why);
    if (got == TFP_AIM_MARKERS_PROBE_RAISED) fx_put("raised %s", fx_str(why));
    else { have_hud[slot] = got == TFP_AIM_MARKERS_PROBE_HUD; if (have_hud[slot]) huds[slot] = found; fx_put(have_hud[slot] ? "hud" : "none"); }
  } else if (!strcmp(call, "show")) {
    int slot = (int)op->a[0].n;
    if (!have_hud[slot]) { fprintf(stderr, "show without a HUD\n"); exit(2); }
    if (tfp_aim_markers_show(&host, &huds[slot], (uint64_t)op->a[1].n, fx_truthy(op->a[2]), &why)) fx_put("ok");
    else fx_put("raised %s", fx_str(why));
  } else if (!strcmp(call, "visible")) {
    bool visible = false;
    if (tfp_aim_markers_visible(&host, (uint64_t)op->a[0].n, &visible, &why)) fx_put("%s", fx_bool(visible));
    else fx_put("raised %s", fx_str(why));
  } else if (!strcmp(call, "same")) {
    int a = (int)op->a[0].n, b = (int)op->a[1].n;
    fx_put("%s", fx_bool(tfp_aim_markers_same(have_hud[a] ? &huds[a] : NULL, have_hud[b] ? &huds[b] : NULL)));
  } else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
  put_state();
}
int main(void) {
  fx_mem = W_mem; fx_any = W_x_any;
  return fx_main("aim_markers", AIM_MARKERS_VECTOR_COMMIT, aim_markers_runs, (int)(sizeof aim_markers_runs / sizeof aim_markers_runs[0]),
                 aim_markers_ops, aim_markers_events, aim_markers_slots, W_COUNT, fresh, dispatch);
}
