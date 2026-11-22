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
 * attachments (view:373-402). The drive runs through the whole native view instead of per action.
 *
 * The alignment (0.3.37's, graft:293-303: the body's weapon socket made to land on the socket the rig shows) was
 * taken once, on the first driven frame of a binding. After a weapon swap that frame is in the middle of the draw,
 * and the offset between a hand on its way and the rig's resting socket stayed for the whole binding: the rifle
 * rested in another place after a swap than before it (0.6.0-feel.25, seen in the game). The retired mod never
 * aligned on such a frame: it aligned at an action's start against the hip pose it had displayed, and kept a resting
 * reference per gun that was sampled only after 350 ms of displayed hip (graft:267-290). Here the same condition is
 * put on the alignment itself: "the resting weapon pose" below, and arm_drive.h tfp_arm_drive_set_rest. One
 * measurement at rest is not kept either: what the tick can call a rest includes a pause in an ammo check, a melee
 * or a reload, so a weapon's resting alignment is the one a second measurement confirms.
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
#include <stdio.h>

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
/* The resting weapon pose (arm_drive.h tfp_arm_drive_set_rest). A measurement: at rest for REST_MS (graft:269, the
 * retired resting hip's 350 ms) with both hands within STILL_* and the same layer states; one per stretch of
 * stillness. Kept: when the next one agrees within STILL_*, or, with nothing kept, when the stretch lasts LONG_MS. A
 * kept one is replaced only by two in a row that agree and are more than FAR_* from the one it was measured at, with
 * the body more than FAR_* away from them in between (the retired mod sampled once per gun and never again). A
 * change is eased over EASE_MS.
 * Where the numbers are from: the retired mod's recording of a standing player (120 pose pairs in 9.0 s, 75 ms
 * apart; arm_drive.h). In it the body's socket, in the head's frame, ranges over 30 mm and 8 degrees; ten stretches
 * reach 350 ms, the longest 1.1 s; measurements of neighbouring stretches are 5 to 16 mm and 1.3 to 5.2 degrees
 * apart, the first two 7.6 mm and 1.8 degrees: kept at 1.9 s; no later pair is both in agreement and FAR from it. */
enum { REST_MS = 350, LONG_MS = 2000, EASE_MS = 150, KEPT = 8 };
static const float STILL_M = .015f, STILL_DEGREES = 2, FAR_M = .030f, FAR_DEGREES = 4;
static const double REST_SPEED = .05, REST_PITCH = .349066; /* m/s; 20 degrees */
typedef struct Kept { /* one weapon's resting alignment in one stance, for this body in this world */
  bool has;
  uintptr_t world, body;
  uint32_t body_ref, weapon, key;
  unsigned stance;
  TfpGraftMatrix grip, alignment;
  TfpGraftMatrix source; /* the body's socket in the head's frame when it was measured */
} Kept;
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
  bool hold, ready; /* tfp_arm_drive_set_hold, tfp_arm_drive_set_hold_ready */
  float weight;
  bool retake; /* the binding's provisional alignment was taken while the copy was hidden or fading (the player aiming): align() */
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
  /* the resting weapon pose */
  uint64_t now;  /* the tick's clock; 0: never told, and nothing below happens */
  unsigned rest; /* TFP_ARM_REST_*: what the tick says of this frame */
  uint32_t key;  /* the body's layer states, as the tick says; 0: not known */
  Kept kept[KEPT];
  unsigned kept_next;
  Kept last; /* the last measurement, while it is neither kept nor refuted: the next one confirms it or takes its place */
  bool left; /* since then the body's socket has been more than FAR_* from where it was measured */
  /* of the binding: whose alignment is in use (used: 1 + its index in kept, 0: a provisional or tentative one), what
   * it is going to (the map holds the eased value while easing), and how long the body's socket has been still */
  uint32_t cur_weapon;
  unsigned cur_stance, used;
  bool easing, still;
  bool measured, measured_long; /* of this stretch of stillness: its measurement is taken; its LONG_MS one as well */
  bool tentative;               /* the alignment in use is a single measurement of this weapon and stance, taken at tentative_at */
  uint32_t still_key;
  uint64_t ease_at, still_since;
  TfpGraftMatrix from_grip, from_alignment, to_grip, to_alignment, still_at[2]; /* the socket, the left hand */
  TfpGraftMatrix tentative_at;
  TfpArmDriveRest seen; /* tfp_arm_drive_rest_take */
  unsigned why;
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
/* The gun's own conditions are not asked: in a hold, and while ready for one (arm_drive.h). */
static bool lenient(void) { return d.hold || d.ready; }
static bool same_units(const Units *a, const Units *b) {
  /* In a hold the gun is not part of what a binding is for: the map joins the body and the rig, and the entry
   * alignment is the rig's own socket. So the gun leaving, returning or being another does not unbind. */
  const bool any_gun = lenient();
  return a->world == b->world && a->body == b->body && a->rig == b->rig && (any_gun || a->weapon == b->weapon) && a->body_ref == b->body_ref &&
         a->rig_ref == b->rig_ref && (any_gun || a->weapon_ref == b->weapon_ref) && a->avatar_owner == b->avatar_owner &&
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

/* ---- the resting weapon pose (arm_drive.h tfp_arm_drive_set_rest) ---- */
static void apart(const TfpGraftMatrix *a, const TfpGraftMatrix *b, float *m, float *degrees) {
  float distance = 0, trace = 0;
  for (unsigned i = 0; i < 3; ++i) {
    distance += (a->values[12 + i] - b->values[12 + i]) * (a->values[12 + i] - b->values[12 + i]);
    for (unsigned j = 0; j < 3; ++j) trace += a->values[4 * i + j] * b->values[4 * i + j];
  }
  const float c = (trace - 1) / 2;
  *m = sqrtf(distance), *degrees = acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
}
/* Rotation the shorter way round, position straight (tfp_graft_mix). false: not rigid. */
static bool mix(const TfpGraftMatrix *from, const TfpGraftMatrix *to, float weight, TfpGraftMatrix *out) {
  TfpGraftLocal a = {0}, b;
  tfp_graft_set_local(&a, from);
  if (!tfp_graft_mix(&a, to, weight, &b)) return false;
  *out = tfp_graft_local_matrix(&b);
  return true;
}
/* The rig's socket local an alignment shows for the body's socket `source` (tfp_graft_pose's three lines). */
static TfpGraftMatrix socket_of(const TfpGraftMatrix *grip, const TfpGraftMatrix *alignment, const TfpGraftMatrix *source) {
  TfpGraftMatrix m = tfp_graft_product(source, alignment);
  return tfp_graft_product(grip, &m);
}
/* any: no alignment for this stance, then the weapon's standing one, then any of the weapon's. */
static Kept *kept_find(const Units *u, uint32_t weapon, unsigned stance, bool any) {
  Kept *other = NULL;
  for (Kept *k = d.kept; k < d.kept + KEPT; ++k) {
    if (!k->has || k->world != u->world || k->body != u->body || k->body_ref != u->body_ref || k->weapon != weapon) continue;
    if (k->stance == stance) return k;
    if (any && (!other || k->stance == TFP_ARM_REST_STAND)) other = k;
  }
  return other;
}
static void use(const TfpGraftMatrix *grip, const TfpGraftMatrix *alignment, bool eased) {
  /* nothing to ease to what is already held (a measurement confirmed in place) */
  d.easing = eased && d.now && (d.easing || memcmp(grip, &d.map.grip, sizeof *grip) || memcmp(alignment, &d.map.alignment, sizeof *alignment));
  if (d.easing) d.from_grip = d.map.grip, d.from_alignment = d.map.alignment, d.ease_at = d.now; /* from what is shown, an ease under way included */
  else d.map.grip = *grip, d.map.alignment = *alignment;
  d.to_grip = *grip, d.to_alignment = *alignment;
}
static void aligned(uint32_t how, uint32_t weapon, unsigned stance, uint64_t rest_ms, float m, float degrees) {
  d.stats.align = (TfpArmDriveAlign){.count = d.stats.align.count + 1, .how = how, .weapon = weapon, .stance = stance,
                                     .rest_ms = rest_ms > UINT32_MAX ? UINT32_MAX : (uint32_t)rest_ms, .key = d.key, .moved_m = m, .moved_degrees = degrees,
                                     .eased = d.easing};
}
static bool within(const TfpGraftMatrix *a, const TfpGraftMatrix *b, float limit_m, float limit_degrees) {
  float m, degrees;
  apart(a, b, &m, &degrees);
  return m <= limit_m && degrees <= limit_degrees; /* a NaN: not within */
}
/* The body is at rest and has been still for long enough: one measurement of this stretch of stillness. whole: the
 * stretch has lasted LONG_MS and nothing is kept for the weapon. source, shown: the body's socket in the head's frame,
 * the rig's own socket local. */
static void measure(const Units *u, const TfpGraftMatrix *body, const TfpGraftLocal *base, const TfpGraftMatrix *source, const TfpGraftMatrix *shown,
                    uint32_t weapon, bool whole) {
  TfpGraft fresh = d.map;
  float m, degrees;
  fresh.grip = d.to_grip;
  tfp_graft_grip(&fresh, body, base, &fresh.grip); /* not rigid: the grip in use stays */
  if (!tfp_graft_align(&fresh, body, &base[TFP_GRAFT_JOINTS - 1])) return; /* tried again on the next frame */
  d.measured = true, d.measured_long = whole;
  d.seen.measured++;
  const Kept now = {.has = true, .world = u->world, .body = u->body, .body_ref = u->body_ref, .weapon = weapon, .key = d.key, .stance = d.rest,
                    .grip = fresh.grip, .alignment = fresh.alignment, .source = *source};
  Kept *k = kept_find(u, weapon, d.rest, false);
  /* Within the sway of the rest the kept one was measured in: nothing to correct, and nothing to confirm later. */
  if (k && within(source, &k->source, FAR_M, FAR_DEGREES)) {
    d.last.has = false;
    return;
  }
  const bool agrees = d.last.has && d.last.world == now.world && d.last.body == now.body && d.last.body_ref == now.body_ref && d.last.weapon == weapon &&
                      d.last.stance == now.stance && d.last.key == now.key && within(source, &d.last.source, STILL_M, STILL_DEGREES);
  const TfpGraftMatrix in_use = socket_of(&d.to_grip, &d.to_alignment, source);
  apart(&in_use, shown, &m, &degrees);
  /* In place of a kept one, only a pose the body left and came back to: the sway of a held pose (an ammo check held
   * for seconds) ends one stretch of stillness and begins the next in the same place, and that is one visit. */
  if (k && agrees && !d.left) return;
  if (!agrees && !(whole && !k)) {
    /* Once: remembered for the next one. Shown only where nothing better is: nothing kept, and no tentative one from
     * about this place already. */
    d.last = now, d.left = false;
    if (k || (d.tentative && within(source, &d.tentative_at, FAR_M, FAR_DEGREES))) return;
    use(&now.grip, &now.alignment, true);
    d.tentative = true, d.tentative_at = *source, d.used = 0;
    aligned(TFP_ARM_ALIGN_TENTATIVE, weapon, d.rest, d.now - d.still_since, m, degrees);
    return;
  }
  if (!k) {
    for (k = d.kept; k < d.kept + KEPT && k->has; ++k) {}
    if (k == d.kept + KEPT) k = &d.kept[d.kept_next++ % KEPT];
  }
  *k = now;
  d.last.has = false, d.tentative = false;
  use(&k->grip, &k->alignment, true);
  d.used = (unsigned)(k - d.kept) + 1;
  aligned(TFP_ARM_ALIGN_REST, weapon, d.rest, d.now - d.still_since, m, degrees);
}
/* Each driven frame, before the pose: which alignment the map holds. base: the game's own values of the 42 joints. */
static bool align(const Units *u, const TfpGraftMatrix *body, const TfpGraftLocal *base) {
  const TfpGraftLocal *own = &base[TFP_GRAFT_JOINTS - 1];
  const uint32_t weapon = u->gun_away ? 0 : u->weapon_ref;
  float m = 0, degrees = 0;
  if (d.aligned && d.retake && d.weight >= 1) {
    /* The first driven frame the copy is fully shown again. The provisional alignment was taken in the aim pose, where
     * body and rig differ most (src/arm_weight.h), and shown it leaves the rifle displaced at rest until a rest is
     * measured, which a player who keeps moving never gives. Unless a kept or a measured one replaced it meanwhile,
     * it is taken again from this frame (as good as any other provisional one) and eased in from what is shown. */
    d.retake = false;
    if (!d.used && !d.tentative) {
      TfpGraft fresh = d.map;
      fresh.grip = d.to_grip;
      if (tfp_graft_align(&fresh, body, own)) {
        const TfpGraftMatrix anchor = tfp_graft_anchor(&d.map, body);
        const TfpGraftMatrix source = tfp_graft_relative(&body[d.map.body[TFP_GRAFT_JOINTS - 1]], &anchor);
        if (tfp_graft_rigid(&source)) {
          const TfpGraftMatrix before = socket_of(&d.to_grip, &d.to_alignment, &source), after = socket_of(&fresh.grip, &fresh.alignment, &source);
          apart(&after, &before, &m, &degrees);
        }
        use(&fresh.grip, &fresh.alignment, true);
        aligned(TFP_ARM_ALIGN_PROVISIONAL, weapon, d.rest, 0, m, degrees);
        m = degrees = 0;
      } else d.retake = true; /* not rigid on this frame: the next one */
    }
  }
  if (!d.aligned) {
    /* A new binding. This frame may be in the middle of a draw or of any action: what was measured at rest for this
     * weapon is used from the start. With nothing kept, 0.3.37's entry alignment, until the weapon's first rest. */
    Kept *k = weapon ? kept_find(u, weapon, d.rest, true) : NULL;
    if (k) d.map.grip = k->grip, d.map.alignment = k->alignment;
    else if (!tfp_graft_align(&d.map, body, own)) return false;
    use(&d.map.grip, &d.map.alignment, false);
    d.aligned = true, d.still = false, d.tentative = false, d.used = k ? (unsigned)(k - d.kept) + 1 : 0, d.cur_weapon = weapon, d.cur_stance = d.rest;
    aligned(k ? TFP_ARM_ALIGN_KEPT : TFP_ARM_ALIGN_PROVISIONAL, weapon, d.rest, 0, 0, 0);
    d.retake = !k && d.weight < 1; /* provisional, and taken while the copy is hidden or fading: once more when it is fully shown */
  }
  const TfpGraftMatrix anchor = tfp_graft_anchor(&d.map, body);
  const TfpGraftMatrix source = tfp_graft_relative(&body[d.map.body[TFP_GRAFT_JOINTS - 1]], &anchor), shown = tfp_graft_local_matrix(own);
  const TfpGraftMatrix left = tfp_graft_relative(&body[d.map.body[13 - 10]], &anchor); /* l_hand: it works while the gun hand waits */
  if (!tfp_graft_rigid(&source) || !tfp_graft_rigid(&shown) || !tfp_graft_rigid(&left)) return false;
  const unsigned stance = d.rest ? d.rest : d.cur_stance; /* an action between two rests keeps the stance it began in */
  if (weapon && (weapon != d.cur_weapon || stance != d.cur_stance)) {
    /* Another gun on a binding a lease keeps, or another stance: what is kept for it, eased from what is shown.
     * With nothing kept the alignment in use stays; for another gun it then counts as provisional. */
    const bool other_gun = weapon != d.cur_weapon;
    Kept *k = kept_find(u, weapon, stance, other_gun);
    const unsigned index = k ? (unsigned)(k - d.kept) + 1 : 0;
    if (k && index != d.used) {
      const TfpGraftMatrix before = socket_of(&d.to_grip, &d.to_alignment, &source), after = socket_of(&k->grip, &k->alignment, &source);
      apart(&after, &before, &m, &degrees);
      use(&k->grip, &k->alignment, true);
      d.used = index;
      aligned(TFP_ARM_ALIGN_KEPT, weapon, stance, 0, m, degrees);
    } else if (!k && other_gun) {
      d.used = 0;
      aligned(TFP_ARM_ALIGN_PROVISIONAL, weapon, stance, 0, 0, 0);
    }
    d.cur_weapon = weapon, d.cur_stance = stance, d.still = false, d.tentative = false;
  }
  if (d.last.has && !within(&source, &d.last.source, FAR_M, FAR_DEGREES)) d.left = true;
  const bool resting = d.now && d.rest && weapon && !d.hold;
  if (resting) {
    if (!d.still || d.key != d.still_key || !within(&source, &d.still_at[0], STILL_M, STILL_DEGREES) || !within(&left, &d.still_at[1], STILL_M, STILL_DEGREES))
      d.still = true, d.measured = d.measured_long = false, d.still_at[0] = source, d.still_at[1] = left, d.still_since = d.now, d.still_key = d.key;
    else if (!d.measured ? d.now - d.still_since >= REST_MS : !d.measured_long && d.now - d.still_since >= LONG_MS && !kept_find(u, weapon, d.rest, false))
      measure(u, body, base, &source, &shown, weapon, d.measured);
  } else d.still = false;
  if (d.now && weapon) { /* for the log (tfp_arm_drive_rest_take) */
    const TfpGraftMatrix in_use = socket_of(&d.to_grip, &d.to_alignment, &source);
    const uint64_t still = d.still ? d.now - d.still_since : 0;
    apart(&in_use, &shown, &m, &degrees);
    if (!d.seen.frames++ || m < d.seen.least_off_m) d.seen.least_off_m = m;
    if (m > d.seen.most_off_m) d.seen.most_off_m = m;
    if (degrees > d.seen.most_off_degrees) d.seen.most_off_degrees = degrees;
    d.seen.rest_frames += resting, d.seen.weapon = weapon, d.seen.key = d.key, d.seen.rest = d.rest, d.seen.off_m = m, d.seen.off_degrees = degrees;
    d.seen.still_ms = still > UINT32_MAX ? UINT32_MAX : (uint32_t)still;
    if (d.seen.still_ms > d.seen.longest_still_ms) d.seen.longest_still_ms = d.seen.still_ms;
    d.seen.how = d.used ? TFP_ARM_ALIGN_KEPT : d.tentative ? TFP_ARM_ALIGN_TENTATIVE : TFP_ARM_ALIGN_PROVISIONAL;
  }
  if (d.easing) {
    const float t = (float)(d.now - d.ease_at) / EASE_MS;
    if (!(t < 1) || !mix(&d.from_grip, &d.to_grip, t * t * (3 - 2 * t), &d.map.grip) ||
        !mix(&d.from_alignment, &d.to_alignment, t * t * (3 - 2 * t), &d.map.alignment))
      d.map.grip = d.to_grip, d.map.alignment = d.to_alignment, d.easing = false;
  }
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
    if (!align(u, body, base)) return TFP_ARM_POSE;
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
void tfp_arm_drive_set_hold_ready(bool on) { d.ready = on; }
void tfp_arm_drive_set_rest(uint64_t now_ms, unsigned rest, uint32_t pose_key) {
  d.now = now_ms, d.rest = rest < TFP_ARM_REST_KINDS ? rest : TFP_ARM_REST_NONE, d.key = pose_key;
}
void tfp_arm_drive_set_rest_why(unsigned why) { d.why = why < TFP_ARM_WHYS ? why : TFP_ARM_WHY_ACTION; }
unsigned tfp_arm_drive_rest_of(const TfpArmRestSample *s, uint32_t *pose_key, unsigned *why) {
  /* each comparison is false for a NaN, which is then not a rest */
  const unsigned reason = !s->stance || s->stance >= TFP_ARM_REST_KINDS ? TFP_ARM_WHY_ACTION : s->fire ? TFP_ARM_WHY_FIRE
                          : !s->has_speed || !(s->speed <= REST_SPEED) ? TFP_ARM_WHY_MOVING
                          : !s->has_pitch || !(s->pitch >= -REST_PITCH && s->pitch <= REST_PITCH) ? TFP_ARM_WHY_PITCH : TFP_ARM_WHY_REST;
  if (why) *why = reason;
  /* 10 bits a layer (a state is -1, none, or an index into the layer's states: 78 is the highest this mod names);
   * the top bit keeps a known triple from being 0 */
  if (pose_key) *pose_key = reason || !s->has_layers ? 0 : UINT32_C(0x80000000) | ((uint32_t)(s->l1 + 1) & 0x3ff) << 20 |
                                                             ((uint32_t)(s->l3 + 1) & 0x3ff) << 10 | ((uint32_t)(s->l14 + 1) & 0x3ff);
  return reason ? TFP_ARM_REST_NONE : s->stance;
}
void tfp_arm_drive_rest_take(TfpArmDriveRest *out) {
  *out = d.seen;
  out->why = d.why;
  memset(&d.seen, 0, sizeof d.seen);
}
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
      if (native_view) reason = prove((uintptr_t)world, &s, lenient(), &u);
    } else {
      /* Not provable in full (no weapon, a request for another weapon): a refusal only in the native view of the local avatar. */
      native_view = tfp_snapshot(mem(), TFP_SNAPSHOT_PLAYER, &s, NULL, 0) && s.camera_kind == 6;
      reason = TFP_ARM_SNAPSHOT;
      /* In a hold that is the expected state (a stow, a throw, an injection): the player's snapshot has the body,
       * the rig and the camera request; it names no weapon unit, so the gun counts as away. */
      if (native_view && lenient()) reason = prove((uintptr_t)world, &s, true, &u);
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
       * comes from was retired: this is a probe.)
       * The same in a frame whose gun is away while only ready: that is an action's first drawn frame, before the
       * tick that makes the controller hold. */
      return (d.restore_after || d.hold || u.gun_away) && d.owned != 0;
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
static const char *const stances[TFP_ARM_REST_KINDS] = {"in an action", "standing", "crouched", "prone"};
static const char *layers(uint32_t key, char out[24]) {
  if (!key) return "not known";
  snprintf(out, 24, "%d:%d:%d", (int)(key >> 20 & 0x3ff) - 1, (int)(key >> 10 & 0x3ff) - 1, (int)(key & 0x3ff) - 1);
  return out;
}
const char *tfp_arm_drive_align_line(const TfpArmDriveAlign *a) {
  static char line[280];
  char key[24];
  const char *stance = stances[a->stance < TFP_ARM_REST_KINDS ? a->stance : 0];
  if (a->how == TFP_ARM_ALIGN_REST || a->how == TFP_ARM_ALIGN_TENTATIVE)
    snprintf(line, sizeof line, "aligned (weapon %u, at rest for %u ms, layers %s, %s, moved %.3f m / %.1f degrees from the one in use%s); %s",
             (unsigned)a->weapon, (unsigned)a->rest_ms, layers(a->key, key),
             a->how == TFP_ARM_ALIGN_REST ? "confirmed: kept" : "measured once: shown, not kept yet", (double)a->moved_m, (double)a->moved_degrees,
             a->eased ? ", eased" : "", stance);
  else if (a->how == TFP_ARM_ALIGN_KEPT && a->eased)
    snprintf(line, sizeof line, "aligned (weapon %u, the rest alignment kept for it, moved %.3f m / %.1f degrees from the one in use, eased); %s",
             (unsigned)a->weapon, (double)a->moved_m, (double)a->moved_degrees, stance);
  else if (a->how == TFP_ARM_ALIGN_KEPT)
    snprintf(line, sizeof line, "aligned (weapon %u, the rest alignment kept for it, from the first frame of a new binding); %s", (unsigned)a->weapon, stance);
  else if (a->how == TFP_ARM_ALIGN_PROVISIONAL && a->eased)
    snprintf(line, sizeof line, "aligned (weapon %u, no rest alignment kept for it yet: the one taken while aiming is taken again now that the arms show, "
             "moved %.3f m / %.1f degrees from the one in use, eased; provisional until its first rest); %s",
             (unsigned)a->weapon, (double)a->moved_m, (double)a->moved_degrees, stance);
  else if (a->how == TFP_ARM_ALIGN_PROVISIONAL && a->weapon)
    snprintf(line, sizeof line, "aligned (weapon %u, no rest alignment kept for it yet: provisional until its first rest); %s", (unsigned)a->weapon, stance);
  else if (a->how == TFP_ARM_ALIGN_PROVISIONAL)
    snprintf(line, sizeof line, "aligned (no gun on the rig: the first driven frame's alignment, provisional); %s", stance);
  else snprintf(line, sizeof line, "no alignment yet");
  return line;
}
const char *tfp_arm_drive_rest_line(const TfpArmDriveRest *r) {
  static const char *const whys[TFP_ARM_WHYS] = {"at rest", "in an action", "the fire button is down", "moving", "looking up or down"};
  static char line[320];
  char key[24];
  snprintf(line, sizeof line, "%u frames, %u at rest (%s), layers %s, socket %.3f to %.3f m / up to %.1f degrees off the rig's own (now %.3f m / %.1f), "
           "still for %u ms (longest %u), %u measured, alignment %s",
           (unsigned)r->frames, (unsigned)r->rest_frames, r->rest && r->rest < TFP_ARM_REST_KINDS ? stances[r->rest] : whys[r->why < TFP_ARM_WHYS && r->why ? r->why : TFP_ARM_WHY_ACTION],
           layers(r->key, key), (double)r->least_off_m, (double)r->most_off_m, (double)r->most_off_degrees, (double)r->off_m, (double)r->off_degrees,
           (unsigned)r->still_ms, (unsigned)r->longest_still_ms, (unsigned)r->measured,
           r->how == TFP_ARM_ALIGN_KEPT ? "kept" : r->how == TFP_ARM_ALIGN_TENTATIVE ? "measured once" : "provisional");
  return line;
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
