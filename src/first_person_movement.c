#include "first_person_movement.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A raised Lua error: its text ("nil" for an error value of nil). */
typedef struct Raised { char text[TFP_MOVEMENT_TEXT]; } Raised;

/* assert(false, message) at retail line `line`: LuaJIT puts the position before the text. */
static bool fail(Raised *e, int line, const char *message) {
  snprintf(e->text, sizeof e->text, "%s:%d: %s", TFP_MOVEMENT_LUA_CHUNK, line, message);
  return false;
}
/* An error raised by the host passes through unchanged. */
static bool raised(Raised *e, const char *text) {
  snprintf(e->text, sizeof e->text, "%s", text ? text : "nil");
  return false;
}

static bool mem_read(const TfpMovementHost *h, uint64_t at, uint32_t size, uint8_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_read(h->user, at, size, out, &why) || raised(e, why);
}
static bool mem_ptr(const TfpMovementHost *h, uint64_t at, uint64_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_ptr(h->user, at, out, &why) || raised(e, why);
}
static bool mem_u32(const TfpMovementHost *h, uint64_t at, uint32_t *out, Raised *e) {
  const char *why = NULL;
  return h->backend_u32(h->user, at, out, &why) || raised(e, why);
}
/* retail uint(bytes,offset), line 16. */
static uint32_t le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* The six code fingerprints (retail `anchors`, lines 8-15) and the pool global. */
const TfpMovementBuild tfp_movement_build_retail = {{
  {0x9de28c, 7, {0x4c,0x8b,0x1d,0x25,0x3c,0x30,0x02}},
  {0x9de309, 24, {0x8b,0xc8,0x49,0x8b,0x43,0x50,0x48,0x8d,0x14,0x89,0x48,0xc1,0xe2,0x05,0x44,0x88,0xb4,0x02,0x94,0x00,0x00,0x00,0x41,0x5e}},
  {0x829b92, 23, {0x8b,0xc3,0x48,0x8d,0x0c,0x80,0x48,0xc1,0xe1,0x05,0x42,0x80,0xbc,0x31,0x94,0x00,0x00,0x00,0x00,0x74,0xce,0xb0,0x01}},
  {0x9dd574, 29, {0x48,0x8b,0x69,0x50,0x48,0x8b,0xf1,0x8b,0xda,0x48,0x8d,0x3c,0x9b,0x48,0xc1,0xe7,0x05,0x80,0xbc,0x2f,0x94,0x00,0x00,0x00,0x00,0x74,0x07,0xb0,0x01}},
  {0x9dc7db, 25, {0x48,0x63,0xd3,0x48,0xc1,0xe2,0x05,0x42,0x0f,0xb6,0x44,0x2a,0x0c,0x42,0x8b,0x0c,0x2a,0x43,0x88,0x84,0x37,0x93,0x00,0x00,0x00}},
  {0x839073, 49, {0x48,0x8b,0xd6,0x49,0x8b,0xcd,0xe8,0x02,0x08,0xff,0xff,0x84,0xc0,0x74,0x48,0x48,0x8b,0x45,0x38,0xf3,0x42,0x0f,0x10,0xbc,0x28,0x1c,0x7c,0x0a,0x00,0xf3,0x42,0x0f,0x10,0xb4,0x28,0x18,0x7c,0x0a,0x00,0xf3,0x46,0x0f,0x10,0x84,0x28,0x20,0x7c,0x0a,0x00}},
}, 0x2ce1eb8};
/* Anchors 1 to 4 are retail's bytes at this build's addresses. Anchor 0 is the same `mov r11,[rip+d]`
 * with this build's displacement (it resolves to the pool global below); anchor 5 differs in the call
 * displacement (it resolves to the function anchor 2 sits in, at the same offset as in retail) and in
 * one stack slot (`mov rax,[rbp+0x48]`, retail `[rbp+0x38]`). */
const TfpMovementBuild tfp_movement_build_25480438 = {{
  {0x9cc2ec, 7, {0x4c,0x8b,0x1d,0x05,0xaa,0x95,0x02}},
  {0x9cc369, 24, {0x8b,0xc8,0x49,0x8b,0x43,0x50,0x48,0x8d,0x14,0x89,0x48,0xc1,0xe2,0x05,0x44,0x88,0xb4,0x02,0x94,0x00,0x00,0x00,0x41,0x5e}},
  {0x81ce72, 23, {0x8b,0xc3,0x48,0x8d,0x0c,0x80,0x48,0xc1,0xe1,0x05,0x42,0x80,0xbc,0x31,0x94,0x00,0x00,0x00,0x00,0x74,0xce,0xb0,0x01}},
  {0x9cb5d4, 29, {0x48,0x8b,0x69,0x50,0x48,0x8b,0xf1,0x8b,0xda,0x48,0x8d,0x3c,0x9b,0x48,0xc1,0xe7,0x05,0x80,0xbc,0x2f,0x94,0x00,0x00,0x00,0x00,0x74,0x07,0xb0,0x01}},
  {0x9ca83b, 25, {0x48,0x63,0xd3,0x48,0xc1,0xe2,0x05,0x42,0x0f,0xb6,0x44,0x2a,0x0c,0x42,0x8b,0x0c,0x2a,0x43,0x88,0x84,0x37,0x93,0x00,0x00,0x00}},
  {0x82c1e3, 49, {0x48,0x8b,0xd6,0x49,0x8b,0xcd,0xe8,0x72,0x09,0xff,0xff,0x84,0xc0,0x74,0x48,0x48,0x8b,0x45,0x48,0xf3,0x42,0x0f,0x10,0xbc,0x28,0x1c,0x7c,0x0a,0x00,0xf3,0x42,0x0f,0x10,0xb4,0x28,0x18,0x7c,0x0a,0x00,0xf3,0x46,0x0f,0x10,0x84,0x28,0x20,0x7c,0x0a,0x00}},
}, 0x3326cf8};
static const TfpMovementBuild *build_of(const TfpMovementHost *h) { return h->build ? h->build : &tfp_movement_build_retail; }

/* retail report(phase,why), lines 31-34. */
static void report(TfpMovement *m, TfpMovementPhase phase, const char *why) {
  if (why && !(m->has_reason && strcmp(why, m->reason) == 0) && m->host->log) {
    char line[TFP_MOVEMENT_TEXT + 32];
    snprintf(line, sizeof line, "First-person movement: %s", why);
    m->host->log(m->host->user, line);
  }
  m->phase = phase;
  m->has_reason = why != NULL;
  if (why && why != m->reason) snprintf(m->reason, sizeof m->reason, "%s", why);
}

/* retail validate(base), lines 35-42. */
static bool validate(TfpMovement *m, uint64_t base, Raised *e) {
  const TfpMovementBuild *build = build_of(m->host);
  uint8_t bytes[64];
  if (m->has_verified && m->verified == base) return true;
  for (size_t i = 0; i < sizeof build->anchors / sizeof build->anchors[0]; i++) {
    if (!mem_read(m->host, base + build->anchors[i].at, build->anchors[i].size, bytes, e)) return false;
    if (memcmp(bytes, build->anchors[i].bytes, build->anchors[i].size) != 0) return fail(e, 39, "Focused movement build fingerprint mismatch");
  }
  m->has_verified = true;
  m->verified = base;
  return true;
}

/* retail location(s), lines 43-70. */
static bool location(TfpMovement *m, const TfpMovementSnapshot *s, TfpMovementRecord *out, Raised *e) {
  const TfpMovementHost *h = m->host;
  uint64_t pool, buckets, descriptors, descriptor, owned_descriptor, rows;
  uint32_t active, cap, empty, mult, limit, index = 0;
  uint8_t header[20], row[8], bytes[20];
  bool has_index = false;
  if (!(s->avatar > 0 && s->avatar < 0xFFFFFFFF)) return fail(e, 44, "Locomotion player unavailable");
  if (!validate(m, s->base, e)) return false;
  if (!mem_ptr(h, s->base + build_of(h)->pool, &pool, e)) return false;
  if (!mem_u32(h, pool + 0xC, &active, e)) return false;
  if (!(active > 0 && active <= 16384)) return fail(e, 49, "Locomotion pool unavailable");
  if (!mem_read(h, pool + 0x20, 20, header, e)) return false;
  cap = le32(header + 8);
  empty = le32(header + 12);
  mult = le32(header + 16);
  if (!(cap != 0 && (cap & (cap - 1)) == 0 && cap <= 65536 && s->avatar != empty)) return fail(e, 52, "Locomotion registry unavailable");
  if (!mem_ptr(h, pool + 0x20, &buckets, e)) return false;
  limit = cap < 128 ? cap : 128;
  for (uint32_t n = 0; n < limit; n++) {
    uint32_t id;
    /* product(avatar,mult) is the low 32 bits of the product; adding n does not wrap in Lua. */
    if (!mem_read(h, buckets + (((uint64_t)(uint32_t)(s->avatar * mult) + n) % cap) * 8, 8, row, e)) return false;
    id = le32(row);
    if (id == empty) return fail(e, 58, "Local locomotion component absent");
    if (id == s->avatar) {
      index = le32(row + 4);
      has_index = true;
      break;
    }
  }
  if (!(has_index && index < active)) return fail(e, 61, "Locomotion component index unavailable");
  if (!mem_ptr(h, pool + 0x38, &descriptors, e)) return false;
  if (!mem_ptr(h, descriptors + (uint64_t)index * 8, &descriptor, e)) return false;
  if (!mem_ptr(h, s->avatar_owner + 0x110 + (uint64_t)s->avatar_index * 8, &owned_descriptor, e)) return false;
  if (owned_descriptor != descriptor) return fail(e, 64, "Locomotion descriptor ownership changed");
  if (!mem_read(h, descriptor, 20, bytes, e)) return false;
  if (memcmp(bytes, s->avatar_descriptor, 20) != 0) return fail(e, 64, "Locomotion descriptor ownership changed");
  if (!mem_ptr(h, pool + 0x50, &rows, e)) return false;
  memset(out, 0, sizeof *out);
  out->base = s->base; out->world = s->world; out->avatar_owner = s->avatar_owner; out->avatar_index = s->avatar_index;
  out->avatar = s->avatar; out->body = s->body; out->descriptor = descriptor;
  memcpy(out->descriptor_bytes, s->avatar_descriptor, 20);
  out->pool = pool; out->index = index; out->descriptors = descriptors; out->rows = rows;
  out->address = rows + (uint64_t)index * 0xA0 + 0x94;
  return true;
}
/* retail same(a,b) over identity_fields, lines 71-76. */
static bool same(const TfpMovementRecord *a, const TfpMovementRecord *b) {
  if (a->base != b->base) return false;
  if (a->world != b->world) return false;
  if (a->avatar_owner != b->avatar_owner) return false;
  if (a->avatar_index != b->avatar_index) return false;
  if (a->avatar != b->avatar) return false;
  if (a->body != b->body) return false;
  if (a->descriptor != b->descriptor) return false;
  if (memcmp(a->descriptor_bytes, b->descriptor_bytes, 20) != 0) return false;
  if (a->pool != b->pool) return false;
  if (a->index != b->index) return false;
  if (a->descriptors != b->descriptors) return false;
  if (a->rows != b->rows) return false;
  if (a->address != b->address) return false;
  return true;
}
/* retail same_player(s,r), lines 77-81. */
static bool same_player(const TfpMovementSnapshot *s, const TfpMovementRecord *r) {
  return s->base == r->base && s->world == r->world && s->avatar_owner == r->avatar_owner && s->avatar_index == r->avatar_index &&
         s->avatar == r->avatar && s->body == r->body && memcmp(s->avatar_descriptor, r->descriptor_bytes, 20) == 0;
}

/* retail writable(address), lines 82-90. */
static bool writable(const TfpMovementHost *h, uint64_t address, bool *answer, Raised *e) {
  TfpMovementRegion r;
  double at = (double)address;
  if (h->deps_writable) {
    const char *why = NULL;
    *answer = false;
    return h->deps_writable(h->user, address, answer, &why) || raised(e, why);
  }
  memset(&r, 0, sizeof r);
  *answer = false;
  if (h->windows_virtual_query(h->user, address, &r, sizeof r) != sizeof r) return true;
  /* Lua compares the numbers as doubles. */
  *answer = r.state == 0x1000 && r.kind == 0x20000 && (r.protect == 4 || r.protect == 8) && at >= (double)r.base &&
            at + 1 <= (double)r.base + (double)r.size;
  return true;
}
/* retail store(address,value), lines 91-107. */
static bool store(const TfpMovementHost *h, uint64_t address, uint8_t value, Raised *e) {
  bool accepted = false;
  uint8_t now;
  if (!writable(h, address, &accepted, e)) return false;
  if (!accepted) return fail(e, 92, "Focused locomotion byte is not writable private data");
  if (h->deps_write) {
    const char *why = NULL;
    accepted = false;
    if (!h->deps_write(h->user, address, value, &accepted, &why)) return raised(e, why);
  } else {
    size_t count = 0;
    accepted = h->windows_write_process_memory(h->user, address, &value, 1, &count) != 0 && count == 1;
  }
  if (!accepted) return fail(e, 106, "Focused locomotion byte write/readback failed");
  if (!mem_read(h, address, 1, &now, e)) return false;
  if (now != value) return fail(e, 106, "Focused locomotion byte write/readback failed");
  return true;
}
/* retail snapshot(), lines 108-117. */
static bool snapshot(const TfpMovementHost *h, TfpMovementSnapshot *s, Raised *e) {
  const char *why = NULL;
  TfpMovementCall got = h->backend_pin_first_person && h->backend_pin_player_snapshot ? h->backend_pin_player_snapshot(h->user, s, &why)
                                                                                       : h->backend_player_snapshot(h->user, s, &why);
  if (got == TFP_MOVEMENT_RAISED) return raised(e, why);
  if (got != TFP_MOVEMENT_DONE) return fail(e, 116, why ? why : "Local locomotion player unavailable");
  return true;
}

void tfp_movement_init(TfpMovement *m, const TfpMovementHost *host) {
  memset(m, 0, sizeof *m);
  m->host = host;
  m->phase = TFP_MOVEMENT_IDLE;
}
void tfp_movement_result(const TfpMovement *m, TfpMovementResult *out) {
  out->phase = m->phase; out->writes = m->writes; out->restores = m->restores; out->foreign_changes = m->foreign_changes;
  out->reason = m->has_reason ? m->reason : NULL;
  out->scope = m->scope;
  out->owned = m->has_owned; out->cleanup_pending = m->phase == TFP_MOVEMENT_CLEANUP_PENDING; out->closing = m->closing;
}
void tfp_movement_status(const TfpMovement *m, char *out, size_t size) {
  snprintf(out, size, "%s; focused writes=%u; restores=%u", tfp_movement_phase_name(m->phase), m->writes, m->restores);
}

/* The protected body of stop(), retail lines 129-141. */
static bool release(TfpMovement *m, Raised *e) {
  const TfpMovementHost *h = m->host;
  const TfpMovementRecord *r = &m->owned;
  TfpMovementSnapshot s;
  TfpMovementRecord current;
  uint8_t value;
  if (!snapshot(h, &s, e)) return false;
  /* A positively replaced local avatar retires our old data lease.
   * An unreadable owner does not authorize any write or retirement. */
  if (!same_player(&s, r)) {
    m->has_owned = false;
    return true;
  }
  if (!location(m, &s, &current, e)) return false;
  if (!same(&current, r)) {
    m->has_owned = false;
    return true;
  }
  if (!mem_read(h, r->address, 1, &value, e)) return false;
  if (value != 1) {
    m->foreign_changes++;
    m->draining = true;
    m->has_owned = false;
    return true;
  }
  if (!snapshot(h, &s, e)) return false;
  if (!location(m, &s, &current, e)) return false;
  if (!same(&current, r)) return fail(e, 138, "Locomotion owner changed before restoration");
  if (!store(h, r->address, r->before, e)) return false;
  m->restores++;
  m->has_owned = false;
  return true;
}
bool tfp_movement_stop(TfpMovement *m, const char **why) {
  Raised e;
  if (why) *why = NULL;
  if (m->scope == TFP_MOVEMENT_SCOPE_FIRD) {
    m->draining = false;
    m->scope = TFP_MOVEMENT_SCOPE_NONE;
  }
  if (!m->has_owned) {
    report(m, TFP_MOVEMENT_IDLE, NULL);
    return true;
  }
  if (!release(m, &e)) {
    /* If a reported write failure still produced the desired original
     * byte, a later stop verifies it and retires without another write. */
    report(m, TFP_MOVEMENT_CLEANUP_PENDING, e.text);
    if (why) *why = m->reason;
    return false;
  }
  report(m, TFP_MOVEMENT_IDLE, NULL);
  return true;
}
bool tfp_movement_shutdown(TfpMovement *m, const char **why) {
  m->closing = true;
  return tfp_movement_stop(m, why);
}

/* retail input_focus(), lines 150-157. */
static bool input_focus(const TfpMovementHost *h, bool *focus, Raised *e) {
  const char *why = NULL;
  bool answer = false;
  *focus = false;
  if (h->window_mouse_focus) {
    if (!h->window_mouse_focus(h->user, &answer, &why)) return raised(e, why);
    if (!answer) return true;
  }
  if (h->window_show_cursor) {
    answer = true;
    if (!h->window_show_cursor(h->user, &answer, &why)) return raised(e, why);
    if (answer) return true;
  }
  *focus = true;
  return true;
}
/* retail backward(), lines 158-171. */
static bool backward(TfpMovement *m, bool *held, Raised *e) {
  const TfpMovementHost *h = m->host;
  const char *why = NULL;
  TfpMovementNumber value = {false, 0};
  bool focus;
  *held = false;
  if (!(h->engine_keyboard != 0 && h->keyboard_button)) return fail(e, 160, "Game keyboard input unavailable");
  if (h->engine_keyboard != m->keyboard) {
    m->key.has = false;
    m->keyboard = h->engine_keyboard;
  }
  if (!m->key.has) {
    bool (*find)(void *, const char *, TfpMovementNumber *, const char **) = h->keyboard_button_index ? h->keyboard_button_index : h->keyboard_button_id;
    TfpMovementNumber key = {false, 0};
    if (!find) return fail(e, 164, "Game S binding lookup unavailable");
    if (!find(h->user, "s", &key, &why)) return raised(e, why);
    m->key = key;
  }
  if (!(m->key.has && m->key.value == floor(m->key.value) && m->key.value >= 0 && m->key.value < 512))
    return fail(e, 166, "Game S binding unavailable");
  if (!input_focus(h, &focus, e)) return false;
  if (!focus) return true;
  if (!h->keyboard_button(h->user, m->key.value, &value, &why)) return raised(e, why);
  if (!(value.has && (value.value == 0 || value.value == 1))) return fail(e, 169, "Game S input state unavailable");
  *held = value.value == 1;
  return true;
}
/* retail focused_owner(current), lines 195-199. */
static bool focused_owner(const TfpMovementHost *h, bool focused, const TfpMovementSnapshot *current, bool *owner, Raised *e) {
  const char *why = NULL;
  bool answer = false;
  *owner = true;
  if (!focused) return true;
  *owner = false;
  if (!(current->safe && current->camera_kind == 4 && !current->controller_active)) return true;
  if (!input_focus(h, &answer, e)) return false;
  if (!answer) return true;
  answer = false;
  if (!h->controller_focus_owned(h->user, current, &answer, &why)) return raised(e, why);
  if (!answer) return true;
  *owner = h->deps_fallback_ready && h->deps_fallback_ready(h->user, current);
  return true;
}
/* The conditions of the two acquisition asserts (lines 225-226, 231-232); *held is their value. */
static bool still_owner(TfpMovement *m, bool focused, const TfpMovementRecord *current, bool *held, Raised *e) {
  TfpMovementSnapshot s;
  TfpMovementRecord again;
  bool owner;
  *held = false;
  if (!snapshot(m->host, &s, e)) return false;
  if (!(s.safe && !s.seated && (s.camera_kind == 4 || s.camera_kind == 6))) return true;
  if (!focused_owner(m->host, focused, &s, &owner, e)) return false;
  if (!owner) return true;
  if (!location(m, &s, &again, e)) return false;
  *held = same(&again, current);
  return true;
}
/* The protected body of update(), retail lines 176-233. */
static bool attempt(TfpMovement *m, bool focused, Raised *e) {
  const TfpMovementHost *h = m->host;
  const char *why = NULL;
  const void *action = NULL;
  TfpMovementSnapshot s;
  TfpMovementRecord current;
  bool answer;
  uint8_t value;
  if (!h->controller_is_enabled) return fail(e, 176, "Movement controller unavailable");
  if (!h->controller_is_enabled(h->user) || !h->controller_result_pose_offset(h->user)) {
    tfp_movement_stop(m, NULL);
    return true;
  }
  m->scope = focused ? TFP_MOVEMENT_SCOPE_FIRD : TFP_MOVEMENT_SCOPE_MANUAL;
  if (h->backend_foreign_camera) {
    TfpMovementForeign foreign = TFP_MOVEMENT_FOREIGN_OTHER;
    if (!h->backend_foreign_camera(h->user, &foreign, &why)) return raised(e, why);
    if (foreign == TFP_MOVEMENT_FOREIGN_OTHER) return fail(e, 181, "Camera ownership unavailable");
    if (foreign == TFP_MOVEMENT_FOREIGN_TRUE) {
      tfp_movement_stop(m, NULL);
      return true;
    }
  }
  /* Manual FPV retains its original S-only behavior. Natural Fird
   * owns a separate, continuous focus interval, without key queries. */
  if (focused) {
    answer = false;
    if (h->controller_focus_owned && !input_focus(h, &answer, e)) return false;
    if (!answer) {
      tfp_movement_stop(m, NULL);
      return true;
    }
  } else {
    if (!backward(m, &answer, e)) return false;
    if (!answer) {
      m->draining = false;
      tfp_movement_stop(m, NULL);
      return true;
    }
  }
  if (m->draining) {
    report(m, TFP_MOVEMENT_FOREIGN, focused ? "Focused locomotion changed externally; switch Fird Person off before retrying"
                                            : "Focused locomotion changed externally; release S before retrying");
    return true;
  }
  if (!snapshot(h, &s, e)) return false;
  if (!s.safe || s.seated || (s.camera_kind != 4 && s.camera_kind != 6)) {
    tfp_movement_stop(m, NULL);
    return true;
  }
  if (!focused_owner(h, focused, &s, &answer, e)) return false;
  if (!answer) {
    tfp_movement_stop(m, NULL);
    return true;
  }
  if (!h->backend_action_state(h->user, &s, &action, &why)) return raised(e, why);
  answer = false;
  if (!h->backend_on_foot(h->user, &s, action, &answer, &why)) return raised(e, why);
  if (!answer) {
    tfp_movement_stop(m, NULL);
    return true;
  }
  if (!focused && s.camera_kind == 4 && !(h->deps_fallback_ready && h->deps_fallback_ready(h->user, &s))) {
    tfp_movement_stop(m, NULL);
    return true;
  }
  if (!location(m, &s, &current, e)) return false;
  if (m->has_owned && !same(&current, &m->owned)) {
    const char *problem = NULL;
    if (!tfp_movement_stop(m, &problem)) return fail(e, 210, problem);
    /* Never acquire another slot in the update which retired one. */
    return true;
  }
  if (!mem_read(h, current.address, 1, &value, e)) return false;
  if (!(value == 0 || value == 1)) return fail(e, 215, "Focused locomotion flag has a foreign value");
  if (m->has_owned) {
    if (value != 1) {
      m->foreign_changes++;
      m->draining = true;
      m->has_owned = false;
      report(m, TFP_MOVEMENT_FOREIGN, "Focused locomotion changed externally; release S before retrying");
    }
    return true;
  }
  if (value == 1) {
    report(m, TFP_MOVEMENT_NATIVE, "Focused locomotion is already native");
    return true;
  }
  if (!still_owner(m, focused, &current, &answer, e)) return false;
  if (!answer) return fail(e, 225, "Locomotion owner changed before acquisition");
  if (!mem_read(h, current.address, 1, &value, e)) return false;
  if (value != 0) return fail(e, 227, "Focused locomotion changed before acquisition");
  current.before = 0;
  m->owned = current;
  m->has_owned = true;
  if (!store(h, current.address, 1, e)) return false;
  if (!still_owner(m, focused, &current, &answer, e)) return false;
  if (!answer) return fail(e, 231, "Locomotion owner changed during acquisition");
  m->writes++;
  report(m, TFP_MOVEMENT_ACTIVE, NULL);
  return true;
}
/* retail update(controller,skip,focused), lines 172-243. */
static bool update(TfpMovement *m, bool skip, bool focused, const char **why) {
  Raised e;
  if (m->closing || skip) return tfp_movement_stop(m, why);
  if (m->has_owned && m->phase == TFP_MOVEMENT_CLEANUP_PENDING) return tfp_movement_stop(m, why);
  if (why) *why = NULL;
  if (!attempt(m, focused, &e)) {
    if (!tfp_movement_stop(m, why)) return false;
    report(m, TFP_MOVEMENT_UNAVAILABLE, e.text);
    if (why) *why = m->reason;
    return true;
  }
  if (m->has_owned && m->phase == TFP_MOVEMENT_CLEANUP_PENDING) {
    if (why) *why = m->has_reason ? m->reason : NULL;
    return false;
  }
  return true;
}
bool tfp_movement_update(TfpMovement *m, bool skip, const char **why) { return update(m, skip, false, why); }
bool tfp_movement_update_fird(TfpMovement *m, bool skip, const char **why) { return update(m, skip, true, why); }

const char *tfp_movement_phase_name(TfpMovementPhase phase) {
  static const char *const names[] = {"idle", "active", "native", "foreign", "unavailable", "cleanup_pending"};
  return names[phase];
}
const char *tfp_movement_scope_name(TfpMovementScope scope) {
  static const char *const names[] = {NULL, "manual", "fird"};
  return names[scope];
}
