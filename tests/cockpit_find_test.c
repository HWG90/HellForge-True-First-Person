/* Round 38: src/cockpit_find.h on a made-up world: a body seated in one suit, another suit nearby, a suit that is
 * something else (an FRV's or a gun's resource, linked or at the seat: never taken), dead references, other worlds, a
 * registry slot that changes while it is read, and memory that cannot be read. */
#include "cockpit_find.h"
#include <stdio.h>
#include <stdlib.h>

static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

/* Memory: 64 KiB pages at fixed places; anything else cannot be read. */
#define PAGES 64
static struct { uint64_t base; uint8_t bytes[65536]; } page[PAGES];
static int pages;
static uint64_t hole_lo, hole_hi;   /* a range that cannot be read */
/* A registry slot whose pointer changes between two reads (a unit freed and its slot reused while it is looked up):
 * from the second read of `moving_slot` on, it reads `moving_to`. */
static uint64_t moving_slot, moving_to; static int moving_reads;
static uint8_t *at(uint64_t address, size_t n) {
  for (int i = 0; i < pages; ++i) if (address >= page[i].base && address + n <= page[i].base + 65536) return page[i].bytes + (address - page[i].base);
  return NULL;
}
static bool read_fn(void *user, uint64_t address, void *out, size_t n) {
  (void)user;
  if (address < hole_hi && address + n > hole_lo) return false;
  uint8_t *p = at(address, n);
  if (!p) return false;
  memcpy(out, p, n);
  if (moving_slot && address == moving_slot && n == 8 && ++moving_reads >= 2) memcpy(out, &moving_to, 8);
  return true;
}
static uint64_t map(void) { page[pages].base = UINT64_C(0x100000000) + (uint64_t)pages * 0x100000; memset(page[pages].bytes, 0, 65536); return page[pages++].base; }
static void put(uint64_t address, const void *v, size_t n) { memcpy(at(address, n), v, n); }
static void put64(uint64_t a, uint64_t v) { put(a, &v, 8); }
static void put32(uint64_t a, uint32_t v) { put(a, &v, 4); }

static TfpCockpitMemory mem;
static uint64_t registry_units, registry_generations;
static const uint64_t WORLD = UINT64_C(0x7000000000), OTHER_WORLD = UINT64_C(0x7000001000);
/* A unit: registry slot `index`, generation 1, with `nodes` nodes, node `seat` named attach_driver at `seat_at`. */
static uint32_t unit(uint32_t index, const uint8_t *resource, uint64_t world, uint64_t parent, const float *root, int seat, const float *seat_at, uint64_t *object_out) {
  const uint64_t object = map(), graph = map();
  const uint32_t ref = index + 0x400000u;
  put32(object + 8, ref); put64(object + 0x18, world); put64(object + 0x1D0, parent);
  if (resource) put(object + 0x358, resource, 8);
  const uint32_t nodes = 8;
  put32(object + 0x70, nodes); put64(object + 0x88, graph); put64(object + 0xA0, graph + 0x8000);
  for (uint32_t i = 0; i < nodes; ++i) {
    put32(graph + 0x8000 + i*4, (int)i == seat ? TFP_COCKPIT_ATTACH_HASH : 0x1000 + i);
    put(graph + i*64 + 0x30, i == 0 ? root : (int)i == seat ? seat_at : root, 12);
  }
  put64(registry_units + (uint64_t)index*8, object);
  uint8_t one = 1; put(registry_generations + index, &one, 1);
  if (object_out) *object_out = object;
  return ref;
}

int main(void) {
  registry_units = map(); registry_generations = map();
  mem = (TfpCockpitMemory){read_fn, NULL, registry_units, registry_generations, 4096};
  static const uint8_t other[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const float body_root[3] = {10, 20, 2.6f}, here[3] = {10, 20.2f, 2.7f}, there[3] = {16, 20, 2.7f}, zero[3] = {0, 0, 0};
  uint64_t suit_object = 0, near_object = 0;
  const uint32_t body = unit(1, NULL, WORLD, 0, body_root, -1, zero, NULL);
  const uint32_t suit = unit(2, TFP_COCKPIT_RESOURCE, WORLD, 0, (const float[]){10, 20.3f, 0}, 5, here, &suit_object);   /* the seat 0.22 m from the root */
  const uint32_t far = unit(3, TFP_COCKPIT_RESOURCE, WORLD, 0, (const float[]){16, 20, 0}, 5, there, NULL);             /* another player's, 6 m away */
  const uint32_t gun = unit(4, other, WORLD, 0, body_root, 5, here, NULL);                                             /* not a suit, at the seat */
  const uint32_t elsewhere = unit(5, TFP_COCKPIT_RESOURCE, OTHER_WORLD, 0, body_root, 5, here, NULL);                  /* a suit in another world */
  const uint32_t seatless = unit(6, TFP_COCKPIT_RESOURCE, WORLD, 0, body_root, -1, here, NULL);                       /* no attach_driver node */
  uint32_t refs[] = {body, gun, far, elsewhere, seatless, suit, 0x400000u + 999, UINT32_MAX};

  TfpCockpitBody b;
  CHECK(tfp_cockpit_body(&mem, body, &b) && b.object && b.world == WORLD && b.parent == 0 && b.root[1] == 20);
  TfpCockpitFound f;
  /* 1. found near: one suit passes, the rest are read and set aside */
  CHECK(tfp_cockpit_search(&mem, &b, refs, sizeof refs / sizeof *refs, &f) && f.ref == suit && f.object == suit_object && f.how == TFP_COCKPIT_NEAR
        && fabsf(f.distance - .2236f) < .001f && f.suits == 4 && f.looked == 5);
  CHECK(tfp_cockpit_check(&mem, &b, suit, &f) && f.how == TFP_COCKPIT_NEAR);
  CHECK(!tfp_cockpit_check(&mem, &b, far, &f) && !tfp_cockpit_check(&mem, &b, gun, &f) && !tfp_cockpit_check(&mem, &b, elsewhere, &f)
        && !tfp_cockpit_check(&mem, &b, seatless, &f) && !tfp_cockpit_check(&mem, &b, body, &f) && !tfp_cockpit_check(&mem, &b, 0, &f));
  /* 2. linked: the body's parent is the suit; the link alone is enough, even with the seat node far */
  uint64_t far_object = 0; CHECK(tfp_cockpit_object(&mem, far, &far_object));
  put64(b.object + 0x1D0, far_object);
  CHECK(tfp_cockpit_body(&mem, body, &b) && b.parent == far_object);
  CHECK(tfp_cockpit_search(&mem, &b, refs, sizeof refs / sizeof *refs, &f) && f.ref == far && f.how == TFP_COCKPIT_LINKED && f.looked == 1);
  /* a link to something that is not a suit: the search goes on and finds the one by its seat */
  uint64_t gun_object = 0; CHECK(tfp_cockpit_object(&mem, gun, &gun_object));
  put64(b.object + 0x1D0, gun_object);
  CHECK(tfp_cockpit_body(&mem, body, &b));
  CHECK(tfp_cockpit_search(&mem, &b, refs, sizeof refs / sizeof *refs, &f) && f.ref == suit && f.how == TFP_COCKPIT_NEAR);
  put64(b.object + 0x1D0, 0);
  CHECK(tfp_cockpit_body(&mem, body, &b));
  /* 3. two suits at the seat: none is taken */
  const uint32_t twin = unit(7, TFP_COCKPIT_RESOURCE, WORLD, 0, body_root, 3, here, &near_object);
  uint32_t two[] = {suit, twin};
  CHECK(!tfp_cockpit_search(&mem, &b, two, 2, &f) && f.ref == 0 && f.how == TFP_COCKPIT_NONE && f.suits == 2);
  /* 4. the suit's reference retired (a new generation in its slot): not found, not checked */
  uint8_t two_gen = 2; put(registry_generations + 2, &two_gen, 1);
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f));
  uint32_t only[] = {suit};
  CHECK(!tfp_cockpit_search(&mem, &b, only, 1, &f) && f.looked == 0);
  uint8_t one_gen = 1; put(registry_generations + 2, &one_gen, 1);
  CHECK(tfp_cockpit_check(&mem, &b, suit, &f));
  /* the object no longer names itself */
  put32(suit_object + 8, 0x400000u + 77);
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f));
  put32(suit_object + 8, suit);
  /* the slot's pointer moved between the two reads of it (the generation the same): refused; read the same twice, found */
  uint64_t twin_object = 0; CHECK(tfp_cockpit_object(&mem, twin, &twin_object));
  moving_slot = registry_units + 2*8; moving_to = twin_object; moving_reads = 0;
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f) && moving_reads >= 2);
  moving_to = suit_object; moving_reads = 0;
  CHECK(tfp_cockpit_check(&mem, &b, suit, &f) && moving_reads >= 2);
  moving_slot = 0;
  /* 5. the suit walked away from the body (the seat 1.5 m off): no longer the one */
  uint64_t suit_graph = 0; CHECK(read_fn(NULL, suit_object + 0x88, &suit_graph, 8));
  const float away[3] = {10, 21.5f, 2.7f};
  put(suit_graph + 5*64 + 0x30, away, 12);
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f));
  put(suit_graph + 5*64 + 0x30, here, 12);
  /* 6. memory that cannot be read: refused, never a fault */
  hole_lo = suit_object + 0x358; hole_hi = hole_lo + 8;
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f));
  hole_lo = suit_graph; hole_hi = suit_graph + 0x10000;
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f));   /* no seat node to measure, no link */
  hole_lo = hole_hi = 0;
  const float bad[3] = {NAN, 0, 0};
  put(suit_graph + 5*64 + 0x30, bad, 12);
  CHECK(!tfp_cockpit_check(&mem, &b, suit, &f));
  put(suit_graph + 5*64 + 0x30, here, 12);
  /* a body that cannot be read, or has too many nodes */
  put32(b.object + 0x70, 5000);
  CHECK(!tfp_cockpit_body(&mem, body, &b));
  put32(b.object + 0x70, 8);
  CHECK(tfp_cockpit_body(&mem, body, &b) && tfp_cockpit_check(&mem, &b, suit, &f));
  /* a reference past the registry */
  CHECK(!tfp_cockpit_check(&mem, &b, 0x400000u + 5000, &f));
  printf("cockpit find: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
