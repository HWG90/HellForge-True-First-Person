/* Read-only, generation-owned native player/camera discovery. No engine calls,
 * writes, process injection, or UnitRef-to-Unit-handle casts. */
#include "backend.h"
#include "profile.h"
#include <stdio.h>
#include <string.h>
#define FP_LOW UINT64_C(65536)
#define FP_HIGH UINT64_C(0x800000000000)
static bool fail(char *why, size_t size, const char *text) {
  if (why && size)
    snprintf(why, size, "%s", text);
  return false;
}
bool fp_read(const FpMemory *m, uintptr_t at, void *out, size_t size) {
  return m && m->read && out && size > 0 && size <= 8192 && at >= FP_LOW &&
         at < FP_HIGH && size < FP_HIGH - at && m->read(m->user, at, out, size);
}
static bool u32(const FpMemory *m, uintptr_t at, uint32_t *v) {
  return fp_read(m, at, v, 4);
}
static bool ptr(const FpMemory *m, uintptr_t at, uintptr_t *v) {
  uint64_t n = 0;
  if (!fp_read(m, at, &n, 8) || n < FP_LOW || n >= FP_HIGH)
    return false;
  *v = (uintptr_t)n;
  return true;
}
static bool id(uint32_t n);
bool fp_pointer(const FpMemory *m, uintptr_t at, uintptr_t *v) {
  return ptr(m, at, v);
}
bool fp_u32(const FpMemory *m, uintptr_t at, uint32_t *v) {
  return u32(m, at, v);
}
uintptr_t fp_unit_object(const FpMemory *m, uint32_t ref) {
  if (!id(ref))
    return 0;
  uintptr_t registry = 0, units = 0, generations = 0, object = 0, vt = 0,
            alive = 0, visible = 0, again = 0;
  uint32_t capacity = 0, self = 0, index = ref & 0x3fffff;
  uint8_t generation = 0, last = 0;
  if (!ptr(m, m->exe + FP_REGISTRY, &registry) ||
      !u32(m, registry + 0x98, &capacity) || !capacity || capacity > 0x400000 ||
      index >= capacity || !ptr(m, registry + 0xa0, &generations) ||
      !fp_read(m, generations + index, &generation, 1) ||
      generation != (uint8_t)(ref >> 22) || !ptr(m, registry + 0x88, &units) ||
      !ptr(m, units + (uintptr_t)index * 8, &object) || !ptr(m, object, &vt) ||
      !ptr(m, vt + 0xf8, &alive) || alive != m->exe + 0x2bd890 ||
      !ptr(m, vt + 0xc8, &visible) || visible != m->exe + 0x2c3990 ||
      !u32(m, object + 8, &self) || self != ref ||
      !fp_read(m, generations + index, &last, 1) || last != generation ||
      !ptr(m, units + (uintptr_t)index * 8, &again) || again != object)
    return 0;
  return object;
}
static bool id(uint32_t n) { return n != 0 && n != UINT32_MAX; }
bool fp_image(const FpMemory *m, uintptr_t base, uint32_t stamp,
              uint32_t size) {
  uint8_t h[64];
  uint32_t pe = 0, signature = 0, t = 0, n = 0;
  uint16_t magic = 0;
  if (!fp_read(m, base, h, sizeof h) || h[0] != 'M' || h[1] != 'Z')
    return false;
  memcpy(&pe, h + 60, 4);
  return pe >= 64 && pe <= 4096 && u32(m, base + pe, &signature) &&
         signature == 0x4550 && u32(m, base + pe + 8, &t) && t == stamp &&
         fp_read(m, base + pe + 24, &magic, 2) && magic == 0x20b &&
         u32(m, base + pe + 80, &n) && n == size;
}
bool fp_map(const FpMemory *m, uintptr_t at, uint32_t key, uint32_t bound,
            uint32_t *index) {
  struct {
    uint64_t buckets;
    uint32_t cap, empty, mult;
  } h;
  /* Native header is 20 bytes, independent of the host struct's tail padding.
   */
  if (!index || !id(key) || !bound || !fp_read(m, at, &h, 20) || !h.cap ||
      h.cap > 1048576 || (h.cap & (h.cap - 1)) || key == h.empty ||
      h.buckets < FP_LOW || h.buckets >= FP_HIGH)
    return false;
  uint32_t count = h.cap < 4096 ? h.cap : 4096;
  for (uint32_t n = 0; n < count; n++) {
    uint32_t row[2];
    uint32_t slot = (key * h.mult + n) & (h.cap - 1);
    if (!fp_read(m, (uintptr_t)h.buckets + (uintptr_t)slot * 8, row, 8) ||
        row[0] == h.empty)
      return false;
    if (row[0] == key) {
      if (row[1] >= bound)
        return false;
      *index = row[1];
      return true;
    }
  }
  return false;
}
bool fp_same_player(const FpSnapshot *a, const FpSnapshot *b) {
  return a && b && a->context == b->context && a->camera == b->camera &&
         a->world == b->world && a->controller == b->controller &&
         a->player == b->player && a->avatar_owner == b->avatar_owner &&
         a->avatar_index == b->avatar_index &&
         a->avatar_descriptor == b->avatar_descriptor &&
         a->avatar == b->avatar && a->body == b->body &&
         !memcmp(a->descriptor, b->descriptor, 20);
}
bool fp_snapshot(const FpMemory *m, bool need_weapon, FpSnapshot *s, char *why,
                 size_t size) {
  uintptr_t ep = 0, rows = 0, wield = 0, pool = 0;
  uint32_t count = 0, index = 0;
  uint32_t request[5], descriptor[6], wd[6], queue[2];
  TfpEntityFields own;
  const TfpEntityFields *f = NULL;
  if (!s || !m)
    return fail(why, size, "Missing native context");
  memset(s, 0, sizeof *s);
  if (!ptr(m, m->game + FP_CAMERA, &s->context) ||
      !ptr(m, s->context, &s->camera) ||
      !fp_read(m, s->context + 0x1f8, queue, 8) || queue[0] >= 32 ||
      queue[1] >= 32)
    return fail(why, size, "Waiting for native gameplay camera");
  s->head = queue[0];
  s->tail = queue[1];
  s->request =
      s->context +
      (s->head == s->tail ? 0x1f00 : 0x200 + ((s->tail + 31) % 32) * 0xe8);
  if (!fp_read(m, s->request, request, 20) ||
      !ptr(m, m->game + FP_WORLD, &s->world) ||
      !ptr(m, m->game + FP_CONTROLLER, &s->controller) ||
      !fp_read(m, s->controller, s->controls, 6) ||
      !ptr(m, m->game + FP_PLAYER, &s->player) ||
      !(f = tfp_fields_use(m->fields, &own)) ||
      !u32(m, s->player + f->player_live, &count) || count == 0 ||
      !u32(m, s->player + f->player_network, &s->local_entity))
    return fail(why, size, "Waiting for local player");
  s->kind = request[0] & 65535;
  s->request_avatar = request[1];
  s->request_weapon = request[2];
  if (!fp_map(m, s->world + f->network_map, s->local_entity, 1048576, &index) ||
      !fp_read(m, tfp_fields_record(s->world, f, index), descriptor, 24) ||
      descriptor[4] != s->local_entity || !(descriptor[5] & 1) ||
      !id(descriptor[2]))
    return fail(why, size, "Local player descriptor unavailable");
  memcpy(s->descriptor, descriptor, 20);
  s->avatar = descriptor[2];
  if (!ptr(m, m->game + FP_AVATAR, &s->avatar_owner) ||
      !fp_map(m, s->avatar_owner + 0xf8, s->avatar, 8, &s->avatar_index) ||
      !u32(m, s->avatar_owner + 0x6c, &count) || count > 8 ||
      s->avatar_index >= count ||
      !ptr(m, s->avatar_owner + 0x110 + (uintptr_t)s->avatar_index * 8, &ep) ||
      !fp_read(m, ep, descriptor, 20) ||
      memcmp(descriptor, s->descriptor, 20) || !id(descriptor[3]))
    return fail(why, size, "Local avatar ownership changed");
  s->avatar_descriptor = ep;
  s->body = descriptor[3];
  uintptr_t row =
      s->avatar_owner + (uintptr_t)s->avatar_index * FP_AVATAR_STRIDE;
  if (!fp_read(m, row + FP_AVATAR_FLAGS, s->flags, 24) ||
      !u32(m, row + FP_AVATAR_RIG, &s->viewmodel) ||
      !u32(m, s->context + 8, &s->camera_unit))
    return fail(why, size, "Avatar presentation unavailable");
  memcpy(&s->mask, s->controls, 4);
  s->aiming = (s->flags[6] & 1) != 0;
  s->full_ads = s->controls[4] != 0;
  s->shoulder_aim = !s->full_ads && (s->flags[10] & 4) && (s->flags[15] & 32);
  s->reloading = (s->flags[15] & 2) != 0;
  s->seated = (s->flags[14] & 4) != 0;
  s->local_request = s->request_avatar == s->avatar;
  s->safe = s->local_request && (s->kind == 4 || s->kind == 6) && !s->seated &&
            !s->mask && !s->controls[5];
  if (!need_weapon) {
    if (why && size)
      *why = 0;
    return true;
  }
  /* Wield row and world descriptor are re-proved for this equipped weapon. */
  if (!ptr(m, m->game + FP_WIELD, &wield) ||
      !fp_map(m, wield + 0x30, s->avatar, 4096, &index) ||
      !ptr(m, wield + 0x48, &rows) ||
      !ptr(m, rows + (uintptr_t)index * 8, &ep) ||
      !fp_read(m, ep, descriptor, 20) ||
      memcmp(descriptor, s->descriptor, 20) || !ptr(m, wield + 0x60, &rows) ||
      !u32(m, rows + (uintptr_t)index * 0x1d0, &s->weapon) || !id(s->weapon) ||
      !fp_map(m, s->world + f->entity_map, s->weapon, 1048576, &index) ||
      !fp_read(m, tfp_fields_record(s->world, f, index), wd, 24) ||
      wd[2] != s->weapon || !(wd[5] & 1) ||
      !ptr(m, m->game + FP_WEAPONS, &pool) ||
      !fp_map(m, pool + 0x30, s->weapon, 4096, &index) ||
      !ptr(m, pool + 0x48, &rows) ||
      !ptr(m, rows + (uintptr_t)index * 8, &ep) ||
      !fp_read(m, ep, descriptor, 20) || memcmp(descriptor, wd, 20) ||
      !id(descriptor[3]))
    return fail(why, size, "Equipped firearm unavailable");
  s->weapon_descriptor = ep;
  s->weapon_unit = descriptor[3];
  memcpy(s->weapon_bytes, wd, 20);
  if (s->kind == 6 && (s->request_weapon != s->weapon || request[3] != s->body))
    return fail(why, size, "FPS request belongs to another weapon");
  s->weapon_ready = true;
  if (why && size)
    *why = 0;
  return true;
}
bool fp_weapon_record(const FpMemory *m, uintptr_t world, uint32_t entity,
                      FpWeaponRecord *out) {
  if (!out)
    return false;
  memset(out, 0, sizeof *out);
  if (!m || !id(entity))
    return false;
  FpWeaponRecord result = {.world = world, .entity = entity};
  uintptr_t current_world = 0, rows = 0, again = 0;
  uint32_t wi = 0, pi = 0, close_index = 0, descriptor[6], pool_record[5];
  TfpEntityFields own;
  const TfpEntityFields *f = tfp_fields_use(m->fields, &own);
  if (!f || !ptr(m, m->game + FP_WORLD, &current_world) || current_world != world ||
      !fp_map(m, world + f->entity_map, entity, 1048576, &wi) ||
      !fp_read(m, tfp_fields_record(world, f, wi),
               descriptor, sizeof descriptor) ||
      descriptor[2] != entity || !(descriptor[5] & 1) || !id(descriptor[3]) ||
      !ptr(m, m->game + FP_WEAPONS, &result.pool) ||
      !fp_map(m, result.pool + 0x30, entity, 4096, &pi) ||
      !ptr(m, result.pool + 0x48, &rows) ||
      !ptr(m, rows + (uintptr_t)pi * 8, &result.descriptor) ||
      !fp_read(m, result.descriptor, pool_record, sizeof pool_record) ||
      memcmp(pool_record, descriptor, sizeof pool_record))
    return false;
  result.unit = descriptor[3];
  result.object = fp_unit_object(m, result.unit);
  if (!result.object)
    return false;
  memcpy(result.bytes, descriptor, sizeof result.bytes);
  uint32_t closing[6];
  if (!ptr(m, m->game + FP_WORLD, &again) || again != world ||
      !ptr(m, m->game + FP_WEAPONS, &again) || again != result.pool ||
      !fp_map(m, world + f->entity_map, entity, 1048576, &close_index) ||
      close_index != wi ||
      !fp_read(m, tfp_fields_record(world, f, wi),
               closing, sizeof closing) || memcmp(closing, descriptor, sizeof closing) ||
      !fp_map(m, result.pool + 0x30, entity, 4096, &close_index) ||
      close_index != pi || !ptr(m, result.pool + 0x48, &again) || again != rows ||
      !ptr(m, rows + (uintptr_t)pi * 8, &again) || again != result.descriptor ||
      !fp_read(m, result.descriptor, pool_record, sizeof pool_record) ||
      memcmp(pool_record, result.bytes, sizeof result.bytes) ||
      fp_unit_object(m, result.unit) != result.object)
    return false;
  *out = result;
  return true;
}
static int digit(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                : -1;
}
bool fp_bytes(const FpMemory *m, uintptr_t at, const char *hex) {
  uint8_t actual[128];
  size_t n = hex ? strlen(hex) : 0;
  if (!n || (n & 1) || n > sizeof actual * 2 || !fp_read(m, at, actual, n / 2))
    return false;
  for (size_t i = 0; i < n / 2; i++) {
    int a = digit(hex[i * 2]), b = digit(hex[i * 2 + 1]);
    if (a < 0 || b < 0 || actual[i] != (uint8_t)(a * 16 + b))
      return false;
  }
  return true;
}
FpProof fp_prove_profile(const FpMemory *m) {
  FpProof proof = {0};
  for (size_t i = 0; i < FP_WITNESS_COUNT; i++) {
    const FpWitness *w = fp_witnesses + i;
    uint8_t actual[128], expected[128];
    size_t n = strlen(w->hex) / 2;
    if (n > sizeof expected) {
      proof.mismatched++;
      continue;
    }
    for (size_t j = 0; j < n; j++)
      expected[j] =
          (uint8_t)(digit(w->hex[j * 2]) * 16 + digit(w->hex[j * 2 + 1]));
    bool read = fp_read(m, m->game + w->rva, actual, n);
    if (read && !memcmp(actual, expected, n)) {
      proof.matched++;
      continue;
    }
    if (read)
      proof.mismatched++;
    else
      proof.unreadable++;
    if (!*proof.first)
      snprintf(proof.first, sizeof proof.first, "%s: %s", w->name,
               read ? "code differs or remains packed" : "unreadable");
  }
  return proof;
}

bool fp_weapon_key(const FpMemory *m, const FpSnapshot *s, uint64_t *key) {
  if (!s || !key || !s->weapon_ready)
    return false;
  uintptr_t object = fp_unit_object(m, s->weapon_unit);
  uint8_t resource[8], descriptor[20];
  if (!object || !fp_read(m, object + 0x358, resource, 8) ||
      !fp_read(m, s->weapon_descriptor, descriptor, 20) ||
      memcmp(descriptor, s->weapon_bytes, 20) ||
      fp_unit_object(m, s->weapon_unit) != object)
    return false;
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++)
    value = (value << 8) | resource[i];
  if (!value)
    return false;
  *key = value;
  return true;
}
