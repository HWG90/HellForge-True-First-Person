#include "native_ads_option.h"
#include <stdio.h>
#include <string.h>

/* A raised Lua error: its text, or the error value nil. */
typedef struct Raised { bool is_nil; char text[TFP_NATIVE_AIM_TEXT]; } Raised;
static Raised shared;              /* what the public source calls hand out */

/* assert(false, message) at retail line `line`: LuaJIT puts the position before a text. */
static bool fail(Raised *e, int line, const char *message) {
  e->is_nil = message == NULL;
  if (message) snprintf(e->text, sizeof e->text, "%s:%d: %s", TFP_NATIVE_AIM_LUA_CHUNK, line, message);
  return false;
}
/* An error raised by the host passes through unchanged. */
static bool raised(Raised *e, const char *text) {
  e->is_nil = text == NULL;
  if (text) snprintf(e->text, sizeof e->text, "%s", text);
  return false;
}
static const char *text_of(const Raised *e) { return e->is_nil ? "nil" : e->text; }

static bool mem_ptr(const TfpNativeAimHost *h, uint64_t at, uint64_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_ptr(h->user, at, out, &why) || raised(e, why);
}
static bool mem_u32(const TfpNativeAimHost *h, uint64_t at, uint32_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_u32(h->user, at, out, &why) || raised(e, why);
}
static bool mem_u8(const TfpNativeAimHost *h, uint64_t at, uint8_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_u8(h->user, at, out, &why) || raised(e, why);
}
static bool mem_read(const TfpNativeAimHost *h, uint64_t at, uint32_t size, uint8_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_read(h->user, at, size, out, &why) || raised(e, why);
}
static bool mem_map(const TfpNativeAimHost *h, uint64_t at, uint32_t id, bool has_limit, uint32_t limit, uint32_t *index, Raised *e) {
  const char *why = NULL;
  return h->backend_map(h->user, at, id, has_limit, limit, index, &why) || raised(e, why);
}

static bool same(const TfpNativeAimRecord *a, const TfpNativeAimRecord *b) {
  return a->key_len == b->key_len && memcmp(a->key, b->key, a->key_len) == 0 && a->at == b->at &&
         memcmp(a->descriptor, b->descriptor, sizeof a->descriptor) == 0;
}
static void make_key(TfpNativeAimRecord *r, const char *kind, uint32_t id) {
  char tail[32];
  int n = snprintf(tail, sizeof tail, ":%s:%u", kind, id);
  memcpy(r->key, r->owner, r->owner_len);
  memcpy(r->key + r->owner_len, tail, (size_t)n);
  r->key_len = r->owner_len + (size_t)n;
}

const TfpNativeAimBuild tfp_native_aim_build_retail = {0x2ce1618, 0x2ce1ea0, {
  {0xa5cff5, 10, {0x8b,0xc8,0x49,0x8b,0x42,0x50,0x0f,0xb6,0x04,0x48}},
  {0xa5cf03, 21, {0x48,0x8b,0x05,0xd6,0x4f,0x28,0x02,0x48,0x8b,0x48,0x40,0xf7,0x81,0xf0,0x00,0x00,0x00,0xfd,0xff,0xff,0xff}},
  {0xa5d0b3, 13, {0x48,0x6b,0xc1,0x38,0x42,0x0f,0xb6,0x84,0x08,0xec,0x02,0x00,0x00}},
}};
/* The first and third witnesses are retail's bytes at this build's addresses; the second is the same
 * `mov rax,[rip+d]` / `mov rcx,[rax+0x40]` / `test dword [rcx+0xf0],0xfffffffd` with this build's
 * displacement (it resolves to the avatar pool global, game+0x3326d20). */
const TfpNativeAimBuild tfp_native_aim_build_25480438 = {0x3326468, 0x3326ce0, {
  {0xa427b5, 10, {0x8b,0xc8,0x49,0x8b,0x42,0x50,0x0f,0xb6,0x04,0x48}},
  {0xa426c3, 21, {0x48,0x8b,0x05,0x56,0x46,0x8e,0x02,0x48,0x8b,0x48,0x40,0xf7,0x81,0xf0,0x00,0x00,0x00,0xfd,0xff,0xff,0xff}},
  {0xa42873, 13, {0x48,0x6b,0xc1,0x38,0x42,0x0f,0xb6,0x84,0x08,0xec,0x02,0x00,0x00}},
}};
static const TfpNativeAimBuild *build_of(const TfpNativeAimHost *h) { return h->build ? h->build : &tfp_native_aim_build_retail; }

/* retail context(s,weapon,read_mode), lines 8-26. */
static bool context(const TfpNativeAimHost *h, const TfpNativeAimSnapshot *s, uint32_t weapon, bool has_mode, uint32_t read_mode,
                    TfpNativeAimRecord *r, Raised *e) {
  uint64_t player, settings, pool, table, descriptor, values, selected;
  uint32_t mode, index, word, entity;
  if (!mem_ptr(h, s->base + build_of(h)->player, &player, e)) return false;
  if (!mem_ptr(h, s->avatar_owner + 0x40, &settings, e)) return false;
  if (has_mode) mode = read_mode;
  else if (!mem_u32(h, settings + 0xf0, &mode, e)) return false;
  if (mode > 3) return fail(e, 10, "Native aim memory mode changed");
  memset(r, 0, sizeof *r);
  r->base = s->base; r->player = player; r->settings = settings; r->mode = mode; r->weapon = weapon;
  r->owner_len = s->player_identity_len < sizeof r->owner ? s->player_identity_len : sizeof r->owner;
  memcpy(r->owner, s->player_identity, r->owner_len);
  if (mode == 1 || mode == 3) {
    if (!mem_ptr(h, s->base + build_of(h)->weapons, &pool, e)) return false;
    if (!mem_map(h, pool + 0x30, weapon, false, 0, &index, e)) return false;
    if (!mem_ptr(h, pool + 0x48, &table, e)) return false;
    if (!mem_ptr(h, table + (uint64_t)index * 8, &descriptor, e)) return false;
    if (!mem_read(h, descriptor, 20, r->descriptor, e)) return false;
    if (!mem_u32(h, descriptor + 8, &word, e)) return false;
    if (word != weapon) return fail(e, 15, "Native weapon aim descriptor changed");
    if (!mem_ptr(h, pool + 0x50, &values, e)) return false;
    r->at = values + (uint64_t)index * 2;
    make_key(r, "weapon", weapon);
  } else {
    if (!mem_u32(h, player + 0x88, &word, e)) return false;
    if (word == 0) return fail(e, 19, "Native local input unavailable");
    if (!mem_ptr(h, player + 0xe8, &selected, e)) return false;
    if (!mem_u32(h, selected + 8, &entity, e)) return false;
    if (!mem_map(h, player + 0xd0, entity, true, 8, &index, e)) return false;
    if (!mem_read(h, selected, 20, r->descriptor, e)) return false;
    r->at = player + 0x2ec + (uint64_t)index * 0x38;
    make_key(r, "input", entity);
  }
  if (!mem_u8(h, r->at, &r->value, e)) return false;
  if (r->value != 0 && r->value != 1) return fail(e, 24, "Native aim preference is not Boolean");
  return true;
}

static bool probe(const TfpNativeAimHost *h, TfpNativeAimRecord *out, Raised *e) {
  const TfpNativeAimBuild *build = build_of(h);
  TfpNativeAimSnapshot s, after;
  TfpNativeAimRecord r, again;
  const char *why = NULL;
  uint8_t bytes[32];
  if (!h->backend_snapshot(h->user, &s, &why)) return fail(e, 28, why);
  if (!(s.safe && (s.camera_kind == 6 || s.camera_kind == 4))) return fail(e, 28, "Native aim owner unavailable");
  for (size_t i = 0; i < sizeof build->witnesses / sizeof build->witnesses[0]; i++) {
    if (!mem_read(h, s.base + build->witnesses[i].at, build->witnesses[i].size, bytes, e)) return false;
    if (memcmp(bytes, build->witnesses[i].bytes, build->witnesses[i].size) != 0) return fail(e, 31, "Native aim selection contract changed");
  }
  if (!context(h, &s, s.weapon, false, 0, &r, e)) return false;
  why = NULL;
  if (!h->backend_snapshot(h->user, &after, &why)) return fail(e, 33, why);
  if (!(after.identity_len == s.identity_len && memcmp(after.identity, s.identity, s.identity_len) == 0 &&
        memcmp(after.weapon_descriptor, s.weapon_descriptor, sizeof s.weapon_descriptor) == 0))
    return fail(e, 34, "Native aim owner changed during read");
  if (!context(h, &after, after.weapon, false, 0, &again, e)) return false;
  if (!same(&r, &again)) return fail(e, 34, "Native aim owner changed during read");
  *out = r;
  return true;
}

static TfpNativeAimRead read_value(const TfpNativeAimHost *h, const TfpNativeAimRecord *r, uint8_t *value, Raised *e) {
  TfpNativeAimSnapshot s;
  TfpNativeAimRecord current;
  const char *why = NULL;
  bool got = h->backend_pin_first_person && h->backend_pin_player_snapshot ? h->backend_pin_player_snapshot(h->user, &s, &why)
                                                                          : h->backend_player_snapshot(h->user, &s, &why);
  if (!got) { fail(e, 44, why); return TFP_NATIVE_AIM_READ_RAISED; }
  if (s.player_identity_len != r->owner_len || memcmp(s.player_identity, r->owner, r->owner_len) != 0 || s.base != r->base)
    return TFP_NATIVE_AIM_READ_RETIRED;
  if (!context(h, &s, r->weapon, true, r->mode, &current, e)) return TFP_NATIVE_AIM_READ_RAISED;
  if (!same(&current, r)) return TFP_NATIVE_AIM_READ_RETIRED;
  *value = current.value;
  return TFP_NATIVE_AIM_READ_VALUE;
}

static bool write_value(const TfpNativeAimHost *h, const TfpNativeAimRecord *r, uint8_t expected, uint8_t value, Raised *e) {
  uint8_t now = 0;
  const char *why = NULL;
  TfpNativeAimRead got = read_value(h, r, &now, e);
  if (got == TFP_NATIVE_AIM_READ_RAISED) return false;
  if (got != TFP_NATIVE_AIM_READ_VALUE || now != expected) return fail(e, 51, "Native aim preference changed before write");
  if (h->backend_foreign_camera && h->backend_foreign_camera(h->user)) return fail(e, 52, "Another camera owns the view");
  switch (h->memory_copy(h->user, r->at, value, &why)) {
    case TFP_NATIVE_AIM_COPY_DONE: break;
    case TFP_NATIVE_AIM_COPY_REFUSED: return fail(e, 54, why);
    default: return raised(e, why);
  }
  got = read_value(h, r, &now, e);
  if (got == TFP_NATIVE_AIM_READ_RAISED) return false;
  if (got != TFP_NATIVE_AIM_READ_VALUE || now != value) return fail(e, 55, "Native aim preference readback differs");
  return true;
}

static void hand(const Raised *e, const char **error) { if (error) *error = e->is_nil ? NULL : e->text; }
bool tfp_native_aim_source_probe(const TfpNativeAimHost *host, TfpNativeAimRecord *out, const char **error) {
  if (probe(host, out, &shared)) return true;
  hand(&shared, error);
  return false;
}
TfpNativeAimRead tfp_native_aim_source_read(const TfpNativeAimHost *host, const TfpNativeAimRecord *r, uint8_t *value, const char **error) {
  TfpNativeAimRead got = read_value(host, r, value, &shared);
  if (got == TFP_NATIVE_AIM_READ_RAISED) hand(&shared, error);
  return got;
}
bool tfp_native_aim_source_write(const TfpNativeAimHost *host, const TfpNativeAimRecord *r, uint8_t expected, uint8_t value,
                                 const char **error) {
  if (write_value(host, r, expected, value, &shared)) return true;
  hand(&shared, error);
  return false;
}

void tfp_native_aim_init(TfpNativeAim *a, const TfpNativeAimHost *host) {
  memset(a, 0, sizeof *a);
  a->host = host;
  a->phase = TFP_NATIVE_AIM_IDLE;
}
static void set_error(TfpNativeAim *a, const char *text) {
  a->has_error = text != NULL;
  if (text && text != a->error_message) snprintf(a->error_message, sizeof a->error_message, "%s", text);
}
static bool answer(TfpNativeAim *a, bool value, const char **why) {
  if (why) *why = a->has_error ? a->error_message : NULL;
  return value;
}

/* The protected body of stop(), retail lines 65-69. */
static bool release(TfpNativeAim *a, Raised *e) {
  const TfpNativeAimHost *h = a->host;
  uint8_t value = 0;
  TfpNativeAimRead got;
  if (h->foreign && h->foreign(h->user)) return fail(e, 65, "Another camera owns the view");
  got = read_value(h, &a->lease, &value, e);
  if (got == TFP_NATIVE_AIM_READ_RAISED) return false;
  if (got == TFP_NATIVE_AIM_READ_VALUE && value == 1) {
    if (!write_value(h, &a->lease, 1, a->lease.before, e)) return false;
    a->restores++;
  }
  a->has_lease = false;
  return true;
}
bool tfp_native_aim_stop(TfpNativeAim *a, const char **why) {
  Raised e;
  bool ok;
  if (!a->has_lease) {
    a->phase = TFP_NATIVE_AIM_IDLE;
    if (why) *why = NULL;
    return true;
  }
  ok = release(a, &e);
  a->phase = ok ? TFP_NATIVE_AIM_IDLE : TFP_NATIVE_AIM_CLEANUP_PENDING;
  set_error(a, ok ? NULL : text_of(&e));
  return answer(a, ok, why);
}
bool tfp_native_aim_shutdown(TfpNativeAim *a, const char **why) {
  a->closing = true;
  return tfp_native_aim_stop(a, why);
}

/* The protected body of update(), retail lines 79-85. */
static bool acquire(TfpNativeAim *a, Raised *e) {
  TfpNativeAimRecord r;
  if (!probe(a->host, &r, e)) return false;
  if (a->has_lease && !same(&a->lease, &r)) {
    const char *problem = NULL;
    if (!tfp_native_aim_stop(a, &problem)) return fail(e, 80, problem);
  }
  if (r.value == 0) {
    if (!a->has_lease) { a->lease = r; a->has_lease = true; }
    a->lease.has_before = true; a->lease.before = 0;
    if (!write_value(a->host, &a->lease, 0, 1, e)) return false;
    a->writes++;
  }
  a->phase = TFP_NATIVE_AIM_ACTIVE;
  set_error(a, NULL);
  return true;
}
bool tfp_native_aim_update(TfpNativeAim *a, const char **why) {
  const TfpNativeAimHost *h = a->host;
  TfpNativeAimControllerState state = {false};
  Raised e;
  if (a->closing || !h->enabled(h->user) || !h->controller_is_enabled(h->user)) return tfp_native_aim_stop(a, why);
  h->controller_result(h->user, &state);
  if (!state.pose_offset) return tfp_native_aim_stop(a, why);
  if (h->foreign && h->foreign(h->user)) {
    a->phase = TFP_NATIVE_AIM_SUSPENDED;
    if (why) *why = NULL;
    return true;
  }
  if (!acquire(a, &e)) {
    a->phase = TFP_NATIVE_AIM_WAITING;
    set_error(a, text_of(&e));
  }
  return answer(a, true, why);
}
void tfp_native_aim_result(const TfpNativeAim *a, TfpNativeAimResult *out) {
  out->phase = a->phase; out->owned = a->has_lease; out->writes = a->writes; out->restores = a->restores;
  out->error_message = a->has_error ? a->error_message : NULL;
}
const char *tfp_native_aim_phase_name(TfpNativeAimPhase phase) {
  static const char *const names[] = {"idle", "active", "suspended", "waiting", "cleanup_pending"};
  return names[phase];
}
