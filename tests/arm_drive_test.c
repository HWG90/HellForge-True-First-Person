/* The arm drive (src/arm_drive.c) with no game: a fake player in the native first-person view
 * (tests/fake_native.h, the same fake the snapshot tests use), a body and a first-person rig holding a
 * frame recorded in the game (tests/reference/arm-graft/paired-poses.bin), sleeves, gloves, a gun with an
 * optic, and the engine's two functions replaced by recorders that write the fake's memory.
 *
 *   arm_drive_test FIXTURE */
#include "fake_native.h"
#include "arm_drive.h"
#include "arm_weight.h"
#include "offhand_grip.h"
#include "grip_fingers.h"
#include "snapshot.h"
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* ---- the fake's addresses ---- */
static const uintptr_t CTX = 0x300000000ULL, WORLD = 0x400000000ULL, AVATAR = 0x700000000ULL;
static const uintptr_t REGISTRY = 0xb00000000ULL, VTABLE = 0xb10000000ULL;
enum { BODY = 401, GUN = 501, OPTIC = 601, CAMERA = 701, RIG = 801, SLEEVE = 901, GLOVE = 902, BODY_SLEEVE = 903, LOOSE_SLEEVE = 904, NEW_RIG = 811 };
static uintptr_t object_of(uint32_t ref) { return 0x1000000000ULL + (uintptr_t)ref * 0x100000; }
static uintptr_t locals_of(uint32_t ref) { return object_of(ref) + 0x10000; }
static uintptr_t worlds_of(uint32_t ref) { return object_of(ref) + 0x20000; }
static uintptr_t links_of(uint32_t ref) { return object_of(ref) + 0x30000; }
static uintptr_t names_of(uint32_t ref) { return object_of(ref) + 0x40000; }
static FpMemory memory;
static uint64_t SET, UPDATE, FOREIGN_SET, FOREIGN_UPDATE, not_executable;

static const TfpGraftMatrix ID = {.values = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
static const TfpGraftLocal REST = {.basis = {1, 0, 0, 0, 1, 0, 0, 0, 1}, .preserved = {1, 1, 1, 0}};

/* ---- the recorded frame ---- */
static uint32_t bn[TFP_GRAFT_BODY_NODES], bl[TFP_GRAFT_BODY_NODES], fn[TFP_GRAFT_RIG_NODES], fl[TFP_GRAFT_RIG_NODES];
static TfpGraftMatrix body_now[TFP_GRAFT_BODY_NODES], body_later[TFP_GRAFT_BODY_NODES], rig_world[TFP_GRAFT_RIG_NODES];
/* The drive sees the body in the view's frame (tfp_graft_view with the rig's view node). What these tests expect is worked
 * out from the same bodies seen the same way: every graft call below that takes a body goes through seen(). */
static const TfpGraftMatrix *seen(const TfpGraftMatrix *body) {
  static TfpGraftMatrix copies[8][TFP_GRAFT_BODY_NODES]; static unsigned next;
  TfpGraftMatrix *copy = copies[next++ % 8];
  memcpy(copy, body, sizeof copies[0]);
  unsigned view;
  CHECK(tfp_graft_view_node(fl, &view) && view == 4);
  if (!tfp_graft_view(copy, &rig_world[view])) memcpy(copy, body, sizeof copies[0]); /* not rigid: as given, and refused as before */
  return copy;
}
#define tfp_graft_bind(out, bn, bl, fn, fl, body, fp) tfp_graft_bind(out, bn, bl, fn, fl, seen(body), fp)
#define tfp_graft_align(map, body, own) tfp_graft_align(map, seen(body), own)
#define tfp_graft_pose(map, body, pose) tfp_graft_pose(map, seen(body), pose)
#define tfp_graft_anchor(map, body) tfp_graft_anchor(map, seen(body))
static TfpGraftLocal game_locals[TFP_GRAFT_RIG_NODES]; /* what the game holds for the rig */
static void load(const char *path) {
  FILE *in = fopen(path, "rb");
  char magic[16];
  uint32_t head[4];
  if (!in) { fprintf(stderr, "%s: cannot be opened\n", path); exit(1); }
  CHECK(fread(magic, 1, 16, in) == 16 && !memcmp(magic, "TFP-ARM-GRAFT-1\n", 16) && fread(head, 4, 4, in) == 4 && head[0] >= 2);
  CHECK(fread(bn, 4, TFP_GRAFT_BODY_NODES, in) == TFP_GRAFT_BODY_NODES && fread(bl, 4, TFP_GRAFT_BODY_NODES, in) == TFP_GRAFT_BODY_NODES);
  CHECK(fread(fn, 4, TFP_GRAFT_RIG_NODES, in) == TFP_GRAFT_RIG_NODES && fread(fl, 4, TFP_GRAFT_RIG_NODES, in) == TFP_GRAFT_RIG_NODES);
  CHECK(!fseek(in, (3 * TFP_GRAFT_JOINTS + 4) * 4 + 128, SEEK_CUR));
  CHECK(fread(body_now, 64, TFP_GRAFT_BODY_NODES, in) == TFP_GRAFT_BODY_NODES && fread(rig_world, 64, TFP_GRAFT_RIG_NODES, in) == TFP_GRAFT_RIG_NODES);
  /* the next frame's body, for a body that moves: skip the rest of frame 0 */
  CHECK(!fseek(in, 16 + TFP_GRAFT_JOINTS * 64 + TFP_GRAFT_JOINTS * 64 + 3 * (16 + TFP_GRAFT_JOINTS * 64) + 16 + 64 + TFP_GRAFT_JOINTS * 64 + TFP_GRAFT_JOINTS * 64, SEEK_CUR));
  CHECK(fread(body_later, 64, TFP_GRAFT_BODY_NODES, in) == TFP_GRAFT_BODY_NODES);
  fclose(in);
  CHECK(memcmp(body_now, body_later, sizeof body_now));
  for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; n++) {
    game_locals[n] = REST;
    game_locals[n].preserved[3] = (float)n;
    if ((fl[n] & 65535) == 1) {
      TfpGraftMatrix local = tfp_graft_relative(&rig_world[n], &rig_world[fl[n] >> 16]);
      if (tfp_graft_rigid(&local)) tfp_graft_set_local(&game_locals[n], &local);
    }
  }
}

/* ---- units ---- */
static void block(uintptr_t at, size_t size) { static const uint8_t zero[8192]; put(at, zero, size); }
static void make_unit(uint32_t ref, uint32_t nodes, uintptr_t parent, uint32_t link0) {
  const uintptr_t object = object_of(ref);
  block(object, 0x400);
  p64(REGISTRY + 0x2000 + (uintptr_t)ref * 8, object);
  p64(object, VTABLE), n32(object + 8, ref), p64(object + 0x18, WORLD), n32(object + 0x70, nodes);
  p64(object + 0x80, locals_of(ref)), p64(object + 0x88, worlds_of(ref)), p64(object + 0x98, links_of(ref)), p64(object + 0xa0, names_of(ref));
  p64(object + 0x1d0, parent);
  if (parent) p64(object + 0x78, parent + 0x20000); /* the external array: the parent's world matrices */
  block(locals_of(ref), nodes * 64), block(worlds_of(ref), nodes * 64), block(links_of(ref), nodes * 4), block(names_of(ref), nodes * 4);
  for (uint32_t i = 0; i < nodes; i++) put(locals_of(ref) + i * 64, &REST, 64), put(worlds_of(ref) + i * 64, &ID, 64);
  n32(links_of(ref), link0);
}
static void request(uint32_t kind) {
  const uint32_t r[5] = {kind, 101, 201, 401, 0};
  put(CTX + 0x200, r, 20);
}
static void slots(uint32_t sleeve, uint32_t glove) {
  uint32_t s[30] = {0};
  s[6] = sleeve, s[7] = UINT32_MAX, s[16] = BODY_SLEEVE, s[17] = glove, s[26] = sleeve, s[27] = LOOSE_SLEEVE;
  put(AVATAR + 0x546a70 + 0xdc, s, sizeof s);
}
static void world_setup(void) {
  memory = setup();
  { const uint8_t amd64[2] = {0x64, 0x86}; put(memory.game + 68, amd64, 2); } /* the donor's fake header has no machine field */
  request(6);
  /* the unit registry and one vtable for every unit (src/backend.c fp_unit_object) */
  block(REGISTRY, 0x100), block(REGISTRY + 0x1000, 1024), block(REGISTRY + 0x2000, 8192), block(VTABLE, 0x100);
  p64(memory.exe + FP_REGISTRY, REGISTRY), n32(REGISTRY + 0x98, 1024), p64(REGISTRY + 0xa0, REGISTRY + 0x1000), p64(REGISTRY + 0x88, REGISTRY + 0x2000);
  p64(VTABLE + 0xf8, memory.exe + 0x2bd890), p64(VTABLE + 0xc8, memory.exe + 0x2c3990);
  /* the engine tables and the two pinned functions */
  SET = memory.exe + 0x1fcad0, UPDATE = memory.exe + 0x336530;
  const uintptr_t root = memory.exe + 0x27c8d80;
  block(root, 0x40);
  p64(memory.game + FP_API_ROOT, root), p64(root + 8, memory.exe + 0x27cab40), p64(root + 0x18, memory.exe + 0x27c8f10);
  p64(memory.exe + 0x27c8f10 + 0x60, SET), p64(memory.exe + 0x27cab40 + 0x100, UPDATE);
  static const uint8_t set_code[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20, 0x49, 0x8b, 0xd8, 0x48, 0x63, 0xfa};
  static const uint8_t update_code[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0x8b, 0xca, 0xe8, 0x80, 0x73, 0xd6, 0xff,
                                        0x48, 0x8b, 0xd0, 0x48, 0x8b, 0xcb, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0x70, 0xc8, 0xe7, 0xff};
  put(SET, set_code, sizeof set_code), put(UPDATE, update_code, sizeof update_code);
  FOREIGN_SET = memory.exe + 0x1fd000; /* another function that begins with the same bytes */
  put(FOREIGN_SET, set_code, sizeof set_code);
  FOREIGN_UPDATE = memory.exe + 0x337000;
  put(FOREIGN_UPDATE, update_code, sizeof update_code);
  /* the avatar's rows */
  { const uint8_t physics = 0; put(AVATAR + 0x547844, &physics, 1); }
  slots(SLEEVE, GLOVE);
  /* body, rig, gun on the rig's socket with an optic, the camera on the rig's eye, sleeves and gloves on the rig */
  make_unit(BODY, TFP_GRAFT_BODY_NODES, 0, 0);
  put(names_of(BODY), bn, sizeof bn), put(links_of(BODY), bl, sizeof bl), put(worlds_of(BODY), body_now, sizeof body_now);
  make_unit(RIG, TFP_GRAFT_RIG_NODES, 0, 0);
  put(names_of(RIG), fn, sizeof fn), put(links_of(RIG), fl, sizeof fl), put(worlds_of(RIG), rig_world, sizeof rig_world);
  put(locals_of(RIG), game_locals, sizeof game_locals);
  { const uint64_t resource = UINT64_C(0xd0cf4d493ca08bed); put(object_of(RIG) + 0x358, &resource, 8); }
  make_unit(GUN, 4, object_of(RIG), (8u << 16) | 2);
  make_unit(OPTIC, 2, object_of(GUN), (1u << 16) | 2);
  { const float off = .5f; put(worlds_of(OPTIC) + 48, &off, 4); } /* stale: not where the gun's node 1 puts it */
  p64(object_of(GUN) + 0x1d8, object_of(OPTIC)); /* first child; the optic has no sibling */
  make_unit(CAMERA, 1, object_of(RIG), (50u << 16) | 2);
  make_unit(SLEEVE, 8, object_of(RIG), (11u << 16) | 2);
  make_unit(GLOVE, 8, object_of(RIG), (13u << 16) | 2);
  make_unit(BODY_SLEEVE, 8, object_of(BODY), (20u << 16) | 2);
  make_unit(LOOSE_SLEEVE, 8, object_of(RIG), (31u << 16) | 2); /* a child of the rig whose external array is not the rig's */
  p64(object_of(LOOSE_SLEEVE) + 0x78, worlds_of(BODY));
}

/* ---- the engine's two functions ---- */
enum { CALLS = 1 << 17 };
static struct { uint32_t unit, node; bool update; } calls[CALLS];
static unsigned call_count, sets, updates, fault_set_at;
static bool is_executable(void *user, uint64_t at) {
  (void)user;
  return at != not_executable && (at == SET || at == UPDATE || at == FOREIGN_SET || at == FOREIGN_UPDATE);
}
static bool set_local(void *user, uint64_t function, uint32_t unit, uint32_t node, const TfpGraftLocal *local) {
  (void)user;
  CHECK(function == SET && node < TFP_GRAFT_RIG_NODES && node != TFP_GRAFT_RIG_ROOT && node != TFP_GRAFT_RIG_EYE && call_count < CALLS);
  if (fault_set_at && sets + 1 == fault_set_at) return false;
  calls[call_count].unit = unit, calls[call_count].node = node, calls[call_count++].update = false;
  sets++;
  put(locals_of(unit) + node * 64, local, 64); /* the engine writes the whole record */
  return true;
}
static bool update_unit(void *user, uint64_t function, uint64_t world, uint32_t unit) {
  (void)user;
  CHECK(function == UPDATE && world == WORLD && call_count < CALLS);
  calls[call_count].unit = unit, calls[call_count].node = 0, calls[call_count++].update = true;
  updates++;
  return true;
}
static void forget_calls(void) { call_count = sets = updates = 0; }
static TfpArmDriveStats stats(void) { TfpArmDriveStats s; tfp_arm_drive_stats(&s); return s; }
static void rig_locals(TfpGraftLocal out[TFP_GRAFT_RIG_NODES]) { CHECK(read_fake(&f, locals_of(RIG), out, TFP_GRAFT_RIG_NODES * 64)); }
static void fresh(void) {
  world_setup();
  tfp_arm_drive_reset_for_tests();
  const TfpArmDriveHost host = {NULL, memory, is_executable, set_local, update_unit};
  tfp_arm_drive_set_host(&host);
  forget_calls();
  fault_set_at = 0, not_executable = 0;
}

/* What the retired maths asks for: bound on the frame, the body's socket aligned to the rig's own socket
 * local on the first driven frame, then the pose of `body`, each joint blended from `base`. */
static TfpGraft bound_map(void) {
  TfpGraft map;
  CHECK(tfp_graft_bind(&map, bn, bl, fn, fl, body_now, rig_world));
  CHECK(tfp_graft_align(&map, body_now, &game_locals[TFP_GRAFT_RIG_SOCKET]));
  return map;
}
static void wanted(const TfpGraft *map, const TfpGraftMatrix *body, const TfpGraftLocal *base, float weight, TfpGraftLocal out[TFP_GRAFT_RIG_NODES]) {
  TfpGraftMatrix pose[TFP_GRAFT_JOINTS];
  CHECK(tfp_graft_pose(map, body, pose));
  memcpy(out, base, TFP_GRAFT_RIG_NODES * 64);
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) CHECK(tfp_graft_mix(&base[map->fp[i]], &pose[i], weight, &out[map->fp[i]]));
}
static unsigned differing(const TfpGraftLocal *a, const TfpGraftLocal *b) {
  unsigned n = 0;
  for (unsigned i = 0; i < TFP_GRAFT_RIG_NODES; i++) n += memcmp(&a[i], &b[i], 48) != 0;
  return n;
}

static void test_off_and_idle(void) {
  fresh();
  const unsigned reads = f.reads;
  CHECK(!tfp_arm_drive_render(WORLD, true) && f.reads == reads && !call_count); /* switched off: not one read */
  tfp_arm_drive_set(true, false, 1);
  request(4); /* the head camera, not the native view */
  CHECK(!tfp_arm_drive_render(WORLD, true) && !call_count);
  TfpArmDriveStats s = stats();
  CHECK(s.idle == 1 && !s.frames && !s.refused && !s.binds && !s.owned);
  CHECK(!tfp_arm_drive_held() && tfp_arm_drive_release());
}

/* Leaving the native view (the camera request is no longer kind 6) with joints written: the game's values go
 * back on that primary render, everything is refreshed, nothing is held, and it is not a refusal. Back in the
 * native view the map is bound again from nothing. */
static void test_leave_native_view(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 1 && stats().owned > 30);
  forget_calls();
  request(4);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  TfpArmDriveStats s = stats();
  CHECK(!memcmp(now, game_locals, sizeof now) && s.owned == 0 && s.restores == 1 && s.idle == 1 && !s.refused && s.frames == 1 && !tfp_arm_drive_held());
  CHECK(sets > 30 && updates == 5 && calls[sets].unit == RIG && calls[sets + 1].unit == SLEEVE && calls[sets + 2].unit == GLOVE &&
        calls[sets + 3].unit == GUN && calls[sets + 4].unit == OPTIC);
  forget_calls();
  CHECK(!tfp_arm_drive_render(WORLD, true) && !call_count && stats().idle == 2 && stats().restores == 1); /* still away: nothing more */
  request(6);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  s = stats();
  CHECK(s.frames == 2 && s.binds == 2 && s.owned > 30 && !s.refused);
}

/* Putting back keeps what the game wrote after this mod's write: one joint's X position and another joint's
 * whole basis, written by the game's animation between the driven frame and the put-back, stay; every other
 * value is the one saved before the write. The three ways out: switched off, release, after the draw. */
static void test_put_back_keeps_game_writes(void) {
  for (int way = 0; way < 3; way++) {
    TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES], expected[TFP_GRAFT_RIG_NODES];
    fresh();
    tfp_arm_drive_set(true, way == 2, 1);
    const TfpGraft map = bound_map();
    wanted(&map, body_now, game_locals, 1, want);
    CHECK(want[20].position[0] != game_locals[20].position[0] && memcmp(want[30].basis, game_locals[30].basis, 36));
    CHECK(tfp_arm_drive_render(WORLD, true) == (way == 2));
    rig_locals(now);
    CHECK(!memcmp(now, want, sizeof now));
    /* the game writes */
    const float x = game_locals[20].position[0] + .25f;
    const TfpGraftLocal turned = tfp_graft_bend_z(game_locals[30], 15);
    CHECK(x != want[20].position[0] && memcmp(turned.basis, want[30].basis, 36) && memcmp(turned.basis, game_locals[30].basis, 36));
    put(locals_of(RIG) + 20 * 64 + 36, &x, 4);
    put(locals_of(RIG) + 30 * 64, turned.basis, 36);
    memcpy(expected, game_locals, sizeof expected);
    expected[20].position[0] = x;
    memcpy(expected[30].basis, turned.basis, 36);
    forget_calls();
    if (way == 0) {
      tfp_arm_drive_set(false, false, 1);
      CHECK(!tfp_arm_drive_render(WORLD, true));
    } else if (way == 1) {
      CHECK(tfp_arm_drive_release());
    } else {
      tfp_arm_drive_after_render();
    }
    rig_locals(now);
    CHECK(!memcmp(now, expected, sizeof now) && stats().owned == 0 && stats().restores == 1 && sets > 30 && updates == 5);
  }
}

static void test_drive(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  fresh();
  tfp_arm_drive_set(true, false, 1);
  const TfpGraft map = bound_map();
  wanted(&map, body_now, game_locals, 1, want);
  const unsigned changed = differing(want, game_locals);
  CHECK(changed > 30); /* the recorded body is not in the rig's pose */

  /* frame 1: every joint that differs is written with the retired maths' value, bit for bit; then the rig, the
   * sleeve and glove on this rig, the gun and its stale optic are refreshed, in that order */
  CHECK(!tfp_arm_drive_render(WORLD, true)); /* accepted behaviour: nothing to do after the draw */
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now));
  TfpArmDriveStats s = stats();
  CHECK(s.frames == 1 && s.binds == 1 && s.joints == changed && sets == changed && s.owned == changed && !s.refused && !s.restores);
  CHECK(updates == 5 && s.pieces == 4);
  for (unsigned i = 0; i < changed; i++) CHECK(!calls[i].update && calls[i].unit == RIG && (i == 0 || calls[i].node > calls[i - 1].node || calls[i].node == 8));
  CHECK(calls[changed].update && calls[changed].unit == RIG && calls[changed + 1].unit == SLEEVE && calls[changed + 2].unit == GLOVE &&
        calls[changed + 3].unit == GUN && calls[changed + 4].unit == OPTIC);
  CHECK(tfp_arm_drive_held() != NULL);

  /* frame 2, nothing moved: nothing is written and nothing refreshed */
  forget_calls();
  CHECK(!tfp_arm_drive_render(WORLD, true) && !call_count);
  s = stats();
  CHECK(s.frames == 2 && s.binds == 1 && s.joints == changed && s.pieces == 4);

  /* frame 3: the body moved, and the game's animation rewrote one joint (the rig's own recoil, say) */
  put(worlds_of(BODY), body_later, sizeof body_later);
  TfpGraftLocal game = game_locals[20];
  game.position[0] += .01f, game.basis[0] = game_locals[20].basis[0];
  TfpGraftLocal rewritten[TFP_GRAFT_RIG_NODES];
  memcpy(rewritten, game_locals, sizeof rewritten);
  rewritten[20].position[0] = game.position[0];
  put(locals_of(RIG) + 20 * 64 + 36, &game.position[0], 4); /* the game wrote X only */
  wanted(&map, body_later, rewritten, 1, want);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && sets > 0 && stats().binds == 1);

  /* weight 0: the game's own locals come back, all of them, the rewritten X included; the tail never changed */
  forget_calls();
  tfp_arm_drive_set(true, false, 0);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, rewritten, sizeof now) && sets > 30);
  /* half way: the blend of the game's local and the pose */
  tfp_arm_drive_set(true, false, .5f);
  wanted(&map, body_later, rewritten, .5f, want);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && differing(now, rewritten) > 30);

  /* switched off: the next primary render puts the game's values back, refreshes, and holds nothing */
  forget_calls();
  tfp_arm_drive_set(false, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  s = stats();
  CHECK(!memcmp(now, rewritten, sizeof now) && s.owned == 0 && s.restores == 1 && !tfp_arm_drive_held());
  CHECK(sets > 30 && updates == 5 && calls[sets].unit == RIG && calls[sets + 3].unit == GUN);
  forget_calls();
  CHECK(!tfp_arm_drive_render(WORLD, true) && !call_count && tfp_arm_drive_release());
}

/* An optic the renderer has already put where the gun's node is: not refreshed again. */
static void test_current_optic(void) {
  fresh();
  { const float on = 0; put(worlds_of(OPTIC) + 48, &on, 4); }
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true) && updates == 4 && stats().pieces == 3 && calls[sets + 3].unit == GUN);
}

static void test_weight_zero_from_rest(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  fresh();
  tfp_arm_drive_set(true, false, 0);
  CHECK(!tfp_arm_drive_render(WORLD, true) && !call_count);
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now) && stats().frames == 1 && stats().owned == 0);
}

/* Putting back writes only the joints that do not hold the game's value already; the rig is refreshed all the same. */
static void test_release_writes_differences(void) {
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  tfp_arm_drive_set(true, false, 0);
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().owned > 30); /* every joint again holds the game's value, written by this mod */
  forget_calls();
  tfp_arm_drive_set(false, false, 1);
  CHECK(tfp_arm_drive_release() && sets == 0 && updates == 5 && stats().owned == 0 && stats().restores == 1);
}

static void test_restore_after_draw(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  fresh();
  tfp_arm_drive_set(true, true, 1);
  const TfpGraft map = bound_map();
  wanted(&map, body_now, game_locals, 1, want);
  for (int frame = 0; frame < 3; frame++) {
    forget_calls();
    CHECK(tfp_arm_drive_render(WORLD, true)); /* the caller forwards the draw, then: */
    rig_locals(now);
    CHECK(!memcmp(now, want, sizeof now) && stats().owned > 30);
    const unsigned written = sets;
    tfp_arm_drive_after_render();
    rig_locals(now);
    CHECK(!memcmp(now, game_locals, sizeof now) && stats().owned == 0 && sets == 2 * written && updates == 10);
  }
  CHECK(stats().frames == 3 && stats().restores == 3 && stats().binds == 1 && !tfp_arm_drive_held());
  tfp_arm_drive_after_render(); /* with nothing to put back: nothing */
  CHECK(stats().restores == 3);
}

static void test_bend(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  fresh();
  n32(names_of(BODY) + 50 * 4, 0x12345678); /* the body is ignored: a body the map cannot bind does not matter */
  tfp_arm_drive_set(true, false, 1);
  tfp_arm_drive_set_bend(true);
  memcpy(want, game_locals, sizeof want);
  want[10] = tfp_graft_bend_z(game_locals[10], 70);
  CHECK(fn[10] == TFP_GRAFT_NAME_L_SHOULDER);
  for (int frame = 0; frame < 3; frame++) { /* 70 degrees from the game's own value every frame, not 70 more each frame */
    CHECK(!tfp_arm_drive_render(WORLD, true));
    rig_locals(now);
    CHECK(!memcmp(now, want, sizeof now));
  }
  TfpArmDriveStats s = stats();
  CHECK(s.frames == 3 && s.joints == 1 && sets == 1 && calls[0].node == 10 && s.owned == 1 && s.pieces == 4 && updates == 5);
  tfp_arm_drive_set(false, false, 1);
  CHECK(tfp_arm_drive_release());
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now));
}

/* One refusal: `break_it` makes a proof fail on a driven rig. The frame is not driven, the reason is named, the
 * game's values are back (or, for a fault in the engine's own function, still held and named as held), and when
 * `mend_it` has undone the damage the next frame binds again from nothing. */
static void refusal(const char *name, uint32_t reason, void (*break_it)(void), void (*mend_it)(void), bool can_restore) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 1 && stats().owned > 30);
  break_it();
  forget_calls();
  CHECK(!tfp_arm_drive_render(WORLD, true));
  TfpArmDriveStats s = stats();
  if (s.frames != 1 || s.refused != 1 || s.reason != reason)
    fprintf(stderr, "%s: frames %llu refused %llu reason %s\n", name, (unsigned long long)s.frames, (unsigned long long)s.refused, tfp_arm_drive_reason_name(s.reason));
  CHECK(s.frames == 1 && s.refused == 1 && s.reason == reason && strcmp(tfp_arm_drive_reason_name(reason), "?"));
  rig_locals(now);
  if (can_restore) CHECK(!memcmp(now, game_locals, sizeof now) && s.owned == 0 && !tfp_arm_drive_held());
  else CHECK(s.owned > 30 && tfp_arm_drive_held());
  if (!mend_it) return;
  mend_it();
  CHECK(!tfp_arm_drive_render(WORLD, true));
  s = stats();
  CHECK(s.frames == 2 && s.binds == 2 && s.owned > 30);
}
static void break_snapshot(void) { n32(CTX + 0x200 + 8, 202); }  /* the request names another weapon */
static void mend_snapshot(void) { n32(CTX + 0x200 + 8, 201); }
static void break_ragdoll(void) { const uint8_t v = 0x10; put(AVATAR + 0x547844, &v, 1); }
static void mend_ragdoll(void) { const uint8_t v = 0; put(AVATAR + 0x547844, &v, 1); }
static void break_resource(void) { n32(object_of(RIG) + 0x358, 1); }
static void mend_resource(void) { n32(object_of(RIG) + 0x358, 0x3ca08bed); }
static void break_world(void) { p64(object_of(BODY) + 0x18, WORLD + 0x1000); }
static void mend_world(void) { p64(object_of(BODY) + 0x18, WORLD); }
static void break_weapon(void) { n32(links_of(GUN), (9u << 16) | 2); }
static void mend_weapon(void) { n32(links_of(GUN), (8u << 16) | 2); }
static void break_camera(void) { p64(object_of(CAMERA) + 0x1d0, object_of(BODY)); } /* the camera hangs on the body: another camera kind's link */
static void mend_camera(void) { p64(object_of(CAMERA) + 0x1d0, object_of(RIG)); }
static void break_names(void) { n32(names_of(BODY) + 4, fn[20]); } /* an arm joint's name twice on the body: seen only when binding again */
static void break_body_pose(void) { const float scale = 3; put(worlds_of(BODY), &scale, 4); }
static void mend_body_pose(void) { put(worlds_of(BODY), body_now, 4); }
static void move_body(void) { put(worlds_of(BODY), body_later, sizeof body_later); }
static void break_tree(void) { move_body(); p64(object_of(OPTIC) + 0x1e0, object_of(OPTIC)); } /* the gun's child list loops; seen when a joint is to be written */
static void mend_tree(void) { p64(object_of(OPTIC) + 0x1e0, 0); }
static void break_api(void) { p64(memory.exe + 0x27c8f10 + 0x60, FOREIGN_SET); }  /* another mod holds UnitAPI+0x60 */
static void mend_api(void) { p64(memory.exe + 0x27c8f10 + 0x60, SET); }
static void break_setter_fault(void) { move_body(); fault_set_at = 3; }
/* one condition of each proof at a time */
static void break_camera_link(void) { n32(links_of(CAMERA), (49u << 16) | 2); } /* on the rig, but not on its eye */
static void mend_camera_link(void) { n32(links_of(CAMERA), (50u << 16) | 2); }
static void break_gun_parent(void) { p64(object_of(GUN) + 0x1d0, object_of(BODY)); } /* the link and the external array as before */
static void mend_gun_parent(void) { p64(object_of(GUN) + 0x1d0, object_of(RIG)); }
static void break_gun_external(void) { p64(object_of(GUN) + 0x78, worlds_of(BODY)); }
static void mend_gun_external(void) { p64(object_of(GUN) + 0x78, worlds_of(RIG)); }
static void break_rig_world(void) { p64(object_of(RIG) + 0x18, WORLD + 0x1000); }
static void mend_rig_world(void) { p64(object_of(RIG) + 0x18, WORLD); }
static void break_gun_world(void) { p64(object_of(GUN) + 0x18, WORLD + 0x1000); }
static void mend_gun_world(void) { p64(object_of(GUN) + 0x18, WORLD); }
static void break_rig_count(void) { n32(object_of(RIG) + 0x70, 62); }
static void mend_rig_count(void) { n32(object_of(RIG) + 0x70, TFP_GRAFT_RIG_NODES); }
static void break_socket_name(void) { n32(names_of(RIG) + 8 * 4, fn[8] ^ 1); }
static void mend_socket_name(void) { n32(names_of(RIG) + 8 * 4, fn[8]); }
static void break_eye_name(void) { n32(names_of(RIG) + 50 * 4, fn[50] ^ 1); }
static void mend_eye_name(void) { n32(names_of(RIG) + 50 * 4, fn[50]); }
static void break_set_code(void) { const uint8_t b = 0x49; put(SET + 20, &b, 1); }   /* the last pinned byte */
static void mend_set_code(void) { const uint8_t b = 0xfa; put(SET + 20, &b, 1); }
static void break_update_code(void) { const uint8_t b = 0xfe; put(UPDATE + 31, &b, 1); }
static void mend_update_code(void) { const uint8_t b = 0xff; put(UPDATE + 31, &b, 1); }
static void break_set_executable(void) { not_executable = SET; }
static void break_update_executable(void) { not_executable = UPDATE; }
static void mend_executable(void) { not_executable = 0; }
static void break_world_api(void) { p64(memory.exe + 0x27cab40 + 0x100, FOREIGN_UPDATE); } /* another mod holds WorldAPI+0x100 */
static void mend_world_api(void) { p64(memory.exe + 0x27cab40 + 0x100, UPDATE); }

static void test_refusals(void) {
  refusal("snapshot", TFP_ARM_SNAPSHOT, break_snapshot, mend_snapshot, true);
  refusal("ragdoll", TFP_ARM_RAGDOLL, break_ragdoll, mend_ragdoll, true);
  refusal("rig resource", TFP_ARM_UNITS, break_resource, mend_resource, true);
  refusal("world", TFP_ARM_WORLD, break_world, mend_world, true);
  refusal("weapon", TFP_ARM_WEAPON, break_weapon, mend_weapon, true);
  refusal("camera", TFP_ARM_CAMERA, break_camera, mend_camera, true);
  refusal("body pose", TFP_ARM_POSE, break_body_pose, mend_body_pose, true);
  refusal("weapon tree", TFP_ARM_TREE, break_tree, mend_tree, true);
  refusal("api", TFP_ARM_API, break_api, mend_api, false); /* without the proven setter nothing can be written back yet */
  refusal("fault", TFP_ARM_FAULT, break_setter_fault, NULL, false);
  refusal("camera link", TFP_ARM_CAMERA, break_camera_link, mend_camera_link, true);
  refusal("gun parent", TFP_ARM_WEAPON, break_gun_parent, mend_gun_parent, true);
  refusal("gun external array", TFP_ARM_WEAPON, break_gun_external, mend_gun_external, true);
  refusal("rig world", TFP_ARM_WORLD, break_rig_world, mend_rig_world, false); /* a rig in another world is not written, not even back */
  refusal("gun world", TFP_ARM_WORLD, break_gun_world, mend_gun_world, true);
  refusal("rig node count", TFP_ARM_UNITS, break_rig_count, mend_rig_count, true);
  refusal("socket name", TFP_ARM_UNITS, break_socket_name, mend_socket_name, true);
  refusal("eye name", TFP_ARM_UNITS, break_eye_name, mend_eye_name, true);
  refusal("setter bytes", TFP_ARM_API, break_set_code, mend_set_code, false);
  refusal("update bytes", TFP_ARM_API, break_update_code, mend_update_code, false);
  refusal("setter not executable", TFP_ARM_API, break_set_executable, mend_executable, false);
  refusal("update not executable", TFP_ARM_API, break_update_executable, mend_executable, false);
  refusal("world api", TFP_ARM_API, break_world_api, mend_world_api, false);

  /* not the game thread: nothing is read, nothing written, nothing released */
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  const unsigned reads = f.reads;
  forget_calls();
  CHECK(!tfp_arm_drive_render(WORLD, false) && f.reads == reads && !call_count);
  CHECK(stats().refused == 1 && stats().reason == TFP_ARM_THREAD && stats().owned > 30);

  /* a held value is put back once the proof returns (the API slot again holds the pinned setter) */
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  break_api();
  CHECK(!tfp_arm_drive_render(WORLD, true) && tfp_arm_drive_held() && !tfp_arm_drive_release());
  mend_api();
  tfp_arm_drive_set(false, false, 1);
  CHECK(tfp_arm_drive_release() && !tfp_arm_drive_held() && stats().owned == 0);
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now));

  /* names are read when binding only: a changed name on a bound body changes nothing until the units change */
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true));
  break_names();
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 2);
  break_camera(); /* a failed proof drops the binding */
  CHECK(!tfp_arm_drive_render(WORLD, true));
  mend_camera();
  CHECK(!tfp_arm_drive_render(WORLD, true));
  CHECK(stats().frames == 2 && stats().refused == 2 && stats().reason == TFP_ARM_BIND && stats().binds == 1 && stats().owned == 0);
}

/* The rig unit is replaced (a weapon change makes a new one): the old pointers are never used again. */
static void test_rebind_on_new_units(void) {
  fresh();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().binds == 1 && stats().owned > 30);
  /* the old rig's generation moves on; the avatar row names a new rig holding the same recorded pose */
  { const uint8_t generation = 1; put(REGISTRY + 0x1000 + RIG, &generation, 1); }
  make_unit(NEW_RIG, TFP_GRAFT_RIG_NODES, 0, 0);
  put(names_of(NEW_RIG), fn, sizeof fn), put(links_of(NEW_RIG), fl, sizeof fl), put(worlds_of(NEW_RIG), rig_world, sizeof rig_world);
  put(locals_of(NEW_RIG), game_locals, sizeof game_locals);
  { const uint64_t resource = UINT64_C(0xd0cf4d493ca08bed); put(object_of(NEW_RIG) + 0x358, &resource, 8); }
  n32(AVATAR + FP_AVATAR_RIG, NEW_RIG);
  p64(object_of(GUN) + 0x1d0, object_of(NEW_RIG)), p64(object_of(GUN) + 0x78, worlds_of(NEW_RIG));
  p64(object_of(CAMERA) + 0x1d0, object_of(NEW_RIG));
  p64(object_of(SLEEVE) + 0x1d0, object_of(NEW_RIG)), p64(object_of(SLEEVE) + 0x78, worlds_of(NEW_RIG));
  TfpGraftLocal old[TFP_GRAFT_RIG_NODES], now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  rig_locals(old);
  forget_calls();
  CHECK(!tfp_arm_drive_render(WORLD, true));
  TfpArmDriveStats s = stats();
  CHECK(s.frames == 2 && s.binds == 2 && !s.refused);
  for (unsigned i = 0; i < call_count; i++) CHECK(calls[i].unit != RIG && calls[i].unit != GLOVE); /* the glove still hangs on the retired rig */
  rig_locals(now);
  CHECK(!memcmp(now, old, sizeof now)); /* the retired unit's memory is not touched */
  const TfpGraft map = bound_map();
  wanted(&map, body_now, game_locals, 1, want);
  CHECK(read_fake(&f, locals_of(NEW_RIG), now, sizeof now) && !memcmp(now, want, sizeof now));
  CHECK(updates == 4 && calls[sets].unit == NEW_RIG && calls[sets + 1].unit == SLEEVE && calls[sets + 2].unit == GUN);
}

/* The action hold (tfp_arm_drive_set_hold): the gun may leave the rig or be gone; the binding and its entry
 * alignment stay; the gun's units are not refreshed while it is away; the game's values go back after every draw. */
static void gun_dead(void) { const uint8_t generation = 1; put(REGISTRY + 0x1000 + GUN, &generation, 1); }
static void gun_alive(void) { const uint8_t generation = 0; put(REGISTRY + 0x1000 + GUN, &generation, 1); }
static void test_hold(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  TfpArmDriveProbe probe;
  fresh();
  tfp_arm_drive_set(true, false, 1); /* the accepted path: values stay written */
  const TfpGraft map = bound_map();  /* the entry alignment: the socket as it was on the first driven frame */
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 1 && stats().binds == 1 && stats().owned > 30);
  tfp_arm_drive_probe(GUN, &probe);
  CHECK(probe.proven && !strcmp(probe.gun_parent, "rig") && probe.has_camera && probe.camera_parent_is_rig_eye && probe.has_eye && probe.has_rig_eye);
  CHECK(probe.rig_eye[0] == rig_world[50].values[12] && probe.rig_eye[2] == rig_world[50].values[14] && probe.eye[0] == 0);

  /* the hold begins with the gun still on the rig: driven as before, but the game's values go back after the draw */
  tfp_arm_drive_set_hold(true);
  move_body();
  wanted(&map, body_later, game_locals, 1, want);
  forget_calls();
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && stats().frames == 2 && stats().binds == 1);
  tfp_arm_drive_after_render();
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now) && stats().owned == 0 && !tfp_arm_drive_held());

  /* the gun goes to the body (a stow): not a refusal, the same binding, the gun's units are left alone */
  break_gun_parent();
  forget_calls();
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  TfpArmDriveStats s = stats();
  CHECK(!memcmp(now, want, sizeof now) && s.frames == 3 && s.binds == 1 && !s.refused);
  CHECK(updates == 3 && calls[sets].unit == RIG && calls[sets + 1].unit == SLEEVE && calls[sets + 2].unit == GLOVE);
  tfp_arm_drive_probe(GUN, &probe);
  CHECK(!strcmp(probe.gun_parent, "body"));
  forget_calls();
  tfp_arm_drive_after_render();
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now) && updates == 3 && stats().owned == 0);
  for (unsigned i = 0; i < call_count; i++) CHECK(calls[i].unit != GUN && calls[i].unit != OPTIC);

  /* the full snapshot fails (the request names another weapon than the hands hold): the player's serves */
  break_snapshot();
  CHECK(tfp_arm_drive_render(WORLD, true) && stats().frames == 4 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  mend_snapshot();
  /* the gun's unit is gone altogether */
  gun_dead();
  CHECK(tfp_arm_drive_render(WORLD, true) && stats().frames == 5 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  tfp_arm_drive_probe(GUN, &probe);
  CHECK(!strcmp(probe.gun_parent, "none"));
  gun_alive();
  /* its links say another socket: away as well */
  mend_gun_parent();
  break_weapon();
  CHECK(tfp_arm_drive_render(WORLD, true) && stats().frames == 6 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  mend_weapon();

  /* the gun is back on the rig: its units are refreshed again; the alignment is still the entry's */
  forget_calls();
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && stats().frames == 7 && stats().binds == 1 && updates == 5 && calls[sets + 3].unit == GUN);
  tfp_arm_drive_after_render();
  /* the hold ends: the accepted path again, on the same binding */
  tfp_arm_drive_set_hold(false);
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 8 && stats().binds == 1 && stats().owned > 30 && !stats().refused);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now));
  /* and without the hold a gun off the rig is the refusal it always was */
  break_gun_parent();
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().refused == 1 && stats().reason == TFP_ARM_WEAPON && stats().owned == 0);
  mend_gun_parent();

  /* what a hold does not excuse: a ragdoll, a camera that is not on the rig's eye, a rig in another world */
  static const struct { void (*break_it)(void), (*mend_it)(void); uint32_t reason; } still[] = {
    {break_ragdoll, mend_ragdoll, TFP_ARM_RAGDOLL}, {break_camera, mend_camera, TFP_ARM_CAMERA}, {break_world, mend_world, TFP_ARM_WORLD}};
  for (unsigned i = 0; i < sizeof still / sizeof *still; i++) {
    fresh();
    tfp_arm_drive_set(true, false, 1);
    tfp_arm_drive_set_hold(true);
    break_gun_parent();
    CHECK(tfp_arm_drive_render(WORLD, true) && stats().frames == 1 && stats().binds == 1); /* bound without the gun */
    tfp_arm_drive_after_render();
    still[i].break_it();
    CHECK(!tfp_arm_drive_render(WORLD, true) && stats().refused == 1 && stats().reason == still[i].reason && stats().owned == 0);
    still[i].mend_it();
  }
  /* empty hands from the start of a binding: the player's snapshot, no gun at all */
  fresh();
  tfp_arm_drive_set(true, false, 1);
  tfp_arm_drive_set_hold(true);
  break_snapshot();
  gun_dead();
  CHECK(tfp_arm_drive_render(WORLD, true) && stats().frames == 1 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now) && !tfp_arm_drive_held());
  /* switched off entirely, the probe reads nothing it should not: no player, no answer */
  request(4);
  n32(CTX + 0x200 + 4, 999); /* the camera is not the local avatar's */
  tfp_arm_drive_probe(GUN, &probe);
  CHECK(!probe.proven && !probe.gun_parent && !probe.has_camera && !probe.has_eye);
}

/* The edge of an action. The tick runs before the game's update, so the first frame of an action is drawn before
 * the tick that sees it: the controller is not holding yet. "Ready" (a lease runs and the switch is on) is what the
 * arm drive has then; the gun leaving the rig in that frame must not cost the binding or its entry alignment. */
static void test_hold_edge(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  fresh();
  tfp_arm_drive_set(true, false, 1);
  const TfpGraft map = bound_map();
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 1 && stats().binds == 1);
  /* ready, the gun on the rig: nothing differs, the values stay written */
  tfp_arm_drive_set_hold_ready(true);
  move_body();
  wanted(&map, body_later, game_locals, 1, want);
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 2 && stats().binds == 1 && stats().owned > 30);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now));
  /* the gun leaves, the controller has not seen it: driven on the same binding, and the values go back after this draw */
  break_gun_parent();
  forget_calls();
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && stats().frames == 3 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  rig_locals(now);
  CHECK(!memcmp(now, game_locals, sizeof now) && stats().owned == 0);
  for (unsigned i = 0; i < call_count; i++) CHECK(calls[i].unit != GUN && calls[i].unit != OPTIC);
  /* the next tick holds: the same binding, the entry's alignment */
  tfp_arm_drive_set_hold(true);
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && stats().frames == 4 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  /* the full snapshot failing on the edge frame is served by the player's as well */
  tfp_arm_drive_set_hold(false);
  break_snapshot();
  CHECK(tfp_arm_drive_render(WORLD, true) && stats().frames == 5 && stats().binds == 1 && !stats().refused);
  tfp_arm_drive_after_render();
  mend_snapshot();
  /* the gun is back and the controller resumed: the accepted path, the same binding */
  mend_gun_parent();
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().frames == 6 && stats().binds == 1 && stats().owned > 30 && !stats().refused);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now));
  /* no lease any more: a gun off the rig is the refusal it always was */
  tfp_arm_drive_set_hold_ready(false);
  break_gun_parent();
  CHECK(!tfp_arm_drive_render(WORLD, true) && stats().refused == 1 && stats().reason == TFP_ARM_WEAPON && stats().owned == 0);
  mend_gun_parent();
}

/* ---- the resting weapon pose (tfp_arm_drive_set_rest) ---- */
enum { GUN2 = 502, GUN3 = 503 };
static TfpGraftMatrix body_draw[TFP_GRAFT_BODY_NODES], body_crouch[TFP_GRAFT_BODY_NODES], body_sway[TFP_GRAFT_BODY_NODES];
/* The recorded body with its arms somewhere else in the frame they are seen in (the view's axes at the head): the
 * head moved, and everything turned about the view's own Z through the head, by the opposite angle (which is the
 * frame turned by `degrees`). Every joint keeps its relation to its parent. */
static void turned_about(const float *axis, const float *centre, float degrees, float *vector, bool point) {
  const float c = cosf(degrees * .01745329252f), s = sinf(degrees * .01745329252f);
  float v[3], cross[3], dot = 0;
  for (unsigned k = 0; k < 3; k++) v[k] = vector[k] - (point ? centre[k] : 0), dot += axis[k] * v[k];
  cross[0] = axis[1] * v[2] - axis[2] * v[1], cross[1] = axis[2] * v[0] - axis[0] * v[2], cross[2] = axis[0] * v[1] - axis[1] * v[0];
  for (unsigned k = 0; k < 3; k++) vector[k] = v[k] * c + cross[k] * s + axis[k] * dot * (1 - c) + (point ? centre[k] : 0);
}
static void displaced(float degrees, float x, float y, float z, TfpGraftMatrix *out) {
  unsigned head = 0;
  memcpy(out, body_now, sizeof body_now);
  for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; i++)
    if (bn[i] == TFP_GRAFT_NAME_HEAD) head = i;
  CHECK(head);
  out[head].values[12] += x, out[head].values[13] += y, out[head].values[14] += z;
  const float *axis = rig_world[TFP_GRAFT_RIG_ROOT].values + 8;
  float centre[3];
  memcpy(centre, out[head].values + 12, sizeof centre);
  for (unsigned i = 1; i < TFP_GRAFT_BODY_NODES; i++) {
    for (unsigned row = 0; row < 3; row++) turned_about(axis, centre, -degrees, out[i].values + 4 * row, false);
    turned_about(axis, centre, -degrees, out[i].values + 12, true);
  }
}
static void body_is(const TfpGraftMatrix *body) { put(worlds_of(BODY), body, sizeof body_now); }
static float apart_m(const TfpGraftLocal *a, const TfpGraftLocal *b) {
  float d = 0;
  for (unsigned i = 0; i < 3; i++) d += (a->position[i] - b->position[i]) * (a->position[i] - b->position[i]);
  return sqrtf(d);
}
static float apart_degrees(const TfpGraftLocal *a, const TfpGraftLocal *b) {
  float trace = 0;
  for (unsigned i = 0; i < 9; i++) trace += a->basis[i] * b->basis[i];
  const float c = (trace - 1) / 2;
  return acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.29578f;
}
static uint64_t clock_ms;
static unsigned stance;
static uint32_t pose_key;
/* One tick and one primary render, 33 ms after the one before. */
static bool frame(void) {
  clock_ms += 33;
  tfp_arm_drive_set_rest(clock_ms, stance, pose_key);
  return tfp_arm_drive_render(WORLD, true);
}
static TfpGraftLocal socket_shown(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  rig_locals(now);
  return now[TFP_GRAFT_RIG_SOCKET];
}
/* The alignment a rest gives: the grip from the rig's own locals, the body's socket on the rig's own socket local. */
static TfpGraft rest_map(const TfpGraftMatrix *body) {
  TfpGraft map;
  TfpGraftLocal base[TFP_GRAFT_JOINTS];
  CHECK(tfp_graft_bind(&map, bn, bl, fn, fl, body_now, rig_world));
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) base[i] = game_locals[map.fp[i]];
  CHECK(tfp_graft_grip(&map, body, base, &map.grip));
  CHECK(tfp_graft_align(&map, body, &game_locals[TFP_GRAFT_RIG_SOCKET]));
  return map;
}
static void rest_start(void) {
  fresh();
  clock_ms = 100000, stance = TFP_ARM_REST_STAND, pose_key = 0;
  displaced(25, .20f, .10f, -.15f, body_draw);
  displaced(-12, -.05f, .08f, .12f, body_crouch);
  displaced(0, .02f, 0, 0, body_sway); /* 20 mm: out of one stretch of stillness (15 mm), inside the sway of a rest (30 mm) */
  tfp_arm_drive_set(true, false, 1);
}
/* Still frames until the alignment count moves; the frame it moves on shows what the frame before showed. */
static unsigned settle(unsigned limit) {
  const uint64_t count = stats().align.count;
  TfpGraftLocal before[TFP_GRAFT_RIG_NODES], now[TFP_GRAFT_RIG_NODES];
  for (unsigned n = 1; n <= limit; n++) {
    rig_locals(before);
    CHECK(!frame());
    if (stats().align.count == count) continue;
    rig_locals(now);
    CHECK(!memcmp(before, now, sizeof now)); /* taken, not yet shown: no step */
    return n;
  }
  return 0;
}
/* The 150 ms that follow: every frame nearer, none by more than half the way. */
static void eases(float from_m) {
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  float last = from_m;
  for (unsigned n = 0; n < 5; n++) {
    CHECK(!frame());
    const TfpGraftLocal s = socket_shown();
    const float left = apart_m(&s, rest);
    CHECK((left < last || left < .0001f) && last - left < from_m * .5f); /* (already there: an ease begun a frame earlier) */
    last = left;
  }
  CHECK(last < .0001f);
}
/* From the first frame of a stretch of stillness, with nothing kept for the weapon and stance: measured once after
 * 350 ms and shown; kept when the same stretch has lasted 2000 ms, and nothing moves then. */
static void kept_by_stillness(void) {
  const uint64_t count = stats().align.count;
  CHECK(settle(30) == 11 && stats().align.how == TFP_ARM_ALIGN_TENTATIVE && stats().align.count == count + 1);
  const TfpGraftLocal shown = socket_shown();
  const float off = apart_m(&shown, &game_locals[TFP_GRAFT_RIG_SOCKET]);
  if (off > .001f) eases(off);
  else for (unsigned n = 0; n < 5; n++) CHECK(!frame());
  CHECK(settle(90) == 45);
  const TfpArmDriveStats s = stats();
  CHECK(s.align.count == count + 2 && s.align.how == TFP_ARM_ALIGN_REST && s.align.rest_ms >= 2000 && s.align.rest_ms < 2050 && !s.align.eased &&
        s.align.moved_m < .0001f && s.align.moved_degrees < .2f);
}

/* The grip from the rig's own locals is the grip the bind takes from the rig's world matrices, where the rig is at rest. */
static void test_rest_grip(void) {
  TfpGraft map;
  TfpGraftLocal base[TFP_GRAFT_JOINTS];
  TfpGraftMatrix grip;
  CHECK(tfp_graft_bind(&map, bn, bl, fn, fl, body_now, rig_world));
  for (unsigned i = 0; i < TFP_GRAFT_JOINTS; i++) base[i] = game_locals[map.fp[i]];
  CHECK(tfp_graft_grip(&map, body_now, base, &grip));
  for (unsigned i = 0; i < 16; i++) CHECK(fabsf(grip.values[i] - map.grip.values[i]) < .0002f);
  base[24].basis[0] = 3; /* r_hand's record is not a rotation */
  const TfpGraftMatrix kept = grip;
  CHECK(!tfp_graft_grip(&map, body_now, base, &grip) && !memcmp(&kept, &grip, sizeof grip));
}

/* The defect of 0.6.0-feel.25 and its repair. A binding begins in the middle of a draw: the body's hand is not where it
 * rests. The first driven frame's alignment puts the socket on the rig's own for that frame; once the body is at
 * rest the gun is off by the rest of the draw. After 350 ms of rest the alignment is measured again and eased in; once
 * that measurement is confirmed it is kept, and the next binding of this weapon, again begun in the middle of a draw,
 * ends on the rig's own socket by itself. */
/* Looking down or up: the view (the whole rig) and the body's arms and head pitch together, the body's root does not.
 * The arms stay where they are in the view. With the root's axes as the frame the arms' pitch was counted twice, and
 * 40 degrees down put the socket 40 degrees and a hand's length from its place (seen in the game 2026-11-22). */
static void test_view_pitch(void) {
  for (int degrees = -40; degrees <= 40; degrees += 80) {
    TfpGraftLocal level[TFP_GRAFT_RIG_NODES], pitched[TFP_GRAFT_RIG_NODES];
    static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES], rig[TFP_GRAFT_RIG_NODES];
    unsigned head = 0;
    rest_start();
    CHECK(!frame());
    rig_locals(level);
    for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; i++)
      if (bn[i] == TFP_GRAFT_NAME_HEAD) head = i;
    const float *axis = rig_world[TFP_GRAFT_RIG_ROOT].values, *centre = body_now[head].values + 12;
    memcpy(body, body_now, sizeof body), memcpy(rig, rig_world, sizeof rig);
    for (unsigned i = 1; i < TFP_GRAFT_BODY_NODES; i++) {
      for (unsigned row = 0; row < 3; row++) turned_about(axis, centre, (float)degrees, body[i].values + 4 * row, false);
      turned_about(axis, centre, (float)degrees, body[i].values + 12, true);
    }
    for (unsigned i = 0; i < TFP_GRAFT_RIG_NODES; i++) {
      for (unsigned row = 0; row < 3; row++) turned_about(axis, centre, (float)degrees, rig[i].values + 4 * row, false);
      turned_about(axis, centre, (float)degrees, rig[i].values + 12, true);
    }
    put(worlds_of(RIG), rig, sizeof rig), body_is(body);
    CHECK(!frame());
    rig_locals(pitched);
    float worst_m = 0, worst_degrees = 0;
    for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; n++) {
      const float m = apart_m(&pitched[n], &level[n]), turned = apart_degrees(&pitched[n], &level[n]);
      if (m > worst_m) worst_m = m;
      if (turned > worst_degrees) worst_degrees = turned;
    }
    CHECK(worst_m < .001f && worst_degrees < .2f);
    CHECK(stats().binds == 1 && !stats().refused);
    put(worlds_of(RIG), rig_world, sizeof rig_world), body_is(body_now);
  }
}
/* The mod's own placement and motion rotation (and the game's own animation) go on rig node 5, the parent of the rig's
 * root: node 5 and everything under it turn, node 5's parent, which the view carries, does not. The driven arms and gun
 * turn with it, rigidly: the socket ends where the rotation takes it. With the rig's root as the frame (0.6.0-feel.35)
 * the rotation was cancelled and the arms and gun stayed where they were. */
static void test_rig_rotation(void) {
  static TfpGraftMatrix rig[TFP_GRAFT_RIG_NODES];
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  CHECK(fl[TFP_GRAFT_RIG_ROOT] == ((5u << 16) | 1u) && fl[5] == ((4u << 16) | 1u));
  rest_start();
  CHECK(!frame());
  rig_locals(now);
  const TfpGraftMatrix local = tfp_graft_local_matrix(&now[TFP_GRAFT_RIG_SOCKET]);
  TfpGraftMatrix want = tfp_graft_product(&local, &rig_world[TFP_GRAFT_RIG_ROOT]);
  /* 15 degrees about node 5's own X through node 5: a placement pitch */
  const float *axis = rig_world[5].values, *centre = rig_world[5].values + 12;
  memcpy(rig, rig_world, sizeof rig);
  for (unsigned n = 5; n < TFP_GRAFT_RIG_NODES; n++) {
    unsigned up = n;
    while (up > 5 && (fl[up] & 65535) == 1) up = fl[up] >> 16;
    if (up != 5) continue;
    for (unsigned row = 0; row < 3; row++) turned_about(axis, centre, 15, rig[n].values + 4 * row, false);
    turned_about(axis, centre, 15, rig[n].values + 12, true);
  }
  CHECK(!memcmp(&rig[4], &rig_world[4], 64) && !memcmp(&rig[TFP_GRAFT_RIG_EYE], &rig_world[TFP_GRAFT_RIG_EYE], 64) &&
        memcmp(&rig[TFP_GRAFT_RIG_ROOT], &rig_world[TFP_GRAFT_RIG_ROOT], 64) && memcmp(&rig[TFP_GRAFT_RIG_SOCKET], &rig_world[TFP_GRAFT_RIG_SOCKET], 64));
  for (unsigned row = 0; row < 3; row++) turned_about(axis, centre, 15, want.values + 4 * row, false);
  turned_about(axis, centre, 15, want.values + 12, true);
  put(worlds_of(RIG), rig, sizeof rig);
  CHECK(!frame());
  rig_locals(now);
  const TfpGraftMatrix after = tfp_graft_local_matrix(&now[TFP_GRAFT_RIG_SOCKET]);
  const TfpGraftMatrix got = tfp_graft_product(&after, &rig[TFP_GRAFT_RIG_ROOT]);
  float m = 0, trace = 0;
  for (unsigned k = 0; k < 3; k++) m += (got.values[12 + k] - want.values[12 + k]) * (got.values[12 + k] - want.values[12 + k]);
  for (unsigned row = 0; row < 3; row++)
    for (unsigned k = 0; k < 3; k++) trace += got.values[4 * row + k] * want.values[4 * row + k];
  CHECK(sqrtf(m) < .001f && trace > 2.99998f); /* 1 mm; a third of a degree */
  CHECK(stats().binds == 1 && !stats().refused);
  put(worlds_of(RIG), rig_world, sizeof rig_world);
  /* another chain above the rig's root: no view node, and nothing is bound */
  uint32_t links[TFP_GRAFT_RIG_NODES];
  unsigned view = 99;
  memcpy(links, fl, sizeof links);
  links[5] = (4u << 16) | 2u;
  CHECK(!tfp_graft_view_node(links, &view) && view == 99);
  links[5] = (3u << 16) | 1u; /* no longer the eye's parent */
  CHECK(!tfp_graft_view_node(links, &view) && view == 99);
  rest_start();
  put(links_of(RIG), links, sizeof links);
  CHECK(!frame() && stats().refused == 1 && stats().reason == TFP_ARM_BIND && !stats().binds && !sets);
}
static void test_rest_after_a_draw(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  rest_start();
  pose_key = tfp_arm_drive_rest_of(&(TfpArmRestSample){.stance = TFP_ARM_REST_STAND, .has_speed = true, .has_pitch = true, .has_layers = true,
                                                       .l1 = 75, .l3 = 68, .l14 = 36}, &pose_key, NULL) ? pose_key : 0;
  CHECK(pose_key);
  body_is(body_draw);
  CHECK(!frame());
  TfpArmDriveStats s = stats();
  TfpGraftLocal shown = socket_shown();
  CHECK(s.binds == 1 && s.align.count == 1 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.weapon == GUN && !s.align.eased);
  CHECK(apart_m(&shown, rest) < .0001f && apart_degrees(&shown, rest) < .2f);
  /* the draw ends: the body is at rest, and the gun is not where the game's placement has it */
  body_is(body_now);
  CHECK(!frame());
  shown = socket_shown();
  const float off = apart_m(&shown, rest), turned = apart_degrees(&shown, rest);
  CHECK(off > .05f && turned > 5);
  /* 350 ms of rest, then measured and shown; nothing moves on that frame */
  const unsigned frames = settle(30);
  s = stats();
  CHECK(frames == 11 && s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_TENTATIVE && s.align.weapon == GUN && s.align.stance == TFP_ARM_REST_STAND &&
        s.align.key == pose_key);
  {
    char want_line[240];
    snprintf(want_line, sizeof want_line, "aligned (weapon %u, at rest for 363 ms, layers 75:68:36, measured once: shown, not kept yet, moved %.3f m / "
             "%.1f degrees from the one in use, eased); standing", (unsigned)GUN, (double)s.align.moved_m, (double)s.align.moved_degrees);
    CHECK(!strcmp(tfp_arm_drive_align_line(&s.align), want_line));
  }
  CHECK(s.align.rest_ms >= 350 && s.align.rest_ms < 400 && s.align.eased && fabsf(s.align.moved_m - off) < .001f && fabsf(s.align.moved_degrees - turned) < .1f);
  eases(off);
  /* at rest: the rig's own socket, and the whole pose is the rest alignment's, bit for bit */
  const TfpGraft map = rest_map(body_now);
  wanted(&map, body_now, game_locals, 1, want);
  rig_locals(now);
  shown = now[TFP_GRAFT_RIG_SOCKET];
  CHECK(!memcmp(now, want, sizeof now) && apart_m(&shown, rest) < .0001f && apart_degrees(&shown, rest) < .2f);
  /* the same stillness for two seconds: kept, and nothing is written for it */
  forget_calls();
  CHECK(settle(90) == 45);
  s = stats();
  CHECK(s.align.count == 3 && s.align.how == TFP_ARM_ALIGN_REST && s.align.rest_ms >= 2000 && !s.align.eased && s.align.moved_m < .0001f && !call_count);
  {
    char want_line[240];
    snprintf(want_line, sizeof want_line, "aligned (weapon %u, at rest for %u ms, layers 75:68:36, confirmed: kept, moved 0.000 m / %.1f degrees "
             "from the one in use); standing", (unsigned)GUN, (unsigned)s.align.rest_ms, (double)s.align.moved_degrees);
    CHECK(!strcmp(tfp_arm_drive_align_line(&s.align), want_line));
  }
  /* resting on: nothing is taken again and nothing written */
  for (unsigned n = 0; n < 120; n++) CHECK(!frame());
  CHECK(stats().align.count == 3 && !call_count && stats().binds == 1);
  /* the body moves away and comes back (an action): around the same place, and back on it */
  body_is(body_draw);
  stance = TFP_ARM_REST_NONE;
  for (unsigned n = 0; n < 20; n++) CHECK(!frame());
  wanted(&map, body_draw, game_locals, 1, want);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now) && stats().align.count == 3);
  body_is(body_now);
  stance = TFP_ARM_REST_STAND;
  for (unsigned n = 0; n < 30; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(apart_m(&shown, rest) < .0001f && stats().align.count == 3);

  /* a swap and back: frames in another camera, then a new binding in the middle of the draw */
  request(4);
  for (unsigned n = 0; n < 12; n++) CHECK(!frame());
  request(6);
  body_is(body_draw);
  CHECK(!frame());
  s = stats();
  CHECK(s.binds == 2 && s.align.count == 4 && s.align.how == TFP_ARM_ALIGN_KEPT && s.align.weapon == GUN && !s.align.eased);
  wanted(&map, body_draw, game_locals, 1, want);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now));
  body_is(body_now); /* the draw ends on the resting place, with no later correction */
  CHECK(!frame());
  shown = socket_shown();
  CHECK(apart_m(&shown, rest) < .0001f && apart_degrees(&shown, rest) < .2f);
  for (unsigned n = 0; n < 90; n++) CHECK(!frame());
  CHECK(stats().align.count == 4 && stats().binds == 2);
}

/* The body's weapon socket in body_now, stretched by one part in a hundred: not rigid. */
static void bend_socket(void) {
  TfpGraft map;
  CHECK(tfp_graft_bind(&map, bn, bl, fn, fl, body_now, rig_world));
  const unsigned socket = map.body[TFP_GRAFT_JOINTS - 1];
  TfpGraftMatrix bent = body_now[socket];
  for (unsigned i = 0; i < 3; i++) bent.values[i] *= 1.01f;
  put(worlds_of(BODY) + 64 * socket, &bent, sizeof bent);
}
/* A binding that begins while the player aims (the copy is hidden: weight 0, src/arm_weight.h) with nothing kept for
 * the weapon. Its provisional alignment is of the aim pose, where body and rig differ most; it shows nothing until
 * the aim is released. On the first driven frame the copy is fully shown again the provisional alignment is taken
 * again, from that frame, and eased in: a player who keeps moving (never a rest) does not keep the rifle displaced.
 * Not when something better replaced the provisional one meanwhile (kept, or measured at a rest). */
static void test_binding_while_aiming(void) {
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  rest_start();
  stance = TFP_ARM_REST_NONE; /* aiming, then moving: never a rest */
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw); /* the body's aim pose: not where it rests */
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  TfpArmDriveStats s = stats();
  rig_locals(now);
  CHECK(s.binds == 1 && s.align.count == 1 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && !memcmp(now, game_locals, sizeof now)); /* hidden */
  /* released: the body goes to its carry pose, the copy fades in over a few frames */
  body_is(body_now);
  tfp_arm_drive_set(true, false, .4f); CHECK(!frame());
  tfp_arm_drive_set(true, false, .8f); CHECK(!frame());
  CHECK(stats().align.count == 1); /* not while it is still fading */
  TfpGraftLocal before = socket_shown();
  tfp_arm_drive_set(true, false, 1);
  rig_locals(now);
  CHECK(!frame());
  s = stats();
  TfpGraftLocal shown = socket_shown();
  const float off = apart_m(&shown, rest);
  CHECK(s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.eased && s.binds == 1);
  CHECK(s.align.moved_m > .05f && strstr(tfp_arm_drive_align_line(&s.align), "taken again now that the arms show"));
  CHECK(off > .05f && apart_m(&shown, &before) < off); /* this frame shows the aim alignment still (now at full weight): no step from the retake */
  eases(off);
  shown = socket_shown();
  CHECK(apart_m(&shown, rest) < .0001f && apart_degrees(&shown, rest) < .2f);
  /* once: moving on changes nothing more */
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  CHECK(stats().align.count == 2 && stats().binds == 1);

  /* a rest measurement arrives while the copy is still partly faded (the weight held under 1): it is the better one,
   * and reaching full weight takes nothing again */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw);
  CHECK(!frame());
  body_is(body_now);
  stance = TFP_ARM_REST_STAND;
  tfp_arm_drive_set(true, false, .5f);
  CHECK(settle(30) == 12 && stats().align.how == TFP_ARM_ALIGN_TENTATIVE && stats().align.count == 2);
  tfp_arm_drive_set(true, false, 1);
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  CHECK(stats().align.count == 2);
  shown = socket_shown();
  CHECK(apart_m(&shown, rest) < .0001f);

  /* a weapon with a kept alignment, bound while aiming: the kept one from the start, nothing taken again */
  rest_start();
  CHECK(!frame());
  kept_by_stillness();
  const uint64_t count = stats().align.count;
  request(4);
  for (unsigned n = 0; n < 12; n++) CHECK(!frame());
  request(6);
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw);
  CHECK(!frame());
  CHECK(stats().align.count == count + 1 && stats().align.how == TFP_ARM_ALIGN_KEPT && stats().binds == 2);
  body_is(body_now);
  tfp_arm_drive_set(true, false, 1);
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == count + 1 && apart_m(&shown, rest) < .0001f);

  /* aim, an action while still aiming (a grenade, a stim), release: the controller holds, the tick answers "not
   * aiming" and the weight reaches 1 inside the action. That frame is no better than the aim pose: nothing is taken
   * there, and the one retake waits for the first fully shown frame after the hold. */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw);
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  CHECK(stats().align.count == 1 && stats().binds == 1);
  tfp_arm_drive_set_hold(true);
  body_is(body_crouch); /* the action's pose */
  tfp_arm_drive_set(true, false, .5f);
  CHECK(frame()); tfp_arm_drive_after_render();
  tfp_arm_drive_set(true, false, 1);
  for (unsigned n = 0; n < 10; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  CHECK(stats().align.count == 1); /* not in the hold */
  tfp_arm_drive_set_hold(false);
  body_is(body_now);
  CHECK(!frame());
  s = stats();
  CHECK(s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.binds == 1);
  CHECK(strstr(tfp_arm_drive_align_line(&s.align), "taken again now that the arms show"));
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 2 && stats().binds == 1 && apart_m(&shown, rest) < .0001f);

  /* the same with the gun away at full weight (a hold that is ready, the gun on the body): not taken there either */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set_hold_ready(true);
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw);
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  break_gun_parent();
  body_is(body_crouch);
  tfp_arm_drive_set(true, false, 1);
  for (unsigned n = 0; n < 10; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  CHECK(stats().align.count == 1 && stats().binds == 1);
  mend_gun_parent();
  body_is(body_now);
  CHECK(!frame());
  CHECK(stats().align.count == 2 && stats().binds == 1);
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 2 && apart_m(&shown, rest) < .0001f);
  tfp_arm_drive_set_hold_ready(false);

  /* a binding that begins while the copy is fading (weight between 0 and 1): the body is still near the aim pose, and
   * its alignment is taken again at full weight like one taken hidden */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set(true, false, .5f);
  body_is(body_draw);
  CHECK(!frame());
  CHECK(stats().align.count == 1 && stats().align.how == TFP_ARM_ALIGN_PROVISIONAL && stats().binds == 1);
  body_is(body_now);
  tfp_arm_drive_set(true, false, .8f); CHECK(!frame());
  CHECK(stats().align.count == 1);
  tfp_arm_drive_set(true, false, 1);
  CHECK(!frame());
  s = stats();
  CHECK(s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.eased && s.align.moved_m > .05f && s.binds == 1);
  CHECK(strstr(tfp_arm_drive_align_line(&s.align), "taken again now that the arms show"));
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 2 && stats().binds == 1 && apart_m(&shown, rest) < .0001f);

  /* the first fully shown frame cannot give an alignment (the body's socket is not rigid there): nothing is taken
   * from it. No pose can be made of that frame either, so it is refused and the binding ends; the next frame's new
   * binding takes its alignment fully shown, and nothing more is owed. */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw);
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  CHECK(stats().align.count == 1 && stats().binds == 1);
  body_is(body_now);
  bend_socket();
  tfp_arm_drive_set(true, false, 1);
  CHECK(!frame());
  CHECK(stats().align.count == 1 && stats().binds == 1 && stats().refused == 1 && stats().reason == TFP_ARM_POSE);
  body_is(body_now);
  CHECK(!frame());
  s = stats();
  shown = socket_shown();
  CHECK(s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && !s.align.eased && s.binds == 2 && apart_m(&shown, rest) < .0001f);
  CHECK(!strstr(tfp_arm_drive_align_line(&s.align), "taken again"));
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 2 && stats().binds == 2 && apart_m(&shown, rest) < .0001f);

  /* aim, then an action while still aiming, and one frame of the hold is not driven (the game replaces a unit, a
   * proof fails): the binding begins again inside the hold at full weight. Its alignment is of a mid-action frame,
   * no better than the aim pose: it is taken again on the first fully shown frame after the hold. */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set(true, false, 0);
  body_is(body_draw);
  for (unsigned n = 0; n < 10; n++) CHECK(!frame());
  tfp_arm_drive_set_hold(true);
  body_is(body_crouch);
  tfp_arm_drive_set(true, false, 1);
  for (unsigned n = 0; n < 5; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  request(4); /* one frame in another camera: the binding is gone */
  CHECK(!frame());
  request(6);
  for (unsigned n = 0; n < 5; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  s = stats();
  CHECK(s.binds == 2 && s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && !s.align.eased);
  CHECK(!strstr(tfp_arm_drive_align_line(&s.align), "taken again"));
  tfp_arm_drive_set_hold(false);
  body_is(body_now);
  CHECK(!frame());
  s = stats();
  CHECK(s.align.count == 3 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.eased && s.align.moved_m > .05f && s.binds == 2);
  CHECK(strstr(tfp_arm_drive_align_line(&s.align), "taken in the middle of an action is taken again now that the action is over"));
  CHECK(!strstr(tfp_arm_drive_align_line(&s.align), "while aiming"));
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 3 && stats().binds == 2 && apart_m(&shown, rest) < .0001f);

  /* no aim at all: a binding that begins with the gun away at full weight (a hold that is ready, the gun on the body)
   * is of a mid-action frame too. The gun comes back to the same binding: taken again there, and the log's one line
   * for that frame is the retake with what it moved, not the returning gun's "provisional". */
  rest_start();
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set_hold_ready(true);
  break_gun_parent();
  body_is(body_crouch);
  for (unsigned n = 0; n < 10; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  s = stats();
  CHECK(s.binds == 1 && s.align.count == 1 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && !s.align.weapon);
  mend_gun_parent();
  body_is(body_now);
  CHECK(!frame());
  s = stats();
  CHECK(s.binds == 1 && s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.weapon == GUN && s.align.eased && s.align.moved_m > .05f);
  CHECK(strstr(tfp_arm_drive_align_line(&s.align), "taken in the middle of an action is taken again now that the action is over"));
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 2 && stats().binds == 1 && apart_m(&shown, rest) < .0001f);
  tfp_arm_drive_set_hold_ready(false);

  /* the same, and the gun that comes back has a kept rest alignment: the kept one replaces the provisional one on that
   * frame. Nothing is taken again first: one record, and what it moved is measured from the alignment that was shown. */
  rest_start();
  CHECK(!frame());
  kept_by_stillness();
  const uint64_t kept_count = stats().align.count;
  request(4);
  for (unsigned n = 0; n < 12; n++) CHECK(!frame());
  request(6);
  stance = TFP_ARM_REST_NONE;
  tfp_arm_drive_set_hold_ready(true);
  break_gun_parent();
  body_is(body_crouch);
  for (unsigned n = 0; n < 10; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  s = stats();
  CHECK(s.binds == 2 && s.align.count == kept_count + 1 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && !s.align.weapon);
  mend_gun_parent();
  body_is(body_now);
  CHECK(!frame());
  s = stats();
  shown = socket_shown(); /* this frame shows the mid-action alignment still: the ease begins from it */
  const float kept_off = apart_m(&shown, rest), kept_turned = apart_degrees(&shown, rest);
  CHECK(kept_off > .05f && kept_turned > 5);
  CHECK(s.binds == 2 && s.align.count == kept_count + 2 && s.align.how == TFP_ARM_ALIGN_KEPT && s.align.weapon == GUN && s.align.eased && !s.align.retake);
  CHECK(fabsf(s.align.moved_m - kept_off) < .001f && fabsf(s.align.moved_degrees - kept_turned) < .1f);
  CHECK(!strstr(tfp_arm_drive_align_line(&s.align), "taken again"));
  eases(kept_off);
  for (unsigned n = 0; n < 60; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == kept_count + 2 && stats().binds == 2 && apart_m(&shown, rest) < .0001f);
  tfp_arm_drive_set_hold_ready(false);
}

/* What is not a rest: an action (the tick says so), a hold, a gun that is away, a body that keeps moving, layer states
 * that keep changing, less than 350 ms. */
static void test_rest_gates(void) {
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  rest_start();
  tfp_arm_drive_set_hold_ready(true);
  body_is(body_draw);
  CHECK(!frame());
  body_is(body_now);
  stance = TFP_ARM_REST_NONE; /* a reload, an aim, a sprint: still for a second, never measured */
  for (unsigned n = 0; n < 30; n++) CHECK(!frame());
  CHECK(stats().align.count == 1);
  stance = TFP_ARM_REST_STAND;
  for (unsigned n = 0; n < 9; n++) CHECK(!frame()); /* 300 ms of rest */
  CHECK(stats().align.count == 1);
  tfp_arm_drive_set_hold(true); /* the controller holds through an action */
  for (unsigned n = 0; n < 30; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  CHECK(stats().align.count == 1);
  tfp_arm_drive_set_hold(false);
  break_gun_parent(); /* the gun is on the body */
  for (unsigned n = 0; n < 30; n++) { CHECK(frame()); tfp_arm_drive_after_render(); }
  CHECK(stats().align.count == 1 && stats().binds == 1);
  mend_gun_parent();
  for (unsigned n = 0; n < 30; n++) { /* the body never stays */
    body_is(n & 1 ? body_now : body_crouch);
    CHECK(!frame());
  }
  CHECK(stats().align.count == 1);
  { /* the gun hand waits while the left hand works (a reload: "idle" to the tick) */
    const TfpGraft map = bound_map();
    const unsigned l_hand = map.body[13 - 10];
    CHECK(fn[13] == TFP_GRAFT_NAME_L_HAND);
    for (unsigned n = 0; n < 30; n++) {
      TfpGraftMatrix hand = body_now[l_hand];
      hand.values[14] += n & 1 ? .05f : 0;
      body_is(body_now);
      put(worlds_of(BODY) + l_hand * 64, &hand, 64);
      CHECK(!frame());
    }
    CHECK(stats().align.count == 1);
    body_is(body_now);
    CHECK(!frame()); /* the left hand is back: the first of the 350 ms */
  }
  for (unsigned n = 0; n < 36; n++) { /* the body is still, its animation goes from state to state every 300 ms */
    pose_key = (n / 9) & 1 ? 7 : 5;
    CHECK(!frame());
  }
  CHECK(stats().align.count == 1);
  TfpGraftLocal shown = socket_shown();
  CHECK(settle(30) == 3 && stats().align.how == TFP_ARM_ALIGN_TENTATIVE && stats().align.key == 7); /* the 350 ms count from when it all holds */
  CHECK(!frame());
  shown = socket_shown();
  eases(apart_m(&shown, rest) + .0001f);
  CHECK(stats().binds == 1 && !stats().refused);
  /* the clock never told (no tick-side caller): the first driven frame's alignment for good, as before */
  fresh();
  tfp_arm_drive_set(true, false, 1);
  for (unsigned n = 0; n < 40; n++) CHECK(!tfp_arm_drive_render(WORLD, true));
  CHECK(stats().align.count == 1 && stats().align.how == TFP_ARM_ALIGN_PROVISIONAL);
}

/* One measurement is not a weapon's resting place (the tick's "at rest" is also a pause in a draw, a reload, an ammo
 * check): it is shown, and a new binding does not start from it. The pose the body comes back to is kept. */
static void test_rest_first(void) {
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  rest_start();
  body_is(body_draw); /* bound in a pause of the draw, and it lasts 400 ms */
  CHECK(!frame() && settle(30) == 11);
  TfpArmDriveStats s = stats();
  CHECK(s.align.count == 2 && s.align.how == TFP_ARM_ALIGN_TENTATIVE && s.align.moved_m < .0001f);
  /* the rest: far from what was measured in the draw, so shown in its place */
  body_is(body_now);
  CHECK(!frame());
  TfpGraftLocal shown = socket_shown();
  const float off = apart_m(&shown, rest);
  CHECK(off > .05f && settle(30) == 11);
  s = stats();
  CHECK(s.align.count == 3 && s.align.how == TFP_ARM_ALIGN_TENTATIVE && s.align.eased && fabsf(s.align.moved_m - off) < .001f);
  eases(off);
  /* the body sways 20 mm and stays there: a measurement, not far from the one shown and not the same place: nothing changes */
  body_is(body_sway);
  for (unsigned n = 0; n < 20; n++) CHECK(!frame());
  CHECK(stats().align.count == 3);
  /* a swap and back before anything was confirmed: nothing was kept */
  request(4);
  for (unsigned n = 0; n < 3; n++) CHECK(!frame());
  request(6);
  body_is(body_draw);
  CHECK(!frame());
  s = stats();
  CHECK(s.binds == 2 && s.align.count == 4 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.weapon == GUN);
  body_is(body_now);
  CHECK(settle(30) == 12 && stats().align.count == 5 && stats().align.how == TFP_ARM_ALIGN_TENTATIVE);
  for (unsigned n = 0; n < 8; n++) CHECK(!frame());
  body_is(body_sway); /* the sway takes the body out of that stretch of stillness, and back */
  for (unsigned n = 0; n < 3; n++) CHECK(!frame());
  body_is(body_now);
  CHECK(settle(30) == 12);
  s = stats();
  CHECK(s.align.count == 6 && s.align.how == TFP_ARM_ALIGN_REST && s.align.rest_ms >= 350 && s.align.rest_ms < 400 && s.align.moved_m < .0001f);
  request(4);
  for (unsigned n = 0; n < 3; n++) CHECK(!frame());
  request(6);
  body_is(body_draw);
  CHECK(!frame());
  s = stats();
  CHECK(s.binds == 3 && s.align.count == 7 && s.align.how == TFP_ARM_ALIGN_KEPT);
  body_is(body_now);
  CHECK(!frame());
  shown = socket_shown();
  CHECK(apart_m(&shown, rest) < .0001f);
}

/* A kept alignment is not given up for a held pose, however long it is held (an ammo check, the weapon wheel, a paused
 * game: "idle" to the tick, and still), nor for the sway of the rest. It is given up for a pose the body comes back
 * to: two measurements in a row, from two stretches of stillness, that agree and are far from the kept one. */
static void test_rest_kept_stays(void) {
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  rest_start();
  CHECK(!frame());
  kept_by_stillness();
  CHECK(stats().align.count == 3);
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  body_is(body_crouch);
  for (unsigned n = 0; n < 150; n++) CHECK(!frame()); /* 5 s */
  TfpGraftLocal shown = socket_shown();
  CHECK(stats().align.count == 3 && apart_m(&shown, rest) > .03f);
  body_is(body_now);
  for (unsigned n = 0; n < 90; n++) CHECK(!frame()); /* back: on the resting place, nothing taken */
  shown = socket_shown();
  CHECK(stats().align.count == 3 && apart_m(&shown, rest) < .0001f);
  for (unsigned n = 0; n < 240; n++) { /* the rest sways: 400 ms here, 400 ms 20 mm away, measured each time */
    body_is((n / 12) & 1 ? body_sway : body_now);
    CHECK(!frame());
  }
  CHECK(stats().align.count == 3);
  body_is(body_now);
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  TfpArmDriveRest seen;
  tfp_arm_drive_rest_take(&seen);
  /* the other pose once (400 ms) */
  body_is(body_crouch);
  for (unsigned n = 0; n < 13; n++) CHECK(!frame());
  shown = socket_shown();
  const float off = apart_m(&shown, rest);
  CHECK(stats().align.count == 3 && off > .03f);
  { /* held on, and its own sway (20 mm for two frames) cuts it into stretches that each give a measurement in the same place: one visit */
    static TfpGraftMatrix held_sway[TFP_GRAFT_BODY_NODES];
    unsigned head = 0;
    for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; i++)
      if (bn[i] == TFP_GRAFT_NAME_HEAD) head = i;
    memcpy(held_sway, body_crouch, sizeof held_sway);
    held_sway[head].values[12] += .02f;
    for (unsigned n = 0; n < 75; n++) {
      body_is(n % 15 < 2 ? held_sway : body_crouch);
      CHECK(!frame());
    }
    CHECK(stats().align.count == 3);
  }
  /* away, and the other pose again */
  body_is(body_draw);
  for (unsigned n = 0; n < 2; n++) CHECK(!frame());
  body_is(body_crouch);
  CHECK(settle(30) == 12);
  const TfpArmDriveStats s = stats();
  CHECK(s.align.count == 4 && s.align.how == TFP_ARM_ALIGN_REST && s.align.rest_ms >= 350 && s.align.rest_ms < 400 && s.align.eased &&
        fabsf(s.align.moved_m - off) < .001f);
  eases(off);
  for (unsigned n = 0; n < 90; n++) CHECK(!frame());
  CHECK(stats().align.count == 4 && stats().binds == 1);
  /* what the log's line is made of: 13 + 75 + 2 + 12 + 5 + 90 frames; one measurement, five in the held pose, the confirming one */
  tfp_arm_drive_rest_take(&seen);
  CHECK(seen.frames == 197 && seen.rest_frames == 197 && seen.measured == 7 && seen.weapon == GUN && seen.rest == TFP_ARM_REST_STAND &&
        seen.how == TFP_ARM_ALIGN_KEPT && seen.off_m < .0001f && seen.least_off_m < .0001f && seen.most_off_m > off /* the two frames away */ &&
        seen.most_off_degrees > 5 && seen.still_ms > 2900 && seen.longest_still_ms == seen.still_ms);
  CHECK(strstr(tfp_arm_drive_rest_line(&seen), "197 frames, 197 at rest (standing), layers not known, socket 0.000 to ") &&
        strstr(tfp_arm_drive_rest_line(&seen), "7 measured, alignment kept"));
  tfp_arm_drive_rest_take(&seen);
  CHECK(!seen.frames && !seen.measured && !seen.longest_still_ms);
}

/* Each stance has its own rest: crouched, the body holds the gun elsewhere; the alignment is measured there as well,
 * and standing up again changes back to the standing one at once, eased, instead of waiting for a new rest. */
static void test_rest_stances(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES], want[TFP_GRAFT_RIG_NODES];
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  rest_start();
  CHECK(!frame()); /* bound at rest: the first driven frame's alignment is already right */
  kept_by_stillness();
  TfpArmDriveStats s = stats();
  CHECK(s.align.count == 3);
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  const TfpGraft standing = rest_map(body_now), crouched = rest_map(body_crouch);
  stance = TFP_ARM_REST_CROUCH;
  body_is(body_crouch);
  CHECK(!frame());
  TfpGraftLocal shown = socket_shown();
  const float off = apart_m(&shown, rest);
  CHECK(off > .03f && stats().align.count == 3);
  CHECK(settle(30) == 11); /* this frame was the first of the 350 ms */
  s = stats();
  CHECK(s.align.count == 4 && s.align.how == TFP_ARM_ALIGN_TENTATIVE && s.align.stance == TFP_ARM_REST_CROUCH && s.align.eased);
  eases(off);
  CHECK(settle(90) == 45 && stats().align.count == 5 && stats().align.how == TFP_ARM_ALIGN_REST && stats().align.stance == TFP_ARM_REST_CROUCH);
  /* up again: the standing alignment, from what is shown */
  stance = TFP_ARM_REST_STAND;
  body_is(body_now);
  CHECK(!frame());
  s = stats();
  CHECK(s.align.count == 6 && s.align.how == TFP_ARM_ALIGN_KEPT && s.align.stance == TFP_ARM_REST_STAND && s.align.eased && s.align.moved_m > .03f);
  wanted(&crouched, body_now, game_locals, 1, want);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now)); /* the frame of the change still shows the alignment that was in use */
  shown = socket_shown();
  eases(apart_m(&shown, rest));
  wanted(&standing, body_now, game_locals, 1, want);
  rig_locals(now);
  CHECK(!memcmp(now, want, sizeof now));
  /* an action between two stances (not at rest) keeps what is in use; crouching again changes to the crouched one */
  stance = TFP_ARM_REST_NONE;
  for (unsigned n = 0; n < 20; n++) CHECK(!frame());
  CHECK(stats().align.count == 6);
  stance = TFP_ARM_REST_CROUCH;
  body_is(body_crouch);
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 7 && stats().align.how == TFP_ARM_ALIGN_KEPT && apart_m(&shown, rest) < .0001f);
  for (unsigned n = 0; n < 90; n++) CHECK(!frame());
  CHECK(stats().align.count == 7 && stats().binds == 1);
}

/* The wielded weapon's unit: its entity row and its weapon component (tests/fake_native.h). */
static void gun_is(uint32_t ref) { n32(WORLD + FP_ENTITY_ROWS + 24 + 0xc, ref), n32(0xa00000000ULL + 0x400 + 0xc, ref); }
/* Another gun on a binding that a lease keeps: the alignment is the weapon's, not the binding's. */
static void test_rest_other_gun(void) {
  const TfpGraftLocal *rest = &game_locals[TFP_GRAFT_RIG_SOCKET];
  rest_start();
  tfp_arm_drive_set_hold_ready(true);
  CHECK(!frame());
  kept_by_stillness();
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  make_unit(GUN2, 4, object_of(RIG), (8u << 16) | 2);
  gun_is(GUN2);
  body_is(body_draw); /* the sidearm's hold, as far as this test goes */
  CHECK(!frame());
  TfpArmDriveStats s = stats();
  CHECK(s.binds == 1 && s.align.count == 4 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.weapon == GUN2 && !s.refused);
  TfpGraftLocal shown = socket_shown();
  const float off = apart_m(&shown, rest);
  CHECK(off > .05f && settle(30) == 11);
  s = stats();
  CHECK(s.align.count == 5 && s.align.how == TFP_ARM_ALIGN_TENTATIVE && s.align.weapon == GUN2);
  eases(off);
  CHECK(settle(90) == 45 && stats().align.count == 6 && stats().align.how == TFP_ARM_ALIGN_REST && stats().align.weapon == GUN2);
  /* the first gun again, its draw still running: its own resting alignment, eased from what is shown */
  gun_is(GUN);
  body_is(body_crouch);
  CHECK(!frame());
  s = stats();
  CHECK(s.binds == 1 && s.align.count == 7 && s.align.how == TFP_ARM_ALIGN_KEPT && s.align.weapon == GUN && s.align.eased);
  body_is(body_now);
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(apart_m(&shown, rest) < .0001f);
  for (unsigned n = 0; n < 90; n++) CHECK(!frame());
  CHECK(stats().align.count == 7 && stats().binds == 1);
  /* and the second: kept as well */
  gun_is(GUN2);
  body_is(body_draw);
  for (unsigned n = 0; n < 6; n++) CHECK(!frame());
  shown = socket_shown();
  CHECK(stats().align.count == 8 && stats().align.how == TFP_ARM_ALIGN_KEPT && stats().align.weapon == GUN2 && apart_m(&shown, rest) < .0001f);
  /* a third gun, nothing kept for it, while the ease to the first one's kept alignment is under way: provisional and
   * no more; an ease begun for something else is not a retake in the log */
  gun_is(GUN);
  body_is(body_crouch);
  CHECK(!frame());
  CHECK(stats().align.count == 9 && stats().align.how == TFP_ARM_ALIGN_KEPT && stats().align.eased);
  make_unit(GUN3, 4, object_of(RIG), (8u << 16) | 2);
  gun_is(GUN3);
  CHECK(!frame());
  s = stats();
  CHECK(s.binds == 1 && s.align.count == 10 && s.align.how == TFP_ARM_ALIGN_PROVISIONAL && s.align.weapon == GUN3);
  CHECK(!strstr(tfp_arm_drive_align_line(&s.align), "taken again") && strstr(tfp_arm_drive_align_line(&s.align), "provisional until its first rest"));
}

/* What the tick calls a rest (tfp_arm_drive_rest_of): the action alone is not enough. */
static void test_rest_of(void) {
  const TfpArmRestSample still = {.stance = TFP_ARM_REST_STAND, .has_speed = true, .has_pitch = true, .has_layers = true, .l1 = 75, .l3 = 68, .l14 = 36};
  TfpArmRestSample s = still;
  uint32_t key = 9, other = 9;
  unsigned why = 9;
  CHECK(tfp_arm_drive_rest_of(&s, &key, &why) == TFP_ARM_REST_STAND && why == TFP_ARM_WHY_REST && key);
  CHECK(tfp_arm_drive_rest_of(&s, NULL, NULL) == TFP_ARM_REST_STAND);
  s.stance = TFP_ARM_REST_PRONE;
  CHECK(tfp_arm_drive_rest_of(&s, NULL, &why) == TFP_ARM_REST_PRONE);
  s.stance = TFP_ARM_REST_NONE; /* an aim, a sprint, a reload the game flags, a hold */
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_ACTION);
  s.stance = TFP_ARM_REST_KINDS;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_ACTION);
  s = still, s.fire = true; /* hip fire is "idle" */
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_FIRE);
  s = still, s.speed = .46; /* a walk is "idle" (the session's log: action=idle speed=0.46) */
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_MOVING);
  s.speed = .04;
  CHECK(tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_REST);
  s.speed = NAN;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_MOVING);
  s = still, s.has_speed = false;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_MOVING);
  s = still, s.pitch = -.5; /* looking down at a weapon on the ground */
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_PITCH);
  s.pitch = .5;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_PITCH);
  s.pitch = -.2618; /* 15 degrees: seen from there the body's socket is some 20 mm from where it is at level, which is
                     * inside the sway of a rest, so a rest kept there would never be replaced at level */
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_PITCH);
  s.pitch = .2618;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_PITCH);
  s.pitch = -.1222; /* 7 degrees: within the 8 a rest is measured in */
  CHECK(tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_REST);
  s.pitch = .1222;
  CHECK(tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_REST);
  s.pitch = NAN;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_PITCH);
  s = still, s.has_pitch = false;
  CHECK(!tfp_arm_drive_rest_of(&s, NULL, &why) && why == TFP_ARM_WHY_PITCH);
  /* the layer states: one number per triple, 0 when not all three are known; not knowing them is not a refusal */
  s = still, s.l3 = 71;
  CHECK(tfp_arm_drive_rest_of(&s, &other, NULL) && other && other != key);
  s = still, s.l14 = -1;
  CHECK(tfp_arm_drive_rest_of(&s, &other, NULL) && other && other != key);
  s = still, s.has_layers = false;
  CHECK(tfp_arm_drive_rest_of(&s, &other, &why) == TFP_ARM_REST_STAND && !other);
  s.stance = TFP_ARM_REST_NONE, other = 9;
  CHECK(!tfp_arm_drive_rest_of(&s, &other, NULL) && !other);
}

/* The one recording there is of a standing player (the retired mod's 120 pose pairs, 75 ms apart, 9.0 s: every frame
 * of it, where the retired tree is there), played to the drive as a rest. The body's socket sways by up to 30 mm and
 * 8 degrees in it and is never still for longer than 1.1 s. What the rules make of it: measured once after 378 ms,
 * kept at the second measurement (1.9 s, 7.6 mm and 1.8 degrees from the first), never replaced in the seven seconds
 * that follow, and the gun never further from the rig's own socket than the sway. */
static void test_rest_recorded(const char *path) {
  enum { FRAMES = 120, RECORD = 64 * TFP_GRAFT_BODY_NODES + 64 * TFP_GRAFT_RIG_NODES + 16 + TFP_GRAFT_JOINTS * 64 + TFP_GRAFT_JOINTS * 64 +
                                3 * (16 + TFP_GRAFT_JOINTS * 64) + 16 + 64 + TFP_GRAFT_JOINTS * 64 + TFP_GRAFT_JOINTS * 64 };
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES];
  FILE *in = fopen(path, "rb");
  uint32_t head[4];
  uint64_t count = 0, tentative_ms = 0, kept_ms = 0;
  TfpArmDriveRest seen;
  CHECK(in && !fseek(in, 16, SEEK_SET) && fread(head, 4, 4, in) == 4 && head[0] == FRAMES);
  rest_start();
  clock_ms = 100000 - 75;
  for (unsigned n = 0; n < FRAMES; n++) {
    CHECK(!fseek(in, 32 + 8 * (TFP_GRAFT_BODY_NODES + TFP_GRAFT_RIG_NODES) + (3 * TFP_GRAFT_JOINTS + 4) * 4 + 128 + (long)n * RECORD, SEEK_SET) &&
          fread(body, 64, TFP_GRAFT_BODY_NODES, in) == TFP_GRAFT_BODY_NODES);
    if (!n) CHECK(!memcmp(body, body_now, sizeof body));
    body_is(body);
    clock_ms += 75 - 33;
    CHECK(!frame());
    const TfpArmDriveStats s = stats();
    if (s.align.count != count) {
      count = s.align.count;
      if (s.align.how == TFP_ARM_ALIGN_TENTATIVE) { CHECK(!tentative_ms); tentative_ms = clock_ms - 100000; }
      if (s.align.how == TFP_ARM_ALIGN_REST) { CHECK(!kept_ms); kept_ms = clock_ms - 100000; }
    }
    if (n == 25) tfp_arm_drive_rest_take(&seen); /* from here on the kept alignment is in use */
  }
  fclose(in);
  tfp_arm_drive_rest_take(&seen);
  printf("arm drive: the recording of a standing player: measured at %u ms, kept at %u ms; then %u measured, socket %.1f to %.1f mm / up to %.1f "
         "degrees off the rig's own, longest stillness %u ms\n", (unsigned)tentative_ms, (unsigned)kept_ms, (unsigned)seen.measured,
         (double)seen.least_off_m * 1000, (double)seen.most_off_m * 1000, (double)seen.most_off_degrees, (unsigned)seen.longest_still_ms);
  CHECK(count == 3 && stats().binds == 1 && tentative_ms == 375 && kept_ms == 1875);
  CHECK(seen.frames == 94 && seen.rest_frames == 94 && seen.how == TFP_ARM_ALIGN_KEPT && seen.measured >= 6 && seen.most_off_m < .020f &&
        seen.most_off_degrees < 6.5f && seen.longest_still_ms < 1200);
}

/* ---- the game sees its own socket (round 13, 2026-11-24) ----
 * The game's kind-6 evaluator seats the first-person eye by the rig's weapon socket (node 8; game+0x1287423 to
 * 0x1287785): it puts the rig so that the socket is at depth D before the eye. The model of it here: the eye's place
 * along the view is D less the socket's, the socket taken from the record the rig holds when the game next looks at
 * it, which is after the draw and whatever the drive does after the draw. With the game's values put back that record
 * is the game's own in every frame and the model eye does not move, whatever the body's hand does; with the written
 * values left, every move of the drawn socket along the view moves the eye the other way. */
static const float *view_axis(void) { return rig_world[TFP_GRAFT_RIG_EYE].values + 4; } /* the rig eye's forward: the Trace line's view= */
static float model_eye(void) {
  const float depth = .2102f; /* D as the game held it in the soaks of 2026-11-24 */
  const TfpGraftLocal socket = socket_shown();
  const float *root = rig_world[TFP_GRAFT_RIG_ROOT].values, *view = view_axis();
  float along = 0;
  for (unsigned i = 0; i < 3; i++) {
    float world = root[12 + i];
    for (unsigned j = 0; j < 3; j++) world += socket.position[j] * root[4 * j + i];
    along += world * view[i];
  }
  return depth - along;
}
/* The recorded body with its arms and gun `metres` further along the view: every node but the root and the head (the
 * arms are seen from the head, in the view's axes). */
static void pushed(float metres, TfpGraftMatrix *out) {
  memcpy(out, body_now, sizeof body_now);
  for (unsigned i = 1; i < TFP_GRAFT_BODY_NODES; i++)
    if (bn[i] != TFP_GRAFT_NAME_HEAD)
      for (unsigned k = 0; k < 3; k++) out[i].values[12 + k] += metres * view_axis()[k];
}
/* One tick and one primary draw `ms` after the one before, and what follows the draw. drawn: the rig as it was drawn. */
static bool frame_after(double ms, TfpGraftLocal drawn[TFP_GRAFT_RIG_NODES]) {
  static double clock;
  if (clock < (double)clock_ms) clock = (double)clock_ms;
  clock += ms, clock_ms = (uint64_t)clock;
  tfp_arm_drive_set_rest(clock_ms, stance, pose_key);
  const bool after = tfp_arm_drive_render(WORLD, true);
  if (drawn) rig_locals(drawn);
  if (after) tfp_arm_drive_after_render();
  return after;
}
static bool game_own(void) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  rig_locals(now);
  return !memcmp(now, game_locals, sizeof now);
}
/* A melee as the soak of 2026-11-24 had it (10:17:00): the hand and the gun 0.8 m along the view in 100 ms, 70 ms
 * there, back in 100 ms. */
static float melee_at(double ms) { return ms < 0 ? 0 : ms < 100 ? .8f * (float)(ms / 100) : ms < 170 ? .8f : ms < 270 ? .8f * (float)((270 - ms) / 100) : 0; }
static void test_game_sees_its_own_socket(void) {
  static const double times[] = {8, 16.7, 33.3, 50};
  /* what the binder asks for: put back, unless arms_live_keep.txt is beside the DLL; arms_live_restore.txt changes nothing */
  CHECK(tfp_arm_drive_put_back(false, false) && tfp_arm_drive_put_back(false, true) && !tfp_arm_drive_put_back(true, false) && !tfp_arm_drive_put_back(true, true));
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES];
  static TfpGraftLocal put_back[64][TFP_GRAFT_RIG_NODES], kept[TFP_GRAFT_RIG_NODES];
  float sock[64];
  for (unsigned at = 0; at < 4; at++) {
    const double dt = times[at];
    const unsigned frames = (unsigned)(400 / dt) + 2 < 64 ? (unsigned)(400 / dt) + 2 : 64;
    /* put back after every draw: the rule */
    rest_start();
    tfp_arm_drive_set(true, true, 1);
    CHECK(frame_after(dt, NULL) && game_own());
    const float eye = model_eye();
    CHECK(fabsf(stats().draw.sock_m) < .0005f && stats().draw.mode == TFP_ARM_DRAW_PUT_BACK); /* aligned on the rig's own socket */
    float most = 0;
    for (unsigned n = 0; n < frames; n++) {
      const float push = melee_at(n * dt - 30);
      pushed(push, body), body_is(body);
      CHECK(frame_after(dt, put_back[n]));
      /* drawn: the body's arms, the socket where the hand is; the Trace line's sock= says how far that is from the game's
       * own along the view (the rig's placement node turns the drawn arms a little against the view: within 6 %) */
      sock[n] = stats().draw.sock_m;
      CHECK(fabsf(sock[n] - push) <= .06f * push + .0005f);
      if (push > most) most = push;
      /* and when the game next looks: its own 63 records, to the bit, and so its own eye */
      CHECK(game_own() && stats().owned == 0 && model_eye() == eye);
    }
    CHECK(most > .5f && stats().binds == 1 && !stats().refused && stats().draw.count == frames + 1);
    /* the written values left (arms_live_keep.txt, the default until round 13): the same picture of the arms, bit for
     * bit, and the model eye thrown about by them */
    rest_start();
    CHECK(!frame_after(dt, NULL));
    CHECK(model_eye() == eye && stats().draw.mode == TFP_ARM_DRAW_KEPT);
    float before = eye, step = 0, jerk = 0, far = 0;
    for (unsigned n = 0; n < frames; n++) {
      pushed(melee_at(n * dt - 30), body), body_is(body);
      CHECK(!frame_after(dt, kept));
      CHECK(!memcmp(kept, put_back[n], sizeof kept));
      const float now = model_eye();
      CHECK(stats().draw.sock_m == sock[n] && fabsf((eye - now) - sock[n]) < .0002f); /* sock= is how far the eye was thrown */
      if (fabsf((now - before) - step) > jerk) jerk = fabsf((now - before) - step);
      if (fabsf(now - eye) > far) far = fabsf(now - eye);
      step = now - before, before = now;
    }
    CHECK(far > .5f);
    if (dt > 33 && dt < 34) CHECK(jerk >= .06f); /* over the judge's limit in a frame at 30 a second */
  }
}
/* The same through the two things of the mod's own that move the written socket with the body still: the aim fade
 * (the copy's weight 1 to 0 to 1 in 120 ms each way, the body's socket 0.455 m from the rig's own) and an alignment
 * taken at a rest and eased in over 150 ms. */
static void test_fade_and_ease_under_put_back(void) {
  static const double times[] = {8, 16.7, 33.3, 50};
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES];
  for (unsigned at = 0; at < 4; at++) {
    const double dt = times[at];
    rest_start();
    tfp_arm_drive_set(true, true, 1);
    CHECK(frame_after(dt, NULL));
    const float eye = model_eye();
    pushed(.455f, body), body_is(body);
    stance = TFP_ARM_REST_NONE; /* aiming: not a rest, nothing is measured */
    CHECK(frame_after(dt, NULL));
    const float full = stats().draw.sock_m;
    CHECK(fabsf(full - .455f) < .03f && game_own() && model_eye() == eye);
    float least = 1, most = 0;
    for (double ms = 0; ms <= 360; ms += dt) {
      const float weight = ms < 120 ? 1 - (float)(ms / 120) : ms < 240 ? 0 : ms < 360 ? (float)((ms - 240) / 120) : 1;
      tfp_arm_drive_set(true, true, weight);
      const bool after = frame_after(dt, NULL);
      CHECK(after == (weight > 0)); /* at weight 0 the targets are the game's own: nothing was written */
      CHECK(fabsf(stats().draw.sock_m - full * weight) < .002f);
      if (stats().draw.sock_m < least) least = stats().draw.sock_m;
      if (stats().draw.sock_m > most) most = stats().draw.sock_m;
      CHECK(game_own() && model_eye() == eye);
    }
    CHECK(least < .001f && most > .3f);
    /* at rest there, 0.455 m off: measured after 350 ms, shown at once (nothing is kept for the weapon) and eased */
    tfp_arm_drive_set(true, true, 1);
    stance = TFP_ARM_REST_STAND;
    const uint64_t count = stats().align.count;
    float last = 1;
    bool eased = false;
    for (double ms = 0; ms <= 700; ms += dt) {
      CHECK(frame_after(dt, NULL) || fabsf(stats().draw.sock_m) < .0005f);
      if (stats().align.count != count) eased = true;
      last = stats().draw.sock_m;
      CHECK(game_own() && model_eye() == eye);
    }
    CHECK(eased && stats().align.eased && stats().align.moved_m > .4f && fabsf(last) < .001f);
  }
}
/* The hold's edge (the soak of 2026-11-24: in 53 of 149 holds the eye made one step of 0.06 to 0.54 m along the view
 * 66 ms after "State: holding"; the socket going from written to the game's own). The drawn socket 0.2 m off along
 * the view, a hold begins: with the values put back in every frame the hold changes nothing the game sees. */
static void test_hold_begins_under_put_back(void) {
  static const double times[] = {8, 16.7, 33.3, 50};
  static TfpGraftMatrix body[TFP_GRAFT_BODY_NODES];
  for (unsigned at = 0; at < 4; at++) {
    const double dt = times[at];
    for (int keep = 0; keep < 2; keep++) {
      rest_start();
      tfp_arm_drive_set(true, !keep, 1);
      stance = TFP_ARM_REST_NONE; /* an action: no rest is measured, the entry's alignment stays */
      CHECK(frame_after(dt, NULL) == !keep);
      const float eye = model_eye();
      pushed(.2f, body), body_is(body);
      float worst = 0, before = 0;
      for (unsigned n = 0; n < 12; n++) {
        if (n == 6) tfp_arm_drive_set_hold_ready(true), tfp_arm_drive_set_hold(true); /* the tick before this draw sees the action */
        CHECK(frame_after(dt, NULL) == (!keep || n >= 6));
        CHECK(fabsf(stats().draw.sock_m - .2f) < .012f && stats().draw.mode == (n >= 6 ? TFP_ARM_DRAW_HOLD : keep ? TFP_ARM_DRAW_KEPT : TFP_ARM_DRAW_PUT_BACK));
        const float now = model_eye();
        if (n && fabsf(now - before) > worst) worst = fabsf(now - before);
        before = now;
        if (!keep) CHECK(game_own() && now == eye);
      }
      CHECK(stats().binds == 1 && !stats().refused);
      if (keep) CHECK(fabsf(worst - stats().draw.sock_m) < .0002f && worst > .18f && fabsf(before - eye) < .0005f); /* the fault: the whole 0.2 m in the hold's first frame */
      else CHECK(worst == 0);
      tfp_arm_drive_set_hold(false), tfp_arm_drive_set_hold_ready(false);
    }
  }
}


/* ---- the support hand on the weapon's grip (round 23; src/offhand_grip.h) ----
 * The game pins the body's left hand on the gun's grip node in the world while the rig is the view. Here the gun hangs
 * on the rig's socket as the rig shows it, and its grip node (name 67C94E4B) is put where the recorded body's left hand
 * is: what the game does. The drawn left hand is then to be on that node as the drive draws the gun. */
#define GRIP_NAME_T UINT32_C(0x67c94e4b)
static unsigned body_left_hand(void) {
  for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; i++) if (bn[i] == TFP_GRAFT_NAME_L_HAND) return i;
  CHECK(!"the body has a left hand");
  return 0;
}
/* The gun (and, with a foregrip, the optic standing in for one) evaluated on the rig's own socket; grip nodes on gun
 * node 2 and optic node 1; `on_gun`, `on_optic`: where each is, relative to the body's left hand (NULL: no such node). */
static void grip_world(const TfpGraftMatrix *gun_root, const float *on_gun, const float *on_optic) {
  put(worlds_of(GUN), gun_root, 64);
  put(worlds_of(GUN) + 64, gun_root, 64); /* node 1, the optic's mount: at the root */
  put(worlds_of(OPTIC), gun_root, 64);
  const TfpGraftMatrix hand = body_now[body_left_hand()];
  if (on_gun) {
    TfpGraftMatrix g = hand;
    for (unsigned i = 0; i < 3; i++) g.values[12 + i] += on_gun[i];
    n32(names_of(GUN) + 2 * 4, GRIP_NAME_T), put(worlds_of(GUN) + 2 * 64, &g, 64);
  }
  if (on_optic) {
    TfpGraftMatrix g = hand;
    for (unsigned i = 0; i < 3; i++) g.values[12 + i] += on_optic[i];
    n32(names_of(OPTIC) + 1 * 4, GRIP_NAME_T), put(worlds_of(OPTIC) + 64, &g, 64);
  }
}
/* The drawn hand (rig node 13) and socket (node 8) in node 7's frame, from the rig's locals as written. */
static void drawn(TfpGraftMatrix *hand, TfpGraftMatrix *socket) {
  TfpGraftLocal now[TFP_GRAFT_RIG_NODES];
  rig_locals(now);
  const TfpGraftMatrix upper = tfp_graft_local_matrix(&now[10]), fore = tfp_graft_local_matrix(&now[12]), h = tfp_graft_local_matrix(&now[13]);
  const TfpGraftMatrix elbow = tfp_graft_product(&fore, &upper);
  *hand = tfp_graft_product(&h, &elbow);
  *socket = tfp_graft_local_matrix(&now[TFP_GRAFT_RIG_SOCKET]);
}
static float grip_m(const TfpGraftMatrix *a, const TfpGraftMatrix *b) {
  float x = a->values[12] - b->values[12], y = a->values[13] - b->values[13], z = a->values[14] - b->values[14];
  return sqrtf(x * x + y * y + z * z);
}
static float grip_degrees(const TfpGraftMatrix *a, const TfpGraftMatrix *b) {
  const TfpOgFrame x = tfp_og_frame(a), y = tfp_og_frame(b);
  return (float)(tfp_og_angle(x.r, y.r) * 180 / TFP_OG_PI);
}
/* One driven frame under put-back at clock `now`: the drawn hand and socket, then the game's values back. */
static void grip_draw(uint64_t now, TfpGraftMatrix *hand, TfpGraftMatrix *socket) {
  tfp_arm_drive_set_rest(now, TFP_ARM_REST_NONE, 0);
  const bool after = tfp_arm_drive_render(WORLD, true); /* false only when nothing was written (weight 0) */
  drawn(hand, socket);
  if (after) tfp_arm_drive_after_render();
}
/* Where the grip is, relative to the socket, in the game: the hand drawn there sits on it. */
static TfpGraftMatrix grip_on_socket(const float *offset) {
  TfpGraftMatrix g = body_now[body_left_hand()];
  for (unsigned i = 0; i < 3; i++) g.values[12 + i] += offset[i];
  return tfp_graft_relative(&g, &rig_world[TFP_GRAFT_RIG_SOCKET]);
}

static void test_grip_holds(void) {
  static const float zero[3] = {0, 0, 0};
  TfpGraftMatrix hand, socket, first_hand, first_socket;
  fresh();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  tfp_arm_drive_set(true, true, 1);
  /* switched off (the default): the drive is what it was, the hand where the body's pose puts it, 0.3 m and more from the grip */
  grip_draw(1000, &first_hand, &first_socket);
  const TfpGraftMatrix want = grip_on_socket(zero), held_at = tfp_graft_product(&want, &first_socket);
  const float before = grip_m(&first_hand, &held_at);
  CHECK(before > .25f && before < TFP_OG_SHIFT_M);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  CHECK(!g.frames && g.state == TFP_ARM_GRIP_OFF);
  tfp_arm_drive_set_grip(true, false);
  float last = before;
  uint64_t t = 1000;
  for (int i = 1; i <= 30; i++) {
    grip_draw(t += 16, &hand, &socket);
    const TfpGraftMatrix at = tfp_graft_product(&want, &socket);
    const float now = grip_m(&hand, &at);
    if (i == 1) CHECK(now == last); /* its first frame has no time before it: nothing moves yet */
    else if (i < 5) CHECK(now < last); /* on its way */
    last = now;
    CHECK(!memcmp(&socket, &first_socket, sizeof socket)); /* the gun is drawn where it was: only the arm moves */
  }
  const TfpGraftMatrix at = tfp_graft_product(&want, &socket);
  printf("arm drive: grip: the drawn left hand from the grip: %.3f m / %.1f degrees with the grip off, %.4f m / %.2f degrees on it\n",
         (double)before, (double)grip_degrees(&first_hand, &held_at), (double)grip_m(&hand, &at), (double)grip_degrees(&hand, &at));
  CHECK(grip_m(&hand, &at) < .001f && grip_degrees(&hand, &at) < .01f);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.frames == 30 && g.held > 10 && g.state == TFP_ARM_GRIP_HELD && g.found && !g.attachment && g.nodes == 1 && g.units == 1);
  CHECK(g.body_m < .001f && g.to_m < .001f && g.weight == 1 && g.step_degrees <= g.limit_degrees + 1e-3f && g.limit_degrees > 11 && g.limit_degrees < 12);
  CHECK(strstr(tfp_arm_drive_grip_line(&g), "30 frames") && strstr(tfp_arm_drive_grip_line(&g), "on the grip; the gun's own grip node"));
  /* round 36: a gun whose only grip node is its own; the optic hangs on it without one. The units line says so, once */
  CHECK(g.tree_units == 2 && g.listed == 2 && g.target_unit == 0 && g.gate_unit == 0 && g.target_node == 2 && g.gate_node == 2);
  const char *units = tfp_arm_drive_grip_units_line(&g, true);
  CHECK(units && strstr(units, "the gun's units: 2, 1 with a grip node: [0] the gun 0000000000000000, 4 nodes, grip node 2; "
                               "[1] on [0] node 1 (00000000) 0000000000000000, 2 nodes, no grip node"));
  CHECK(!tfp_arm_drive_grip_units_line(&g, true) && tfp_arm_drive_grip_units_line(&g, false));
  /* the rest of the arm and the right side are the drive's: only nodes 10, 12, 13 and what hangs on them moved */
  TfpGraftLocal with[TFP_GRAFT_RIG_NODES], without[TFP_GRAFT_RIG_NODES];
  tfp_arm_drive_set_rest(t += 16, TFP_ARM_REST_NONE, 0);
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(with);
  tfp_arm_drive_after_render();
  tfp_arm_drive_set_grip(false, false);
  for (int i = 0; i < 20; i++) grip_draw(t += 16, &hand, &socket);
  /* switched off it went back to the body's pose and holds nothing of the reach */
  CHECK(!memcmp(&hand, &first_hand, sizeof hand));
  tfp_arm_drive_set_rest(t += 16, TFP_ARM_REST_NONE, 0);
  CHECK(tfp_arm_drive_render(WORLD, true));
  rig_locals(without);
  tfp_arm_drive_after_render();
  for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; n++) {
    const bool corrected = n == 10 || n == 12 || n == 13;
    CHECK(corrected ? memcmp(with[n].basis, without[n].basis, 36) != 0 : !memcmp(&with[n], &without[n], 64));
    CHECK(!memcmp(with[n].position, without[n].position, 12)); /* bone lengths: positions never change */
  }
}
/* Round 26, from the feel.52 Grip: lines: a grip further forward along the gun than the recorded one (a foregrip or
 * vertical grip), so that it is 1.05 of the drawn arm's length from the drawn shoulder, where the logged hand stopped
 * 0.03 to 0.05 m short of it (the 97 % straightening limit). The hand is to be on it, the shoulder forward. */
static void test_grip_reach_forward(void) {
  static const float zero[3] = {0, 0, 0};
  TfpGraftMatrix hand, socket;
  /* the body's hand stays on the node (the game pins it there); the gun's root 0.19 m further back along it puts the
   * node 0.19 m further forward on the gun, as the rig draws it */
  TfpGraftMatrix root = rig_world[TFP_GRAFT_RIG_SOCKET];
  for (unsigned i = 0; i < 3; i++) root.values[12 + i] -= .19f * root.values[4 + i];
  fresh();
  grip_world(&root, zero, NULL);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  float most_step = 0;
  for (int i = 0; i < 60; i++) {
    grip_draw(t += 33, &hand, &socket);
    if (i == 29) { TfpArmDriveGrip g; tfp_arm_drive_grip_take(&g); }
  }
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  most_step = g.held_step_degrees;
  const TfpGraftMatrix want = tfp_graft_relative(&body_now[body_left_hand()], &root), at = tfp_graft_product(&want, &socket);
  printf("arm drive: grip %.2f of the arm's length from the shoulder: the drawn hand %.4f m from it, the shoulder %.3f m forward, held step %.2f degrees\n",
         (double)g.reach, (double)grip_m(&hand, &at), (double)g.shoulder_m, (double)most_step);
  CHECK(g.reach > 1.02f && g.reach < 1.10f);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.held == 30 && grip_m(&hand, &at) < .001f && grip_degrees(&hand, &at) < .01f && g.to_m < .001f);
  CHECK(fabsf(g.shoulder_m - (g.reach - (float)TFP_OG_STRETCH) * (float)(.3027 + .2872)) < .002f && g.most_shoulder_m >= g.shoulder_m);
  CHECK(most_step < 1 && !g.cut && !g.out_of_reach && !g.too_far);
  const char *line = tfp_arm_drive_grip_line(&g);
  CHECK(strstr(line, "reach 1.0") && strstr(line, "shoulder forward 0.0") && strstr(line, "0 frames cut, not taken: 0 out of reach, 0 too far"));
  /* the fake engine does not evaluate the rig and gun again after the write: what it reads is what the game held. The
   * hand as drawn is a line of its own since round 36b (test_grip_drawn_line) */
  CHECK(g.drawn && !strstr(line, "as drawn") && strstr(tfp_arm_drive_grip_drawn_line(&g), "as drawn at the hip (30 frames, 30 held)"));
  /* aiming fading in or out (the copy's weight under 1): measured too since round 36b, apart from the hip */
  tfp_arm_drive_set(true, true, .5f);
  for (int i = 0; i < 10; i++) grip_draw(t += 33, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.held == 10 && g.drawn && !g.hip.frames && g.aim.frames == 10 && g.aim.held == 10);
  CHECK(strstr(tfp_arm_drive_grip_drawn_line(&g), "as drawn at the hip: not measured; while aiming (10 frames, 10 held)"));
  tfp_arm_drive_set(true, true, 1);
  for (int i = 0; i < 10; i++) grip_draw(t += 33, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.drawn && g.hip.frames == 10 && !g.aim.frames);
  CHECK(strstr(tfp_arm_drive_grip_drawn_line(&g), "while aiming: not measured"));
}
/* A foregrip: its own node, where the body's hand is, and the gun's own node 17 cm from there: the foregrip's is held. */
static void test_grip_foregrip(void) {
  static const float zero[3] = {0, 0, 0}, gun_own[3] = {0, .17f, 0};
  TfpGraftMatrix hand, socket;
  fresh();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], gun_own, zero);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  const TfpGraftMatrix want = grip_on_socket(zero), at = tfp_graft_product(&want, &socket);
  CHECK(grip_m(&hand, &at) < .001f);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  CHECK(g.found && g.attachment && g.nodes == 2 && g.units == 2 && g.state == TFP_ARM_GRIP_HELD);
  CHECK(strstr(tfp_arm_drive_grip_line(&g), "an attachment's grip node (2 grip nodes on 2 of the gun's units)"));
  /* the gun evaluated a frame behind its rig (root and nodes 4 cm off together): the grip read through the gun's own
   * matrices is the same relation, and the hand stays on it */
  TfpGraftMatrix behind = rig_world[TFP_GRAFT_RIG_SOCKET];
  behind.values[12] += .04f;
  static const float shifted[3] = {.04f, 0, 0}, shifted_gun[3] = {.04f, .17f, 0};
  grip_world(&behind, shifted_gun, shifted);
  for (int i = 0; i < 3; i++) grip_draw(t += 16, &hand, &socket);
  CHECK(grip_m(&hand, &at) < .002f);
}
/* Round 36 (the user's R-4 Hyena, "Hand on foregrip attachments is not working"): the game holds the body's left hand on
 * the gun's OWN grip node, while a fitted vertical grip carries its own node further forward (the Hyena's standard vertical
 * grip, mounted on the gun's attach_underbarrel node 7950E36E, has its node 0.169 m from the gun's: filediver, the unit
 * files). The hand is to go on the grip, the attachment's node; the body's hand on the gun's node is what says the game pins
 * it. Here the optic stands in for the grip unit, its node 0.12 m forward along the gun and 0.04 m down. */
enum { FOREGRIP = 603 };
static void grip_resources(void) {
  const uint64_t gun = UINT64_C(0xe5796355a8fd67e0);
  put(object_of(GUN) + 0x358, &gun, 8);
  n32(names_of(GUN) + 1 * 4, UINT32_C(0x7950e36e)); /* attach_underbarrel */
}
/* The grip fitted: a new unit, the gun's first child (the optic its sibling), on gun node 1, its grip node at `offset`
 * from the body's left hand. */
static void fit_grip(const float *offset) {
  const uint64_t grip = UINT64_C(0xf9df21c8e52cac95);
  make_unit(FOREGRIP, 2, object_of(GUN), (1u << 16) | 2);
  put(object_of(FOREGRIP) + 0x358, &grip, 8);
  p64(object_of(GUN) + 0x1d8, object_of(FOREGRIP)), p64(object_of(FOREGRIP) + 0x1e0, object_of(OPTIC));
  put(worlds_of(FOREGRIP), &rig_world[TFP_GRAFT_RIG_SOCKET], 64);
  TfpGraftMatrix g = body_now[body_left_hand()];
  for (unsigned i = 0; i < 3; i++) g.values[12 + i] += offset[i];
  n32(names_of(FOREGRIP) + 1 * 4, GRIP_NAME_T), put(worlds_of(FOREGRIP) + 64, &g, 64);
}
static void forward_of_socket(float along, float up, float *out) {
  const float *s = rig_world[TFP_GRAFT_RIG_SOCKET].values;
  for (unsigned i = 0; i < 3; i++) out[i] = along * s[4 + i] + up * s[8 + i];
}
static void test_grip_foregrip_body_on_gun(void) {
  static const float zero[3] = {0, 0, 0};
  float ahead[3];
  forward_of_socket(.12f, -.04f, ahead);
  TfpGraftMatrix hand, socket;
  /* first the gun alone, held on its own node; then the grip is fitted: the hand goes there within the step limits */
  fresh();
  grip_resources();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && !g.attachment && g.target_unit == 0);
  fit_grip(ahead);
  const TfpGraftMatrix want = grip_on_socket(ahead), own = grip_on_socket(zero);
  TfpGraftMatrix at = tfp_graft_product(&want, &socket);
  const float from_own = grip_m(&want, &own);
  const float first = grip_m(&hand, &at);
  int frames = 0;
  for (; frames < 60; frames++) {
    grip_draw(t += 16, &hand, &socket);
    at = tfp_graft_product(&want, &socket);
    const float now = grip_m(&hand, &at);
    CHECK(now <= first + .01f); /* on its way: the joints' limits allow a wobble, not a turn back to the gun's own node */
    tfp_arm_drive_grip_take(&g);
    CHECK(g.step_degrees <= g.limit_degrees + 1e-3f);
    if (now < .001f && grip_degrees(&hand, &at) < .01f) break;
  }
  printf("arm drive: grip: a vertical grip fitted %.3f m from the gun's own node, the body's hand on the gun's: the drawn hand on the "
         "grip after %d frames, %.4f m / %.2f degrees from it\n", (double)from_own, frames + 1, (double)grip_m(&hand, &at), (double)grip_degrees(&hand, &at));
  CHECK(frames < 40 && grip_m(&hand, &at) < .001f && grip_degrees(&hand, &at) < .01f);
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.held == 30 && g.found && g.attachment && g.nodes == 2 && g.units == 2 && g.body_m < .001f);
  CHECK(g.target_unit == 1 && g.target_node == 1 && g.gate_unit == 0 && g.gate_node == 2 && g.drawn);
  CHECK(grip_m(&hand, &at) < .001f && g.step_degrees <= g.limit_degrees + 1e-3f);
  CHECK(strstr(tfp_arm_drive_grip_line(&g), "on the grip; an attachment's grip node (2 grip nodes on 2 of the gun's units), the body's hand 0.000 m from the gun's own"));
  const char *units = tfp_arm_drive_grip_units_line(&g, false);
  CHECK(strstr(units, "the gun's units: 3, 2 with a grip node: [0] the gun e5796355a8fd67e0, 4 nodes, grip node 2; [1] on [0] node 1 (7950e36e) "
                      "f9df21c8e52cac95, 2 nodes, grip node 1; [2] on [0] node 1 (7950e36e) 0000000000000000, 2 nodes, no grip node"));
  /* steady: the target stays the grip's while the body's hand stays on the gun's own node (no drift between the two) */
  TfpGraftMatrix before = hand;
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  CHECK(!memcmp(&hand, &before, sizeof hand));
  /* the body's hand leaves the gun (a reload): the arm goes back to the body's pose; then back on the grip */
  static const float far[3] = {.2f, 0, 0};
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], far, NULL);
  for (int i = 0; i < 5; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_FREE);
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  for (int i = 0; i < 60; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.target_unit == 1 && grip_m(&hand, &at) < .001f);
}
/* Round 36b (E of the Adjudicator diagnosis): the BR-14's standard vertical grip has its node 0.084 m straight below the gun's
 * own, nearer than the 0.10 m the latch keeps a node for. Wherever the body's hand is latched (on the gun's own node, on the
 * attachment's, or between the two inside the keep), the hand holds the attachment's node and never the gun's own. */
static void test_grip_attachment_never_gun_own(void) {
  float below[3], between[3];
  forward_of_socket(0, -.084f, below);
  forward_of_socket(0, -.05f, between);
  TfpGraftMatrix hand, socket;
  fresh();
  grip_resources();
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  /* where the two nodes are from the body's hand (the test world puts nodes against the hand, so the hand moving is the
   * nodes moving the other way): on the gun's own node; 0.05 m below it, the latch keeping the gun's own (0.034 m from
   * the attachment's: neither pins); on the attachment's; back on the gun's own */
  float gun[4][3], att[4][3];
  for (unsigned i = 0; i < 3; i++) {
    gun[0][i] = 0, att[0][i] = below[i];
    gun[1][i] = -between[i], att[1][i] = below[i] - between[i];
    gun[2][i] = -below[i], att[2][i] = 0;
    gun[3][i] = 0, att[3][i] = below[i];
  }
  static const int32_t gate[4] = {0, 0, 1, 0};
  for (unsigned phase = 0; phase < 4; phase++) {
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], gun[phase], NULL);
    fit_grip(att[phase]);
    TfpArmDriveGrip g = {0};
    for (int i = 0; i < 60; i++) {
      grip_draw(t += 16, &hand, &socket);
      tfp_arm_drive_grip_take(&g);
      CHECK(g.found && g.attachment && g.target_unit == 1); /* every frame: never the gun's own */
    }
    CHECK(g.state == TFP_ARM_GRIP_HELD && g.gate_unit == gate[phase]);
    const TfpGraftMatrix want = grip_on_socket(att[phase]), at = tfp_graft_product(&want, &socket);
    CHECK(grip_m(&hand, &at) < .001f);
  }
  printf("arm drive: grip: an attachment's node 0.084 m below the gun's own: held whichever node the body's hand is latched on\n");
}
/* Round 36b (A of the Adjudicator diagnosis): the hand as drawn, which the Grip line has to prove each cause with: the
 * wrist (rig node 13) from the node it holds, its turn from the node's, the palm (0.05 m along each one's X) from the
 * node's palm point, the fingers' curl (middle and index, joints 1 to 2, X to X) and the wrist from the gun's own node; at
 * the hip and while aiming apart. Here the rig's evaluated matrices are put by hand (the fake engine does not evaluate)
 * and each number is checked against them; and the two lines stay inside say()'s 700 characters, whatever the numbers. */
static void put_rig_world(unsigned node, const TfpGraftMatrix *m) { put(worlds_of(RIG) + 64 * (uintptr_t)node, m, 64); }
static TfpGraftMatrix own_turn(const TfpGraftMatrix *m, unsigned row, double degrees) {
  TfpOgFrame f = tfp_og_frame(m);
  double axis[3];
  memcpy(axis, f.r[row], sizeof axis);
  tfp_og_turn_rows(f.r, axis, degrees * TFP_OG_PI / 180);
  TfpGraftMatrix out = *m;
  for (unsigned i = 0; i < 3; i++)
    for (unsigned j = 0; j < 3; j++) out.values[4 * i + j] = (float)f.r[i][j];
  return out;
}
static void test_grip_drawn_line(void) {
  static const float zero[3] = {0, 0, 0};
  float ahead[3];
  forward_of_socket(.12f, -.04f, ahead);
  TfpGraftMatrix hand, socket;
  fresh();
  grip_resources();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  fit_grip(ahead);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  for (int i = 0; i < 40; i++) grip_draw(t += 16, &hand, &socket);
  /* the node it holds, the gun's own, and the drawn wrist: 1 cm along the node's X from it, turned 30 degrees about its Y */
  TfpGraftMatrix node = body_now[body_left_hand()], own = node;
  for (unsigned i = 0; i < 3; i++) node.values[12 + i] += ahead[i];
  TfpGraftMatrix wrist = own_turn(&node, 1, 30);
  for (unsigned i = 0; i < 3; i++) wrist.values[12 + i] += .01f * node.values[i];
  put_rig_world(13, &wrist);
  /* the fingers: middle 1 to 2 turned 60 degrees, index 1 to 2 turned 40 */
  const TfpGraftMatrix m2 = own_turn(&wrist, 1, 60), i2 = own_turn(&wrist, 1, 40);
  put_rig_world(20, &wrist), put_rig_world(21, &m2), put_rig_world(23, &wrist), put_rig_world(24, &i2);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  for (int i = 0; i < 5; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  double palm[3];
  for (unsigned i = 0; i < 3; i++) palm[i] = wrist.values[12 + i] + .05 * wrist.values[i] - node.values[12 + i] - .05 * node.values[i];
  const double palm_m = tfp_og_len(palm), own_m = grip_m(&wrist, &own);
  printf("arm drive: grip as drawn: wrist %.4f m, turned %.2f degrees, palm %.4f m, fingers %.2f degrees, %.4f m from the gun's own node\n",
         (double)g.hip.m, (double)g.hip.turn, (double)g.hip.palm_m, (double)g.hip.curl, (double)g.hip.own_m);
  CHECK(g.drawn && g.hip.frames == 5 && g.hip.held == 5 && !g.aim.frames);
  CHECK(fabsf(g.hip.m - .01f) < 1e-5f && fabsf(g.hip.most_m - .01f) < 1e-5f);
  CHECK(fabsf(g.hip.turn - 30) < .01f && fabsf(g.hip.most_turn - 30) < .01f);
  CHECK(fabs(g.hip.palm_m - palm_m) < 1e-5 && fabs(g.hip.most_palm_m - palm_m) < 1e-5);
  CHECK(fabsf(g.hip.curl - 50) < .01f && fabsf(g.hip.least_curl - 50) < .01f);
  CHECK(fabs(g.hip.own_m - own_m) < 1e-5);
  const char *line = tfp_arm_drive_grip_drawn_line(&g);
  CHECK(line && strstr(line, "as drawn at the hip (5 frames, 5 held): the hand 0.010 m from the grip (most 0.010), turned 30.0 degrees (most 30.0), palm "));
  CHECK(strstr(line, "fingers curled 50.0 degrees (least 50.0)") && strstr(line, "m from the gun's own node; while aiming: not measured"));
  /* aiming: the same numbers in the other half */
  tfp_arm_drive_set(true, true, 0);
  for (int i = 0; i < 3; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  /* (the weight dropped at once, not over arm_weight.h's 120 ms: the arm eases to the grip on the game's aim pose, so these
   * frames are measured, not yet held) */
  CHECK(!g.hip.frames && g.aim.frames == 3 && fabsf(g.aim.m - .01f) < 1e-5f && fabsf(g.aim.curl - 50) < .01f);
  CHECK(strstr(tfp_arm_drive_grip_drawn_line(&g), "as drawn at the hip: not measured; while aiming (3 frames, "));
  CHECK(strstr(tfp_arm_drive_grip_drawn_line(&g), " held): the hand 0.010 m from the grip"));
  /* the body's hand off the gun (a reload): nothing measured, no line */
  static const float far[3] = {.3f, 0, 0};
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], far, NULL);
  for (int i = 0; i < 3; i++) grip_draw(t += 16, &hand, &socket); /* (the first: the body's hand may trail the node by a frame) */
  tfp_arm_drive_grip_take(&g);
  for (int i = 0; i < 3; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_FREE && !g.drawn && !tfp_arm_drive_grip_drawn_line(&g));
  /* the longest lines say() is ever given: within its 700 characters with "Grip: " before them */
  TfpArmDriveGrip big;
  memset(&big, 0, sizeof big);
  big.frames = big.held = big.nodes = big.units = big.cut = big.out_of_reach = big.too_far = UINT32_MAX;
  big.state = TFP_ARM_GRIP_FREE, big.found = true, big.attachment = true, big.target_unit = 1, big.gate_unit = 0;
  big.body_m = big.to_m = big.most_to_m = big.reach = big.most_reach = big.shoulder_m = big.most_shoulder_m = -999999.f;
  big.step_degrees = big.limit_degrees = big.weight = big.held_step_degrees = big.body_degrees = -999999.f;
  big.drawn = true;
  big.gate = TFP_ARM_GATE_NOT_AT_REST, big.taken = big.let_far = big.let_fast = big.let_action = UINT32_MAX;
  big.gun_mps = big.most_held_mps = -999999.f;
  TfpArmDriveDrawn *const b[2] = {&big.hip, &big.aim};
  for (unsigned k = 0; k < 2; k++) {
    b[k]->frames = b[k]->held = UINT32_MAX;
    b[k]->m = b[k]->most_m = b[k]->turn = b[k]->most_turn = b[k]->palm_m = b[k]->most_palm_m = b[k]->curl = b[k]->least_curl = b[k]->own_m = -999999.f;
  }
  for (uint32_t state = 0; state < TFP_ARM_GRIPS; state++) {
    big.state = state;
    for (uint32_t gate = 0; gate < TFP_ARM_GATES; gate++) {
      big.gate = gate, big.gun_mps = gate % 2 ? -1.f : -999999.f;
      CHECK(strlen(tfp_arm_drive_grip_line(&big)) + 6 < 700 && strlen(tfp_arm_drive_grip_gate_line(&big)) + 12 < 700);
    }
  }
  big.gate = TFP_ARM_GATE_NOT_AT_REST, big.gun_mps = -999999.f;
  CHECK(strlen(tfp_arm_drive_grip_drawn_line(&big)) + 6 < 700);
  printf("arm drive: grip lines at most %zu and %zu characters (say() keeps 699)\n", strlen(tfp_arm_drive_grip_line(&big)) + 6,
         strlen(tfp_arm_drive_grip_drawn_line(&big)) + 6);
}
/* Round 36: a grip unit that is not a child of the gun but hangs on one of its nodes from elsewhere (a child of the rig
 * whose external array is the gun's): seen, listed as from elsewhere, and held. */
enum { LOOSE_GRIP = 602 };
static void test_grip_mounted_from_rig(void) {
  static const float zero[3] = {0, 0, 0};
  float ahead[3];
  forward_of_socket(.12f, -.04f, ahead);
  TfpGraftMatrix hand, socket;
  fresh();
  grip_resources();
  make_unit(LOOSE_GRIP, 2, object_of(RIG), (1u << 16) | 2);
  p64(object_of(LOOSE_GRIP) + 0x78, worlds_of(GUN));
  { const uint64_t grip = UINT64_C(0x177863efec0c9807); put(object_of(LOOSE_GRIP) + 0x358, &grip, 8); }
  p64(object_of(RIG) + 0x1d8, object_of(LOOSE_GRIP)); /* the rig's first child; the gun is not in this list here */
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  put(worlds_of(LOOSE_GRIP), &rig_world[TFP_GRAFT_RIG_SOCKET], 64);
  {
    TfpGraftMatrix g = body_now[body_left_hand()];
    for (unsigned i = 0; i < 3; i++) g.values[12 + i] += ahead[i];
    n32(names_of(LOOSE_GRIP) + 1 * 4, GRIP_NAME_T), put(worlds_of(LOOSE_GRIP) + 64, &g, 64);
  }
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  for (int i = 0; i < 60; i++) grip_draw(t += 16, &hand, &socket);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  const TfpGraftMatrix want = grip_on_socket(ahead), at = tfp_graft_product(&want, &socket);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.attachment && g.nodes == 2 && g.units == 2 && g.tree_units == 3 && g.target_unit == 2);
  CHECK(grip_m(&hand, &at) < .001f && grip_degrees(&hand, &at) < .01f);
  const char *units = tfp_arm_drive_grip_units_line(&g, false);
  CHECK(strstr(units, "[2] from elsewhere, on [0] node 1 (7950e36e) 177863efec0c9807, 2 nodes, grip node 1"));
  /* a unit of the rig that hangs on the rig, not on the gun, is not taken */
  p64(object_of(LOOSE_GRIP) + 0x78, worlds_of(RIG));
  for (int i = 0; i < 5; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.tree_units == 2 && !g.attachment && g.target_unit == 0);
}
/* Round 36b (B of the Adjudicator diagnosis): held on an attachment's grip node, the hand closes round the grip: each finger
 * joint (rig nodes 14 to 28, thumb joint 1 left the body's) turned about its own +Y towards the grip's curl by the grip's
 * weight; positions kept. Never on the gun's own node (test_grip_holds: only nodes 10, 12, 13 differ there), and back to
 * the body's fingers when let go. */
static double rig_curl(const TfpGraftLocal *l) { return tfp_gf_curl(l) * 180 / TFP_OG_PI; }
static void drawn_locals(uint64_t now, TfpGraftLocal out[TFP_GRAFT_RIG_NODES]) {
  tfp_arm_drive_set_rest(now, TFP_ARM_REST_NONE, 0);
  const bool after = tfp_arm_drive_render(WORLD, true);
  rig_locals(out);
  if (after) tfp_arm_drive_after_render();
}
static void test_grip_fingers_close(void) {
  static const float zero[3] = {0, 0, 0};
  float ahead[3];
  forward_of_socket(.12f, -.04f, ahead);
  for (int aiming = 0; aiming < 2; aiming++) {
    TfpGraftLocal plain[TFP_GRAFT_RIG_NODES], held[TFP_GRAFT_RIG_NODES], easing[TFP_GRAFT_RIG_NODES], back[TFP_GRAFT_RIG_NODES];
    fresh();
    grip_resources();
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
    fit_grip(ahead);
    tfp_arm_drive_set(true, true, aiming ? 0 : 1);
    uint64_t t = 1000;
    drawn_locals(t, plain); /* the grip off: the body's fingers (aiming: the game's) */
    tfp_arm_drive_set_grip(true, false);
    drawn_locals(t += 16, easing);
    drawn_locals(t += 16, easing); /* on its way: part of the curl */
    TfpArmDriveGrip g;
    tfp_arm_drive_grip_take(&g);
    const float part = g.weight;
    CHECK(part > 0 && part < 1);
    for (int i = 0; i < 40; i++) drawn_locals(t += 16, held);
    tfp_arm_drive_grip_take(&g);
    CHECK(g.state == TFP_ARM_GRIP_HELD && g.attachment && g.weight == 1);
    double least = 180;
    for (unsigned n = 14; n <= 28; n++) {
      const double was = rig_curl(&plain[n]), want = n >= 26 ? was + TFP_GF_THUMB_TURN_DEGREES : tfp_gf_curl_degrees[(n - 14) / 3];
      CHECK(!memcmp(held[n].position, plain[n].position, 12) && !memcmp(held[n].preserved, plain[n].preserved, 16));
      if (n == 26) { /* thumb joint 1: the body's */
        CHECK(!memcmp(&held[n], &plain[n], 64) && !memcmp(&easing[n], &plain[n], 64));
        continue;
      }
      CHECK(fabs(rig_curl(&held[n]) - want) < .01);
      CHECK(fabs(rig_curl(&easing[n]) - was - (want - was) * part) < .05); /* the grip's weight of the way */
      if (n < 26) least = fmin(least, rig_curl(&held[n]));
    }
    for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; n++) /* the rest is what the grip alone writes: the left arm */
      if ((n < 14 || n > 28) && n != 10 && n != 12 && n != 13) CHECK(!memcmp(&held[n], &plain[n], 64));
    /* let go (a reload): back to the body's fingers, exactly */
    static const float far[3] = {.2f, 0, 0};
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], far, NULL);
    for (int i = 0; i < 40; i++) drawn_locals(t += 16, back);
    for (unsigned n = 14; n <= 28; n++) CHECK(!memcmp(&back[n], &plain[n], 64));
    /* held again, then the grip switched off: the fingers ease out with the arm, no frame drops them at once */
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
    fit_grip(ahead);
    for (int i = 0; i < 40; i++) drawn_locals(t += 16, held);
    tfp_arm_drive_set_grip(false, false);
    double last = rig_curl(&held[21]);
    for (int i = 0; i < 40; i++) {
      drawn_locals(t += 16, back);
      const double now = rig_curl(&back[21]);
      CHECK(now <= last + 1e-6 && last - now < (tfp_gf_curl_degrees[TFP_GF_MIDDLE] - rig_curl(&plain[21])) * 16 / 150.0 + .05);
      last = now;
    }
    for (unsigned n = 14; n <= 28; n++) CHECK(!memcmp(&back[n], &plain[n], 64));
    printf("arm drive: grip on an attachment%s: the fingers closed to %.1f degrees a joint at least (%.0f%% of it at weight %.2f)\n",
           aiming ? " while aiming" : "", least, 100.0 * part, (double)part);
  }
}
/* Round 36b (C of the Adjudicator diagnosis): while aiming the copy fades out and the game's own aim pose holds the gun
 * (src/arm_weight.h), and until now the hand went with it, off the grip (retail measured the game's aim hand 17 cm from a
 * foregrip's node) and slid there over 120 ms at every aim edge. The reach now runs on the mixed pose: the hand stays on the
 * grip through the fade and the whole aim; while aiming only the left arm (nodes 10, 12, 13) differs from the game's own,
 * so the socket, the right arm and the camera the game seats by the socket are the game's (the rifle still rises). */
static void test_grip_while_aiming(void) {
  static const float zero[3] = {0, 0, 0};
  TfpGraftMatrix hand, socket, game_hand, game_socket;
  /* where the game's aim pose has the hand: weight 0, the grip off */
  fresh();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  tfp_arm_drive_set(true, true, 0);
  grip_draw(1000, &game_hand, &game_socket);
  const TfpGraftMatrix want = grip_on_socket(zero), game_at = tfp_graft_product(&want, &game_socket);
  const float game_off = grip_m(&game_hand, &game_at);
  CHECK(game_off > .02f);
  /* held at the hip, then aim in (the weight down over 120 ms as arm_weight.h steps it), aim, aim out */
  fresh();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  float weight = 1, worst = 0, jump = 0;
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  for (int i = 0; i < 90; i++) {
    weight = tfp_arm_weight_step(weight, i < 60, 16);
    tfp_arm_drive_set(true, true, weight);
    grip_draw(t += 16, &hand, &socket);
    const TfpGraftMatrix at = tfp_graft_product(&want, &socket);
    worst = fmaxf(worst, grip_m(&hand, &at));
    /* the hand moves with the gun only: its place on the gun stays */
    const TfpGraftMatrix on_gun = tfp_graft_relative(&hand, &socket);
    jump = fmaxf(jump, grip_m(&on_gun, &want));
    if (i == 40) {
      tfp_arm_drive_grip_take(&g);
      CHECK(weight == 0 && g.state == TFP_ARM_GRIP_HELD && g.held == 41 && g.attachment == false);
      CHECK(!memcmp(&socket, &game_socket, sizeof socket) && stats().draw.sock_m == 0); /* the gun where the game's aim pose has it */
    }
  }
  printf("arm drive: grip while aiming: the game's aim pose has the hand %.3f m from the grip; held, at most %.4f m from it through "
         "the fade, the aim and back\n", (double)game_off, (double)worst);
  CHECK(worst < .001f && jump < .001f);
  /* at weight 0 every node but the left arm is the game's own, also the socket the camera is seated by */
  tfp_arm_drive_set(true, true, 0);
  for (int i = 0; i < 10; i++) grip_draw(t += 16, &hand, &socket);
  TfpGraftLocal drawn_now[TFP_GRAFT_RIG_NODES];
  tfp_arm_drive_set_rest(t += 16, TFP_ARM_REST_NONE, 0);
  CHECK(tfp_arm_drive_render(WORLD, true)); /* written: the game's values go back after the draw */
  rig_locals(drawn_now);
  tfp_arm_drive_after_render();
  for (unsigned n = 0; n < TFP_GRAFT_RIG_NODES; n++) {
    const bool left_arm = n == 10 || n == 12 || n == 13;
    if (!left_arm) CHECK(!memcmp(&drawn_now[n], &game_locals[n], 64));
    else CHECK(memcmp(drawn_now[n].basis, game_locals[n].basis, 36) != 0);
  }
  TfpGraftLocal after[TFP_GRAFT_RIG_NODES];
  rig_locals(after);
  CHECK(!memcmp(after, game_locals, sizeof after)); /* put back: the game finds its own */
  CHECK(stats().draw.sock_m == 0);
}
/* The body's left hand on no grip node (a reload, a weapon the game does not pin): the arm is the body's. One-handed (at
 * the hip, and aiming: weight 0, nothing written at all), and the test switch off: the same. (Aiming with a two-handed gun
 * holds the grip since round 36b: test_grip_while_aiming.) */
static void test_grip_left_alone(void) {
  static const float far[3] = {.2f, 0, 0}, zero[3] = {0, 0, 0};
  TfpGraftMatrix hand, socket, plain_hand, plain_socket;
  for (int way = 0; way < 5; way++) {
    fresh();
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], way == 0 || way == 4 ? far : zero, NULL);
    tfp_arm_drive_set(true, true, way == 2 || way == 4 ? 0 : 1);
    grip_draw(1000, &plain_hand, &plain_socket);
    tfp_arm_drive_set_grip(way != 3, way == 1 || way == 2);
    uint64_t t = 1000;
    for (int i = 0; i < 10; i++) {
      grip_draw(t += 16, &hand, &socket);
      CHECK(!memcmp(&hand, &plain_hand, sizeof hand) && !memcmp(&socket, &plain_socket, sizeof socket));
      if (way == 2 || way == 4) CHECK(!stats().owned); /* a handgun aimed, or a reload aimed: the game's own pose, nothing written */
    }
    TfpArmDriveGrip g;
    tfp_arm_drive_grip_take(&g);
    static const uint32_t expected[5] = {TFP_ARM_GRIP_FREE, TFP_ARM_GRIP_ONE_HANDED, TFP_ARM_GRIP_ONE_HANDED, TFP_ARM_GRIP_OFF,
                                         TFP_ARM_GRIP_FREE};
    CHECK(g.state == expected[way] && !g.held && g.frames == (way == 3 ? 0u : 10u));
    if (!way || way == 4) CHECK(g.nodes == 1 && g.body_m > .19f && strstr(tfp_arm_drive_grip_line(&g), "the body's left hand is on no grip"));
  }
  /* held, then the body's hand leaves (a reload): back to the body's pose within the step limit, then exactly it. At the
   * hip, and aiming (weight 0: the reach runs on the game's aim pose since round 36b, so a reload while aimed lets go to
   * the game's own pose, socket untouched throughout, and then nothing is written at all) */
  for (int aiming = 0; aiming < 2; aiming++) {
    fresh();
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
    tfp_arm_drive_set(true, true, aiming ? 0 : 1);
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], far, NULL);
    grip_draw(1000, &plain_hand, &plain_socket); /* the pose with the body's hand off the grip */
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
    tfp_arm_drive_set_grip(true, false);
    uint64_t t = 1000;
    for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
    TfpArmDriveGrip g;
    tfp_arm_drive_grip_take(&g);
    CHECK(g.state == TFP_ARM_GRIP_HELD && g.weight == 1);
    CHECK(memcmp(&hand, &plain_hand, sizeof hand) != 0);
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], far, NULL);
    TfpGraftMatrix prev = hand;
    int frames = 0;
    for (; frames < 40 && memcmp(&hand, &plain_hand, sizeof hand); frames++) {
      grip_draw(t += 16, &hand, &socket);
      CHECK(grip_degrees(&hand, &prev) < 60); /* the hand's world turn: a sum of three limited joints */
      if (aiming) CHECK(!memcmp(&socket, &plain_socket, sizeof socket)); /* the gun where the game's aim pose has it */
      prev = hand;
    }
    tfp_arm_drive_grip_take(&g);
    CHECK(frames > 2 && frames < 40 && g.state == TFP_ARM_GRIP_FREE && g.step_degrees <= g.limit_degrees + 1e-3f);
    for (int i = 0; i < 5; i++) {
      grip_draw(t += 16, &hand, &socket);
      CHECK(!memcmp(&hand, &plain_hand, sizeof hand) && !memcmp(&socket, &plain_socket, sizeof socket));
      if (aiming) CHECK(!stats().owned);
    }
    printf("arm drive: a reload %s: the hand back on the %s pose in %d frames\n", aiming ? "while aimed" : "at the hip",
           aiming ? "game's aim" : "body's", frames);
  }
}
/* A moving body and a grip that wanders, at four frame times: no written joint ever turns by more than the
 * frame's allowance. */
static void test_grip_step_limit(void) {
  static const unsigned frame_ms[4] = {8, 17, 33, 50};
  uint64_t seedling = 12345;
  for (unsigned k = 0; k < 4; k++) {
    TfpGraftMatrix hand, socket;
    fresh();
    tfp_arm_drive_set(true, true, 1);
    tfp_arm_drive_set_grip(true, false);
    uint64_t t = 1000;
    float most = 0;
    for (int i = 0; i < 200; i++) {
      seedling = seedling * 6364136223846793005u + 1442695040888963407u;
      float off[3];
      for (unsigned j = 0; j < 3; j++) off[j] = (float)((seedling >> (20 + 10 * j)) & 1023) / 1023.f * .02f - .01f;
      if (i % 37 == 36) off[1] += .3f; /* the hand off the grip for a frame */
      put(worlds_of(BODY), i % 2 ? body_later : body_now, sizeof body_now);
      grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], off, NULL);
      grip_draw(t += frame_ms[k], &hand, &socket);
      TfpArmDriveGrip g;
      tfp_arm_drive_grip_take(&g);
      const float allowed = fminf(720.f * (float)frame_ms[k] / 1000, 30);
      CHECK(g.step_degrees <= allowed + 1e-3f && fabsf(g.limit_degrees - allowed) < 1e-3f + (g.step_degrees == 0 ? 1e9f : 0));
      if (g.step_degrees > most) most = g.step_degrees;
    }
    printf("arm drive: grip at %u ms frames with a moving body: largest joint step %.2f degrees (allowed %.2f)\n", frame_ms[k], (double)most,
           (double)fminf(720.f * (float)frame_ms[k] / 1000, 30));
  }
}

/* ---- Round 38b: the grip gate (the user on feel.56, AR-23 Liberator with a vertical grip: "Still not holding onto the
 * grip"). The game rests the body's left hand some 9 cm from both grip nodes (the gun's own and the grip's), never within
 * the 3 cm that took a grip until now: 16925 frames free, 134 on the grip. The hand is now held whenever the body's hand
 * is with the gun (by the grip and steady against it) and let go when it truly leaves: beyond the leave radius, moving
 * fast against the gun (a reload's pull, a melee), or the game's own action (its reload flag, the drive's hold).
 * The test world puts the grip nodes against the body's hand, so the hand moving by d is every node moving by -d. */
static const float LIB_GUN[3] = {-.06f, .07f, 0}, LIB_GRIP[3] = {.06f, -.07f, 0}; /* along the socket, up: 0.092 m each */
static float lib_side[3];  /* the socket's X: a reload's reach to the magazine, across the gun */
static void liberator(const float *hand_moved) {
  float gun[3], grip[3], g[3], a[3];
  forward_of_socket(LIB_GUN[0], LIB_GUN[1], g), forward_of_socket(LIB_GRIP[0], LIB_GRIP[1], a);
  for (unsigned i = 0; i < 3; i++) gun[i] = g[i] - hand_moved[i], grip[i] = a[i] - hand_moved[i];
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], gun, NULL);
  fit_grip(grip);
}
static void liberator_at(float side) {
  float moved[3];
  for (unsigned i = 0; i < 3; i++) moved[i] = side * lib_side[i];
  liberator(moved);
}
static void reload_flag(bool on) {
  uint8_t byte;
  CHECK(read_fake(&f, AVATAR + FP_AVATAR_FLAGS + 15, &byte, 1));
  byte = on ? (uint8_t)(byte | 2) : (uint8_t)(byte & ~2);
  put(AVATAR + FP_AVATAR_FLAGS + 15, &byte, 1);
}
static float liberator_hand_off(const TfpGraftMatrix *hand, const TfpGraftMatrix *socket) {
  float a[3];
  forward_of_socket(LIB_GRIP[0], LIB_GRIP[1], a);
  const TfpGraftMatrix want = grip_on_socket(a), at = tfp_graft_product(&want, socket);
  return grip_m(hand, &at);
}
/* frames until held again (each checked within the step limits), at most `limit` */
static int until_held(uint64_t *t, int limit, TfpGraftMatrix *hand, TfpGraftMatrix *socket) {
  for (int i = 0; i < limit; i++) {
    grip_draw(*t += 16, hand, socket);
    TfpArmDriveGrip g;
    tfp_arm_drive_grip_take(&g);
    CHECK(g.step_degrees <= g.limit_degrees + 1e-3f);
    if (g.state == TFP_ARM_GRIP_HELD && liberator_hand_off(hand, socket) < .001f) return i + 1;
  }
  return limit + 1;
}
static void test_grip_gate_liberator(void) {
  TfpGraftMatrix hand, socket;
  static const float still[3] = {0, 0, 0};
  for (unsigned i = 0; i < 3; i++) lib_side[i] = rig_world[TFP_GRAFT_RIG_SOCKET].values[i];
  fresh();
  grip_resources();
  liberator(still);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  /* the body's hand 0.092 m from both nodes, steady: held on the grip's node, after a short settle */
  const int first = until_held(&t, 60, &hand, &socket);
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  printf("arm drive: grip gate: the body's hand %.3f m from both nodes and steady (the Liberator): held after %d frames, "
         "the drawn hand %.4f m from the grip's node\n", (double)g.body_m, first, (double)liberator_hand_off(&hand, &socket));
  CHECK(first < 30 && g.state == TFP_ARM_GRIP_HELD && g.held == 30 && g.attachment && g.target_unit == 1);
  CHECK(g.gate == TFP_ARM_GATE_WITH_GUN && g.body_m > .09f && g.body_m < .095f && g.gun_mps >= 0 && g.gun_mps < .01f);
  CHECK(liberator_hand_off(&hand, &socket) < .001f && g.step_degrees <= g.limit_degrees + 1e-3f);
  CHECK(strstr(tfp_arm_drive_grip_line(&g), "on the grip; an attachment's grip node"));
  CHECK(strstr(tfp_arm_drive_grip_gate_line(&g), "held, the body's hand with the gun (0.092 m from the nearest grip node, 0.00 m/s against the gun)"));
  /* aiming (round 36b's path, the weight down as arm_weight.h steps it): still held */
  for (float w = 1; w > 0;) {
    w = tfp_arm_weight_step(w, true, 16);
    tfp_arm_drive_set(true, true, w);
    grip_draw(t += 16, &hand, &socket);
  }
  tfp_arm_drive_grip_take(&g);
  CHECK(!g.let_far && !g.let_fast && !g.let_action);
  for (int i = 0; i < 10; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.held == 10 && g.gate == TFP_ARM_GATE_WITH_GUN && liberator_hand_off(&hand, &socket) < .001f);
  for (float w = 0; w < 1;) {
    w = tfp_arm_weight_step(w, false, 16);
    tfp_arm_drive_set(true, true, w);
    grip_draw(t += 16, &hand, &socket);
  }
  for (int i = 0; i < 10; i++) grip_draw(t += 16, &hand, &socket);
  /* a reload: the body's hand goes 0.10 m across the gun to the magazine in 80 ms (still within the take radius of
   * both nodes), rests there 400 ms, comes back in 80 ms. Let go on the pull; not taken at the magazine; held again */
  tfp_arm_drive_grip_take(&g);
  bool let_go = false;
  int let_at = -1;
  for (int i = 1; i <= 5; i++) {
    liberator_at(.02f * (float)i);
    grip_draw(t += 16, &hand, &socket);
    tfp_arm_drive_grip_take(&g);
    CHECK(g.step_degrees <= g.limit_degrees + 1e-3f);
    if (!let_go && g.state != TFP_ARM_GRIP_HELD) let_go = true, let_at = i;
  }
  CHECK(let_go && g.state != TFP_ARM_GRIP_HELD && g.body_m < .15f);
  bool any_held = false;
  TfpArmDriveGrip at_mag;
  memset(&at_mag, 0, sizeof at_mag);
  for (int i = 0; i < 25; i++) {
    grip_draw(t += 16, &hand, &socket);
    tfp_arm_drive_grip_take(&g);
    any_held |= g.held != 0;
    CHECK(g.step_degrees <= g.limit_degrees + 1e-3f);
    if (i == 24) at_mag = g;
  }
  CHECK(!any_held && at_mag.state == TFP_ARM_GRIP_FREE && at_mag.gate == TFP_ARM_GATE_NOT_AT_REST && at_mag.body_m < .15f);
  CHECK(strstr(tfp_arm_drive_grip_gate_line(&at_mag), "not held, the body's hand by the grip but not where it rests on this gun"));
  for (int i = 4; i >= 0; i--) {
    liberator_at(.02f * (float)i);
    grip_draw(t += 16, &hand, &socket);
  }
  const int back = until_held(&t, 60, &hand, &socket);
  for (int i = 0; i < 5; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  printf("arm drive: grip gate: a reload's pull (0.10 m in 80 ms) let go on frame %d, not taken at the magazine for 400 ms, "
         "held again %d frames after the hand came back\n", let_at, back);
  CHECK(back < 40 && g.state == TFP_ARM_GRIP_HELD);
  /* the game's reload flag lets go though the body's hand stays; back once it clears */
  reload_flag(true);
  for (int i = 0; i < 3; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_FREE && g.gate == TFP_ARM_GATE_ACTION && g.let_action == 1 && !g.held);
  CHECK(strstr(tfp_arm_drive_grip_gate_line(&g), "not held, the game's action (a reload it flags, or a hold)"));
  for (int i = 0; i < 20; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(!g.held && g.gate == TFP_ARM_GATE_ACTION && g.step_degrees <= g.limit_degrees + 1e-3f);
  reload_flag(false);
  CHECK(until_held(&t, 60, &hand, &socket) < 40);
  /* a melee: the hand 0.30 m away in 100 ms: let go (fast, then far), then back */
  tfp_arm_drive_grip_take(&g);
  for (int i = 1; i <= 6; i++) {
    float moved[3];
    forward_of_socket(.05f * (float)i, 0, moved);
    liberator(moved);
    grip_draw(t += 16, &hand, &socket);
  }
  for (int i = 0; i < 10; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_FREE && g.gate == TFP_ARM_GATE_FAR && g.let_fast + g.let_far == 1 && g.body_m > .18f);
  liberator(still);
  CHECK(until_held(&t, 60, &hand, &socket) < 40);
  /* a throw or a stratagem: the drive's hold says the game is in an action */
  tfp_arm_drive_set_hold(true);
  for (int i = 0; i < 5; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_FREE && g.gate == TFP_ARM_GATE_ACTION && g.let_action == 1);
  tfp_arm_drive_set_hold(false);
  CHECK(until_held(&t, 60, &hand, &socket) < 40);
  /* moving slowly against the gun (walking sway, breathing: 0.2 m/s, a few centimetres) keeps it */
  tfp_arm_drive_grip_take(&g);
  for (int i = 0; i < 60; i++) {
    liberator_at(.02f * sinf((float)i * .2f));
    grip_draw(t += 16, &hand, &socket);
  }
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.held == 60 && !g.let_fast && !g.let_far && !g.let_action && g.most_held_mps > .05f);
  printf("arm drive: grip gate: the game's reload flag and a hold let go, a melee too; a sway of %.2f m/s against the gun keeps it\n",
         (double)g.most_held_mps);
  liberator(still);
}
/* Round 38b: the pinned case (the body's hand on the gun's own node, the old take) still holds at once, says so, and a hand
 * that only passes a node fast is not taken; far away the line says why. */
static void test_grip_gate_pinned(void) {
  static const float zero[3] = {0, 0, 0}, far[3] = {.2f, 0, 0};
  TfpGraftMatrix hand, socket;
  fresh();
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], zero, NULL);
  tfp_arm_drive_set(true, true, 1);
  tfp_arm_drive_set_grip(true, false);
  uint64_t t = 1000;
  grip_draw(t += 16, &hand, &socket);
  TfpArmDriveGrip g;
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state != TFP_ARM_GRIP_FREE && g.gate == TFP_ARM_GATE_ON_NODE && g.taken == 1); /* the first frame: no settle */
  for (int i = 0; i < 30; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_HELD && g.gate == TFP_ARM_GATE_ON_NODE);
  CHECK(strstr(tfp_arm_drive_grip_gate_line(&g), "held, the body's hand on a grip node (0.000 m"));
  grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], far, NULL);
  for (int i = 0; i < 10; i++) grip_draw(t += 16, &hand, &socket);
  tfp_arm_drive_grip_take(&g);
  CHECK(g.state == TFP_ARM_GRIP_FREE && g.gate == TFP_ARM_GATE_FAR && g.let_far + g.let_fast == 1);
  CHECK(strstr(tfp_arm_drive_grip_gate_line(&g), "not held, the body's hand 0.200 m from the nearest grip node"));
  /* swept through the node at 2.5 m/s: not taken on the way */
  for (int i = -4; i <= 4; i++) {
    const float at[3] = {.04f * (float)i, 0, 0};
    grip_world(&rig_world[TFP_GRAFT_RIG_SOCKET], at, NULL);
    grip_draw(t += 16, &hand, &socket);
    tfp_arm_drive_grip_take(&g);
    CHECK(!g.held && g.state == TFP_ARM_GRIP_FREE);
  }
  printf("arm drive: grip gate: the body's hand on the node is held at once; swept through it at 2.5 m/s, not taken\n");
}

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3) { fprintf(stderr, "usage: arm_drive_test FIXTURE [EVERY-FRAME-FIXTURE]\n"); return 2; }
  load(argv[1]);
  test_off_and_idle();
  test_leave_native_view();
  test_put_back_keeps_game_writes();
  test_drive();
  test_current_optic();
  test_weight_zero_from_rest();
  test_release_writes_differences();
  test_restore_after_draw();
  test_bend();
  test_refusals();
  test_rebind_on_new_units();
  test_hold();
  test_hold_edge();
  test_rest_grip();
  test_view_pitch();
  test_rig_rotation();
  test_rest_after_a_draw();
  test_binding_while_aiming();
  test_rest_gates();
  test_rest_first();
  test_rest_kept_stays();
  test_rest_stances();
  test_rest_other_gun();
  test_rest_of();
  test_game_sees_its_own_socket();
  test_fade_and_ease_under_put_back();
  test_hold_begins_under_put_back();
  test_grip_holds();
  test_grip_reach_forward();
  test_grip_foregrip();
  test_grip_foregrip_body_on_gun();
  test_grip_attachment_never_gun_own();
  test_grip_drawn_line();
  test_grip_mounted_from_rig();
  test_grip_while_aiming();
  test_grip_fingers_close();
  test_grip_left_alone();
  test_grip_step_limit();
  test_grip_gate_liberator();
  test_grip_gate_pinned();
  if (argc == 3) test_rest_recorded(argv[2]);
  printf("arm drive: %u checks passed\n", checks);
  return 0;
}
