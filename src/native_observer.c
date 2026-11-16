/* Build25480438 native state observer. Only FpMemory protected reads and pure
 * validation helpers are used: no SDK calls, hooks, setters, recording loop
 * or files. */
#include "native_observer.h"
#include "profile.h"
#include <math.h>
#include <string.h>

static bool fail(FpNativeObservation *out, FpNativeObservationFailure why) {
  memset(out, 0, sizeof *out);
  out->unavailable = FP_OBSERVE_ALL;
  out->failure = why;
  return false;
}
static bool finite_array(const float *p, unsigned count) {
  for (unsigned i = 0; i < count; i++)
    if (!isfinite(p[i]))
      return false;
  return true;
}
static bool same_graph(const FpGraph *a, const FpGraph *b) {
  return a->object == b->object && a->world == b->world &&
         a->hashes == b->hashes && a->links == b->links &&
         a->locals == b->locals && a->worlds == b->worlds && a->ref == b->ref &&
         a->count == b->count;
}
static bool matrix(const FpMemory *m, uintptr_t at, float out[16]) {
  return fp_read(m, at, out, 64) && finite_array(out, 16);
}
static bool camera(const FpMemory *m, FpNativeObservation *o) {
  FpGraph *g = &o->camera_graph;
  uintptr_t graph = 0;
  if (!fp_graph(m, o->owner.camera_unit, g) ||
      !fp_pointer(m, o->owner.camera + 0x18, &graph) ||
      graph != g->object + 0x60 ||
      !fp_u32(m, o->owner.camera + 0x20, &o->camera_node) ||
      o->camera_node >= g->count ||
      !fp_u32(m, o->owner.camera + 4, &o->camera_render_id) ||
      !fp_u32(m, g->links + (uintptr_t)o->camera_node * 4, &o->camera_link) ||
      !fp_read(m, g->object + 0x1d0, &o->camera_parent, 8) ||
      !matrix(m, g->worlds + (uintptr_t)o->camera_node * 64, o->camera_world))
    return false;
  if (!o->camera_parent)
    return (o->camera_link & 65535) == 0;
  uint32_t ref = 0;
  return (o->camera_link & 65535) == 2 && o->camera_parent != g->object &&
         fp_u32(m, o->camera_parent + 8, &ref) &&
         fp_graph(m, ref, &o->camera_parent_graph) &&
         o->camera_parent_graph.object == o->camera_parent &&
         o->camera_parent_graph.world == g->world &&
         (o->camera_link >> 16) < o->camera_parent_graph.count &&
         fp_pointer(m, g->object + 0x78, &o->camera_parent_worlds) &&
         o->camera_parent_worlds == o->camera_parent_graph.worlds;
}
static bool lens(const FpMemory *m, const FpNativeObservation *o,
                 FpNativeLensObservation *out) {
  uint8_t raw[32];
  if (!fp_bytes(m, m->exe + 0x30d940, "f30f104128c3") ||
      !fp_bytes(m, m->exe + 0x30d980, "f30f104134c3") ||
      !fp_read(m, o->owner.camera + 0x28, raw, sizeof raw) ||
      !fp_u32(m, o->owner.camera + 0x50, &out->frustum_mode))
    return false;
  memcpy(&out->near_range, raw, 4);
  memcpy(&out->far_range, raw + 4, 4);
  memcpy(&out->projection_type, raw + 8, 4);
  memcpy(&out->vertical_fov, raw + 12, 4);
  memcpy(out->frustum, raw + 16, 16);
  return isfinite(out->near_range) && isfinite(out->far_range) &&
         isfinite(out->vertical_fov) && finite_array(out->frustum, 4);
}
static bool camera_current(const FpMemory *m, const FpNativeObservation *o) {
  FpNativeObservation now;
  memset(&now, 0, sizeof now);
  now.owner = o->owner;
  if (!camera(m, &now) || !same_graph(&now.camera_graph, &o->camera_graph) ||
      now.camera_node != o->camera_node ||
      now.camera_render_id != o->camera_render_id ||
      now.camera_link != o->camera_link ||
      now.camera_parent != o->camera_parent ||
      now.camera_parent_worlds != o->camera_parent_worlds)
    return false;
  return !o->camera_parent ||
         same_graph(&now.camera_parent_graph, &o->camera_parent_graph);
}
static bool weapon(const FpMemory *m, FpNativeObservation *o) {
  FpSnapshot *s = &o->weapon_owner;
  FpGraph *g = &o->weapon_graph;
  if (!fp_snapshot(m, true, s, NULL, 0) || !fp_same_player(&o->owner, s) ||
      o->owner.request != s->request || o->owner.kind != s->kind ||
      o->owner.head != s->head || o->owner.tail != s->tail ||
      !fp_graph(m, s->weapon_unit, g) ||
      !fp_u32(m, g->links, &o->weapon_link) ||
      !fp_read(m, g->object + 0x1d0, &o->weapon_parent, 8) ||
      !matrix(m, g->worlds, o->weapon_world))
    return false;
  if (!o->weapon_parent)
    return (o->weapon_link & 65535) == 0;
  uint32_t ref = 0;
  return (o->weapon_link & 65535) == 2 && o->weapon_parent != g->object &&
         fp_u32(m, o->weapon_parent + 8, &ref) &&
         fp_graph(m, ref, &o->weapon_parent_graph) &&
         o->weapon_parent_graph.object == o->weapon_parent &&
         o->weapon_parent_graph.world == g->world &&
         (o->weapon_link >> 16) < o->weapon_parent_graph.count &&
         fp_pointer(m, g->object + 0x78, &o->weapon_parent_worlds) &&
         o->weapon_parent_worlds == o->weapon_parent_graph.worlds &&
         matrix(m,
                o->weapon_parent_worlds +
                    (uintptr_t)(o->weapon_link >> 16) * 64,
                o->weapon_parent_world);
}
static bool weapon_current(const FpMemory *m, const FpNativeObservation *o) {
  FpNativeObservation current;
  memset(&current, 0, sizeof current);
  current.owner = o->owner;
  if (!weapon(m, &current) ||
      current.weapon_owner.weapon != o->weapon_owner.weapon ||
      current.weapon_owner.weapon_unit != o->weapon_owner.weapon_unit ||
      current.weapon_owner.weapon_descriptor !=
          o->weapon_owner.weapon_descriptor ||
      memcmp(current.weapon_owner.weapon_bytes, o->weapon_owner.weapon_bytes,
             sizeof current.weapon_owner.weapon_bytes) ||
      !same_graph(&current.weapon_graph, &o->weapon_graph) ||
      current.weapon_link != o->weapon_link ||
      current.weapon_parent != o->weapon_parent ||
      current.weapon_parent_worlds != o->weapon_parent_worlds)
    return false;
  return !o->weapon_parent ||
         same_graph(&current.weapon_parent_graph, &o->weapon_parent_graph);
}
bool fp_native_observe(const FpMemory *m, FpNativeObservation *out) {
  if (!out)
    return false;
  if (!m || !m->read)
    return fail(out, FP_OBSERVE_BAD_ARGUMENT);
  fail(out, FP_OBSERVE_OWNER_UNAVAILABLE);
  if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
      !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE))
    return fail(out, FP_OBSERVE_WRONG_BUILD);
  FpNativeObservation o;
  memset(&o, 0, sizeof o);
  if (!fp_snapshot(m, false, &o.owner, NULL, 0) ||
      !fp_read(m, o.owner.request, o.request_words, sizeof o.request_words))
    return false;
  if ((o.request_words[0] & 65535) != o.owner.kind ||
      o.request_words[1] != o.owner.request_avatar ||
      o.request_words[2] != o.owner.request_weapon)
    return fail(out, FP_OBSERVE_REQUEST_CHANGED);
  o.available = FP_OBSERVE_CORE;
  FpNativeObservation partial;
  memset(&partial, 0, sizeof partial);
  partial.owner = o.owner;
  if (camera(m, &partial)) {
    o.camera_graph = partial.camera_graph;
    o.camera_parent_graph = partial.camera_parent_graph;
    o.camera_node = partial.camera_node;
    o.camera_link = partial.camera_link;
    o.camera_render_id = partial.camera_render_id;
    o.camera_parent = partial.camera_parent;
    o.camera_parent_worlds = partial.camera_parent_worlds;
    memcpy(o.camera_world, partial.camera_world, sizeof o.camera_world);
    o.available |= FP_OBSERVE_CAMERA;
    FpNativeLensObservation sampled = {0};
    if (lens(m, &o, &sampled)) {
      o.lens = sampled;
      o.available |= FP_OBSERVE_LENS;
    }
  }
  if (fp_body_sample(m, o.owner.body, o.body_root, o.body_head, o.body_basis,
                     &o.body_sample))
    o.available |= FP_OBSERVE_BODY;
  else {
    memset(&o.body_sample, 0, sizeof o.body_sample);
    memset(o.body_root, 0, sizeof o.body_root);
    memset(o.body_head, 0, sizeof o.body_head);
    memset(o.body_basis, 0, sizeof o.body_basis);
  }
  FpGraph rig;
  float poses[3][16], hands[2][16], attachment[16];
  if (fp_graph_rig(m, o.owner.viewmodel, &rig) &&
      matrix(m, rig.worlds, poses[0]) &&
      matrix(m, rig.worlds + 5 * 64, poses[1]) &&
      matrix(m, rig.worlds + 50 * 64, poses[2]) &&
      matrix(m, rig.worlds + 13 * 64, hands[0]) &&
      matrix(m, rig.worlds + 34 * 64, hands[1]) &&
      matrix(m, rig.worlds + 8 * 64, attachment)) {
    o.rig_graph = rig;
    memcpy(o.rig_world, poses, sizeof poses);
    memcpy(o.rig_hands, hands, sizeof hands);
    memcpy(o.rig_weapon_attachment, attachment, sizeof attachment);
    o.available |= FP_OBSERVE_RIG;
  }
  memset(&partial, 0, sizeof partial);
  partial.owner = o.owner;
  if (weapon(m, &partial)) {
    o.weapon_owner = partial.weapon_owner;
    o.weapon_graph = partial.weapon_graph;
    o.weapon_parent_graph = partial.weapon_parent_graph;
    o.weapon_link = partial.weapon_link;
    o.weapon_parent = partial.weapon_parent;
    o.weapon_parent_worlds = partial.weapon_parent_worlds;
    memcpy(o.weapon_world, partial.weapon_world, sizeof o.weapon_world);
    memcpy(o.weapon_parent_world, partial.weapon_parent_world,
           sizeof o.weapon_parent_world);
    o.available |= FP_OBSERVE_WEAPON;
  }
  if ((o.available & FP_OBSERVE_CAMERA) && !camera_current(m, &o))
    return fail(out, FP_OBSERVE_GRAPH_CHANGED);
  if ((o.available & FP_OBSERVE_BODY) &&
      !fp_body_sample_current(m, &o.body_sample))
    return fail(out, FP_OBSERVE_GRAPH_CHANGED);
  if (o.available & FP_OBSERVE_RIG) {
    FpGraph current;
    if (!fp_graph_rig(m, o.owner.viewmodel, &current) ||
        !same_graph(&o.rig_graph, &current))
      return fail(out, FP_OBSERVE_GRAPH_CHANGED);
  }
  if (o.available & FP_OBSERVE_LENS) {
    FpNativeLensObservation current = {0};
    if (!lens(m, &o, &current) || memcmp(&current, &o.lens, sizeof current))
      return fail(out, FP_OBSERVE_FIELDS_CHANGED);
  }
  if ((o.available & FP_OBSERVE_WEAPON) && !weapon_current(m, &o))
    return fail(out, FP_OBSERVE_GRAPH_CHANGED);
  FpSnapshot current;
  uint32_t words[5];
  if (!fp_snapshot(m, false, &current, NULL, 0) ||
      !fp_same_player(&o.owner, &current) ||
      o.owner.camera_unit != current.camera_unit ||
      o.owner.viewmodel != current.viewmodel || o.owner.head != current.head ||
      o.owner.tail != current.tail || o.owner.request != current.request ||
      o.owner.kind != current.kind ||
      o.owner.request_avatar != current.request_avatar ||
      o.owner.request_weapon != current.request_weapon ||
      memcmp(o.owner.controls, current.controls, sizeof current.controls) ||
      memcmp(o.owner.flags, current.flags, sizeof current.flags) ||
      !fp_read(m, current.request, words, sizeof words) ||
      memcmp(o.request_words, words, sizeof words))
    return fail(out, FP_OBSERVE_REQUEST_CHANGED);
  o.unavailable = FP_OBSERVE_ALL & ~o.available;
  o.failure = FP_OBSERVE_OK;
  *out = o;
  return true;
}
