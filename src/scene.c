/* Generation-proved native scene-graph copies. Unit references are used only
 * for registry lookup, never passed as engine Lua/SDK Unit handles. */
#include "scene.h"
#include <math.h>
#include <string.h>
static bool finite3(const float *v) {
  return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}
bool fp_graph(const FpMemory *m, uint32_t ref, FpGraph *g) {
  if (!g)
    return false;
  memset(g, 0, sizeof *g);
  g->ref = ref;
  g->object = fp_unit_object(m, ref);
  uintptr_t vt = 0, getter = 0;
  if (!g->object || !fp_pointer(m, g->object, &vt) ||
      !fp_pointer(m, vt + 0xe8, &getter) || getter != m->exe + 0x2bd870 ||
      !fp_bytes(m, getter, "488d4160c3") ||
      !fp_u32(m, g->object + 0x70, &g->count) || !g->count || g->count > 512 ||
      !fp_pointer(m, g->object + 0x18, &g->world) ||
      !fp_read(m, g->object + 0xa0, &g->hashes, 8) ||
      !fp_pointer(m, g->object + 0x98, &g->links) ||
      !fp_pointer(m, g->object + 0x80, &g->locals) ||
      !fp_pointer(m, g->object + 0x88, &g->worlds))
    return false;
  return fp_unit_object(m, ref) == g->object;
}
bool fp_graph_rig(const FpMemory *m, uint32_t ref, FpGraph *g) {
  if (!fp_graph(m, ref, g) || g->count != 63)
    return false;
  static const struct {
    unsigned node;
    uint32_t hash, link;
  } rows[] = {{0, 0x4a182741, 0},         {1, 0x7f30e61c, 1},
              {2, 0x32623eb2, 0x10001},   {3, 0x975cebbd, 0x20001},
              {4, 0xbccf91e5, 0x30001},   {5, 0x9b115563, 0x40001},
              {7, 0x6f3bd021, 0x50001},   {8, 0x4fdd3337, 0x70001},
              {10, 0x03752c98, 0x70001},  {12, 0x1ca2cd66, 0xa0001},
              {13, 0x5c0c68d3, 0xc0001},  {30, 0x1620b2ce, 0x70001},
              {32, 0x31e34dac, 0x1e0001}, {34, 0x248004f5, 0x200001},
              {50, 0x60ed8c39, 0x40001}};
  uint32_t h[63], l[63];
  if (!fp_read(m, g->hashes, h, sizeof h) || !fp_read(m, g->links, l, sizeof l))
    return false;
  for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
    if (h[rows[i].node] != rows[i].hash || l[rows[i].node] != rows[i].link)
      return false;
  return fp_unit_object(m, ref) == g->object;
}
bool fp_body_sample(const FpMemory *m, uint32_t ref, float root[3],
                    float head[3], float basis[9], FpBodySample *proof) {
  FpGraph g;
  if (!root || !head || !fp_graph(m, ref, &g))
    return false;
  uint32_t h[512], l[512], found = UINT32_MAX;
  if (!fp_read(m, g.hashes, h, g.count * 4) ||
      !fp_read(m, g.links, l, g.count * 4))
    return false;
  for (uint32_t i = 0; i < g.count; i++)
    if (h[i] == 0x8c5570c9) {
      if (found != UINT32_MAX)
        return false;
      found = i;
    }
  if (found == UINT32_MAX || (l[found] & 65535) != 1 ||
      (l[found] >> 16) >= g.count || h[l[found] >> 16] != 0xe9ecca21)
    return false;
  float matrix[16];
  FpQuat q;
  if (!fp_read(m, g.worlds, matrix, 64) ||
      !fp_read(m, g.worlds + (uintptr_t)found * 64 + 48, head, 12) ||
      !finite3(matrix + 12) || !finite3(head) || !fp_q_matrix(matrix, &q))
    return false;
  double distance = 0;
  for (unsigned i = 0; i < 3; i++) {
    root[i] = matrix[12 + i];
    double d = root[i] - head[i];
    distance += d * d;
  }
  if (distance < .05 * .05 || distance > 9)
    return false;
  if (basis)
    for (unsigned a = 0; a < 3; a++)
      for (unsigned b = 0; b < 3; b++)
        basis[a * 3 + b] = matrix[a * 4 + b];
  if (fp_unit_object(m, ref) != g.object)
    return false;
  if (proof)
    *proof = (FpBodySample){g, found, l[found]};
  return true;
}
bool fp_body_points(const FpMemory *m, uint32_t ref, float root[3],
                    float head[3], float basis[9]) {
  return fp_body_sample(m, ref, root, head, basis, NULL);
}
bool fp_body_sample_current(const FpMemory *m, const FpBodySample *p) {
  if (!p)
    return false;
  FpGraph g;
  uint32_t hash = 0, link = 0, parent = 0;
  if (!fp_graph(m, p->graph.ref, &g) || g.object != p->graph.object ||
      g.world != p->graph.world || g.hashes != p->graph.hashes ||
      g.links != p->graph.links || g.locals != p->graph.locals ||
      g.worlds != p->graph.worlds || g.count != p->graph.count ||
      p->head >= g.count)
    return false;
  return fp_u32(m, g.hashes + (uintptr_t)p->head * 4, &hash) &&
         hash == 0x8c5570c9 &&
         fp_u32(m, g.links + (uintptr_t)p->head * 4, &link) &&
         link == p->head_link && (link & 65535) == 1 &&
         (link >> 16) < g.count &&
         fp_u32(m, g.hashes + (uintptr_t)(link >> 16) * 4, &parent) &&
         parent == 0xe9ecca21 && fp_unit_object(m, g.ref) == g.object;
}
bool fp_camera_space(const FpMemory *m, const FpSnapshot *s,
                     FpCameraSpace *space) {
  if (!s || !space)
    return false;
  memset(space, 0, sizeof *space);
  space->rotation = fp_q_identity();
  FpGraph camera, body;
  if (!fp_graph(m, s->camera_unit, &camera) || !fp_graph(m, s->body, &body) ||
      camera.world != body.world ||
      !fp_pointer(m, s->camera + 0x18, &space->graph) ||
      space->graph != camera.object + 0x60 ||
      !fp_u32(m, s->camera + 0x20, &space->index) || space->index != 0 ||
      !fp_u32(m, camera.links, &space->link) ||
      !fp_read(m, camera.object + 0x1d0, &space->parent, 8))
    return false;
  space->links = camera.links;
  if (!(space->link & 65535))
    return space->parent == 0;
  if ((space->link & 65535) != 2 || space->parent < 65536 ||
      space->parent == camera.object ||
      !fp_u32(m, space->parent + 8, &space->parent_ref) ||
      fp_unit_object(m, space->parent_ref) != space->parent)
    return false;
  FpGraph parent;
  uintptr_t external = 0;
  if (!fp_graph(m, space->parent_ref, &parent) ||
      parent.world != camera.world ||
      !fp_pointer(m, camera.object + 0x78, &external) ||
      external != parent.worlds)
    return false;
  space->parent_node = space->link >> 16;
  space->worlds = parent.worlds;
  if (space->parent_node >= parent.count)
    return false;
  float matrix[16];
  if (!fp_read(m, parent.worlds + (uintptr_t)space->parent_node * 64, matrix,
               64) ||
      !finite3(matrix + 12) || !fp_q_matrix(matrix, &space->rotation) ||
      fabsf(matrix[3]) + fabsf(matrix[7]) + fabsf(matrix[11]) > .001f ||
      fabsf(matrix[15] - 1) > .001f)
    return false;
  memcpy(space->position, matrix + 12, 12);
  return fp_unit_object(m, parent.ref) == parent.object;
}
bool fp_camera_space_same(const FpCameraSpace *a, const FpCameraSpace *b) {
  return a && b && a->graph == b->graph && a->links == b->links &&
         a->parent == b->parent && a->worlds == b->worlds &&
         a->index == b->index && a->link == b->link &&
         a->parent_ref == b->parent_ref && a->parent_node == b->parent_node;
}
bool fp_camera_native_parent(const FpMemory *m, const FpSnapshot *s,
                             FpCameraSpace *space) {
  if (!fp_camera_space(m, s, space) || space->link != 0x00320002 ||
      space->parent_node != 50)
    return false;
  FpGraph rig;
  return fp_graph_rig(m, s->viewmodel, &rig) && rig.object == space->parent &&
         rig.worlds == space->worlds;
}

static bool attachment_graph_same(const FpGraph *a, const FpGraph *b) {
  return a->ref == b->ref && a->object == b->object && a->world == b->world &&
         a->hashes == b->hashes && a->links == b->links &&
         a->locals == b->locals && a->worlds == b->worlds &&
         a->count == b->count;
}
static bool attachment_root(const FpMemory *m, const FpGraph *child,
                            const FpGraph *rig, uint32_t node) {
  uint32_t link = 0;
  uintptr_t parent = 0, worlds = 0;
  return child->object != rig->object && child->world == rig->world &&
         node < rig->count && fp_u32(m, child->links, &link) &&
         link == ((node << 16) | 2) &&
         fp_pointer(m, child->object + 0x1d0, &parent) &&
         parent == rig->object &&
         fp_pointer(m, child->object + 0x78, &worlds) && worlds == rig->worlds;
}
static bool attachment_camera(const FpMemory *m, const FpSnapshot *s,
                              const FpGraph *camera, const FpGraph *rig) {
  uintptr_t graph = 0;
  uint32_t node = UINT32_MAX;
  return fp_pointer(m, s->camera + 0x18, &graph) &&
         graph == camera->object + 0x60 &&
         fp_u32(m, s->camera + 0x20, &node) && node == 0 &&
         attachment_root(m, camera, rig, 50);
}
static bool attachment_owner_same(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && b->weapon_ready && b->local_request &&
         a->head == b->head && a->tail == b->tail &&
         a->request == b->request && a->kind == b->kind &&
         a->request_avatar == b->request_avatar &&
         a->request_weapon == b->request_weapon &&
         a->camera_unit == b->camera_unit && a->viewmodel == b->viewmodel &&
         a->weapon == b->weapon && a->weapon_unit == b->weapon_unit &&
         a->weapon_descriptor == b->weapon_descriptor &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, sizeof a->weapon_bytes) &&
         !memcmp(a->controls, b->controls, sizeof a->controls) &&
         !memcmp(a->flags, b->flags, sizeof a->flags);
}
bool fp_rig_attachments(const FpMemory *m, const FpSnapshot *s,
                        FpGraph *rig_out) {
  if (!rig_out)
    return false;
  memset(rig_out, 0, sizeof *rig_out);
  if (!m || !m->read || !s || !s->weapon_ready || !s->local_request ||
      (s->kind != 4 && s->kind != 6))
    return false;
  /* The caller supplies an already qualified snapshot. Close its strict held
   * weapon/descriptor proof below, without calling any native SDK function.
   * Game World ownership and engine Unit graph worlds are distinct domains. */
  FpSnapshot owner = *s, current;
  FpGraph rig, weapon, camera, body, after;
  uint32_t request[5], request_after[5];
  uint8_t descriptor[20];
  if (!fp_read(m, owner.request, request, sizeof request) ||
      (request[0] & 65535) != owner.kind ||
      request[1] != owner.request_avatar ||
      request[2] != owner.request_weapon ||
      !fp_read(m, owner.weapon_descriptor, descriptor, sizeof descriptor) ||
      memcmp(descriptor, owner.weapon_bytes, sizeof descriptor) ||
      !fp_graph_rig(m, owner.viewmodel, &rig) ||
      !fp_graph(m, owner.weapon_unit, &weapon) ||
      !fp_graph(m, owner.camera_unit, &camera) ||
      !fp_graph(m, owner.body, &body) || body.world != rig.world ||
      !attachment_root(m, &weapon, &rig, 8) ||
      !attachment_camera(m, &owner, &camera, &rig))
    return false;
  /* Native entry queues these links before its first evaluation. The old rig
   * may already have the right topology while the gun/camera still belong to
   * the body/unparented view. Require completed links and close them again. */
  if (!fp_graph_rig(m, owner.viewmodel, &after) ||
      !attachment_graph_same(&rig, &after) ||
      !fp_graph(m, owner.weapon_unit, &after) ||
      !attachment_graph_same(&weapon, &after) ||
      !fp_graph(m, owner.camera_unit, &after) ||
      !attachment_graph_same(&camera, &after) ||
      !fp_graph(m, owner.body, &after) ||
      !attachment_graph_same(&body, &after) ||
      !attachment_root(m, &weapon, &rig, 8) ||
      !attachment_camera(m, &owner, &camera, &rig) ||
      !fp_snapshot(m, true, &current, NULL, 0) ||
      !attachment_owner_same(&owner, &current) ||
      !fp_read(m, owner.request, request_after, sizeof request_after) ||
      memcmp(request, request_after, sizeof request) ||
      fp_unit_object(m, rig.ref) != rig.object ||
      fp_unit_object(m, weapon.ref) != weapon.object ||
      fp_unit_object(m, camera.ref) != camera.object ||
      fp_unit_object(m, body.ref) != body.object)
    return false;
  *rig_out = rig;
  return true;
}
