/* HellForge 0.5: the entity records, the entity maps and the player fields read where the game's code has them
 * (src/entity_fields.h), and the two snapshots (src/snapshot.c, src/backend.c) reading through them.
 *   - the resolver on the game's own code (HellForge 0.4, the unmodded game): every value the game's own;
 *   - the records moved before the manager (a signed, negative offset), the player lists moved by a mod;
 *   - an unreadable site waits, a mismatching instruction or values that do not hold together refuse;
 *   - the displacement is taken from the live bytes, the instruction check from the code reader;
 *   - both snapshots with the records in place and moved, the player fields moved, the network-id map grown to 8192
 *     slots with the local player behind a long probe chain and past the game's 2048 records, and unresolved fields.
 * Built twice by tests/run-entity-fields.sh: as the tests are, and with -DTFP_FIELDS_REQUIRED (a shipped build), where
 * a reader handed no fields must refuse. */
#include "entity_fields.h"
#include "snapshot.h"
#include "fake_native.h"
#include <stdlib.h>

static unsigned checks;
#define CHECK(condition)                                                                                         \
  do {                                                                                                           \
    checks++;                                                                                                    \
    if (!(condition)) {                                                                                          \
      fprintf(stderr, "entity fields check failed: %s:%d: %s\n", __FILE__, __LINE__, #condition);               \
      exit(1);                                                                                                   \
    }                                                                                                            \
  } while (0)

/* ---- the resolver, on a fake game.dll holding build 25480438's fifteen instructions ---- */
#define GAME UINT64_C(0x100000000)
static Fake code_image, live_image; /* what code_matches reads (before any hook) and what the game runs now */
static bool code_read(void *user, uint64_t at, void *out, size_t size) { (void)user; return read_fake(&code_image, (uintptr_t)at, out, size); }
static bool live_read(void *user, uint64_t at, void *out, size_t size) { (void)user; return read_fake(&live_image, (uintptr_t)at, out, size); }
static void put_into(Fake *x, uintptr_t at, const void *data, size_t size) { /* fake_native.h put(), into another image */
  for (unsigned i = x->count; i > 0; i--) {
    Block *b = x->blocks + i - 1;
    if (at >= b->at && at - b->at <= b->size && size <= b->size - (at - b->at)) {
      memcpy(b->data + (at - b->at), data, size);
      return;
    }
  }
  assert(x->count < 256 && size <= 8192);
  Block *b = x->blocks + x->count++;
  b->at = at, b->size = size;
  memcpy(b->data, data, size);
}
static void site(unsigned which, int32_t disp) { /* both images: the instruction and its displacement */
  const TfpFieldSite *s = tfp_field_sites + which;
  uint8_t bytes[16];
  memcpy(bytes, s->before, s->before_size);
  memcpy(bytes + s->before_size, &disp, 4);
  memcpy(bytes + s->before_size + 4, s->after, s->after_size);
  put_into(&code_image, (uintptr_t)(GAME + s->rva - s->before_size), bytes, s->before_size + 4u + s->after_size);
  put_into(&live_image, (uintptr_t)(GAME + s->rva - s->before_size), bytes, s->before_size + 4u + s->after_size);
}
static void live_disp(unsigned which, int32_t disp) { put_into(&live_image, (uintptr_t)(GAME + tfp_field_sites[which].rva), &disp, 4); }
/* The image as build 25480438 has it (tests/test values checked against game-pre1020.dll by tools/verify_entity_sites.py). */
static void game_image(void) {
  code_image.count = live_image.count = 0, code_image.denied = live_image.denied = 0;
  site(TFP_SITE_RECORDS, 0x1e65e3), site(TFP_SITE_RECORDS_NETWORK, 0x1e65e4);
  site(TFP_SITE_NETWORK_BUCKETS, 0xf22ec8), site(TFP_SITE_NETWORK_CAPACITY, 0xf22ed0), site(TFP_SITE_NETWORK_EMPTY, 0xf22ed4),
      site(TFP_SITE_NETWORK_MULTIPLIER, 0xf22ed8);
  site(TFP_SITE_ENTITY_BUCKETS, 0xf1aeb0), site(TFP_SITE_ENTITY_CAPACITY, 0xf1aeb8), site(TFP_SITE_ENTITY_EMPTY, 0xf1aebc),
      site(TFP_SITE_ENTITY_MULTIPLIER, 0xf1aec0);
  site(TFP_SITE_PLAYER_COUNT, 0x84), site(TFP_SITE_PLAYER_LIVE, 0x88), site(TFP_SITE_PLAYER_DESCRIPTORS, 0xe8),
      site(TFP_SITE_PLAYER_NETWORK, 0x3a8), site(TFP_SITE_PLAYER_NETWORK_STEERING, 0x3a8);
}
static TfpFieldsResult resolve(TfpEntityFields *out, char *why) { return tfp_fields_resolve(GAME, code_read, live_read, NULL, out, why, 200); }

static void resolver(void) {
  TfpEntityFields got, game = tfp_fields_game();
  char why[200], line[400];
  /* HellForge 0.4 and the unmodded game: the game's own, nothing moved */
  game_image();
  CHECK(resolve(&got, why) == TFP_FIELDS_OK && !*why);
  CHECK(got.resolved && got.moved == 0 && got.records == 0xf32f18 && got.network_map == 0xf22ec8 && got.entity_map == 0xf1aeb0);
  CHECK(got.player_count == 0x84 && got.player_live == 0x88 && got.player_descriptors == 0xe8 && got.player_network == 0x3a8);
  CHECK(got.records == game.records && got.network_map == game.network_map && got.entity_map == game.entity_map &&
        got.player_count == game.player_count && got.player_live == game.player_live &&
        got.player_descriptors == game.player_descriptors && got.player_network == game.player_network);
  tfp_fields_describe(&got, line, sizeof line);
  CHECK(strstr(line, "entity records manager+0xf32f18 (the game's own)") && strstr(line, "network ids +0x3a8 (the game's own)") &&
        !strstr(line, "moved"));

  /* HellForge 0.5: the records behind the Game block, here 32 MiB before the manager (8 x a negative displacement) */
  game_image();
  site(TFP_SITE_RECORDS, -0x400000), site(TFP_SITE_RECORDS_NETWORK, -0x400000 + 1);
  CHECK(resolve(&got, why) == TFP_FIELDS_OK);
  CHECK(got.records == -INT64_C(0x2000000) && got.moved == TFP_FIELD_RECORDS && got.network_map == 0xf22ec8);
  tfp_fields_describe(&got, line, sizeof line);
  CHECK(strstr(line, "entity records manager-0x2000000 (moved)") && strstr(line, "network-id map +0xf22ec8 (the game's own)"));
  CHECK(tfp_fields_record(0x400000000ULL, &got, 3) == 0x400000000ULL - 0x2000000 + 72);
  /* and far beyond the manager */
  site(TFP_SITE_RECORDS, 0x10000000), site(TFP_SITE_RECORDS_NETWORK, 0x10000001);
  CHECK(resolve(&got, why) == TFP_FIELDS_OK && got.records == INT64_C(0x80000000));

  /* the displacement is the one the game runs now: the code reader may still hold the game's original */
  game_image();
  live_disp(TFP_SITE_RECORDS, -0x400000), live_disp(TFP_SITE_RECORDS_NETWORK, -0x400000 + 1);
  CHECK(resolve(&got, why) == TFP_FIELDS_OK && got.records == -INT64_C(0x2000000));

  /* the two record sites disagree (one rewritten, one not): refused, nothing written */
  game_image();
  memset(&got, 0x5a, sizeof got);
  TfpEntityFields before = got;
  live_disp(TFP_SITE_RECORDS, -0x400000);
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "entity records disagree") && !memcmp(&got, &before, sizeof got));
  /* records on top of the manager's own fields */
  game_image();
  site(TFP_SITE_RECORDS, 0x20000), site(TFP_SITE_RECORDS_NETWORK, 0x20001);
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "manager's own fields"));

  /* an unreadable site: wait (a cold page), naming it */
  game_image();
  live_image.denied = (uintptr_t)(GAME + tfp_field_sites[TFP_SITE_PLAYER_NETWORK].rva + 2);
  CHECK(resolve(&got, why) == TFP_FIELDS_UNREADABLE && strstr(why, "game+0xacc670") && strstr(why, "not readable"));
  game_image();
  code_image.denied = (uintptr_t)(GAME + tfp_field_sites[TFP_SITE_RECORDS].rva - 1);
  CHECK(resolve(&got, why) == TFP_FIELDS_UNREADABLE && strstr(why, "game+0xfd9dde"));
  /* a mismatching instruction: refused, naming it (before and after the displacement) */
  game_image();
  {
    uint8_t other = 0x8b;
    put_into(&code_image, (uintptr_t)(GAME + tfp_field_sites[TFP_SITE_RECORDS].rva - 1), &other, 1);
  }
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "game+0xfd9dde") && strstr(why, "not the reviewed one"));
  game_image();
  {
    uint8_t other = 0x90;
    put_into(&code_image, (uintptr_t)(GAME + tfp_field_sites[TFP_SITE_NETWORK_BUCKETS].rva + 4), &other, 1);
  }
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "game+0xfd9bfc"));

  /* the map headers: moved as a whole is taken; a header that does not hold together refuses */
  game_image();
  site(TFP_SITE_NETWORK_BUCKETS, 0xf40000), site(TFP_SITE_NETWORK_CAPACITY, 0xf40008), site(TFP_SITE_NETWORK_EMPTY, 0xf4000c),
      site(TFP_SITE_NETWORK_MULTIPLIER, 0xf40010);
  CHECK(resolve(&got, why) == TFP_FIELDS_OK && got.network_map == 0xf40000 && got.moved == TFP_FIELD_NETWORK_MAP);
  site(TFP_SITE_NETWORK_MULTIPLIER, 0xf22ed8);
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "network-id map header"));
  game_image();
  site(TFP_SITE_ENTITY_BUCKETS, 0xf1aeb4), site(TFP_SITE_ENTITY_CAPACITY, 0xf1aebc), site(TFP_SITE_ENTITY_EMPTY, 0xf1aec0),
      site(TFP_SITE_ENTITY_MULTIPLIER, 0xf1aec4);
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "entity map header")); /* not 8-aligned */

  /* the player lists a mod moved (+0x400), every site following */
  game_image();
  site(TFP_SITE_PLAYER_COUNT, 0x484), site(TFP_SITE_PLAYER_LIVE, 0x488), site(TFP_SITE_PLAYER_DESCRIPTORS, 0x4e8),
      site(TFP_SITE_PLAYER_NETWORK, 0x7a8), site(TFP_SITE_PLAYER_NETWORK_STEERING, 0x7a8);
  CHECK(resolve(&got, why) == TFP_FIELDS_OK && got.player_count == 0x484 && got.player_live == 0x488 && got.player_descriptors == 0x4e8 &&
        got.player_network == 0x7a8 && got.moved == TFP_FIELD_PLAYERS);
  site(TFP_SITE_PLAYER_NETWORK_STEERING, 0x3a8); /* one site not rewritten */
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "network ids disagree"));
  game_image();
  site(TFP_SITE_PLAYER_LIVE, 0x8c);
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "player count"));
  game_image();
  site(TFP_SITE_PLAYER_DESCRIPTORS, 0xec);
  CHECK(resolve(&got, why) == TFP_FIELDS_REFUSED && strstr(why, "player descriptors"));
}

/* ---- the snapshots ---- */
enum { AVATAR = 101, WEAPON = 201, BODY = 401, UNIT = 501, LIGHT = 301 };
static const uintptr_t WORLD = 0x400000000ULL, PLAYER = 0x600000000ULL;

static bool take(const FpMemory *m, TfpSnapshot *s, char *why) { return tfp_snapshot(m, TFP_SNAPSHOT_FULL, s, why, 160); }
static bool take_old(const FpMemory *m, FpSnapshot *s, char *why) { return fp_snapshot(m, true, s, why, 160); }
static void same(const TfpSnapshot *a, const TfpSnapshot *b) {
  CHECK(a->avatar == AVATAR && a->body == BODY && a->weapon == WEAPON && a->weapon_unit == UNIT && a->local_entity == LIGHT && a->full);
  CHECK(tfp_same_identity(a, b) && !memcmp(a->avatar_descriptor, b->avatar_descriptor, 20) &&
        !memcmp(a->weapon_descriptor, b->weapon_descriptor, 20));
}
static void same_old(const FpSnapshot *a) {
  CHECK(a->avatar == AVATAR && a->body == BODY && a->weapon == WEAPON && a->weapon_unit == UNIT && a->local_entity == LIGHT && a->weapon_ready);
}
/* The two records fake_native.h writes at the game's place, moved to manager + `records`; the game's place cleared,
 * as HellForge 0.5 leaves it. */
static void move_records(int64_t records) {
  uint32_t local[6] = {11, 22, 101, 401, 301, 1}, weapon[6] = {33, 44, 201, 501, 601, 1}, zero[12] = {0};
  put(WORLD + (uint64_t)records, local, 24);
  put(WORLD + (uint64_t)records + 24, weapon, 24);
  put(WORLD + FP_ENTITY_ROWS, zero, 48);
}

static void snapshots(void) {
  FpMemory m = setup();
  TfpSnapshot in_place, s;
  FpSnapshot old;
  char why[160];
  { const uint8_t amd64[2] = {0x64, 0x86}; put(m.game + 68, amd64, 2); } /* as tests/snapshot_test.c */

  /* the records in place (0.4), given the game's own fields or none (a test build) */
  TfpEntityFields game = tfp_fields_game(), moved = game;
  m.fields = &game;
  CHECK(take(&m, &in_place, why));
  CHECK(take_old(&m, &old, why)); same_old(&old);
  m.fields = NULL;
#ifdef TFP_FIELDS_REQUIRED
  /* a shipped build: a reader handed nothing refuses rather than read at the game's place */
  CHECK(!take(&m, &s, why) && !strcmp(why, "Entity fields unresolved"));
  CHECK(!take_old(&m, &old, why));
#else
  CHECK(take(&m, &s, why)); same(&s, &in_place);
  CHECK(take_old(&m, &old, why)); same_old(&old);
#endif

  /* 0.5: the records 32 MiB before the manager */
  moved.records = -INT64_C(0x2000000), moved.moved = TFP_FIELD_RECORDS;
  move_records(moved.records);
  m.fields = &game; /* the game's place now holds nothing: both refuse */
  CHECK(!take(&m, &s, why) && !strcmp(why, "Local player generation unavailable"));
  CHECK(!take_old(&m, &old, why) && !strcmp(why, "Local player descriptor unavailable"));
  m.fields = &moved;
  CHECK(take(&m, &s, why)); same(&s, &in_place);
  CHECK(take_old(&m, &old, why)); same_old(&old);

  /* fields handed in but not resolved (the session refused them): refuse */
  TfpEntityFields unresolved = moved;
  unresolved.resolved = false;
  m.fields = &unresolved;
  CHECK(!take(&m, &s, why) && !strcmp(why, "Entity fields unresolved"));
  CHECK(!take_old(&m, &old, why));

  /* the player lists moved by a mod (+0x400): the old places cleared */
  moved.player_count += 0x400, moved.player_live += 0x400, moved.player_descriptors += 0x400, moved.player_network += 0x400;
  moved.moved |= TFP_FIELD_PLAYERS;
  n32(PLAYER + 0x88, 0), n32(PLAYER + 0x3a8, 0), n32(PLAYER + 0x488, 1), n32(PLAYER + 0x7a8, LIGHT);
  m.fields = &moved;
  CHECK(take(&m, &s, why)); same(&s, &in_place);
  CHECK(take_old(&m, &old, why)); same_old(&old);
  m.fields = &game;
  CHECK(!take(&m, &s, why) && !strcmp(why, "Local player unavailable"));

  /* HellForge 0.5's network-id map of 8192 slots: the local player's network id hashes into the upper half (slot
   * 8127), behind 300 other ids that wrap past the last slot, so it sits near the start; its record is 3000 (past the
   * game's 2048 records) */
  {
    const uintptr_t buckets = 0x1400000000ULL;
    const uint32_t capacity = 8192, empty = UINT32_MAX, multiplier = 8000 / LIGHT + 1, start = (LIGHT * multiplier) & (capacity - 1);
    static uint32_t rows[8192 * 2];
    memset(rows, 255, sizeof rows);
    for (uint32_t n = 0; n < 300; n++) rows[((start + n) & (capacity - 1)) * 2] = 100000 + n, rows[((start + n) & (capacity - 1)) * 2 + 1] = n;
    const uint32_t slot = (start + 300) & (capacity - 1);
    rows[slot * 2] = LIGHT, rows[slot * 2 + 1] = 3000;
    for (unsigned chunk = 0; chunk < 8; chunk++) put(buckets + chunk * 8192, (const uint8_t *)rows + chunk * 8192, 8192);
    uint8_t header[20];
    uint64_t at = buckets;
    memcpy(header, &at, 8), memcpy(header + 8, &capacity, 4), memcpy(header + 12, &empty, 4), memcpy(header + 16, &multiplier, 4);
    put(WORLD + moved.network_map, header, 20);
    CHECK(start >= 4096 && slot < start);
    uint32_t local[6] = {11, 22, 101, 401, 301, 1};
    put(WORLD + (uint64_t)moved.records + 3000 * 24, local, 24);
    m.fields = &moved;
    CHECK(take(&m, &s, why)); same(&s, &in_place);
    CHECK(take_old(&m, &old, why)); same_old(&old);
    /* an id that is not in the map: the probe ends at the first empty slot, as the game's */
    n32(PLAYER + 0x7a8, 999);
    CHECK(!take(&m, &s, why) && !strcmp(why, "Entity absent"));
    n32(PLAYER + 0x7a8, LIGHT);
  }
}

int main(void) {
  resolver();
  snapshots();
#ifdef TFP_FIELDS_REQUIRED
  printf("Entity fields (shipped-build rules): %u checks passed\n", checks);
#else
  printf("Entity fields: %u checks passed\n", checks);
#endif
  return 0;
}
