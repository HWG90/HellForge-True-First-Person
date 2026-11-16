/* Replays tests/vectors/first_person_movement_vectors.h (the retail first_person_movement.lua's own answers)
 * against src/first_person_movement.c. The fakes are those of tools/retail_ref/run_first_person_movement.lua. */
#include "first_person_movement.h"
#include "effects_fx.h"
#include "vectors/first_person_movement_vectors.h"

static TfpMovementHost host;
static TfpMovement movement;

static bool mem(const char *name, uint64_t address, int size, unsigned char *out, const char **error, int raise_slot, bool with_size) {
  fx_tick();
  if (with_size) fx_note("%s(%s %d)", name, fx_num((double)address), size);
  else fx_note("%s(%s)", name, fx_num((double)address));
  if ((*error = fx_raise(raise_slot))) return false;
  if (!fx_read((double)address, size, out)) { *error = "Memory unavailable"; return false; }
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
static bool f_u32(void *u, uint64_t address, uint32_t *out, const char **error) {
  unsigned char b[4];
  (void)u;
  if (!mem("u32", address, 4, b, error, W_x_u32, false)) return false;
  *out = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
  return true;
}
static double serial(const void *handle) { return (double)(uintptr_t)handle; }
static TfpMovementCall snapshot(const char *name, int raise_slot, int ok, int why_slot, TfpMovementSnapshot *s, const char **why) {
  fx_tick();
  fx_note("%s()", name);
  memset(s, 0, sizeof *s);
  if ((*why = fx_raise(raise_slot))) return TFP_MOVEMENT_RAISED;
  if (!fx_is(ok, 't')) { *why = fx_s(why_slot); return TFP_MOVEMENT_REFUSED; }
  s->handle = (void *)(uintptr_t)fx_calls;
  s->base = (uint64_t)fx_n(W_s_base); s->world = (uint64_t)fx_n(W_s_world); s->avatar_owner = (uint64_t)fx_n(W_s_owner);
  s->avatar_index = (uint32_t)fx_n(W_s_index); s->avatar = (uint32_t)fx_n(W_s_avatar); s->body = (uint32_t)fx_n(W_s_body);
  s->camera_kind = (uint32_t)fx_n(W_s_kind);
  if (fx_is(W_s_desc, 's') && fx_world[W_s_desc].len == 20) memcpy(s->avatar_descriptor, fx_world[W_s_desc].s, 20);
  s->safe = fx_truthy(fx_world[W_s_safe]); s->seated = fx_truthy(fx_world[W_s_seated]); s->controller_active = fx_truthy(fx_world[W_s_ctl]);
  return TFP_MOVEMENT_DONE;
}
static TfpMovementCall f_player(void *u, TfpMovementSnapshot *s, const char **why) {
  (void)u;
  return snapshot("player_snapshot", W_x_player_snapshot, W_s_ok, W_s_why, s, why);
}
static TfpMovementCall f_pin(void *u, TfpMovementSnapshot *s, const char **why) {
  (void)u;
  return snapshot("pin_player_snapshot", W_x_pin_player_snapshot, W_p_ok, W_p_why, s, why);
}
static bool f_action_state(void *u, const TfpMovementSnapshot *s, const void **action, const char **error) {
  (void)u;
  fx_tick();
  fx_note("action_state(%s)", fx_num(serial(s->handle)));
  if ((*error = fx_raise(W_x_action_state))) return false;
  *action = (const void *)(uintptr_t)fx_calls;
  return true;
}
static bool f_on_foot(void *u, const TfpMovementSnapshot *s, const void *action, bool *on_foot, const char **error) {
  (void)u;
  fx_tick();
  fx_note("on_foot(%s %s)", fx_num(serial(s->handle)), fx_num(serial(action)));
  if ((*error = fx_raise(W_x_on_foot))) return false;
  *on_foot = fx_is(W_on_foot, 't');
  return true;
}
static bool f_foreign_camera(void *u, TfpMovementForeign *out, const char **error) {
  (void)u;
  fx_tick();
  fx_note("foreign_camera()");
  if ((*error = fx_raise(W_x_foreign_camera))) return false;
  *out = fx_is(W_fc, 't') ? TFP_MOVEMENT_FOREIGN_TRUE : fx_is(W_fc, 'f') ? TFP_MOVEMENT_FOREIGN_FALSE : TFP_MOVEMENT_FOREIGN_OTHER;
  return true;
}
static double slot_or(int slot, double fallback) { return fx_is(slot, 'd') ? fx_n(slot) : fallback; }
/* What the write stand-ins do to the byte: 0 store it, 1 nothing, 2 store another value. */
static void effect(uint64_t address, uint8_t byte) {
  int kind = (int)slot_or(W_w_effect, 0);
  unsigned char value = kind == 2 ? (unsigned char)(byte + 2) : byte;
  if (kind == 0 || kind == 2) fx_write((double)address, &value, 1);
}
static size_t f_query(void *u, uint64_t address, TfpMovementRegion *r, size_t size) {
  (void)u;
  fx_tick();
  fx_note("virtual_query(%s)", fx_num((double)address));
  r->base = (uint64_t)slot_or(W_q_base, 0); r->size = (uint64_t)slot_or(W_q_size, 0); r->state = (uint32_t)slot_or(W_q_state, 0);
  r->protect = (uint32_t)slot_or(W_q_protect, 0); r->kind = (uint32_t)slot_or(W_q_kind, 0);
  return (size_t)slot_or(W_q_ret, (double)size);
}
static int f_wpm(void *u, uint64_t address, const uint8_t *bytes, size_t size, size_t *written) {
  (void)u;
  if (size != 1) { fprintf(stderr, "write_process_memory: size %zu\n", size); exit(2); }
  fx_tick();
  fx_note("write_process_memory(%s %u)", fx_num((double)address), bytes[0]);
  effect(address, bytes[0]);
  if (!fx_is(W_w_count, 'f')) *written = (size_t)slot_or(W_w_count, 1);      /* false: the count is left as it was */
  return (int)slot_or(W_w_ret, 1);
}
static bool find(const char *name, int raise_slot, const char *what, TfpMovementNumber *key, const char **error) {
  fx_tick();
  fx_note("%s(%s)", name, fx_str(what));
  if ((*error = fx_raise(raise_slot))) return false;
  key->has = fx_is(W_key, 'd'); key->value = fx_n(W_key);
  return true;
}
static bool f_button_index(void *u, const char *what, TfpMovementNumber *key, const char **error) {
  (void)u;
  return find("button_index", W_x_button_index, what, key, error);
}
static bool f_button_id(void *u, const char *what, TfpMovementNumber *key, const char **error) {
  (void)u;
  return find("button_id", W_x_button_id, what, key, error);
}
static bool f_button(void *u, double key, TfpMovementNumber *value, const char **error) {
  (void)u;
  fx_tick();
  fx_note("button(%s)", fx_num(key));
  if ((*error = fx_raise(W_x_button))) return false;
  value->has = fx_is(W_s_down, 'd'); value->value = fx_n(W_s_down);
  return true;
}
static bool f_mouse_focus(void *u, bool *focus, const char **error) {
  (void)u;
  fx_tick();
  fx_note("mouse_focus()");
  if ((*error = fx_raise(W_x_mouse_focus))) return false;
  *focus = fx_is(W_mf, 't');
  return true;
}
static bool f_show_cursor(void *u, bool *shown, const char **error) {
  (void)u;
  fx_tick();
  fx_note("show_cursor()");
  if ((*error = fx_raise(W_x_show_cursor))) return false;
  *shown = !fx_is(W_sc, 'f');
  return true;
}
static bool f_is_enabled(void *u) { (void)u; fx_tick(); fx_note("is_enabled()"); return fx_truthy(fx_world[W_c_enabled]); }
static bool f_result(void *u) { (void)u; fx_tick(); fx_note("result()"); return fx_truthy(fx_world[W_c_pose]); }
static bool f_focus_owned(void *u, const TfpMovementSnapshot *s, bool *owned, const char **error) {
  (void)u;
  fx_tick();
  fx_note("focus_owned(%s)", fx_num(serial(s->handle)));
  if ((*error = fx_raise(W_x_focus_owned))) return false;
  *owned = fx_is(W_c_focus, 't');
  return true;
}
static bool f_fallback_ready(void *u, const TfpMovementSnapshot *s) {
  (void)u;
  fx_tick();
  fx_note("fallback_ready(%s)", fx_num(serial(s->handle)));
  return fx_is(W_fb, 't');
}
static bool f_writable(void *u, uint64_t address, bool *answer, const char **error) {
  (void)u;
  fx_tick();
  fx_note("writable(%s)", fx_num((double)address));
  if ((*error = fx_raise(W_x_writable))) return false;
  *answer = fx_is(W_dw, 't');
  return true;
}
static bool f_write(void *u, uint64_t address, uint8_t value, bool *answer, const char **error) {
  (void)u;
  fx_tick();
  fx_note("write(%s %u)", fx_num((double)address), value);
  if ((*error = fx_raise(W_x_write))) return false;
  effect(address, value);
  *answer = fx_is(W_dwr, 't');
  return true;
}
static void f_log(void *u, const char *text) { (void)u; fx_log(text); }

static void fresh(void) {
  memset(&host, 0, sizeof host);
  host.backend_player_snapshot = f_player; host.backend_read = f_read; host.backend_ptr = f_ptr; host.backend_u32 = f_u32;
  host.backend_action_state = f_action_state; host.backend_on_foot = f_on_foot;
  host.windows_virtual_query = f_query; host.windows_write_process_memory = f_wpm;
  host.controller_result_pose_offset = f_result;
  tfp_movement_init(&movement, &host);
}
/* Fields the module reads at call time follow the world; they are set before each operation. */
static void sync(void) {
  bool t_button = fx_truthy(fx_world[W_kb_button]), t_index = fx_truthy(fx_world[W_kb_index]), t_bid = fx_truthy(fx_world[W_kb_bid]);
  host.backend_pin_first_person = fx_is(W_pin_first, 't');
  host.backend_pin_player_snapshot = fx_truthy(fx_world[W_pin_fn]) ? f_pin : NULL;
  host.backend_foreign_camera = fx_truthy(fx_world[W_has_fc]) ? f_foreign_camera : NULL;
  if (fx_truthy(fx_world[W_kb])) {
    host.engine_keyboard = (uint64_t)fx_n(W_kb) * 8 + (t_button ? 1 : 0) + (t_index ? 2 : 0) + (t_bid ? 4 : 0);
    host.keyboard_button = t_button ? f_button : NULL;
    host.keyboard_button_index = t_index ? f_button_index : NULL;
    host.keyboard_button_id = t_bid ? f_button_id : NULL;
  } else {
    host.engine_keyboard = 0;
    host.keyboard_button = NULL; host.keyboard_button_index = NULL; host.keyboard_button_id = NULL;
  }
  host.window_mouse_focus = fx_truthy(fx_world[W_has_mf]) ? f_mouse_focus : NULL;
  host.window_show_cursor = fx_truthy(fx_world[W_has_sc]) ? f_show_cursor : NULL;
  host.controller_is_enabled = fx_truthy(fx_world[W_c_has]) ? f_is_enabled : NULL;
  host.controller_focus_owned = fx_truthy(fx_world[W_c_has_focus]) ? f_focus_owned : NULL;
  host.deps_fallback_ready = fx_truthy(fx_world[W_has_fb]) ? f_fallback_ready : NULL;
  host.deps_writable = fx_truthy(fx_world[W_has_dw]) ? f_writable : NULL;
  host.deps_write = fx_truthy(fx_world[W_has_dwr]) ? f_write : NULL;
}
static void put_record(const TfpMovementRecord *r) {
  if (!r) { fx_put("~"); return; }
  fx_put("%s %s %s %u %u %u ", fx_num((double)r->base), fx_num((double)r->world), fx_num((double)r->avatar_owner), r->avatar_index, r->avatar,
         r->body);
  fx_put("%s %s %s %u ", fx_num((double)r->descriptor), fx_strn((const char *)r->descriptor_bytes, 20), fx_num((double)r->pool), r->index);
  fx_put("%s %s %s %s", fx_num((double)r->descriptors), fx_num((double)r->rows), fx_num((double)r->address), fx_strn((const char *)&r->before, 1));
}
static void dispatch(const FxOp *op) {
  const char *why = "(unset)", *call = op->call;
  bool skip = fx_truthy(op->a[0]);
  TfpMovementResult r;
  char status[128];
  if (!strcmp(call, "new")) { fresh(); host.log = fx_truthy(op->a[0]) ? f_log : NULL; }
  sync();
  if (!strcmp(call, "new")) fx_put("new");
  else {
    bool done;
    if (!strcmp(call, "update")) done = tfp_movement_update(&movement, skip, &why);
    else if (!strcmp(call, "update_fird")) done = tfp_movement_update_fird(&movement, skip, &why);
    else if (!strcmp(call, "stop")) done = tfp_movement_stop(&movement, &why);
    else if (!strcmp(call, "shutdown")) done = tfp_movement_shutdown(&movement, &why);
    else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
    fx_put("%s %s", fx_bool(done), fx_str(why));
  }
  tfp_movement_result(&movement, &r);
  tfp_movement_status(&movement, status, sizeof status);
  fx_put(" | %s %s %u %u %u %s %s ", fx_bool(tfp_movement_is_clean(&movement)), tfp_movement_phase_name(r.phase), r.writes, r.restores,
         r.foreign_changes, fx_str(r.reason), fx_str(tfp_movement_scope_name(r.scope)));
  fx_put("%s %s %s %s | %s %s %s %s | ", fx_bool(r.owned), fx_bool(r.cleanup_pending), fx_bool(r.closing), fx_str(status),
         fx_bool(movement.draining), movement.has_verified ? fx_num((double)movement.verified) : "~",
         movement.key.has ? fx_num(movement.key.value) : "~", movement.keyboard ? fx_num((double)movement.keyboard) : "~");
  put_record(movement.has_owned ? &movement.owned : NULL);
}
int main(void) {
  fx_mem = W_mem; fx_any = W_x_any;
  return fx_main("first_person_movement", FIRST_PERSON_MOVEMENT_VECTOR_COMMIT, first_person_movement_runs,
                 (int)(sizeof first_person_movement_runs / sizeof first_person_movement_runs[0]), first_person_movement_ops,
                 first_person_movement_events, first_person_movement_slots, W_COUNT, fresh, dispatch);
}
