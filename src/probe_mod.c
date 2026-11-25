/* Read-only probe for True First Person on build 25480438. It reads game
 * memory through HellForge's guarded reader and writes to its own log:
 *   - once, the identity of both images and the result of every code pin;
 *   - the player and weapon snapshot whenever it changes;
 *   - the camera request queue whenever it changes;
 *   - the held weapon's resource key when the weapon changes.
 * It installs nothing in the game, swaps no table entry, calls no game function,
 * writes no game memory and handles no key. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "hellforge_mod.h"
#include "backend.h"
#include "profile.h"
#include "native_contract.h"
#include "snapshot.h"
#include "probe_log.h"
#include "request_log.h"

static const HfApi *hf;
static HfMod *owner;
static TfpProbeLog snapshot_log;
static FpcRequestLog request_log;
static bool pins_reported;
static uint32_t keyed_unit;

static uintptr_t game_base, exe_base;
/* A code pin is compared with the code as it was before any hook HellForge placed: another mod's
 * hook on a pinned function (the First Person Controller hooks the aim controller's exit) must not
 * make this diagnostic refuse every snapshot, as it did in the first probe session (2026-11-16).
 * Everything else is read as it is now. */
static bool pinned_code(uintptr_t at, size_t size) {
  for (size_t i = 0; i < FP_WITNESS_COUNT; i++) {
    uintptr_t pin = game_base + fp_witnesses[i].rva;
    if (at >= pin && at + size <= pin + strlen(fp_witnesses[i].hex) / 2) return true;
  }
  for (size_t i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    uintptr_t pin = (fp_native_sites[i].exe ? exe_base : game_base) + fp_native_sites[i].rva;
    if (at >= pin && at + size <= pin + strlen(fp_native_sites[i].hex) / 2) return true;
  }
  return false;
}
static bool can_read_code(void) {
  return hf->size >= offsetof(HfApi, read_code) + sizeof hf->read_code && hf->read_code;
}
static bool sdk_read(void *unused, uintptr_t at, void *out, size_t size) {
  (void)unused;
  if (can_read_code() && pinned_code(at, size)) return hf->read_code(at, out, size) == 0;
  return hf->read(at, out, size) == 0;
}

static void report_pins(const FpMemory *m) {
  unsigned ok = 0, bad = 0, unread = 0;
  hf->log(owner, "Image: game.dll %s (stamp %#x size %#x expected), helldivers2.exe %s",
          fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ? "ok" : "DIFFERENT", (unsigned)FP_GAME_STAMP,
          (unsigned)FP_GAME_SIZE, fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE) ? "ok" : "DIFFERENT");
  for (size_t i = 0; i < FP_WITNESS_COUNT; i++) {
    const FpWitness *w = fp_witnesses + i;
    uint8_t first;
    bool readable = fp_read(m, m->game + w->rva, &first, 1);
    bool match = readable && fp_bytes(m, m->game + w->rva, w->hex);
    uint8_t live[64], original[64];
    size_t n = strlen(w->hex) / 2;
    bool hooked = match && n <= sizeof live && can_read_code() && hf->read(m->game + w->rva, live, n) == 0 &&
                  hf->read_code(m->game + w->rva, original, n) == 0 && memcmp(live, original, n);
    hf->log(owner, "Pin: %s %s game+%#x%s", match ? "ok" : readable ? "MISMATCH" : "unreadable", w->name, (unsigned)w->rva,
            hooked ? " (hooked by another mod; its original code matches)" : "");
    if (match) ok++; else if (readable) bad++; else unread++;
  }
  for (size_t i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    const FpNativeSite *w = fp_native_sites + i;
    uintptr_t at = (w->exe ? m->exe : m->game) + w->rva;
    uint8_t first;
    bool readable = fp_read(m, at, &first, 1);
    bool match = readable && fp_bytes(m, at, w->hex);
    hf->log(owner, "Pin: %s %s %s+%#x", match ? "ok" : readable ? "MISMATCH" : "unreadable", w->name,
            w->exe ? "exe" : "game", (unsigned)w->rva);
    if (match) ok++; else if (readable) bad++; else unread++;
  }
  hf->log(owner, "Pins: %u ok, %u mismatched, %u unreadable", ok, bad, unread);
}

/* The camera request queue, oldest first (snapshot.c reads only its newest row). */
static bool requests(const FpMemory *m, const TfpSnapshot *s, FpcRequestSnapshot *out) {
  FpcRequestSnapshot q = {0};
  uint32_t ring[2], row[2], closing[2];
  if (!fp_read(m, s->context + 0x1f8, ring, sizeof ring) || ring[0] >= FPC_REQUEST_ROWS || ring[1] >= FPC_REQUEST_ROWS ||
      !fp_read(m, s->context + 0x1f00, row, sizeof row)) return false;
  q.head = ring[0];
  q.tail = ring[1];
  q.local_owner = s->avatar;
  q.base_kind = row[0] & 65535;
  for (uint32_t i = ring[0]; i != ring[1]; i = (i + 1u) % FPC_REQUEST_ROWS) {
    if (!fp_read(m, s->context + 0x200 + (uintptr_t)i * 0xe8, row, sizeof row)) return false;
    q.kind[q.count] = (uint16_t)row[0];
    q.owner[q.count++] = row[1];
  }
  if (!fp_read(m, s->context + 0x1f8, closing, sizeof closing) || memcmp(ring, closing, sizeof ring)) return false;
  q.mask = s->mask;
  q.active = s->controller_bytes[4];
  q.suppress = s->controller_bytes[5];
  q.aiming = s->aiming;
  q.seated = s->seated;
  *out = q;
  return true;
}

static void tick(HfMod *mod, float dt, void *unused) {
  (void)mod; (void)dt; (void)unused;
  size_t size = 0;
  FpMemory memory = {sdk_read, NULL, hf->module_base("game.dll", &size), hf->module_base("helldivers2.exe", &size), NULL};
  if (!memory.game || !memory.exe) return;
  game_base = memory.game;
  exe_base = memory.exe;
  uint64_t now = GetTickCount64();
  TfpSnapshot s;
  char error[160] = "", why[160] = "", text[640];
  bool full = tfp_snapshot(&memory, TFP_SNAPSHOT_FULL, &s, why, sizeof why);
  bool player = full || tfp_snapshot(&memory, TFP_SNAPSHOT_PLAYER, &s, error, sizeof error);
  if (player && !pins_reported) { /* code pages are readable once a player exists */
    pins_reported = true;
    report_pins(&memory);
  }
  /* Before a mission the snapshot fails for minutes with the same text: one line. */
  if (tfp_probe_line(&snapshot_log, now, player ? &s : NULL, error, why, text, sizeof text)) hf->log(owner, "%s", text);
  if (!player) return;
  FpcRequestSnapshot queue;
  if (requests(&memory, &s, &queue) && fpc_request_line(&request_log, now, &queue, text, sizeof text))
    hf->log(owner, "%s", text);
  if (full && s.weapon_unit != keyed_unit) {
    uintptr_t object = fp_unit_object(&memory, s.weapon_unit);
    uint8_t key[8];
    /* Remembered only once it was read: a key that cannot be read yet is tried again next update. */
    if (object && fp_read(&memory, object + 0x358, key, sizeof key)) {
      keyed_unit = s.weapon_unit;
      hf->log(owner, "Weapon: entity=%u unit=%u key=%02x%02x%02x%02x%02x%02x%02x%02x", s.weapon, s.weapon_unit, key[0],
              key[1], key[2], key[3], key[4], key[5], key[6], key[7]);
    }
  }
}

__declspec(dllexport) int hellforge_mod_init(const HfApi *api, HfMod *mod) {
  if (!api || !mod || api->version < 16 || !api->log || !api->read || !api->module_base || !api->build_id ||
      !api->on_game_thread || strcmp(api->build_id(), "25480438")) return 1;
  hf = api;
  owner = mod;
  hf->log(mod, "True First Person probe 0.6.0-probe.2. READ ONLY: no game calls, no table swaps, no memory writes, no keys.");
  if (!hf->on_game_thread(mod, tick, NULL)) {
    hf->log(mod, "Probe could not register its game-thread tick");
    return 1;
  }
  return 0;
}
