/* Replays tests/vectors/native_ads_option_vectors.h (the retail native_ads_option.lua's own answers)
 * against src/native_ads_option.c. The fakes are those of tools/retail_ref/run_native_ads_option.lua. */
#include "native_ads_option.h"
#include "effects_fx.h"
#include "vectors/native_ads_option_vectors.h"

static TfpNativeAimHost host;
static TfpNativeAim lease;
static TfpNativeAimRecord records[3];
static bool have_record[3];

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
static bool f_u8(void *u, uint64_t address, uint8_t *out, const char **error) {
  (void)u;
  return mem("u8", address, 1, out, error, W_x_u8, false);
}
static bool f_map(void *u, uint64_t address, uint32_t id, bool has_limit, uint32_t limit, uint32_t *index, const char **error) {
  FxValue v;
  (void)u;
  fx_tick();
  fx_note("map(%s %s %s)", fx_num((double)address), fx_num(id), has_limit ? fx_num(limit) : "~");
  if ((*error = fx_raise(W_x_map))) return false;
  v = fx_getk(W_map, (double)(address % 65536) * 4294967296.0 + id);
  if (v.t != 'd') { *error = "Entity absent"; return false; }
  if (v.n >= (has_limit ? limit : 4096)) { *error = "Entity index out of bounds"; return false; }
  *index = (uint32_t)v.n;
  return true;
}
static void text(char *out, size_t *len, int slot) {
  *len = fx_is(slot, 's') ? (size_t)fx_world[slot].len : 0;
  memcpy(out, fx_world[slot].s, *len);
}
static bool snapshot(const char *name, bool full, TfpNativeAimSnapshot *s, const char **why) {
  fx_tick();
  fx_note("%s()", name);
  memset(s, 0, sizeof *s);
  if (!fx_is(full ? W_s_ok : W_p_ok, 't')) { *why = fx_s(full ? W_s_why : W_p_why); return false; }
  s->base = (uint64_t)fx_n(full ? W_s_base : W_p_base);
  s->avatar_owner = (uint64_t)fx_n(full ? W_s_owner : W_p_owner);
  text(s->player_identity, &s->player_identity_len, full ? W_s_pid : W_p_pid);
  if (full) {
    size_t n;
    char wd[TFP_NATIVE_AIM_IDENTITY];
    text(s->identity, &s->identity_len, W_s_id);
    text(wd, &n, W_s_wd);
    memcpy(s->weapon_descriptor, wd, 20);
    s->weapon = (uint32_t)fx_n(W_s_weapon); s->safe = fx_truthy(fx_world[W_s_safe]); s->camera_kind = (int)fx_n(W_s_kind);
  }
  return true;
}
static bool f_snapshot(void *u, TfpNativeAimSnapshot *s, const char **why) { (void)u; return snapshot("snapshot", true, s, why); }
static bool f_player(void *u, TfpNativeAimSnapshot *s, const char **why) { (void)u; return snapshot("player_snapshot", false, s, why); }
static bool f_pin(void *u, TfpNativeAimSnapshot *s, const char **why) { (void)u; return snapshot("pin_player_snapshot", false, s, why); }
static bool f_foreign_camera(void *u) { (void)u; fx_tick(); fx_note("foreign_camera()"); return !fx_is(W_fc, 'f'); }
static TfpNativeAimCopy f_copy(void *u, uint64_t at, uint8_t value, const char **why) {
  int effect;
  unsigned char byte = value;
  (void)u;
  fx_tick();
  fx_note("copy(%s %s)", fx_num((double)at), fx_strn((const char *)&byte, 1));
  if ((*why = fx_raise(W_x_copy))) return TFP_NATIVE_AIM_COPY_RAISED;
  if (!fx_is(W_copy_ok, 't')) { *why = fx_s(W_copy_why); return TFP_NATIVE_AIM_COPY_REFUSED; }
  effect = fx_is(W_copy_effect, 'd') ? (int)fx_n(W_copy_effect) : 0;
  if (effect == 2) byte = (unsigned char)(1 - byte);
  if (effect != 1) fx_write((double)at, &byte, 1);
  return TFP_NATIVE_AIM_COPY_DONE;
}
static bool f_enabled(void *u) { (void)u; fx_tick(); fx_note("enabled()"); return fx_truthy(fx_world[W_enabled]); }
static bool f_foreign(void *u) { (void)u; fx_tick(); fx_note("foreign()"); return !fx_is(W_foreign, 'f'); }
static bool f_is_enabled(void *u) { (void)u; fx_tick(); fx_note("is_enabled()"); return fx_truthy(fx_world[W_c_enabled]); }
static void f_result(void *u, TfpNativeAimControllerState *out) { (void)u; fx_tick(); fx_note("result()"); out->pose_offset = fx_truthy(fx_world[W_c_pose]); }

static void fresh(void) {
  memset(&host, 0, sizeof host);
  host.backend_snapshot = f_snapshot; host.backend_player_snapshot = f_player; host.backend_read = f_read; host.backend_ptr = f_ptr;
  host.backend_u32 = f_u32; host.backend_u8 = f_u8; host.backend_map = f_map; host.memory_copy = f_copy; host.enabled = f_enabled;
  host.controller_is_enabled = f_is_enabled; host.controller_result = f_result;
  memset(have_record, 0, sizeof have_record);
  tfp_native_aim_init(&lease, &host);
}
static void put_record(const TfpNativeAimRecord *r) {
  if (!r) { fx_put("~"); return; }
  fx_put("%s %s %s %u %s %u %s %s %s %u %s", fx_num((double)r->base), fx_num((double)r->player), fx_num((double)r->settings), r->mode,
         fx_strn(r->owner, r->owner_len), r->weapon, fx_num((double)r->at), fx_strn(r->key, r->key_len),
         fx_strn((const char *)r->descriptor, 20), r->value, r->has_before ? fx_num(r->before) : "~");
}
static void dispatch(const FxOp *op) {
  const char *why = "(unset)", *call = op->call;
  int slot = (int)op->a[0].n;
  bool direct = false;
  if (!strcmp(call, "new")) { fresh(); host.foreign = fx_truthy(op->a[0]) ? f_foreign : NULL; }
  host.backend_pin_first_person = fx_is(W_pin_first, 't');
  host.backend_pin_player_snapshot = fx_truthy(fx_world[W_pin_fn]) ? f_pin : NULL;
  host.backend_foreign_camera = fx_truthy(fx_world[W_has_fc]) ? f_foreign_camera : NULL;
  if (!strcmp(call, "new")) fx_put("new");
  else if (!strcmp(call, "update")) { bool r = tfp_native_aim_update(&lease, &why); fx_put("%s %s", fx_bool(r), fx_str(why)); }
  else if (!strcmp(call, "stop")) { bool r = tfp_native_aim_stop(&lease, &why); fx_put("%s %s", fx_bool(r), fx_str(why)); }
  else if (!strcmp(call, "shutdown")) { bool r = tfp_native_aim_shutdown(&lease, &why); fx_put("%s %s", fx_bool(r), fx_str(why)); }
  else if (!strcmp(call, "probe")) {
    direct = true;
    if (tfp_native_aim_source_probe(&host, &records[slot], &why)) { have_record[slot] = true; fx_put("ok"); }
    else fx_put("raised %s", fx_str(why));
  } else if (!strcmp(call, "read")) {
    uint8_t value = 0;
    TfpNativeAimRead got = tfp_native_aim_source_read(&host, &records[slot], &value, &why);
    direct = true;
    if (got == TFP_NATIVE_AIM_READ_VALUE) fx_put("%u ~", value);
    else if (got == TFP_NATIVE_AIM_READ_RETIRED) fx_put("~ \"retired\"");
    else fx_put("raised %s", fx_str(why));
  } else if (!strcmp(call, "write")) {
    direct = true;
    if (tfp_native_aim_source_write(&host, &records[slot], (uint8_t)op->a[1].n, (uint8_t)op->a[2].n, &why)) fx_put("ok");
    else fx_put("raised %s", fx_str(why));
  } else { fprintf(stderr, "unknown call %s\n", call); exit(2); }
  if (direct) {
    fx_put(" | ");
    put_record(have_record[slot] ? &records[slot] : NULL);
  } else {
    TfpNativeAimResult r;
    tfp_native_aim_result(&lease, &r);
    fx_put(" | %s %s %s %u %u %s %s | ", tfp_native_aim_phase_name(r.phase), fx_bool(tfp_native_aim_is_clean(&lease)), fx_bool(r.owned),
           r.writes, r.restores, fx_str(r.error_message), fx_str(tfp_native_aim_phase_name(lease.phase)));
    put_record(lease.has_lease ? &lease.lease : NULL);
  }
}
int main(void) {
  fx_mem = W_mem; fx_any = W_x_any;
  return fx_main("native_ads_option", NATIVE_ADS_OPTION_VECTOR_COMMIT, native_ads_option_runs,
                 (int)(sizeof native_ads_option_runs / sizeof native_ads_option_runs[0]), native_ads_option_ops, native_ads_option_events,
                 native_ads_option_slots, W_COUNT, fresh, dispatch);
}
