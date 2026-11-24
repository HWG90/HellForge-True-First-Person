/* See warp_glow.h. Addresses and layouts: docs/design/specs/2026-11-24-warp-pack-glow.md. */
#include "warp_glow.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOW UINT64_C(65536)
#define HIGH UINT64_C(0x800000000000)

/* ---- layouts (the spec, section 2) ---------------------------------------------------------------------------- */
#define M_COUNT          0x10
#define M_MAP            0x20   /* buckets, +8 capacity, +0xc empty key, +0x10 multiplier */
#define M_DESCRIPTORS    0x38
#define M_ADS_ROWS       0x40
#define M_ADS_STRIDE     0x334
#define M_ADS_CACHE      0x330
#define M_MODE_ROWS      0x48
#define M_MODE_STRIDE    24
#define M_MODE_WORD      0x10
#define M_CHARGE         0x50
#define M_OVERRIDE_MAP   0x60
#define M_OVERRIDE_ROWS  0xa0
#define SETTINGS_STRIDE  0x278
#define W_SETTINGS_TABLE UINT32_C(0xf12d08) /* world + this: two 16-byte buckets {type, index}, rows at +0x20 */
#define SETTINGS_MODES   0x1c0
#define MODE_STRIDE      0x34
/* the entity effect manager (the spec, section 3) */
#define E_MAP            0x20
#define E_DESCRIPTORS    0x38
#define E_RUNTIME        0x48
#define E_RUNTIME_STRIDE 0x218
#define W_RESOURCES      UINT32_C(0xf127b8) /* world + this: 0x560 buckets {key, index}, resources at +0x5600, stride 0xf48 */
#define RESOURCE_BUCKETS 0x560
#define RESOURCE_BASE    0x5600
#define RESOURCE_STRIDE  0xf48
#define RESOURCE_EVENTS  0x38               /* 32 slots, stride 0x50: the event id */
#define RESOURCE_SLOT    0x50

const TfpWarpPin tfp_warp_pins[] = {
  {"stop", 0x8abb20, "48895c241848896c242056574154415641574883ec4033ed418bf13b15df80bd02418bf88bda4c8bf9744f"},
  {"stop_replicate", 0x8abc6c, "83bc2490000000017557"},
  {"playing", 0x8abf40, "48895c240848896c2410488974241857415641574883ec203b15c27cbd02418bd8488b3508a6a702"},
  {"playing_scan", 0x8abfda, "8be8488b46384c8b34e8418b4e0c85c974c6488b0515a3a702488b5018488b8220070000ffd084c074ae498b0ee89403c5ff"
                             "4869cd1802000048034e484883c0380f1f4400003918740dffc74883c05083ff2072f1eb818b04b9e9"},
  {"resource", 0x4fc3a0, "4885c97471488b05ecfbf602448bc14c8b90b827f10048b8bfa02fe80bfa82be48f7e148c1ea0a69c260050000442bc04533c9"},
  {"resource_rows", 0x4fc419, "4885c074f88b48084869c1480f00004805005600004903c2c3"},
  {"bit48", 0x88c614, "b8ffffffff8bc84869c138120000420fb6ac1886e853004080e5014088ac24c8000000"},
  {"ads_cache", 0x88c637, "4138af300300000f84870000004084ed0f84a4000000"},
  {"ads_stops", 0x88c64d, "4c8b7c244041bc030000004981c7d801000090498bf7498bdf498bed0f1f8000000000448b03418b5608e8c4f8010085c07423"
                          "448b064533c9418b5608488b0ddf9ea902c644242800c744242000000000e87df401004883c3044883c6044883ed0175bf"},
  {"mode_cache", 0x88c817, "488b442448488b8c24d800000044394cc11075094584e40f8442010000488b5c24404533e444894cc1104881c3c0010000418bc40f1f440000"},
  {"mode_stops", 0x88c870, "448b441f18418b5608e8c2f6010085c07422448b441e18458bcc418b5608488b0ddb9ca90244886424284489642420e87cf20100"},
  {"frame_loop", 0x88a7a0, "488b42384b8d3c7f488b72484a8b1cf8488bcbe88852c8ff8b53084c8bf0488b4da8e869190000"},
  {"manager", 0x88a5fb, "488b3dc6c4a902"},
  {"settings", 0x50fa40, "4883ec284c8bd14885c9750733c04883c428c38b41083b05c841f7020f84930000004c8b1d5f70e1024533c048895c243048896c24384889742440458b4b68"},
  {"settings_rows", 0x50fad6, "3901751b8b410483f8ff74134869c078020000490383a00000004883c428c3498b0a4883c428e9aff9ffff"},
  {"settings_type", 0x50f4b0, "4885c9744c488b05dccaf502448bc14183e0014533c94c8b90082df1000f1f00418bc048c1e0044903c2488b10483bd174224885d2741a"
                              "418bc0418d50014533c041ffc183f801440f45c24183f90272cf33c0c34885c074f88b40084869c878020000498d42204803c1c3"},
};
const size_t tfp_warp_pin_count = sizeof tfp_warp_pins / sizeof *tfp_warp_pins;

/* ---- reads -------------------------------------------------------------------------------------------------------- */
typedef struct Ctx { TfpWarp *w; const TfpOpsHost *h; uint64_t game; } Ctx;
static bool why(TfpWarp *w, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(w->why, sizeof w->why, format, args);
  va_end(args);
  return false;
}
static bool rd(Ctx *c, uint64_t at, void *out, size_t n, const char *what) {
  if (at < LOW || at + n >= HIGH || !c->h->read(c->h->user, at, out, n)) return why(c->w, "%s unreadable at %#llx", what, (unsigned long long)at);
  return true;
}
static bool ptr(Ctx *c, uint64_t at, uint64_t *out, const char *what) {
  if (!rd(c, at, out, 8, what)) return false;
  if (*out < LOW || *out >= HIGH) return why(c->w, "%s is not a pointer (%#llx)", what, (unsigned long long)*out);
  return true;
}
/* The game's entity hash map (buckets {key, index}, slot = (n + key * multiplier) & (capacity - 1), ends at the empty
 * key). 1 found, 0 absent, -1 unreadable. */
static int lookup(Ctx *c, uint64_t at, uint32_t key, uint32_t *index, const char *what) {
  uint64_t buckets;
  uint32_t head[3];
  if (!ptr(c, at, &buckets, what) || !rd(c, at + 8, head, sizeof head, what)) return -1;
  uint32_t capacity = head[0], empty = head[1], multiplier = head[2];
  if (!capacity || capacity > (1u << 20) || (capacity & (capacity - 1))) return why(c->w, "%s capacity %u", what, capacity), -1;
  if (key == empty) return 0;
  uint32_t probes = capacity < 4096 ? capacity : 4096;
  for (uint32_t n = 0; n < probes; n++) {
    uint32_t row[2], slot = (n + key * multiplier) & (capacity - 1);
    if (!rd(c, buckets + (uint64_t)slot * 8, row, sizeof row, what)) return -1;
    if (row[0] == empty) return 0;
    if (row[0] == key) {
      if (row[1] == UINT32_MAX) return 0;
      *index = row[1];
      return 1;
    }
  }
  return 0;
}
static int hex_digit(char ch) { return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 0; }
static bool pins(Ctx *c) {
  if (c->w->pinned_base == c->game) return c->w->pins_ok;
  c->w->pinned_base = c->game, c->w->pins_ok = false, c->w->pin_failed[0] = 0;
  for (size_t i = 0; i < tfp_warp_pin_count; i++) {
    const TfpWarpPin *p = tfp_warp_pins + i;
    uint8_t want[128], have[128];
    size_t n = strlen(p->hex) / 2;
    for (size_t k = 0; k < n; k++) want[k] = (uint8_t)(hex_digit(p->hex[2 * k]) * 16 + hex_digit(p->hex[2 * k + 1]));
    if (!c->h->read(c->h->user, c->game + p->rva, have, n)) {
      snprintf(c->w->pin_failed, sizeof c->w->pin_failed, "%s at game+%#x is unreadable", p->name, (unsigned)p->rva);
      return false;
    }
    if (memcmp(want, have, n)) {
      snprintf(c->w->pin_failed, sizeof c->w->pin_failed, "%s at game+%#x differs", p->name, (unsigned)p->rva);
      return false;
    }
  }
  return c->w->pins_ok = true;
}
/* The settings row of a descriptor, as game+0x50fa40 finds it. */
static bool settings_of(Ctx *c, uint64_t manager, uint64_t descriptor, uint32_t entity, uint64_t type, TfpWarpRead *r) {
  uint32_t index;
  int found = lookup(c, manager + M_OVERRIDE_MAP, entity, &index, "displacement override map");
  if (found < 0) return false;
  if (found) {
    uint64_t rows;
    if (!ptr(c, manager + M_OVERRIDE_ROWS, &rows, "displacement override rows")) return false;
    r->settings = rows + (uint64_t)index * SETTINGS_STRIDE, r->override_settings = true;
    return true;
  }
  uint64_t world, table;
  if (!ptr(c, c->game + TFP_WARP_G_WORLD, &world, "world") || !ptr(c, world + W_SETTINGS_TABLE, &table, "displacement type table")) return false;
  for (uint32_t n = 0, slot = (uint32_t)(type & 1); n < 2; n++, slot = slot == 1 ? 0 : slot + 1) {
    struct { uint64_t key; uint32_t index, pad; } bucket;
    if (!rd(c, table + slot * 16u, &bucket, sizeof bucket, "displacement type table")) return false;
    if (bucket.key == type) {
      if (bucket.index > 64) return why(c->w, "displacement type row %u", bucket.index);
      r->settings = table + 0x20 + (uint64_t)bucket.index * SETTINGS_STRIDE, r->override_settings = false;
      return true;
    }
    if (!bucket.key) break;
  }
  (void)descriptor;
  return why(c->w, "the Warp Pack's displacement settings are not in the type table");
}
static void add_id(TfpWarpRead *r, uint32_t id) {
  if (!id || id == UINT32_MAX) return;
  for (unsigned i = 0; i < r->id_count; i++) if (r->ids[i] == id) return;
  if (r->id_count < 30) r->ids[r->id_count++] = id;
}
/* Which of the table's ids play, the way game+0x8abf40 answers (the first slot of the pack's effect resource with that
 * id; its runtime id nonzero), by memory. */
static bool playing(Ctx *c, uint32_t entity, uint64_t type, TfpWarpRead *r) {
  uint32_t index, events[32], ids[32];
  uint64_t descriptor, runtime, world, table;
  r->playing_count = 0, r->in_effects = false;
  if (!ptr(c, c->game + TFP_WARP_G_EFFECTS, &r->effects, "effect manager")) return false;
  int found = lookup(c, r->effects + E_MAP, entity, &index, "effect map");
  if (found <= 0) return found == 0;
  if (!ptr(c, r->effects + E_DESCRIPTORS, &descriptor, "effect descriptors") || !ptr(c, descriptor + (uint64_t)index * 8, &descriptor, "effect descriptor"))
    return false;
  uint64_t key;
  uint32_t again;
  if (!rd(c, descriptor, &key, 8, "effect descriptor") || !rd(c, descriptor + 8, &again, 4, "effect descriptor")) return false;
  if (again != entity || key != type) return why(c->w, "the effect manager's row %u is another entity's (%u)", index, again);
  r->in_effects = true;
  if (!ptr(c, r->effects + E_RUNTIME, &runtime, "effect runtime") || !rd(c, runtime + (uint64_t)index * E_RUNTIME_STRIDE, ids, sizeof ids, "effect runtime"))
    return false;
  if (!ptr(c, c->game + TFP_WARP_G_WORLD, &world, "world") || !ptr(c, world + W_RESOURCES, &table, "effect resources")) return false;
  uint64_t resource = 0;
  for (uint32_t n = 0, slot = (uint32_t)(key % RESOURCE_BUCKETS); n < RESOURCE_BUCKETS; n++, slot = slot == RESOURCE_BUCKETS - 1 ? 0 : slot + 1) {
    struct { uint64_t key; uint32_t index, pad; } bucket;
    if (!rd(c, table + (uint64_t)slot * 16, &bucket, sizeof bucket, "effect resources")) return false;
    if (bucket.key == key) { resource = table + RESOURCE_BASE + (uint64_t)bucket.index * RESOURCE_STRIDE; break; }
    if (!bucket.key) break;
  }
  if (!resource) return why(c->w, "the pack's effect resource is not in the table");
  for (int s = 0; s < 32; s++)
    if (!rd(c, resource + RESOURCE_EVENTS + (uint64_t)s * RESOURCE_SLOT, events + s, 4, "effect resource")) return false;
  for (unsigned i = 0; i < r->id_count; i++)
    for (int s = 0; s < 32; s++)
      if (events[s] == r->ids[i]) {
        if (ids[s]) r->playing[r->playing_count++] = r->ids[i];
        break;
      }
  return true;
}
/* Everything the tick needs of the pack. false: w->why. */
static bool read_pack(Ctx *c, const TfpWarpIn *in, uint64_t type, TfpWarpRead *r) {
  uint64_t descriptors, rows;
  uint64_t key;
  uint32_t entity;
  memset(r, 0, sizeof *r);
  if (!ptr(c, c->game + TFP_WARP_G_MANAGER, &r->manager, "displacement manager")) return false;
  int found = lookup(c, r->manager + M_MAP, in->pack, &r->index, "displacement map");
  if (found < 0) return false;
  if (!found) return why(c->w, "the displacement manager does not hold the pack");
  if (!rd(c, r->manager + M_COUNT, &r->count, 4, "displacement count")) return false;
  if (r->index >= r->count || r->count > 4096) return why(c->w, "displacement row %u of %u", r->index, r->count);
  if (!ptr(c, r->manager + M_DESCRIPTORS, &descriptors, "displacement descriptors") ||
      !ptr(c, descriptors + (uint64_t)r->index * 8, &r->descriptor, "displacement descriptor") ||
      !rd(c, r->descriptor, &key, 8, "displacement descriptor") || !rd(c, r->descriptor + 8, &entity, 4, "displacement descriptor"))
    return false;
  if (entity != in->pack || key != type) return why(c->w, "the displacement row %u is not the pack's (entity %u)", r->index, entity);
  if (!settings_of(c, r->manager, r->descriptor, entity, key, r)) return false;
  if (!rd(c, r->settings + SETTINGS_MODES, r->modes, sizeof r->modes, "mode table")) return false;
  for (int m = 0; m < 3; m++) for (int k = 0; k < 5; k++) add_id(r, r->modes[m].stop[k]);
  for (int m = 0; m < 3; m++) for (int k = 0; k < 5; k++) add_id(r, r->modes[m].play[k]);
  uint8_t flags;
  if (in->avatar_index >= 8 || !rd(c, in->avatar_owner + TFP_WARP_AVATAR_BIT48 + (uint64_t)in->avatar_index * TFP_WARP_AVATAR_STRIDE, &flags, 1, "avatar flags"))
    return in->avatar_index >= 8 ? why(c->w, "avatar index %u", in->avatar_index) : false;
  r->bit48 = flags & 1;
  if (!ptr(c, r->manager + M_ADS_ROWS, &rows, "ADS rows") || !rd(c, rows + (uint64_t)r->index * M_ADS_STRIDE + M_ADS_CACHE, &r->ads_cache, 1, "ADS cache")) return false;
  if (!ptr(c, r->manager + M_MODE_ROWS, &rows, "mode rows") || !rd(c, rows + (uint64_t)r->index * M_MODE_STRIDE + M_MODE_WORD, &r->mode_word, 4, "mode word"))
    return false;
  if (!ptr(c, r->manager + M_CHARGE, &rows, "charge") || !rd(c, rows + (uint64_t)r->index * 4, &r->charge, 4, "charge")) return false;
  if (!playing(c, in->pack, key, r)) return false;
  return r->ok = true;
}

/* ---- the log ------------------------------------------------------------------------------------------------------ */
static size_t ids_text(char *out, size_t size, const uint32_t *ids, unsigned n) {
  size_t used = 0;
  if (size) out[0] = 0;
  for (unsigned i = 0; i < n && used + 1 < size; i++) {
    int k = snprintf(out + used, size - used, i ? " %08x" : "%08x", ids[i]);
    if (k < 0) break;
    used += (size_t)k < size - used ? (size_t)k : size - used - 1;
  }
  return used;
}
static uint64_t mix(uint64_t h, const void *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * UINT64_C(0x100000001b3);
  return h;
}
static const char *did_name(TfpWarpDid d) {
  static const char *const names[] = {"nothing", "watching (nothing is stopped)", "not owned (the view is not TFP's: nothing is stopped)",
                                      "the game's full ADS (bit 48 set: the game stops them itself)", "quiet (nothing of the stop lists plays)",
                                      "stopped", "restored", "refused"};
  return names[d];
}
static void append(char *text, size_t size, size_t *used, const char *format, ...) {
  va_list args;
  if (*used + 1 >= size) return;
  va_start(args, format);
  int k = vsnprintf(text + *used, size - *used, format, args);
  va_end(args);
  if (k > 0) *used += (size_t)k < size - *used ? (size_t)k : size - *used - 1;
}

void tfp_warp_init(TfpWarp *w) { memset(w, 0, sizeof *w); w->last_line = -1e300; }

bool tfp_warp_tick(TfpWarp *w, const TfpOpsHost *host, const TfpWarpIn *in, char *text, size_t size) {
  Ctx c = {w, host, host->module ? host->module(host->user, TFP_OPS_GAME) : 0};
  size_t used = 0;
  uint64_t type = 0;
  if (size) text[0] = 0;
  w->did = TFP_WARP_NOTHING, w->did_stops = 0, w->stopped_count = 0, w->why[0] = 0;
  bool warp = in->player && in->backpack == 1;
  if (warp) memcpy(&type, in->pack_type, 8);
  warp = warp && type == TFP_WARP_TYPE;
  if (!warp) {
    memset(&w->read, 0, sizeof w->read);
    /* No Warp Pack on: one line when that becomes so, none after. */
    uint64_t key = mix(UINT64_C(0xcbf29ce484222325), &in->backpack, sizeof in->backpack);
    key = mix(key, &type, sizeof type);
    if (w->said_none && key == w->last_key) return false;
    w->said_none = true, w->last_key = key;
    if (!in->player) append(text, size, &used, "Warp: no player snapshot\n");
    else if (in->backpack == 1) append(text, size, &used, "Warp: the backpack is not a Warp Pack (entity %u, type %016llx)\n", in->pack, (unsigned long long)type);
    else append(text, size, &used, "Warp: no Warp Pack (backpack %s)\n", in->backpack == 0 ? "none" : "not read");
    return used > 0;
  }
  w->said_none = false;
  TfpWarpRead *r = &w->read;
  bool read = c.game && read_pack(&c, in, type, r);
  if (!c.game) why(w, "no game module");
  if (!read) w->did = TFP_WARP_REFUSED;
  else w->did = TFP_WARP_WATCHING;
  const bool code = c.game && pins(&c); /* the game's code the module relies on (the spec, section 4); read once per game base */

  /* The settings line: when the pack's table is first read, or changes. */
  if (read) {
    uint64_t sk = mix(UINT64_C(0xcbf29ce484222325), &in->pack, 4);
    sk = mix(sk, &r->settings, 8), sk = mix(sk, r->modes, sizeof r->modes);
    if (sk != w->settings_key) {
      w->settings_key = sk;
      append(text, size, &used, "Warp: pack %u: displacement row %u of %u, settings %s; mode table:", in->pack, r->index, r->count,
             r->override_settings ? "its own (M+0xa0)" : "the type's");
      for (int m = 0; m < 3; m++) {
        char play[64], stop[64];
        ids_text(play, sizeof play, r->modes[m].play, 5), ids_text(stop, sizeof stop, r->modes[m].stop, 5);
        append(text, size, &used, " mode %d (tag %u) play [%s] stop [%s];", m, r->modes[m].tag, play, stop);
      }
      append(text, size, &used, " %u distinct ids\n", r->id_count);
    }
  }
  /* The "Warp:" line: on every change, and once a second while the pack is on. */
  uint64_t key = mix(UINT64_C(0x84222325cbf29ce4), &in->pack, 4);
  key = mix(key, &w->did, sizeof w->did), key = mix(key, w->why, strlen(w->why));
  key = mix(key, &in->owns, 1), key = mix(key, &in->armed, 1), key = mix(key, &code, 1);
  if (read) {
    key = mix(key, &r->bit48, 1), key = mix(key, &r->ads_cache, 1), key = mix(key, &r->mode_word, 4), key = mix(key, &r->in_effects, 1);
    key = mix(key, r->playing, r->playing_count * sizeof *r->playing);
  }
  if (key == w->last_key && in->now - w->last_line < 1.0 && in->now >= w->last_line) return used > 0;
  w->last_key = key, w->last_line = in->now;
  append(text, size, &used, "Warp: pack %u", in->pack);
  if (read) {
    char list[300];
    ids_text(list, sizeof list, r->playing, r->playing_count);
    append(text, size, &used, " (row %u) bit48=%u ads_cache=%u mode_word=%#x charge=%.3f effects=%s playing [%s] of %u ids", r->index, r->bit48,
           r->ads_cache, r->mode_word, (double)r->charge, r->in_effects ? "held" : "none", list, r->id_count);
  }
  append(text, size, &used, "; code %s; view %s, switch %s; did: %s", code ? "ok" : w->pin_failed[0] ? w->pin_failed : "unread", in->owns ? "owned" : "not owned",
         in->armed ? "on" : "off", did_name(w->did));
  if (w->did == TFP_WARP_REFUSED) append(text, size, &used, ": %s", w->why);
  append(text, size, &used, "\n");
  return used > 0;
}

bool tfp_warp_release(TfpWarp *w, const TfpOpsHost *host) {
  (void)w, (void)host;
  return false;
}
