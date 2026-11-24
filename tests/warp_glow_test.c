/* The Warp Pack module (src/warp_glow.c) on mocked memory: a game image holding the module's fingerprints, the
 * displacement manager, the type table, the effect manager and its resource, laid out as the spec says
 * (docs/design/specs/2026-11-24-warp-pack-glow.md). The fake game's stop zeroes the event's runtime id, as
 * game+0x8abb20 does. */
#include "warp_glow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

/* ---- memory: a few regions --------------------------------------------------------------------------------------- */
typedef struct Region { uint64_t base; size_t size; uint8_t *bytes; } Region;
static Region regions[16];
static int region_count;
static uint64_t reads, writes, calls;
typedef struct Write { uint64_t at; uint32_t value; size_t size; } Write;
static Write written[64];
static int written_count;
typedef struct Call { int which; uint64_t address, a0, a1, a2; } Call;
static Call called[256];
static int called_count;
static bool unreadable_pins;

static void region(uint64_t base, size_t size) {
  regions[region_count++] = (Region){base, size, calloc(1, size)};
}
static uint8_t *at(uint64_t address, size_t n) {
  for (int i = 0; i < region_count; i++)
    if (address >= regions[i].base && address + n <= regions[i].base + regions[i].size) return regions[i].bytes + (address - regions[i].base);
  return NULL;
}
static void put(uint64_t address, const void *bytes, size_t n) {
  uint8_t *p = at(address, n);
  if (!p) { printf("fixture: no memory at %#llx\n", (unsigned long long)address); exit(2); }
  memcpy(p, bytes, n);
}
static void put32(uint64_t a, uint32_t v) { put(a, &v, 4); }
static void put64(uint64_t a, uint64_t v) { put(a, &v, 8); }
static void put8(uint64_t a, uint8_t v) { put(a, &v, 1); }
static uint32_t get32(uint64_t a) { uint32_t v; memcpy(&v, at(a, 4), 4); return v; }

/* ---- the fixture -------------------------------------------------------------------------------------------------- */
#define GAME  UINT64_C(0x140000000)
#define M     UINT64_C(0x200000000)   /* the displacement manager */
#define E     UINT64_C(0x210000000)   /* the effect manager */
#define WORLD UINT64_C(0x220000000)
#define HEAP  UINT64_C(0x230000000)
#define OWNER UINT64_C(0x240000000)   /* the avatar owner */
#define PACK  938u
#define INDEX 3u                      /* the pack's row in M */
#define EIDX  5u                      /* and in E */
#define AVATAR_INDEX 1u
/* heap layout */
#define M_BUCKETS     (HEAP + 0x0000)
#define M_DESCS       (HEAP + 0x1000)
#define M_ADS         (HEAP + 0x2000)
#define M_MODES       (HEAP + 0x6000)
#define M_CHARGE      (HEAP + 0x7000)
#define M_OVR_BUCKETS (HEAP + 0x7800)
#define DESCRIPTOR    (HEAP + 0x8000)
#define TYPE_TABLE    (HEAP + 0x9000)
#define E_BUCKETS     (HEAP + 0xa000)
#define E_DESCS       (HEAP + 0xb000)
#define E_RUNTIME     (HEAP + 0xc000)
#define RESOURCES     (HEAP + 0x10000)
#define SETTINGS      (TYPE_TABLE + 0x20 + 1 * 0x278)  /* the warp type's row 1 */
#define MODE_WORD     (M_MODES + INDEX * 24 + 0x10)
#define ADS_CACHE     (M_ADS + INDEX * 0x334 + 0x330)
#define BIT48         (OWNER + 0x53e886 + AVATAR_INDEX * 0x1238)
#define RESOURCE      (RESOURCES + 0x5600 + 2 * 0xf48)
static const uint32_t PLAY[3][5] = {{0x7f09d871, 0, 0, 0, 0}, {0x11110001, 0x11110002, 0, 0, 0}, {0x22220001, 0, 0, 0, 0}};
static const uint32_t STOP[3][5] = {{0x11110001, 0x11110002, 0x22220001, 0, 0}, {0x7f09d871, 0x22220001, 0, 0, 0}, {0x7f09d871, 0x11110001, 0x11110002, 0, 0}};
static const uint32_t SLOT_EVENTS[6] = {0x7f09d871, 0x11110001, 0x11110002, 0x22220001, 0x99990001, 0}; /* resource slots 0..5 */

static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10; }
static void pin_bytes(bool right) {
  for (size_t i = 0; i < tfp_warp_pin_count; i++) {
    const TfpWarpPin *p = tfp_warp_pins + i;
    size_t n = strlen(p->hex) / 2;
    for (size_t k = 0; k < n; k++) put8(GAME + p->rva + k, (uint8_t)(hex_digit(p->hex[2 * k]) * 16 + hex_digit(p->hex[2 * k + 1])));
    if (!right && !strcmp(p->name, "ads_cache")) put8(GAME + p->rva + 3, 0x31); /* +0x331: another build */
  }
}
/* An entity map: buckets {key, index} with multiplier 1, capacity 16, empty key 0xffffffff. */
static void map_header(uint64_t header, uint64_t buckets) {
  put64(header, buckets); put32(header + 8, 16); put32(header + 12, 0xffffffff); put32(header + 16, 1);
  for (int i = 0; i < 16; i++) put32(buckets + i * 8, 0xffffffff), put32(buckets + i * 8 + 4, 0xffffffff);
}
static void map_put(uint64_t buckets, uint32_t key, uint32_t index) {
  uint32_t slot = key & 15;
  while (get32(buckets + slot * 8) != 0xffffffff) slot = (slot + 1) & 15;
  put32(buckets + slot * 8, key); put32(buckets + slot * 8 + 4, index);
}
static void map_clear(uint64_t buckets) { for (int i = 0; i < 16; i++) put32(buckets + i * 8, 0xffffffff), put32(buckets + i * 8 + 4, 0xffffffff); }
static void fixture(void) {
  for (int i = 0; i < region_count; i++) free(regions[i].bytes);
  region_count = 0;
  region(GAME + 0x4fc000, 0x3b2000);   /* code: 0x4fc000 .. 0x8ae000 */
  region(GAME + 0x3326000, 0x1000);    /* globals */
  region(GAME + 0x346b000, 0x1000);
  region(M, 0x1000); region(E, 0x1000); region(WORLD, 0xf13000); region(HEAP, 0x20000); region(OWNER, 0x541000);
  pin_bytes(true);
  put64(GAME + 0x3326ac8, M); put64(GAME + 0x3326570, E); put64(GAME + 0x346bf98, WORLD);
  /* M */
  put32(M + 0x10, 6);
  map_header(M + 0x20, M_BUCKETS); map_put(M_BUCKETS, 17, 0); map_put(M_BUCKETS, PACK, INDEX);
  put64(M + 0x38, M_DESCS); put64(M_DESCS + INDEX * 8, DESCRIPTOR);
  put64(M + 0x40, M_ADS); put64(M + 0x48, M_MODES); put64(M + 0x50, M_CHARGE);
  map_header(M + 0x60, M_OVR_BUCKETS);  /* no override */
  put64(M + 0xa0, HEAP + 0x18000);
  put64(DESCRIPTOR, TFP_WARP_TYPE); put32(DESCRIPTOR + 8, PACK); put32(DESCRIPTOR + 12, 0x4000d1);
  put32(MODE_WORD, 1);
  float charge = .25f; put(M_CHARGE + INDEX * 4, &charge, 4);
  /* the type table: two buckets, the warp type's at slot type & 1 = 0 is taken by another type, so it sits at 1 */
  put64(WORLD + 0xf12d08, TYPE_TABLE);
  put64(TYPE_TABLE, UINT64_C(0x1234567800000000)); put32(TYPE_TABLE + 8, 0);
  put64(TYPE_TABLE + 16, TFP_WARP_TYPE); put32(TYPE_TABLE + 24, 1);
  for (int m = 0; m < 3; m++) {
    uint64_t row = SETTINGS + 0x1c0 + m * 0x34;
    put32(row, (uint32_t)m);
    for (int k = 0; k < 5; k++) put32(row + 4 + k * 4, PLAY[m][k]), put32(row + 0x18 + k * 4, STOP[m][k]);
  }
  /* E */
  map_header(E + 0x20, E_BUCKETS); map_put(E_BUCKETS, PACK, EIDX);
  put64(E + 0x38, E_DESCS); put64(E_DESCS + EIDX * 8, DESCRIPTOR);
  put64(E + 0x48, E_RUNTIME);
  /* the resource table: key % 0x560 */
  put64(WORLD + 0xf127b8, RESOURCES);
  uint32_t slot = (uint32_t)(TFP_WARP_TYPE % 0x560);
  put64(RESOURCES + slot * 16, TFP_WARP_TYPE); put32(RESOURCES + slot * 16 + 8, 2);
  for (int s = 0; s < 6; s++) put32(RESOURCE + 0x38 + s * 0x50, SLOT_EVENTS[s]);
  /* mode 1 plays: its two play events have live runtime ids (slots 1 and 2); slot 4 plays too, not in the table */
  put32(E_RUNTIME + EIDX * 0x218 + 1 * 4, 501); put32(E_RUNTIME + EIDX * 0x218 + 2 * 4, 502); put32(E_RUNTIME + EIDX * 0x218 + 4 * 4, 504);
  put8(BIT48, 0x00); put8(ADS_CACHE, 0);
  written_count = 0, called_count = 0, unreadable_pins = false; /* reads, writes and calls count on across fixtures */
}
static void play_mode(int mode) { /* what the game's updater does on a mode change, not ADS: stop the stop list, play the play list */
  static const int slot_of[] = {0, 1, 2, 3};
  for (int s = 0; s < 4; s++) {
    uint32_t id = SLOT_EVENTS[slot_of[s]];
    bool play = false;
    for (int k = 0; k < 5; k++) play = play || PLAY[mode][k] == id;
    put32(E_RUNTIME + EIDX * 0x218 + s * 4, play ? 600 + (uint32_t)s : 0);
  }
  put32(MODE_WORD, (uint32_t)mode);
}

/* ---- the host ---------------------------------------------------------------------------------------------------- */
static uint64_t host_module(void *u, TfpOpsModule which) { (void)u; return which == TFP_OPS_GAME ? GAME : 0; }
static bool host_read(void *u, uint64_t address, void *out, size_t n) {
  (void)u; reads++;
  if (unreadable_pins && address >= GAME + 0x4fc000 && address < GAME + 0x8ae000) return false;
  uint8_t *p = at(address, n);
  if (!p) return false;
  memcpy(out, p, n);
  return true;
}
static bool host_write(void *u, uint64_t address, const void *bytes, size_t n) {
  (void)u; writes++;
  uint8_t *p = at(address, n);
  if (!p) return false;
  if (written_count < 64) { written[written_count].at = address, written[written_count].size = n; memcpy(&written[written_count].value, bytes, n < 4 ? n : 4); written_count++; }
  memcpy(p, bytes, n);
  return true;
}
static bool host_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  (void)u; (void)raised; calls++;
  if (called_count < 256) called[called_count++] = (Call){(int)which, address, args[0], args[1], args[2]};
  if (result) *result = 0;
  return true;
}
static TfpOpsHost host;

static TfpWarpIn in_pack(bool owns, bool armed, double now) {
  TfpWarpIn in; memset(&in, 0, sizeof in);
  in.owns = owns, in.armed = armed, in.player = true, in.avatar_owner = OWNER, in.avatar_index = AVATAR_INDEX;
  in.backpack = 1, in.pack = PACK, in.now = now;
  uint64_t type = TFP_WARP_TYPE; memcpy(in.pack_type, &type, 8);
  return in;
}
static char text[2000];
static bool has(const char *s) { if (getenv("WARP_SHOW")) printf("%s---\n", text); bool ok = strstr(text, s) != NULL; if (!ok) printf("  not in: %s\n  wanted: %s\n", text, s); return ok; }

static void test_log(void) {
  TfpWarp w; tfp_warp_init(&w);
  fixture();
  /* No backpack: one line when that becomes so, none after. */
  TfpWarpIn in = in_pack(false, false, 10); in.backpack = 0;
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("Warp: no Warp Pack (backpack none)"));
  CHECK(!tfp_warp_tick(&w, &host, &in, text, sizeof text));
  in.now = 20;
  CHECK(!tfp_warp_tick(&w, &host, &in, text, sizeof text));
  /* Another backpack: said with its type. */
  in = in_pack(false, false, 21); in.pack_type[0] ^= 1;
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("the backpack is not a Warp Pack (entity 938, type b49756795d9b0e69)"));
  /* The Warp Pack: the settings line once, then the Warp line, with what the game holds. */
  in = in_pack(false, false, 22);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text));
  CHECK(has("Warp: pack 938: displacement row 3 of 6, settings the type's; mode table: mode 0 (tag 0) play [7f09d871 00000000 00000000 00000000 00000000] stop [11110001 11110002 22220001 00000000 00000000];"));
  CHECK(has("mode 1 (tag 1) play [11110001 11110002 00000000 00000000 00000000] stop [7f09d871 22220001 00000000 00000000 00000000];"));
  CHECK(has(" 4 distinct ids\nWarp: pack 938 (row 3) bit48=0 ads_cache=0 mode_word=0x1 charge=0.250 effects=held playing [11110001 11110002] of 4 ids; code ok; view not owned, switch off; did: watching"));
  CHECK(w.read.ok && w.read.playing_count == 2);
  CHECK(!strstr(text, "99990001")); /* slot 4 plays, but it is not the table's */
  /* Nothing changed: no line until a second has passed; then the Warp line alone. */
  in.now = 22.5;
  CHECK(!tfp_warp_tick(&w, &host, &in, text, sizeof text));
  in.now = 23.01;
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && !strstr(text, "mode table") && has("Warp: pack 938 (row 3) bit48=0"));
  /* A change is a line at once: bit 48, the ADS cache, the mode word, the playing set. */
  in.now = 23.1; put8(BIT48, 1);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("bit48=1 ads_cache=0"));
  in.now = 23.2; put8(ADS_CACHE, 1);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("bit48=1 ads_cache=1"));
  in.now = 23.3; put8(BIT48, 0); put8(ADS_CACHE, 0); play_mode(2);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("bit48=0 ads_cache=0 mode_word=0x2") && has("playing [22220001]"));
  /* The pack is not in the effect manager: said. */
  in.now = 23.4; map_clear(E_BUCKETS);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("effects=none playing []"));
  /* A fingerprint differs: said, once per game base. */
  fixture(); tfp_warp_init(&w); pin_bytes(false);
  in = in_pack(false, false, 30);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("code ads_cache at game+0x88c637 differs"));
  /* The pack is not the manager's: refused, with the reason. */
  fixture(); tfp_warp_init(&w); map_clear(M_BUCKETS);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("did: refused: the displacement manager does not hold the pack"));
  /* The settings override (M+0xa0) wins over the type table. */
  fixture(); tfp_warp_init(&w); map_put(M_OVR_BUCKETS, PACK, 0);
  CHECK(tfp_warp_tick(&w, &host, &in, text, sizeof text) && has("settings its own (M+0xa0)") && w.read.settings == HEAP + 0x18000);
  /* Reading only: no write and no call, ever, in all of the above. */
  CHECK(writes == 0 && calls == 0);
}

int main(void) {
  host.module = host_module, host.read = host_read, host.write = host_write, host.call = host_call;
  test_log();
  if (failed) { printf("warp glow: %d of %d checks failed\n", failed, checks); return 1; }
  printf("warp glow: %d checks: the Warp: line on mocked memory (the pack's row, settings, mode table, playing events, code)\n", checks);
  return 0;
}
