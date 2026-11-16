/* Native scoped depth maxima at the existing RenderData property dispatch.
 * The original property setter publishes each change. No optic-selector hook.
 */
#include "blur_policy.h"
#include "feature_contract.h"
#include "policy_owner.h"
#include "profile.h"
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef void (*PropertySetter)(void *, uint64_t, uint32_t, const void *);
static _Atomic(FpBlurPolicy *) active;
static _Atomic(PropertySetter) original_setter;
static bool tracked_read(void *user, uintptr_t at, void *out, size_t size) {
  FpBlurPolicy *p = user;
  bool ok = p->backing.read(p->backing.user, at, out, size);
  if (!ok)
    p->read_failed = true;
  return ok;
}
static const uint32_t hashes[] = {0xa7990477, 0xf07fc12e, 0x209f72cf};
#define FP_DOF_CLASS UINT32_C(0xf7a3101f)
static bool fail(char *out, size_t size, const char *text) {
  if (out && size)
    snprintf(out, size, "%s", text);
  return false;
}
static bool provider(FpBlurPolicy *p, uintptr_t *system, uintptr_t *object,
                     uintptr_t *table) {
  const FpMemory *m = &p->memory;
  uintptr_t root = 0, vt = 0, lookup = 0, apply = 0, query = 0, pack = 0,
            getter = 0, apis = 0, hash_api = 0, hash = 0;
  return fp_pointer(m, m->game + FP_PLAYER, &root) &&
         fp_pointer(m, root + 0x18, system) &&
         fp_pointer(m, *system + 0xdc8, object) &&
         fp_pointer(m, *object, &vt) && vt == m->exe + 0x1677dc8 &&
         fp_pointer(m, vt + 0x38, &lookup) && lookup == m->exe + 0x62adb0 &&
         fp_pointer(m, vt + 0x28, &apply) && apply == m->exe + 0x2dd5e0 &&
         fp_pointer(m, *system + 0xdd8, &query) &&
         query == m->exe + 0x27cc5e0 && fp_pointer(m, query + 0x28, &pack) &&
         pack == m->exe + 0x30ffc0 && fp_pointer(m, *system + 0xde8, table) &&
         *table == m->exe + 0x27cc638 && fp_pointer(m, *table + 8, &getter) &&
         getter == m->exe + 0x3100c0 &&
         fp_pointer(m, m->game + 0x3326308, &apis) &&
         fp_pointer(m, apis + 0xc0, &hash_api) &&
         fp_pointer(m, hash_api + 0x28, &hash) && hash == m->exe + 0x310140;
}
static bool owner(FpBlurPolicy *p, FpSnapshot *s) {
  return fp_snapshot(&p->memory, true, s, NULL, 0) &&
         fp_same_player(&p->origin, s) && s->weapon == p->origin.weapon &&
         s->weapon_descriptor == p->origin.weapon_descriptor &&
         !memcmp(s->weapon_bytes, p->origin.weapon_bytes, 20) && s->kind == 6 &&
         s->request == p->origin.request && s->local_request &&
         (s->mask == 0 || s->mask == 2) && !s->controls[5] && !s->seated;
}
static int parameters(FpBlurPolicy *p, bool capture, uint32_t indices[2]) {
  const FpMemory *m = &p->memory;
  uintptr_t pool = 0, descriptors = 0, rows = 0, d = 0;
  uint32_t index = 0;
  if (!fp_pointer(m, m->game + 0x3326a38, &pool) ||
      !fp_map(m, pool + 0x278, p->origin.weapon, 65536, &index) ||
      !fp_pointer(m, pool + 0x290, &descriptors) ||
      !fp_pointer(m, pool + 0x298, &rows) ||
      !fp_pointer(m, descriptors + (uintptr_t)index * 8, &d))
    return 0;
  uint8_t data[24];
  uint32_t entity = 0;
  if (!fp_read(m, d, data, 24))
    return -1;
  memcpy(&entity, data + 8, 4);
  if (entity != p->origin.weapon)
    return -1;
  if (!(data[20] & 1))
    return 0;
  uintptr_t row = rows + (uintptr_t)index * 8;
  if (!fp_read(m, row, indices, 8))
    return -1;
  if (capture) {
    p->param_pool = pool;
    p->param_descriptor = d;
    p->param_row = row;
    memcpy(p->param_bytes, data, 24);
  } else if (pool != p->param_pool || d != p->param_descriptor ||
             row != p->param_row || memcmp(p->param_bytes, data, 24))
    return -1;
  return 1;
}
static int component(FpBlurPolicy *p, uint32_t entity, uint32_t *row) {
  const FpMemory *m = &p->memory;
  uintptr_t buckets = 0;
  uint32_t count = 0, capacity = 0, active_count = 0, divisor = 0, rows = 0,
           allocated = 0;
  if (!fp_pointer(m, p->provider + 0x98, &buckets) ||
      !fp_u32(m, p->provider + 0x90, &count) ||
      !fp_u32(m, p->provider + 0x94, &capacity) ||
      !fp_u32(m, p->provider + 0xa8, &active_count) ||
      !fp_u32(m, p->provider + 0xac, &divisor) ||
      !fp_u32(m, p->provider + 0xb8, &rows) ||
      !fp_u32(m, p->provider + 0xbc, &allocated) || !capacity ||
      capacity > 4096 || count > capacity || active_count > count || !divisor ||
      divisor > capacity || !rows || rows > allocated || allocated > 4096)
    return -1;
  uint32_t at = entity % divisor, index = 0;
  bool found = false;
  for (uint32_t n = 0; n < capacity; n++) {
    uint32_t b[3];
    if (at >= count || !fp_read(m, buckets + (uintptr_t)at * 12, b, 12))
      return -1;
    if (b[2] == UINT32_C(0xfffffffe))
      return 0;
    if (b[0] == entity) {
      index = b[1];
      found = true;
      break;
    }
    if (b[2] == UINT32_C(0x7fffffff))
      return 0;
    at = b[2];
  }
  if (!found)
    return -1;
  for (uint32_t n = 0; n < rows; n++) {
    if (!index)
      return 0;
    if (index >= rows)
      return -1;
    uint32_t id = 0, kind = 0, next = 0;
    if (!fp_u32(m, p->ids + (uintptr_t)index * 4, &id) || id != entity ||
        !fp_u32(m, p->classes + (uintptr_t)index * 4, &kind))
      return -1;
    if (kind == FP_DOF_CLASS) {
      *row = index;
      return 1;
    }
    if (!fp_u32(m, p->chains + (uintptr_t)index * 4, &next))
      return -1;
    index = next;
  }
  return -1;
}
static bool fields(FpBlurPolicy *p, FpBlurSource *s, bool capture) {
  uint32_t count = 0, keys[512];
  const FpMemory *m = &p->memory;
  if (!fp_u32(m, s->blob + 4, &count) || !count || count > 512 ||
      !fp_read(m, s->blob + 16, keys, count * 4))
    return false;
  if (!capture && (s->count != count || memcmp(s->hashes, keys, count * 4)))
    return false;
  uintptr_t offsets[3] = {0};
  for (unsigned i = 0; i < count; i++)
    for (unsigned k = 0; k < 3; k++)
      if (keys[i] == hashes[k]) {
        uint32_t type = 0;
        if (offsets[k])
          return false;
        offsets[k] = s->blob + 16 + (uintptr_t)count * 4 + (uintptr_t)i * 16;
        if (!fp_u32(m, offsets[k], &type) || type != 2)
          return false;
      }
  for (unsigned k = 0; k < 3; k++)
    if (!offsets[k] || (!capture && offsets[k] != s->fields[k]))
      return false;
  if (capture) {
    s->count = count;
    memcpy(s->hashes, keys, count * 4);
    memcpy(s->fields, offsets, sizeof offsets);
    for (unsigned k = 0; k < 3; k++)
      if (!fp_read(m, offsets[k] + 8, s->native + k, 4) ||
          !isfinite(s->native[k]))
        return false;
  }
  return true;
}
static int sources(FpBlurPolicy *p, bool capture) {
  const FpMemory *m = &p->memory;
  uintptr_t system = 0, object = 0, table = 0, ids = 0, classes = 0, chains = 0,
            blobs = 0;
  if (!provider(p, &system, &object, &table) ||
      !fp_pointer(m, object + 0xc8, &ids) ||
      !fp_pointer(m, object + 0xd0, &classes) ||
      !fp_pointer(m, object + 0xd8, &chains) ||
      !fp_pointer(m, object + 0xe8, &blobs))
    return -1;
  if (capture) {
    p->system = system;
    p->provider = object;
    p->table = table;
    p->ids = ids;
    p->classes = classes;
    p->chains = chains;
    p->blobs = blobs;
  } else if (system != p->system || object != p->provider ||
             table != p->table || ids != p->ids || classes != p->classes ||
             chains != p->chains || blobs != p->blobs)
    return -1;
  uint32_t indices[2];
  int params = parameters(p, capture, indices);
  if (params <= 0)
    return params;
  if (!capture && memcmp(indices, p->indices, 8))
    return -1;
  unsigned found = 0;
  for (unsigned i = 0; i < 2; i++) {
    if (indices[i] >= 4096)
      continue;
    FpBlurSource s = {.index = indices[i]};
    uint32_t entity = 0, row = 0;
    if (!fp_u32(m, system + 0x348 + (uintptr_t)indices[i] * 0x30, &entity) ||
        !entity || entity == UINT32_MAX)
      return -1;
    int member = component(p, entity, &row);
    if (member < 0)
      return -1;
    if (!member)
      continue;
    s.entity = entity;
    s.row = row;
    if (!fp_pointer(m, blobs + (uintptr_t)row * 8, &s.blob))
      return -1;
    if (!capture) {
      if (found >= p->count || s.index != p->sources[found].index ||
          s.entity != p->sources[found].entity ||
          s.row != p->sources[found].row || s.blob != p->sources[found].blob)
        return -1;
      s = p->sources[found];
    }
    if (!fields(p, &s, capture))
      return -1;
    if (found && s.entity == p->sources[0].entity)
      return -1;
    if (capture)
      p->sources[found] = s;
    found++;
  }
  if (!found)
    return 0;
  if (capture) {
    p->count = found;
    memcpy(p->indices, indices, 8);
  } else if (found != p->count)
    return -1;
  return 1;
}
static void property_call(uintptr_t caller, void *object, uint64_t instance,
                          uint32_t hash, const void *native) {
  PropertySetter original =
      atomic_load_explicit(&original_setter, memory_order_acquire);
  FpBlurPolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return;
  if (!p || !atomic_load_explicit(&p->lease.armed, memory_order_acquire) ||
      caller != p->memory.game + 0x100a80e ||
      (uintptr_t)object != p->provider ||
      (uint32_t)(instance >> 32) != FP_DOF_CLASS) {
    original(object, instance, hash, native);
    return;
  }
  AcquireSRWLockExclusive(&p->lock);
  FpSnapshot s;
  const void *output = native;
  _Alignas(16) uint8_t variant[24];
  if (atomic_load(&p->lease.armed) &&
      fp_policy_dispatch(&p->memory, &p->lease) && owner(p, &s) &&
      sources(p, false) == 1) {
    for (unsigned i = 0; i < p->count; i++)
      for (unsigned k = 0; k < 3; k++)
        if ((uint32_t)instance == p->sources[i].entity && hash == hashes[k]) {
          uint32_t type = 0;
          float value = 0;
          if (fp_read(&p->memory, (uintptr_t)native, variant, 24)) {
            memcpy(&type, variant, 4);
            memcpy(&value, variant + 8, 4);
            if (type == 2 && isfinite(value)) {
              p->sources[i].native[k] = value;
              p->sources[i].owned[k] = true;
              memset(variant + 8, 0, 4);
              output = variant;
              p->changes++;
            }
          }
        }
  }
  original(object, instance, hash, output);
  ReleaseSRWLockExclusive(&p->lock);
}
static __declspec(noinline) void property_setter(void *object,
                                                 uint64_t instance,
                                                 uint32_t hash,
                                                 const void *native) {
  property_call((uintptr_t)_ReturnAddress(), object, instance, hash, native);
}
void fp_blur_policy_init(FpBlurPolicy *p) {
  memset(p, 0, sizeof *p);
  InitializeSRWLock(&p->lock);
  fp_dispatch_init(&p->lease);
}
bool fp_blur_policy_open(FpBlurPolicy *p, const FpMemory *m,
                         const FpSnapshot *s, bool *no_source, char *error,
                         size_t size) {
  if (no_source)
    *no_source = false;
  if (!p || !m || !s || p->token || atomic_load(&active))
    return fail(error, size, "Weapon blur owner is busy");
  if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
      !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE) ||
      !fp_feature_contract(m, FP_FEATURE_BLUR))
    return fail(error, size, "Weapon blur code differs");
  p->backing = *m;
  p->memory = *m;
  p->memory.read = tracked_read;
  p->memory.user = p;
  p->read_failed = false;
  p->origin = *s;
  p->count = 0;
  FpSnapshot current;
  if (!owner(p, &current))
    return fail(error, size, "Weapon blur owner changed");
  int found = sources(p, true);
  if (found == 0) {
    if (no_source)
      *no_source = true;
    return true;
  }
  if (found < 0)
    return fail(error, size, "Weapon blur source is unavailable");
  uintptr_t setter = 0;
  if (!fp_pointer(m, p->table, &setter) || setter != m->exe + 0x310050)
    return fail(error, size, "Weapon blur dispatch differs");
  HMODULE pin = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_PIN,
                          (LPCWSTR)(uintptr_t)property_setter, &pin))
    return fail(error, size, "Cannot retain weapon blur callback");
  p->platform =
      (FpSlotPlatform){m->exe, GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                              "wine_get_version") != NULL};
  atomic_store(&original_setter, (PropertySetter)setter);
  atomic_store(&active, p);
  FpDispatchSlot slot = {p->table, setter, (uintptr_t)property_setter, false};
  unsigned result = fp_dispatch_begin(&p->lease, &slot, 1, fp_slot_read,
                                      fp_slot_cas, &p->platform, &p->token);
  if (!result) {
    atomic_store(&active, NULL);
    return fail(error, size, "Weapon blur slot changed");
  }
  if (result != 1)
    return fail(error, size, "Weapon blur restoration pending");
  AcquireSRWLockExclusive(&p->lock);
  PropertySetter original = (PropertySetter)setter;
  for (unsigned i = 0; i < p->count; i++)
    for (unsigned k = 0; k < 3; k++) {
      if (!owner(p, &current) || sources(p, false) != 1) {
        ReleaseSRWLockExclusive(&p->lock);
        return fail(error, size, "Weapon blur changed during application");
      }
      _Alignas(16) uint8_t v[24] = {2};
      p->sources[i].owned[k] = true;
      original((void *)p->provider,
               ((uint64_t)FP_DOF_CLASS << 32) | p->sources[i].entity, hashes[k],
               v);
    }
  ReleaseSRWLockExclusive(&p->lock);
  return true;
}
bool fp_blur_policy_close(FpBlurPolicy *p) {
  if (!p || !p->token)
    return true;
  AcquireSRWLockExclusive(&p->lock);
  atomic_store(&p->lease.armed, false);
  FpSnapshot s;
  p->read_failed = false;
  bool current_owner = owner(p, &s);
  int current_source = current_owner ? sources(p, false) : 0;
  if (p->read_failed) {
    ReleaseSRWLockExclusive(&p->lock);
    return false;
  }
  if (current_owner && current_source == 1) {
    PropertySetter original = atomic_load(&original_setter);
    for (unsigned i = 0; i < p->count; i++)
      for (unsigned k = 0; k < 3; k++)
        if (p->sources[i].owned[k]) {
          float current = 1;
          if (!fp_read(&p->memory, p->sources[i].fields[k] + 8, &current, 4)) {
            ReleaseSRWLockExclusive(&p->lock);
            return false;
          }
          if (current == 0) {
            _Alignas(16) uint8_t v[24] = {2};
            memcpy(v + 8, p->sources[i].native + k, 4);
            original((void *)p->provider,
                     ((uint64_t)FP_DOF_CLASS << 32) | p->sources[i].entity,
                     hashes[k], v);
            if (!fp_read(&p->memory, p->sources[i].fields[k] + 8, &current,
                         4) ||
                current != p->sources[i].native[k]) {
              ReleaseSRWLockExclusive(&p->lock);
              return false;
            }
          }
          p->sources[i].owned[k] = false;
        }
  }
  bool done = fp_dispatch_end(&p->lease, p->token, fp_slot_cas, &p->platform);
  if (done) {
    p->token = 0;
    if (atomic_load(&active) == p)
      atomic_store(&active, NULL);
  }
  ReleaseSRWLockExclusive(&p->lock);
  return done;
}
