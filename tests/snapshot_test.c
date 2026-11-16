#include "snapshot.h"
#include "fake_native.h"
#include <stdlib.h>

static unsigned checks;
#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      fprintf(stderr, "snapshot check failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

/* Addresses of fake_native.h's setup(): one local player (entity 301, avatar
 * 101, body 401, rig 801) holding weapon 201 (unit 501), request kind 4. */
enum { AVATAR = 101, WEAPON = 201, BODY = 401, UNIT = 501, RIG = 801, LIGHT = 301 };
static const uintptr_t CTX = 0x300000000ULL, WORLD = 0x400000000ULL, CTRL = 0x500000000ULL,
                       PLAYER = 0x600000000ULL, POOL = 0x700000000ULL, EP = 0x800000000ULL,
                       WIELD = 0x900000000ULL, WEAPONS = 0xa00000000ULL;

static bool fails(const FpMemory *m, TfpSnapshotScope scope, const char *text) {
  TfpSnapshot s, before;
  char why[160] = "";
  memset(&s, 0x5a, sizeof s);
  before = s;
  if (tfp_snapshot(m, scope, &s, why, sizeof why)) return false;
  if (memcmp(&s, &before, sizeof s)) return false; /* *out untouched on failure */
  if (strcmp(why, text)) fprintf(stderr, "  got \"%s\", wanted \"%s\"\n", why, text);
  return !strcmp(why, text);
}
static TfpSnapshot take(const FpMemory *m, TfpSnapshotScope scope) {
  TfpSnapshot s;
  char why[160] = "";
  if (!tfp_snapshot(m, scope, &s, why, sizeof why)) {
    fprintf(stderr, "snapshot refused: %s\n", why);
    exit(1);
  }
  return s;
}
static void controls(uint32_t mask, uint8_t active, uint8_t guard) {
  uint8_t bytes[6];
  memcpy(bytes, &mask, 4);
  bytes[4] = active;
  bytes[5] = guard;
  put(CTRL, bytes, 6);
}
static void flag(unsigned byte, uint8_t value) {
  uint8_t flags[24] = {0};
  flags[byte] = value;
  put(POOL + FP_AVATAR_FLAGS, flags, 24);
}
static void request(uintptr_t at, uint32_t kind, uint32_t avatar, uint32_t weapon, uint32_t body) {
  uint32_t row[5] = {kind, avatar, weapon, body, 0};
  put(at, row, 20);
}

int main(void) {
  FpMemory m = setup();
  TfpSnapshot s, other;
  { const uint8_t amd64[2] = {0x64, 0x86}; put(m.game + 68, amd64, 2); } /* the donor's fake header has no machine field */

  /* hip_rifle */
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.base == m.game && s.context == CTX && s.camera == CTX + 0x3000 && s.world == WORLD &&
        s.controller == CTRL && s.player == PLAYER && s.avatar_owner == POOL && s.avatar_entry == EP);
  CHECK(s.head == 0 && s.tail == 1 && s.record == CTX + 0x200 && s.camera_kind == 4 &&
        s.request_avatar == AVATAR && s.request_weapon == WEAPON && s.request_body == BODY);
  CHECK(s.local_entity == LIGHT && s.avatar == AVATAR && s.avatar_index == 0 && s.body == BODY &&
        s.viewmodel == RIG && s.weapon == WEAPON && s.weapon_unit == UNIT &&
        s.flags_address == POOL + FP_AVATAR_FLAGS && s.weapon_component_descriptor == WEAPONS + 0x400);
  CHECK(s.request_local && s.safe && s.engine_safe && s.weapon_ready && s.full && !s.blocked &&
        !s.controller_active && !s.aiming && !s.shoulder_aim && !s.seated && !s.reloading &&
        !s.weapon_transition && !s.head_camera_transition);
  other = take(&m, TFP_SNAPSHOT_PLAYER);
  CHECK(!other.full && !other.weapon_ready && other.weapon == 0 && other.safe && tfp_same_player(&s, &other) &&
        !tfp_same_identity(&s, &other));
  CHECK(tfp_same_identity(&s, &s));

  /* rig_view: our kind-6 request is the newest ring row */
  n32(CTX + 0x1fc, 2);
  request(CTX + 0x200 + 0xe8, 6, AVATAR, WEAPON, BODY);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.camera_kind == 6 && s.record == CTX + 0x200 + 0xe8 && s.safe);
  /* rig_view_other_weapon, rig_view_other_body */
  request(CTX + 0x200 + 0xe8, 6, AVATAR, WEAPON + 1, BODY);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "ADS request belongs to another weapon"));
  CHECK(take(&m, TFP_SNAPSHOT_PLAYER).camera_kind == 6); /* the player part does not look at the weapon */
  request(CTX + 0x200 + 0xe8, 6, AVATAR, WEAPON, BODY + 1);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "ADS request body changed"));
  request(CTX + 0x200 + 0xe8, 6, AVATAR, WEAPON, BODY);

  /* aiming_ads */
  controls(0, 1, 0);
  flag(6, 1);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.controller_active && s.aiming && !s.blocked && s.safe && !s.shoulder_aim);
  /* shoulder aim: flag bits 82 and 125 while the game's sights are not active */
  controls(0, 0, 0);
  { uint8_t flags[24] = {0}; flags[10] = 4; flags[15] = 32; put(POOL + FP_AVATAR_FLAGS, flags, 24); }
  CHECK(take(&m, TFP_SNAPSHOT_FULL).shoulder_aim);
  controls(0, 1, 0);
  CHECK(!take(&m, TFP_SNAPSHOT_FULL).shoulder_aim);
  flag(15, 2);
  controls(0, 0, 0);
  CHECK(take(&m, TFP_SNAPSHOT_FULL).reloading);
  flag(0, 0);
  n32(CTX + 0x1fc, 1); /* back to the kind-4 row */

  /* swap_masked, stim, vault, guard */
  controls(2, 0, 0);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.blocked && !s.safe && s.weapon_transition && !s.head_camera_transition && s.mask == 2);
  controls(64, 0, 0);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.blocked && !s.safe && !s.weapon_transition && s.mask == 64);
  controls(0x20, 0, 0);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.blocked && !s.safe && s.head_camera_transition);
  controls(0, 0, 1);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.blocked && !s.safe && !s.weapon_transition && !s.head_camera_transition);
  controls(2, 0, 1);
  CHECK(!take(&m, TFP_SNAPSHOT_FULL).weapon_transition); /* the guard byte rules both annotations out */
  controls(0, 0, 0);

  /* seated */
  flag(14, 4);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.seated && !s.safe && !s.blocked);
  controls(2, 0, 0);
  CHECK(!take(&m, TFP_SNAPSHOT_FULL).weapon_transition);
  controls(0, 0, 0);
  flag(0, 0);

  /* other camera kinds are not safe */
  request(CTX + 0x200, 10, AVATAR, WEAPON, BODY);
  CHECK(!take(&m, TFP_SNAPSHOT_FULL).safe);
  request(CTX + 0x200, 4, AVATAR, WEAPON, BODY);

  /* foreign_request */
  request(CTX + 0x200, 4, AVATAR + 1, WEAPON, BODY);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Camera is not owned by local player"));
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Camera is not owned by local player"));
  request(CTX + 0x200, 4, AVATAR, WEAPON, BODY);

  /* empty_queue: the fallback record */
  n32(CTX + 0x1fc, 0);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Memory unavailable")); /* no fallback row mapped yet */
  request(CTX + 0x1f00, 4, AVATAR, WEAPON, BODY);
  s = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(s.record == CTX + 0x1f00 && s.head == s.tail && s.safe);
  n32(CTX + 0x1fc, 1);
  n32(CTX + 0x1f8, 32);
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Invalid camera request queue"));
  n32(CTX + 0x1f8, 0);

  /* no_weapon */
  n32(WIELD + 0x400, UINT32_MAX);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Equip a firearm before starting the camera test"));
  CHECK(take(&m, TFP_SNAPSHOT_PLAYER).safe);
  n32(WIELD + 0x400, 0);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Equip a firearm before starting the camera test"));
  n32(WIELD + 0x400, WEAPON);

  /* respawned: a new body and descriptor is another player identity */
  s = take(&m, TFP_SNAPSHOT_FULL);
  { uint32_t local[6] = {11, 23, AVATAR, BODY + 1, LIGHT, 1};
    put(WORLD + FP_ENTITY_ROWS, local, 24);
    put(EP, local, 20); }
  request(CTX + 0x200, 4, AVATAR, WEAPON, BODY + 1);
  other = take(&m, TFP_SNAPSHOT_FULL);
  CHECK(other.body == BODY + 1 && !tfp_same_player(&s, &other) && !tfp_same_identity(&s, &other));
  { uint32_t local[6] = {11, 22, AVATAR, BODY, LIGHT, 1};
    put(WORLD + FP_ENTITY_ROWS, local, 24);
    put(EP, local, 20); }
  request(CTX + 0x200, 4, AVATAR, WEAPON, BODY);
  /* a swapped weapon is the same player and another identity */
  other = s;
  other.weapon_unit++;
  CHECK(tfp_same_player(&s, &other) && !tfp_same_identity(&s, &other));

  /* descriptor_mismatch_is_refused */
  { uint32_t stale[5] = {11, 22, AVATAR, BODY, LIGHT + 5}; put(EP, stale, 20); }
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Avatar descriptor changed"));
  { uint32_t local[5] = {11, 22, AVATAR, BODY, LIGHT}; put(EP, local, 20); }
  n32(WORLD + FP_ENTITY_ROWS + 20, 0); /* generation byte even */
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Local player generation unavailable"));
  n32(WORLD + FP_ENTITY_ROWS + 20, 1);
  n32(POOL + 0x6c, 0);
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Local avatar retired"));
  n32(POOL + 0x6c, 9);
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Local avatar retired"));
  n32(POOL + 0x6c, 1);
  n32(PLAYER + 0x88, 0);
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Local player unavailable"));
  n32(PLAYER + 0x88, 1);
  n32(WEAPONS + 0x400 + 16, 999);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Weapon component ownership changed"));
  n32(WEAPONS + 0x400 + 16, 601);
  n32(WORLD + FP_ENTITY_ROWS + 24 + 20, 0);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Entity generation unavailable"));
  n32(WORLD + FP_ENTITY_ROWS + 24 + 20, 1);

  /* A second avatar slot, the row stride and the flag and rig offsets are checked against the
   * retail backend itself in tests/snapshot_vectors_test.c (scenes slot1, slot2, slot7). */
  /* unreadable_mid_snapshot: whichever read fails, the answer is a refusal */
  s = take(&m, TFP_SNAPSHOT_FULL);
  f.reads = 0;
  take(&m, TFP_SNAPSHOT_FULL);
  unsigned total = f.reads;
  CHECK(total > 20);
  static const uintptr_t holes[] = {0x300000000ULL + 0x1f8, 0x300000000ULL + 0x200, 0x500000000ULL,
                                    0x600000000ULL + 0x88, 0x600000000ULL + 0x3a8, 0x800000000ULL,
                                    0x900000000ULL + 0x400, 0xa00000000ULL + 0x400};
  for (unsigned i = 0; i < sizeof holes / sizeof *holes; i++) {
    f.denied = holes[i];
    TfpSnapshot out;
    char why[160] = "";
    memset(&out, 0x5a, sizeof out);
    other = out;
    CHECK(!tfp_snapshot(&m, TFP_SNAPSHOT_FULL, &out, why, sizeof why) && !memcmp(&out, &other, sizeof out) &&
          (!strcmp(why, "Memory unavailable") || !strncmp(why, "Pointer unavailable", 19)));
  }
  f.denied = 0;
  CHECK(tfp_snapshot(&m, TFP_SNAPSHOT_FULL, &other, NULL, 0) && tfp_same_identity(&s, &other));
  CHECK(!tfp_snapshot(NULL, TFP_SNAPSHOT_FULL, &other, NULL, 0) && !tfp_snapshot(&m, TFP_SNAPSHOT_FULL, NULL, NULL, 0));

  /* module identity runs inside every snapshot (texts checked against retail in the vector test) */
  char why[160] = "";
  CHECK(tfp_module_identity(&m, why, sizeof why)); /* no pin readable in the fake: tolerated */
  { uint8_t code[4] = {0x41, 0xff, 0x51, 0x39}; put(m.game + 0x12887c9, code, 4); }
  CHECK(!tfp_module_identity(&m, why, sizeof why) && !strcmp(why, "Native ADS fingerprint mismatch: root_position_call"));
  CHECK(fails(&m, TFP_SNAPSHOT_PLAYER, "Native ADS fingerprint mismatch: root_position_call"));
  { uint8_t code[4] = {0x41, 0xff, 0x51, 0x38}; put(m.game + 0x12887c9, code, 4); }
  CHECK(take(&m, TFP_SNAPSHOT_PLAYER).safe);
  n32(m.game + 72, FP_GAME_STAMP + 1);
  CHECK(fails(&m, TFP_SNAPSHOT_FULL, "Unsupported game build"));

  printf("Snapshot: %u checks; player and weapon fields, masks, aim and seat flags, queue rows, identities, refusals and unreadable memory passed\n", checks);
  return 0;
}
