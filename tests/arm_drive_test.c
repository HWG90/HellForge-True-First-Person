/* The arm drive (src/arm_drive.c) with no game: a fake player in the native first-person view
 * (tests/fake_native.h, the same fake the snapshot tests use), a body and a first-person rig holding a
 * frame recorded in the game (tests/reference/arm-graft/paired-poses.bin), sleeves, gloves, a gun with an
 * optic, and the engine's two functions replaced by recorders that write the fake's memory.
 *
 *   arm_drive_test FIXTURE */
#include "fake_native.h"
#include "arm_drive.h"
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
enum { GUN2 = 502 };
static TfpGraftMatrix body_draw[TFP_GRAFT_BODY_NODES], body_crouch[TFP_GRAFT_BODY_NODES], body_sway[TFP_GRAFT_BODY_NODES];
/* The recorded body with its arms somewhere else relative to the head: the root's axes turned about their own Z and
 * the head moved. Every joint below a shoulder keeps its relation to its parent. */
static void displaced(float degrees, float x, float y, float z, TfpGraftMatrix *out) {
  const float c = cosf(degrees * .01745329252f), s = sinf(degrees * .01745329252f);
  unsigned head = 0;
  memcpy(out, body_now, sizeof body_now);
  for (unsigned k = 0; k < 3; k++) {
    const float a = body_now[0].values[k], b = body_now[0].values[4 + k];
    out[0].values[k] = c * a + s * b, out[0].values[4 + k] = c * b - s * a;
  }
  for (unsigned i = 0; i < TFP_GRAFT_BODY_NODES; i++)
    if (bn[i] == TFP_GRAFT_NAME_HEAD) head = i;
  CHECK(head);
  out[head].values[12] += x, out[head].values[13] += y, out[head].values[14] += z;
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
  s.pitch = -.3;
  CHECK(tfp_arm_drive_rest_of(&s, NULL, &why));
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
  test_rest_after_a_draw();
  test_binding_while_aiming();
  test_rest_gates();
  test_rest_first();
  test_rest_kept_stays();
  test_rest_stances();
  test_rest_other_gun();
  test_rest_of();
  if (argc == 3) test_rest_recorded(argv[2]);
  printf("arm drive: %u checks passed\n", checks);
  return 0;
}
