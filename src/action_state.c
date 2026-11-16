/* See action_state.h. Line references are to the retail files
 * src/first_person/action_state.lua and backend.lua at 0.5.41-smooth.66.
 * The order of reads and checks and the error texts are retail's.
 *
 * ======================================================================
 * Build constants, game build 25480438. Each line names the retail value and
 * how this build's value was established.
 *   build_map = tools/build_map.py --retail <kit> --to-here (the kit's own
 *               migration records; docs/BUILD-DATA-25480438.md lists them)
 *   image     = bytes read from this build's executable image
 *               (native-work/evidence/exe-25480438.bin: PE stamp 0x6ab382e4,
 *               image size 0x39e8000)
 *   layout    = structure layout, the same number in retail; the named file of
 *               this repository reads the same field on this build
 * ====================================================================== */
#include "action_state.h"
#include "profile.h"
#include <stdio.h>
#include <string.h>

/* -- executable identity (backend.lua:688-725, profile.lua alignment.executable and body_pose.executable) -- */
#define B_EXE_STAMP           FP_EXE_STAMP          /* 0x6ab382e4; retail 0x6af2d5e6; profile.h; image PE header */
#define B_EXE_SIZE            FP_EXE_SIZE           /* 0x39e8000;  retail 0x29fe000;  profile.h; image PE header */
#define B_EXE_REGISTRY        FP_REGISTRY           /* 0x1a100f0;  retail unit_registry 0x1ab76f8; build_map; profile.h; the same
                                                       VALUE as camera_helper.c B_EXE_REGISTRY */
#define B_EXE_EXISTS          UINT32_C(0x201710)    /* retail body_pose.executable.exists 0x1f75d0; build_map */
#define B_HEX_EXISTS          "48895c2408574883ec208bfae89fc1e9ff488bc8488bd84c8b0041ff90b00100"
                                                    /* 32 bytes as retail; retail call displacement ef90eaff, here 9fc1e9ff; image at exe+0x201710 */
#define B_EXE_TRIGGER         UINT32_C(0x201900)    /* retail body_pose.executable.trigger 0x1f77c0; build_map */
#define B_HEX_TRIGGER         "48895c2408574883ec208bda8bf9e8adbfe9ff448bc38bd7488b4818488b5c24"
                                                    /* 32 bytes as retail; retail call displacement fd8eeaff, here adbfe9ff; image at exe+0x201900 */
/* -- animation getters (action_state.lua:6-7, 54-57) -- */
#define B_EXE_ANIM_GETTER     UINT32_C(0x2bd9c0)    /* retail 0x28d510; build_map; native_contract.h anim_getter; the same VALUE as
                                                       camera_helper.c B_EXE_ANIM_GETTER */
#define B_EXE_RESOURCE_GETTER UINT32_C(0x2bd9d0)    /* retail 0x28d520; build_map; image (bytes below) */
static const uint8_t ANIM_GETTER[8] = {0x48, 0x8b, 0x81, 0x78, 0x01, 0x00, 0x00, 0xc3};
                                                    /* retail the same; image at exe+0x2bd9c0; = camera_helper.c B_HEX_ANIM_GETTER */
static const uint8_t RESOURCE_GETTER[14] = {0x48, 0x8b, 0x81, 0x58, 0x03, 0x00, 0x00, 0x48, 0x89, 0x02, 0x48, 0x8b, 0xc2, 0xc3};
                                                    /* retail the same; image at exe+0x2bd9d0 */
/* -- structure layout shared by both builds -- */
#define L_REGISTRY_BLOCK      0x88   /* layout; backend.c fp_unit_object (units +0x88, capacity +0x98, generations +0xa0);
                                        = camera_helper.c L_REGISTRY_BLOCK */
#define L_VT_ANIM_GETTER      0x1B0  /* layout; action.c; = camera_helper.c L_VT_ANIM_GETTER. image: the one table that holds
                                        exe+0x2bd9c0 has it at +0x1B0, with exe+0x2bd870 (graph getter) at +0xE8 */
#define L_VT_RESOURCE_GETTER  0x1B8  /* layout; image: the same table holds exe+0x2bd9d0 at +0x1B8 */
#define L_UNIT_ANIM           0x178  /* layout; action.c; = camera_helper.c L_UNIT_ANIM; the getter's own code reads [rcx+0x178] */
#define L_UNIT_RESOURCE       0x358  /* layout; action.c, backend.c; = camera_helper.c L_UNIT_RESOURCE; the getter reads [rcx+0x358] */
#define L_ANIM_GRAPH          0x28   /* layout; action.c; = camera_helper.c L_ANIM_GRAPH */
#define L_ANIM_ACTIVE         0x50   /* layout; action.c; = camera_helper.c L_ANIM_ACTIVE */
/* graph header: +4 layer count, +8 layer table offset; layer table +4: an offset per layer; layer definition +8: state
 * count, +12: an offset per state. layout; action.c; = camera_helper.c L_ANIM_LAYER_COUNT, L_ANIM_TABLE and its on_foot proof. */
static const uint8_t EXPECTED_RESOURCE[8] = {0x97, 0xfa, 0x4d, 0x29, 0x4d, 0x33, 0x1c, 0x4d}; /* 4D1C334D294DFA97, :5; same in both builds
                                                       (camera_helper.c L_BODY_RESOURCE) */

#define TFP_LOW UINT64_C(65536)
#define TFP_HIGH UINT64_C(0x800000000000)

const char tfp_action_state_sprint_evidence[] = "historical ActionCam state mapping; current action unverified"; /* :122 */

typedef struct Reader { const FpMemory *m; char why[TFP_ACTION_STATE_ERROR]; } Reader;

static bool refuse(Reader *r, const char *text) {
  if (!*r->why) snprintf(r->why, sizeof r->why, "%s", text);
  return false;
}
static bool bytes(Reader *r, uintptr_t at, void *out, size_t size) { /* backend:_read, backend.lua:65-79 */
  if ((uint64_t)at < TFP_LOW || !size || size > 1024 || (uint64_t)at + size >= TFP_HIGH) return refuse(r, "Invalid read");
  return fp_read(r->m, at, out, size) || refuse(r, "Memory unavailable");
}
static bool u8(Reader *r, uintptr_t at, uint8_t *out) { return bytes(r, at, out, 1); }   /* _u8 */
static bool u32(Reader *r, uintptr_t at, uint32_t *out) { return bytes(r, at, out, 4); } /* _u32 */
/* string.format('%x', n) of a Lua number, as LuaJIT prints it: the pointer value has gone through a double
 * (uint(b)+uint(b,4)*4294967296), so above 2^53 it is the nearest double, and 2^64 itself prints as 0. */
static unsigned long long lua_hex(uint64_t value) {
  double n = (double)(uint32_t)value + (double)(uint32_t)(value >> 32) * 4294967296.0;
  return (unsigned long long)(n < 9223372036854775808.0 ? (int64_t)n : (int64_t)(n - 18446744073709551616.0));
}
static bool ptr(Reader *r, uintptr_t at, uintptr_t *out) { /* _ptr, backend.lua:82-85 */
  uint64_t value = 0;
  if (!bytes(r, at, &value, 8)) return false;
  if (value < TFP_LOW || value >= TFP_HIGH) {
    if (!*r->why)
      snprintf(r->why, sizeof r->why, "Pointer unavailable at0x%llx (value0x%llx)", (unsigned long long)at, lua_hex(value));
    return false;
  }
  *out = (uintptr_t)value;
  return true;
}
static uint32_t word(const uint8_t *b, size_t offset) { /* uint(bytes, offset), :8-13 */
  return (uint32_t)b[offset] | (uint32_t)b[offset + 1] << 8 | (uint32_t)b[offset + 2] << 16 | (uint32_t)b[offset + 3] << 24;
}
static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; }
static bool fingerprint(Reader *r, uintptr_t at, const char *hex) {
  uint8_t actual[64];
  size_t n = strlen(hex) / 2;
  if (!bytes(r, at, actual, n)) return false;
  for (size_t i = 0; i < n; i++)
    if (actual[i] != (uint8_t)(hex_digit(hex[i * 2]) * 16 + hex_digit(hex[i * 2 + 1])))
      return refuse(r, "Alignment executable fingerprint mismatch");
  return true;
}
static bool valid_id(uint32_t id) { return id > 0 && id < UINT32_MAX; } /* backend.lua:20 */

/* backend:_alignment_executable, backend.lua:688-725. Retail remembers a passed check for the rest of the host frame;
 * here every call checks. */
static bool executable(Reader *r, uintptr_t *exe) {
  uintptr_t base = r->m->exe;
  uint8_t mz[2], h[96];
  uint32_t pe = 0;
  if ((uint64_t)base < TFP_LOW) return refuse(r, "Game executable unavailable");
  if (!bytes(r, base, mz, 2)) return false;
  if (mz[0] != 'M' || mz[1] != 'Z') return refuse(r, "Invalid game executable");
  if (!u32(r, base + 0x3c, &pe)) return false;
  if (pe < 0x40 || pe > 0x1000) return refuse(r, "Invalid executable PE header");
  if (!bytes(r, base + pe, h, sizeof h)) return false;
  if (memcmp(h, "PE\0\0", 4) || h[4] != 0x64 || h[5] != 0x86 || h[24] != 0x0b || h[25] != 0x02 || word(h, 8) != B_EXE_STAMP ||
      word(h, 80) != B_EXE_SIZE)
    return refuse(r, "Unsupported executable build");
  if (!fingerprint(r, base + B_EXE_EXISTS, B_HEX_EXISTS) || !fingerprint(r, base + B_EXE_TRIGGER, B_HEX_TRIGGER)) return false;
  *exe = base;
  return true;
}
/* backend:_alignment_context, backend.lua:783-803. */
static bool context(Reader *r, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index, TfpAlignmentContext *c) {
  uintptr_t exe = 0, registry = 0, object = 0, again = 0;
  uint8_t metadata[32], generation = 0;
  uint64_t generations = 0, units = 0;
  if (!valid_id(viewmodel)) return refuse(r, "Native first-person viewmodel unavailable");
  if (!executable(r, &exe) || !ptr(r, exe + B_EXE_REGISTRY, &registry)) return false;
  uint32_t index = viewmodel % 0x400000, wanted = viewmodel / 0x400000 % 256;
  if (!bytes(r, registry + L_REGISTRY_BLOCK, metadata, sizeof metadata)) return false;
  uint32_t capacity = word(metadata, 16);
  if (!(capacity > 0 && capacity <= 0x400000 && index < capacity)) return refuse(r, "Viewmodel registry index unavailable");
  memcpy(&generations, metadata + 24, 8);
  memcpy(&units, metadata, 8);
  if (!(generations >= TFP_LOW && generations < TFP_HIGH && units >= TFP_LOW && units < TFP_HIGH)) return refuse(r, "Pointer unavailable");
  if (!u8(r, (uintptr_t)generations + index, &generation)) return false;
  if (generation != wanted) return refuse(r, "Viewmodel generation retired");
  if (!ptr(r, (uintptr_t)units + (uintptr_t)index * 8, &object)) return false;
  if (!u8(r, (uintptr_t)generations + index, &generation)) return false;
  if (generation != wanted) return refuse(r, "Viewmodel generation changed during lookup");
  if (!ptr(r, (uintptr_t)units + (uintptr_t)index * 8, &again)) return false;
  if (again != object) return refuse(r, "Viewmodel generation changed during lookup");
  c->exe = exe; c->unit = viewmodel; c->index = index; c->generation = wanted; c->object = object;
  c->registry = registry; c->units = (uintptr_t)units; c->generations = (uintptr_t)generations;
  c->owner = avatar_owner; c->avatar_index = avatar_index;
  return true;
}
static void report(const Reader *r, char *error, size_t error_size) {
  if (error && error_size) snprintf(error, error_size, "%s", *r->why ? r->why : "Memory unavailable");
}
bool tfp_alignment_executable(const FpMemory *memory, uintptr_t *exe, char *error, size_t error_size) {
  Reader r = {memory, ""};
  uintptr_t base = 0;
  if (!executable(&r, &base)) { report(&r, error, error_size); return false; }
  if (exe) *exe = base;
  return true;
}
bool tfp_alignment_context(const FpMemory *memory, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index,
                           TfpAlignmentContext *out, char *error, size_t error_size) {
  Reader r = {memory, ""};
  TfpAlignmentContext c;
  if (!context(&r, viewmodel, avatar_owner, avatar_index, &c)) { report(&r, error, error_size); return false; }
  *out = c;
  return true;
}
static bool own_player_snapshot(void *user, TfpSnapshot *out, char *error, size_t error_size) { /* backend.lua:330-334 */
  return tfp_snapshot((const FpMemory *)user, TFP_SNAPSHOT_PLAYER, out, error, error_size);
}
static bool own_alignment_context(void *user, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index,
                                  TfpAlignmentContext *out, char *error, size_t error_size) {
  return tfp_alignment_context((const FpMemory *)user, viewmodel, avatar_owner, avatar_index, out, error, error_size);
}
void tfp_action_state_backend(TfpActionStateBackend *backend, const FpMemory *memory) {
  backend->memory = memory;
  backend->user = (void *)(uintptr_t)memory;
  backend->player_snapshot = own_player_snapshot;
  backend->alignment_context = own_alignment_context;
}

void tfp_action_state_init(TfpActionStateReader *reader) {
  reader->cached = false; /* cache = nil, :35 */
  for (int layer = 0; layer <= TFP_ACTION_STATE_LAYERS; layer++) reader->map[layer].present = false;
}

static bool offset(Reader *r, uint32_t value) { /* :20-23 */
  return (value >= 12 && value <= 0x1000000) || refuse(r, "Animation definition offset out of bounds");
}
static bool player(const TfpSnapshot *s) { /* :29-32; a TfpSnapshot always carries its identity */
  return s->body > 0 && s->body < UINT32_MAX;
}
static bool same_context(const TfpAlignmentContext *a, const TfpAlignmentContext *b) { /* :107-109, key by key */
  return a->exe == b->exe && a->unit == b->unit && a->index == b->index && a->generation == b->generation && a->object == b->object &&
         a->registry == b->registry && a->units == b->units && a->generations == b->generations && a->owner == b->owner &&
         a->avatar_index == b->avatar_index;
}

bool tfp_action_state_sample(TfpActionStateReader *cache, const TfpActionStateBackend *backend, const TfpSnapshot *expected,
                             const int *layers, size_t layer_count, TfpActionState *out, char *error, size_t error_size) { /* :47-126 */
  static const int standard[3] = {1, 3, 14}; /* :75 */
  Reader r = {backend->memory, ""};
  TfpSnapshot s, after;
  TfpAlignmentContext c, final;
  TfpActionState result;
  char why[TFP_ACTION_STATE_ERROR] = "";
  uintptr_t vt = 0, getter = 0, instance = 0, graph = 0, active = 0, again = 0;
  uint8_t code[14], resource[8], header[12], definitions[TFP_ACTION_STATE_LAYERS * 4], check[TFP_ACTION_STATE_LAYERS * 4];
  struct { uintptr_t at, def; uint8_t raw[8]; uint32_t count; } observed[TFP_ACTION_STATE_LAYERS];
  bool chosen[TFP_ACTION_STATE_LAYERS + 1] = {false};
  bool ok = false;
  memset(&result, 0, sizeof result);
  do {
    if (expected) s = *expected; /* :49-50 */
    else if (!backend->player_snapshot(backend->user, &s, why, sizeof why)) { refuse(&r, *why ? why : "Animation local player unavailable"); break; }
    if (!player(&s)) { refuse(&r, "Animation local player unavailable"); break; } /* :51 */
    if (!backend->alignment_context(backend->user, s.body, s.avatar_owner, s.avatar_index, &c, why, sizeof why)) { refuse(&r, why); break; } /* :52 */
    if (!ptr(&r, c.object, &vt)) break;                                           /* :53 */
    if (!ptr(&r, vt + L_VT_ANIM_GETTER, &getter)) break;                         /* :54-55 */
    if (getter != c.exe + B_EXE_ANIM_GETTER) { refuse(&r, "Animation interface getter changed"); break; }
    if (!bytes(&r, c.exe + B_EXE_ANIM_GETTER, code, sizeof ANIM_GETTER)) break;
    if (memcmp(code, ANIM_GETTER, sizeof ANIM_GETTER)) { refuse(&r, "Animation interface getter changed"); break; }
    if (!ptr(&r, vt + L_VT_RESOURCE_GETTER, &getter)) break;                     /* :56-57 */
    if (getter != c.exe + B_EXE_RESOURCE_GETTER) { refuse(&r, "Animation resource getter changed"); break; }
    if (!bytes(&r, c.exe + B_EXE_RESOURCE_GETTER, code, sizeof RESOURCE_GETTER)) break;
    if (memcmp(code, RESOURCE_GETTER, sizeof RESOURCE_GETTER)) { refuse(&r, "Animation resource getter changed"); break; }
    if (!bytes(&r, c.object + L_UNIT_RESOURCE, resource, 8) || !ptr(&r, c.object + L_UNIT_ANIM, &instance) || /* :58-61 */
        !ptr(&r, instance + L_ANIM_GRAPH, &graph) || !bytes(&r, graph, header, 12)) break;
    uint32_t count = word(header, 4);                                             /* :62-63 */
    if (!(count > 0 && count <= 32)) { refuse(&r, "Animation layer count out of bounds"); break; }
    uint32_t table_offset = word(header, 8);                                      /* :64-65 */
    if (!offset(&r, table_offset)) break;
    if (table_offset > 0x10000) { refuse(&r, "Animation layer table out of bounds"); break; }
    uintptr_t table_at = graph + table_offset;                                    /* :66 */
    if (!bytes(&r, table_at + 4, definitions, (size_t)count * 4) || !ptr(&r, instance + L_ANIM_ACTIVE, &active)) break; /* :67-68 */
    if (!cache->cached || cache->body != c.object || cache->ref != c.unit || cache->generation != c.generation || /* :69-74 */
        cache->instance != instance || cache->graph != graph || memcmp(cache->resource, resource, 8) ||
        memcmp(cache->header, header, 12) || cache->definition_count != count || memcmp(cache->definitions, definitions, (size_t)count * 4)) {
      cache->cached = true;
      cache->body = c.object; cache->ref = c.unit; cache->generation = c.generation; cache->instance = instance; cache->graph = graph;
      memcpy(cache->resource, resource, 8);
      memcpy(cache->header, header, 12);
      memcpy(cache->definitions, definitions, (size_t)count * 4);
      cache->definition_count = count;
      for (int layer = 0; layer <= TFP_ACTION_STATE_LAYERS; layer++) cache->map[layer].present = false;
    }
    if (!layers) { layers = standard; layer_count = 3; }                          /* :75 */
    if (!(layer_count > 0 && layer_count <= 32)) { refuse(&r, "Animation layer selection unavailable"); break; } /* :76 */
    size_t done = 0;
    for (; done < layer_count; done++) {                                          /* :78-101 */
      int layer = layers[done];
      if (!(layer >= 1 && (uint32_t)layer <= count && !chosen[layer])) { refuse(&r, "Animation layer selection out of bounds"); break; }
      chosen[layer] = true;
      uint32_t def_offset = word(definitions, (size_t)(layer - 1) * 4), n = 0;    /* :82 */
      if (!offset(&r, def_offset)) break;
      uintptr_t def = table_at + def_offset;
      if (!u32(&r, def + 8, &n)) break;                                           /* :83 */
      if (!(n > 0 && n <= 2048)) { refuse(&r, "Animation state count out of bounds"); break; }
      if (!cache->map[layer].present || cache->map[layer].def != def || cache->map[layer].count != n) { /* :85-94 */
        uint8_t entries[TFP_ACTION_STATE_STATES * 4];
        uint32_t offsets[TFP_ACTION_STATE_STATES];
        size_t size = (size_t)n * 4, at = 0;
        for (; at < size; at += 1024)                                             /* block(), :37-42 */
          if (!bytes(&r, def + 12 + at, entries + at, size - at < 1024 ? size - at : 1024)) break;
        if (at < size) break;
        uint32_t state = 0;
        for (; state < n; state++) {
          offsets[state] = word(entries, (size_t)state * 4);
          if (!offset(&r, offsets[state])) break;
        }
        if (state < n) break;
        cache->map[layer].present = true; /* the old map stays when a read or an offset above refuses */
        cache->map[layer].def = def;
        cache->map[layer].count = n;
        memcpy(cache->offsets[layer], offsets, size);
      }
      uint64_t p = 0;
      uintptr_t slot = active + (uintptr_t)(layer - 1) * 8;
      if (!bytes(&r, slot, observed[done].raw, 8)) break;                         /* :95 */
      memcpy(&p, observed[done].raw, 8);
      if (p != 0 && !(p >= 65536 && p < TFP_HIGH)) { refuse(&r, "Animation state pointer unavailable"); break; } /* :14-19 */
      int index = -1;                                                             /* :97 */
      if (p != 0) {
        bool found = false;
        for (uint32_t state = 0; state < n && !found; state++)                    /* the first state at that address, :91 */
          if ((uint64_t)def + cache->offsets[layer][state] == p) { index = (int)state; found = true; }
        if (!found) { index = 0; result.unknown_layers[result.unknown_count++] = layer; } /* :98-99 */
      }
      result.states[layer] = index;
      result.has[layer] = true;
      observed[done].at = slot; observed[done].def = def; observed[done].count = n; /* :100 */
    }
    if (done < layer_count) break;
    /* :104-105 */
    *why = 0;
    if (!backend->player_snapshot(backend->user, &after, why, sizeof why)) { refuse(&r, *why ? why : "assertion failed!"); break; }
    if (!player(&after)) { refuse(&r, "assertion failed!"); break; }
    if (!(tfp_same_player(&after, &s) && after.body == s.body)) { refuse(&r, "Animation player changed during read"); break; }
    /* :106-109 */
    if (!backend->alignment_context(backend->user, after.body, after.avatar_owner, after.avatar_index, &final, why, sizeof why)) { refuse(&r, why); break; }
    if (!same_context(&final, &c)) { refuse(&r, "Animation body generation changed during read"); break; }
    /* :110-112, each read only when the one before it agreed */
    static const char changed[] = "Animation graph changed during read";
    if (!ptr(&r, c.object + L_UNIT_ANIM, &again)) break;
    if (again != instance) { refuse(&r, changed); break; }
    if (!bytes(&r, c.object + L_UNIT_RESOURCE, check, 8)) break;
    if (memcmp(check, resource, 8)) { refuse(&r, changed); break; }
    if (!ptr(&r, instance + L_ANIM_GRAPH, &again)) break;
    if (again != graph) { refuse(&r, changed); break; }
    if (!ptr(&r, instance + L_ANIM_ACTIVE, &again)) break;
    if (again != active) { refuse(&r, changed); break; }
    if (!bytes(&r, graph, check, 12)) break;
    if (memcmp(check, header, 12)) { refuse(&r, changed); break; }
    if (!bytes(&r, table_at + 4, check, (size_t)count * 4)) break;
    if (memcmp(check, definitions, (size_t)count * 4)) { refuse(&r, changed); break; }
    size_t entry = 0;
    for (; entry < layer_count; entry++) {                                        /* :113-116 */
      uint32_t n = 0;
      if (!bytes(&r, observed[entry].at, check, 8)) break;
      if (memcmp(check, observed[entry].raw, 8)) { refuse(&r, "Animation layer changed during read"); break; }
      if (!u32(&r, observed[entry].def + 8, &n)) break;
      if (n != observed[entry].count) { refuse(&r, "Animation layer changed during read"); break; }
    }
    if (entry < layer_count) break;
    /* :117-122 */
    result.resource_match = !memcmp(resource, EXPECTED_RESOURCE, 8) && count == 31;
    result.sprint_hypothesis = result.resource_match && ((result.has[1] && (result.states[1] == 78 || result.states[1] == 79)) ||
                                                         (result.has[3] && result.states[3] == 71));
    result.player = s;
    result.body = s.body;
    result.body_object = c.object;
    result.graph = graph;
    result.instance = instance;
    result.layer_count = count;
    for (int i = 0; i < 8; i++) snprintf(result.resource + i * 2, 3, "%02X", resource[7 - i]); /* resource_name, :24-28 */
    ok = true;
  } while (0);
  if (!ok) { /* :124 */
    report(&r, error, error_size);
    return false;
  }
  if (error && error_size) *error = 0;
  *out = result;
  return true;
}
