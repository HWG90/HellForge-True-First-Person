/* See arm_drive.h. Build 25480438.
 *
 * Lifted from the retired First Person Controller (true-first-person-controller-body14-20261118), whose
 * versions 0.3.36 / 0.3.37 put the body's arm animation on this rig in the game and were accepted for
 * sprint, reload and inspection (its docs/VALIDATION.md). "graft:N" below is a line of its
 * src/native_arm_graft.h, "tree:N" of native_weapon_tree.h, "mount:N" of native_attachment_pose.h,
 * "view:N" of native_view.c, "source:N" of native_camera_source.h.
 *
 * One primary render, while switched on and the game's camera request for the local avatar is kind 6:
 *   1. prove the player, the body unit, the first-person rig unit, the gun on the rig's socket and the
 *      camera on the rig's eye, all in the world being rendered; prove the engine's setter and update;
 *   2. bind the joint map when nothing is bound or any of those units changed (never across a failed
 *      proof: every refusal drops the binding);
 *   3. read the rig's 63 local records; take the game's own value of each joint (graft_underlying);
 *      compute the 42 locals from the body's world matrices now (src/arm_graft.h);
 *   4. write each joint whose record differs, through the engine's setter (UnitAPI+0x60: the whole
 *      64-byte local record), then refresh the rig, the sleeve and glove units whose external parent
 *      array is this rig's, and the weapon's units, parents first;
 *   5. the caller forwards the render call.
 *
 * After the draw, the ACCEPTED behaviour (0.3.36 / 0.3.37, graft:662-817 with graft.drawing false):
 * nothing. The written values stay in the rig; on the next frame each joint's game value is recovered
 * by graft_underlying (the value saved before the write where the record still holds what was written,
 * the record itself where the game has written since), and when the drive stops the game's values are
 * written back once (graft_restore_graph, graft:147-179). The other behaviour, restore_after_draw,
 * writes them back right after every primary render (0.3.46, graft:186-209: there only for weapon draws
 * and holsters, and its acceptance in the game was still pending when the mod was retired).
 *
 * What is not lifted: the per-action state machine (160 ms blends in and out, per-action profiles, the
 * resting-hip cache), the weapon draw and holster paths, and the evaluated-transform test of the two
 * attachments (view:373-402). The drive runs through the whole native view instead of per action; its
 * entry alignment is 0.3.37's (graft:293-303) on the first driven frame of a binding.
 *
 * Threads: everything here runs on the game thread. The retired host established that the primary
 * world render (the application table's slot +0x28, called from game+0xab7ac7) is called on the same
 * thread as HellForge's game-thread tick (native_host.c:388, `same_thread`, a condition of every call
 * into its controller) and called the engine's setter and World update from there in the accepted
 * versions; it called the same setters from the tick as well (graft_release from native_hip_trial.h).
 * src/render_slot.c passes that condition in as `game_thread`; without it nothing is read or written. */
#include "arm_drive.h"
#include "profile.h"
#include "snapshot.h"
#include <float.h>

/* ---- build constants, all from the retired sources for this same build ---- */
#define B_RIG_RESOURCE     UINT64_C(0xd0cf4d493ca08bed) /* view:256, :527: the first-person rig's unit resource */
#define B_EXE_API_ROOT     UINT32_C(0x27c8d80)  /* source:132: *(game+FP_API_ROOT) */
#define B_EXE_WORLD_API    UINT32_C(0x27cab40)  /* source:133: root+0x08 */
#define B_EXE_UNIT_API     UINT32_C(0x27c8f10)  /* source:137: root+0x18 */
#define L_UNIT_SET_LOCAL   0x60                 /* graft:88: UnitAPI slot of the full local-record setter */
#define B_EXE_SET_LOCAL    UINT32_C(0x1fcad0)   /* graft:88 */
#define B_HEX_SET_LOCAL    "48895c24084889742410574883ec20498bd84863fa" /* graft:89 */
#define L_WORLD_UPDATE     0x100                /* source:141: WorldAPI slot of update_unit */
#define B_EXE_UPDATE       UINT32_C(0x336530)   /* source:141 */
#define B_HEX_UPDATE       "40534883ec20488bd98bcae88073d6ff488bd0488bcb4883c4205be970c8e7ff" /* source:95 */
#define B_AVATAR_PHYSICS   UINT32_C(0x547844)   /* view:208: byte, bit 0x10 = ragdoll; stride 0x78 */
#define B_AVATAR_PARTS     UINT32_C(0x546a70)   /* view:249, :281; src/models.c: appearance rows, stride 0x1b8, slots at +0xdc */
/* -- unit object layout (the same offsets as src/pose_helper.c, camera_helper.c, models.c) -- */
#define L_UNIT_WORLD       0x18
#define L_UNIT_NODES       0x70
#define L_UNIT_EXTERNAL    0x78   /* the parent unit's world matrices, for a linked unit */
#define L_UNIT_LOCALS      0x80
#define L_UNIT_WORLDS      0x88
#define L_UNIT_LINKS       0x98
#define L_UNIT_NAMES       0xa0
#define L_UNIT_PARENT      0x1d0
#define L_UNIT_CHILD       0x1d8
#define L_UNIT_SIBLING     0x1e0
#define L_UNIT_RESOURCE    0x358
#define L_CONTEXT_CAMERA_UNIT 8   /* src/backend.c:158 */

enum { TREE_UNITS = 32, ARM_UNITS = 6, BEND_DEGREES = 70 };
typedef struct Part { uint32_t ref, depth; uintptr_t object, parent; } Part;   /* tree:4-7 */
typedef struct Tree { Part units[TREE_UNITS]; unsigned count; } Tree;
typedef struct Units {
  uintptr_t world, body, rig, weapon, avatar_owner;
  uint32_t body_ref, rig_ref, weapon_ref, avatar_index;
  bool gun_away; /* only ever true in a hold: no live gun on the rig's socket (weapon may then be 0) */
} Units;
typedef struct Api { uint64_t set_local, update_unit; } Api;

static struct {
  TfpArmDriveHost host;
  bool has_host, on, restore_after, bend;
  bool hold; /* tfp_arm_drive_set_hold */
  float weight;
  TfpArmDriveStats stats;
  /* the binding: only while `bound`, and only for exactly these units */
  bool bound, bound_bend, aligned;
  TfpGraft map;
  unsigned joints; /* 42, or 1 for the test bend */
  Units units;
  uintptr_t locals;
  uint32_t arms[ARM_UNITS];
  unsigned arm_count;
  /* what is written in the game now (graft.owned, .original, .applied, .attachments): its own record of
   * the unit, so it can be put back after the binding is gone */
  uint64_t owned;
  uintptr_t own_world, own_rig, own_locals, own_worlds;
  uint32_t own_ref, own_node[TFP_GRAFT_JOINTS], own_arms[ARM_UNITS];
  unsigned own_arm_count;
  TfpGraftLocal original[TFP_GRAFT_JOINTS], applied[TFP_GRAFT_JOINTS];
  Tree attachments;
} d;

static const FpMemory *mem(void) { return &d.host.memory; }
static bool rd(uintptr_t at, void *out, size_t size) { return fp_read(mem(), at, out, size); }
static bool ptr(uintptr_t at, uintptr_t *out) { return fp_pointer(mem(), at, out); }
static bool u32(uintptr_t at, uint32_t *out) { return fp_u32(mem(), at, out); }
static uintptr_t unit(uint32_t ref) { return fp_unit_object(mem(), ref); }
static unsigned bits(uint64_t v) { unsigned n = 0; for (; v; v &= v - 1) n++; return n; }

/* ---- the engine's two functions (graft:82-91 graft_setter; source:129-141) ---- */
static bool api(Api *out) {
  const FpMemory *m = mem();
  uintptr_t root, world_api, unit_api, set, update;
  if (!ptr(m->game + FP_API_ROOT, &root) || root != m->exe + B_EXE_API_ROOT || !ptr(root + 8, &world_api) || world_api != m->exe + B_EXE_WORLD_API ||
      !ptr(root + 0x18, &unit_api) || unit_api != m->exe + B_EXE_UNIT_API || !ptr(unit_api + L_UNIT_SET_LOCAL, &set) ||
      set != m->exe + B_EXE_SET_LOCAL || !ptr(world_api + L_WORLD_UPDATE, &update) || update != m->exe + B_EXE_UPDATE ||
      !d.host.executable(d.host.user, set) || !d.host.executable(d.host.user, update) || !fp_bytes(m, set, B_HEX_SET_LOCAL) ||
      !fp_bytes(m, update, B_HEX_UPDATE))
    return false;
  out->set_local = set, out->update_unit = update;
  return true;
}
static bool update(const Api *a, uintptr_t world, uint32_t ref) { return d.host.update_unit(d.host.user, a->update_unit, world, ref); }

/* ---- the weapon's units (tree:14-30, mount:8-73) ---- */
/* Only units attached below this gun, parents before children. */
static bool collect(uintptr_t world, uintptr_t parent, uintptr_t object, uint32_t ref, unsigned depth, Tree *tree) {
  uintptr_t owner, current_world, child = 0;
  if (!object || depth > 8 || tree->count == TREE_UNITS || unit(ref) != object || !ptr(object + L_UNIT_WORLD, &current_world) ||
      current_world != world || !ptr(object + L_UNIT_PARENT, &owner) || owner != parent)
    return false;
  for (unsigned i = 0; i < tree->count; ++i)
    if (tree->units[i].object == object) return false;
  tree->units[tree->count++] = (Part){.ref = ref, .depth = depth, .object = object, .parent = parent};
  if (!rd(object + L_UNIT_CHILD, &child, sizeof child)) return false;
  while (child) {
    uint32_t child_ref;
    if (!u32(child + 8, &child_ref) || !collect(world, object, child, child_ref, depth + 1, tree) || !rd(child + L_UNIT_SIBLING, &child, sizeof child))
      return false;
  }
  return true;
}
/* fpc_branch_position_evaluated (retired pose_math.h:46-56) */
static bool position_evaluated(const TfpGraftLocal *local, const TfpGraftMatrix *parent, const TfpGraftMatrix *child) {
  for (unsigned i = 0; i < 3; ++i) {
    double expected = parent->values[12 + i];
    for (unsigned j = 0; j < 3; ++j) expected += (double)local->position[j] * parent->values[4 * j + i];
    if (!isfinite(expected) || !isfinite(child->values[12 + i]) || fabs(expected - child->values[12 + i]) > .001 + 4. * FLT_EPSILON * fabs(expected))
      return false;
  }
  return true;
}
/* The renderer can already have evaluated a mounted part: every external root is compared before a
 * refresh is asked for. false: the part is not mounted as expected. */
static bool mount_current(const Part *part, bool *current) {
  uintptr_t external, parent_worlds, links, locals, worlds;
  uint32_t nodes, parent_nodes;
  static uint32_t graph[512];
  if (!u32(part->object + L_UNIT_NODES, &nodes) || !nodes || nodes > 512 || !u32(part->parent + L_UNIT_NODES, &parent_nodes) || !parent_nodes ||
      parent_nodes > 512 || !ptr(part->object + L_UNIT_EXTERNAL, &external) || !ptr(part->parent + L_UNIT_WORLDS, &parent_worlds) ||
      external != parent_worlds || !ptr(part->object + L_UNIT_LINKS, &links) || !ptr(part->object + L_UNIT_LOCALS, &locals) ||
      !ptr(part->object + L_UNIT_WORLDS, &worlds) || !rd(links, graph, nodes * sizeof *graph))
    return false;
  bool mounted = false;
  *current = true;
  for (unsigned i = 0; i < nodes; ++i) {
    if ((uint16_t)graph[i] != 2) continue;
    unsigned socket = graph[i] >> 16;
    TfpGraftLocal local;
    TfpGraftMatrix parent, actual;
    if (socket >= parent_nodes || !rd(external + 64 * (uintptr_t)socket, &parent, sizeof parent) || !rd(locals + 64 * (uintptr_t)i, &local, sizeof local) ||
        !rd(worlds + 64 * (uintptr_t)i, &actual, sizeof actual))
      return false;
    mounted = true;
    /* The full affine basis: a folded or mirrored attachment can have a zero or negative local scale. */
    for (unsigned axis = 0; axis < 3; ++axis) {
      for (unsigned j = 0; j < 3; ++j) {
        double expected = 0;
        for (unsigned k = 0; k < 3; ++k) expected += (double)local.basis[3 * axis + k] * parent.values[4 * k + j];
        expected *= local.preserved[axis];
        double seen = actual.values[4 * axis + j];
        if (!isfinite(expected) || !isfinite(seen)) return false;
        if (fabs(expected - seen) > .00002 * (1 + fabs(expected))) *current = false;
      }
    }
    if (!position_evaluated(&local, &parent, &actual)) *current = false;
  }
  return mounted;
}
static bool part_owner(uintptr_t world, const Part *part) {
  uintptr_t owner, current_world;
  return unit(part->ref) == part->object && ptr(part->object + L_UNIT_PARENT, &owner) && owner == part->parent &&
         ptr(part->object + L_UNIT_WORLD, &current_world) && current_world == world;
}
/* The rig moved: the gun always, each mounted part that is not already where its parent puts it. A
 * mount that left is skipped with its descendants. 0: not this gun any more; -1: an engine call faulted. */
static int update_weapon(const Api *a, uintptr_t world, const Tree *tree) {
  if (!tree->count || !part_owner(world, &tree->units[0])) return 0;
  if (!update(a, world, tree->units[0].ref)) return -1;
  d.stats.pieces++;
  for (unsigned i = 1; i < tree->count; ++i) {
    const Part *part = &tree->units[i];
    bool current = false;
    if (!part_owner(world, part) || !mount_current(part, &current)) {
      while (i + 1 < tree->count && tree->units[i + 1].depth > part->depth) ++i;
      continue;
    }
    if (current) continue;
    if (!update(a, world, part->ref)) return -1;
    d.stats.pieces++;
  }
  return 1;
}
/* Armour sleeves and gloves have their own evaluated graphs: only the appearance units whose external
 * parent array is this exact rig (graft:124-134). */
static bool refresh_arms(const Api *a, uintptr_t world, uintptr_t rig, uintptr_t rig_worlds, const uint32_t *arms, unsigned count) {
  for (unsigned i = 0; i < count; ++i) {
    uintptr_t object = unit(arms[i]), external, parent, unit_world;
    if (object && ptr(object + L_UNIT_PARENT, &parent) && parent == rig && ptr(object + L_UNIT_WORLD, &unit_world) && unit_world == world &&
        ptr(object + L_UNIT_EXTERNAL, &external) && external == rig_worlds) {
      if (!update(a, world, arms[i])) return false;
      d.stats.pieces++;
    }
  }
  return true;
}

/* ---- putting the game's values back (graft:147-179 graft_restore_graph) ---- */
/* Only a changed unit generation is a positive observation that the rig is gone (retired generation_retired). */
static bool generation_retired(uint32_t ref) {
  uintptr_t registry, generations;
  uint32_t count, index = ref & 0x3fffff;
  uint8_t generation;
  return ptr(mem()->exe + FP_REGISTRY, &registry) && u32(registry + 0x98, &count) && count <= 0x400000 && index < count &&
         ptr(registry + 0xa0, &generations) && rd(generations + index, &generation, 1) && generation != (uint8_t)(ref >> 22);
}
/* true: no joint holds a value of this mod any more. false: could not be done now; everything is kept
 * for the next try. */
static bool restore(void) {
  if (!d.owned) return true;
  if (!generation_retired(d.own_ref)) {
    uintptr_t locals, world, worlds;
    Api a;
    if (unit(d.own_ref) != d.own_rig || !ptr(d.own_rig + L_UNIT_WORLD, &world) || world != d.own_world || !ptr(d.own_rig + L_UNIT_LOCALS, &locals) ||
        locals != d.own_locals || !ptr(d.own_rig + L_UNIT_WORLDS, &worlds) || worlds != d.own_worlds || !api(&a))
      return false;
    for (unsigned i = 0; i < TFP_GRAFT_JOINTS; ++i) {
      if (!(d.owned & (UINT64_C(1) << i))) continue;
      TfpGraftLocal now;
      if (!rd(locals + (uintptr_t)d.own_node[i] * sizeof now, &now, sizeof now)) return false;
      const TfpGraftLocal game = tfp_graft_underlying(true, &d.original[i], &d.applied[i], now);
      if (!memcmp(&game, &now, 48)) continue;
      if (!d.host.set_local(d.host.user, a.set_local, d.own_ref, d.own_node[i], &game)) return false;
      d.stats.joints++;
    }
    if (!update(&a, world, d.own_ref) || !refresh_arms(&a, world, d.own_rig, worlds, d.own_arms, d.own_arm_count)) return false;
    /* The evaluated gun and optic in the same frame. A gun already returned to the body belongs to its new parent. */
    if (update_weapon(&a, world, &d.attachments) < 0) return false;
    d.stats.restores++;
  }
  d.owned = 0, d.attachments.count = 0;
  return true;
}

/* ---- proofs ---- */
/* hold false: the proof as it always was. hold true (arm_drive.h): the gun's own conditions do not refuse;
 * they set gun_away instead. s may then be a player snapshot, whose weapon_unit is 0. */
static uint32_t prove(uintptr_t world, const TfpSnapshot *s, bool hold, Units *out) {
  Units u = {.world = world, .body_ref = s->body, .rig_ref = s->viewmodel, .weapon_ref = s->weapon_unit,
             .avatar_owner = s->avatar_owner, .avatar_index = s->avatar_index};
  uintptr_t unit_world, names, links, parent, external, rig_worlds, camera, context = s->context;
  uint64_t resource;
  uint32_t count, socket, eye, link, camera_ref;
  uint8_t physics = 0;
  /* view:208-210: the avatar's ragdoll flag outlives the gun attachment */
  if (!rd(u.avatar_owner + B_AVATAR_PHYSICS + (uintptr_t)u.avatar_index * 0x78, &physics, 1)) return TFP_ARM_SNAPSHOT;
  if (physics & 0x10) return TFP_ARM_RAGDOLL;
  /* view:493-509 resolve_rig, :520-531 */
  u.body = unit(u.body_ref), u.rig = unit(u.rig_ref), u.weapon = unit(u.weapon_ref);
  if (!u.body || !u.rig || (!u.weapon && !hold) || u.body == u.rig) return TFP_ARM_UNITS;
  if (!rd(u.rig + L_UNIT_RESOURCE, &resource, 8) || resource != B_RIG_RESOURCE || !u32(u.rig + L_UNIT_NODES, &count) || count != TFP_GRAFT_RIG_NODES ||
      !ptr(u.rig + L_UNIT_NAMES, &names) || !u32(names + TFP_GRAFT_RIG_SOCKET * 4, &socket) || socket != TFP_GRAFT_NAME_SOCKET ||
      !u32(names + TFP_GRAFT_RIG_EYE * 4, &eye) || eye != TFP_GRAFT_NAME_RIG_EYE || !ptr(u.rig + L_UNIT_WORLDS, &rig_worlds))
    return TFP_ARM_UNITS;
  if (!ptr(u.rig + L_UNIT_WORLD, &unit_world) || unit_world != world || !ptr(u.body + L_UNIT_WORLD, &unit_world) || unit_world != world)
    return TFP_ARM_WORLD;
  if (!u.weapon) u.gun_away = true; /* hold only (see above) */
  else if (!ptr(u.weapon + L_UNIT_WORLD, &unit_world) || unit_world != world) {
    if (!hold) return TFP_ARM_WORLD;
    u.gun_away = true;
  }
  /* view:758-766: the gun's root is linked to the rig's socket, the camera's to the rig's eye */
  else if (!ptr(u.weapon + L_UNIT_PARENT, &parent) || parent != u.rig || !ptr(u.weapon + L_UNIT_LINKS, &links) || !u32(links, &link) ||
           link != (((uint32_t)TFP_GRAFT_RIG_SOCKET << 16) | 2u) || !ptr(u.weapon + L_UNIT_EXTERNAL, &external) || external != rig_worlds) {
    if (!hold) return TFP_ARM_WEAPON;
    u.gun_away = true;
  }
  if (!u32(context + L_CONTEXT_CAMERA_UNIT, &camera_ref) || !(camera = unit(camera_ref)) || camera == u.rig || !ptr(camera + L_UNIT_PARENT, &parent) ||
      parent != u.rig || !ptr(camera + L_UNIT_LINKS, &links) || !u32(links, &link) || link != (((uint32_t)TFP_GRAFT_RIG_EYE << 16) | 2u))
    return TFP_ARM_CAMERA;
  *out = u;
  return TFP_ARM_NONE;
}
static bool same_units(const Units *a, const Units *b) {
  /* In a hold the gun is not part of what a binding is for: the map joins the body and the rig, and the entry
   * alignment is the rig's own socket. So the gun leaving, returning or being another does not unbind. */
  return a->world == b->world && a->body == b->body && a->rig == b->rig && (d.hold || a->weapon == b->weapon) && a->body_ref == b->body_ref &&
         a->rig_ref == b->rig_ref && (d.hold || a->weapon_ref == b->weapon_ref) && a->avatar_owner == b->avatar_owner &&
         a->avatar_index == b->avatar_index;
}
static bool body_matrices(uintptr_t body, TfpGraftMatrix *out) { /* graft:92-96 */
  uintptr_t p;
  return ptr(body + L_UNIT_WORLDS, &p) && rd(p, out, 64 * sizeof *out) &&
         rd(p + 64 * sizeof *out, out + 64, (TFP_GRAFT_BODY_NODES - 64) * sizeof *out);
}
/* graft:210-229 graft_bind_rig, graft:135-144 graft_arm_units */
static bool bind(const Units *u) {
  static uint32_t bn[TFP_GRAFT_BODY_NODES], bl[TFP_GRAFT_BODY_NODES], fn[TFP_GRAFT_RIG_NODES], fl[TFP_GRAFT_RIG_NODES], parts[30];
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES], fp[TFP_GRAFT_RIG_NODES];
  uint32_t body_count, fp_count;
  uintptr_t bnames, blinks, fnames, flinks, fworlds, locals;
  d.bound = false;
  if (!u32(u->rig + L_UNIT_NODES, &fp_count) || fp_count != TFP_GRAFT_RIG_NODES || !ptr(u->rig + L_UNIT_NAMES, &fnames) ||
      !ptr(u->rig + L_UNIT_LINKS, &flinks) || !ptr(u->rig + L_UNIT_WORLDS, &fworlds) || !ptr(u->rig + L_UNIT_LOCALS, &locals) ||
      !rd(fnames, fn, sizeof fn) || !rd(flinks, fl, sizeof fl) || !rd(fworlds, fp, sizeof fp))
    return false;
  if (d.bend) {
    /* The test bend needs the rig only: the retired map's first joint, node 10, is l_shoulder under the root. */
    if (fn[TFP_GRAFT_RIG_ROOT] != TFP_GRAFT_NAME_RIG_ROOT || fn[10] != TFP_GRAFT_NAME_L_SHOULDER || fl[10] != ((7u << 16) | 1u)) return false;
    memset(&d.map, 0, sizeof d.map);
    d.map.fp[0] = 10, d.map.parent[0] = 7;
    d.joints = 1;
  } else {
    if (!u32(u->body + L_UNIT_NODES, &body_count) || body_count != TFP_GRAFT_BODY_NODES || !ptr(u->body + L_UNIT_NAMES, &bnames) ||
        !ptr(u->body + L_UNIT_LINKS, &blinks) || !rd(bnames, bn, sizeof bn) || !rd(blinks, bl, sizeof bl) || !body_matrices(u->body, body) ||
        !tfp_graft_bind(&d.map, bn, bl, fn, fl, body, fp))
      return false;
    d.joints = TFP_GRAFT_JOINTS;
  }
  /* the avatar's appearance rows: three armour layers, slots 6 and 7 of each are the arms */
  if (!rd(u->avatar_owner + B_AVATAR_PARTS + (uintptr_t)u->avatar_index * 0x1b8 + 0xdc, parts, sizeof parts)) return false;
  d.arm_count = 0;
  for (unsigned group = 0; group < 3; ++group)
    for (unsigned side = 6; side <= 7; ++side) {
      uint32_t ref = parts[group * 10 + side];
      if (!ref || ref == UINT32_MAX) continue;
      unsigned i = 0;
      while (i < d.arm_count && d.arms[i] != ref) ++i;
      if (i == d.arm_count) d.arms[d.arm_count++] = ref;
    }
  d.units = *u, d.locals = locals, d.bound_bend = d.bend, d.aligned = false, d.bound = true;
  d.stats.binds++;
  return true;
}

/* ---- one frame (graft:662-817 graft_present, its steady action path) ---- */
static uint32_t drive(const Units *u) {
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES], poses[TFP_GRAFT_JOINTS];
  static TfpGraftLocal current[TFP_GRAFT_RIG_NODES], base[TFP_GRAFT_JOINTS], target[TFP_GRAFT_JOINTS];
  uintptr_t locals, rig_worlds;
  Api a;
  if (!api(&a)) return TFP_ARM_API;
  if (d.owned && (!d.bound || !same_units(u, &d.units) || d.bound_bend != d.bend)) {
    /* What is written belongs to another binding: the game's values go back before anything is bound. */
    d.bound = false;
    if (!restore()) return TFP_ARM_RESTORE;
  }
  if ((!d.bound || !same_units(u, &d.units) || d.bound_bend != d.bend) && !bind(u)) return TFP_ARM_BIND;
  if (!ptr(u->rig + L_UNIT_LOCALS, &locals) || locals != d.locals || !ptr(u->rig + L_UNIT_WORLDS, &rig_worlds) || !rd(locals, current, sizeof current) ||
      (!d.bend && !body_matrices(u->body, body)))
    return TFP_ARM_READ;
  for (unsigned i = 0; i < d.joints; ++i)
    base[i] = tfp_graft_underlying((d.owned >> i) & 1, &d.original[i], &d.applied[i], current[d.map.fp[i]]);
  if (d.bend) {
    target[0] = tfp_graft_bend_z(base[0], BEND_DEGREES);
  } else {
    /* 0.3.37: on entry the body's socket is aligned to the weapon socket being shown on this rig */
    if (!d.aligned && !tfp_graft_align(&d.map, body, &base[TFP_GRAFT_JOINTS - 1])) return TFP_ARM_POSE;
    d.aligned = true;
    if (!tfp_graft_pose(&d.map, body, poses)) return TFP_ARM_POSE;
    for (unsigned i = 0; i < d.joints; ++i)
      if (!tfp_graft_mix(&base[i], &poses[i], d.weight, &target[i])) return TFP_ARM_POSE;
  }
  Tree tree = {0};
  bool changed = false;
  for (unsigned i = 0; i < d.joints; ++i) {
    unsigned node = d.map.fp[i];
    if (!memcmp(&current[node], &target[i], 48)) continue;
    if (!changed) {
      /* gun_away (a hold): the gun is not this rig's to refresh; the tree stays empty, here and when the values go back */
      if (!u->gun_away && !collect(u->world, u->rig, u->weapon, u->weapon_ref, 0, &tree)) return TFP_ARM_TREE;
      d.attachments = tree;
      d.own_world = u->world, d.own_rig = u->rig, d.own_locals = locals, d.own_worlds = rig_worlds, d.own_ref = u->rig_ref;
      memcpy(d.own_arms, d.arms, sizeof d.own_arms), d.own_arm_count = d.arm_count;
    }
    d.original[i] = base[i], d.applied[i] = target[i], d.own_node[i] = node, d.owned |= UINT64_C(1) << i;
    changed = true;
    if (!d.host.set_local(d.host.user, a.set_local, u->rig_ref, node, &target[i])) return TFP_ARM_FAULT;
    d.stats.joints++;
  }
  if (changed) {
    if (!update(&a, u->world, u->rig_ref) || !refresh_arms(&a, u->world, u->rig, rig_worlds, d.arms, d.arm_count)) return TFP_ARM_FAULT;
    int weapon = u->gun_away ? 1 : update_weapon(&a, u->world, &tree);
    if (weapon <= 0) return weapon ? TFP_ARM_FAULT : TFP_ARM_TREE;
  }
  return TFP_ARM_NONE;
}

/* ---- the interface ---- */
void tfp_arm_drive_set_host(const TfpArmDriveHost *host) { d.host = *host, d.has_host = true; }
void tfp_arm_drive_set(bool on, bool restore_after_draw, float weight) {
  d.on = on, d.restore_after = restore_after_draw;
  d.weight = weight >= 0 && weight <= 1 ? weight : weight > 1 ? 1 : 0; /* a NaN: 0, the game's own pose */
}
void tfp_arm_drive_set_bend(bool on) { d.bend = on; }
void tfp_arm_drive_set_hold(bool on) { d.hold = on; }
static void refused(uint32_t reason) { d.stats.refused++, d.stats.reason = reason; }

bool tfp_arm_drive_render(uint64_t world, bool game_thread) {
  if (!d.has_host || (!d.on && !d.owned)) return false;
  if (!game_thread) {
    if (d.on) refused(TFP_ARM_THREAD);
    return false;
  }
  uint32_t reason = TFP_ARM_NONE;
  bool native_view = false;
  if (d.on) {
    static TfpSnapshot s;
    Units u;
    if (tfp_snapshot(mem(), TFP_SNAPSHOT_FULL, &s, NULL, 0)) {
      native_view = s.camera_kind == 6;
      if (native_view) reason = prove((uintptr_t)world, &s, d.hold, &u);
    } else {
      /* Not provable in full (no weapon, a request for another weapon): a refusal only in the native view of the local avatar. */
      native_view = tfp_snapshot(mem(), TFP_SNAPSHOT_PLAYER, &s, NULL, 0) && s.camera_kind == 6;
      reason = TFP_ARM_SNAPSHOT;
      /* In a hold that is the expected state (a stow, a throw, an injection): the player's snapshot has the body,
       * the rig and the camera request; it names no weapon unit, so the gun counts as away. */
      if (native_view && d.hold) reason = prove((uintptr_t)world, &s, true, &u);
    }
    if (native_view && reason == TFP_ARM_NONE) reason = drive(&u);
    if (native_view && reason == TFP_ARM_NONE) {
      d.stats.frames++;
      /* In a hold the game's values go back after every draw, the retired mod's path for weapon draws and holsters
       * (0.3.46, graft:186-209), not the accepted leave-written path. Why: the leave-written path recovers the
       * game's value of a joint on the next frame from the record it finds there, which is right only while the
       * game keeps animating the same rig with the same gun on it. During a stow, a draw, a throw or an injection
       * the game re-parents the gun and may stop writing some joints or replace the rig unit between two frames;
       * a value of this mod left in the rig would then be taken for the game's, and the gun's units would be
       * evaluated against it by the game's own update. Put back at once, the rig holds only the game's values
       * whenever the game looks at it. (That path's acceptance in the game was still pending when the mod it
       * comes from was retired: this is a probe.) */
      return (d.restore_after || d.hold) && d.owned != 0;
    }
    if (native_view) refused(reason);
    else d.stats.idle++;
  }
  /* Not driven: nothing of the binding is kept, and what is written goes back to the game's values. */
  d.bound = false;
  restore();
  return false;
}
void tfp_arm_drive_after_render(void) {
  if (d.has_host && d.owned) restore();
}
bool tfp_arm_drive_release(void) {
  if (!d.has_host || !d.owned) return true;
  d.bound = false;
  return restore();
}
const char *tfp_arm_drive_held(void) { return d.owned ? "the arm drive has not put the first-person rig's own joint poses back" : NULL; }
void tfp_arm_drive_stats(TfpArmDriveStats *out) {
  *out = d.stats;
  out->owned = bits(d.owned);
}
const char *tfp_arm_drive_reason_name(uint32_t reason) {
  static const char *const names[TFP_ARM_REASONS] = {
      "none", "not the game thread", "player or weapon not proven", "ragdoll", "body or rig unit", "another world",
      "gun not on the rig's socket", "camera not on the rig's eye", "engine setter or tables differ", "joint map did not bind",
      "pose not readable", "pose not rigid", "weapon units", "engine call faulted", "game values not put back yet"};
  return reason < TFP_ARM_REASONS ? names[reason] : "?";
}
void tfp_arm_drive_reset_for_tests(void) { memset(&d, 0, sizeof d); }

/* ---- the hold's log line (arm_drive.h); reads only ---- */
void tfp_arm_drive_probe(uint32_t gun, TfpArmDriveProbe *out) {
  static TfpSnapshot s;
  uintptr_t body, rig, weapon, parent = 0, camera, links, worlds;
  uint32_t camera_ref, link;
  TfpGraftMatrix m;
  memset(out, 0, sizeof *out);
  if (!d.has_host || !tfp_snapshot(mem(), TFP_SNAPSHOT_PLAYER, &s, NULL, 0)) return;
  out->proven = true;
  body = unit(s.body), rig = unit(s.viewmodel), weapon = gun ? unit(gun) : 0;
  if (!weapon || !rd(weapon + L_UNIT_PARENT, &parent, sizeof parent) || !parent) out->gun_parent = "none";
  else out->gun_parent = parent == rig ? "rig" : parent == body ? "body" : "other";
  if (u32(s.context + L_CONTEXT_CAMERA_UNIT, &camera_ref) && (camera = unit(camera_ref)) != 0) {
    out->has_camera = true;
    out->camera_parent_is_rig_eye = rig && rd(camera + L_UNIT_PARENT, &parent, sizeof parent) && parent == rig && ptr(camera + L_UNIT_LINKS, &links) &&
                                    u32(links, &link) && link == (((uint32_t)TFP_GRAFT_RIG_EYE << 16) | 2u);
    if (ptr(camera + L_UNIT_WORLDS, &worlds) && rd(worlds, &m, sizeof m))
      out->has_eye = true, out->eye[0] = m.values[12], out->eye[1] = m.values[13], out->eye[2] = m.values[14];
  }
  if (rig && ptr(rig + L_UNIT_WORLDS, &worlds) && rd(worlds + 64 * (uintptr_t)TFP_GRAFT_RIG_EYE, &m, sizeof m))
    out->has_rig_eye = true, out->rig_eye[0] = m.values[12], out->rig_eye[1] = m.values[13], out->rig_eye[2] = m.values[14];
}
