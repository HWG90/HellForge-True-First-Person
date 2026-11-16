/* Replays tests/vectors/camera_compat_vectors.h (the retail camera_compat.lua's own answers) against
 * src/camera_compat.c. The fakes are those of tools/retail_ref/run_camera_compat.lua; its header comment
 * describes the world. */
#include "camera_compat.h"
#include "effects_fx.h"
#include "vectors/camera_compat_vectors.h"

static TfpCameraCompatHost host;
static TfpCameraCompat compat;
static bool have_compat, no_debug;
static int depth;

static TfpCameraCompatUpvalue f_getupvalue(void *u, TfpCameraCompatValue fn, int index, TfpCameraCompatValue *value);
static TfpCameraCompatEnabled f_is_enabled(void *u);
/* What the module reads without a call follows the world. */
static void sync_host(void) {
  host.debug_getupvalue = !no_debug && fx_truthy(fx_world[W_dbg_fn]) ? f_getupvalue : NULL;
  host.controller_is_enabled = fx_truthy(fx_world[W_c_gone]) ? NULL : f_is_enabled;
}
static void tick(void) { fx_tick(); sync_host(); }

static TfpCameraCompatValue value_of(TfpCameraCompatKind kind, uint64_t id, uint32_t shim) { return (TfpCameraCompatValue){kind, id, shim}; }
/* nil, false, true, a number or a text as the Lua value itself. */
static TfpCameraCompatValue plain(FxValue v) {
  switch (v.t) {
    case 'n': return value_of(TFP_CAMERA_COMPAT_NIL, 0, 0);
    case 'f': return value_of(TFP_CAMERA_COMPAT_FALSE, 0, 0);
    case 't': return value_of(TFP_CAMERA_COMPAT_TRUE, 0, 0);
    case 'd': return value_of(TFP_CAMERA_COMPAT_NUMBER, 0, 0);
    default: return value_of(TFP_CAMERA_COMPAT_STRING, 0, 0);
  }
}
/* A global: a number is a provider table. */
static TfpCameraCompatValue global(FxValue v) { return v.t == 'd' ? value_of(TFP_CAMERA_COMPAT_TABLE, (uint64_t)v.n, 0) : plain(v); }
/* What a setter holds: a number is a foreign reader (below 1000) or one of the module's shims. */
static TfpCameraCompatValue decode(FxValue v) {
  if (v.t != 'd') return plain(v);
  if (v.n < 1000) return value_of(TFP_CAMERA_COMPAT_FUNCTION, 512 + (uint64_t)v.n, 0);
  return value_of(TFP_CAMERA_COMPAT_FUNCTION, 0, (uint32_t)v.n - 1000);
}
static FxValue encode(TfpCameraCompatValue v) {
  if (v.kind == TFP_CAMERA_COMPAT_NIL) return (FxValue){'n', 0, NULL, 0};
  if (v.shim) return (FxValue){'d', 1000 + v.shim, NULL, 0};
  return (FxValue){'d', (double)(v.id - 512), NULL, 0};
}
static const char *vt(TfpCameraCompatValue v) {
  size_t size;
  char *out = fx_scratch(&size);
  switch (v.kind) {
    case TFP_CAMERA_COMPAT_NIL: return "~";
    case TFP_CAMERA_COMPAT_FALSE: return "F";
    case TFP_CAMERA_COMPAT_TRUE: return "T";
    case TFP_CAMERA_COMPAT_NUMBER: return "<number>";
    case TFP_CAMERA_COMPAT_STRING: return "<string>";
    case TFP_CAMERA_COMPAT_TABLE: snprintf(out, size, "tb%llu", (unsigned long long)v.id); return out;
    case TFP_CAMERA_COMPAT_FUNCTION:
      if (v.shim) snprintf(out, size, "H%u", v.shim);
      else snprintf(out, size, "f%llu", (unsigned long long)v.id);
      return out;
    default: return "<other>";
  }
}

static TfpCameraCompatValue f_own(void *u) { (void)u; tick(); fx_note("photo_camera_ownership()"); return global(fx_world[W_g_own]); }
static TfpCameraCompatValue f_api(void *u) { (void)u; tick(); fx_note("photo_actioncam_compat()"); return global(fx_world[W_g_api]); }
static bool f_actioncam(void *u) {
  (void)u; tick(); fx_note("actioncam_loaded()");
  return fx_is(W_pkg, 'd') && fx_is(W_pkg_loaded, 'd') && fx_truthy(fx_world[W_actioncam]);
}
static TfpCameraCompatValue f_field(void *u, TfpCameraCompatValue table, TfpCameraCompatField field) {
  static const char *const names[] = {"held", "available", "disabled", "set_input_blocked"};
  static const int slots[] = {W_t_held, W_t_avail, W_t_dis, W_t_set};
  FxValue v;
  (void)u;
  tick();
  fx_note("field(%s %s)", vt(table), names[field]);
  if (table.kind != TFP_CAMERA_COMPAT_TABLE) { fprintf(stderr, "field of a value that is no table\n"); exit(2); }
  v = fx_getk(slots[field], (double)table.id);
  if (field == TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED && v.t == 'd') return value_of(TFP_CAMERA_COMPAT_FUNCTION, 256 + (uint64_t)v.n, 0);
  return plain(v);
}
static TfpCameraCompatUpvalue f_getupvalue(void *u, TfpCameraCompatValue fn, int index, TfpCameraCompatValue *value) {
  FxValue pos_value, mode;
  double sid;
  int pos;
  (void)u;
  tick();
  fx_note("getupvalue(%s %d)", vt(fn), index);
  if (fx_raise(W_x_getupvalue)) return TFP_CAMERA_COMPAT_UPVALUE_RAISED;
  if (fn.kind != TFP_CAMERA_COMPAT_FUNCTION || fn.shim || fn.id < 256 || fn.id >= 512) return TFP_CAMERA_COMPAT_UPVALUE_RAISED;
  sid = (double)(fn.id - 256);
  pos_value = fx_getk(W_up_pos, sid);
  pos = pos_value.t == 'd' ? (int)pos_value.n : 1;
  if (index < pos) return TFP_CAMERA_COMPAT_UPVALUE_OTHER;
  if (index > pos) return TFP_CAMERA_COMPAT_UPVALUE_END;
  mode = fx_getk(W_up_mode, sid);
  if (mode.t == 'd' && mode.n == 1) return TFP_CAMERA_COMPAT_UPVALUE_END;
  if (mode.t == 'd' && mode.n == 2) return TFP_CAMERA_COMPAT_UPVALUE_RAISED;
  *value = decode(fx_getk(W_rd, sid));
  return TFP_CAMERA_COMPAT_UPVALUE_INPUT_READER;
}
/* Calls a reader function as Lua would: one of the module's shims or a foreign reader. */
static TfpCameraCompatRead call_reader(TfpCameraCompatValue reader);
static TfpCameraCompatRead f_previous(void *u, TfpCameraCompatValue reader) { (void)u; return call_reader(reader); }
static TfpCameraCompatRead call_reader(TfpCameraCompatValue reader) {
  FxValue beh;
  if (reader.shim) return tfp_camera_compat_shim(&compat, reader.shim) ? TFP_CAMERA_COMPAT_READ_NOT_FALSE : TFP_CAMERA_COMPAT_READ_FALSE;
  tick();
  fx_note("previous(%s)", vt(reader));
  if (fx_raise(W_x_previous)) return TFP_CAMERA_COMPAT_READ_RAISED;
  beh = fx_getk(W_r_beh, (double)(reader.id - 512));
  if (beh.t == 's') return TFP_CAMERA_COMPAT_READ_RAISED;
  if (beh.t == 'd') {
    TfpCameraCompatValue current = decode(fx_getk(W_rd, beh.n));
    TfpCameraCompatRead got;
    if (current.kind != TFP_CAMERA_COMPAT_FUNCTION || depth >= 6) return TFP_CAMERA_COMPAT_READ_FALSE;
    depth++;
    got = call_reader(current);
    depth--;
    return got;
  }
  return beh.t == 'f' ? TFP_CAMERA_COMPAT_READ_FALSE : TFP_CAMERA_COMPAT_READ_NOT_FALSE;
}
static TfpCameraCompatCall f_set(void *u, TfpCameraCompatValue setter, TfpCameraCompatValue given, bool *accepted_is_false, const char **text) {
  FxValue install, raise, issue;
  double sid;
  (void)u;
  tick();
  fx_note("set(%s %s)", vt(setter), vt(given));
  if ((*text = fx_raise(W_x_set))) return TFP_CAMERA_COMPAT_RAISED;
  if (setter.kind != TFP_CAMERA_COMPAT_FUNCTION || setter.shim || setter.id < 256 || setter.id >= 512) {
    fprintf(stderr, "the module called a value that is no setter\n");
    exit(2);
  }
  sid = (double)(setter.id - 256);
  install = fx_getk(W_set_install, sid);
  if (install.t == 'n') fx_poke(W_rd, sid, encode(given));
  else if (install.t != 'f') fx_poke(W_rd, sid, install);
  if (fx_truthy(fx_getk(W_set_calls, sid)) && given.kind == TFP_CAMERA_COMPAT_FUNCTION) call_reader(given);
  raise = fx_getk(W_set_raise, sid);
  if (raise.t != 'n') { *text = raise.s; return TFP_CAMERA_COMPAT_RAISED; }
  *accepted_is_false = fx_getk(W_set_ret, sid).t == 'f';
  issue = fx_getk(W_set_issue, sid);
  *text = issue.t == 's' ? issue.s : NULL;
  return TFP_CAMERA_COMPAT_CALLED;
}
static TfpCameraCompatEnabled f_is_enabled(void *u) {
  (void)u;
  tick();
  fx_note("is_enabled()");
  if (fx_raise(W_x_is_enabled)) return TFP_CAMERA_COMPAT_ENABLED_RAISED;
  if (fx_is(W_c_enabled, 't')) return TFP_CAMERA_COMPAT_ENABLED_TRUE;
  return fx_is(W_c_enabled, 'f') ? TFP_CAMERA_COMPAT_ENABLED_FALSE : TFP_CAMERA_COMPAT_ENABLED_NOT_BOOLEAN;
}
static void f_log(void *u, const char *text) { (void)u; fx_log(text); }

static void put_state(void) {
  fx_put(" | %s %s %s %s | ", tfp_camera_compat_phase_name(compat.phase), fx_bool(compat.conflict), fx_bool(compat.closing),
         compat.has_message ? fx_str(compat.message) : "~");
  if (compat.record) {
    const TfpCameraCompatRecord *r = &compat.records[compat.record - 1];
    fx_put("#%u %s %s %s %s %s", compat.record, vt(r->provider), vt(r->setter), vt(r->previous), fx_bool(r->blocking), fx_bool(r->reading));
  } else fx_put("~");
}
static void fresh(void) {
  if (have_compat) tfp_camera_compat_free(&compat);
  have_compat = false;
  memset(&compat, 0, sizeof compat);
}
static void two(bool first, const char *why) { fx_put("%s %s", fx_bool(first), fx_str(why)); }
static void dispatch(const FxOp *op) {
  const char *why = NULL, *call = op->call;
  depth = 0;
  if (!strcmp(call, "new")) {
    int mode = op->a[0].t == 'd' ? (int)op->a[0].n : 0;
    const char *error = "(unset)";
    fresh();
    memset(&host, 0, sizeof host);
    host.photo_camera_ownership = f_own; host.photo_actioncam_compat = f_api; host.actioncam_loaded = f_actioncam; host.table_field = f_field;
    host.set_input_blocked = f_set; host.previous_reader_call = f_previous;
    host.log = fx_truthy(op->a[2]) ? NULL : f_log;
    no_debug = mode == 2 || mode == 3;
    sync_host();
    if (fx_truthy(op->a[1])) host.controller_is_enabled = NULL;
    have_compat = true;
    if (!tfp_camera_compat_init(&compat, &host, &error)) { fx_put("new raised %s", fx_str(error)); return; }
    fx_put("new");
    put_state();
    return;
  }
  sync_host();
  if (!strcmp(call, "foreign")) { bool r = tfp_camera_compat_foreign(&compat, &why); two(r, why); }
  else if (!strcmp(call, "prepare")) { bool r = tfp_camera_compat_prepare(&compat, &why); two(r, why); }
  else if (!strcmp(call, "stop")) { bool r = tfp_camera_compat_stop(&compat, &why); two(r, why); }
  else if (!strcmp(call, "shutdown")) { bool r = tfp_camera_compat_shutdown(&compat, &why); two(r, why); }
  else if (!strcmp(call, "update")) { bool r = tfp_camera_compat_update(&compat, &why); two(r, why); }
  else if (!strcmp(call, "is_clean")) fx_put("%s", fx_bool(tfp_camera_compat_is_clean(&compat)));
  else if (!strcmp(call, "result")) {
    TfpCameraCompatResult r;
    tfp_camera_compat_result(&compat, &r);
    fx_put("%s %s %s %s %s %s %s %s", tfp_camera_compat_phase_name(r.phase), fx_bool(r.installed), fx_bool(r.blocking), fx_bool(r.foreign),
           fx_str(r.foreign_reason), fx_bool(r.cleanup_pending), fx_str(r.error_message), fx_bool(r.closing));
  } else if (!strcmp(call, "status")) fx_put("%s", fx_str(tfp_camera_compat_status(&compat)));
  else if (!strcmp(call, "shim")) fx_put("%s", fx_bool(tfp_camera_compat_shim(&compat, (uint32_t)op->a[0].n)));
  else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
  put_state();
}
int main(void) {
  int code;
  fx_any = W_x_any;
  code = fx_main("camera_compat", CAMERA_COMPAT_VECTOR_COMMIT, camera_compat_runs, (int)(sizeof camera_compat_runs / sizeof camera_compat_runs[0]),
                 camera_compat_ops, camera_compat_events, camera_compat_slots, W_COUNT, fresh, dispatch);
  fresh();
  return code;
}
