/* Translation of retail src/first_person/head_visibility.lua (0.5.41-smooth.66, commit
 * 1d0d453). Comments of the form ":NN" are the retail line.
 *
 * How the Lua is carried over:
 * - A raised error is a longjmp to the nearest pcall (protect()); its text is in h->error.
 *   Assertion texts carry "<chunk>:<line>: " as the reference LuaJIT prints them.
 * - Tables are objects on one list, freed by mark and sweep at the end of each public
 *   call, so rows, layouts and observations live exactly as long as something refers to
 *   them (a row is shared between `owned`, a spec's rows, `wanted` and the steady record).
 * - Tables walked with pairs() are sorted maps, walked in ascending key order; a walk
 *   that removes entries walks a copy of the keys and reads the live value.
 * - nil against false and 0: every optional value has its own has_* flag. */
#include "head_visibility.h"
#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK "src/first_person/head_visibility.lua"
#define HIGH 140737488355328.0   /* 2^47 */
#define NONE 4294967295.0

typedef bool (*Setter)(void *user, TfpEngineUnit unit, int mesh_index, bool visible, const char *pass_name, TfpEngineError *error);
/* Beyond retail: which of retail's parts may be hidden (bit PART_HEAD = 1, PART_TORSO = 2, PART_SHOULDER = 4).
 * 7 is retail. Set once before the view is first used. */
static unsigned tfp_head_parts = 7;
void tfp_head_set_parts(unsigned parts) { tfp_head_parts = parts & 7u; }
typedef enum Part { PART_HEAD, PART_TORSO, PART_SHOULDER, PART_BACKPACK, PART_BODY, PART_MODEL } Part;
static const char *const PART_NAMES[] = {"head", "torso", "shoulder", "backpack", "body", "model"};
typedef struct Selected { int slot; Part part; } Selected;
static const Selected SELECTED[] = {{0, PART_HEAD}, {2, PART_TORSO}, {8, PART_SHOULDER}, {9, PART_SHOULDER}};   /* :5 */
typedef struct Pass { double mask; const char *name; bool visible; } Pass;
static const Pass PASSES[] = {{1, "default", false}, {2, "shadow_caster", true}};              /* :7 */
static const Pass SHADOW_ONLY[] = {{2, "shadow_caster", false}};                                /* :8 */
static const Pass HIDDEN_NO_SHADOW[] = {{1, "default", false}, {2, "shadow_caster", false}};    /* :9 */
#define TORSO_SETTLE .6
static const uint8_t RIG_RESOURCE[8] = {237, 139, 160, 60, 73, 77, 207, 208};                   /* :12 */
typedef struct Pad { const char *name; double hash, node; } Pad;
static const Pad RIG_PADS[] = {{"g_l_shoulderpad", 0x44E19D12, 57}, {"g_r_shoulderpad", 0xCB0E1712, 58}};   /* :13 */

/* ---- objects ---- */
typedef enum Kind { K_NMAP, K_SMAP, K_LAYOUT, K_SPEC, K_ROW, K_OBSERVED, K_STEADY, K_SHAPE, K_BLOB } Kind;
typedef struct Obj { struct Obj *next; uint8_t kind, mark; } Obj;
/* A table keyed by a number. What an entry holds depends on the table. */
typedef struct NEntry { double key; Obj *obj; const void *raw; double a, b; bool flag; } NEntry;
typedef struct NMap { Obj o; NEntry *e; size_t n, room; } NMap;
/* A table keyed by "<unit reference>:<mesh address>". */
#define KEY 48
typedef struct SEntry { char key[KEY]; Obj *obj; double num; } SEntry;
typedef struct SMap { Obj o; SEntry *e; size_t n, room; } SMap;
typedef struct Blob { Obj o; uint8_t bytes[32]; } Blob;
/* {count=,array=,bytes=,indices=}: `indices` maps a mesh address to its 1-based index (entry.a). */
typedef struct Layout { Obj o; double count; bool has_array; double array; uint8_t *bytes; size_t len; NMap *indices; } Layout;
/* An entry of `refs`: what one renderer contributes. */
typedef struct Spec {
  Obj o;
  Part part;
  double offsets[8]; int offset_count;
  bool body_only, rig, shadow_rig, model_shadow, has_backpack, has_model_parent, has_model_owner;
  TfpVisIdentity backpack;
  double model_parent;
  TfpModelOwner model_owner;
  SMap *rows;
  Layout *layout;
  bool has_object, has_resource, has_retry;
  double object, retry;
  uint8_t resource[8];
} Spec;
/* One mesh whose pass bits may be owned. */
typedef struct Row {
  Obj o;
  double ref, object, world, mesh, bits, before, written, body_ref, body_object, customization, current, last;
  TfpEngineUnit handle;
  Setter setter;
  TfpVisIdentity owner, backpack;
  double offsets[8]; int offset_count;
  Part part;
  bool body_only, rig, has_backpack, shadow_suppressed, shadow_only, has_model_owner, has_model_parent, has_last, restoring_default;
  const Pad *rig_pad;
  TfpModelOwner model_owner;
  double model_parent;
  bool has_reset_epoch, has_reset_default_frame, has_reset_shadow_frame;
  double reset_epoch, reset_bits, reset_default_frame, reset_shadow_frame;
} Row;
typedef struct Observed {
  Obj o;
  TfpVisIdentity owner, pack_identity;
  double body, body_ref, world, customization, viewmodel;
  TfpEngineWorld sdk_world;
  uint8_t appearance[120];
  bool has_viewmodel, rig_capable, full_body, hide_torso, show_torso, has_model_signature;
  NMap *refs;           /* unit reference -> Spec */
  NMap *kept_layouts;   /* unit reference -> Layout, a = object, b = parent */
  uint8_t *model_signature; size_t model_signature_len;
} Observed;
typedef struct Shape {
  Obj o;
  double object, parent;
  Layout *layout;
  bool has_parent, has_handle;
  uint8_t resource[8];
  TfpEngineUnit handle;
} Shape;
typedef struct Bindings { void (*engine[9])(void); void (*backend[4])(void); } Bindings;
typedef struct Steady {
  Obj o;
  Observed *observed;
  SMap *wanted;
  NMap *layouts, *shapes;
  Bindings bindings;
  uint8_t flags[32]; uint8_t flags_len;
} Steady;

struct TfpHead {
  const TfpHeadIo *io;
  const TfpEngineIface *engine;
  TfpModelShadow *models;
  const TfpHeadBuild *build;
  /* self */
  TfpHeadPhase phase;
  bool closing, has_model_shadow_error;
  TfpVisText reason;
  double hidden, hidden_head, hidden_torso, hidden_shoulders, hidden_body, hidden_backpack;
  double preserved_partial, writes, restores, retired, foreign_masks, shadows_disabled, steady_observations, full_observations;
  char *model_shadow_error; size_t model_shadow_error_len;
  /* upvalues */
  SMap *owned, *ignored;      /* key -> Row; key -> pass bits (entry.num) */
  TfpVisIdentity identity;
  Observed *observed;
  Steady *steady;
  NMap *handles;              /* unit reference -> SDK Unit (entry.raw); entry.flag false is Lua false */
  unsigned long long handles_table;
  bool has_cache_world, has_cache_time, has_cache_request, has_checked, has_epoch, has_torso_steady_since;
  double cache_world, cache_time, exe, checked, checked_game, update_serial, torso_steady_since;
  TfpEngineWorld cache_sdk_world;
  char *cache_request;
  Setter setter;
  struct { TfpVisIdentity owner; double kind, record, head, tail, weapon, serial, started; bool ads, aim; } epoch;
  /* machinery */
  Obj *objects;
  jmp_buf *jump;
  TfpVisText error;
};

static void *new_obj(TfpHead *h, Kind kind, size_t size) {
  Obj *o = calloc(1, size);
  o->kind = (uint8_t)kind;
  o->next = h->objects;
  h->objects = o;
  return o;
}
static NMap *new_nmap(TfpHead *h) { return new_obj(h, K_NMAP, sizeof(NMap)); }
static SMap *new_smap(TfpHead *h) { return new_obj(h, K_SMAP, sizeof(SMap)); }
static void mark(Obj *o) {
  if (!o || o->mark) return;
  o->mark = 1;
  switch ((Kind)o->kind) {
  case K_NMAP: for (size_t i = 0; i < ((NMap *)o)->n; i++) mark(((NMap *)o)->e[i].obj); break;
  case K_SMAP: for (size_t i = 0; i < ((SMap *)o)->n; i++) mark(((SMap *)o)->e[i].obj); break;
  case K_LAYOUT: mark((Obj *)((Layout *)o)->indices); break;
  case K_SPEC: mark((Obj *)((Spec *)o)->rows); mark((Obj *)((Spec *)o)->layout); break;
  case K_OBSERVED: mark((Obj *)((Observed *)o)->refs); mark((Obj *)((Observed *)o)->kept_layouts); break;
  case K_STEADY:
    mark((Obj *)((Steady *)o)->observed); mark((Obj *)((Steady *)o)->wanted);
    mark((Obj *)((Steady *)o)->layouts); mark((Obj *)((Steady *)o)->shapes);
    break;
  case K_SHAPE: mark((Obj *)((Shape *)o)->layout); break;
  case K_ROW: case K_BLOB: break;
  }
}
static void release(Obj *o) {
  switch ((Kind)o->kind) {
  case K_NMAP: free(((NMap *)o)->e); break;
  case K_SMAP: free(((SMap *)o)->e); break;
  case K_LAYOUT: free(((Layout *)o)->bytes); break;
  case K_OBSERVED: free(((Observed *)o)->model_signature); break;
  default: break;
  }
  free(o);
}
static void collect_garbage(TfpHead *h) {
  mark((Obj *)h->owned); mark((Obj *)h->ignored); mark((Obj *)h->observed); mark((Obj *)h->steady); mark((Obj *)h->handles);
  for (Obj **at = &h->objects; *at;) {
    Obj *o = *at;
    if (o->mark) { o->mark = 0; at = &o->next; }
    else { *at = o->next; release(o); }
  }
}

static NEntry *nget(const NMap *m, double key) {
  if (!m) return NULL;
  for (size_t i = 0; i < m->n && m->e[i].key <= key; i++) if (m->e[i].key == key) return &m->e[i];
  return NULL;
}
static NEntry *nset(NMap *m, double key) {
  size_t at = 0;
  while (at < m->n && m->e[at].key < key) at++;
  if (at < m->n && m->e[at].key == key) return &m->e[at];
  if (m->n == m->room) m->e = realloc(m->e, (m->room = m->room * 2 + 8) * sizeof *m->e);
  memmove(&m->e[at + 1], &m->e[at], (m->n - at) * sizeof *m->e);
  m->e[at] = (NEntry){.key = key};
  m->n++;
  return &m->e[at];
}
static void ndel(NMap *m, double key) {
  NEntry *e = nget(m, key);
  if (!e) return;
  memmove(e, e + 1, (size_t)(m->e + m->n - (e + 1)) * sizeof *e);
  m->n--;
}
static SEntry *sget(const SMap *m, const char *key) {
  if (!m) return NULL;
  for (size_t i = 0; i < m->n; i++) {
    int order = strcmp(m->e[i].key, key);
    if (order == 0) return &m->e[i];
    if (order > 0) break;
  }
  return NULL;
}
static SEntry *sset(SMap *m, const char *key) {
  size_t at = 0;
  while (at < m->n && strcmp(m->e[at].key, key) < 0) at++;
  if (at < m->n && strcmp(m->e[at].key, key) == 0) return &m->e[at];
  if (m->n == m->room) m->e = realloc(m->e, (m->room = m->room * 2 + 8) * sizeof *m->e);
  memmove(&m->e[at + 1], &m->e[at], (m->n - at) * sizeof *m->e);
  memset(&m->e[at], 0, sizeof *m->e);
  snprintf(m->e[at].key, KEY, "%s", key);
  m->n++;
  return &m->e[at];
}
static void sdel(SMap *m, const char *key) {
  SEntry *e = sget(m, key);
  if (!e) return;
  memmove(e, e + 1, (size_t)(m->e + m->n - (e + 1)) * sizeof *e);
  m->n--;
}
/* The keys of a table as they are when a walk starts. */
static SMap *skeys(TfpHead *h, const SMap *m) {
  SMap *copy = new_smap(h);
  copy->e = malloc((m->n + 1) * sizeof *copy->e);
  if (m->n) memcpy(copy->e, m->e, m->n * sizeof *copy->e);
  copy->n = copy->room = m->n;
  return copy;
}

/* ---- errors ---- */
static _Noreturn void raise(TfpHead *h) {
  h->error.present = true;
  longjmp(*h->jump, 1);
}
static _Noreturn void fail(TfpHead *h, int line, const char *message) {
  char text[TFP_VIS_TEXT];
  snprintf(text, sizeof text, "%s", message);   /* the message may be h->error.text itself */
  snprintf(h->error.text, sizeof h->error.text, CHUNK ":%d: %.400s", line, text);
  raise(h);
}
#define CHECK(condition, line, message) do { if (!(condition)) fail(h, line, message); } while (0)
static bool protect(TfpHead *h, void (*call)(TfpHead *, void *), void *arg) {
  jmp_buf here;
  jmp_buf *outer = h->jump;
  h->jump = &here;
  bool ok = setjmp(here) == 0;
  if (ok) call(h, arg);
  h->jump = outer;
  return ok;
}

/* ---- numbers ---- */
static double lmod(double a, double b) { return a - floor(a / b) * b; }                 /* Lua's % */
static bool has(double value, double mask) { return lmod(value, mask * 2) >= mask; }     /* :16 */
static double assign(double value, double mask, bool on) {                               /* :17-20 */
  if (has(value, mask) == on) return value;
  return value + (on ? mask : -mask);
}
static double integer(const uint8_t *bytes, size_t offset) {   /* :82-86; every caller's buffer is long enough */
  return bytes[offset] + bytes[offset + 1] * 256.0 + bytes[offset + 2] * 65536.0 + bytes[offset + 3] * 16777216.0;
}
static void make_key(char *key, double ref, double mesh) {   /* :348 */
  char a[24], b[23];   /* "%.14g" is at most 21 characters */
  tfp_vis_number(ref, a, sizeof a); tfp_vis_number(mesh, b, sizeof b);
  snprintf(key, KEY, "%s:%s", a, b);
}

/* ---- memory (:78-95) ---- */
static void rd(TfpHead *h, double at, size_t n, uint8_t *out) {
  CHECK(at >= 65536 && at + (double)n < HIGH && n > 0 && n <= 256, 79, "Head visibility read bounds changed");
  if (!h->io->read(h->io->user, at, n, out, &h->error)) raise(h);
}
static double u32(TfpHead *h, double at) { uint8_t b[4]; rd(h, at, 4, b); return integer(b, 0); }
static double pointer(TfpHead *h, const uint8_t *bytes, size_t offset) {
  double value = integer(bytes, offset) + integer(bytes, offset + 4) * 4294967296.0;
  CHECK(value >= 65536 && value < HIGH, 91, "Head visibility pointer unavailable");
  return value;
}
static double ptr(TfpHead *h, double at) { uint8_t b[8]; rd(h, at, 8, b); return pointer(h, b, 0); }
static double byte_at(TfpHead *h, double at) { uint8_t b[1]; rd(h, at, 1, b); return b[0]; }
static bool bytes_at(TfpHead *h, double at, size_t n, const uint8_t *expected) {
  uint8_t now[256];
  rd(h, at, n, now);
  return memcmp(now, expected, n) == 0;
}
static double address(TfpHead *h, const void *handle) { return h->io->userdata_address(h->io->user, handle); }
static double handle_ref(TfpHead *h, const void *handle) { return floor(address(h, handle) / 4); }   /* :49 */
static double world_address(TfpHead *h, TfpEngineWorld world) {                                     /* :51 */
  double at = address(h, world), value;
  if (!h->io->ptr(h->io->user, at, &value, &h->error)) raise(h);
  return value;
}

/* ---- the engine ---- */
static _Noreturn void engine_raise(TfpHead *h, const TfpEngineError *error) {
  snprintf(h->error.text, sizeof h->error.text, "%s", error->text);
  raise(h);
}
static _Noreturn void no_function(TfpHead *h, int line, const char *field) {
  char text[96];
  snprintf(text, sizeof text, "attempt to call field '%s' (a nil value)", field);
  fail(h, line, text);
}
static Setter current_setter(const TfpHead *h) { return h->engine ? h->engine->Unit_set_mesh_visibility : NULL; }

/* ---- snapshots and options ---- */
static bool view_snapshot(TfpHead *h, TfpVisSnapshot *out, TfpVisText *why) {   /* :55-63 */
  TfpVisText unused = {0};
  why->present = false;
  if (h->io->pinned_ready && h->io->pin_player_snapshot && h->io->pin_player_snapshot(h->io->user, out, &unused)) return true;
  if (!h->io->player_snapshot) {
    why->present = true;
    snprintf(why->text, sizeof why->text, "attempt to call a nil value");
    return false;
  }
  return h->io->player_snapshot(h->io->user, out, why);
}
static bool pinned_ready(TfpHead *h, const TfpVisSnapshot *s) {   /* :64-68 */
  if (!s || !h->io->pinned_ready) return false;
  return h->io->pinned_ready(h->io->user, s);
}
static bool foreign(TfpHead *h) {   /* :278-281 */
  if (!h->io->foreign_camera) return false;
  return h->io->foreign_camera(h->io->user) != 0;
}
static bool seated_head(TfpHead *h) { return h->io->seated_head && h->io->seated_head(h->io->user); }   /* :282 */
static bool available(TfpHead *h, const TfpVisSnapshot *s) {   /* :284-291 */
  if (!s || !s->player_identity.present || s->player_identity.len == 0) return false;
  if (pinned_ready(h, s)) return true;
  if ((s->camera_kind == 4 || s->camera_kind == 6) && !s->seated && (s->safe || s->weapon_transition || s->head_camera_transition)) return true;
  return s->camera_kind == 4 && s->seated && seated_head(h);
}
static bool first_person(TfpHead *h, const TfpVisSnapshot *s) {   /* :292-303 */
  TfpHeadActionState state;
  if (pinned_ready(h, s)) return true;
  if (s->camera_kind == 6) return true;
  if (!h->io->fallback_ready) return false;
  if (h->io->fallback_ready(h->io->user, s) != 1) return false;
  if (!h->io->action_state || !h->io->action_state(h->io->user, s, &state)) return false;
  if (!(tfp_vis_identity_equal(&state.owner, &s->player_identity) && state.body == s->body)) return false;
  return h->io->on_foot && h->io->on_foot(h->io->user, s, &state) == 1;
}
static void report(TfpHead *h, TfpHeadPhase phase, const char *reason) {   /* :73-77 */
  char text[TFP_VIS_TEXT];
  h->phase = phase;
  if (reason) {
    snprintf(text, sizeof text, "%s", reason);
    if (!(h->reason.present && strcmp(h->reason.text, text) == 0) && h->io->log) {
      char line[TFP_VIS_TEXT + 32];
      snprintf(line, sizeof line, "FPV head visibility: %s", text);
      h->io->log(h->io->user, line);
    }
    memcpy(h->reason.text, text, sizeof text);
  }
  h->reason.present = reason != NULL;
}

/* ---- the executable and the unit registry ---- */
const TfpHeadBuild tfp_head_build_retail = {
  0x3f4ca0, {0x48, 0x8b, 0xd1, 0xe9, 0xa8, 0xf8, 0xff, 0xff},
  0x51fe80, {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x44, 0x0f, 0xb6, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x3f4c23, {0x3b, 0x3d, 0x97, 0x0e, 0x48, 0x02, 0x75, 0x04, 0xb1, 0x01, 0xeb, 0x36, 0x3b, 0x3d, 0x93, 0x0e, 0x48, 0x02, 0x75, 0x04, 0xb1, 0x02, 0xeb, 0x2a},
  0x51feaa, {0x84, 0xd2, 0x74, 0x05, 0x45, 0x0a, 0xc8, 0xeb, 0x06, 0x41, 0xf6, 0xd0, 0x45, 0x22, 0xc8, 0x48, 0x8b, 0x59, 0x08, 0x44, 0x88, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x2875ac0, 0xF2760503, 0x2875ac8, 0x3100C3D2,
  0xa06d0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x35, 0x12, 0x70, 0xa1, 0x01},
  0x1ab76f8, 0x28d3e0, 0x293600, 0x546ab0};
const TfpHeadBuild tfp_head_build_25480438 = {
  0x4128e0, {0x48, 0x8b, 0xd1, 0xe9, 0xe8, 0xfb, 0xff, 0xff},                       /* feature_contract.h FP_FEATURE_MESH */
  0x50f620, {0x40, 0x57, 0x48, 0x83, 0xec, 0x30, 0x44, 0x0f, 0xb6, 0x89, 0xdc, 0x00, 0x00, 0x00},   /* feature_contract.h */
  0x412875, {0x3b, 0x3d, 0x35, 0xbd, 0x3b, 0x02, 0x75, 0x04, 0xb1, 0x01, 0xeb, 0x36, 0x3b, 0x3d, 0x41, 0xbd, 0x3b, 0x02, 0x75, 0x04, 0xb1, 0x02, 0xeb, 0x2a},
  0x50f64a, {0x84, 0xd2, 0x74, 0x05, 0x45, 0x0a, 0xc8, 0xeb, 0x06, 0x41, 0xf6, 0xd0, 0x45, 0x22, 0xc8, 0x48, 0x8b, 0x59, 0x08, 0x44, 0x88, 0x89, 0xdc, 0x00, 0x00, 0x00},
  0x27ce5b0, 0xF2760503, 0x27ce5c8, 0x3100C3D2,                                     /* build_map; the two addresses context_a compares with */
  0x9d8c0, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x35, 0x1a, 0x28, 0x97, 0x01},
  0x1a100f0,                                                                        /* profile.h FP_REGISTRY; what registry_code loads */
  0x2bd890, 0x2c3990, 0x546a70};                                                    /* build_map */
/* Beyond retail: 0 is "not filled yet". The engine fills both ids inside its own Unit.set_mesh_visibility the first
 * time that runs (a guarded static before exe+0x412875 on build 25480438); on HellForge nothing calls it before this
 * module's first write. The two code sites that compare with them are still checked byte for byte above. */
static bool pass_id(TfpHead *h, double at, uint32_t value) {
  uint32_t seen = u32(h, at);
  return seen == value || seen == 0;
}
static void contract(TfpHead *h, const TfpVisSnapshot *s) {   /* :97-121 */
  const TfpEngineIface *sr = h->engine;
  CHECK(sr && sr->Application_main_world && sr->World_units && sr->Unit_set_mesh_visibility, 98, "Head mesh SDK unavailable");
  CHECK(!h->setter || h->setter == sr->Unit_set_mesh_visibility || h->owned->n == 0, 101,
        "Head mesh SDK binding changed; original restoration remains pending");
  if (h->has_checked && h->setter == sr->Unit_set_mesh_visibility && s->base == h->checked_game) return;
  double exe;
  if (!h->io->alignment_executable(h->io->user, &exe, &h->error)) raise(h);
  h->exe = exe;
  const TfpHeadBuild *b = h->build;
  CHECK(bytes_at(h, exe + b->binding, 8, b->binding_bytes), 113, "Head mesh visibility binding changed");
  CHECK(bytes_at(h, exe + b->mask_layout, 14, b->mask_layout_bytes), 114, "Head mesh pass-mask layout changed");
  CHECK(bytes_at(h, exe + b->context_a, 24, b->context_a_bytes) && bytes_at(h, exe + b->context_b, 26, b->context_b_bytes)
        && pass_id(h, exe + b->context_default, b->context_default_value) && pass_id(h, exe + b->context_shadow, b->context_shadow_value), 115,
        "Head camera/shadow visibility contexts changed");
  CHECK(bytes_at(h, exe + b->registry_code, 22, b->registry_code_bytes), 119, "Head Unit registry changed");
  h->checked = exe; h->has_checked = true; h->checked_game = s->base;
  h->setter = sr->Unit_set_mesh_visibility;
  h->observed = NULL;
}
typedef struct Registry { double address, objects, capacity, generations; NMap *types; } Registry;
static Registry registry(TfpHead *h) {   /* :122-127 */
  Registry r;
  uint8_t fields[32];
  r.address = ptr(h, h->exe + h->build->unit_registry);
  rd(h, r.address + 0x88, 32, fields);
  r.capacity = integer(fields, 16);
  CHECK(r.capacity > 0 && r.capacity <= 0x400000, 125, "Head Unit registry capacity changed");
  r.objects = pointer(h, fields, 0);
  r.generations = pointer(h, fields, 24);
  r.types = new_nmap(h);
  return r;
}
/* :128-146. false: nil. `context` NULL: a fresh registry(). */
static bool unit(TfpHead *h, bool has_ref, double ref, bool has_expected, double expected, Registry *context, double *object, uint8_t header[32]) {
  Registry fresh;
  CHECK(has_ref && ref > 0 && ref < NONE && ref == floor(ref), 129, "Head Unit reference invalid");
  if (!context) { fresh = registry(h); context = &fresh; }
  double index = lmod(ref, 0x400000), generation = lmod(floor(ref / 0x400000), 256);
  CHECK(index < context->capacity, 132, "Head Unit index changed");
  if (byte_at(h, context->generations + index) != generation) return false;
  *object = ptr(h, context->objects + index * 8);
  if (has_expected && expected != *object) return false;
  rd(h, *object, 32, header);
  if (byte_at(h, context->generations + index) != generation || ptr(h, context->objects + index * 8) != *object) return false;
  CHECK(integer(header, 8) == ref, 138, "Head Unit self reference changed");
  double vt = pointer(h, header, 0);
  if (!nget(context->types, vt)) {
    uint8_t methods[56];
    rd(h, vt + 0xC8, 56, methods);
    CHECK(pointer(h, methods, 48) == h->exe + h->build->renderer_method_6 && pointer(h, methods, 0) == h->exe + h->build->renderer_method_0,
          142, "Head renderer type changed");
    nset(context->types, vt);
  }
  return true;
}
static double mesh_fields(TfpHead *h, double object, bool *has_array, double *array) {   /* :147-151 */
  uint8_t fields[16];
  rd(h, object + 0xC8, 16, fields);
  double count = integer(fields, 0);
  CHECK(count <= 128, 149, "Head mesh count changed");
  *has_array = count > 0;
  *array = *has_array ? pointer(h, fields, 8) : 0;
  return count;
}
static Layout *mesh_layout(TfpHead *h, double object) {   /* :152-156 */
  Layout *layout = new_obj(h, K_LAYOUT, sizeof *layout);
  layout->count = mesh_fields(h, object, &layout->has_array, &layout->array);
  layout->len = (size_t)layout->count * 16;
  layout->bytes = calloc(1, layout->len + 1);
  for (size_t offset = 0; offset < layout->len; offset += 256) {
    size_t n = layout->len - offset < 256 ? layout->len - offset : 256;
    rd(h, layout->array + (double)offset, n, layout->bytes + offset);
  }
  return layout;
}
static Layout *index_layout(TfpHead *h, Layout *layout) {   /* :157-164 */
  NMap *indices = new_nmap(h);
  for (int index = 1; index <= (int)layout->count; index++) {
    double mesh = pointer(h, layout->bytes, (size_t)(index - 1) * 16);
    CHECK(!nget(indices, mesh), 161, "Head mesh object is ambiguous");
    nset(indices, mesh)->a = index;
  }
  layout->indices = indices;
  return layout;
}
static bool same_layout(const Layout *a, const Layout *b) {   /* count, array and bytes */
  return a->count == b->count && a->has_array == b->has_array && (!a->has_array || a->array == b->array)
    && a->len == b->len && memcmp(a->bytes, b->bytes, a->len) == 0;
}

/* :189-204. false: nil. */
static bool rig_mesh(TfpHead *h, TfpEngineUnit handle, const Pad *pad, double *out) {
  const TfpEngineIface *sr = h->engine;
  TfpEngineError error = {{0}};
  TfpEngineMesh mesh = NULL;
  bool present = false, material = false;
  double name = 0, node = 0, count = 0;
  if (!sr->Unit_has_mesh) no_function(h, 190, "has_mesh");
  if (!sr->Unit_has_mesh(sr->user, handle, pad->name, &present, &error)) engine_raise(h, &error);
  if (!present) return false;
  if (!sr->Unit_mesh) no_function(h, 193, "mesh");
  if (!sr->Unit_mesh(sr->user, handle, pad->name, &mesh, &error)) engine_raise(h, &error);
  CHECK(mesh, 193, "Named rig shoulder unavailable");
  if (!sr->Mesh_name) no_function(h, 194, "name");
  if (!sr->Mesh_name(sr->user, mesh, &name, &error)) engine_raise(h, &error);
  if (!sr->Mesh_node) no_function(h, 194, "node");
  if (!sr->Mesh_node(sr->user, mesh, &node, &error)) engine_raise(h, &error);
  if (!sr->Mesh_num_materials) no_function(h, 194, "num_materials");
  if (!sr->Mesh_num_materials(sr->user, mesh, &count, &error)) engine_raise(h, &error);
  if (!sr->Mesh_has_material) no_function(h, 195, "has_material");
  if (!sr->Mesh_has_material(sr->user, mesh, "m_armor_standard", &material, &error)) engine_raise(h, &error);
  /* A readable replacement is retired; an unavailable query keeps an old
   * lease pending rather than restoring into geometry we cannot identify. */
  if (lmod(name, 4294967296.0) != pad->hash || node != pad->node || count != 1 || !material) return false;
  double at = address(h, mesh);
  CHECK(at >= 65536 && at < HIGH, 202, "Rig shoulder address unavailable");
  *out = at;
  return true;
}
/* :168-182. false: nil. A layout is observed once per renderer in an operation; generation, world,
 * array and slot checks still surround every SDK write. */
static bool locate(TfpHead *h, Row *row, NMap *layouts, bool check_rig, double *index_out) {
  NEntry *known = nget(layouts, row->ref);
  Layout *layout = known ? (Layout *)known->obj : NULL;
  double object, array, pad_mesh;
  uint8_t header[32];
  bool has_array;
  if (!unit(h, true, row->ref, true, row->object, NULL, &object, header) || pointer(h, header, 24) != row->world) return false;
  if (row->rig && (!bytes_at(h, object + 0x358, 8, RIG_RESOURCE)
                   || (check_rig && row->rig_pad && !(rig_mesh(h, row->handle, row->rig_pad, &pad_mesh) && pad_mesh == row->mesh)))) return false;
  CHECK(handle_ref(h, row->handle) == row->ref, 174, "Head SDK handle changed");
  double count = mesh_fields(h, object, &has_array, &array);
  NEntry *index = layout ? nget(layout->indices, row->mesh) : NULL;
  if (!layout || layout->has_array != has_array || (has_array && layout->array != array) || layout->count != count
      || (index && ptr(h, array + (index->a - 1) * 16) != row->mesh)) {
    layout = index_layout(h, mesh_layout(h, object));
    nset(layouts, row->ref)->obj = &layout->o;
    index = nget(layout->indices, row->mesh);
  }
  if (!index) return false;
  *index_out = index->a;
  return true;
}
static double mask(TfpHead *h, const Row *row) { return byte_at(h, row->mesh + 0xDC); }   /* :183 */
static void set_pass(TfpHead *h, Row *row, double index, bool visible, const char *pass) {   /* :184-188 */
  TfpEngineError error = {{0}};
  CHECK(current_setter(h) == row->setter, 185, "Head mesh SDK binding ownership changed");
  if (!row->setter(h->engine->user, row->handle, (int)index, visible, pass, &error)) engine_raise(h, &error);
  CHECK(current_setter(h) == row->setter, 187, "Head mesh SDK binding changed during visibility update");
}
static const Pass *passes(const Row *row, int *count) {   /* :10 */
  if (row->shadow_only) { *count = 1; return SHADOW_ONLY; }
  *count = 2;
  return row->shadow_suppressed ? HIDDEN_NO_SHADOW : PASSES;
}

/* ---- restore (:205-236) ---- */
typedef struct Restore { const char *key; Row *row; NMap *layouts; const TfpVisSnapshot *exiting; const Pass *pass; } Restore;
static void restore_pass(TfpHead *h, void *arg) {   /* :217-231 */
  Restore *q = arg;
  Row *row = q->row;
  const Pass *pass = q->pass;
  double index;
  CHECK(locate(h, row, q->layouts, true, &index), 218, "Head mesh retired during restore");
  double before = mask(h, row);
  if (has(before, pass->mask) != has(row->written, pass->mask)) {
    h->foreign_masks += 1;
    SEntry *ignored = sset(h->ignored, q->key);
    ignored->num = assign(ignored->num, pass->mask, true);
  } else {
    double unused;
    if (pass->mask == 1 && has(row->before, 1)) row->restoring_default = true;
    set_pass(h, row, index, has(row->before, pass->mask), pass->name);
    CHECK(locate(h, row, q->layouts, true, &unused) && mask(h, row) == assign(before, pass->mask, has(row->before, pass->mask)),
          226, "Head mesh pass restore readback failed");
    h->restores += 1;
  }
  row->bits = assign(row->bits, pass->mask, false);
}
static void restore(TfpHead *h, void *arg) {
  Restore *q = arg;
  Row *row = q->row;
  const TfpVisSnapshot *exiting = q->exiting;
  double index;
  if (!locate(h, row, q->layouts, true, &index)) { h->retired += 1; sdel(h->owned, q->key); return; }
  double current = mask(h, row);
  /* Native exit can publish a complete visible mask after our shadow-only
   * 0->2 write. That publication supersedes the old visibility lease;
   * clearing its shadow bit would incorrectly turn TPV255 into253. */
  if (!row->restoring_default && has(current, 1) && !has(row->written, 1)
      && (current == 255 || (exiting && exiting->camera_kind == 4 && tfp_vis_identity_equal(&exiting->player_identity, &row->owner)
                             && exiting->body == row->body_ref))) { sdel(h->owned, q->key); return; }
  char problem[TFP_VIS_TEXT];
  bool has_problem = false;
  int count;
  const Pass *list = passes(row, &count);
  for (int i = 0; i < count; i++) if (has(row->bits, list[i].mask)) {
    q->pass = &list[i];
    if (!protect(h, restore_pass, q)) { has_problem = true; memcpy(problem, h->error.text, sizeof problem); }
  }
  if (row->bits == 0) sdel(h->owned, q->key);
  CHECK(!has_problem, 235, problem);
}
static double count_owned(TfpHead *h) {   /* :237-247 */
  double total = 0, head = 0, torso = 0, shoulder = 0, body = 0, backpack = 0, shadows = 0;
  for (size_t i = 0; i < h->owned->n; i++) {
    const Row *row = (const Row *)h->owned->e[i].obj;
    total += 1;
    if (row->shadow_suppressed && has(row->bits, 2)) shadows += 1;
    if (row->shadow_only) continue;
    if (row->part == PART_TORSO) torso += 1; else if (row->part == PART_SHOULDER) shoulder += 1;
    else if (row->part == PART_BODY) body += 1; else if (row->part == PART_BACKPACK) backpack += 1; else head += 1;
  }
  h->hidden = total; h->hidden_head = head; h->hidden_torso = torso; h->hidden_shoulders = shoulder;
  h->hidden_body = body; h->hidden_backpack = backpack;
  h->shadows_disabled = shadows;
  return total;
}
static bool stop(TfpHead *h, TfpVisText *why) {   /* :249-256 */
  char reason[TFP_VIS_TEXT];
  bool has_reason = false, has_exiting = false;
  TfpVisSnapshot exiting;
  TfpVisText unused;
  NMap *layouts = new_nmap(h);
  h->steady = NULL;
  if (h->owned->n) has_exiting = view_snapshot(h, &exiting, &unused);
  SMap *keys = skeys(h, h->owned);
  for (size_t i = 0; i < keys->n; i++) {
    SEntry *live = sget(h->owned, keys->e[i].key);
    if (!live) continue;
    Restore q = {keys->e[i].key, (Row *)live->obj, layouts, has_exiting ? &exiting : NULL, NULL};
    if (!protect(h, restore, &q)) { has_reason = true; memcpy(reason, h->error.text, sizeof reason); }
  }
  if (count_owned(h) > 0) {
    report(h, TFP_HEAD_CLEANUP_PENDING, has_reason ? reason : "Head mesh restoration pending");
    if (why) *why = h->reason;
    return false;
  }
  h->identity.present = false; h->observed = NULL; h->ignored = new_smap(h); h->has_epoch = false;
  report(h, TFP_HEAD_IDLE, NULL);
  if (why) why->present = false;
  return true;
}

/* ---- collect (:304-372) ---- */
typedef struct Collect {
  double ref; Spec *spec; const TfpVisSnapshot *s; Registry *context; double body; const uint8_t *body_header;
  double world_native, base; bool unchanged;
  Layout *layout; bool changed;   /* results */
} Collect;
static void collect(TfpHead *h, void *arg) {
  Collect *q = arg;
  Spec *spec = q->spec;
  const TfpVisSnapshot *s = q->s;
  double ref = q->ref, body = q->body, object;
  uint8_t header[32], resource[8] = {0};
  bool found = true;
  if (spec->body_only) { object = body; memcpy(header, q->body_header, 32); }
  else found = unit(h, true, ref, false, 0, q->context, &object, header);
  CHECK(found, 307, "Head customization generation retired");
  CHECK(pointer(h, header, 24) == q->world_native
        && ((spec->body_only && object == body) || (spec->rig && s->has_viewmodel && ref == s->viewmodel)
            || (spec->model_shadow && ptr(h, object + 0x1D0) == spec->model_parent)
            || (!spec->body_only && !spec->rig && !spec->model_shadow && ptr(h, object + 0x1D0) == body)),
        308, "Head customization attachment changed");
  NEntry *held = nget(h->handles, ref);
  CHECK(held && held->flag, 312, "Head SDK Unit handle unavailable");
  TfpEngineUnit handle = held->raw;
  CHECK(handle_ref(h, handle) == ref, 313, "Head SDK handle changed");
  if (spec->rig) rd(h, object + 0x358, 8, resource);
  bool is_rig = spec->rig && memcmp(resource, RIG_RESOURCE, 8) == 0;
  Layout *layout;
  if (spec->rig && !is_rig) {
    layout = new_obj(h, K_LAYOUT, sizeof *layout);
    layout->bytes = calloc(1, 1);
    layout->indices = new_nmap(h);
  } else layout = mesh_layout(h, object);
  Layout *old = q->unchanged ? spec->layout : NULL;
  bool same = old && spec->has_object && spec->object == object && spec->has_resource == spec->rig
    && (!spec->rig || memcmp(spec->resource, resource, 8) == 0) && same_layout(old, layout);
  if (same) layout = old;
  if (same && spec->has_retry) {
    double now = h->io->clock(h->io->user);
    if (now >= spec->retry - .5 && now < spec->retry) { q->layout = layout; q->changed = false; return; }
  }
  bool changed = !same || spec->has_retry;
  if (!same || spec->has_retry) {
    if (!same) layout = index_layout(h, layout);
    /* A weapon-tree change can rebuild specs while these renderers
     * remain unchanged. Keep their last observation even when they
     * own no bits, so a foreign pass change is not mistaken for an
     * unseen native baseline. This history cannot authorize a write. */
    const Observed *seen = h->observed;
    const NEntry *previous = seen && tfp_vis_identity_equal(&seen->owner, &s->player_identity) && seen->body_ref == s->body
      && seen->body == body && seen->world == q->world_native && seen->customization == q->base ? nget(seen->refs, ref) : NULL;
    const SMap *history = previous ? ((const Spec *)previous->obj)->rows : NULL;
    spec->rows = new_smap(h);
    spec->layout = layout; spec->object = object; spec->has_object = true;
    spec->has_resource = spec->rig; memcpy(spec->resource, resource, 8);
    spec->has_retry = false;
    const Pad *selected[2] = {NULL, NULL};
    double selected_mesh[2] = {0, 0};
    bool has_selected = false;
    if (is_rig) {
      CHECK(layout->count == 5, 339, "Known rig shoulder layout changed");
      has_selected = true;
      for (int i = 0; i < 2; i++) {
        double mesh;
        CHECK(rig_mesh(h, handle, &RIG_PADS[i], &mesh), 341, "Rig shoulder mesh identity changed");
        CHECK(nget(layout->indices, mesh) && !(i == 1 && selected_mesh[0] == mesh), 342, "Named shoulder is not a unique current rig mesh");
        selected[i] = &RIG_PADS[i]; selected_mesh[i] = mesh;
      }
    }
    for (size_t i = 0; i < layout->indices->n; i++) {
      double mesh = layout->indices->e[i].key;
      const Pad *pad = has_selected ? (selected_mesh[0] == mesh ? selected[0] : selected_mesh[1] == mesh ? selected[1] : NULL) : NULL;
      if (!(!spec->rig || spec->shadow_rig || pad)) continue;
      char key[KEY];
      make_key(key, ref, mesh);
      const SEntry *past = sget(history, key);
      Row *earlier = past ? (Row *)past->obj : NULL;
      if (earlier && (!tfp_vis_identity_equal(&earlier->owner, &s->player_identity) || earlier->body_ref != s->body
                      || earlier->handle != handle || earlier->setter != h->setter || earlier->part != spec->part)) earlier = NULL;
      const SEntry *mine = sget(h->owned, key);
      Row *existing = mine ? (Row *)mine->obj : earlier;
      bool shadow_suppressed = spec->shadow_rig || spec->model_shadow;
      bool shadow_only = spec->model_shadow || (spec->shadow_rig && !pad);
      Row *row = existing && existing->object == object && existing->body_object == body
        && existing->shadow_suppressed == shadow_suppressed && existing->shadow_only == shadow_only
        && existing->has_model_parent == spec->has_model_parent && (!spec->has_model_parent || existing->model_parent == spec->model_parent)
        ? existing : NULL;
      if (!row) {
        row = new_obj(h, K_ROW, sizeof *row);
        row->ref = ref; row->object = object; row->world = q->world_native; row->handle = handle; row->mesh = mesh;
        row->setter = h->setter; row->body_ref = s->body; row->body_object = body; row->customization = q->base;
        row->owner = s->player_identity;
      }
      memcpy(row->offsets, spec->offsets, sizeof row->offsets); row->offset_count = spec->offset_count;
      row->part = spec->part; row->body_only = spec->body_only; row->rig = spec->rig;
      row->has_backpack = spec->has_backpack; row->backpack = spec->backpack;
      row->rig_pad = pad;
      row->shadow_suppressed = shadow_suppressed;
      row->shadow_only = shadow_only;
      row->has_model_owner = spec->has_model_owner; row->model_owner = spec->model_owner;
      row->has_model_parent = spec->has_model_parent; row->model_parent = spec->model_parent;
      sset(spec->rows, key)->obj = &row->o;
    }
  }
  for (size_t i = 0; spec->rows && i < spec->rows->n; i++) {
    Row *row = (Row *)spec->rows->e[i].obj;
    row->current = mask(h, row);
  }
  q->layout = layout; q->changed = changed;
}

/* ---- inventory (:373-385) ---- */
typedef struct Inventory { TfpEngineWorld world; NMap *refs, *found; } Inventory;
static void inventory(TfpHead *h, void *arg) {
  Inventory *q = arg;
  const TfpEngineUnit *list = NULL;
  size_t count = 0;
  TfpEngineError error = {{0}};
  if (!h->engine->World_units(h->engine->user, q->world, &list, &count, &error)) engine_raise(h, &error);
  CHECK(list && count <= 131072, 374, "Head SDK Unit inventory unavailable");
  NMap *found = new_nmap(h);
  for (size_t i = 0; i < count; i++) {
    double ref = handle_ref(h, list[i]);
    const NEntry *wanted = nget(q->refs, ref);
    if (!wanted) continue;
    NEntry *seen = nget(found, ref);
    if (seen) {
      CHECK(((const Spec *)wanted->obj)->rig, 380, "Head SDK Unit reference is ambiguous");
      seen->flag = false; seen->raw = NULL;
    } else {
      seen = nset(found, ref);
      seen->flag = true; seen->raw = list[i];
    }
  }
  q->found = found;
}

/* ---- resolve (:386-542) ---- */
typedef struct Resolve { const TfpVisSnapshot *s; bool refresh; SMap *wanted; NMap *layouts; } Resolve;
typedef struct Keep { const TfpVisSnapshot *s; Registry *context; double body, world_native; NMap *kept, *kept_layouts; } Keep;
static void keep(TfpHead *h, Keep *k, double ref) {   /* :509-522 */
  double object, rig;
  uint8_t part_header[32], rig_header[32];
  if (ref == 0 || ref == NONE || nget(k->kept, ref)) return;
  CHECK(unit(h, true, ref, false, 0, k->context, &object, part_header), 511, "Kept limb generation retired");
  double parent = ptr(h, object + 0x1D0);
  if (parent != k->body) {
    bool found = unit(h, k->s->has_viewmodel, k->s->viewmodel, false, 0, k->context, &rig, rig_header);
    CHECK(found && parent == rig && pointer(h, rig_header, 24) == k->world_native, 515, "Kept limb rig attachment changed");
  }
  CHECK(pointer(h, part_header, 24) == k->world_native, 517, "Kept limb world changed");
  Layout *layout = mesh_layout(h, object);
  nset(k->kept, ref);
  NEntry *entry = nset(k->kept_layouts, ref);
  entry->obj = &layout->o; entry->a = object; entry->b = parent;
  for (int i = 1; i <= (int)layout->count; i++) nset(k->kept, pointer(h, layout->bytes, (size_t)(i - 1) * 16));
}
static Spec *new_spec(TfpHead *h, Part part) {
  Spec *spec = new_obj(h, K_SPEC, sizeof *spec);
  spec->part = part;
  return spec;
}
static bool blob_equal(bool has_a, const uint8_t *a, size_t a_len, bool has_b, const uint8_t *b, size_t b_len) {
  if (!has_a || !has_b) return has_a == has_b;
  return a_len == b_len && memcmp(a, b, a_len) == 0;
}
static void resolve(TfpHead *h, void *arg) {
  Resolve *q = arg;
  const TfpVisSnapshot *s = q->s;
  const TfpEngineIface *sr;
  TfpEngineError error = {{0}};
  contract(h, s);
  sr = h->engine;
  Registry context = registry(h);
  double body;
  uint8_t header[32];
  CHECK(unit(h, true, s->body, false, 0, &context, &body, header), 389, "Head body generation retired");
  double world_native = pointer(h, header, 24);
  TfpEngineWorld world = NULL;
  if (!sr->Application_main_world(sr->user, &world, &error)) engine_raise(h, &error);
  CHECK(world && world_address(h, world) == world_native, 391, "Head SDK world changed");
  CHECK(s->avatar_index >= 0 && s->avatar_index < 8, 392, "Head avatar index changed");
  double base = s->avatar_owner + h->build->appearance + s->avatar_index * 0x1B8;
  uint8_t appearance[120];
  rd(h, base + 0xDC, 120, appearance);
  bool full_body = h->io->full_body && h->io->full_body(h->io->user);
  bool hide_torso = h->io->hide_torso && h->io->hide_torso(h->io->user);
  /* Menu option: leave the torso meshes drawn in the ordinary (head, torso and shoulders hidden) view.
   * Never while the first-person rig is up (native request): its arms do not join the body's torso.
   * Nor while something is being drawn or put away: through a swap the camera is held at the rig's eye,
   * which sits in the body's neck and back. The torso appears once the rig-less state has been steady
   * for TORSO_SETTLE seconds. */
  const uint8_t *bytes = s->controller_bytes;
  bool steady = s->camera_kind == 4 && s->has_controller_bytes && s->controller_len >= 6
    && bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0 && bytes[5] == 0;
  double now = h->io->clock(h->io->user);
  if (!steady) h->has_torso_steady_since = false;
  else if (!h->has_torso_steady_since || now < h->torso_steady_since) { h->torso_steady_since = now; h->has_torso_steady_since = true; }
  bool settled = h->has_torso_steady_since && now - h->torso_steady_since >= TORSO_SETTLE;
  bool show_torso = !full_body && settled && h->io->show_torso && h->io->show_torso(h->io->user);
  bool has_models = false, has_model_signature = false;
  const uint8_t *model_signature = NULL;
  size_t model_signature_len = 0;
  if (!full_body && h->models) {
    has_models = tfp_model_shadow_probe(h->models, s, appearance, 120, &model_signature, &model_signature_len);
    has_model_signature = true;
    h->has_model_shadow_error = !has_models;
    if (!has_models) {
      free(h->model_shadow_error);
      h->model_shadow_error = malloc(model_signature_len + 1);
      memcpy(h->model_shadow_error, model_signature, model_signature_len + 1);
      h->model_shadow_error_len = model_signature_len;
    }
  } else h->has_model_shadow_error = false;
  static const int KEPT_FULL[] = {3, 4, 5, 6, 7, 8, 9}, KEPT[] = {3, 4, 5, 6, 7};
  const int *kept_slots = full_body && hide_torso ? KEPT_FULL : KEPT;
  int kept_count = full_body && hide_torso ? 7 : 5;
  bool rig_capable = sr->Unit_has_mesh && sr->Unit_mesh && sr->Mesh_name && sr->Mesh_node && sr->Mesh_num_materials && sr->Mesh_has_material;
  TfpHeadBackpack backpack;
  TfpVisText unused;
  bool has_backpack = h->io->backpack && h->io->backpack(h->io->user, s, &backpack, &unused) == 1;
  Observed *previous = h->observed;
  bool unchanged = previous && tfp_vis_identity_equal(&previous->owner, &s->player_identity) && previous->body == body
    && previous->body_ref == s->body && previous->world == world_native && previous->sdk_world == world && previous->customization == base
    && blob_equal(previous->pack_identity.present, previous->pack_identity.bytes, previous->pack_identity.len,
                  has_backpack && backpack.identity.present, backpack.identity.bytes, backpack.identity.len)
    && memcmp(previous->appearance, appearance, 120) == 0
    && previous->has_viewmodel == s->has_viewmodel && (!s->has_viewmodel || previous->viewmodel == s->viewmodel)
    && previous->rig_capable == rig_capable && previous->full_body == full_body && previous->hide_torso == hide_torso
    && previous->show_torso == show_torso
    && blob_equal(previous->has_model_signature, previous->model_signature, previous->model_signature_len,
                  has_model_signature, model_signature, model_signature_len);
  NMap *refs = unchanged ? previous->refs : new_nmap(h);
  bool changed = !unchanged || q->refresh;
  if (!unchanged) {
    for (int layer = 0; layer <= 2; layer++) for (size_t i = 0; i < 4; i++) {
      const Selected *selected = &SELECTED[i];
      if (!(tfp_head_parts & (1u << selected->part))) continue;   /* beyond retail; all parts by default */
      if (!((!full_body && !(show_torso && selected->part == PART_TORSO)) || selected->part == PART_HEAD
            || (hide_torso && selected->part == PART_TORSO))) continue;
      int offset = layer * 40 + selected->slot * 4;
      double ref = integer(appearance, (size_t)offset);
      if (ref == 0 || ref == NONE) continue;
      CHECK(ref != s->body && !(s->has_viewmodel && ref == s->viewmodel), 436, "Customization reference overlaps player rig");
      NEntry *entry = nget(refs, ref);
      CHECK(!entry || ((Spec *)entry->obj)->part == selected->part, 437, "Head and torso share an ambiguous renderer");
      if (!entry) { entry = nset(refs, ref); entry->obj = &new_spec(h, selected->part)->o; }
      Spec *spec = (Spec *)entry->obj;
      if (spec->offset_count < 8) spec->offsets[spec->offset_count++] = 0xDC + offset;
    }
    /* Never hide a combined renderer which also contains a kept limb. */
    for (int layer = 0; layer <= 2; layer++) for (int i = 0; i < kept_count; i++) {
      double ref = integer(appearance, (size_t)(layer * 40 + kept_slots[i] * 4));
      NEntry *entry = nget(refs, ref);
      if (full_body && hide_torso && entry && ((Spec *)entry->obj)->part == PART_TORSO) { ndel(refs, ref); entry = NULL; }
      CHECK(!entry, 448, "Selected customization renderer is shared with kept clothing");
    }
    if (full_body && hide_torso && has_backpack) {
      NEntry *entry = nget(refs, backpack.ref);
      if (entry && ((Spec *)entry->obj)->part == PART_TORSO) ndel(refs, backpack.ref);
    }
    if (has_backpack && !full_body) {
      CHECK(!nget(refs, backpack.ref), 454, "Backpack overlaps customization renderer");
      Spec *spec = new_spec(h, PART_BACKPACK);
      spec->has_backpack = backpack.identity.present; spec->backpack = backpack.identity;
      nset(refs, backpack.ref)->obj = &spec->o;
    }
    Spec *body_spec = new_spec(h, PART_BODY);
    body_spec->body_only = true;
    nset(refs, s->body)->obj = &body_spec->o;
    if (!full_body && rig_capable && s->has_viewmodel && s->viewmodel > 0 && s->viewmodel < NONE
        && s->viewmodel == floor(s->viewmodel) && s->viewmodel != s->body) {
      Spec *spec = new_spec(h, PART_SHOULDER);
      spec->rig = true;
      nset(refs, s->viewmodel)->obj = &spec->o;
    }
    for (size_t i = 0; has_models && i < h->models->row_count; i++) {
      const TfpModelRow *row = &h->models->rows[i];
      NEntry *entry = nget(refs, row->unit);
      CHECK(!entry || ((Spec *)entry->obj)->rig, 463, "First-person shadow target overlaps hidden clothing");
      Spec *spec = new_spec(h, row->rig ? PART_SHOULDER : PART_MODEL);
      if (row->rig) spec->rig = spec->shadow_rig = true;
      else {
        spec->model_shadow = true; spec->has_model_parent = true; spec->model_parent = row->model_parent;
        spec->has_model_owner = true; spec->model_owner = row->owner;
      }
      nset(refs, row->unit)->obj = &spec->o;
    }
  }
  if (!h->has_cache_world || h->cache_world != world_native || h->cache_sdk_world != world) {
    h->handles = new_nmap(h); h->handles_table++;
    h->cache_world = world_native; h->has_cache_world = true; h->cache_sdk_world = world;
    h->has_cache_request = false; h->has_cache_time = false;
  }
  bool missing = false, required_missing = false;
  for (size_t i = 0; i < refs->n; i++) {
    const NEntry *held = nget(h->handles, refs->e[i].key);
    if (!held || !held->flag || handle_ref(h, held->raw) != refs->e[i].key) {
      missing = true;
      required_missing = required_missing || !((const Spec *)refs->e[i].obj)->rig;
    }
  }
  /* No public Unit-created event exists here. Enumerate only on a missing
   * required handle; a failed, unchanged request retries at most twice/sec. */
  if (missing) {
    char *request = malloc(refs->n * 40 + 1);
    size_t length = 0;
    for (size_t i = 0; i < refs->n; i++) {
      if (i) request[length++] = ':';
      length += tfp_vis_number(refs->e[i].key, request + length, 40);
    }
    request[length] = 0;
    double timestamp = h->io->clock(h->io->user);
    bool due = !h->has_cache_request || strcmp(h->cache_request, request) != 0 || !h->has_cache_time
      || timestamp < h->cache_time || timestamp - h->cache_time >= .5;
    bool clock_ok = timestamp == timestamp && fabs(timestamp) < INFINITY;
    if (clock_ok && due) {
      free(h->cache_request);
      h->cache_request = request; h->has_cache_request = true;
      h->cache_time = timestamp; h->has_cache_time = true;
    } else free(request);
    CHECK(clock_ok, 481, "Head visibility clock unavailable");
    CHECK(due || !required_missing, 483, "Head SDK Unit handle refresh pending");
    if (due) {
      Inventory inv = {world, refs, NULL};
      if (protect(h, inventory, &inv)) { h->handles = inv.found; h->handles_table++; }
      else CHECK(!required_missing, 487, h->error.text);
    }
  }
  SMap *wanted = new_smap(h);
  NMap *layouts = new_nmap(h);
  for (size_t i = 0; i < refs->n; i++) {
    Spec *spec = (Spec *)refs->e[i].obj;
    Collect c = {refs->e[i].key, spec, s, &context, body, header, world_native, base, unchanged, NULL, false};
    if (spec->rig) {
      /* The FPV rig may be absent during a weapon gap, or use an
       * unsupported armor layout. It is optional; verified body and
       * clothing passes keep working independently of its metadata. */
      if (!protect(h, collect, &c)) {
        c.layout = NULL;
        spec->rows = new_smap(h);
        spec->retry = h->io->clock(h->io->user) + .5; spec->has_retry = true;
      }
    } else collect(h, &c);
    if (c.layout) {
      changed = changed || c.changed;
      nset(layouts, refs->e[i].key)->obj = &c.layout->o;
      for (size_t r = 0; spec->rows && r < spec->rows->n; r++) sset(wanted, spec->rows->e[r].key)->obj = spec->rows->e[r].obj;
    }
  }
  NMap *kept_layouts = previous && previous->kept_layouts ? previous->kept_layouts : new_nmap(h);
  if (changed) {
    kept_layouts = new_nmap(h);
    Keep k = {s, &context, body, world_native, new_nmap(h), kept_layouts};
    for (int layer = 0; layer <= 2; layer++) for (int i = 0; i < kept_count; i++) keep(h, &k, integer(appearance, (size_t)(layer * 40 + kept_slots[i] * 4)));
    if (full_body && hide_torso && has_backpack) keep(h, &k, backpack.ref);
    SMap *keys = skeys(h, wanted);
    for (size_t i = 0; i < keys->n; i++) {
      SEntry *live = sget(wanted, keys->e[i].key);
      if (!live) continue;
      Row *row = (Row *)live->obj;
      if (nget(k.kept, row->mesh) && full_body && hide_torso && row->part == PART_TORSO) {
        /* A combined torso/limb mesh cannot be hidden selectively.
         * Remove its cached row too, so unchanged frames keep it. */
        sdel(wanted, keys->e[i].key);
        NEntry *from = nget(refs, row->ref);
        if (from) sdel(((Spec *)from->obj)->rows, keys->e[i].key);
      } else if (!row->shadow_only) CHECK(!nget(k.kept, row->mesh), 530, "Selected mesh is shared with kept clothing");
    }
    TfpVisSnapshot after;
    TfpVisText why;
    bool has_after = view_snapshot(h, &after, &why);
    CHECK(available(h, has_after ? &after : NULL) && tfp_vis_identity_equal(&after.player_identity, &s->player_identity), 532,
          why.present ? why.text : "Head player changed during observation");
    double again;
    uint8_t again_header[32];
    CHECK(unit(h, true, s->body, true, body, NULL, &again, again_header) && again == body && !foreign(h) && first_person(h, &after),
          533, "Head owner or first-person camera changed during observation");
    CHECK(bytes_at(h, base + 0xDC, 120, appearance), 534, "Customization changed during observation");
  }
  Observed *observed = new_obj(h, K_OBSERVED, sizeof *observed);
  observed->owner = s->player_identity; observed->body = body; observed->body_ref = s->body; observed->world = world_native;
  observed->sdk_world = world; observed->customization = base; memcpy(observed->appearance, appearance, 120);
  if (has_backpack) observed->pack_identity = backpack.identity;
  observed->has_viewmodel = s->has_viewmodel; observed->viewmodel = s->viewmodel; observed->rig_capable = rig_capable;
  observed->full_body = full_body; observed->hide_torso = hide_torso; observed->show_torso = show_torso;
  observed->refs = refs; observed->kept_layouts = kept_layouts;
  observed->has_model_signature = has_model_signature;
  if (has_model_signature) {
    observed->model_signature = malloc(model_signature_len + 1);
    memcpy(observed->model_signature, model_signature, model_signature_len);
    observed->model_signature_len = model_signature_len;
  }
  h->observed = observed;
  h->preserved_partial = 0;
  q->wanted = wanted; q->layouts = layouts;
}

/* ---- epochs (:543-568) ---- */
static bool same_epoch(const TfpHead *h, const TfpVisSnapshot *s) {
  return h->has_epoch && tfp_vis_identity_equal(&h->epoch.owner, &s->player_identity) && h->epoch.kind == s->camera_kind
    && h->epoch.record == s->record && h->epoch.head == s->head && h->epoch.tail == s->tail && h->epoch.weapon == s->request_weapon
    && h->epoch.ads == s->controller_active && h->epoch.aim == s->aiming;
}
static bool new_epoch(TfpHead *h, const TfpVisSnapshot *s) {
  bool changed = !same_epoch(h, s);
  if (changed) {
    double serial = h->has_epoch ? h->epoch.serial : 0;
    h->epoch.owner = s->player_identity; h->epoch.kind = s->camera_kind; h->epoch.record = s->record;
    h->epoch.head = s->head; h->epoch.tail = s->tail; h->epoch.weapon = s->request_weapon;
    h->epoch.ads = s->controller_active; h->epoch.aim = s->aiming;
    h->epoch.serial = serial + 1; h->has_epoch = true;
    h->epoch.started = h->io->clock(h->io->user);
  }
  return changed;
}
static bool native_reset(TfpHead *h, Row *row, double current, const Pass *pass) {   /* :555-563 */
  if (!h->has_epoch || (current != 0 && current != 1 && current != 254 && current != 255)) return false;
  double now = h->io->clock(h->io->user);
  if (now < h->epoch.started || now - h->epoch.started > .25) return false;
  if (!row->has_reset_epoch || row->reset_epoch != h->epoch.serial) { row->reset_epoch = h->epoch.serial; row->has_reset_epoch = true; row->reset_bits = 0; }
  bool *has_frame = pass->mask == 1 ? &row->has_reset_default_frame : &row->has_reset_shadow_frame;
  double *frame = pass->mask == 1 ? &row->reset_default_frame : &row->reset_shadow_frame;
  if (has(row->reset_bits, pass->mask)) return *has_frame && *frame == h->update_serial;
  *frame = h->update_serial; *has_frame = true;
  row->reset_bits = assign(row->reset_bits, pass->mask, true);
  return true;
}

/* ---- the steady path (:569-662) ---- */
static Bindings bindings(const TfpHead *h) {
  const TfpEngineIface *sr = h->engine;
  Bindings b;
  memset(&b, 0, sizeof b);
  if (sr) {
    b.engine[0] = (void (*)(void))sr->Application_main_world; b.engine[1] = (void (*)(void))sr->World_units;
    b.engine[2] = (void (*)(void))sr->Unit_set_mesh_visibility; b.engine[3] = (void (*)(void))sr->Unit_has_mesh;
    b.engine[4] = (void (*)(void))sr->Unit_mesh; b.engine[5] = (void (*)(void))sr->Mesh_name;
    b.engine[6] = (void (*)(void))sr->Mesh_node; b.engine[7] = (void (*)(void))sr->Mesh_num_materials;
    b.engine[8] = (void (*)(void))sr->Mesh_has_material;
  }
  b.backend[0] = (void (*)(void))h->io->player_snapshot; b.backend[1] = (void (*)(void))h->io->pin_player_snapshot;
  b.backend[2] = (void (*)(void))h->io->backpack; b.backend[3] = (void (*)(void))h->io->foreign_camera;
  return b;
}
typedef struct Apply { const TfpVisSnapshot *s; SMap *wanted; NMap *layouts; } Apply;
static void retain_steady(TfpHead *h, void *arg) {
  Apply *q = arg;
  const TfpVisSnapshot *s = q->s;
  const Observed *observed = h->observed;
  h->steady = NULL;
  if (s->camera_kind != 6 || !s->safe || !s->has_flags || s->flags_len != 24 || !observed || observed->full_body
      || h->has_model_shadow_error || !h->models || !tfp_model_shadow_has_observation(h->models)) return;
  NMap *shapes = new_nmap(h);
  for (size_t i = 0; i < observed->refs->n; i++) {
    const Spec *spec = (const Spec *)observed->refs->e[i].obj;
    const NEntry *seen = nget(q->layouts, observed->refs->e[i].key);
    if (spec->has_retry || !spec->layout || !seen || &spec->layout->o != seen->obj) return;
    Shape *shape = new_obj(h, K_SHAPE, sizeof *shape);
    shape->object = spec->object; shape->layout = spec->layout;
    if (!spec->body_only && !spec->rig) { shape->has_parent = true; shape->parent = spec->has_model_parent ? spec->model_parent : observed->body; }
    nset(shapes, observed->refs->e[i].key)->obj = &shape->o;
  }
  for (size_t i = 0; i < observed->kept_layouts->n; i++) {
    const NEntry *kept = &observed->kept_layouts->e[i];
    const NEntry *seen = nget(shapes, kept->key);
    if (seen) {
      const Shape *old = (const Shape *)seen->obj;
      if (old->object != kept->a || !same_layout(old->layout, (const Layout *)kept->obj)) return;
    } else {
      Shape *shape = new_obj(h, K_SHAPE, sizeof *shape);
      shape->object = kept->a; shape->has_parent = true; shape->parent = kept->b; shape->layout = (Layout *)kept->obj;
      nset(shapes, kept->key)->obj = &shape->o;
    }
  }
  for (size_t i = 0; i < q->wanted->n; i++) if (!((const Row *)q->wanted->e[i].obj)->has_last) return;
  /* Resource bytes and handles are cached as values only. A later
   * matching observation cannot authorize even one SDK pass write. */
  for (size_t i = 0; i < shapes->n; i++) {
    Shape *shape = (Shape *)shapes->e[i].obj;
    rd(h, shape->object + 0x358, 8, shape->resource);
    const NEntry *held = nget(h->handles, shapes->e[i].key);
    shape->has_handle = held && held->flag;
    shape->handle = shape->has_handle ? held->raw : NULL;
  }
  Steady *steady = new_obj(h, K_STEADY, sizeof *steady);
  steady->observed = h->observed; steady->wanted = q->wanted; steady->layouts = q->layouts; steady->shapes = shapes;
  steady->bindings = bindings(h);
  steady->flags_len = s->flags_len; memcpy(steady->flags, s->flags, sizeof steady->flags);
  h->steady = steady;
}
typedef struct Inspect { TfpHead *h; Registry *context; NMap *objects; double id, expected; bool has_expected; double object; uint8_t *header; } Inspect;
static void inspect(TfpHead *h, void *arg) {   /* :617-625 */
  Inspect *q = arg;
  NEntry *found = nget(q->objects, q->id);
  if (!found) {
    double object;
    Blob *blob = new_obj(h, K_BLOB, sizeof *blob);
    CHECK(unit(h, true, q->id, q->has_expected, q->expected, q->context, &object, blob->bytes), 620, "Head steady generation retired");
    found = nset(q->objects, q->id);
    found->a = object; found->obj = &blob->o;
  }
  CHECK(!q->has_expected || q->expected == found->a, 623, "Head steady object changed");
  q->object = found->a;
  memcpy(q->header, ((Blob *)found->obj)->bytes, 32);
}
static bool inspect_for_models(void *context, double id, double expected, double *object, uint8_t header[32], TfpVisText *why) {
  Inspect *q = context;
  q->id = id; q->expected = expected; q->has_expected = true; q->header = header;
  if (!protect(q->h, inspect, q)) { *why = q->h->error; return false; }
  *object = q->object;
  return true;
}
static bool flags_equal(const TfpVisSnapshot *s, const uint8_t *flags, uint8_t len) {
  return s->has_flags && s->flags_len == len && memcmp(s->flags, flags, len) == 0;
}
#define STEADY(condition) do { if (!(condition)) fail(h, 0, "steady"); } while (0)   /* the text is never read: pcall(observe_steady) */
static void observe_steady(TfpHead *h, void *arg) {
  const TfpVisSnapshot *s = arg;
  const Steady *saved = h->steady;
  const Observed *observed = h->observed;
  const TfpEngineIface *sr;
  TfpEngineError error = {{0}};
  uint8_t header[32], appearance[120];
  STEADY(saved->observed == observed && s->safe && s->camera_kind == 6 && same_epoch(h, s) && flags_equal(s, saved->flags, saved->flags_len)
         && tfp_vis_identity_equal(&observed->owner, &s->player_identity) && observed->body_ref == s->body
         && observed->has_viewmodel == s->has_viewmodel && (!s->has_viewmodel || observed->viewmodel == s->viewmodel)
         && observed->customization == s->avatar_owner + h->build->appearance + s->avatar_index * 0x1B8
         && !(h->io->full_body && h->io->full_body(h->io->user))
         && observed->hide_torso == (h->io->hide_torso && h->io->hide_torso(h->io->user))
         && !observed->show_torso);   /* steady observation is the native view, where the torso is always hidden */
  Bindings now = bindings(h);
  STEADY(memcmp(&now, &saved->bindings, sizeof now) == 0);
  contract(h, s);
  sr = h->engine;
  Registry context = registry(h);
  STEADY(h->observed);   /* contract() drops the observation when it qualifies the executable again */
  Inspect q = {h, &context, new_nmap(h), s->body, observed->body, true, 0, header};
  inspect(h, &q);
  STEADY(pointer(h, header, 24) == observed->world);
  TfpEngineWorld world = NULL;
  if (!sr->Application_main_world(sr->user, &world, &error)) engine_raise(h, &error);
  STEADY(world == observed->sdk_world && world && world_address(h, world) == observed->world);
  double base = observed->customization;
  rd(h, base + 0xDC, 120, appearance);
  STEADY(memcmp(appearance, observed->appearance, 120) == 0);
  TfpHeadBackpack pack;
  TfpVisText unused;
  int packed = h->io->backpack ? h->io->backpack(h->io->user, s, &pack, &unused) : 0;
  STEADY(packed >= 0 && blob_equal(packed == 1 && pack.identity.present, pack.identity.bytes, pack.identity.len,
                                   observed->pack_identity.present, observed->pack_identity.bytes, observed->pack_identity.len));
  STEADY(tfp_model_shadow_unchanged(h->models, s, appearance, 120, inspect_for_models, &q, NULL));
  for (size_t i = 0; i < saved->shapes->n; i++) {
    const Shape *shape = (const Shape *)saved->shapes->e[i].obj;
    double ref = saved->shapes->e[i].key;
    q.id = ref; q.expected = shape->object; q.has_expected = true; q.header = header;
    inspect(h, &q);
    STEADY(pointer(h, header, 24) == observed->world && bytes_at(h, q.object + 0x358, 8, shape->resource)
           && (!shape->has_parent || ptr(h, q.object + 0x1D0) == shape->parent));
    Layout *layout = mesh_layout(h, q.object);
    STEADY(same_layout(layout, shape->layout));
    if (shape->has_handle) {
      const NEntry *held = nget(h->handles, ref);
      STEADY(held && held->flag && held->raw == shape->handle && handle_ref(h, shape->handle) == ref);
    }
  }
  for (size_t i = 0; i < saved->wanted->n; i++) {
    const Row *row = (const Row *)saved->wanted->e[i].obj;
    STEADY(mask(h, row) == row->last);
  }
  /* Close player, appearance and registry ownership after the bounded
   * read. No saved proof survives into a native/SDK mutation path. */
  TfpVisSnapshot after;
  STEADY(view_snapshot(h, &after, &unused));
  STEADY(available(h, &after) && after.safe && after.camera_kind == 6 && first_person(h, &after)
         && tfp_vis_identity_equal(&after.player_identity, &s->player_identity) && after.body == s->body
         && after.has_viewmodel == s->has_viewmodel && (!s->has_viewmodel || after.viewmodel == s->viewmodel)
         && after.has_flags == s->has_flags && (!s->has_flags || flags_equal(&after, s->flags, s->flags_len))
         && same_epoch(h, &after) && !foreign(h));
  STEADY(bytes_at(h, base + 0xDC, 120, appearance));
  Registry closing = registry(h);
  STEADY(closing.address == context.address && closing.objects == context.objects && closing.generations == context.generations
         && closing.capacity == context.capacity);
  for (size_t i = 0; i < q.objects->n; i++) {
    double ref = q.objects->e[i].key;
    double index = lmod(ref, 0x400000), generation = lmod(floor(ref / 0x400000), 256);
    STEADY(byte_at(h, context.generations + index) == generation && ptr(h, context.objects + index * 8) == q.objects->e[i].a);
  }
}

/* ---- update (:663-764) ---- */
static void apply(TfpHead *h, void *arg) {   /* :694-756 */
  Apply *q = arg;
  const TfpVisSnapshot *s = q->s;
  SMap *wanted = q->wanted;
  NMap *layouts = q->layouts;
  SMap *keys = skeys(h, h->owned);
  for (size_t i = 0; i < keys->n; i++) {
    const char *key = keys->e[i].key;
    SEntry *live = sget(h->owned, key), *want = sget(wanted, key);
    if (!live || (want && want->obj == live->obj)) continue;
    Restore r = {key, (Row *)live->obj, layouts, NULL, NULL};
    restore(h, &r);
    want = sget(wanted, key);
    if (want) ((Row *)want->obj)->current = mask(h, (Row *)want->obj);
  }
  for (size_t i = 0; i < wanted->n; i++) {
    const char *key = wanted->e[i].key;
    Row *row = (Row *)wanted->e[i].obj;
    double current = row->current;
    int count;
    const Pass *list;
    if (row->has_last && current == 255 && !has(row->last, 1)) {
      row->bits = 0; sdel(h->owned, key);   /* Complete native/foreign visibility supersedes the old cohort. */
      if (!native_reset(h, row, current, &PASSES[0])) sset(h->ignored, key)->num = row->shadow_suppressed ? 1 : 3;
    }
    list = passes(row, &count);
    for (int p = 0; p < count; p++) {
      const Pass *pass = &list[p];
      bool targeted = !row->body_only || pass->mask == 2;
      if (targeted && has(current, pass->mask) != pass->visible
          && (has(row->bits, pass->mask) || (row->has_last && has(row->last, pass->mask) == pass->visible))) {
        row->bits = assign(row->bits, pass->mask, false);
        if (!(row->shadow_suppressed && pass->mask == 2) && !native_reset(h, row, current, pass)) {
          h->foreign_masks += 1;
          SEntry *ignored = sset(h->ignored, key);
          ignored->num = assign(ignored->num, pass->mask, true);
        }
      }
    }
    if (row->bits == 0) sdel(h->owned, key);
    list = passes(row, &count);
    for (int p = 0; p < count; p++) {
      const Pass *pass = &list[p];
      bool desired = !row->body_only || pass->mask == 2;
      const SEntry *ignored = sget(h->ignored, key);
      if (!(desired && has(current, pass->mask) != pass->visible && !has(row->bits, pass->mask)
            && !has(ignored ? ignored->num : 0, pass->mask))) continue;
      double again, index, unused, pad_mesh;
      uint8_t header[32];
      CHECK(unit(h, true, row->body_ref, true, row->body_object, NULL, &again, header) && again == row->body_object
            && (row->body_only || row->rig || (row->has_model_owner && ptr(h, row->object + 0x1D0) == row->model_parent)
                || (!row->has_model_owner && ptr(h, row->object + 0x1D0) == row->body_object))
            && !foreign(h), 721, "Head owner changed before hide");
      bool present = row->body_only;
      if (row->has_backpack) {
        TfpHeadBackpack pack;
        TfpVisText why;
        bool packed = h->io->backpack && h->io->backpack(h->io->user, s, &pack, &why) == 1;
        CHECK(packed && pack.ref == row->ref && tfp_vis_identity_equal(&pack.identity, &row->backpack), 728, "Backpack changed before pass update");
        present = true;
      }
      if (row->rig) {
        CHECK(s->has_viewmodel && row->ref == s->viewmodel && bytes_at(h, row->object + 0x358, 8, RIG_RESOURCE)
              && (!row->rig_pad || (rig_mesh(h, row->handle, row->rig_pad, &pad_mesh) && pad_mesh == row->mesh)),
              732, "Owned rig shoulder changed before pass update");
        present = true;
      }
      if (row->has_model_owner) {
        CHECK(h->models && tfp_model_shadow_valid(h->models, s, &row->model_owner), 737, "First-person shadow owner changed before pass update");
        present = true;
      }
      for (int o = 0; o < row->offset_count; o++) if (u32(h, row->customization + row->offsets[o]) == row->ref) present = true;
      CHECK(locate(h, row, layouts, false, &index), 742, "Head mesh retired before hide");
      CHECK(present && mask(h, row) == current, 743, "Head customization changed before pass update");
      row->before = assign(row->before, pass->mask, has(current, pass->mask));
      row->written = assign(current, pass->mask, pass->visible);
      if (pass->mask == 1) row->restoring_default = false;
      row->bits = assign(row->bits, pass->mask, true);
      sset(h->owned, key)->obj = &row->o;   /* Retain uncertain SDK outcomes before calling it. */
      set_pass(h, row, index, pass->visible, pass->name);
      h->writes += 1;
      CHECK(locate(h, row, layouts, false, &unused) && mask(h, row) == row->written, 750, "Head mesh pass update readback failed");
      current = row->written; row->current = current;
    }
    row->last = current; row->has_last = true;
  }
}
static bool unavailable(TfpHead *h, TfpVisText *why) {   /* :687-688, :758-759 */
  char problem[TFP_VIS_TEXT];
  memcpy(problem, h->error.text, sizeof problem);
  if (!stop(h, why)) return false;
  report(h, TFP_HEAD_UNAVAILABLE, problem);
  if (why) *why = h->reason;
  return true;
}
static bool update(TfpHead *h, TfpVisText *why) {
  TfpVisSnapshot s;
  TfpVisText unused;
  if (h->phase == TFP_HEAD_CLEANUP_PENDING) return stop(h, why);
  bool pose_offset = h->io->controller_result_is_pose_offset(h->io->user);
  if (h->closing || !h->io->controller_is_enabled(h->io->user) || !pose_offset || foreign(h)) return stop(h, why);
  bool has_s = view_snapshot(h, &s, &unused);
  if (!available(h, has_s ? &s : NULL) || !first_person(h, &s)) return stop(h, why);
  if (h->identity.present && !tfp_vis_identity_equal(&h->identity, &s.player_identity) && !stop(h, why)) return false;
  if (why) why->present = false;
  bool refresh = false;
  if (h->steady) {
    if (protect(h, observe_steady, &s)) {
      h->steady_observations += 1;
      count_owned(h); report(h, TFP_HEAD_ACTIVE, NULL);
      return true;
    }
    /* Any unreadable/changed fingerprint immediately takes the full
     * observation path, including newly linked formerly-empty leaves.
     * Retain row.last / ignored bits for foreign-mask arbitration.
     * A forced full observation refreshes kept membership without
     * erasing the ownership history of unchanged mesh objects. */
    h->steady = NULL; refresh = true;
  }
  h->full_observations += 1;
  Resolve r = {&s, refresh, NULL, NULL};
  if (!protect(h, resolve, &r)) return unavailable(h, why);
  h->identity = s.player_identity;
  h->update_serial += 1;
  if (new_epoch(h, &s)) h->ignored = new_smap(h);
  Apply a = {&s, r.wanted, r.layouts};
  if (!protect(h, apply, &a)) return unavailable(h, why);
  count_owned(h); report(h, TFP_HEAD_ACTIVE, NULL);
  if (!protect(h, retain_steady, &a)) h->steady = NULL;
  return true;
}

/* ---- public ---- */
TfpHead *tfp_head_new(const TfpHeadIo *io, const TfpEngineIface *engine, TfpModelShadow *models, const TfpHeadBuild *build) {
  TfpHead *h = calloc(1, sizeof *h);
  h->io = io; h->engine = engine; h->models = models; h->build = build;
  h->owned = new_smap(h); h->ignored = new_smap(h);
  return h;
}
void tfp_head_free(TfpHead *h) {
  if (!h) return;
  for (Obj *o = h->objects; o;) { Obj *next = o->next; release(o); o = next; }
  free(h->model_shadow_error); free(h->cache_request);
  free(h);
}
bool tfp_head_update(TfpHead *h, TfpVisText *why) { bool ok = update(h, why); collect_garbage(h); return ok; }
bool tfp_head_stop(TfpHead *h, TfpVisText *why) { bool ok = stop(h, why); collect_garbage(h); return ok; }
bool tfp_head_shutdown(TfpHead *h, TfpVisText *why) { h->closing = true; return tfp_head_stop(h, why); }   /* :257 */
bool tfp_head_is_clean(const TfpHead *h) { return h->owned->n == 0; }   /* :248 */
const char *tfp_head_phase_name(TfpHeadPhase phase) {
  static const char *const names[] = {"idle", "active", "unavailable", "cleanup_pending"};
  return names[phase];
}
void tfp_head_result(TfpHead *h, TfpHeadResult *out) {   /* :265-273 */
  double total = count_owned(h);
  *out = (TfpHeadResult){h->phase, total, h->hidden_head, h->hidden_torso, h->hidden_shoulders, h->hidden_body, h->hidden_backpack,
                         h->preserved_partial, h->writes, h->restores, h->retired, h->foreign_masks, h->phase == TFP_HEAD_CLEANUP_PENDING,
                         h->reason, h->has_model_shadow_error, h->model_shadow_error, h->model_shadow_error_len, h->shadows_disabled,
                         h->steady_observations, h->full_observations};
}
size_t tfp_head_status(TfpHead *h, char *out, size_t size) {   /* :274-277 */
  char head[40], torso[40], shoulders[40], body[40];
  count_owned(h);
  tfp_vis_number(h->hidden_head, head, sizeof head); tfp_vis_number(h->hidden_torso, torso, sizeof torso);
  tfp_vis_number(h->hidden_shoulders, shoulders, sizeof shoulders); tfp_vis_number(h->hidden_body, body, sizeof body);
  return (size_t)snprintf(out, size, "%s; owned head/torso/shoulder/body passes=%s/%s/%s/%s; %s", tfp_head_phase_name(h->phase), head, torso,
                          shoulders, body, h->reason.present ? h->reason.text : "");
}
bool tfp_head_render_appearance(const TfpHead *h, const uint8_t **appearance, TfpVisIdentity *owner, double *world) {   /* :260-262 */
  if (!h->observed) return false;
  *appearance = h->observed->appearance; *owner = h->observed->owner; *world = h->observed->world;
  return true;
}
bool tfp_head_sdk_handles(const TfpHead *h, size_t *count, unsigned long long *table) {   /* :264 */
  if (!h->handles) return false;
  *count = h->handles->n; *table = h->handles_table;
  return true;
}
TfpHeadHandle tfp_head_sdk_handle(const TfpHead *h, size_t index) {
  const NEntry *e = &h->handles->e[index];
  return (TfpHeadHandle){e->key, e->flag ? (TfpEngineUnit)e->raw : NULL};
}
void tfp_head_debug(const TfpHead *h, TfpHeadDebug *out) {
  memset(out, 0, sizeof *out);
  out->owned = h->owned->n; out->ignored = h->ignored->n;
  out->steady = h->steady != NULL; out->has_epoch = h->has_epoch; out->epoch_serial = h->epoch.serial; out->epoch_started = h->epoch.started;
  out->update_serial = h->update_serial; out->identity = h->identity;
  out->has_cache_request = h->has_cache_request; out->cache_request = h->cache_request;
  out->has_cache_time = h->has_cache_time; out->cache_time = h->cache_time;
  out->has_torso_steady_since = h->has_torso_steady_since; out->torso_steady_since = h->torso_steady_since;
}
void tfp_head_debug_owned(const TfpHead *h, size_t index, TfpHeadOwnedRow *out) {
  const Row *row = (const Row *)h->owned->e[index].obj;
  *out = (TfpHeadOwnedRow){h->owned->e[index].key, PART_NAMES[row->part], row->bits, row->before, row->written, row->last, row->current,
                           row->reset_epoch, row->reset_bits, row->has_last, row->has_reset_epoch, row->restoring_default,
                           row->shadow_suppressed, row->shadow_only};
}
double tfp_head_debug_ignored(const TfpHead *h, size_t index, const char **key) {
  *key = h->ignored->e[index].key;
  return h->ignored->e[index].num;
}
