/* See backend_ops.h. Line references (":123") are to the retail file
 * src/first_person/backend.lua at 0.5.41-smooth.66; "A:" ones to
 * src/first_person/action_state.lua. The order of reads, checks and calls and
 * every text are retail's. A Lua `assert` / `error` is `fail`: the function
 * returns false with the text in b->why; a `pcall` is a call whose false
 * result is looked at instead of being passed on. */
#include "backend_ops.h"
#include "profile.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOW UINT64_C(65536)
#define HIGH UINT64_C(0x800000000000)

/* ---- constants ---------------------------------------------------------------------------
 * B_ = build data of 25480438, L_ = structure layout (the same number as retail).
 * Evidence: "map" = tools/build_map.py; "profile.h"; "image" = bytes read from
 * game-pre1020.dll / exe-25480438.bin on 2026-11-16; a file name = that file of this
 * repository reads the same field on this build. docs/translation/BACKEND-OPS-TRANSLATION.md has the table. */
#define B_GAME_INVENTORY      FP_INVENTORY     /* retail 0x2ce18e8; BUILD-DIFFERENCES (by hand) */
#define B_GAME_STATE          FP_STATE         /* retail 0x2ce1508; map; action.c */
#define B_STATE_SCENE         FP_STATE_SCENE   /* retail 0xac29c; map; action.c */
#define B_GAME_API_ROOT       FP_API_ROOT      /* retail 0x2ce14c8; map; image (game+0x127ea56) */
#define B_PIN_ENTRY_CALL      UINT32_C(0x127ddf2) /* retail 0x110fcc2 = enter wrapper + 0x42; image */
#define B_HEX_ENTRY_CALL      "0f57d2488bcbe833850000" /* retail …e8a3860000; here the call lands on the evaluator pin 0x1286330 */
#define B_EXE_REGISTRY        FP_REGISTRY      /* retail 0x1ab76f8; map; backend.c */
#define B_EXE_EXISTS          UINT32_C(0x201710) /* retail 0x1f75d0; map */
#define B_HEX_EXISTS          "48895c2408574883ec208bfae89fc1e9ff488bc8488bd84c8b0041ff90b00100" /* image; only the call displacement differs */
#define B_EXE_TRIGGER         UINT32_C(0x201900) /* retail 0x1f77c0; map */
#define B_HEX_TRIGGER         "48895c2408574883ec208bda8bf9e8adbfe9ff448bc38bd7488b4818488b5c24" /* image; same */
#define B_EXE_ANIM_GETTER     UINT32_C(0x2bd9c0) /* retail 0x28d510; map; action.c; native_contract.h */
#define B_EXE_RESOURCE_GETTER UINT32_C(0x2bd9d0) /* retail 0x28d520; map; image (same 14 bytes) */
#define B_EXE_RENDER_GET      UINT32_C(0x3fa3d0) /* retail 0x3d43d0; map; feature_contract.h */
#define B_HEX_RENDER_GET      "488bd1e938feffff" /* retail 488bd1e908fdffff; feature_contract.h; image */
#define B_EXE_RENDER_SET      UINT32_C(0x3fa120) /* retail 0x3d3ff0; map; feature_contract.h */
#define B_HEX_RENDER_SET      "488bd1e998fbffff" /* retail 488bd1e988faffff; feature_contract.h; image */
#define B_EXE_RENDER_LOAD     UINT32_C(0x3fa262) /* retail 0x3d419a; map */
#define B_HEX_RENDER_LOAD     "4c8b354f606101"   /* retail 488b0d1f376e01 (rcx); image: mov r14,[rip+…] -> exe+0x1a102b8, the table */
#define B_EXE_RENDER_TYPE     UINT32_C(0x3fa2f8) /* retail 0x3d420e; map */
#define B_HEX_RENDER_TYPE     "83f80475120fb65708" /* retail 83f804751a41387108; image: cmp eax,4 / jne / movzx edx,byte[rdi+8] */
#define B_EXE_RENDER_TABLE    UINT32_C(0x1a102b8) /* retail 0x1ab78c0; map; effects.c */
#define L_QUEUE               0x1f8  /* snapshot.c */
#define L_RING                0x200
#define L_RECORD              0xe8
#define L_FALLBACK            0x1f00
#define L_CAMERA_UNIT         8      /* context+8; backend.c; image (game+0x127ea5d) */
#define L_CAMERA_WORLD        0x180  /* context+0x180; image (game+0x127ea62) */
#define L_CAMERA_GRAPH        0x18   /* camera+0x18 = unit object+0x60; scene.c */
#define L_UNIT_GRAPH          0x60
#define L_UNIT_PARENT         0x1D0  /* scene.c, visibility_policy.c */
#define L_UNIT_RESOURCE       0x358  /* action.c, backend.c */
#define L_UNIT_ANIM           0x178  /* action.c */
#define L_VT_ANIM_GETTER      0x1B0  /* action.c */
#define L_VT_RESOURCE_GETTER  0x1B8  /* proven at run time: the slot must hold exe+B_EXE_RESOURCE_GETTER */
#define L_ANIM_GRAPH          0x28   /* action.c */
#define L_ANIM_ACTIVE         0x50   /* action.c */
#define L_REGISTRY_BLOCK      0x88   /* backend.c: units +0x88, capacity +0x98, generations +0xa0 */
#define L_REGISTRY_CAPACITY   0x98
#define L_API_CAMERA_LINK     8      /* root+8, slot 0xF0: image (game+0x127ea6a, +0x127ea6e) */
#define L_API_UNLINK          0xF0
#define L_API_UNIT            0x18   /* pose_policy.c, visibility_policy.c */
#define L_API_EXISTS          0x350  /* proven at run time: the slot must hold exe+B_EXE_EXISTS */
#define L_API_TRIGGER         0x370  /* proven at run time: the slot must hold exe+B_EXE_TRIGGER */
#define L_BODY_EVENT          UINT32_C(0x372071D0) /* an event name hash, not build data */
#define L_REQUEST_SIGHT       0x98   /* pose_policy.c (request+0x98) */
#define L_CONTROLLER_ACTIVE   4      /* snapshot.c (controller byte 4) */
#define L_INVENTORY_MAP       40     /* First Person Controller native_view.c (BUILD-DIFFERENCES) */
#define L_INVENTORY_OWNERS    64
#define L_INVENTORY_ROWS      80
#define L_INVENTORY_ROW       48
#define L_INVENTORY_BACKPACK  12     /* the row's type-6 slot: the game's slot switch (game+0x797cb5, table 0x797d78) reads
                                       * types 1-3 at +0/+4/+8, type 6 at +0xc, types 4-5 at +0x10; 26251122's (0x7a3645)
                                       * is the same, and there +12 was the pack in the game. docs/design/specs/2026-11-24-warp-pack-glow.md */

/* OPEN on build 25480438: the game's local presentation switch (retail game+0x833440), its nine
 * code pins and its mode byte (retail avatar_owner+0x546B20, stride 0x1B8). Only
 * `_invoke_presentation` needs them (:517; fird-only, nothing on the shipped path calls it), and
 * it refuses while B_PRESENTATION_ESTABLISHED is 0. */
#define B_PRESENTATION_ESTABLISHED 0
#define B_PRESENTATION_FUNCTION    0
#define B_PRESENTATION_MODE        0
#define B_PRESENTATION_MODE_STRIDE 0
#define B_PRESENTATION_PINS \
  {false, 0, ""}, {false, 0, ""}, {false, 0, ""}, {false, 0, ""}, {false, 0, ""}, {false, 0, ""}, {false, 0, ""}, \
  {true, 0, ""}, {true, 0, ""}
/* Test only: tests/run-vectors-backend-ops.sh builds a second object with
 * -DTFP_OPS_TEST_OPEN_CONSTANTS='"backend_ops_open.h"', which puts the open names at places in
 * the test's fake image so the logic can be compared with retail's. No shipped build defines it. */
#ifdef TFP_OPS_TEST_OPEN_CONSTANTS
#include TFP_OPS_TEST_OPEN_CONSTANTS
#endif
typedef struct Pin { bool exe; uint32_t rva; const char *hex; } Pin;
static const Pin presentation_pins[9] = {B_PRESENTATION_PINS};

/* ---- raising, texts ---------------------------------------------------------------------- */
static bool fail(TfpOps *b, const char *format, ...) {
  char text[sizeof b->why];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  memcpy(b->why, text, sizeof text);
  return false;
}
static void none(TfpOpsText *t) {
  if (t) t->present = false, t->text[0] = 0;
}
static void put(TfpOpsText *t, const char *format, ...) {
  char text[sizeof t->text];
  va_list args;
  if (!t) return;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  memcpy(t->text, text, sizeof text);
  t->present = true;
}
static TfpOpsResult say(TfpOpsText *t, TfpOpsResult result, const char *text) {
  put(t, "%s", text);
  return result;
}
static TfpOpsResult raised(TfpOps *b, TfpOpsText *t) { return say(t, TFP_OPS_RAISED, b->why); }
/* tostring(called and second or first) for a call that did not answer true. */
static const char *second_or_first(TfpOpsResult first, const TfpOpsText *second) {
  if (second->present) return second->text;
  return first == TFP_OPS_FALSE ? "false" : "nil";
}
void tfp_ops_number(double value, char *out, size_t size) { /* lj_strfmt_num: %.14g */
  if (value != value) snprintf(out, size, "nan");
  else if (isinf(value)) snprintf(out, size, value < 0 ? "-inf" : "inf");
  else snprintf(out, size, "%.14g", value);
}
static const char *truth(bool value) { return value ? "true" : "false"; }
static void keep_text(char *out, size_t size, const char *text) { /* a text into a shorter buffer, cut if it must be */
  size_t n = strlen(text);
  if (n >= size) n = size - 1;
  memcpy(out, text, n);
  out[n] = 0;
}

/* ---- _read, _u32, _u8, _ptr (:65-84) ----------------------------------------------------- */
static bool rd(TfpOps *b, uint64_t at, void *out, size_t size) {
  if (at < LOW || !size || size > 1024 || at + size >= HIGH) return fail(b, "Invalid read");
  if (!b->host->read(b->host->user, at, out, size)) return fail(b, "Memory unavailable");
  return true;
}
static bool u32(TfpOps *b, uint64_t at, uint32_t *out) { return rd(b, at, out, 4); }
static bool u8(TfpOps *b, uint64_t at, uint8_t *out) { return rd(b, at, out, 1); }
static uint64_t lua_hex(double n) { /* what LuaJIT's %x prints for a number */
  int64_t k = n >= 9223372036854775808.0 ? (int64_t)(n - 18446744073709551616.0) : (int64_t)n;
  return (uint64_t)k;
}
static bool ptr(TfpOps *b, uint64_t at, uint64_t *out) {
  uint32_t half[2];
  if (!rd(b, at, half, 8)) return false;
  uint64_t value = (uint64_t)half[0] | (uint64_t)half[1] << 32;
  if (value < LOW || value >= HIGH)
    return fail(b, "Pointer unavailable at0x%llx (value0x%llx)", (unsigned long long)at,
                 (unsigned long long)lua_hex((double)half[0] + (double)half[1] * 4294967296.0));
  *out = value;
  return true;
}
static bool valid_id(uint32_t id) { return id > 0 && id < UINT32_MAX; } /* :20 */
static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0; }
static size_t unpack_hex(const char *hex, uint8_t *out) { /* :22 */
  size_t n = strlen(hex) / 2;
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(hex_digit(hex[i * 2]) * 16 + hex_digit(hex[i * 2 + 1]));
  return n;
}
/* _read(at,#bytes)==bytes; false without a text when it only differs. */
static bool pinned(TfpOps *b, uint64_t at, const char *hex, bool *equal) {
  uint8_t wanted[64], actual[64];
  size_t n = unpack_hex(hex, wanted);
  if (!rd(b, at, actual, n)) return false;
  *equal = !memcmp(actual, wanted, n);
  return true;
}

/* ---- frame (:36-42) ---------------------------------------------------------------------- */
void tfp_ops_init(TfpOps *b, const TfpOpsHost *host, const TfpOpsPoseHelper *alignment, const TfpOpsPoseHelper *pose) { /* :28 */
  memset(b, 0, sizeof *b);
  b->host = host;
  b->profile = (TfpOpsProfile)TFP_OPS_PROFILE_SHIPPED;
  b->alignment = alignment;
  b->pose = pose;
}
void tfp_ops_begin_frame(TfpOps *b) {
  b->frame_epoch++;
  b->frame_active = true;
  b->publication.has = false;
}
void tfp_ops_end_frame(TfpOps *b) {
  b->frame_active = false;
  b->publication.has = false;
}
static void invalidate(TfpOps *b) {
  if (b->frame_active) b->frame_epoch++;
}

/* ---- _module (:86) ----------------------------------------------------------------------- */
enum { SCOPE_FULL = TFP_OPS_SCOPE_FULL, SCOPE_IDENTITY = TFP_OPS_SCOPE_IDENTITY, SCOPE_ENTRY = TFP_OPS_SCOPE_ENTRY };
static bool cold(const char *name) { /* FP_COLD_CHECK, :85 */
  return !strcmp(name, "evaluator") || !strcmp(name, "root_position_call") || !strcmp(name, "root_rotation_call") ||
         !strcmp(name, "pose_branch_position_call");
}
static bool pe_header(TfpOps *b, uint64_t base, uint8_t h[96], const char *bad_module, const char *bad_header) {
  uint8_t mz[2];
  uint32_t pe = 0;
  if (!rd(b, base, mz, 2)) return false;
  if (mz[0] != 'M' || mz[1] != 'Z') return fail(b, "%s", bad_module);
  if (!u32(b, base + 0x3c, &pe)) return false;
  if (pe < 0x40 || pe > 0x1000) return fail(b, "%s", bad_header);
  return rd(b, base + pe, h, 96);
}
static bool amd64(const uint8_t h[96]) { return !memcmp(h, "PE\0\0", 4) && h[4] == 0x64 && h[5] == 0x86 && h[24] == 0x0b && h[25] == 0x02; }
static bool stamped(const uint8_t h[96], uint32_t stamp, uint32_t size) {
  uint32_t a, c;
  memcpy(&a, h + 8, 4);
  memcpy(&c, h + 80, 4);
  return a == stamp && c == size;
}
static bool module(TfpOps *b, int scope, uint64_t *out) {
  uint64_t base = b->host->module(b->host->user, TFP_OPS_GAME);
  uint8_t h[96];
  if (b->frame_active) {
    if (b->frame_module_base.has && b->frame_module_base.base != base) invalidate(b);
    b->frame_module_base.has = true;
    b->frame_module_base.base = base;
  }
  if (base < LOW) return fail(b, "game.dll unavailable");
  if (b->frame_active && b->frame_game_checks[scope].has && b->frame_game_checks[scope].base == base &&
      b->frame_game_checks[scope].epoch == b->frame_epoch) {
    *out = base;
    return true;
  }
  if (!pe_header(b, base, h, "Invalid game module", "Invalid PE header")) return false;
  if (!amd64(h)) return fail(b, "AMD64 PE32+ required");
  if (!stamped(h, FP_GAME_STAMP, FP_GAME_SIZE)) return fail(b, "Unsupported game build");
  for (size_t i = 0; i < FP_WITNESS_COUNT; i++) {
    const FpWitness *w = fp_witnesses + i;
    bool equal = false;
    bool readable = pinned(b, base + w->rva, w->hex, &equal);
    bool required = scope == SCOPE_FULL || (scope == SCOPE_ENTRY && !cold(w->name));
    if (!readable && required) return fail(b, "Waiting for native ADS camera initialization (%s)", w->name);
    if (readable && !equal) return fail(b, "Native ADS fingerprint mismatch: %s", w->name);
  }
  if (scope == SCOPE_ENTRY) {
    bool equal = false;
    if (!pinned(b, base + B_PIN_ENTRY_CALL, B_HEX_ENTRY_CALL, &equal)) return false;
    if (!equal) return fail(b, "Native ADS warm entry evaluator call changed");
  }
  if (b->frame_active) {
    b->frame_game_checks[scope].has = true;
    b->frame_game_checks[scope].base = base;
    b->frame_game_checks[scope].epoch = b->frame_epoch;
  }
  *out = base;
  return true;
}
TfpOpsResult tfp_ops_camera_ready(TfpOps *b, TfpOpsText *why) { /* :128 */
  uint64_t base;
  char text[32];
  none(why);
  if (!module(b, SCOPE_FULL, &base)) return say(why, TFP_OPS_FALSE, b->why);
  /* `return ok,ok and nil or tostring(why)`: on success the second result is the base, as text. */
  tfp_ops_number((double)base, text, sizeof text);
  return say(why, TFP_OPS_TRUE, text);
}

/* ---- _map, _descriptor, _top (:132-166) -------------------------------------------------- */
static bool map(TfpOps *b, uint64_t at, uint32_t id, uint32_t limit, uint32_t *index) {
  uint8_t header[20];
  uint64_t buckets;
  uint32_t capacity, empty, multiplier;
  if (!valid_id(id)) return fail(b, "Invalid entity");
  if (!rd(b, at, header, sizeof header)) return false;
  memcpy(&buckets, header, 8);
  memcpy(&capacity, header + 8, 4);
  memcpy(&empty, header + 12, 4);
  memcpy(&multiplier, header + 16, 4);
  if (!capacity || capacity > 1048576 || (capacity & (capacity - 1))) return fail(b, "Invalid registry");
  if (id == empty) return fail(b, "Empty entity");
  if (buckets < LOW || buckets >= HIGH) return fail(b, "Pointer unavailable");
  uint32_t probes = capacity < 4096 ? capacity : 4096;
  for (uint32_t n = 0; n < probes; n++) {
    uint32_t row[2], slot = (id * multiplier + n) & (capacity - 1);
    if (!rd(b, buckets + (uint64_t)slot * 8, row, sizeof row)) return false;
    if (row[0] == empty) return fail(b, "Entity absent");
    if (row[0] == id) {
      if (row[1] >= limit) return fail(b, "Entity index out of bounds");
      *index = row[1];
      return true;
    }
  }
  return fail(b, "Entity lookup bound reached");
}
/* The entity records and maps, and the player fields, where the game's code has them now (src/entity_fields.h). */
static const TfpEntityFields *fields(TfpOps *b, TfpEntityFields *own) {
  const TfpEntityFields *f = tfp_fields_use(b->host->fields, own);
  if (!f) fail(b, "Entity fields unresolved");
  return f;
}
static bool descriptor(TfpOps *b, uint64_t world, uint32_t id, uint8_t d[24]) {
  uint32_t index, entity;
  TfpEntityFields own;
  const TfpEntityFields *f = fields(b, &own);
  if (!f || !map(b, world + f->entity_map, id, 1048576, &index) || !rd(b, tfp_fields_record(world, f, index), d, 24)) return false;
  memcpy(&entity, d + 8, 4);
  if (entity != id || !(d[20] & 1)) return fail(b, "Entity generation unavailable");
  return true;
}
static bool top(TfpOps *b, uint64_t base, TfpOpsSnapshot *s) {
  uint32_t queue[2], request[5];
  if (!ptr(b, base + FP_CAMERA, &s->context) || !rd(b, s->context + L_QUEUE, queue, sizeof queue)) return false;
  if (queue[0] >= 32 || queue[1] >= 32) return fail(b, "Invalid camera request queue");
  s->head = queue[0];
  s->tail = queue[1];
  s->record = s->context + (s->head != s->tail ? L_RING + ((s->tail + 31) % 32) * L_RECORD : L_FALLBACK);
  if (!rd(b, s->record, request, sizeof request) || !ptr(b, s->context, &s->camera)) return false;
  s->base = base;
  s->camera_kind = request[0] & 65535;
  s->request_avatar = request[1];
  s->request_weapon = request[2];
  return true;
}
/* _top(_module(scope)) under pcall. */
static bool top_of(TfpOps *b, int scope, TfpOpsSnapshot *s) {
  uint64_t base;
  memset(s, 0, sizeof *s);
  return module(b, scope, &base) && top(b, base, s);
}

/* ---- identities -------------------------------------------------------------------------- */
bool tfp_ops_same_player(const TfpOpsSnapshot *a, const TfpOpsSnapshot *b) { /* :209-214 */
  return a && b && a->base == b->base && a->context == b->context && a->camera == b->camera && a->world == b->world &&
         a->controller == b->controller && a->player == b->player && a->local_entity == b->local_entity &&
         a->avatar_owner == b->avatar_owner && a->avatar_index == b->avatar_index && a->avatar == b->avatar &&
         a->body == b->body && a->avatar_entry == b->avatar_entry && !memcmp(a->avatar_descriptor, b->avatar_descriptor, 20);
}
bool tfp_ops_same_identity(const TfpOpsSnapshot *a, const TfpOpsSnapshot *b) { /* :233, :266-269 */
  if (!a || !b || a->full != b->full) return false;
  if (!a->full) return tfp_ops_same_player(a, b);
  return a->base == b->base && a->context == b->context && a->camera == b->camera && a->world == b->world &&
         a->controller == b->controller && a->avatar == b->avatar && a->body == b->body && a->weapon == b->weapon &&
         a->weapon_unit == b->weapon_unit && a->viewmodel == b->viewmodel && a->avatar_entry == b->avatar_entry &&
         a->weapon_component_descriptor == b->weapon_component_descriptor &&
         !memcmp(a->avatar_descriptor, b->avatar_descriptor, 20) && !memcmp(a->weapon_descriptor, b->weapon_descriptor, 20);
}
static uint64_t mix(uint64_t hash, const void *data, size_t size) {
  const uint8_t *p = data;
  for (size_t i = 0; i < size; i++) hash = (hash ^ p[i]) * UINT64_C(0x100000001b3);
  return hash;
}
uint64_t tfp_ops_player_key(const TfpOpsSnapshot *s) {
  uint64_t h = UINT64_C(0xcbf29ce484222325);
  uint64_t wide[] = {s->base, s->context, s->camera, s->world, s->controller, s->player, s->local_entity, s->avatar_owner,
                     s->avatar_index, s->avatar, s->body, s->avatar_entry};
  h = mix(mix(h, wide, sizeof wide), s->avatar_descriptor, 20);
  return h ? h : 1;
}
uint64_t tfp_ops_identity_key(const TfpOpsSnapshot *s) {
  if (!s->full) return tfp_ops_player_key(s);
  uint64_t h = UINT64_C(0x84222325cbf29ce4);
  uint64_t wide[] = {s->base, s->context, s->camera, s->world, s->controller, s->avatar, s->body, s->weapon, s->weapon_unit,
                     s->viewmodel, s->avatar_entry, s->weapon_component_descriptor};
  h = mix(mix(mix(h, wide, sizeof wide), s->avatar_descriptor, 20), s->weapon_descriptor, 20);
  return h ? h : 1;
}
static void set_reason(TfpOpsSnapshot *s, const char *text) {
  s->has_reason = true;
  snprintf(s->reason, sizeof s->reason, "%s", text);
}
#define FOREIGN_REASON "Photo Mode or another camera owns the view"

/* ---- _snapshot (:167) -------------------------------------------------------------------- */
static TfpOpsResult action_state(TfpOps *b, const TfpOpsSnapshot *snapshot, TfpOpsAction *out, char *why, size_t why_size);
static bool snapshot(TfpOps *b, bool player_only, const TfpOpsSnapshot *retiring, bool pin_player, TfpOpsSnapshot *out) {
  TfpOpsSnapshot s;
  uint64_t base;
  uint32_t count = 0, index = 0, d[6];
  uint8_t entry[20];
  memset(&s, 0, sizeof s);
  if (!module(b, SCOPE_IDENTITY, &base) || !top(b, base, &s)) return false;
  if (!ptr(b, s.base + FP_WORLD, &s.world) || !ptr(b, s.base + FP_CONTROLLER, &s.controller) ||
      !rd(b, s.controller, s.controller_bytes, 6)) return false;
  uint32_t mask;
  memcpy(&mask, s.controller_bytes, 4);
  s.controller_active = s.controller_bytes[4] != 0;
  s.blocked = mask != 0 || s.controller_bytes[5] != 0;
  TfpEntityFields own;
  const TfpEntityFields *f = fields(b, &own);
  if (!f || !ptr(b, s.base + FP_PLAYER, &s.player) || !u32(b, s.player + f->player_live, &count)) return false;
  if (!count) return fail(b, "Local player unavailable");
  if (!u32(b, s.player + f->player_network, &s.local_entity) || !map(b, s.world + f->network_map, s.local_entity, 1048576, &index) ||
      !rd(b, tfp_fields_record(s.world, f, index), d, sizeof d)) return false;
  if (d[4] != s.local_entity || !(d[5] & 1)) return fail(b, "Local player generation unavailable");
  s.avatar = d[2];
  memcpy(s.avatar_descriptor, d, 20);
  if (!valid_id(s.avatar)) return fail(b, "Local avatar unavailable");
  s.request_local = s.request_avatar == s.avatar;
  if (!pin_player && !s.request_local) return fail(b, "Camera is not owned by local player");
  if (!ptr(b, s.base + FP_AVATAR, &s.avatar_owner) || !map(b, s.avatar_owner + 0xf8, s.avatar, 8, &s.avatar_index) ||
      !u32(b, s.avatar_owner + 0x6c, &count)) return false;
  if (s.avatar_index >= count) return fail(b, "Local avatar retired");
  if (!u32(b, s.avatar_owner + 0x6c, &count)) return false; /* :187 reads the count twice */
  if (count > 8) return fail(b, "Local avatar retired");
  if (!ptr(b, s.avatar_owner + 0x110 + (uint64_t)s.avatar_index * 8, &s.avatar_entry) || !rd(b, s.avatar_entry, entry, sizeof entry))
    return false;
  if (memcmp(entry, s.avatar_descriptor, 20)) return fail(b, "Avatar descriptor changed");
  if (!u32(b, s.avatar_entry + 0xc, &s.body)) return false;
  if (!valid_id(s.body)) return fail(b, "First-person body unavailable");
  s.flags_address = s.avatar_owner + FP_AVATAR_FLAGS + (uint64_t)s.avatar_index * FP_AVATAR_STRIDE;
  if (!rd(b, s.flags_address, s.flags, 24) ||
      !u32(b, s.avatar_owner + FP_AVATAR_RIG + (uint64_t)s.avatar_index * FP_AVATAR_STRIDE, &s.viewmodel)) return false;
  s.aiming = (s.flags[6] & 1) != 0;
  s.shoulder_aim = !s.controller_active && (s.flags[10] & 4) != 0 && (s.flags[15] & 32) != 0;
  s.seated = (s.flags[14] & 4) != 0;
  s.reloading = (s.flags[15] & 2) != 0;
  bool view = s.camera_kind == 4 || s.camera_kind == 6;
  s.safe = s.request_local && view && !s.seated && !s.blocked;
  s.weapon_transition = view && !s.seated && mask == 2 && s.controller_bytes[5] == 0;
  s.head_camera_transition = s.camera_kind == 4 && !s.seated && mask == 0x20 && s.controller_bytes[5] == 0;
  if (player_only) {
    if (pin_player) {
      s.pin_camera = true;
      if (!u32(b, s.context + L_CAMERA_UNIT, &s.camera_unit)) return false;
      if (!valid_id(s.camera_unit)) return fail(b, "Local render camera unavailable");
      if (b->host->foreign_camera) {
        TfpOpsText text;
        none(&text);
        TfpOpsForeign foreign = b->host->foreign_camera(b->host->user, &text);
        if (foreign == TFP_OPS_FOREIGN_RAISED) return fail(b, "%s", text.text);
        if (foreign != TFP_OPS_FOREIGN_FALSE) {
          s.safe = false;
          s.foreign_camera = true;
          set_reason(&s, FOREIGN_REASON);
        }
      }
    }
    *out = s;
    return true;
  }
  uint8_t wd[24];
  uint64_t rows = 0, owner = 0, pool = 0;
  if (retiring) { /* :235 */
    if (!(tfp_ops_same_player(&s, retiring) && s.viewmodel == retiring->viewmodel && s.camera_kind == 6 &&
          s.request_weapon == retiring->weapon && s.safe && !s.aiming && !s.controller_active))
      return fail(b, "Retiring camera player ownership changed");
    s.weapon = retiring->weapon;
  } else {
    uint64_t wield = 0;
    if (!ptr(b, s.base + FP_WIELD, &wield) || !map(b, wield + 0x30, s.avatar, 4096, &index) || !ptr(b, wield + 0x48, &rows) ||
        !ptr(b, rows + (uint64_t)index * 8, &owner) || !rd(b, owner, entry, sizeof entry)) return false;
    if (memcmp(entry, s.avatar_descriptor, 20)) return fail(b, "Wield ownership changed");
    if (!ptr(b, wield + 0x60, &rows) || !u32(b, rows + (uint64_t)index * 0x1d0, &s.weapon)) return false;
  }
  if (!valid_id(s.weapon)) return fail(b, "Equip a firearm before starting the camera test");
  if (!descriptor(b, s.world, s.weapon, wd)) return false;
  if (!ptr(b, s.base + FP_WEAPONS, &pool) || !map(b, pool + 0x30, s.weapon, 4096, &index) || !ptr(b, pool + 0x48, &rows) ||
      !ptr(b, rows + (uint64_t)index * 8, &s.weapon_component_descriptor) || !rd(b, s.weapon_component_descriptor, entry, sizeof entry))
    return false;
  if (memcmp(entry, wd, 20)) return fail(b, "Weapon component ownership changed");
  if (!u32(b, s.weapon_component_descriptor + 0xc, &s.weapon_unit)) return false;
  if (!valid_id(s.weapon_unit)) return fail(b, "Weapon unit unavailable");
  memcpy(s.weapon_descriptor, wd, 20);
  if (s.camera_kind == 6) {
    uint32_t body = 0;
    if (s.request_weapon != s.weapon) return fail(b, "ADS request belongs to another weapon");
    if (!u32(b, s.record + 0xc, &body)) return false;
    if (body != s.body) return fail(b, "ADS request body changed");
  }
  s.full = true;
  s.weapon_ready = true;
  s.engine_safe = s.safe;
  /* :272-283, the shipped path (no io) */
  s.has_action = action_state(b, &s, &s.action, s.action_error, sizeof s.action_error) == TFP_OPS_TRUE;
  s.has_action_error = !s.has_action;
  if (!tfp_ops_on_foot(b, &s, s.has_action ? &s.action : NULL)) {
    s.safe = false;
    set_reason(&s, s.has_action_error ? s.action_error : "Character is not in the verified on-foot lifecycle");
  }
  if (b->host->foreign_camera) {
    TfpOpsText text;
    none(&text);
    TfpOpsForeign foreign = b->host->foreign_camera(b->host->user, &text);
    if (foreign == TFP_OPS_FOREIGN_RAISED) return fail(b, "%s", text.text);
    if (foreign == TFP_OPS_FOREIGN_TRUE) {
      s.safe = false;
      s.foreign_camera = true;
      set_reason(&s, FOREIGN_REASON);
    }
  }
  s.native_pose_eligible = s.safe;
  *out = s;
  return true;
}
enum { TAKE_FULL, TAKE_PLAYER, TAKE_PIN };
/* snapshot() / player_snapshot() / pin_player_snapshot(): pcall(_snapshot, …). */
static bool take(TfpOps *b, int mode, TfpOpsSnapshot *out) {
  return snapshot(b, mode != TAKE_FULL, NULL, mode == TAKE_PIN, out);
}
static TfpOpsResult taken(TfpOps *b, int mode, TfpOpsSnapshot *out, TfpOpsText *why) {
  none(why);
  if (take(b, mode, out)) return TFP_OPS_TRUE;
  return say(why, TFP_OPS_NIL, b->why);
}
TfpOpsResult tfp_ops_snapshot(TfpOps *b, TfpOpsSnapshot *out, TfpOpsText *why) { return taken(b, TAKE_FULL, out, why); }
TfpOpsResult tfp_ops_player_snapshot(TfpOps *b, TfpOpsSnapshot *out, TfpOpsText *why) { return taken(b, TAKE_PLAYER, out, why); }
TfpOpsResult tfp_ops_pin_player_snapshot(TfpOps *b, TfpOpsSnapshot *out, TfpOpsText *why) { return taken(b, TAKE_PIN, out, why); }

/* ---- the action hold's view (not in retail; backend_ops.h) --------------------------------- */
#define B_AVATAR_PHYSICS UINT32_C(0x547844) /* src/arm_drive.c B_AVATAR_PHYSICS: a byte per avatar, stride 0x78, bit 0x10 = ragdoll */
void tfp_ops_set_hold(TfpOps *b, bool on) { b->hold = on; }
TfpOpsResult tfp_ops_hold_snapshot(TfpOps *b, TfpOpsHold *out, TfpOpsText *why) {
  TfpOpsHold h;
  uint8_t physics = 0, entry[20];
  uint64_t wield = 0, rows = 0, owner = 0;
  uint32_t index = 0, weapon = 0;
  none(why);
  memset(&h, 0, sizeof h);
  if (!take(b, TAKE_PLAYER, &h.player)) return say(why, TFP_OPS_NIL, b->why);
  if (!rd(b, h.player.avatar_owner + B_AVATAR_PHYSICS + (uint64_t)h.player.avatar_index * 0x78, &physics, 1)) return say(why, TFP_OPS_NIL, b->why);
  h.ragdoll = (physics & 0x10) != 0;
  /* The wield row as _snapshot reads it (:240-246); here a row that cannot be read is empty hands, not a failure. */
  if (ptr(b, h.player.base + FP_WIELD, &wield) && map(b, wield + 0x30, h.player.avatar, 4096, &index) && ptr(b, wield + 0x48, &rows) &&
      ptr(b, rows + (uint64_t)index * 8, &owner) && rd(b, owner, entry, sizeof entry) && !memcmp(entry, h.player.avatar_descriptor, 20) &&
      ptr(b, wield + 0x60, &rows) && u32(b, rows + (uint64_t)index * 0x1d0, &weapon) && valid_id(weapon))
    h.wielded = weapon;
  if (b->host->foreign_camera) { /* as the full snapshot: a raise is not an answer, TRUE is another owner */
    TfpOpsText text;
    none(&text);
    TfpOpsForeign foreign = b->host->foreign_camera(b->host->user, &text);
    if (foreign == TFP_OPS_FOREIGN_RAISED) return say(why, TFP_OPS_NIL, text.text);
    h.foreign_camera = foreign == TFP_OPS_FOREIGN_TRUE;
  }
  *out = h;
  return TFP_OPS_TRUE;
}

/* ---- _alignment_executable (:688) -------------------------------------------------------- */
/* the three `executable` tables of profile.lua */
enum { EXE_ALIGNMENT = TFP_OPS_EXECUTABLE_ALIGNMENT, EXE_POSE = TFP_OPS_EXECUTABLE_POSE, EXE_CAMERA = TFP_OPS_EXECUTABLE_CAMERA };
static bool executable(TfpOps *b, int profile, uint64_t *out) {
  uint64_t base = b->host->module(b->host->user, TFP_OPS_EXE);
  uint8_t h[96];
  bool equal = false;
  if (b->frame_active) { /* all three tables name helldivers2.exe: one key */
    if (b->frame_executable_base.has && b->frame_executable_base.base != base) invalidate(b);
    b->frame_executable_base.has = true;
    b->frame_executable_base.base = base;
  }
  if (base < LOW) return fail(b, "Game executable unavailable");
  bool cached = b->frame_active && b->frame_executable_checks[profile].has;
  if (cached && b->frame_executable_checks[profile].base == base && b->frame_executable_checks[profile].epoch == b->frame_epoch) {
    *out = base;
    return true;
  }
  if (!pe_header(b, base, h, "Invalid game executable", "Invalid executable PE header")) return false;
  if (!amd64(h) || !stamped(h, FP_EXE_STAMP, FP_EXE_SIZE)) return fail(b, "Unsupported executable build");
  if (!pinned(b, base + B_EXE_EXISTS, B_HEX_EXISTS, &equal)) return false;
  if (!equal) return fail(b, "Alignment executable fingerprint mismatch");
  if (!pinned(b, base + B_EXE_TRIGGER, B_HEX_TRIGGER, &equal)) return false;
  if (!equal) return fail(b, "Alignment executable fingerprint mismatch");
  if (b->frame_active) {
    b->frame_executable_checks[profile].has = true;
    b->frame_executable_checks[profile].base = base;
    b->frame_executable_checks[profile].epoch = b->frame_epoch;
  }
  *out = base;
  return true;
}

/* ---- _alignment_context, _pose_context (:783, :811) --------------------------------------- */
static bool alignment_context(TfpOps *b, uint32_t unit, bool has_owner, uint64_t owner, bool has_avatar_index, uint32_t avatar_index,
                              int profile, TfpOpsContext *out) {
  TfpOpsContext c;
  uint8_t metadata[32], generation = 0;
  uint32_t capacity;
  uint64_t again = 0;
  memset(&c, 0, sizeof c);
  if (!valid_id(unit)) return fail(b, "Native first-person viewmodel unavailable");
  if (!executable(b, profile, &c.exe) || !ptr(b, c.exe + B_EXE_REGISTRY, &c.registry)) return false;
  c.unit = unit;
  c.index = unit % 0x400000;
  c.generation = unit / 0x400000 % 256;
  if (!rd(b, c.registry + L_REGISTRY_BLOCK, metadata, sizeof metadata)) return false;
  memcpy(&capacity, metadata + 16, 4);
  if (!capacity || capacity > 0x400000 || c.index >= capacity) return fail(b, "Viewmodel registry index unavailable");
  memcpy(&c.generations, metadata + 24, 8);
  memcpy(&c.units, metadata, 8);
  if (c.generations < LOW || c.generations >= HIGH || c.units < LOW || c.units >= HIGH) return fail(b, "Pointer unavailable");
  if (!u8(b, c.generations + c.index, &generation)) return false;
  if (generation != c.generation) return fail(b, "Viewmodel generation retired");
  if (!ptr(b, c.units + (uint64_t)c.index * 8, &c.object)) return false;
  if (!u8(b, c.generations + c.index, &generation)) return false;
  if (generation != c.generation) return fail(b, "Viewmodel generation changed during lookup");
  if (!ptr(b, c.units + (uint64_t)c.index * 8, &again)) return false;
  if (again != c.object) return fail(b, "Viewmodel generation changed during lookup");
  c.has_owner = has_owner;
  c.owner = has_owner ? owner : 0;
  c.has_avatar_index = has_avatar_index;
  c.avatar_index = has_avatar_index ? avatar_index : 0;
  *out = c;
  return true;
}
static bool snapshot_context(TfpOps *b, const TfpOpsSnapshot *s, int profile, TfpOpsContext *out) {
  return alignment_context(b, s->viewmodel, true, s->avatar_owner, true, s->avatar_index, profile, out);
}
static bool pose_context(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsContext *out) {
  TfpOpsContext c;
  uint8_t bytes[20], generation = 0;
  uint32_t unit, capacity = 0;
  uint64_t again = 0;
  if (!snapshot_context(b, s, EXE_POSE, &c)) return false;
  if (!s->full || s->weapon_component_descriptor < LOW) return fail(b, "Weapon component descriptor unavailable");
  if (!rd(b, s->weapon_component_descriptor, bytes, 20)) return false;
  memcpy(&unit, bytes + 12, 4);
  if (memcmp(bytes, s->weapon_descriptor, 20) || unit != s->weapon_unit) return fail(b, "Weapon component descriptor changed");
  c.weapon_index = s->weapon_unit % 0x400000;
  c.weapon_generation = s->weapon_unit / 0x400000 % 256;
  if (!u32(b, c.registry + L_REGISTRY_CAPACITY, &capacity)) return false;
  if (c.weapon_index >= capacity) return fail(b, "Weapon registry index unavailable");
  if (!u8(b, c.generations + c.weapon_index, &generation)) return false;
  if (generation != c.weapon_generation) return fail(b, "Weapon generation retired");
  if (!ptr(b, c.units + (uint64_t)c.weapon_index * 8, &c.weapon_object)) return false;
  if (!u8(b, c.generations + c.weapon_index, &generation)) return false;
  if (generation != c.weapon_generation) return fail(b, "Weapon changed during generation lookup");
  if (!ptr(b, c.units + (uint64_t)c.weapon_index * 8, &again)) return false;
  if (again != c.weapon_object) return fail(b, "Weapon changed during generation lookup");
  if (!rd(b, s->weapon_component_descriptor, bytes, 20)) return false;
  if (memcmp(bytes, s->weapon_descriptor, 20)) return fail(b, "Weapon changed during generation lookup");
  c.weapon_descriptor = s->weapon_component_descriptor;
  memcpy(c.weapon_bytes, s->weapon_descriptor, 20);
  c.weapon_unit = s->weapon_unit;
  c.pose = true;
  *out = c;
  return true;
}
static bool same_alignment_context(const TfpOpsContext *a, const TfpOpsContext *b) { /* :1055 */
  return a->exe == b->exe && a->unit == b->unit && a->index == b->index && a->generation == b->generation && a->object == b->object &&
         a->registry == b->registry && a->units == b->units && a->generations == b->generations && a->has_owner == b->has_owner &&
         a->owner == b->owner && a->has_avatar_index == b->has_avatar_index && a->avatar_index == b->avatar_index;
}
static bool same_pose_context(const TfpOpsContext *a, const TfpOpsContext *b) { /* :831 */
  return same_alignment_context(a, b) && a->pose == b->pose && a->weapon_descriptor == b->weapon_descriptor &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, 20) && a->weapon_unit == b->weapon_unit && a->weapon_index == b->weapon_index &&
         a->weapon_generation == b->weapon_generation && a->weapon_object == b->weapon_object;
}
static bool context_of(TfpOps *b, bool pose, const TfpOpsSnapshot *s, TfpOpsContext *out) {
  return pose ? pose_context(b, s, out) : snapshot_context(b, s, EXE_ALIGNMENT, out);
}
static bool same_context(bool pose, const TfpOpsContext *a, const TfpOpsContext *b) {
  return pose ? same_pose_context(a, b) : same_alignment_context(a, b);
}

/* ---- action_state.lua: sample (A:47) ----------------------------------------------------- */
static const uint8_t EXPECTED_RESOURCE[8] = {151, 250, 77, 41, 77, 51, 28, 77}; /* 4D1C334D294DFA97 */
static const uint8_t ANIM_GETTER[8] = {0x48, 0x8b, 0x81, 0x78, 0x01, 0, 0, 0xc3};
static const uint8_t RESOURCE_GETTER[14] = {0x48, 0x8b, 0x81, 0x58, 0x03, 0, 0, 0x48, 0x89, 0x02, 0x48, 0x8b, 0xc2, 0xc3};
static const uint8_t LAYERS[] = {1, 3, 14}, SUPPORT_LAYERS[] = {1, 3, 10, 11, 14}; /* A:71, :343 */
static bool definition_offset(TfpOps *b, uint32_t value) { /* A:21 */
  if (value < 12 || value > 0x1000000) return fail(b, "Animation definition offset out of bounds");
  return true;
}
static int layer_slot(unsigned layer) {
  for (int i = 0; i < 5; i++)
    if (SUPPORT_LAYERS[i] == layer) return i;
  return -1;
}
static void owner_of(TfpOpsAction *a, const TfpOpsSnapshot *s) {
  a->owner_base = s->base, a->owner_context = s->context, a->owner_camera = s->camera, a->owner_world = s->world;
  a->owner_controller = s->controller, a->owner_player = s->player, a->owner_avatar_owner = s->avatar_owner;
  a->owner_entry = s->avatar_entry, a->owner_light = s->local_entity, a->owner_avatar_index = s->avatar_index;
  a->owner_avatar = s->avatar, a->owner_body = s->body;
  memcpy(a->owner_descriptor, s->avatar_descriptor, 20);
}
static bool owner_is(const TfpOpsAction *a, const TfpOpsSnapshot *s) { /* action.owner==s.player_identity */
  return a->owner_base == s->base && a->owner_context == s->context && a->owner_camera == s->camera && a->owner_world == s->world &&
         a->owner_controller == s->controller && a->owner_player == s->player && a->owner_avatar_owner == s->avatar_owner &&
         a->owner_entry == s->avatar_entry && a->owner_light == s->local_entity && a->owner_avatar_index == s->avatar_index &&
         a->owner_avatar == s->avatar && a->owner_body == s->body && !memcmp(a->owner_descriptor, s->avatar_descriptor, 20);
}
static bool sample(TfpOps *b, const TfpOpsSnapshot *expected, const uint8_t *layers, unsigned layer_count, TfpOpsAction *out) {
  TfpOpsSnapshot own, after;
  const TfpOpsSnapshot *s = expected;
  TfpOpsContext c, final;
  TfpOpsAction a;
  uint64_t vt = 0, getter = 0, active = 0, again = 0, table_at;
  uint8_t code[14], header[12], definitions[128], bytes[128];
  uint32_t count, table_offset;
  struct { uint64_t at, raw, def; uint32_t count; } observed[5];
  memset(&a, 0, sizeof a);
  if (!s) { /* A:49 */
    if (!take(b, TAKE_PLAYER, &own)) return false; /* assert(player(s), why) */
    s = &own;
  }
  if (!alignment_context(b, s->body, true, s->avatar_owner, true, s->avatar_index, EXE_ALIGNMENT, &c)) return false;
  if (!ptr(b, c.object, &vt)) return false;
  if (!ptr(b, vt + L_VT_ANIM_GETTER, &getter)) return false;
  if (getter != c.exe + B_EXE_ANIM_GETTER) return fail(b, "Animation interface getter changed");
  if (!rd(b, c.exe + B_EXE_ANIM_GETTER, code, sizeof ANIM_GETTER)) return false;
  if (memcmp(code, ANIM_GETTER, sizeof ANIM_GETTER)) return fail(b, "Animation interface getter changed");
  if (!ptr(b, vt + L_VT_RESOURCE_GETTER, &getter)) return false;
  if (getter != c.exe + B_EXE_RESOURCE_GETTER) return fail(b, "Animation resource getter changed");
  if (!rd(b, c.exe + B_EXE_RESOURCE_GETTER, code, sizeof RESOURCE_GETTER)) return false;
  if (memcmp(code, RESOURCE_GETTER, sizeof RESOURCE_GETTER)) return fail(b, "Animation resource getter changed");
  if (!rd(b, c.object + L_UNIT_RESOURCE, a.resource, 8) || !ptr(b, c.object + L_UNIT_ANIM, &a.instance) ||
      !ptr(b, a.instance + L_ANIM_GRAPH, &a.graph) || !rd(b, a.graph, header, 12)) return false;
  memcpy(&count, header + 4, 4);
  if (!count || count > 32) return fail(b, "Animation layer count out of bounds");
  memcpy(&table_offset, header + 8, 4);
  if (!definition_offset(b, table_offset)) return false;
  if (table_offset > 0x10000) return fail(b, "Animation layer table out of bounds");
  table_at = a.graph + table_offset;
  if (!rd(b, table_at + 4, definitions, count * 4) || !ptr(b, a.instance + L_ANIM_ACTIVE, &active)) return false;
  if (!b->actions.has || b->actions.body != c.object || b->actions.ref != c.unit || b->actions.generation != c.generation ||
      b->actions.instance != a.instance || b->actions.graph != a.graph || memcmp(b->actions.resource, a.resource, 8) ||
      memcmp(b->actions.header, header, 12) || b->actions.count != count || memcmp(b->actions.definitions, definitions, count * 4)) {
    b->actions.has = true;
    b->actions.body = c.object, b->actions.ref = c.unit, b->actions.generation = c.generation;
    b->actions.instance = a.instance, b->actions.graph = a.graph, b->actions.count = count;
    memcpy(b->actions.resource, a.resource, 8);
    memcpy(b->actions.header, header, 12);
    memcpy(b->actions.definitions, definitions, count * 4);
    for (int i = 0; i < 5; i++) b->actions.maps[i].has = false;
  }
  for (unsigned i = 0; i < layer_count; i++) {
    unsigned layer = layers[i];
    uint32_t relative, n = 0;
    uint64_t def, raw = 0;
    if (layer > count) return fail(b, "Animation layer selection out of bounds");
    memcpy(&relative, definitions + (layer - 1) * 4, 4);
    if (!definition_offset(b, relative)) return false;
    def = table_at + relative;
    if (!u32(b, def + 8, &n)) return false;
    if (!n || n > 2048) return fail(b, "Animation state count out of bounds");
    TfpOpsLayerMap *m = b->actions.maps + layer_slot(layer);
    if (!m->has || m->def != def || m->count != n) {
      uint32_t entries[2048];
      for (uint32_t done = 0; done < n * 4; done += 1024) /* block, A:39 */
        if (!rd(b, def + 12 + done, (uint8_t *)entries + done, n * 4 - done < 1024 ? n * 4 - done : 1024)) return false;
      for (uint32_t state = 0; state < n; state++)
        if (!definition_offset(b, entries[state])) return false;
      m->has = true, m->def = def, m->count = n;
      for (uint32_t state = 0; state < n; state++) m->at[state] = def + entries[state];
    }
    if (!rd(b, active + (uint64_t)(layer - 1) * 8, &raw, 8)) return false;
    int index = -1;
    if (raw) {
      if (raw < LOW || raw >= HIGH) return fail(b, "Animation state pointer unavailable");
      index = -2;
      for (uint32_t state = 0; state < n && index < 0; state++)
        if (m->at[state] == raw) index = (int)state;
    }
    a.sampled |= UINT64_C(1) << layer;
    if (index == -2) a.unknown |= UINT64_C(1) << layer;
    a.states[layer] = (int16_t)(index == -2 ? 0 : index);
    observed[i].at = active + (uint64_t)(layer - 1) * 8, observed[i].raw = raw, observed[i].def = def, observed[i].count = n;
  }
  if (!take(b, TAKE_PLAYER, &after)) return false; /* A:93 */
  if (!tfp_ops_same_player(&after, s) || after.body != s->body) return fail(b, "Animation player changed during read");
  if (!alignment_context(b, after.body, true, after.avatar_owner, true, after.avatar_index, EXE_ALIGNMENT, &final)) return false;
  if (!same_alignment_context(&final, &c)) return fail(b, "Animation body generation changed during read");
  if (!ptr(b, c.object + L_UNIT_ANIM, &again)) return false;
  if (again != a.instance) return fail(b, "Animation graph changed during read");
  if (!rd(b, c.object + L_UNIT_RESOURCE, bytes, 8)) return false;
  if (memcmp(bytes, a.resource, 8)) return fail(b, "Animation graph changed during read");
  if (!ptr(b, a.instance + L_ANIM_GRAPH, &again)) return false;
  if (again != a.graph) return fail(b, "Animation graph changed during read");
  if (!ptr(b, a.instance + L_ANIM_ACTIVE, &again)) return false;
  if (again != active) return fail(b, "Animation graph changed during read");
  if (!rd(b, a.graph, bytes, 12)) return false;
  if (memcmp(bytes, header, 12)) return fail(b, "Animation graph changed during read");
  if (!rd(b, table_at + 4, bytes, count * 4)) return false;
  if (memcmp(bytes, definitions, count * 4)) return fail(b, "Animation graph changed during read");
  for (unsigned i = 0; i < layer_count; i++) {
    uint64_t raw = 0;
    uint32_t n = 0;
    if (!rd(b, observed[i].at, &raw, 8)) return false;
    if (raw != observed[i].raw) return fail(b, "Animation layer changed during read");
    if (!u32(b, observed[i].def + 8, &n)) return false;
    if (n != observed[i].count) return fail(b, "Animation layer changed during read");
  }
  a.resource_match = !memcmp(a.resource, EXPECTED_RESOURCE, 8) && count == 31;
  a.sprint_hypothesis = a.resource_match && (a.states[1] == 78 || a.states[1] == 79 || a.states[3] == 71);
  owner_of(&a, s);
  a.body = s->body;
  a.body_object = c.object;
  a.layer_count = count;
  *out = a;
  return true;
}
static TfpOpsResult action_state(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsAction *out, char *why, size_t why_size) { /* :344 */
  bool support = b->support_grip_tracking;
  if (sample(b, s, support ? SUPPORT_LAYERS : LAYERS, support ? 5 : 3, out)) return TFP_OPS_TRUE;
  if (why && why_size) keep_text(why, why_size, b->why);
  return TFP_OPS_NIL;
}
TfpOpsResult tfp_ops_action_state(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsAction *out, TfpOpsText *why) {
  none(why);
  if (action_state(b, s, out, NULL, 0) == TFP_OPS_TRUE) return TFP_OPS_TRUE;
  return say(why, TFP_OPS_NIL, b->why);
}
static bool state_is(const TfpOpsAction *a, unsigned layer, int value) {
  return (a->sampled >> layer & 1) && a->states[layer] == value;
}
bool tfp_ops_on_foot(TfpOps *b, const TfpOpsSnapshot *s, const TfpOpsAction *action) { /* :351 */
  uint32_t low, scene = 0, again = 0;
  uint64_t state = 0, state_again = 0;
  uint8_t bytes[4];
  if (!action || !action->resource_match || action->layer_count != 31) return false;
  if (state_is(action, 14, 36)) return true;
  if (!state_is(action, 14, 3) || !s) return false;
  memcpy(&low, s->flags, 4);
  if ((low & 6) != 2) return false;
  /* the pcall of :359; any raise is "not on foot" */
  if (s->avatar_index >= 8) return false;
  uint64_t flags = s->avatar_owner + FP_AVATAR_FLAGS + (uint64_t)s->avatar_index * FP_AVATAR_STRIDE;
  if (s->flags_address != flags) return false;
  if (!rd(b, flags, bytes, 4) || memcmp(bytes, s->flags, 4)) return false;
  if (!ptr(b, s->base + B_GAME_STATE, &state) || !u32(b, state + B_STATE_SCENE, &scene)) return false;
  if (scene != 3 && scene != 4) return false;
  if (!u32(b, state + B_STATE_SCENE, &again) || again != scene) return false;
  if (!ptr(b, s->base + B_GAME_STATE, &state_again) || state_again != state) return false;
  if (!rd(b, flags, bytes, 4) || memcmp(bytes, s->flags, 4)) return false;
  return true;
}

/* ---- backpack, camera_only_weapon (:301, :317) -------------------------------------------- */
TfpOpsResult tfp_ops_backpack(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsBackpack *out, TfpOpsText *why) {
  uint64_t base = 0, inventory = 0, owners = 0, owner = 0, rows = 0;
  uint32_t index = 0, entity = 0, again = 0, ref;
  uint8_t bytes[20], d[24];
  none(why);
  if (!s) return say(why, TFP_OPS_RAISED, "Backpack player unavailable");
  if (!module(b, SCOPE_FULL, &base)) return raised(b, why);
  if (base != s->base) return say(why, TFP_OPS_RAISED, "Backpack player unavailable");
  if (!ptr(b, s->base + B_GAME_INVENTORY, &inventory) || !map(b, inventory + L_INVENTORY_MAP, s->avatar, 4096, &index) ||
      !ptr(b, inventory + L_INVENTORY_OWNERS, &owners) || !ptr(b, owners + (uint64_t)index * 8, &owner) || !rd(b, owner, bytes, 20))
    return raised(b, why);
  if (memcmp(bytes, s->avatar_descriptor, 20)) return say(why, TFP_OPS_RAISED, "Backpack inventory owner changed");
  if (!ptr(b, inventory + L_INVENTORY_ROWS, &rows)) return raised(b, why);
  uint64_t row = rows + (uint64_t)index * L_INVENTORY_ROW;
  if (!u32(b, row + L_INVENTORY_BACKPACK, &entity)) return raised(b, why);
  if (!valid_id(entity)) return TFP_OPS_NIL;
  if (!descriptor(b, s->world, entity, d)) return raised(b, why);
  memcpy(&ref, d + 12, 4);
  if (!valid_id(ref) || ref == s->body || ref == s->viewmodel || (s->full && ref == s->weapon_unit))
    return say(why, TFP_OPS_RAISED, "Backpack renderer overlaps player or weapon");
  if (!u32(b, row + L_INVENTORY_BACKPACK, &again)) return raised(b, why);
  if (again != entity) return say(why, TFP_OPS_RAISED, "Backpack inventory changed");
  if (!rd(b, owner, bytes, 20)) return raised(b, why);
  if (memcmp(bytes, s->avatar_descriptor, 20)) return say(why, TFP_OPS_RAISED, "Backpack inventory changed");
  out->ref = ref;
  out->entity = entity;
  memcpy(out->descriptor, d, 20);
  return TFP_OPS_TRUE;
}
/* Beyond retail (round 22, for the log only): the snapshot player's inventory row, words +0x0..+0x14, read the way
 * tfp_ops_backpack finds the row. false: not read (b->why says why). */
bool tfp_ops_inventory_row(TfpOps *b, const TfpOpsSnapshot *s, uint32_t slots[6]) {
  uint64_t inventory = 0, owners = 0, owner = 0, rows = 0;
  uint32_t index = 0;
  uint8_t bytes[20];
  if (!s || !ptr(b, s->base + B_GAME_INVENTORY, &inventory) || !map(b, inventory + L_INVENTORY_MAP, s->avatar, 4096, &index) ||
      !ptr(b, inventory + L_INVENTORY_OWNERS, &owners) || !ptr(b, owners + (uint64_t)index * 8, &owner) || !rd(b, owner, bytes, 20))
    return false;
  if (memcmp(bytes, s->avatar_descriptor, 20)) return fail(b, "Backpack inventory owner changed");
  if (!ptr(b, inventory + L_INVENTORY_ROWS, &rows)) return false;
  return rd(b, rows + (uint64_t)index * L_INVENTORY_ROW, slots, 6 * sizeof *slots);
}
TfpOpsResult tfp_ops_camera_only_weapon(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsText *why) {
  static const uint8_t HAMMER[8] = {83, 133, 189, 162, 189, 201, 62, 95};
  TfpOpsContext c;
  uint8_t resource[8];
  none(why);
  if (!s || !s->full || !valid_id(s->weapon_unit)) return TFP_OPS_FALSE;
  if (b->camera_only_identity.has && b->camera_only_identity.world == s->world && b->camera_only_identity.unit == s->weapon_unit &&
      !memcmp(b->camera_only_identity.descriptor, s->weapon_descriptor, 20))
    return b->camera_only_identity.hammer ? TFP_OPS_TRUE : TFP_OPS_FALSE;
  if (!alignment_context(b, s->weapon_unit, false, 0, false, 0, EXE_ALIGNMENT, &c) || !rd(b, c.object + L_UNIT_RESOURCE, resource, 8))
    return raised(b, why);
  b->camera_only_identity.has = true;
  b->camera_only_identity.hammer = !memcmp(resource, HAMMER, 8);
  b->camera_only_identity.world = s->world;
  b->camera_only_identity.unit = s->weapon_unit;
  memcpy(b->camera_only_identity.descriptor, s->weapon_descriptor, 20);
  return b->camera_only_identity.hammer ? TFP_OPS_TRUE : TFP_OPS_FALSE;
}

/* ---- camera_config, fov_config, blur_config, fird_fov_config (:374-437) ------------------- */
static bool camera_config(TfpOps *b, TfpOpsConfig *out) {
  TfpOpsSnapshot s;
  TfpOpsAction action;
  TfpOpsConfig c;
  char problem[sizeof b->why];
  bool direct = b->fird_camera_active;
  memset(&c, 0, sizeof c);
  memset(&action, 0, sizeof action);
  if (!take(b, TAKE_PLAYER, &s)) return false;
  if (s.foreign_camera) return fail(b, FOREIGN_REASON);
  if (!((s.camera_kind == 4 || s.camera_kind == 6) && !s.seated)) return fail(b, "No supported local gameplay camera");
  bool sampled = action_state(b, &s, &action, problem, sizeof problem) == TFP_OPS_TRUE;
  if (!(tfp_ops_on_foot(b, &s, sampled ? &action : NULL) && owner_is(&action, &s) && action.body == s.body))
    return fail(b, "%s", sampled ? "Head camera requires the verified current on-foot lifecycle" : problem);
  if (!b->profile.has_camera) return fail(b, "assertion failed!");
  bool offset = b->fird_camera_active && b->has_fird_camera_offset;
  c.is_camera = true;
  c.game = s.base;
  if (!executable(b, EXE_CAMERA, &c.exe)) return false;
  c.context = s.context, c.camera = s.camera, c.avatar_owner = s.avatar_owner, c.avatar_index = s.avatar_index;
  c.avatar = s.avatar, c.body = s.body;
  c.presentation_enabled = !direct;
  if (!u32(b, s.context + L_CAMERA_UNIT, &c.camera_unit)) return false;
  c.camera_kind = s.camera_kind;
  c.forward = offset ? b->fird_camera_offset[1] : b->profile.camera_forward;
  c.up = offset ? b->fird_camera_offset[2] : b->profile.camera_up;
  c.has_right = offset;
  c.right = offset ? b->fird_camera_offset[0] : 0;
  c.smoothing_ms = direct ? 0 : b->profile.camera_smoothing_ms;
  c.blend_ms = direct ? 0 : b->profile.camera_blend_ms;
  c.max_gap_ms = b->profile.camera_max_gap_ms;
  *out = c;
  return true;
}
TfpOpsResult tfp_ops_camera_config(TfpOps *b, TfpOpsConfig *out, TfpOpsText *why) {
  none(why);
  if (camera_config(b, out)) return TFP_OPS_TRUE;
  return say(why, TFP_OPS_NIL, b->why);
}
static bool pose_lease(const TfpOps *b, const TfpOpsLease *lease) { /* the current, live pose lease */
  return lease && lease == b->alignment_lease && !lease->done && !lease->cleanup_only && lease->has_alignment &&
         lease->alignment.kind == TFP_OPS_KIND_POSE;
}
TfpOpsResult tfp_ops_fov_config(TfpOps *b, const TfpOpsLease *lease, TfpOpsConfig *out, TfpOpsText *why) {
  TfpOpsSnapshot s;
  uint64_t exe = 0;
  none(why);
  if (!take(b, TAKE_FULL, &s)) return say(why, TFP_OPS_NIL, b->why);
  if (lease) {
    if (!pose_lease(b, lease) || !tfp_ops_same_identity(&s, &lease->origin) || s.camera_kind != 6 || s.record != lease->request ||
        !s.engine_safe)
      return say(why, TFP_OPS_NIL, "FOV requires the current owned pose-camera request");
    exe = lease->alignment.context.exe;
  } else {
    if (s.camera_kind != 4 || !s.safe || s.aiming || s.controller_active)
      return say(why, TFP_OPS_NIL, "FOV preflight requires a safe normal camera with aiming released");
    if (!executable(b, EXE_POSE, &exe)) return say(why, TFP_OPS_NIL, b->why);
  }
  if (exe < LOW) return say(why, TFP_OPS_NIL, "FOV executable identity unavailable");
  memset(out, 0, sizeof *out);
  out->game = s.base, out->exe = exe, out->context = s.context, out->camera = s.camera, out->avatar_owner = s.avatar_owner;
  out->has_request = true, out->request = s.record, out->avatar_index = s.avatar_index, out->avatar = s.avatar, out->body = s.body;
  out->has_weapon = true, out->weapon = s.weapon;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_blur_config(TfpOps *b, const TfpOpsLease *lease, TfpOpsConfig *out, TfpOpsText *why) {
  return tfp_ops_fov_config(b, lease, out, why);
}
TfpOpsResult tfp_ops_fird_fov_config(TfpOps *b, TfpOpsConfig *out, TfpOpsText *why) {
  TfpOpsSnapshot s;
  TfpOpsConfig c;
  none(why);
  if (!take(b, TAKE_PLAYER, &s)) return say(why, TFP_OPS_NIL, b->why);
  if (!s.safe || s.camera_kind != 4 || s.controller_active) return say(why, TFP_OPS_NIL, "Fird Person requires the current normal gameplay camera");
  if (!camera_config(b, &c)) return say(why, TFP_OPS_NIL, b->why);
  if (c.context != s.context || c.camera != s.camera || c.body != s.body || c.avatar != s.avatar)
    return say(why, TFP_OPS_NIL, "Fird Person camera changed during FOV validation");
  memset(out, 0, sizeof *out);
  out->game = c.game, out->exe = c.exe, out->context = c.context, out->camera = c.camera, out->avatar_owner = c.avatar_owner;
  out->has_request = true, out->request = s.record, out->avatar_index = c.avatar_index, out->avatar = c.avatar, out->body = c.body;
  out->has_weapon = true, out->weapon = 0;
  return TFP_OPS_TRUE;
}

/* ---- _invoke (:438) ---------------------------------------------------------------------- */
static bool executable_page(TfpOps *b, uint64_t address, uint64_t image, bool image_kind) {
  TfpOpsRegion r;
  memset(&r, 0, sizeof r);
  b->host->query(b->host->user, address, &r);
  return r.answered && r.state == 0x1000 &&
         (!image_kind || r.kind == 0x1000000 || (r.kind == 0x40000 && b->host->wine && r.allocation == image)) &&
         (r.protect == 0x20 || r.protect == 0x40 || r.protect == 0x80);
}
static bool native(TfpOps *b, TfpOpsNative which, uint64_t address, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t *result) {
  const uint64_t args[4] = {a0, a1, a2, a3};
  uint64_t value = 0;
  TfpOpsText text;
  none(&text);
  if (!b->host->call(b->host->user, which, address, args, &value, &text)) return fail(b, "%s", text.text);
  if (result) *result = value;
  return true;
}
/* s: base and context always; controller, avatar and weapon for `allowed` and `enter`. */
static bool invoke(TfpOps *b, TfpOpsNative which, const TfpOpsSnapshot *s, bool entry, bool *allowed) {
  uint64_t base = 0, result = 0;
  invalidate(b);
  if (!module(b, entry ? SCOPE_ENTRY : SCOPE_FULL, &base)) return false;
  if (base != s->base) return fail(b, "Game module changed");
  uint64_t address = s->base + (which == TFP_OPS_ALLOWED ? FP_ALLOWED : which == TFP_OPS_ENTER ? FP_ENTRY : FP_EXIT);
  if (!executable_page(b, address, s->base, true)) return fail(b, "Native function is not in an executable image page");
  if (which == TFP_OPS_ALLOWED) {
    if (!native(b, which, address, s->controller, s->weapon, 0, 0, &result)) return false;
    *allowed = (result & 0xff) != 0;
    return true;
  }
  if (which == TFP_OPS_ENTER) return native(b, which, address, s->context, s->avatar, s->weapon, 0, NULL);
  return native(b, which, address, s->context, 0, 0, 0, NULL);
}
static bool normal_released(const TfpOpsSnapshot *s) { return s->safe && s->camera_kind == 4 && !s->aiming && !s->controller_active; }
static int8_t flag(bool value) { return value ? 1 : 0; }
static void lease_new(TfpOpsLease *lease, const TfpOpsSnapshot *origin) {
  memset(lease, 0, sizeof *lease);
  lease->origin = *origin;
  lease->request = origin->context + L_RING + (uint64_t)origin->tail * L_RECORD;
  lease->pose_blend_entry = lease->entry_aim_changed = lease->entry_controller_changed = -1;
  lease->body_event_aim_changed = lease->body_event_controller_changed = -1;
}
static void lease_error(TfpOpsLease *lease, const char *text) {
  lease->has_error = true;
  snprintf(lease->error, sizeof lease->error, "%s", text);
}
static bool foreign_not_false(TfpOps *b, bool *raised_out) { /* self.foreign_camera and self.foreign_camera()~=false */
  TfpOpsText text;
  *raised_out = false;
  if (!b->host->foreign_camera) return false;
  none(&text);
  TfpOpsForeign foreign = b->host->foreign_camera(b->host->user, &text);
  if (foreign == TFP_OPS_FOREIGN_RAISED) {
    fail(b, "%s", text.text);
    *raised_out = true;
  }
  return foreign != TFP_OPS_FOREIGN_FALSE;
}

/* ---- warm_start, warm_finish (:461, :488) ------------------------------------------------- */
TfpOpsResult tfp_ops_warm_start(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsLease *lease, TfpOpsText *why) {
  TfpOpsSnapshot current, fresh, after;
  bool allowed = false, escaped = false;
  none(why);
  if (b->warm_lease && !b->warm_lease->done) return say(why, TFP_OPS_FALSE, "Native FP initialization cleanup is pending");
  if (b->alignment_lease && !b->alignment_lease->done) return say(why, TFP_OPS_FALSE, "A pose helper already owns the camera");
  if (!take(b, TAKE_FULL, &current)) return say(why, TFP_OPS_FALSE, b->why);
  if (!expected || !tfp_ops_same_identity(&current, expected)) return say(why, TFP_OPS_FALSE, "Warm camera owner changed");
  if (!normal_released(&current)) return say(why, TFP_OPS_FALSE, "Warm entry requires the safe released normal camera");
  if (foreign_not_false(b, &escaped)) return escaped ? raised(b, why) : say(why, TFP_OPS_FALSE, "Another camera owns initialization");
  if (!invoke(b, TFP_OPS_ALLOWED, &current, true, &allowed)) return say(why, TFP_OPS_FALSE, b->why);
  if (!allowed) return say(why, TFP_OPS_FALSE, "Weapon does not support native FPV");
  bool taken_fresh = take(b, TAKE_FULL, &fresh);
  if (!taken_fresh) return say(why, TFP_OPS_FALSE, b->why);
  if (!tfp_ops_same_identity(&fresh, &current) || fresh.record != current.record || fresh.tail != current.tail || !normal_released(&fresh))
    return say(why, TFP_OPS_FALSE, "Warm owner changed before entry");
  lease_new(lease, &fresh);
  lease->owner = b;
  lease->bootstrap = true;
  b->warm_lease = lease;
  bool called = invoke(b, TFP_OPS_ENTER, &fresh, true, NULL);
  char error[sizeof b->why];
  memcpy(error, b->why, sizeof error);
  bool has_after = take(b, TAKE_FULL, &after);
  if (has_after && tfp_ops_same_identity(&after, &fresh)) {
    lease->entry_aim_changed = flag(after.aiming != fresh.aiming);
    lease->entry_controller_changed = flag(memcmp(after.controller_bytes, fresh.controller_bytes, 6) != 0);
  }
  if (!called) {
    lease->cleanup_only = true;
    lease_error(lease, error);
  } else if (!has_after || !tfp_ops_same_identity(&after, &fresh) || after.camera_kind != 6 || after.record != lease->request) {
    lease->cleanup_only = true;
    lease_error(lease, "Native FP initialization entry acknowledgement is pending");
  }
  return TFP_OPS_TRUE;
}
static TfpOpsResult warm_done(TfpOps *b, TfpOpsLease *lease, TfpOpsText *why, const char *text) {
  lease->done = true;
  b->warm_lease = NULL;
  return say(why, TFP_OPS_TRUE, text);
}
TfpOpsResult tfp_ops_warm_finish(TfpOps *b, TfpOpsLease *lease, TfpOpsText *why) {
  TfpOpsSnapshot top_now, current, after;
  bool escaped = false;
  none(why);
  if (!lease || lease->owner != b || !lease->bootstrap) return say(why, TFP_OPS_FALSE, "Initialization lease is not owned");
  if (lease->done) return TFP_OPS_TRUE;
  if (b->warm_lease != lease) return say(why, TFP_OPS_FALSE, "Initialization generation changed");
  const TfpOpsSnapshot *origin = &lease->origin;
  if (!top_of(b, SCOPE_IDENTITY, &top_now)) {
    put(why, "Waiting to verify initialization cleanup: %s", b->why);
    return TFP_OPS_FALSE;
  }
  if (top_now.base != origin->base || top_now.context != origin->context || top_now.camera != origin->camera)
    return warm_done(b, lease, why, "Engine replaced initialization camera");
  if (top_now.camera_kind != 6 || top_now.record != lease->request || top_now.request_avatar != origin->avatar ||
      top_now.request_weapon != origin->weapon)
    return warm_done(b, lease, why, "Native initialization request retired");
  if (!take(b, TAKE_FULL, &current)) return say(why, TFP_OPS_FALSE, b->why);
  if (!tfp_ops_same_identity(&current, origin)) return say(why, TFP_OPS_FALSE, "Waiting for outgoing initialization request retirement");
  if ((current.aiming || current.controller_active) && lease->entry_aim_changed != 1 && lease->entry_controller_changed != 1)
    return warm_done(b, lease, why, "Player aiming took ownership");
  if (!current.safe) return say(why, TFP_OPS_FALSE, "Another action or camera owns initialization cleanup");
  if (foreign_not_false(b, &escaped)) return escaped ? raised(b, why) : say(why, TFP_OPS_FALSE, "Another action or camera owns initialization cleanup");
  if (!invoke(b, TFP_OPS_EXIT, &current, true, NULL)) {
    put(why, "Native initialization exit pending: %s", b->why);
    return TFP_OPS_FALSE;
  }
  if (!top_of(b, SCOPE_IDENTITY, &after)) return say(why, TFP_OPS_FALSE, "Initialization exit readback pending");
  if (after.context == origin->context && after.camera_kind == 6 && after.record == lease->request &&
      after.request_avatar == origin->avatar && after.request_weapon == origin->weapon)
    return say(why, TFP_OPS_FALSE, "Native initialization request is still present");
  return warm_done(b, lease, why, "Native FP initialization camera exited");
}

/* ---- _invoke_presentation (:517) ----------------------------------------------------------- */
TfpOpsResult tfp_ops_invoke_presentation(TfpOps *b, const TfpOpsSnapshot *expected, bool enabled, TfpOpsText *why) {
  TfpOpsSnapshot current, after;
  uint64_t game = 0, exe = 0;
  uint8_t before = 0, now = 0;
  none(why);
  if (!B_PRESENTATION_ESTABLISHED)
    return say(why, TFP_OPS_RAISED, "FP presentation is not established on build 25480438 (function, code pins and mode byte are open)");
  invalidate(b);
  if (!take(b, TAKE_FULL, &current)) return say(why, TFP_OPS_RAISED, "Presentation player unavailable");
  if (!expected || !tfp_ops_same_player(&current, expected) || current.viewmodel != expected->viewmodel)
    return say(why, TFP_OPS_RAISED, "Presentation owner changed before invocation");
  if (!(current.safe && current.camera_kind == 4 && !current.controller_active))
    return say(why, TFP_OPS_RAISED, "Presentation requires the current normal gameplay camera");
  if (b->host->foreign_camera) {
    TfpOpsText text;
    none(&text);
    TfpOpsForeign foreign = b->host->foreign_camera(b->host->user, &text);
    if (foreign == TFP_OPS_FOREIGN_RAISED) return say(why, TFP_OPS_RAISED, text.text);
    if (foreign != TFP_OPS_FOREIGN_FALSE) return say(why, TFP_OPS_RAISED, "Another camera owns presentation");
  }
  if (!module(b, SCOPE_FULL, &game)) return raised(b, why);
  if (game != current.base) return say(why, TFP_OPS_RAISED, "Presentation game module changed");
  if (!executable(b, EXE_POSE, &exe)) return raised(b, why);
  for (size_t i = 0; i < sizeof presentation_pins / sizeof *presentation_pins; i++) {
    bool equal = false;
    if (!pinned(b, (presentation_pins[i].exe ? exe : game) + presentation_pins[i].rva, presentation_pins[i].hex, &equal)) return raised(b, why);
    if (!equal) return say(why, TFP_OPS_RAISED, "FP presentation function contract changed");
  }
  uint64_t mode = current.avatar_owner + B_PRESENTATION_MODE + (uint64_t)current.avatar_index * B_PRESENTATION_MODE_STRIDE;
  if (!u8(b, mode, &before)) return raised(b, why);
  if (before > 1) return say(why, TFP_OPS_RAISED, "Presentation mode is unavailable");
  uint64_t address = game + B_PRESENTATION_FUNCTION;
  if (!executable_page(b, address, game, true)) return say(why, TFP_OPS_RAISED, "Presentation function is not an executable image page");
  if (!native(b, TFP_OPS_PRESENTATION, address, current.avatar_owner, current.avatar, enabled ? 1 : 0, 1, NULL)) return raised(b, why);
  invalidate(b);
  if (!take(b, TAKE_FULL, &after)) return say(why, TFP_OPS_RAISED, "Presentation invocation result is uncertain");
  if (!(tfp_ops_same_player(&after, &current) && after.context == current.context && after.camera_kind == 4 &&
        after.body == current.body && after.viewmodel == current.viewmodel))
    return say(why, TFP_OPS_RAISED, "Presentation owner changed during invocation");
  if (!u8(b, mode, &now)) return raised(b, why);
  if (now != (enabled ? 1 : 0)) return say(why, TFP_OPS_RAISED, "Presentation mode acknowledgement is pending");
  return TFP_OPS_TRUE;
}

/* ---- _alignment_config, _pose_config, placement_copy (:839-876, :1063) --------------------- */
static void alignment_config(const TfpOps *b, const TfpOpsSnapshot *s, uint64_t request, const TfpOpsContext *context, TfpOpsHelperConfig *c) {
  memset(c, 0, sizeof *c);
  c->game = s->base, c->exe = context->exe, c->context = s->context, c->request = request, c->avatar_owner = s->avatar_owner;
  c->avatar_index = s->avatar_index, c->avatar = s->avatar, c->weapon = s->weapon, c->body = s->body, c->viewmodel = s->viewmodel;
  c->duration_ms = b->profile.alignment_duration_ms;
}
/* remaining_ms < 0: nil. */
static bool pose_config(TfpOps *b, const TfpOpsSnapshot *s, uint64_t request, const TfpOpsContext *context, double remaining_ms,
                        int blend_entry, TfpOpsHelperConfig *c) {
  uint8_t resource[8];
  char key[17];
  TfpOpsPlacement placement;
  bool found = false;
  alignment_config(b, s, request, context, c);
  c->pose = true;
  c->duration_ms = remaining_ms >= 0 ? remaining_ms : b->profile.pose_duration_ms;
  c->weapon_descriptor = context->weapon_descriptor;
  c->weapon_unit = context->weapon_unit;
  if (!rd(b, context->weapon_object + L_UNIT_RESOURCE, resource, 8)) return false;
  for (int i = 0; i < 8; i++) snprintf(key + i * 2, 3, "%02x", resource[i]);
  if (!strcmp(key, "0000000000000000")) return fail(b, "Weapon resource unavailable");
  memset(&placement, 0, sizeof placement);
  if (b->host->placement_resolver) {
    TfpOpsText text;
    none(&text);
    TfpOpsResult r = b->host->placement_resolver(b->host->user, key, &placement, &text);
    if (r == TFP_OPS_RAISED) return fail(b, "%s", text.text);
    found = r == TFP_OPS_TRUE;
  }
  if (!found && b->has_pose_placement) placement = b->pose_placement, found = true;
  if (!found) {
    memset(&placement, 0, sizeof placement);
    placement.x = b->profile.pose_offset_x, placement.y = b->profile.pose_offset_y, placement.z = b->profile.pose_offset_z;
  }
  placement.present = TFP_OPS_X | TFP_OPS_Y | TFP_OPS_Z | TFP_OPS_YAW | TFP_OPS_PITCH | TFP_OPS_ROLL;
  b->pose_placement = placement;
  b->has_pose_placement = true;
  memcpy(b->placement_weapon_key, key, sizeof key);
  b->has_placement_weapon_key = true;
  c->blend_ms = b->profile.pose_blend_ms;
  c->steady_entry = remaining_ms == 0 && blend_entry != 1;
  c->offset_x = placement.x, c->offset_y = placement.y, c->offset_z = placement.z;
  return true;
}
static const char *placement_copy(const TfpOpsPlacement *p, TfpOpsPlacement *copy) {
  if (!p) return "Placement requires XYZ";
  const double xyz[3] = {p->x, p->y, p->z}, angles[3] = {p->yaw, p->pitch, p->roll};
  memset(copy, 0, sizeof *copy);
  for (int i = 0; i < 3; i++)
    if (!(p->present >> i & 1) || xyz[i] != xyz[i] || xyz[i] < -.5 || xyz[i] > .5) return "Placement must be finite XYZ within 0.5 metres";
  double out[3];
  for (int i = 0; i < 3; i++) {
    out[i] = p->present >> (i + 3) & 1 ? angles[i] : 0;
    if (out[i] != out[i] || fabs(out[i]) > 90) return "Invalid placement angle";
  }
  copy->x = p->x, copy->y = p->y, copy->z = p->z, copy->yaw = out[0], copy->pitch = out[1], copy->roll = out[2];
  copy->present = TFP_OPS_X | TFP_OPS_Y | TFP_OPS_Z | TFP_OPS_YAW | TFP_OPS_PITCH | TFP_OPS_ROLL;
  return NULL;
}

/* ---- _enter, enter, enter_pose (:567-679) -------------------------------------------------- */
static TfpOpsToken *issued(const TfpOps *b, TfpOpsToken *token, TfpOpsKind kind) { /* tokens[token] */
  /* :573 `kind=='pose' and self.pose_tokens or self.alignment_tokens`: until the first pose token
   * exists, a pose entry looks its token up among the alignment tokens. */
  if (kind == TFP_OPS_KIND_POSE && !b->has_tokens[TFP_OPS_KIND_POSE]) kind = TFP_OPS_KIND_ALIGNMENT;
  return b->has_tokens[kind] && token && token->issued && token->owner == b && token->kind == kind ? token : NULL;
}
static TfpOpsResult enter(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, bool pose, double remaining_ms, int blend_entry,
                          TfpOpsLease *out, TfpOpsText *why) {
  TfpOpsSnapshot s, current, final, after;
  TfpOpsToken *prepared = NULL;
  TfpOpsLease lease;
  bool allowed = false;
  none(why);
  memset(&after, 0, sizeof after);
  if (!take(b, TAKE_FULL, &s)) return say(why, TFP_OPS_FALSE, b->why);
  if (!expected || !tfp_ops_same_identity(expected, &s)) return say(why, TFP_OPS_FALSE, "Owner changed before entry");
  if (token) {
    prepared = issued(b, token, pose ? TFP_OPS_KIND_POSE : TFP_OPS_KIND_ALIGNMENT);
    if (!prepared || prepared->used || !tfp_ops_same_identity(&prepared->identity, &s))
      return say(why, TFP_OPS_FALSE, "Alignment preparation token is invalid, consumed, or owned by another backend");
  }
  if (!normal_released(&s)) return say(why, TFP_OPS_FALSE, "Entry requires normal on-foot camera with real aiming released");
  if (!invoke(b, TFP_OPS_ALLOWED, &s, false, &allowed)) return say(why, TFP_OPS_FALSE, b->why);
  if (!allowed) return say(why, TFP_OPS_FALSE, "Weapon does not support native FPV");
  if (!take(b, TAKE_FULL, &current) || !tfp_ops_same_identity(&current, &s) || !normal_released(&current))
    return say(why, TFP_OPS_FALSE, "Owner changed during preflight");
  lease_new(&lease, &current);
  if (pose) lease.pose_blend_entry = flag(blend_entry == 1);
  if (prepared) {
    TfpOpsContext context, final_context;
    TfpOpsHelperConfig config;
    TfpOpsHandle *handle = NULL;
    TfpOpsText second;
    if (!context_of(b, pose, &current, &context)) return say(why, TFP_OPS_FALSE, b->why);
    if (!same_context(pose, &context, &prepared->context)) return say(why, TFP_OPS_FALSE, "Prepared viewmodel or executable changed before entry");
    lease.has_alignment = true;
    lease.alignment.helper = prepared->helper, lease.alignment.prepared = prepared->native, lease.alignment.context = context;
    lease.alignment.ended = false, lease.alignment.kind = pose ? TFP_OPS_KIND_POSE : TFP_OPS_KIND_ALIGNMENT;
    prepared->used = true;
    if (pose) {
      if (!pose_config(b, &current, lease.request, &context, remaining_ms, blend_entry, &config)) return raised(b, why);
    } else alignment_config(b, &current, lease.request, &context, &config);
    none(&second);
    TfpOpsResult accepted = prepared->helper->begin(prepared->helper->user, &config, prepared->native, &handle, &second);
    if (accepted == TFP_OPS_TRUE) {
      lease.alignment.handle = handle;
    } else {
      TfpOpsHandle *owned = NULL;
      if (prepared->helper->owned_handle) {
        TfpOpsText ignored;
        TfpOpsHandle *value = NULL;
        none(&ignored);
        if (prepared->helper->owned_handle(prepared->helper->user, &value, &ignored) != TFP_OPS_RAISED) owned = value;
      }
      if (owned) {
        TfpOpsText text;
        lease.alignment.handle = owned;
        if (accepted == TFP_OPS_RAISED) put(&text, "Alignment arm result uncertain: %s", second.text);
        else put(&text, "%s", second.present ? second.text : "nil");
        lease_error(&lease, text.text);
        lease.cleanup_only = true;
        *out = lease;
        b->alignment_lease = out;
        return TFP_OPS_TRUE;
      }
      put(why, "Alignment arm refused: %s", second_or_first(accepted, &second));
      return TFP_OPS_FALSE;
    }
    bool valid = false;
    if (take(b, TAKE_FULL, &final) && tfp_ops_same_identity(&final, &current) && final.camera_kind == 4 && final.safe && !final.aiming &&
        !final.controller_active && final.tail == current.tail && final.record == current.record)
      valid = context_of(b, pose, &final, &final_context);
    if (!valid || !same_context(pose, &context, &final_context)) {
      lease_error(&lease, "Owner or viewmodel changed after alignment arm; native entry skipped");
      lease.cleanup_only = true;
      *out = lease;
      b->alignment_lease = out;
      return TFP_OPS_TRUE;
    }
    current = final;
  }
  if (!invoke(b, TFP_OPS_ENTER, &current, false, NULL)) {
    char text[sizeof b->why + 32];
    lease_error(&lease, b->why);
    lease.cleanup_only = true;
    snprintf(text, sizeof text, "Native entry error: %s", lease.error);
    if (b->host->log) b->host->log(b->host->user, text);
  }
  bool has_after = take(b, TAKE_FULL, &after);
  if (has_after && tfp_ops_same_identity(&after, &current)) {
    lease.entry_aim_changed = flag(after.aiming != current.aiming);
    lease.entry_controller_changed = flag(memcmp(after.controller_bytes, current.controller_bytes, 6) != 0);
  }
  if (b->host->log) {
    char text[256], kind[16];
    snprintf(kind, sizeof kind, "%u", after.camera_kind);
    snprintf(text, sizeof text, "Native entry: aim before=%s, after=%s; controller before=%s, after=%s; camera=%s; packed flags unchanged=%s",
             truth(current.aiming), has_after ? truth(after.aiming) : "nil", truth(current.controller_active),
             has_after ? truth(after.controller_active) : "nil", has_after ? kind : "nil",
             has_after ? truth(!memcmp(after.flags, current.flags, 24)) : "nil");
    b->host->log(b->host->user, text);
  }
  *out = lease;
  if (prepared) b->alignment_lease = out;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_enter(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsLease *lease, TfpOpsText *why) {
  return enter(b, expected, token, false, -1, -1, lease, why);
}
TfpOpsResult tfp_ops_enter_pose(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, double remaining_ms, int blend_entry,
                                TfpOpsLease *lease, TfpOpsText *why) {
  none(why);
  if (remaining_ms != floor(remaining_ms) || remaining_ms < 0 || remaining_ms > b->profile.pose_duration_ms) {
    char limit[32];
    tfp_ops_number(b->profile.pose_duration_ms, limit, sizeof limit);
    put(why, "Pose duration must be an integer from 0 to %s milliseconds", limit);
    return TFP_OPS_FALSE;
  }
  if (blend_entry != -1 && blend_entry != 0 && blend_entry != 1) return say(why, TFP_OPS_FALSE, "Pose entry blend must be boolean");
  if (!token) return say(why, TFP_OPS_FALSE, "Pose preparation token unavailable");
  if (b->host->camera_before_entry) {
    TfpOpsText second;
    none(&second);
    TfpOpsResult ready = b->host->camera_before_entry(b->host->user, &second);
    if (ready != TFP_OPS_TRUE) return say(why, TFP_OPS_FALSE, second_or_first(ready, &second));
  }
  return enter(b, expected, token, true, remaining_ms, blend_entry, lease, why);
}

/* ---- placements and per-frame publications (:878-1021) ------------------------------------- */
TfpOpsResult tfp_ops_set_placement_defaults(TfpOps *b, const TfpOpsPlacement *placement, TfpOpsText *why) {
  TfpOpsPlacement copy;
  const char *problem = placement_copy(placement, &copy);
  none(why);
  if (problem) return say(why, TFP_OPS_FALSE, problem);
  b->pose_placement = copy;
  b->has_pose_placement = true;
  return TFP_OPS_TRUE;
}
static bool running_pose(const TfpOps *b, const TfpOpsLease *lease) { return pose_lease(b, lease) && !lease->alignment.ended; }
/* snapshot(); s.identity==lease.origin.identity, safe, kind 6, our record. false: `why` holds the text or the default. */
static bool owned_view(TfpOps *b, const TfpOpsLease *lease, TfpOpsSnapshot *s, TfpOpsText *why, const char *otherwise) {
  if (!take(b, TAKE_FULL, s)) {
    put(why, "%s", b->why);
    return false;
  }
  if (!tfp_ops_same_identity(s, &lease->origin) || !s->safe || s->camera_kind != 6 || s->record != lease->request) {
    put(why, "%s", otherwise);
    return false;
  }
  return true;
}
TfpOpsResult tfp_ops_set_pose_placement(TfpOps *b, TfpOpsLease *lease, const TfpOpsPlacement *placement, TfpOpsText *why) {
  TfpOpsPlacement copy;
  TfpOpsSnapshot s;
  TfpOpsContext current;
  TfpOpsText second;
  const char *problem = placement_copy(placement, &copy);
  none(why);
  if (problem) return say(why, TFP_OPS_FALSE, problem);
  if (!running_pose(b, lease)) return say(why, TFP_OPS_FALSE, "Pose placement lease is not active or owned");
  if (!owned_view(b, lease, &s, why, "Owned pose camera changed before placement")) return TFP_OPS_FALSE;
  if (!pose_context(b, &s, &current) || !same_pose_context(&current, &lease->alignment.context))
    return say(why, TFP_OPS_FALSE, "Pose weapon or viewmodel ownership changed before placement");
  none(&second);
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  TfpOpsResult accepted = helper->set_placement(helper->user, lease->alignment.handle, copy.x, copy.y, copy.z, &second);
  if (accepted != TFP_OPS_TRUE) return say(why, TFP_OPS_FALSE, second_or_first(accepted, &second));
  b->pose_placement = copy;
  b->has_pose_placement = true;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_set_pose_zoom_policy(TfpOps *b, TfpOpsLease *lease, bool active, TfpOpsText *why) {
  TfpOpsSnapshot s;
  none(why);
  if (!running_pose(b, lease)) return say(why, TFP_OPS_FALSE, "Zoom pose lease is not active or owned");
  if (!owned_view(b, lease, &s, why, "Zoom pose owner changed")) return TFP_OPS_FALSE;
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  return helper->set_zoom_policy(helper->user, lease->alignment.handle, active, why);
}
TfpOpsResult tfp_ops_set_ads_placement(TfpOps *b, TfpOpsLease *lease, const TfpOpsPlacement *value, TfpOpsText *why) {
  TfpOpsPlacement copy;
  TfpOpsSnapshot s;
  const char *problem = placement_copy(value, &copy);
  none(why);
  if (problem) return say(why, TFP_OPS_FALSE, problem);
  if (!running_pose(b, lease)) return say(why, TFP_OPS_FALSE, "ADS lease unavailable");
  if (!owned_view(b, lease, &s, why, "ADS placement owner changed")) return TFP_OPS_FALSE;
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  return helper->set_ads_placement(helper->user, lease->alignment.handle, copy.x, copy.y, copy.z, why);
}
static bool same_phase(const TfpOpsPhase *a, const TfpOpsPhase *b) { /* PHASE_FIELDS, :935 */
  return a->has_epoch == b->has_epoch && a->epoch == b->epoch && a->has_progress == b->has_progress && a->progress == b->progress &&
         a->ads == b->ads && a->identity == b->identity && a->player_identity == b->player_identity && a->reason == b->reason &&
         a->action == b->action && a->has_started == b->has_started && (!a->has_started || a->started == b->started) &&
         a->has_duration == b->has_duration && (!a->has_duration || a->duration == b->duration) && a->continuous == b->continuous;
}
TfpOpsResult tfp_ops_set_pose_transition(TfpOps *b, TfpOpsLease *lease, const TfpOpsPhase *token, TfpOpsText *why) { /* :936 */
  static const char NOT_OWNED[] = "Shared pose transition handle is not owned";
  TfpOpsText second;
  none(why);
  if (!token) return TFP_OPS_TRUE;
  if (!b->frame_active) return say(why, TFP_OPS_FALSE, "Transition publication requires the qualified Update");
  if (!lease || lease != b->alignment_lease || lease->done || lease->cleanup_only || !lease->has_alignment ||
      lease->alignment.kind != TFP_OPS_KIND_POSE || lease->alignment.ended)
    return say(why, TFP_OPS_FALSE, "Transition pose lease is not owned");
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  TfpOpsHandle *handle = lease->alignment.handle, *owned = NULL;
  if (!helper || !handle || handle->owner != helper || handle->disarmed || !helper->owned_handle || !helper->transition)
    return say(why, TFP_OPS_FALSE, NOT_OWNED);
  none(&second);
  if (helper->owned_handle(helper->user, &owned, &second) == TFP_OPS_RAISED || owned != handle) return say(why, TFP_OPS_FALSE, NOT_OWNED);
  if (!token->has_epoch || token->epoch != token->epoch || token->epoch < 1 || token->epoch > 9007199254740991.0 ||
      fmod(token->epoch, 1) != 0 || !token->has_progress || token->progress != token->progress || token->progress < 0 ||
      token->progress > 1 || token->ads < 0 || !token->identity || !token->player_identity)
    return say(why, TFP_OPS_FALSE, "Invalid shared presentation phase");
  if (token->identity != tfp_ops_identity_key(&lease->origin) || token->player_identity != tfp_ops_player_key(&lease->origin))
    return TFP_OPS_TRUE;
  if (b->publication.has && b->publication.frame == b->frame_epoch && b->publication.lease == lease &&
      b->publication.helper == helper && b->publication.handle == handle && same_phase(&b->publication.token, token))
    return TFP_OPS_TRUE;
  b->publication.has = false;
  none(&second);
  TfpOpsResult done = helper->transition(helper->user, handle, token, &second);
  if (done != TFP_OPS_TRUE) return say(why, TFP_OPS_FALSE, second_or_first(done, &second));
  b->publication.has = true;
  b->publication.frame = b->frame_epoch, b->publication.lease = lease, b->publication.helper = helper, b->publication.handle = handle;
  b->publication.token = *token;
  b->presentation_pose_lease = lease;
  b->presentation_pose_epoch = token->epoch;
  b->has_presentation_pose_epoch = true;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_set_pose_motion(TfpOps *b, TfpOpsLease *lease, const TfpOpsMotion *value, TfpOpsText *why) { /* :983 */
  TfpOpsSnapshot s;
  TfpOpsContext current;
  TfpOpsText second;
  none(why);
  if (!value) return say(why, TFP_OPS_FALSE, "Motion requires XYZ displacement");
  const double d[3] = {value->dx, value->dy, value->dz};
  for (int i = 0; i < 3; i++)
    if (d[i] != d[i] || fabs(d[i]) > .15) return say(why, TFP_OPS_FALSE, "Motion displacement out of bounds");
  double q[4] = {value->q[0], value->q[1], value->q[2], value->q[3]}, norm = 0;
  unsigned present = value->q_present & 15;
  if (!present) q[0] = q[1] = q[2] = 0, q[3] = 1, present = 15;
  for (int i = 0; i < 4; i++) {
    if (!(present >> i & 1) || q[i] != q[i] || fabs(q[i]) > 1.001) return say(why, TFP_OPS_FALSE, "Motion requires a finite unit quaternion");
    norm = norm + q[i] * q[i];
  }
  if (fabs(norm - 1) > .002 || fabs(q[3]) / sqrt(norm) < .5)
    return say(why, TFP_OPS_FALSE, "Motion rotation exceeds 120 degrees or is not normalized");
  if (!running_pose(b, lease)) return say(why, TFP_OPS_FALSE, "Motion pose lease is not owned");
  if (!owned_view(b, lease, &s, why, "Motion camera ownership changed")) return TFP_OPS_FALSE;
  if (!pose_context(b, &s, &current) || !same_pose_context(&current, &lease->alignment.context))
    return say(why, TFP_OPS_FALSE, "Motion weapon or viewmodel generation changed");
  if (b->host->presentation_transition) {
    TfpOpsPhase token;
    TfpOpsText text;
    memset(&token, 0, sizeof token);
    none(&text);
    TfpOpsResult got = b->host->presentation_transition(b->host->user, &token, &text);
    if (got == TFP_OPS_RAISED) return say(why, TFP_OPS_RAISED, text.text);
    TfpOpsResult synchronized = tfp_ops_set_pose_transition(b, lease, got == TFP_OPS_TRUE ? &token : NULL, why);
    if (synchronized != TFP_OPS_TRUE) return TFP_OPS_FALSE; /* return false,reason */
  }
  none(&second);
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  TfpOpsResult accepted = helper->set_motion(helper->user, lease->alignment.handle, value->dx, value->dy, value->dz, q[0], q[1], q[2], q[3],
                                             value->authored_ads, &second);
  if (accepted != TFP_OPS_TRUE) return say(why, TFP_OPS_FALSE, second_or_first(accepted, &second));
  none(why);
  return TFP_OPS_TRUE;
}

/* ---- prepare_pose, prepare_alignment (:1023, :1069) ---------------------------------------- */
static bool normal(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsSnapshot *s, TfpOpsText *why, const char *changed, const char *unsafe) {
  if (!take(b, TAKE_FULL, s)) {
    put(why, "%s", b->why);
    return false;
  }
  if (!expected || !tfp_ops_same_identity(expected, s)) {
    put(why, "%s", changed);
    return false;
  }
  if (!normal_released(s)) {
    put(why, "%s", unsafe);
    return false;
  }
  return true;
}
static TfpOpsResult prepare(TfpOps *b, const TfpOpsSnapshot *expected, bool pose, TfpOpsToken *token, TfpOpsText *why) {
  const char *changed = pose ? "Owner changed before pose preparation" : "Owner changed before alignment preparation";
  const char *unsafe = pose ? "Pose preparation requires a safe normal camera with real aiming released"
                            : "Alignment preparation requires a safe normal camera with real aiming released";
  TfpOpsSnapshot s, final;
  TfpOpsContext context, fresh;
  TfpOpsHelperConfig config;
  TfpOpsText second;
  uint64_t base;
  void *prepared = NULL;
  none(why);
  if (pose && b->warm_lease && !b->warm_lease->done) return say(why, TFP_OPS_FALSE, "Native FP initialization cleanup is pending");
  if (b->alignment_lease && !b->alignment_lease->done)
    return say(why, TFP_OPS_FALSE, pose ? "Previous native helper lease has not completed cleanup" : "Previous alignment lease has not completed cleanup");
  if (!normal(b, expected, &s, why, changed, unsafe)) return TFP_OPS_FALSE;
  if (pose && !module(b, SCOPE_FULL, &base)) return say(why, TFP_OPS_FALSE, b->why);
  if (!context_of(b, pose, &s, &context)) return say(why, TFP_OPS_FALSE, b->why);
  const TfpOpsPoseHelper **slot = pose ? &b->pose : &b->alignment;
  if (!*slot) { /* _pose_helper / _alignment_helper */
    TfpOpsText text;
    const TfpOpsPoseHelper *(*load)(void *, TfpOpsText *) = pose ? b->host->load_pose : b->host->load_alignment;
    none(&text);
    if (!load) return say(why, TFP_OPS_FALSE, pose ? "Pose module loader unavailable" : "Alignment module loader unavailable");
    const TfpOpsPoseHelper *loaded = load(b->host->user, &text);
    if (!loaded) return say(why, TFP_OPS_FALSE, text.text);
    *slot = loaded;
  }
  const TfpOpsPoseHelper *helper = *slot;
  uint64_t request = s.context + L_RING + (uint64_t)s.tail * L_RECORD;
  if (pose) {
    if (!pose_config(b, &s, request, &context, -1, -1, &config)) return raised(b, why);
  } else alignment_config(b, &s, request, &context, &config);
  none(&second);
  TfpOpsResult ready = helper->prepare(helper->user, &config, &prepared, &second);
  if (ready != TFP_OPS_TRUE) return say(why, TFP_OPS_FALSE, second_or_first(ready, &second));
  if (!normal(b, expected, &final, why, changed, unsafe)) return TFP_OPS_FALSE;
  if (!context_of(b, pose, &final, &fresh)) return say(why, TFP_OPS_FALSE, b->why);
  if (!same_context(pose, &context, &fresh))
    return say(why, TFP_OPS_FALSE, pose ? "Weapon or viewmodel changed during pose preparation" : "Viewmodel changed during alignment preparation");
  memset(token, 0, sizeof *token);
  token->owner = b, token->kind = pose ? TFP_OPS_KIND_POSE : TFP_OPS_KIND_ALIGNMENT, token->issued = true;
  token->identity = final, token->context = fresh, token->helper = helper, token->native = prepared;
  b->has_tokens[token->kind] = true;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_prepare_pose(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsText *why) {
  return prepare(b, expected, true, token, why);
}
TfpOpsResult tfp_ops_prepare_alignment(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsText *why) {
  return prepare(b, expected, false, token, why);
}

/* ---- _stop_alignment, pose_status, alignment_status (:1100-1185) --------------------------- */
static bool stop_alignment(TfpOpsLease *lease, TfpOpsText *why) {
  TfpOpsText second;
  TfpOpsHelperStats stats;
  bool has_stats = false;
  if (!lease->has_alignment || lease->alignment.ended) return true;
  none(&second);
  memset(&stats, 0, sizeof stats);
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  TfpOpsResult done = helper->finish(helper->user, lease->alignment.handle, &has_stats, &stats, &second);
  if (done != TFP_OPS_TRUE) {
    put(why, "Alignment disarm pending: %s", second_or_first(done, &second));
    return false;
  }
  lease->alignment.ended = true;
  if (has_stats) {
    lease->alignment_stats = stats, lease->has_alignment_stats = true;
    if (lease->alignment.kind == TFP_OPS_KIND_POSE) lease->pose_stats = stats, lease->has_pose_stats = true;
  }
  return true;
}
static void stop_quietly(TfpOpsLease *lease) {
  TfpOpsText ignored;
  stop_alignment(lease, &ignored);
}
static TfpOpsResult status(TfpOps *b, TfpOpsLease *lease, bool pose, TfpOpsHelperStats *out, TfpOpsText *why) {
  TfpOpsHelperStats stats;
  TfpOpsText second;
  TfpOpsSnapshot s;
  TfpOpsContext current;
  char number[32];
  none(why);
  if (!lease || !lease->has_alignment || (pose && lease->alignment.kind != TFP_OPS_KIND_POSE))
    return say(why, TFP_OPS_NIL, pose ? "Invalid pose lease" : "Invalid alignment lease");
  if (lease->alignment.ended) return say(why, TFP_OPS_NIL, pose ? "Pose lease is disarmed" : "Alignment lease is disarmed");
  none(&second);
  memset(&stats, 0, sizeof stats);
  const TfpOpsPoseHelper *helper = lease->alignment.helper;
  TfpOpsResult got = helper->stats(helper->user, lease->alignment.handle, &stats, &second);
  if (got != TFP_OPS_TRUE) {
    stop_quietly(lease);
    put(why, "%s statistics unavailable: %s", pose ? "Pose" : "Alignment", second_or_first(got, &second));
    return TFP_OPS_NIL;
  }
  stats.armed = stats.armed == 1, stats.expired = stats.expired == 1, stats.normalised = true;
  lease->alignment_stats = stats, lease->has_alignment_stats = true;
  if (pose) {
    lease->pose_stats = stats, lease->has_pose_stats = true;
    lease->has_pose_error = stats.has_error, lease->pose_error = stats.error;
  }
  bool taken_now = take(b, TAKE_FULL, &s);
  if (pose) {
    if (!taken_now || !tfp_ops_same_identity(&s, &lease->origin) || !s.safe ||
        (s.camera_kind != 4 && (s.camera_kind != 6 || s.record != lease->request))) {
      lease->pose_owner_retired = true;
      lease->has_pose_retire_reason = true;
      snprintf(lease->pose_retire_reason, sizeof lease->pose_retire_reason, "%s",
               !taken_now ? b->why : s.has_reason ? s.reason : "Owned pose camera or player changed");
      stop_quietly(lease);
      return say(why, TFP_OPS_NIL, lease->pose_retire_reason);
    }
  } else if (!taken_now || !tfp_ops_same_identity(&s, &lease->origin) || s.camera_kind != 6 || s.record != lease->request || s.aiming ||
             s.controller_active || !s.safe) {
    char problem[sizeof b->why];
    snprintf(problem, sizeof problem, "%s",
             !taken_now ? b->why : lease->has_error ? lease->error : "Owned alignment camera changed or native aim took control");
    stop_quietly(lease);
    return say(why, TFP_OPS_NIL, problem);
  }
  bool valid = context_of(b, pose, &s, &current);
  if (!valid || !same_context(pose, &current, &lease->alignment.context)) {
    TfpOpsText text;
    put(&text, "%s ownership changed: %s", pose ? "Pose weapon or viewmodel" : "Alignment viewmodel", valid ? "identity mismatch" : b->why);
    if (pose) {
      lease->pose_owner_retired = true;
      lease->has_pose_retire_reason = true;
      memcpy(lease->pose_retire_reason, text.text, sizeof lease->pose_retire_reason);
    }
    stop_quietly(lease);
    return say(why, TFP_OPS_NIL, text.text);
  }
  if (stats.has_error && stats.error != 0) {
    stop_quietly(lease);
    if (pose && stats.error == 14)
      return say(why, TFP_OPS_NIL, "Pose test stopped: native sight node is zero; weapon-only offset is unavailable");
    tfp_ops_number(stats.error, number, sizeof number);
    put(why, "%s helper error %s", pose ? "Pose" : "Alignment", number);
    return TFP_OPS_NIL;
  }
  *out = stats;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_pose_status(TfpOps *b, TfpOpsLease *lease, TfpOpsHelperStats *out, TfpOpsText *why) {
  return status(b, lease, true, out, why);
}
TfpOpsResult tfp_ops_alignment_status(TfpOps *b, TfpOpsLease *lease, TfpOpsHelperStats *out, TfpOpsText *why) {
  return status(b, lease, false, out, why);
}

/* ---- the paired exit body event (:1189-1332) ----------------------------------------------- */
typedef struct BodyApi { uint64_t executable, table, query, trigger; uint32_t event; } BodyApi;
static bool body_api(TfpOps *b, uint64_t game_base, BodyApi *out) {
  uint64_t game = 0, root = 0;
  uint8_t h[96];
  bool equal = false;
  BodyApi api;
  if (!module(b, SCOPE_FULL, &game)) return false;
  if (game != game_base) return fail(b, "Game module changed before body event");
  api.executable = b->host->module(b->host->user, TFP_OPS_EXE);
  if (api.executable < LOW) return fail(b, "Game executable unavailable");
  if (!pe_header(b, api.executable, h, "Invalid game executable", "Invalid executable PE header")) return false;
  if (!amd64(h)) return fail(b, "AMD64 executable PE32+ required");
  if (!stamped(h, FP_EXE_STAMP, FP_EXE_SIZE)) return fail(b, "Unsupported executable build");
  if (!pinned(b, api.executable + B_EXE_EXISTS, B_HEX_EXISTS, &equal)) return false;
  if (!equal) return fail(b, "Paired exit body event fingerprint mismatch");
  if (!pinned(b, api.executable + B_EXE_TRIGGER, B_HEX_TRIGGER, &equal)) return false;
  if (!equal) return fail(b, "Paired exit body event fingerprint mismatch");
  if (!ptr(b, game_base + B_GAME_API_ROOT, &root) || !ptr(b, root + L_API_UNIT, &api.table) || !ptr(b, api.table + L_API_EXISTS, &api.query) ||
      !ptr(b, api.table + L_API_TRIGGER, &api.trigger)) return false;
  if (api.query != api.executable + B_EXE_EXISTS || api.trigger != api.executable + B_EXE_TRIGGER)
    return fail(b, "Unit API function ownership changed");
  api.event = L_BODY_EVENT;
  *out = api;
  return true;
}
static bool same_body_api(const BodyApi *a, const BodyApi *b) {
  return a->executable == b->executable && a->table == b->table && a->query == b->query && a->trigger == b->trigger && a->event == b->event;
}
static bool invoke_body(TfpOps *b, bool trigger, const TfpOpsSnapshot *s, const BodyApi *expected, bool *exists) {
  BodyApi api;
  uint64_t result = 0;
  invalidate(b);
  if (!body_api(b, s->base, &api)) return false;
  if (!same_body_api(&api, expected)) return fail(b, "Unit API changed during body event preflight");
  uint64_t address = trigger ? api.trigger : api.query;
  if (!executable_page(b, address, api.executable, true)) return fail(b, "Body event function is not in an executable image page");
  if (!native(b, trigger ? TFP_OPS_BODY_TRIGGER : TFP_OPS_BODY_EXISTS, address, s->body, api.event, 0, 0, &result)) return false;
  if (exists) *exists = (uint32_t)result != 0;
  return true;
}
TfpOpsResult tfp_ops_prepare_body_pose(TfpOps *b, const TfpOpsSnapshot *expected, TfpOpsToken *token, TfpOpsText *why) {
  static const char CHANGED[] = "Owner changed before body event preparation";
  static const char UNSAFE[] = "Body event preparation requires a safe normal camera with real aiming released";
  TfpOpsSnapshot s, current, final;
  BodyApi api, fresh;
  none(why);
  if (!normal(b, expected, &s, why, CHANGED, UNSAFE)) return TFP_OPS_FALSE;
  if (!body_api(b, s.base, &api)) return say(why, TFP_OPS_FALSE, b->why);
  if (!normal(b, expected, &current, why, CHANGED, UNSAFE)) return TFP_OPS_FALSE;
  if (!body_api(b, current.base, &fresh)) return say(why, TFP_OPS_FALSE, b->why);
  if (!same_body_api(&api, &fresh)) return say(why, TFP_OPS_FALSE, "Unit API changed during body event preparation");
  if (!normal(b, expected, &final, why, CHANGED, UNSAFE)) return TFP_OPS_FALSE;
  memset(token, 0, sizeof *token);
  token->owner = b, token->kind = TFP_OPS_KIND_BODY, token->issued = true, token->identity = final;
  token->api.executable = fresh.executable, token->api.table = fresh.table, token->api.query = fresh.query;
  token->api.trigger = fresh.trigger, token->api.event = fresh.event;
  b->has_tokens[TFP_OPS_KIND_BODY] = true;
  return TFP_OPS_TRUE;
}
static bool owned_camera(TfpOps *b, const TfpOpsLease *lease, TfpOpsSnapshot *s, TfpOpsText *why) { /* owned(), :1282 */
  if (!take(b, TAKE_FULL, s)) {
    put(why, "%s", b->why);
    return false;
  }
  if (!tfp_ops_same_identity(s, &lease->origin) || s->camera_kind != 6 || s->record != lease->request) {
    put(why, "Owned native camera request changed before body event");
    return false;
  }
  if (!s->safe || s->aiming || s->controller_active) {
    put(why, "Paired exit body event requires a safe owned camera with real aiming released");
    return false;
  }
  return true;
}
TfpOpsResult tfp_ops_release_body_pose(TfpOps *b, TfpOpsLease *lease, TfpOpsToken *token, TfpOpsText *why) {
  TfpOpsSnapshot s, current, after;
  TfpOpsToken *prepared = NULL;
  BodyApi api, fresh;
  bool exists = false;
  none(why);
  if (!lease) return say(why, TFP_OPS_FALSE, "Invalid camera lease");
  if (lease->body_pose_released) return say(why, TFP_OPS_TRUE, "Paired exit body event already dispatched");
  if (lease->body_pose_attempted) return say(why, TFP_OPS_FALSE, "Paired exit body event dispatch was already attempted");
  if (lease->done) return say(why, TFP_OPS_FALSE, "Camera lease has retired");
  if (!owned_camera(b, lease, &s, why)) return TFP_OPS_FALSE;
  if (token) {
    prepared = issued(b, token, TFP_OPS_KIND_BODY);
    if (!prepared || prepared->used || !tfp_ops_same_identity(&prepared->identity, &s) || prepared->identity.body != s.body)
      return say(why, TFP_OPS_FALSE, "Body event preparation token is invalid, consumed, or owned by another player");
  }
  if (!body_api(b, s.base, &api)) return say(why, TFP_OPS_FALSE, b->why);
  if (prepared) {
    BodyApi kept = {prepared->api.executable, prepared->api.table, prepared->api.query, prepared->api.trigger, prepared->api.event};
    if (!same_body_api(&api, &kept)) return say(why, TFP_OPS_FALSE, "Prepared Unit API changed before body event");
  }
  if (!invoke_body(b, false, &s, &api, &exists)) return say(why, TFP_OPS_FALSE, b->why);
  if (!exists) return say(why, TFP_OPS_FALSE, "Paired exit body event is unavailable on the local body unit");
  if (!owned_camera(b, lease, &current, why)) return TFP_OPS_FALSE;
  if (!body_api(b, current.base, &fresh)) return say(why, TFP_OPS_FALSE, b->why);
  if (!same_body_api(&fresh, &api)) return say(why, TFP_OPS_FALSE, "Unit API changed during body event preflight");
  lease->body_pose_attempted = true;
  if (prepared) prepared->used = true;
  bool called = invoke_body(b, true, &current, &fresh, NULL);
  char error[sizeof b->why];
  memcpy(error, b->why, sizeof error);
  if (take(b, TAKE_FULL, &after) && tfp_ops_same_identity(&after, &current)) {
    lease->body_event_aim_changed = flag(after.aiming != current.aiming);
    lease->body_event_controller_changed = flag(memcmp(after.controller_bytes, current.controller_bytes, 6) != 0);
  }
  if (!called) {
    put(why, "Paired exit body event dispatch uncertain: %s", error);
    return TFP_OPS_FALSE;
  }
  lease->body_pose_released = true;
  if (b->host->log) b->host->log(b->host->user, "Paired exit body event dispatched; its pose effect requires observation");
  return say(why, TFP_OPS_TRUE, "Paired exit body event dispatched");
}

/* ---- keep_aim_view, detach_seated_camera (:1340, :1372) ------------------------------------ */
static bool store(TfpOps *b, uint64_t at, const void *bytes, size_t size) {
  if (!b->host->write || !b->host->write(b->host->user, at, bytes, size)) return fail(b, "Memory write unavailable");
  return true;
}
TfpOpsResult tfp_ops_keep_aim_view(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsText *why) {
  static const uint8_t zero[4] = {0, 0, 0, 0};
  uint8_t bytes[6];
  uint32_t mask;
  none(why);
  if (!b->profile.has_camera || !b->profile.keep_aim_view) return TFP_OPS_FALSE;
  if (!s || !s->safe || s->camera_kind != 6 || !s->controller_active || s->seated) return TFP_OPS_FALSE;
  memcpy(&mask, s->controller_bytes, 4);
  if (mask != 0 || s->controller_bytes[5] != 0) return TFP_OPS_FALSE;
  if (!rd(b, s->controller, bytes, 6)) return raised(b, why);
  if (memcmp(bytes, s->controller_bytes, 6)) return TFP_OPS_FALSE;
  if (!store(b, s->controller + L_CONTROLLER_ACTIVE, zero, 1)) return raised(b, why);
  if (s->request_local && !store(b, s->record + L_REQUEST_SIGHT, zero, 4)) return raised(b, why);
  b->aim_view_keeps++;
  return TFP_OPS_TRUE;
}
TfpOpsResult tfp_ops_detach_seated_camera(TfpOps *b, TfpOpsText *why) {
  TfpOpsSnapshot s;
  uint64_t graph = 0, root = 0, api = 0, address = 0, world = 0;
  uint32_t unit = 0;
  uint8_t parent[8];
  static const uint8_t none_linked[8] = {0};
  none(why);
  if (!b->profile.has_camera || !b->profile.seated_head) return TFP_OPS_FALSE;
  if (!take(b, TAKE_PLAYER, &s) || s.camera_kind != 4 || !s.seated || !s.request_local) return TFP_OPS_FALSE;
  if (!ptr(b, s.camera + L_CAMERA_GRAPH, &graph) || !rd(b, graph - L_UNIT_GRAPH + L_UNIT_PARENT, parent, 8)) return raised(b, why);
  if (!memcmp(parent, none_linked, 8)) return TFP_OPS_FALSE;
  if (!ptr(b, s.base + B_GAME_API_ROOT, &root) || !ptr(b, root + L_API_CAMERA_LINK, &api) || !ptr(b, api + L_API_UNLINK, &address) ||
      !ptr(b, s.context + L_CAMERA_WORLD, &world) || !u32(b, s.context + L_CAMERA_UNIT, &unit)) return raised(b, why);
  if (!executable_page(b, address, 0, false)) return say(why, TFP_OPS_RAISED, "Camera detach function is not in an executable page");
  if (!native(b, TFP_OPS_DETACH, address, world, unit, 0, 0, NULL)) return raised(b, why);
  b->seated_detaches++;
  return TFP_OPS_TRUE;
}

/* ---- release (:1397) ----------------------------------------------------------------------- */
#define ORPHAN_POLLS 4
static bool still_ours(const TfpOpsSnapshot *after, uint64_t context, uint32_t avatar, uint32_t weapon) {
  return after->context == context && after->camera_kind == 6 && after->request_avatar == avatar && after->request_weapon == weapon;
}
/* Not in retail (backend_ops.h tfp_ops_set_hold). The top request is the lease's own record, kind 6, for the lease's
 * avatar; a full snapshot cannot be had for it. renamed: its weapon field is no longer the lease's (the full
 * snapshot refuses a request that names another weapon). Not renamed: the request still names the lease's weapon,
 * but an action flag is set and the wield row has moved on, so neither the full nor the retiring snapshot is there
 * (a swap whose hold ended when the gun left the rig); retail's orphan path waits ORPHAN_POLLS polls for this and
 * the game moves its camera 0.6 m in them (2026-11-22). The proof is the orphan path's: the player's snapshot,
 * the same player and context, and the game's own exit, which takes the context alone. A request the player's own
 * aim took is left to the game, as in retail. A player that cannot be proven is asked again ORPHAN_POLLS times
 * and then given up, with a line, so that a cleanup cannot wait for ever on an avatar that is gone. */
static TfpOpsResult release_held(TfpOps *b, TfpOpsLease *lease, bool renamed, TfpOpsText *why) {
  const TfpOpsSnapshot *origin = &lease->origin;
  TfpOpsSnapshot player, after;
  uint32_t mask = 0;
  bool proven = take(b, TAKE_PLAYER, &player) && tfp_ops_same_player(&player, origin) && player.base == origin->base &&
                player.context == origin->context && player.camera_kind == 6 && player.record == lease->request;
  if (proven) memcpy(&mask, player.controller_bytes, 4);
  if (!proven || mask == 64) {
    char problem[sizeof b->why];
    snprintf(problem, sizeof problem, "%s", proven ? "the action does not allow the exit yet" : b->why[0] ? b->why : "the local player changed");
    if (!proven && ++lease->orphan_polls >= ORPHAN_POLLS) {
      lease->done = true;
      if (b->host->log) b->host->log(b->host->user, "Hold: lease ended without a native exit: its request is still on top but the local player is not proven");
      return say(why, TFP_OPS_TRUE, "Native camera request was replaced");
    }
    put(why, "Waiting for the held native request: %s", problem);
    return TFP_OPS_FALSE;
  }
  lease->orphan_polls = 0;
  if ((player.aiming || player.controller_active) && lease->entry_aim_changed != 1 && lease->entry_controller_changed != 1 &&
      lease->body_event_aim_changed != 1 && lease->body_event_controller_changed != 1) {
    lease->done = true;
    return say(why, TFP_OPS_TRUE, "Player aiming took ownership");
  }
  if (!invoke(b, TFP_OPS_EXIT, &player, false, NULL)) {
    put(why, "Native exit pending: %s", b->why);
    return TFP_OPS_FALSE;
  }
  if (!top_of(b, SCOPE_FULL, &after)) return say(why, TFP_OPS_FALSE, "Native exit readback pending");
  if (after.context == player.context && after.camera_kind == 6 && after.request_avatar == origin->avatar && after.record == lease->request)
    return say(why, TFP_OPS_FALSE, "Native ADS request still present");
  lease->done = true;
  if (b->host->log)
    b->host->log(b->host->user, renamed ? "Hold: owned native camera exit acknowledged; the game had put another weapon into the request"
                                        : "Hold: owned native camera exit acknowledged at once, on the player's snapshot (no full snapshot during the action)");
  return say(why, TFP_OPS_TRUE, "Native camera exited");
}
TfpOpsResult tfp_ops_release(TfpOps *b, TfpOpsLease *lease, TfpOpsText *why) {
  TfpOpsSnapshot top_now, s, after;
  char problem[sizeof b->why];
  none(why);
  if (lease && lease->bootstrap) return tfp_ops_warm_finish(b, lease, why);
  if (!lease) return say(why, TFP_OPS_FALSE, "Invalid camera lease");
  if (!stop_alignment(lease, why)) return TFP_OPS_FALSE;
  if (lease->done) return say(why, TFP_OPS_TRUE, "Already clean");
  const TfpOpsSnapshot *origin = &lease->origin;
  if (!top_of(b, SCOPE_FULL, &top_now)) {
    put(why, "Waiting to verify native camera cleanup: %s", b->why);
    return TFP_OPS_FALSE;
  }
  if (top_now.base != origin->base || top_now.context != origin->context || top_now.camera != origin->camera) {
    lease->done = true;
    return say(why, TFP_OPS_TRUE, "Engine replaced camera context");
  }
  if (top_now.camera_kind != 6 || top_now.request_avatar != origin->avatar || top_now.request_weapon != origin->weapon ||
      top_now.record != lease->request) {
    if (b->hold) { /* not in retail; b->hold false: the three lines below, as they were */
      if (top_now.camera_kind == 6 && top_now.request_avatar == origin->avatar && top_now.record == lease->request)
        return release_held(b, lease, true, why);
      if (b->host->log) b->host->log(b->host->user, "Hold: lease ended without a native exit: the top camera request is no longer the lease's");
    }
    lease->done = true;
    return say(why, TFP_OPS_TRUE, "Native camera request was replaced");
  }
  bool have = take(b, TAKE_FULL, &s);
  if (!have) { /* the retiring form; when it fails too, its text is the one kept (:1417-1418) */
    have = snapshot(b, false, origin, false, &s);
    if (!have) memcpy(problem, b->why, sizeof problem);
  }
  if (!have && b->hold) return release_held(b, lease, false, why); /* not in retail; b->hold false: the orphan path below, as it was */
  if (!have) { /* the orphan path, :1420 */
    TfpOpsSnapshot player;
    uint32_t mask = 0;
    bool taken_player = take(b, TAKE_PLAYER, &player);
    if (taken_player) memcpy(&mask, player.controller_bytes, 4);
    if (taken_player && tfp_ops_same_player(&player, origin) && player.base == origin->base && player.context == origin->context &&
        player.camera_kind == 6 && !player.controller_active && mask != 64) {
      lease->orphan_polls++;
      if (lease->orphan_polls >= ORPHAN_POLLS) {
        if (!invoke(b, TFP_OPS_EXIT, &player, false, NULL)) {
          put(why, "Native exit pending: %s", b->why);
          return TFP_OPS_FALSE;
        }
        if (!top_of(b, SCOPE_FULL, &after)) return say(why, TFP_OPS_FALSE, "Native exit readback pending");
        if (still_ours(&after, player.context, origin->avatar, origin->weapon)) return say(why, TFP_OPS_FALSE, "Native ADS request still present");
        lease->done = true;
        if (b->host->log) b->host->log(b->host->user, "Owned native camera exit acknowledged after its weapon was destroyed");
        return say(why, TFP_OPS_TRUE, "Native camera exited");
      }
    } else lease->orphan_polls = 0;
    put(why, "Waiting for owned native request to retire: %s", problem);
    return TFP_OPS_FALSE;
  }
  lease->orphan_polls = 0;
  if (!tfp_ops_same_identity(&s, origin) || s.record != lease->request)
    return say(why, TFP_OPS_FALSE, "Owner changed; waiting for native request retirement");
  if ((s.aiming || s.controller_active) && lease->entry_aim_changed != 1 && lease->entry_controller_changed != 1 &&
      lease->body_event_aim_changed != 1 && lease->body_event_controller_changed != 1) {
    lease->done = true;
    return say(why, TFP_OPS_TRUE, "Player aiming took ownership");
  }
  if (!invoke(b, TFP_OPS_EXIT, &s, false, NULL)) {
    put(why, "Native exit pending: %s", b->why);
    return TFP_OPS_FALSE;
  }
  if (!top_of(b, SCOPE_FULL, &after)) return say(why, TFP_OPS_FALSE, "Native exit readback pending");
  if (still_ours(&after, s.context, s.avatar, s.weapon)) return say(why, TFP_OPS_FALSE, "Native ADS request still present");
  lease->done = true;
  if (b->host->log) b->host->log(b->host->user, "Owned native camera exit acknowledged");
  return say(why, TFP_OPS_TRUE, "Native camera exited");
}

/* ---- renderer_dof_value (:731) ------------------------------------------------------------- */
static bool renderer_value(TfpOps *b, TfpOpsBinding getter, TfpOpsBinding setter, TfpOpsSetting setting, bool *value) {
  static const uint32_t KEYS[2] = {UINT32_C(0x06f1d0d3), UINT32_C(0x72e8f18c)};
  uint64_t base = 0, table_pointer = 0, rows, again = 0;
  uint8_t header[16], row[24], check[24];
  uint32_t count, key, index = 0, found_key, type;
  bool have = false, equal = false;
  if (setting == TFP_OPS_SETTING_OTHER) return fail(b, "Unsupported renderer Boolean setting");
  int which = setting == TFP_OPS_SETTING_VIGNETTE;
  key = KEYS[which];
  if (!getter.is_function || !setter.is_function) return fail(b, "Renderer setting bindings unavailable");
  if (!executable(b, EXE_POSE, &base)) return false;
  if (getter.address != base + B_EXE_RENDER_GET || setter.address != base + B_EXE_RENDER_SET) return fail(b, "Renderer setting bindings changed");
  if (!b->renderer.has || b->renderer.base != base || b->renderer.get != getter.identity || b->renderer.set != setter.identity) {
    static const struct { uint32_t rva; const char *hex; } pins[4] = {{B_EXE_RENDER_GET, B_HEX_RENDER_GET}, {B_EXE_RENDER_SET, B_HEX_RENDER_SET},
                                                                      {B_EXE_RENDER_LOAD, B_HEX_RENDER_LOAD}, {B_EXE_RENDER_TYPE, B_HEX_RENDER_TYPE}};
    for (int i = 0; i < 4; i++) {
      if (!pinned(b, base + pins[i].rva, pins[i].hex, &equal)) return false;
      if (!equal) return fail(b, "Renderer setting observer contract changed");
    }
    memset(&b->renderer, 0, sizeof b->renderer);
    b->renderer.has = true, b->renderer.base = base, b->renderer.get = getter.identity, b->renderer.set = setter.identity;
  }
  if (!ptr(b, base + B_EXE_RENDER_TABLE, &table_pointer) || !rd(b, table_pointer, header, 16)) return false;
  memcpy(&count, header, 4);
  memcpy(&rows, header + 8, 8);
  if (!count || count > 4096 || rows < LOW || rows + (uint64_t)count * 24 >= HIGH || rows >= HIGH)
    return fail(b, "Renderer setting table unavailable");
  if (!b->renderer.has_table || b->renderer.table_pointer != table_pointer || memcmp(b->renderer.header, header, 16))
    b->renderer.has_index[0] = b->renderer.has_index[1] = false;
  b->renderer.has_indices = true;
  if (b->renderer.has_index[which]) {
    index = b->renderer.index[which];
    if (!rd(b, rows + (uint64_t)index * 24, row, 24)) return false;
    have = true;
  } else {
    int64_t low = 0, high = (int64_t)count - 1;
    while (low <= high) {
      int64_t middle = (low + high) / 2;
      if (!rd(b, rows + (uint64_t)middle * 24, check, 24)) return false;
      memcpy(&found_key, check + 16, 4);
      if (found_key == key) {
        index = (uint32_t)middle, have = true;
        memcpy(row, check, 24);
        break;
      }
      if (found_key < key) low = middle + 1;
      else high = middle - 1;
    }
  }
  if (have) {
    memcpy(&found_key, row + 16, 4);
    memcpy(&type, row, 4);
  }
  if (!have || index >= count || found_key != key || type != 4) return fail(b, "Renderer Boolean row changed");
  if (row[8] > 1) return fail(b, "Renderer DoF Boolean unavailable");
  if (!rd(b, rows + (uint64_t)index * 24, check, 24)) return false;
  if (memcmp(check, row, 24)) return fail(b, "Renderer DoF table changed during observation");
  if (!rd(b, table_pointer, check, 16)) return false;
  if (memcmp(check, header, 16)) return fail(b, "Renderer DoF table changed during observation");
  if (!ptr(b, base + B_EXE_RENDER_TABLE, &again)) return false;
  if (again != table_pointer) return fail(b, "Renderer DoF table changed during observation");
  b->renderer.has_table = true, b->renderer.table_pointer = table_pointer;
  memcpy(b->renderer.header, header, 16);
  b->renderer.has_index[which] = true, b->renderer.index[which] = index;
  *value = row[8] == 1;
  return true;
}
TfpOpsResult tfp_ops_renderer_dof_value(TfpOps *b, TfpOpsBinding getter, TfpOpsBinding setter, TfpOpsSetting setting, TfpOpsText *why) {
  bool value = false;
  none(why);
  if (renderer_value(b, getter, setter, setting, &value)) return value ? TFP_OPS_TRUE : TFP_OPS_FALSE;
  if (b->renderer.has) b->renderer.has_indices = false, b->renderer.has_index[0] = b->renderer.has_index[1] = false;
  return say(why, TFP_OPS_NIL, b->why);
}

/* ---- the readers other modules use --------------------------------------------------------- */
static bool told(TfpOps *b, bool ok, TfpOpsText *why) {
  none(why);
  if (!ok) put(why, "%s", b->why);
  return ok;
}
bool tfp_ops_read(TfpOps *b, uint64_t at, void *out, size_t size, TfpOpsText *why) { return told(b, rd(b, at, out, size), why); }
bool tfp_ops_u8(TfpOps *b, uint64_t at, uint8_t *out, TfpOpsText *why) { return told(b, u8(b, at, out), why); }
bool tfp_ops_map(TfpOps *b, uint64_t at, uint32_t id, uint32_t limit, bool has_limit, uint32_t *index, TfpOpsText *why) {
  return told(b, map(b, at, id, has_limit ? limit : 4096, index), why);
}
bool tfp_ops_descriptor(TfpOps *b, uint64_t world, uint32_t id, uint8_t out[24], TfpOpsText *why) {
  return told(b, descriptor(b, world, id, out), why);
}
bool tfp_ops_module(TfpOps *b, TfpOpsScope scope, uint64_t *base, TfpOpsText *why) { return told(b, module(b, scope, base), why); }
bool tfp_ops_alignment_executable(TfpOps *b, TfpOpsExecutable profile, uint64_t *base, TfpOpsText *why) {
  return told(b, executable(b, profile, base), why);
}
void tfp_ops_invalidate_frame_checks(TfpOps *b) { invalidate(b); }
bool tfp_ops_u32(TfpOps *b, uint64_t at, uint32_t *out, TfpOpsText *why) { return told(b, u32(b, at, out), why); }
bool tfp_ops_ptr(TfpOps *b, uint64_t at, uint64_t *out, TfpOpsText *why) { return told(b, ptr(b, at, out), why); }
bool tfp_ops_alignment_context(TfpOps *b, uint32_t unit, bool has_owner, uint64_t owner, bool has_avatar_index, uint32_t avatar_index,
                               bool profile_pose, TfpOpsContext *out, TfpOpsText *why) {
  return told(b, alignment_context(b, unit, has_owner, owner, has_avatar_index, avatar_index, profile_pose ? EXE_POSE : EXE_ALIGNMENT, out), why);
}
bool tfp_ops_pose_context(TfpOps *b, const TfpOpsSnapshot *s, TfpOpsContext *out, TfpOpsText *why) {
  return told(b, pose_context(b, s, out), why);
}
