/* Mesh pass leases and exact native-cache redraws: no transforms, IK, copied
 * poses or numerical Unit handles. SDK Units come from real world inventory. */
#include "models.h"
#include "feature_contract.h"
#include "profile.h"
#include "scene.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static bool id(uint32_t r) { return r && r != UINT32_MAX; }
static uint64_t mix(uint64_t h, uint64_t v) {
  return (h ^ v) * UINT64_C(1099511628211);
}
static bool retired(const FpMemory *m, uint32_t ref) {
  uintptr_t registry = 0, generations = 0;
  uint32_t count = 0;
  uint8_t generation = 0;
  uint32_t i = ref & 0x3fffff;
  if (!fp_pointer(m, m->exe + FP_REGISTRY, &registry) ||
      !fp_u32(m, registry + 0x98, &count) || count > 0x400000)
    return false;
  if (i >= count)
    return true;
  return fp_pointer(m, registry + 0xa0, &generations) &&
         fp_read(m, generations + i, &generation, 1) &&
         generation != (uint8_t)(ref >> 22);
}
static void drop(FpModels *p) {
  for (int i = 0; i < p->count; i++)
    eng_unref(&p->rows[i].handle);
  for (int i = 0; i < p->target_count; i++)
    eng_unref(&p->targets[i].handle);
  p->count = p->target_count = 0;
  eng_unref(&p->inventory);
  eng_unref(&p->world);
  p->signature = 0;
  p->next_scan = 0;
  memset(p->repairs, 0, sizeof p->repairs);
}
void fp_models_init(FpModels *p) { memset(p, 0, sizeof *p); }
static bool add(FpModelTarget targets[FP_MODEL_TARGETS], int *count,
                const FpMemory *m, uint32_t ref, uintptr_t parent,
                uintptr_t world, bool hide, bool shadow, bool rig,
                bool held_weapon) {
  if (!id(ref))
    return true;
  uintptr_t object = fp_unit_object(m, ref), actual_parent = 0,
            actual_world = 0;
  if (!object || !fp_read(m, object + 0x1d0, &actual_parent, 8) ||
      actual_parent != parent || !fp_pointer(m, object + 0x18, &actual_world) ||
      actual_world != world)
    return false;
  for (int i = 0; i < *count; i++)
    if (targets[i].ref == ref) {
      if (targets[i].object != object || targets[i].parent != parent)
        return false;
      targets[i].hide |= hide;
      targets[i].shadow &= shadow;
      targets[i].held_weapon |= held_weapon;
      return true;
    }
  if (*count == FP_MODEL_TARGETS)
    return false;
  targets[(*count)++] =
      (FpModelTarget){ref, object, parent, world, hide, shadow, rig, held_weapon, 0};
  return true;
}
static bool weapon(FpModelTarget *targets, int *count, const FpMemory *m,
                   uint32_t ref, uintptr_t parent, uintptr_t world,
                   unsigned depth, unsigned *total) {
  if (depth > 8 || ++*total > FP_MODEL_REPAIRS)
    return false;
  for (int i = 0; i < *count; i++)
    if (targets[i].ref == ref)
      return false;
  if (!add(targets, count, m, ref, parent, world, false, false, false, true))
    return false;
  uintptr_t object = fp_unit_object(m, ref), child = 0;
  if (!fp_read(m, object + 0x1d8, &child, 8))
    return false;
  unsigned siblings = 0;
  while (child) {
    uint32_t child_ref = 0;
    uintptr_t next = 0;
    if (++siblings > 32 || !fp_u32(m, child + 8, &child_ref) ||
        fp_unit_object(m, child_ref) != child ||
        !fp_read(m, child + 0x1e0, &next, 8) ||
        !weapon(targets, count, m, child_ref, object, world, depth + 1, total))
      return false;
    child = next;
  }
  return true;
}
static bool targets(FpModels *p, const FpMemory *m, const FpSnapshot *s) {
  FpModelTarget next[FP_MODEL_TARGETS] = {0};
  int count = 0;
  bool pending_repair = false;
  for (unsigned i = 0; i < FP_MODEL_REPAIRS; i++)
    pending_repair |= p->repairs[i].active;
  uintptr_t body = fp_unit_object(m, s->body), world = 0, parent = 0;
  if (!body || !fp_pointer(m, body + 0x18, &world) ||
      !fp_read(m, body + 0x1d0, &parent, 8) ||
      !add(next, &count, m, s->body, parent, world, true, true, false, false))
    return false;
  uint32_t appearance[30];
  uintptr_t at =
      s->avatar_owner + 0x546a70 + (uintptr_t)s->avatar_index * 0x1b8 + 0xdc;
  if (!fp_read(m, at, appearance, sizeof appearance))
    return false;
  p->kept_count = 0;
  for (unsigned layer = 0; layer < 3; layer++)
    for (unsigned slot = 3; slot <= 7; slot++) {
      uint32_t ref = appearance[layer * 10 + slot];
      if (!id(ref))
        continue;
      uintptr_t object = fp_unit_object(m, ref), part_parent = 0, array = 0;
      uint32_t meshes = 0;
      if (!object || !fp_read(m, object + 0x1d0, &part_parent, 8) ||
          !fp_u32(m, object + 0xc8, &meshes) || meshes > 128)
        return false;
      if (part_parent != body)
        continue;
      if (meshes && !fp_pointer(m, object + 0xd0, &array))
        return false;
      for (unsigned i = 0; i < meshes; i++) {
        if (p->kept_count == 2048 ||
            !fp_pointer(m, array + (uintptr_t)i * 16,
                        p->kept_meshes + p->kept_count))
          return false;
        p->kept_count++;
      }
    }
  const unsigned hidden[] = {0, 2, 8, 9};
  for (unsigned layer = 0; layer < 3; layer++)
    for (unsigned k = 0; k < 4; k++) {
      uint32_t ref = appearance[layer * 10 + hidden[k]];
      if (!id(ref))
        continue;
      bool shared = false;
      for (unsigned a = 0; a < 3; a++)
        for (unsigned slot = 3; slot <= 7; slot++)
          if (appearance[a * 10 + slot] == ref)
            shared = true;
      if (!shared && ref != s->body && ref != s->viewmodel &&
          !add(next, &count, m, ref, body, world, true, true, false, false))
        return false;
    }
  FpGraph rig;
  bool have_rig = fp_graph_rig(m, s->viewmodel, &rig);
  if (have_rig && rig.world == world) {
    uintptr_t rig_parent = 0;
    fp_read(m, rig.object + 0x1d0, &rig_parent, 8);
    if (!add(next, &count, m, s->viewmodel, rig_parent, world, false, false,
             true, false))
      return false;
    /* Native armor sleeves and hands attached to the FPS rig. */
    for (unsigned layer = 0; layer < 3; layer++)
      for (unsigned slot = 6; slot <= 9; slot++) {
        uint32_t ref = appearance[layer * 10 + slot];
        if (!id(ref) || ref == s->body || ref == s->viewmodel)
          continue;
        uintptr_t object = fp_unit_object(m, ref), model_parent = 0;
        if (object && fp_read(m, object + 0x1d0, &model_parent, 8) &&
            model_parent == rig.object)
          if (!add(next, &count, m, ref, rig.object, world, false, false,
                   false, false))
            return false;
      }
    uintptr_t gun = fp_unit_object(m, s->weapon_unit), gun_parent = 0;
    if (!gun && pending_repair && !retired(m, s->weapon_unit))
      return false;
    if (gun && !fp_read(m, gun + 0x1d0, &gun_parent, 8))
      return false;
    if (gun && gun_parent == rig.object) {
      unsigned total = 0;
      if (!weapon(next, &count, m, s->weapon_unit, rig.object, world, 0,
                  &total))
        return false;
    }
  } else if (!have_rig && pending_repair)
    return false;
  /* Backpack inventory uses the same descriptor ownership as the local avatar.
   */
  uintptr_t inventory = 0, descriptors = 0, d = 0, rows = 0;
  uint32_t index = 0, entity = 0;
  if (fp_pointer(m, m->game + 0x3326738, &inventory) &&
      fp_map(m, inventory + 40, s->avatar, 4096, &index) &&
      fp_pointer(m, inventory + 64, &descriptors) &&
      fp_pointer(m, descriptors + (uintptr_t)index * 8, &d) &&
      d == s->avatar_descriptor && fp_pointer(m, inventory + 80, &rows) &&
      fp_u32(m, rows + (uintptr_t)index * 48 + 12, &entity) && id(entity)) {
    if (fp_map(m, s->world + FP_ENTITY_MAP, entity, 1048576, &index)) {
      uint32_t descriptor[6];
      if (fp_read(m, s->world + FP_ENTITY_ROWS + (uintptr_t)index * 24,
                  descriptor, 24) &&
          descriptor[2] == entity && (descriptor[5] & 1) &&
          descriptor[3] != s->body && descriptor[3] != s->viewmodel &&
          descriptor[3] != s->weapon_unit)
        add(next, &count, m, descriptor[3], body, world, true, true, false,
            false);
    }
  }
  FpSnapshot after;
  uint32_t again[30];
  if (!fp_snapshot(m, false, &after, NULL, 0) || !fp_same_player(s, &after) ||
      !fp_read(m, at, again, sizeof again) ||
      memcmp(again, appearance, sizeof again))
    return false;
  uint64_t signature = UINT64_C(1469598103934665603);
  for (int i = 0; i < count; i++) {
    signature = mix(signature, next[i].ref);
    signature = mix(signature, next[i].object);
    signature = mix(signature, next[i].parent);
    signature =
        mix(signature, (next[i].hide ? 1 : 0) | (next[i].shadow ? 2 : 0) |
                           (next[i].held_weapon ? 4 : 0));
  }
  if (p->signature != signature) {
    for (int i = 0; i < count; i++)
      for (int j = 0; j < p->target_count; j++)
        if (next[i].ref == p->targets[j].ref &&
            next[i].object == p->targets[j].object) {
          next[i].handle = p->targets[j].handle;
          p->targets[j].handle = 0;
        }
    for (int j = 0; j < p->target_count; j++)
      eng_unref(&p->targets[j].handle);
    memcpy(p->targets, next, (size_t)count * sizeof *next);
    p->target_count = count;
    p->signature = signature;
    eng_unref(&p->inventory);
    p->next_scan = 0;
  }
  p->origin = *s;
  return true;
}
/* 1 exact mesh, 0 positively retired, -1 unavailable: never forget an
 * outstanding restore merely because an engine/memory read failed. */
static int locate(const FpMemory *m, FpMeshLease *r) {
  uintptr_t object = fp_unit_object(m, r->ref);
  if (!object)
    return retired(m, r->ref) ? 0 : -1;
  if (object != r->object)
    return 0;
  uintptr_t world = 0, parent = 0, array = 0, mesh = 0;
  uint32_t count = 0;
  if (!fp_pointer(m, object + 0x18, &world) ||
      !fp_read(m, object + 0x1d0, &parent, 8) ||
      !fp_u32(m, object + 0xc8, &count) || count > 128)
    return -1;
  if (world != r->world || parent != r->parent)
    return 0;
  if (!count)
    return 0;
  if (!fp_pointer(m, object + 0xd0, &array))
    return -1;
  for (unsigned i = 0; i < count; i++) {
    if (!fp_pointer(m, array + (uintptr_t)i * 16, &mesh))
      return -1;
    if (mesh == r->mesh) {
      r->index = i + 1;
      r->array = array;
      return 1;
    }
  }
  return 0;
}
static bool set(const FpMemory *m, FpMeshLease *r, uint8_t bit, bool visible,
                bool *attempted) {
  if (locate(m, r) != 1 || eng_pointer(r->handle) / 4 != r->ref)
    return false;
  uint8_t before = 0;
  if (!fp_read(m, r->mesh + 0xdc, &before, 1))
    return false;
  if (attempted)
    *attempted = true;
  if (!eng_call("Unit.set_mesh_visibility", "ribs", r->handle, (int)r->index,
                visible ? 1 : 0, bit == 1 ? "default" : "shadow_caster"))
    return false;
  uint8_t after = 0;
  return locate(m, r) == 1 && fp_read(m, r->mesh + 0xdc, &after, 1) &&
         after ==
             (visible ? (uint8_t)(before | bit) : (uint8_t)(before & ~bit));
}
static bool restore(FpModels *p, const FpMemory *m, FpMeshLease *r,
                    const FpSnapshot *s) {
  if (!r->bits)
    return true;
  int state = locate(m, r);
  if (state == 0) {
    r->bits = 0;
    return true;
  }
  if (state < 0)
    return false;
  uint8_t current = 0;
  if (!fp_read(m, r->mesh + 0xdc, &current, 1))
    return false;
  if ((current & 1) && !(r->written & 1) &&
      (current == 255 ||
       (s && s->kind == 4 && fp_same_player(s, &p->origin)))) {
    r->bits = 0;
    return true;
  }
  for (uint8_t bit = 1; bit <= 2; bit <<= 1)
    if (r->bits & bit) {
      if (!fp_read(m, r->mesh + 0xdc, &current, 1))
        return false;
      if ((current & bit) == (r->written & bit) &&
          !set(m, r, bit, (r->before & bit) != 0, NULL))
        return false;
      r->bits &= (uint8_t)~bit;
      p->restores++;
    }
  return true;
}
static bool shoulder(FpModelTarget *t, uintptr_t mesh) {
  const char *names[] = {"g_l_shoulderpad", "g_r_shoulderpad"};
  const uint32_t hashes[] = {0x44e19d12, 0xcb0e1712};
  const int nodes[] = {57, 58};
  for (unsigned i = 0; i < 2; i++) {
    int has = 0;
    EngRef handle = 0;
    double name = 0, node = 0, count = 0;
    int material = 0;
    bool queried = eng_call("Unit.has_mesh", "rs>b", t->handle, names[i], &has);
    if (!queried || !has)
      continue;
    bool ok = eng_call("Unit.mesh", "rs>r", t->handle, names[i], &handle) &&
              eng_pointer(handle) == mesh &&
              eng_call("Mesh.name", "r>n", handle, &name) &&
              eng_call("Mesh.node", "r>n", handle, &node) &&
              eng_call("Mesh.num_materials", "r>n", handle, &count) &&
              eng_call("Mesh.has_material", "rs>b", handle, "m_armor_standard",
                       &material) &&
              isfinite(name) &&
              fmod(name + 4294967296.0, 4294967296.0) == hashes[i] &&
              node == nodes[i] && count == 1 && material;
    eng_unref(&handle);
    if (ok)
      return true;
  }
  return false;
}
typedef struct MeshSample {
  uint32_t count;
  uintptr_t array, meshes[FP_MODEL_UNIT_MESHES];
  uint8_t masks[FP_MODEL_UNIT_MESHES];
} MeshSample;
static bool target_current(const FpMemory *m, const FpModelTarget *t) {
  uintptr_t parent = 0, world = 0;
  return fp_unit_object(m, t->ref) == t->object &&
         fp_read(m, t->object + 0x1d0, &parent, sizeof parent) &&
         parent == t->parent && fp_pointer(m, t->object + 0x18, &world) &&
         world == t->world;
}
static bool sample_current(const FpMemory *m, const FpModelTarget *t,
                           const MeshSample *sample, bool masks) {
  uint32_t count = 0;
  uintptr_t array = 0, mesh = 0;
  uint8_t mask = 0;
  if (!target_current(m, t) ||
      !fp_u32(m, t->object + 0xc8, &count) || count != sample->count ||
      (count && (!fp_pointer(m, t->object + 0xd0, &array) ||
                 array != sample->array)))
    return false;
  for (unsigned i = 0; i < count; i++)
    if (!fp_pointer(m, array + (uintptr_t)i * 16, &mesh) ||
        mesh != sample->meshes[i] ||
        (masks && (!fp_read(m, mesh + 0xdc, &mask, 1) ||
                   mask != sample->masks[i])))
      return false;
  return target_current(m, t);
}
static bool sample_meshes(const FpMemory *m, const FpModelTarget *t,
                          MeshSample *sample) {
  memset(sample, 0, sizeof *sample);
  if (!target_current(m, t) ||
      !fp_u32(m, t->object + 0xc8, &sample->count) ||
      sample->count > FP_MODEL_UNIT_MESHES ||
      (sample->count && !fp_pointer(m, t->object + 0xd0, &sample->array)))
    return false;
  for (unsigned i = 0; i < sample->count; i++)
    if (!fp_pointer(m, sample->array + (uintptr_t)i * 16,
                    sample->meshes + i) ||
        !fp_read(m, sample->meshes[i] + 0xdc, sample->masks + i, 1))
      return false;
  return sample_current(m, t, sample, true);
}
static bool repair_domain(const FpSnapshot *s) {
  return s->local_request && !s->seated && !s->controls[5] &&
         (s->kind == 6 ||
          (s->kind == 4 && (s->mask == 0 || s->mask == 2 || s->mask == 0x20)));
}
static bool repair_owner(const FpSnapshot *a, const FpSnapshot *b) {
  return repair_domain(a) && repair_domain(b) && fp_same_player(a, b) &&
         a->camera_unit == b->camera_unit && a->request == b->request &&
         a->kind == b->kind && a->request_avatar == b->request_avatar &&
         a->request_weapon == b->request_weapon &&
         a->weapon == b->weapon && a->weapon_unit == b->weapon_unit &&
         a->weapon_descriptor == b->weapon_descriptor &&
         a->viewmodel == b->viewmodel &&
         !memcmp(a->controls, b->controls, sizeof a->controls) &&
         !memcmp(a->flags, b->flags, sizeof a->flags) &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, sizeof a->weapon_bytes);
}
/* Distinguish unavailable reads from a positively observed authoring successor. */
static int repair_owner_current(const FpMemory *m, const FpSnapshot *owner) {
  FpSnapshot current;
  if (!fp_snapshot(m, true, &current, NULL, 0))
    return -1;
  return repair_owner(owner, &current) ? 1 : 0;
}
static void prune_repairs(FpModels *p) {
  for (unsigned i = 0; i < FP_MODEL_REPAIRS; i++) {
    FpDefaultRepair *r = p->repairs + i;
    if (!r->active)
      continue;
    bool found = false;
    if (repair_owner(&r->owner, &p->origin))
      for (int j = 0; j < p->target_count; j++) {
        const FpModelTarget *t = p->targets + j;
        if (t->held_weapon && !t->hide && t->ref == r->ref &&
            t->object == r->object && t->parent == r->parent &&
            t->world == r->world)
          found = true;
      }
    if (!found)
      memset(r, 0, sizeof *r);
  }
}
static bool cache_header(const FpMemory *m, const FpModelTarget *t,
                         uint32_t *count, uintptr_t *cache) {
  return fp_u32(m, t->object + 0xf8, count) &&
         fp_pointer(m, t->object + 0x100, cache);
}
static bool redraw(FpModels *p, const FpMemory *m, FpModelTarget *t,
                   const MeshSample *sample, FpMeshLease **rows,
                   bool prepare_only) {
  if (!t->held_weapon || t->hide || !sample->count)
    return true;
  uintptr_t body_object = fp_unit_object(m, p->origin.body),
            camera_object = fp_unit_object(m, p->origin.camera_unit);
  if (!body_object || !camera_object)
    return false;
  FpDefaultRepair *r = NULL, *free_plan = NULL;
  for (unsigned i = 0; i < FP_MODEL_REPAIRS; i++) {
    if (p->repairs[i].active && p->repairs[i].ref == t->ref)
      r = p->repairs + i;
    else if (!p->repairs[i].active && !free_plan)
      free_plan = p->repairs + i;
  }
  if (r && ((prepare_only && !r->started) ||
            r->object != t->object || r->parent != t->parent ||
            r->world != t->world || r->array != sample->array ||
            r->count != sample->count ||
            r->body_object != body_object || r->camera_object != camera_object ||
            memcmp(r->meshes, sample->meshes,
                   sample->count * sizeof *sample->meshes) ||
            !repair_owner(&r->owner, &p->origin))) {
    memset(r, 0, sizeof *r);
    free_plan = r;
    r = NULL;
  }
  if (!r) {
    /* A new snapshot belongs only to the global prepare pass. Earlier Unit
     * setters may already have changed shared meshes/cache in the apply pass. */
    if (!prepare_only)
      return true;
    bool hidden = true;
    for (unsigned i = 0; i < sample->count; i++)
      if (sample->masks[i] & 1)
        hidden = false;
    /* Partly visible Units retain their authored per-mesh variants. */
    if (!hidden)
      return true;
    uint32_t cached_count = 0, closing_count = 0;
    uintptr_t cache = 0, closing_cache = 0;
    uint8_t cached[FP_MODEL_UNIT_MESHES], closing[FP_MODEL_UNIT_MESHES];
    if (!cache_header(m, t, &cached_count, &cache) ||
        cached_count != sample->count ||
        !fp_read(m, cache, cached, cached_count))
      return false;
    bool any = false;
    for (unsigned i = 0; i < cached_count; i++)
      any |= (cached[i] & 1) != 0;
    if (!any)
      return true;
    /* Close the entire authored vector and layout before publishing intent.
     * A mesh setter can rewrite every cache entry, including future entries. */
    if (!free_plan || !sample_current(m, t, sample, true) ||
        !cache_header(m, t, &closing_count, &closing_cache) ||
        closing_count != cached_count || closing_cache != cache ||
        !fp_read(m, cache, closing, cached_count) ||
        memcmp(cached, closing, cached_count) ||
        repair_owner_current(m, &p->origin) != 1 ||
        fp_unit_object(m, p->origin.body) != body_object ||
        fp_unit_object(m, p->origin.camera_unit) != camera_object)
      return false;
    r = free_plan;
    memset(r, 0, sizeof *r);
    r->owner = p->origin;
    r->ref = t->ref;
    r->count = sample->count;
    r->object = t->object;
    r->parent = t->parent;
    r->world = t->world;
    r->array = sample->array;
    r->cache = cache;
    r->body_object = body_object;
    r->camera_object = camera_object;
    memcpy(r->meshes, sample->meshes, r->count * sizeof *r->meshes);
    memcpy(r->cached, cached, r->count);
    for (unsigned i = 0; i < r->count; i++)
      r->pending[i] = cached[i] & 1;
    r->active = true;
  }
  if (prepare_only)
    return true;
  for (unsigned i = 0; i < r->count; i++) {
    if (!(r->cached[i] & 1))
      continue;
    uint32_t cached_count = 0;
    uintptr_t cache = 0;
    if (!cache_header(m, t, &cached_count, &cache))
      return false;
    if (cached_count != r->count || cache != r->cache) {
      memset(r, 0, sizeof *r);
      return false;
    }
    int owner = repair_owner_current(m, &r->owner);
    if (owner == 0) {
      memset(r, 0, sizeof *r);
      return false;
    }
    if (owner != 1 || fp_unit_object(m, r->owner.body) != r->body_object ||
        fp_unit_object(m, r->owner.camera_unit) != r->camera_object ||
        !sample_current(m, t, sample, false))
      return false;
    uint8_t current = 0;
    if (!fp_read(m, r->meshes[i] + 0xdc, &current, 1))
      return false;
    if (!(current & 1)) {
      r->pending[i] = 1;
      /* No default-pass lease: TFP-off must not re-hide this native redraw. */
      if (!set(m, rows[i], 1, true, &r->started))
        return false;
      p->writes++;
    }
    r->pending[i] = 0;
  }
  /* A later native hide may have hidden an earlier completed entry again.
   * Keep the complete authored vector until every intended default is visible. */
  uint32_t closing_count = 0;
  uintptr_t closing_cache = 0;
  int owner = repair_owner_current(m, &r->owner);
  if (owner == 0) {
    memset(r, 0, sizeof *r);
    return false;
  }
  if (owner != 1 || !sample_current(m, t, sample, false) ||
      !cache_header(m, t, &closing_count, &closing_cache))
    return false;
  if (closing_count != r->count || closing_cache != r->cache) {
    memset(r, 0, sizeof *r);
    return false;
  }
  for (unsigned i = 0; i < r->count; i++)
    if (r->cached[i] & 1) {
      uint8_t current = 0;
      if (!fp_read(m, r->meshes[i] + 0xdc, &current, 1))
        return false;
      if (!(current & 1)) {
        r->pending[i] = 1;
        return false;
      }
    }
  memset(r, 0, sizeof *r);
  return true;
}
static bool collect(FpModels *p, const FpMemory *m, FpModelTarget *t,
                    bool prepare_only) {
  if (!t->handle || eng_pointer(t->handle) / 4 != t->ref ||
      fp_unit_object(m, t->ref) != t->object)
    return false;
  MeshSample sample;
  FpMeshLease *rows[FP_MODEL_UNIT_MESHES];
  if (!sample_meshes(m, t, &sample))
    return false;
  /* Allocate and resolve every row before any setter can destroy cache data. */
  for (unsigned i = 0; i < sample.count; i++) {
    uintptr_t mesh = sample.meshes[i];
    FpMeshLease *r = NULL;
    for (int j = 0; j < p->count; j++)
      if (p->rows[j].ref == t->ref && p->rows[j].object == t->object &&
          p->rows[j].mesh == mesh)
        r = p->rows + j;
    if (!r) {
      if (p->count == FP_MODEL_MESHES)
        return false;
      r = p->rows + p->count++;
      memset(r, 0, sizeof *r);
      r->ref = t->ref;
      r->object = t->object;
      r->parent = t->parent;
      r->world = t->world;
      r->array = sample.array;
      r->mesh = mesh;
      r->handle = eng_copy(t->handle);
      if (!r->handle)
        return false;
      r->rig_pad = t->rig && sample.count == 5 && shoulder(t, mesh);
    }
    rows[i] = r;
    r->wanted = true;
    r->index = i + 1;
  }
  if (!redraw(p, m, t, &sample, rows, prepare_only))
    return false;
  if (prepare_only)
    return true;
  for (unsigned i = 0; i < sample.count; i++) {
    uintptr_t mesh = sample.meshes[i];
    FpMeshLease *r = rows[i];
    bool hide = t->hide || r->rig_pad;
    uint8_t desired = (hide ? 0 : 1) | (t->shadow ? 2 : 0),
            mask = hide || t->shadow ? 3 : 2;
    for (unsigned j = 0; j < p->kept_count; j++)
      if (mesh == p->kept_meshes[j])
        mask &= (uint8_t)~1;
    uint8_t current = 0;
    if (!fp_read(m, mesh + 0xdc, &current, 1))
      return false;
    for (uint8_t bit = 1; bit <= 2; bit <<= 1)
      if (mask & bit) {
        if ((current & bit) == (desired & bit))
          continue;
        if (!(r->bits & bit)) {
          r->before = (uint8_t)((r->before & ~bit) | (current & bit));
          r->bits |= bit;
        }
        r->written = (uint8_t)((r->written & ~bit) | (desired & bit));
        if (!set(m, r, bit, (desired & bit) != 0, NULL))
          return false;
        p->writes++;
        current = (uint8_t)((current & ~bit) | (desired & bit));
      }
  }
  return true;
}
bool fp_models_update(FpModels *p, const FpMemory *m, const FpSnapshot *s,
                      bool wanted, uint64_t now) {
  if (!wanted)
    memset(p->repairs, 0, sizeof p->repairs);
  if (p->generation != eng_state_generation()) {
    drop(p);
    p->generation = eng_state_generation();
  }
  if (!eng_ready())
    return p->count == 0;
  if (!fp_feature_contract(m, FP_FEATURE_MESH) ||
      eng_cfunction("Unit.set_mesh_visibility") != m->exe + 0x4128e0) {
    snprintf(p->status, sizeof p->status, "Mesh SDK binding differs");
    return p->count == 0;
  }
  bool clean = true;
  for (int i = 0; i < p->count; i++)
    p->rows[i].wanted = false;
  if (wanted && s)
    for (unsigned i = 0; i < FP_MODEL_REPAIRS; i++)
      if (p->repairs[i].active && !repair_owner(&p->repairs[i].owner, s))
        memset(p->repairs + i, 0, sizeof p->repairs[i]);
  if (wanted && s && targets(p, m, s)) {
    prune_repairs(p);
    for (int i = 0; i < p->count; i++)
      for (int j = 0; j < p->target_count; j++)
        if (p->rows[i].ref == p->targets[j].ref &&
            p->rows[i].object == p->targets[j].object)
          p->rows[i].wanted = locate(m, p->rows + i) != 0;
    EngRef world = 0;
    if (eng_call("Application.main_world", ">r", &world) && world) {
      uintptr_t native = 0;
      if (fp_pointer(m, eng_pointer(world), &native) && p->target_count &&
          native == p->targets[0].world) {
        if (p->world && !eng_same(p->world, world)) {
          eng_unref(&p->inventory);
          eng_unref(&p->world);
          p->next_scan = 0;
        }
        if (!p->world)
          p->world = eng_copy(world);
        bool missing = false;
        for (int i = 0; i < p->target_count; i++)
          if (!p->targets[i].handle)
            missing = true;
        if (missing && !p->inventory && now >= p->next_scan) {
          if (eng_call("World.units", "r>r", world, &p->inventory) &&
              eng_len(p->inventory) <= 131072) {
            p->scan = 1;
            p->next_scan = now + 500;
          } else
            eng_unref(&p->inventory);
        }
        for (unsigned n = 0; n < 512 && p->inventory; n++, p->scan++) {
          if (p->scan > eng_len(p->inventory)) {
            eng_unref(&p->inventory);
            break;
          }
          EngRef unit = 0;
          if (!eng_index(p->inventory, p->scan, &unit) || !unit)
            continue;
          uintptr_t encoded = eng_pointer(unit);
          /* Original World.units pushes UnitRef*4+1 as lightuserdata. Keep
           * the real SDK handle and decode only its evidenced Unit tag. */
          if ((encoded & 3) == 1 && (encoded >> 2) < UINT32_MAX) {
            uint32_t ref = (uint32_t)(encoded >> 2);
            for (int i = 0; i < p->target_count; i++)
              if (ref == p->targets[i].ref &&
                  fp_unit_object(m, ref) == p->targets[i].object) {
                if (!p->targets[i].handle)
                  p->targets[i].handle = eng_copy(unit);
              }
          }
          eng_unref(&unit);
        }
        bool prepared = true;
        for (int i = 0; i < p->target_count; i++)
          if (p->targets[i].handle && !collect(p, m, p->targets + i, true))
            prepared = false;
        /* All current mesh rows and eligible full cache vectors precede the
         * first setter, including when Units share underlying Mesh objects. */
        if (prepared) {
          for (int i = 0; i < p->target_count; i++)
            if (p->targets[i].handle && !collect(p, m, p->targets + i, false))
              clean = false;
        } else
          clean = false;
      }
      eng_unref(&world);
    }
  } else if (wanted) { /* A transient unreadable target never means a restore is
                          safe. */
    snprintf(p->status, sizeof p->status, "Waiting for model ownership");
    return false;
  }
  for (int i = p->count - 1; i >= 0; i--)
    if (!p->rows[i].wanted) {
      if (restore(p, m, p->rows + i, s)) {
        eng_unref(&p->rows[i].handle);
        p->rows[i] = p->rows[--p->count];
      } else
        clean = false;
    }
  if (!wanted && clean)
    drop(p);
  if (wanted && p->target_count) {
    int unresolved = 0;
    for (int i = 0; i < p->target_count; i++)
      if (!p->targets[i].handle)
        unresolved++;
    if (unresolved == p->target_count) {
      snprintf(p->status, sizeof p->status,
               "Waiting for engine Unit inventory: %d model targets unresolved",
               unresolved);
      return false;
    }
  }
  snprintf(p->status, sizeof p->status,
           "Mesh passes %d; shadow and body leases %s", p->count,
           clean ? "active" : "pending");
  return clean;
}
