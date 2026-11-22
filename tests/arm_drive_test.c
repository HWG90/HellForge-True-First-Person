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
enum { CALLS = 512 };
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

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: arm_drive_test FIXTURE\n"); return 2; }
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
  printf("arm drive: %u checks passed\n", checks);
  return 0;
}
