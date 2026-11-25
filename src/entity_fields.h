/* The manager fields that HellForge 0.5 (or a mod on it) can move, read where the game's code addresses them now.
 *
 * HellForge 0.5 enlarges the world's entity table (4096 records a world, the game's 2048; entity maps of 8192 slots)
 * and moves the 24-byte entity records out of the EntityManager, "behind the Game block", rewriting every game
 * instruction that addresses them. A mod that grows a per-player list moves PlayerManager's fields the same way.
 * HellForge SDK 20, docs/modding/hooks.md "Fields a mod moved": take the offset from the displacement of an instruction
 * that uses the field, check the instruction first (the bytes before the displacement), refuse a value that is not
 * aligned or not where the field can be. docs/design/specs/2026-11-25-hellforge-05.md has the reasoning.
 *
 * Here every field is read from two places of the game's code where the SDK names one, and the two must agree:
 *   records       EntityDescriptor (game+0xFD9D40) lea rax,[rax+disp] at game+0xFD9DDE, 8 x disp (the SDK's site), and
 *                 EntityOfNetwork (game+0xFD9BA0) at game+0xFD9C53, which addresses the record's +8 (disp + 1).
 *                 Signed: the records may lie before the manager.
 *   network map   EntityOfNetwork's four reads of the map header (buckets, capacity, empty key, multiplier)
 *   entity map    EntityDescriptor's four reads of its map header
 *   players       count +0x84 (game+0x62C727, the SDK's), the second count +0x88 the game's steering reads
 *                 (game+0x128D836; it must be count + 4), descriptors +0xE8 (game+0x62C755, the SDK's), network ids
 *                 +0x3A8 (game+0xACC670, the SDK's, and the steering's game+0x128D83F)
 * On HellForge 0.4 and the unmodded game every displacement is the game's own, so the values are the constants the
 * mod used before. Header only: every consumer includes it, no build list changes. Reads only; no state. */
#ifndef TFP_ENTITY_FIELDS_H
#define TFP_ENTITY_FIELDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The game's own offsets (build 25480438). */
#define TFP_FIELDS_GAME_RECORDS      INT64_C(0xF32F18)  /* EntityManager + : the 24-byte entity records */
#define TFP_FIELDS_GAME_NETWORK_MAP  UINT32_C(0xF22EC8) /* EntityManager + : network ids to records (profile.h FP_LIGHT_MAP) */
#define TFP_FIELDS_GAME_ENTITY_MAP   UINT32_C(0xF1AEB0) /* EntityManager + : entity ids to records (profile.h FP_ENTITY_MAP) */
#define TFP_FIELDS_GAME_PLAYER_COUNT UINT32_C(0x84)     /* PlayerManager + : players (u32) */
#define TFP_FIELDS_GAME_PLAYER_LIVE  UINT32_C(0x88)     /* PlayerManager + : the second count, which the mod and the steering read */
#define TFP_FIELDS_GAME_PLAYER_DESCRIPTORS UINT32_C(0xE8) /* PlayerManager + : player descriptor pointers, 8 a player */
#define TFP_FIELDS_GAME_PLAYER_NETWORK UINT32_C(0x3A8)  /* PlayerManager + : the local player's Helldiver network id, 0x20 a player */
#define TFP_FIELDS_RECORD_SIZE 24u
#define TFP_FIELDS_RECORDS_MAX 4096u                    /* HellForge 0.5: 4096 records a world (the game's 2048) */

enum {
  TFP_FIELD_RECORDS = 1u << 0, TFP_FIELD_NETWORK_MAP = 1u << 1, TFP_FIELD_ENTITY_MAP = 1u << 2,
  TFP_FIELD_PLAYER_COUNT = 1u << 3, TFP_FIELD_PLAYER_DESCRIPTORS = 1u << 4, TFP_FIELD_PLAYER_NETWORK = 1u << 5,
  TFP_FIELD_PLAYERS = TFP_FIELD_PLAYER_COUNT | TFP_FIELD_PLAYER_DESCRIPTORS | TFP_FIELD_PLAYER_NETWORK
};

typedef struct TfpEntityFields {
  bool resolved;               /* read from the game's code (or the game's own, for a caller that passes none) */
  int64_t records;             /* EntityManager + records: record i at manager + records + 24 i (signed) */
  uint32_t network_map, entity_map;                      /* EntityManager + the map headers (20 bytes each) */
  uint32_t player_count, player_live, player_descriptors, player_network;  /* PlayerManager + */
  uint32_t moved;              /* TFP_FIELD_* bits: the fields that are not where the game has them */
} TfpEntityFields;

static inline TfpEntityFields tfp_fields_game(void) {
  TfpEntityFields f = {true, TFP_FIELDS_GAME_RECORDS, TFP_FIELDS_GAME_NETWORK_MAP, TFP_FIELDS_GAME_ENTITY_MAP,
                       TFP_FIELDS_GAME_PLAYER_COUNT, TFP_FIELDS_GAME_PLAYER_LIVE, TFP_FIELDS_GAME_PLAYER_DESCRIPTORS,
                       TFP_FIELDS_GAME_PLAYER_NETWORK, 0};
  return f;
}

/* The fields a reader uses: `given` when it was resolved; with none given, the game's own, except in a shipped build
 * (TFP_FIELDS_REQUIRED, set by build.sh for the view and feel DLLs), where a reader that was handed nothing refuses
 * rather than read where 0.5 may have moved things. NULL: refuse. */
static inline const TfpEntityFields *tfp_fields_use(const TfpEntityFields *given, TfpEntityFields *scratch) {
  if (given) return given->resolved ? given : NULL;
#ifdef TFP_FIELDS_REQUIRED
  (void)scratch;
  return NULL;
#else
  *scratch = tfp_fields_game();
  return scratch;
#endif
}
/* manager + signed offset (+ index * 24): wraps like the game's own lea. */
static inline uint64_t tfp_fields_record(uint64_t manager, const TfpEntityFields *f, uint32_t index) {
  return manager + (uint64_t)f->records + (uint64_t)index * TFP_FIELDS_RECORD_SIZE;
}

/* ---- reading the fields from the game's code ----------------------------------------------------------------------
 * `code` reads code bytes as code_matches does (HellForge's read_code: before any hook), for the instruction check;
 * `live` reads the bytes the game runs now (HellForge's read), for the displacement itself, because a mod that moved a
 * field rewrote that displacement and the bytes "before any hook" may be the game's original ones. In tests both are
 * the same fake. Each returns all of the bytes or false. */
typedef bool (*TfpFieldsRead)(void *user, uint64_t at, void *out, size_t size);
typedef enum TfpFieldsResult { TFP_FIELDS_OK = 0, TFP_FIELDS_UNREADABLE = 1, TFP_FIELDS_REFUSED = 2 } TfpFieldsResult;

/* One instruction: the bytes before its 4-byte displacement and the bytes after it, as in build 25480438's image
 * (game-pre1020.dll, file offset = RVA). */
typedef struct TfpFieldSite { const char *name; uint32_t rva; uint8_t before_size, after_size; uint8_t before[7], after[5]; } TfpFieldSite;
enum {
  TFP_SITE_RECORDS, TFP_SITE_RECORDS_NETWORK,
  TFP_SITE_NETWORK_BUCKETS, TFP_SITE_NETWORK_CAPACITY, TFP_SITE_NETWORK_EMPTY, TFP_SITE_NETWORK_MULTIPLIER,
  TFP_SITE_ENTITY_BUCKETS, TFP_SITE_ENTITY_CAPACITY, TFP_SITE_ENTITY_EMPTY, TFP_SITE_ENTITY_MULTIPLIER,
  TFP_SITE_PLAYER_COUNT, TFP_SITE_PLAYER_LIVE, TFP_SITE_PLAYER_DESCRIPTORS, TFP_SITE_PLAYER_NETWORK, TFP_SITE_PLAYER_NETWORK_STEERING,
  TFP_FIELD_SITES
};
static const TfpFieldSite tfp_field_sites[TFP_FIELD_SITES] = {
    /* lea rax,[rax+rax*2]; lea rax,[rax+disp]; lea rax,[r10+rax*8]: EntityDescriptor's record (SDK: 8 x these 4 bytes) */
    {"entity records (EntityDescriptor)", UINT32_C(0xFD9DDE), 7, 4, {0x48, 0x8d, 0x04, 0x40, 0x48, 0x8d, 0x80}, {0x49, 0x8d, 0x04, 0xc2}},
    /* the same in EntityOfNetwork, with r11: the record's +8 (the entity), so disp is the records' + 1 */
    {"entity records (EntityOfNetwork)", UINT32_C(0xFD9C53), 7, 4, {0x48, 0x8d, 0x04, 0x40, 0x48, 0x8d, 0x80}, {0x49, 0x8d, 0x04, 0xc3}},
    {"network map buckets", UINT32_C(0xFD9BFC), 3, 3, {0x49, 0x8b, 0xbb}, {0x41, 0x8b, 0xb3}},            /* mov rdi,[r11+disp] */
    {"network map capacity", UINT32_C(0xFD9BCC), 3, 2, {0x45, 0x8b, 0x93}, {0x33, 0xd2}},                 /* mov r10d,[r11+disp] */
    {"network map empty key", UINT32_C(0xFD9C03), 3, 2, {0x41, 0x8b, 0xb3}, {0x66, 0x0f}},                /* mov esi,[r11+disp] */
    {"network map multiplier", UINT32_C(0xFD9BDA), 3, 2, {0x41, 0x8b, 0x9b}, {0x48, 0x89}},               /* mov ebx,[r11+disp] */
    {"entity map buckets", UINT32_C(0xFD9D86), 3, 3, {0x49, 0x8b, 0x9a}, {0x41, 0x8b, 0xba}},             /* mov rbx,[r10+disp] */
    {"entity map capacity", UINT32_C(0xFD9D6B), 3, 3, {0x45, 0x8b, 0x8a}, {0x45, 0x8b, 0x9a}},            /* mov r9d,[r10+disp] */
    {"entity map empty key", UINT32_C(0xFD9D8D), 3, 2, {0x41, 0x8b, 0xba}, {0x0f, 0x1f}},                 /* mov edi,[r10+disp] */
    {"entity map multiplier", UINT32_C(0xFD9D72), 3, 4, {0x45, 0x8b, 0x9a}, {0x44, 0x0f, 0xaf, 0xd9}},    /* mov r11d,[r10+disp] */
    {"player count", UINT32_C(0x62C727), 3, 3, {0x44, 0x8b, 0x91}, {0x45, 0x85, 0xd2}},                   /* mov r10d,[rcx+disp] (SDK) */
    {"player second count (steering)", UINT32_C(0x128D836), 2, 2, {0x83, 0xb8}, {0x00, 0x74}},           /* cmp dword [rax+disp],0 */
    {"player descriptors", UINT32_C(0x62C755), 4, 3, {0x48, 0x8b, 0x8c, 0xc1}, {0x8b, 0x51, 0x08}},       /* mov rcx,[rcx+rax*8+disp] (SDK) */
    {"player network ids", UINT32_C(0xACC670), 4, 1, {0x42, 0x8b, 0x94, 0x30}, {0xe8}},                  /* mov edx,[rax+r14+disp] (SDK) */
    {"player network ids (steering)", UINT32_C(0x128D83F), 2, 5, {0x8b, 0x90}, {0x48, 0x8d, 0x4c, 0x24, 0x30}}, /* mov edx,[rax+disp] */
};

/* The checked displacement of one site. UNREADABLE: a read failed (a cold page: ask again later); REFUSED: the bytes
 * around it are not the reviewed instruction. */
static inline TfpFieldsResult tfp_field_site(uint64_t game, unsigned which, TfpFieldsRead code, TfpFieldsRead live, void *user,
                                             int32_t *out, char *why, size_t why_size) {
  const TfpFieldSite *s = tfp_field_sites + which;
  uint8_t before[7], after[5], disp[4];
  const uint64_t at = game + s->rva;
  if (!code(user, at - s->before_size, before, s->before_size) || !code(user, at + 4, after, s->after_size) || !live(user, at, disp, 4)) {
    if (why && why_size) snprintf(why, why_size, "the game's code at game+0x%x (%s) is not readable yet", (unsigned)s->rva, s->name);
    return TFP_FIELDS_UNREADABLE;
  }
  if (memcmp(before, s->before, s->before_size) || memcmp(after, s->after, s->after_size)) {
    if (why && why_size) snprintf(why, why_size, "the instruction at game+0x%x (%s) is not the reviewed one", (unsigned)s->rva, s->name);
    return TFP_FIELDS_REFUSED;
  }
  memcpy(out, disp, 4);
  return TFP_FIELDS_OK;
}

/* All fields, checked. TFP_FIELDS_OK: *out filled and resolved. Otherwise *out is untouched and `why` says which site
 * and what. A refusal means a reader must not guess: first person stays off. */
static inline TfpFieldsResult tfp_fields_resolve(uint64_t game, TfpFieldsRead code, TfpFieldsRead live, void *user, TfpEntityFields *out,
                                                 char *why, size_t why_size) {
  int32_t d[TFP_FIELD_SITES];
  if (why && why_size) *why = 0;
  if (!game || !code || !live || !out) {
    if (why && why_size) snprintf(why, why_size, "no game module");
    return TFP_FIELDS_UNREADABLE;
  }
  for (unsigned i = 0; i < TFP_FIELD_SITES; i++) {
    TfpFieldsResult r = tfp_field_site(game, i, code, live, user, d + i, why, why_size);
    if (r != TFP_FIELDS_OK) return r;
  }
#define TFP_FIELDS_REFUSE(...) do { if (why && why_size) snprintf(why, why_size, __VA_ARGS__); return TFP_FIELDS_REFUSED; } while (0)
  TfpEntityFields f = tfp_fields_game();
  f.resolved = true, f.moved = 0;
  /* the records: 8 x the signed displacement; both functions must address the same records */
  if (d[TFP_SITE_RECORDS_NETWORK] != (int32_t)((uint32_t)d[TFP_SITE_RECORDS] + 1u))
    TFP_FIELDS_REFUSE("the entity records disagree: EntityDescriptor says manager %+lld, EntityOfNetwork %+lld (less 8)",
                      (long long)d[TFP_SITE_RECORDS] * 8, (long long)d[TFP_SITE_RECORDS_NETWORK] * 8 - 8);
  f.records = (int64_t)d[TFP_SITE_RECORDS] * 8;
  /* Not on top of the manager's own fields: below the game's own records lie its maps and tables. Before the manager
   * (negative), at the game's place or beyond it (inside the instruction's reach of +-16 GiB) are where a mover puts them. */
  if (f.records == 0 || (f.records > 0 && f.records < TFP_FIELDS_GAME_RECORDS))
    TFP_FIELDS_REFUSE("the entity records would lie on the manager's own fields (manager %+lld)", (long long)f.records);
  /* the two map headers: buckets, then capacity +8, empty key +12, multiplier +16, inside the manager */
  for (unsigned m = 0; m < 2; m++) {
    const unsigned b = m ? TFP_SITE_ENTITY_BUCKETS : TFP_SITE_NETWORK_BUCKETS;
    const int32_t at = d[b];
    if (at <= 0 || at >= 0x2000000 || (at & 7) || d[b + 1] != at + 8 || d[b + 2] != at + 12 || d[b + 3] != at + 16)
      TFP_FIELDS_REFUSE("the %s map header does not hold together (buckets %+d, capacity %+d, empty %+d, multiplier %+d)",
                        m ? "entity" : "network-id", at, d[b + 1], d[b + 2], d[b + 3]);
    if (m) f.entity_map = (uint32_t)at;
    else f.network_map = (uint32_t)at;
  }
  /* PlayerManager: aligned, inside the manager (64 KiB is far more than any per-player list needs), the second count
   * right after the first, both network-id sites the same */
  const int32_t count = d[TFP_SITE_PLAYER_COUNT], live_count = d[TFP_SITE_PLAYER_LIVE], descriptors = d[TFP_SITE_PLAYER_DESCRIPTORS],
                network = d[TFP_SITE_PLAYER_NETWORK];
  if (count <= 0 || count >= 0x10000 || (count & 3) || live_count != count + 4)
    TFP_FIELDS_REFUSE("the player count is not where it can be (+%d, the second +%d)", count, live_count);
  if (descriptors <= 0 || descriptors >= 0x10000 || (descriptors & 7))
    TFP_FIELDS_REFUSE("the player descriptors are not where they can be (+%d)", descriptors);
  if (network <= 0 || network >= 0x10000 || (network & 3) || d[TFP_SITE_PLAYER_NETWORK_STEERING] != network)
    TFP_FIELDS_REFUSE("the players' network ids disagree (+%d, the steering's +%d)", network, d[TFP_SITE_PLAYER_NETWORK_STEERING]);
#undef TFP_FIELDS_REFUSE
  f.player_count = (uint32_t)count, f.player_live = (uint32_t)live_count, f.player_descriptors = (uint32_t)descriptors,
  f.player_network = (uint32_t)network;
  if (f.records != TFP_FIELDS_GAME_RECORDS) f.moved |= TFP_FIELD_RECORDS;
  if (f.network_map != TFP_FIELDS_GAME_NETWORK_MAP) f.moved |= TFP_FIELD_NETWORK_MAP;
  if (f.entity_map != TFP_FIELDS_GAME_ENTITY_MAP) f.moved |= TFP_FIELD_ENTITY_MAP;
  if (f.player_count != TFP_FIELDS_GAME_PLAYER_COUNT) f.moved |= TFP_FIELD_PLAYER_COUNT;
  if (f.player_descriptors != TFP_FIELDS_GAME_PLAYER_DESCRIPTORS) f.moved |= TFP_FIELD_PLAYER_DESCRIPTORS;
  if (f.player_network != TFP_FIELDS_GAME_PLAYER_NETWORK) f.moved |= TFP_FIELD_PLAYER_NETWORK;
  *out = f;
  return TFP_FIELDS_OK;
}

/* The "Ready:" line's text: each field's offset as read and whether it is the game's own. */
static inline void tfp_fields_describe(const TfpEntityFields *f, char *out, size_t size) {
#define TFP_OWN(bit) ((f->moved & (bit)) ? "moved" : "the game's own")
  char records[48];
  if (f->records < 0) snprintf(records, sizeof records, "manager-0x%llx", (unsigned long long)-f->records);
  else snprintf(records, sizeof records, "manager+0x%llx", (unsigned long long)f->records);
  snprintf(out, size,
           "entity records %s (%s), network-id map +0x%x (%s), entity map +0x%x (%s); players: count +0x%x and +0x%x (%s), "
           "descriptors +0x%x (%s), network ids +0x%x (%s)",
           records, TFP_OWN(TFP_FIELD_RECORDS), (unsigned)f->network_map, TFP_OWN(TFP_FIELD_NETWORK_MAP), (unsigned)f->entity_map,
           TFP_OWN(TFP_FIELD_ENTITY_MAP), (unsigned)f->player_count, (unsigned)f->player_live, TFP_OWN(TFP_FIELD_PLAYER_COUNT),
           (unsigned)f->player_descriptors, TFP_OWN(TFP_FIELD_PLAYER_DESCRIPTORS), (unsigned)f->player_network,
           TFP_OWN(TFP_FIELD_PLAYER_NETWORK));
#undef TFP_OWN
}
#endif
