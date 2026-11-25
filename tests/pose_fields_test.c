/* The pose helper's look-gain steering read (src/pose_helper.c input_steering, retail's copy of the game's
 * EntityOfNetwork) with the entity records and the player fields where src/entity_fields.h found them:
 *   - in place (HellForge 0.4): the game's own, with or without the fields handed in;
 *   - the records moved before the manager (HellForge 0.5), the game's place cleared: found once handed in, refused
 *     (not misread) without;
 *   - the player fields moved by a mod; fields that were not resolved are not taken;
 *   - built with -DTFP_FIELDS_REQUIRED (a shipped build): nothing handed in, the steering refuses.
 * The helper reads only through its host's read; nothing here calls into a game. */
#include "pose_helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool tfp_pose_steering_for_tests(const TfpPoseConfig *c, float out[2]);

static unsigned checks;
#define CHECK(condition)                                                                                         \
  do {                                                                                                           \
    checks++;                                                                                                    \
    if (!(condition)) {                                                                                          \
      fprintf(stderr, "pose fields check failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);                 \
      exit(1);                                                                                                   \
    }                                                                                                            \
  } while (0)

/* A sparse process: byte-addressed cells. */
#define CELLS (1u << 16)
static struct { uint64_t at; uint8_t value, used; } cells[CELLS];
static unsigned slot_of(uint64_t at) {
  unsigned i = (unsigned)((at * UINT64_C(0x9E3779B97F4A7C15)) >> 48) & (CELLS - 1);
  while (cells[i].used && cells[i].at != at) i = (i + 1) & (CELLS - 1);
  return i;
}
static void put(uint64_t at, const void *data, size_t size) {
  for (size_t k = 0; k < size; k++) {
    unsigned i = slot_of(at + k);
    cells[i].at = at + k, cells[i].used = 1, cells[i].value = ((const uint8_t *)data)[k];
  }
}
static void u32(uint64_t at, uint32_t v) { put(at, &v, 4); }
static void u64(uint64_t at, uint64_t v) { put(at, &v, 8); }
static bool fake_read(void *user, uint64_t at, void *out, size_t length) {
  (void)user;
  for (size_t k = 0; k < length; k++) {
    unsigned i = slot_of(at + k);
    if (!cells[i].used) return false;
    ((uint8_t *)out)[k] = cells[i].value;
  }
  return true;
}
/* An open-address map header and its eight rows: key -> value. */
static void map(uint64_t header, uint64_t buckets, uint32_t key, uint32_t value) {
  uint8_t h[20];
  uint32_t capacity = 8, empty = UINT32_MAX, multiplier = 1;
  memcpy(h, &buckets, 8), memcpy(h + 8, &capacity, 4), memcpy(h + 12, &empty, 4), memcpy(h + 16, &multiplier, 4);
  put(header, h, 20);
  for (uint32_t s = 0; s < 8; s++) u32(buckets + s * 8, empty), u32(buckets + s * 8 + 4, 0);
  u32(buckets + (key & 7) * 8, key), u32(buckets + (key & 7) * 8 + 4, value);
}

/* build 25480438's places, as src/pose_helper.c names them */
#define GAME UINT64_C(0x100000000)
#define B_GAME_PLAYER 0x3326468u
#define B_GAME_WORLD 0x346bf98u
#define B_GAME_NO_AVATAR 0x3484788u
#define B_AVATAR_STEERING 0x53e9a4u
#define B_AVATAR_STRIDE 0x1238u
static const uint64_t PLAYER = 0x600000000ULL, WORLD = 0x400000000ULL, OWNER = 0x700000000ULL;
enum { NETWORK = 0x2a, ROW = 5, AVATAR = 101, INDEX = 1 };

static void scene(int64_t records, uint32_t player_shift) {
  memset(cells, 0, sizeof cells);
  u64(GAME + B_GAME_PLAYER, PLAYER), u64(GAME + B_GAME_WORLD, WORLD), u32(GAME + B_GAME_NO_AVATAR, 0xffffffffu);
  u32(PLAYER + 0x88 + player_shift, 1), u32(PLAYER + 0x3a8 + player_shift, NETWORK);
  map(WORLD + 0xf22ec8, WORLD + 0x100, NETWORK, ROW);
  u32(WORLD + (uint64_t)records + 8 + ROW * 24, AVATAR); /* the record's +8: the entity */
  map(OWNER + 0xf8, OWNER + 0x200, AVATAR, INDEX);
  const float steering[2] = {0.25f, -0.5f};
  put(OWNER + B_AVATAR_STEERING + INDEX * B_AVATAR_STRIDE, steering, sizeof steering);
}
static bool steer(float out[2]) {
  TfpPoseConfig c;
  memset(&c, 0, sizeof c);
  c.game = GAME, c.avatar_owner = OWNER, c.avatar = AVATAR, c.avatar_index = INDEX;
  out[0] = out[1] = 9;
  return tfp_pose_steering_for_tests(&c, out);
}

int main(void) {
  TfpPoseHost h;
  memset(&h, 0, sizeof h);
  h.read = fake_read;
  tfp_pose_set_host(&h);
  float out[2];
  TfpEntityFields game = tfp_fields_game(), moved = game;

  /* in place (0.4) */
  tfp_pose_reset_for_tests();
  scene(0xf32f18, 0);
#ifdef TFP_FIELDS_REQUIRED
  CHECK(!steer(out)); /* a shipped build that was handed nothing refuses */
#else
  CHECK(steer(out) && out[0] == 0.25f && out[1] == -0.5f);
#endif
  CHECK(tfp_pose_set_entity_fields(&game) == 1);
  CHECK(steer(out) && out[0] == 0.25f && out[1] == -0.5f);

  /* 0.5: the records 32 MiB before the manager; the game's place holds nothing */
  moved.records = -INT64_C(0x2000000), moved.moved = TFP_FIELD_RECORDS;
  scene(moved.records, 0);
  CHECK(!steer(out)); /* still given the game's own: the avatar there is 0, not ours: refused */
  CHECK(tfp_pose_set_entity_fields(&moved) == 1);
  CHECK(steer(out) && out[0] == 0.25f && out[1] == -0.5f);

  /* the player fields moved by a mod (+0x400) as well */
  moved.player_count += 0x400, moved.player_live += 0x400, moved.player_descriptors += 0x400, moved.player_network += 0x400;
  scene(moved.records, 0x400);
  CHECK(!steer(out));
  CHECK(tfp_pose_set_entity_fields(&moved) == 1);
  CHECK(steer(out) && out[0] == 0.25f && out[1] == -0.5f);

  /* fields that were not resolved are not taken; the ones before stay */
  TfpEntityFields unresolved = game;
  unresolved.resolved = false;
  CHECK(tfp_pose_set_entity_fields(&unresolved) == 0 && tfp_pose_set_entity_fields(NULL) == 0);
  CHECK(steer(out) && out[0] == 0.25f);

  /* the test reset puts the game's own back (and, in a shipped build, nothing handed in) */
  tfp_pose_reset_for_tests();
  CHECK(!steer(out));
#ifdef TFP_FIELDS_REQUIRED
  printf("Pose helper fields (shipped-build rules): %u checks passed\n", checks);
#else
  printf("Pose helper fields: %u checks passed\n", checks);
#endif
  return 0;
}
