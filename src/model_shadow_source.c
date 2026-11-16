/* Translation of retail src/first_person/model_shadow_source.lua (0.5.41-smooth.66,
 * commit 1d0d453). Comments of the form ":NN" are the retail line.
 *
 * A raised Lua error is a longjmp to the pcall that the retail function wraps its body
 * in; its text is in m->error. Assertion texts carry "<chunk>:<line>: " as the reference
 * LuaJIT prints them. Tables walked with pairs() are kept in ascending key order. */
#include "model_shadow_source.h"
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK "src/first_person/model_shadow_source.lua"
static const uint8_t RIG[8] = {237, 139, 160, 60, 73, 77, 207, 208};   /* :4 */

size_t tfp_vis_number(double value, char *out, size_t size) {
  int n;
  if (value != value) n = snprintf(out, size, "nan");
  else if (value == INFINITY) n = snprintf(out, size, "inf");
  else if (value == -INFINITY) n = snprintf(out, size, "-inf");
  else n = snprintf(out, size, "%.14g", value);
  return (size_t)n;
}
bool tfp_vis_identity_equal(const TfpVisIdentity *a, const TfpVisIdentity *b) {
  if (!a->present || !b->present) return a->present == b->present;
  return a->len == b->len && memcmp(a->bytes, b->bytes, a->len) == 0;
}

/* ---- errors ---- */
static _Noreturn void raise(TfpModelShadow *m) {
  m->error.present = true;
  longjmp(*(jmp_buf *)m->jump, 1);
}
static _Noreturn void fail(TfpModelShadow *m, int line, const char *message) {
  snprintf(m->error.text, sizeof m->error.text, CHUNK ":%d: %s", line, message);
  raise(m);
}
#define CHECK(condition, line, message) do { if (!(condition)) fail(m, line, message); } while (0)

/* ---- the backend ---- */
static void rd(TfpModelShadow *m, double at, size_t size, uint8_t *out) { if (!m->io->read(m->io->user, at, size, out, &m->error)) raise(m); }
static double ptr(TfpModelShadow *m, double at) { double v; if (!m->io->ptr(m->io->user, at, &v, &m->error)) raise(m); return v; }
static double u32(TfpModelShadow *m, double at) { double v; if (!m->io->u32(m->io->user, at, &v, &m->error)) raise(m); return v; }
static double u8(TfpModelShadow *m, double at) { double v; if (!m->io->u8(m->io->user, at, &v, &m->error)) raise(m); return v; }
/* assert(b:snapshot()) */
static void snapshot(TfpModelShadow *m, int line, TfpVisSnapshot *out) {
  TfpVisText why = {0};
  if (m->io->snapshot(m->io->user, out, &why)) return;
  fail(m, line, why.present ? why.text : "assertion failed!");
}
/* if b.foreign_camera then assert(b.foreign_camera()==false,'Another camera owns the view')end */
static void not_foreign(TfpModelShadow *m, int line) {
  int value = 1;
  if (!m->io->foreign_camera) return;
  if (!m->io->foreign_camera(m->io->user, &value, &m->error)) raise(m);
  CHECK(value == 0, line, "Another camera owns the view");
}

/* :5-7 */
static double u(const uint8_t *raw, size_t o) { return raw[o] + raw[o + 1] * 256.0 + raw[o + 2] * 65536.0 + raw[o + 3] * 16777216.0; }
static double p(const uint8_t *raw, size_t o) { return u(raw, o) + u(raw, o + 4) * 4294967296.0; }
static bool ref(bool has, double n) { return has && n > 0 && n < 4294967295.0 && fmod(n, 1) == 0; }

/* :10-16 */
static TfpModelContext unit(TfpModelShadow *m, bool has_id, double id, const TfpVisSnapshot *s) {
  TfpModelContext c;
  CHECK(ref(has_id, id), 11, "Model reference invalid");
  if (!m->io->alignment_context(m->io->user, id, s->avatar_owner, s->avatar_index, &c, &m->error)) raise(m);
  CHECK(c.unit == id && u32(m, c.object + 8) == id && u8(m, c.generations + c.index) == c.generation
        && ptr(m, c.units + c.index * 8) == c.object, 13, "Model generation retired");
  return c;
}
/* :17 */
static double parent(TfpModelShadow *m, const TfpModelContext *c) {
  uint8_t raw[8];
  rd(m, c->object + 0x1d0, 8, raw);
  return p(raw, 0);
}

/* ---- tables keyed by a number, in ascending order ---- */
static void *slot(void **items, size_t *count, size_t *room, size_t size, double key) {
  char *base = *items;
  size_t at = 0;
  while (at < *count && *(double *)(void *)(base + at * size) < key) at++;
  if (at < *count && *(double *)(void *)(base + at * size) == key) return base + at * size;
  if (*count == *room) {
    *room = *room * 2 + 16;
    base = *items = realloc(base, *room * size);
  }
  memmove(base + (at + 1) * size, base + at * size, (*count - at) * size);
  memset(base + at * size, 0, size);
  *(double *)(void *)(base + at * size) = key;
  ++*count;
  return base + at * size;
}
static TfpModelInspected *inspected(TfpModelShadow *m, double id) {
  return slot((void **)&m->inspected, &m->inspected_count, &m->inspected_room, sizeof *m->inspected, id);
}
static void text_add(TfpModelShadow *m, const void *bytes, size_t len) {
  if (m->text_len + len + 1 > m->text_room) {
    m->text_room = (m->text_len + len + 1) * 2;
    m->text = realloc(m->text, m->text_room);
  }
  memcpy(m->text + m->text_len, bytes, len);
  m->text_len += len;
}
static void text_number(TfpModelShadow *m, double value) {
  char number[40];
  text_add(m, number, tfp_vis_number(value, number, sizeof number));
}

typedef struct Probe {
  TfpModelShadow *m;
  const TfpVisSnapshot *expected;
  const uint8_t *appearance; size_t appearance_len;
  TfpVisSnapshot s;
  TfpModelContext rig, gun;
  double world;
} Probe;

/* :25-28 */
static TfpModelContext watch(Probe *q, TfpModelContext c, bool root) {
  TfpModelShadow *m = q->m;
  TfpModelInspected row = {.id = c.unit, .object = c.object};
  if (!root) { row.parent = parent(m, &c); row.has_parent = true; }
  row.world = ptr(m, c.object + 0x18);
  *inspected(m, c.unit) = row;
  return c;
}
/* :37-43 */
static void add(Probe *q, const TfpModelContext *c, double wanted_parent) {
  TfpModelShadow *m = q->m;
  const TfpVisSnapshot *s = &q->s;
  CHECK(c->unit != s->body && !(s->has_viewmodel && c->unit == s->viewmodel) && ptr(m, c->object + 0x18) == q->world
        && parent(m, c) == wanted_parent, 38, "Model attachment changed");
  TfpModelRow *row = slot((void **)&m->rows, &m->row_count, &m->row_room, sizeof *m->rows, c->unit);
  row->rig = false;
  row->model_parent = wanted_parent;
  row->owner = (TfpModelOwner){.identity = s->identity, .rig = s->viewmodel, .rig_object = q->rig.object, .weapon = s->weapon_unit,
                               .weapon_object = q->gun.object, .world = q->world, .parent = wanted_parent, .unit = c->unit, .object = c->object};
  TfpModelClosing *closing = slot((void **)&m->closing, &m->closing_count, &m->closing_room, sizeof *m->closing, c->unit);
  closing->context = *c;
  closing->parent = wanted_parent;
  closing->has_links = false;
  text_add(m, "|", 1); text_number(m, c->unit); text_add(m, ":", 1); text_number(m, c->object); text_add(m, ":", 1); text_number(m, wanted_parent);
}
/* :47-59 */
static void weapon(Probe *q, double id, double wanted_parent, int depth) {
  TfpModelShadow *m = q->m;
  bool seen = false;
  for (size_t i = 0; i < m->visited_count; i++) seen = seen || m->visited[i] == id;
  CHECK(depth <= 8 && !seen, 48, "Weapon model cycle/depth exceeded");
  m->visited[m->visited_count++] = id;
  CHECK(m->visited_count <= 32, 49, "Weapon model count exceeded");
  TfpModelContext c = watch(q, unit(m, true, id, &q->s), false);
  add(q, &c, wanted_parent);
  uint8_t links[16], next[8];
  rd(m, c.object + 0x1d8, 16, links);
  for (size_t i = 0; i < m->closing_count; i++) if (m->closing[i].id == id) { m->closing[i].has_links = true; memcpy(m->closing[i].links, links, 16); }
  TfpModelInspected *row = inspected(m, id);
  row->has_links = true; memcpy(row->links, links, 16);
  double child = p(links, 0), siblings[40];
  size_t sibling_count = 0;
  while (child != 0) {
    bool again = false;
    for (size_t i = 0; i < sibling_count; i++) again = again || siblings[i] == child;
    CHECK(child >= 65536 && child < 140737488355328.0 && !again, 54, "Weapon sibling changed");
    siblings[sibling_count++] = child;
    double child_id = u32(m, child + 8);
    rd(m, child + 0x1e0, 8, next);
    double next_child = p(next, 0);
    CHECK(unit(m, true, child_id, &q->s).object == child, 56, "Weapon child generation changed");
    weapon(q, child_id, c.object, depth + 1);
    child = next_child;
  }
}
static bool same_appearance(TfpModelShadow *m, double at, const uint8_t *appearance, size_t len) {
  uint8_t now[120];
  rd(m, at, 120, now);
  return len == 120 && memcmp(now, appearance, 120) == 0;
}
static bool same_links(TfpModelShadow *m, double object, bool has, const uint8_t *links) {
  uint8_t now[16];
  rd(m, object + 0x1d8, 16, now);
  return has && memcmp(now, links, 16) == 0;
}
static bool opt_equal(bool has_a, double a, bool has_b, double b) { return has_a && has_b ? a == b : has_a == has_b; }

/* :20-80, the protected function */
static void probe_body(Probe *q) {
  TfpModelShadow *m = q->m;
  const TfpVisSnapshot *expected = q->expected;
  TfpVisSnapshot *s = &q->s;
  uint8_t raw[16];
  snapshot(m, 21, s);
  CHECK(s->safe && s->camera_kind == 6 && tfp_vis_identity_equal(&s->player_identity, &expected->player_identity)
        && s->body == expected->body && opt_equal(s->has_viewmodel, s->viewmodel, expected->has_viewmodel, expected->viewmodel),
        21, "First-person model owner unavailable");
  not_foreign(m, 23);
  TfpModelContext body = watch(q, unit(m, true, s->body, s), true);
  q->rig = watch(q, unit(m, s->has_viewmodel, s->viewmodel, s), false);
  const TfpModelContext *rig = &q->rig;
  rd(m, rig->object + 0x1d8, 16, raw);
  TfpModelInspected *rig_row = inspected(m, rig->unit);
  rig_row->has_links = true; memcpy(rig_row->links, raw, 16);
  CHECK(body.object != rig->object && u32(m, rig->object + 0x70) == 63 && (rd(m, rig->object + 0x358, 8, raw), memcmp(raw, RIG, 8) == 0),
        31, "Native first-person rig changed");
  q->world = ptr(m, rig->object + 0x18);
  CHECK(q->world == ptr(m, body.object + 0x18), 32, "First-person rig world changed");
  double at = s->avatar_owner + m->appearance + s->avatar_index * 0x1b8 + 0xdc;
  CHECK(same_appearance(m, at, q->appearance, q->appearance_len), 34, "Arm appearance changed");
  q->gun = watch(q, unit(m, s->has_weapon_unit, s->weapon_unit, s), false);
  CHECK(parent(m, &q->gun) == rig->object, 35, "First-person weapon parent changed");
  text_add(m, s->identity.bytes, s->identity.len); text_add(m, "|", 1); text_number(m, rig->object);   /* :36 */
  for (int layer = 0; layer <= 2; layer++) for (int slot_index = 6; slot_index <= 9; slot_index++) {
    double id = u(q->appearance, (size_t)(layer * 40 + slot_index * 4));
    if (ref(true, id)) {
      TfpModelContext c = watch(q, unit(m, true, id, s), false);
      if (parent(m, &c) == rig->object) add(q, &c, rig->object);
    }
  }
  weapon(q, s->weapon_unit, rig->object, 0);
  TfpModelRow *rig_out = slot((void **)&m->rows, &m->row_count, &m->row_room, sizeof *m->rows, s->viewmodel);   /* :61 */
  memset(rig_out, 0, sizeof *rig_out);
  rig_out->unit = s->viewmodel; rig_out->rig = true;
  TfpVisSnapshot after;
  snapshot(m, 62, &after);
  CHECK(tfp_vis_identity_equal(&after.identity, &s->identity) && after.record == s->record && after.safe && after.camera_kind == 6
        && same_appearance(m, at, q->appearance, q->appearance_len), 62, "First-person models changed during observation");
  rig_row = inspected(m, rig->unit);
  CHECK(unit(m, s->has_viewmodel, s->viewmodel, &after).object == rig->object
        && same_links(m, rig->object, rig_row->has_links, rig_row->links), 64, "First-person rig retired or attachments changed");
  for (size_t i = 0; i < m->closing_count; i++) {   /* :66-69 */
    const TfpModelClosing *row = &m->closing[i];
    TfpModelContext c = unit(m, true, row->id, &after);
    CHECK(c.object == row->context.object && parent(m, &c) == row->parent && ptr(m, c.object + 0x18) == q->world
          && (!row->has_links || same_links(m, c.object, true, row->links)), 67, "First-person attachment changed during observation");
  }
  if (ref(s->has_weapon, s->weapon) && s->has_weapon_component_descriptor && s->has_weapon_descriptor
      && s->weapon_descriptor_len == 20 && m->io->descriptor) {   /* :73-78 */
    free(m->fingerprint.appearance); free(m->fingerprint.inspected);
    m->fingerprint.owner = s->player_identity; m->fingerprint.body = s->body; m->fingerprint.rig = s->viewmodel;
    m->fingerprint.weapon = s->weapon; m->fingerprint.weapon_unit = s->weapon_unit; m->fingerprint.record = s->record;
    m->fingerprint.world = s->world; m->fingerprint.component = s->weapon_component_descriptor;
    memcpy(m->fingerprint.descriptor, s->weapon_descriptor, 20);
    m->fingerprint.appearance = malloc(q->appearance_len + 1);
    memcpy(m->fingerprint.appearance, q->appearance, q->appearance_len);
    m->fingerprint.appearance_len = q->appearance_len;
    m->fingerprint.inspected = malloc((m->inspected_count + 1) * sizeof *m->inspected);
    memcpy(m->fingerprint.inspected, m->inspected, m->inspected_count * sizeof *m->inspected);
    m->fingerprint.inspected_count = m->inspected_count;
    m->has_fingerprint = true;
  }
  /* :79 table.concat over {s.identity, ...}: a snapshot without identity fails here, after the observation is kept. */
  CHECK(s->identity.present, 79, "invalid value (nil) at index 1 in table for 'concat'");
}
static void probe_call(void *q) { probe_body(q); }

typedef struct Unchanged {
  TfpModelShadow *m;
  const TfpVisSnapshot *s;
  const uint8_t *appearance; size_t appearance_len;
  TfpModelInspect inspect; void *context;
} Unchanged;
static bool descriptor_same(TfpModelShadow *m, double world, int line) {   /* :93-94, :108-109 */
  uint8_t d[24], component[20];
  if (!m->io->descriptor) fail(m, line, "attempt to call method '_descriptor' (a nil value)");
  if (!m->io->descriptor(m->io->user, world, m->fingerprint.weapon, d, &m->error)) raise(m);
  if (memcmp(d, m->fingerprint.descriptor, 20) != 0) return false;
  rd(m, m->fingerprint.component, 20, component);
  return memcmp(component, m->fingerprint.descriptor, 20) == 0;
}
/* :88-110, the protected function */
static void unchanged_call(void *arg) {
  Unchanged *q = arg;
  TfpModelShadow *m = q->m;
  const TfpVisSnapshot *s = q->s;
  CHECK(m->has_fingerprint, 89, "No qualified model observation");
  CHECK(q->inspect && s->safe && s->camera_kind == 6 && tfp_vis_identity_equal(&s->player_identity, &m->fingerprint.owner)
        && s->body == m->fingerprint.body && s->has_viewmodel && s->viewmodel == m->fingerprint.rig && s->world == m->fingerprint.world
        && s->record == m->fingerprint.record && s->request_weapon == m->fingerprint.weapon
        && q->appearance_len == m->fingerprint.appearance_len && memcmp(q->appearance, m->fingerprint.appearance, q->appearance_len) == 0,
        90, "Model observation owner changed");
  CHECK(descriptor_same(m, s->world, 93), 93, "Model weapon descriptor changed");
  m->observation_count = 0;
  for (size_t i = 0; i < m->fingerprint.inspected_count; i++) {   /* :96-102 */
    const TfpModelInspected *row = &m->fingerprint.inspected[i];
    double object = 0;
    uint8_t header[32], raw[24];
    if (!q->inspect(q->context, row->id, row->object, &object, header, &m->error)) raise(m);
    CHECK(object == row->object && p(header, 24) == row->world, 98, "Model observation generation/world changed");
    size_t len = row->has_links ? 24 : 8;
    rd(m, object + 0x1d0, len, raw);
    CHECK((!row->has_parent || p(raw, 0) == row->parent) && (!row->has_links || memcmp(raw + 8, row->links, 16) == 0),
          100, "Model observation topology changed");
    TfpModelObservation *entry = slot((void **)&m->observations, &m->observation_count, &m->observation_room, sizeof *m->observations, object);
    memcpy(entry->raw, raw, len);
    entry->len = len;
  }
  const TfpModelInspected *rig = NULL;
  for (size_t i = 0; i < m->fingerprint.inspected_count; i++) if (m->fingerprint.inspected[i].id == m->fingerprint.rig) rig = &m->fingerprint.inspected[i];
  CHECK(rig, 103, "assertion failed!");
  uint8_t resource[8], again[24];
  CHECK(u32(m, rig->object + 0x70) == 63 && (rd(m, rig->object + 0x358, 8, resource), memcmp(resource, RIG, 8) == 0),
        104, "Model observation rig changed");
  for (size_t i = 0; i < m->observation_count; i++) {   /* :107 */
    rd(m, m->observations[i].object + 0x1d0, m->observations[i].len, again);
    CHECK(memcmp(again, m->observations[i].raw, m->observations[i].len) == 0, 107, "Model topology changed during observation");
  }
  CHECK(descriptor_same(m, s->world, 108), 108, "Model weapon changed during observation");
}

typedef struct Valid { TfpModelShadow *m; const TfpVisSnapshot *expected; const TfpModelOwner *owner; } Valid;
/* :114-124, the protected function */
static void valid_call(void *arg) {
  Valid *q = arg;
  TfpModelShadow *m = q->m;
  const TfpModelOwner *owner = q->owner;
  TfpVisSnapshot s;
  uint8_t resource[8];
  snapshot(m, 115, &s);
  CHECK(s.safe && s.camera_kind == 6 && tfp_vis_identity_equal(&s.player_identity, &q->expected->player_identity)
        && tfp_vis_identity_equal(&s.identity, &owner->identity) && s.has_viewmodel && s.viewmodel == owner->rig
        && s.has_weapon_unit && s.weapon_unit == owner->weapon, 115, "First-person model target changed");
  TfpModelContext rig = unit(m, true, owner->rig, &s);
  CHECK(rig.object == owner->rig_object && ptr(m, rig.object + 0x18) == owner->world && u32(m, rig.object + 0x70) == 63
        && (rd(m, rig.object + 0x358, 8, resource), memcmp(resource, RIG, 8) == 0), 117, "First-person model rig changed");
  TfpModelContext gun = unit(m, true, owner->weapon, &s);
  TfpModelContext target = unit(m, true, owner->unit, &s);
  CHECK(gun.object == owner->weapon_object && parent(m, &gun) == rig.object && target.object == owner->object
        && parent(m, &target) == owner->parent && ptr(m, target.object + 0x18) == owner->world, 120, "First-person shadow attachment changed");
  not_foreign(m, 122);
}

/* pcall */
static bool protect(TfpModelShadow *m, void (*call)(void *), void *arg) {
  jmp_buf here;
  void *outer = m->jump;
  m->jump = &here;
  m->error.present = false;
  bool ok = setjmp(here) == 0;
  if (ok) call(arg);
  m->jump = outer;
  return ok;
}

void tfp_model_shadow_init(TfpModelShadow *m, const TfpModelShadowIo *io, double appearance) {
  memset(m, 0, sizeof *m);
  m->io = io;
  m->appearance = appearance;
}
void tfp_model_shadow_free(TfpModelShadow *m) {
  free(m->fingerprint.appearance); free(m->fingerprint.inspected);
  free(m->rows); free(m->text); free(m->inspected); free(m->closing); free(m->observations);
  memset(m, 0, sizeof *m);
}
bool tfp_model_shadow_probe(TfpModelShadow *m, const TfpVisSnapshot *expected, const uint8_t *appearance, size_t appearance_len,
                            const uint8_t **text, size_t *text_len) {
  Probe q = {.m = m, .expected = expected, .appearance = appearance, .appearance_len = appearance_len};
  m->has_fingerprint = false;   /* :19 */
  m->row_count = m->inspected_count = m->closing_count = m->visited_count = m->text_len = 0;
  bool ok = protect(m, probe_call, &q);
  if (!ok) {   /* :81 */
    m->row_count = m->text_len = 0;
    text_add(m, m->error.text, strlen(m->error.text));
  }
  text_add(m, "", 1);
  *text = m->text;
  *text_len = m->text_len - 1;
  return ok;
}
bool tfp_model_shadow_has_observation(const TfpModelShadow *m) { return m->has_fingerprint; }   /* :86 */
bool tfp_model_shadow_unchanged(TfpModelShadow *m, const TfpVisSnapshot *s, const uint8_t *appearance, size_t appearance_len,
                                TfpModelInspect inspect, void *inspect_context, TfpVisText *why) {
  Unchanged q = {m, s, appearance, appearance_len, inspect, inspect_context};
  bool ok = protect(m, unchanged_call, &q);
  if (why) {   /* :111 */
    why->present = !ok;
    if (!ok) memcpy(why->text, m->error.text, sizeof why->text);
  }
  return ok;
}
bool tfp_model_shadow_valid(TfpModelShadow *m, const TfpVisSnapshot *expected, const TfpModelOwner *owner) {
  Valid q = {m, expected, owner};
  return protect(m, valid_call, &q);   /* :125 */
}
