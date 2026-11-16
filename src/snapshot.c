/* See snapshot.h. Line references are to the retail file
 * src/first_person/backend.lua at 0.5.41-smooth.66. */
#include "snapshot.h"
#include "profile.h"
#include <stdio.h>
#include <string.h>

#define TFP_LOW UINT64_C(65536)
#define TFP_HIGH UINT64_C(0x800000000000)

typedef struct Reader { const FpMemory *m; char why[160]; } Reader;

static bool refuse(Reader *r, const char *text) {
  if (!*r->why) snprintf(r->why, sizeof r->why, "%s", text);
  return false;
}
static bool bytes(Reader *r, uintptr_t at, void *out, size_t size) { /* _read, :65 */
  return fp_read(r->m, at, out, size) || refuse(r, "Memory unavailable");
}
static bool u32(Reader *r, uintptr_t at, uint32_t *out) { return bytes(r, at, out, 4); }
static bool ptr(Reader *r, uintptr_t at, uintptr_t *out) { /* _ptr, :81 */
  uint64_t value = 0;
  if (!bytes(r, at, &value, 8)) return false;
  if (value < TFP_LOW || value >= TFP_HIGH) {
    if (!*r->why)
      snprintf(r->why, sizeof r->why, "Pointer unavailable at0x%llx (value0x%llx)", (unsigned long long)at,
               (unsigned long long)value);
    return false;
  }
  *out = (uintptr_t)value;
  return true;
}
static bool valid_id(uint32_t id) { return id > 0 && id < UINT32_MAX; } /* :20 */

/* _map, :132. Open-addressing table: buckets pointer, capacity (a power of
 * two), empty key, multiplier; rows of key and index. Checks in retail's order. */
static bool map(Reader *r, uintptr_t at, uint32_t id, uint32_t limit, uint32_t *index) {
  uint8_t header[20];
  uint64_t buckets;
  uint32_t capacity, empty, multiplier;
  if (!valid_id(id)) return refuse(r, "Invalid entity");
  if (!bytes(r, at, header, sizeof header)) return false;
  memcpy(&buckets, header, 8);
  memcpy(&capacity, header + 8, 4);
  memcpy(&empty, header + 12, 4);
  memcpy(&multiplier, header + 16, 4);
  if (!capacity || capacity > 1048576 || (capacity & (capacity - 1))) return refuse(r, "Invalid registry");
  if (id == empty) return refuse(r, "Empty entity");
  if (buckets < TFP_LOW || buckets >= TFP_HIGH) return refuse(r, "Pointer unavailable");
  uint32_t probes = capacity < 4096 ? capacity : 4096;
  for (uint32_t n = 0; n < probes; n++) {
    uint32_t row[2], slot = (id * multiplier + n) & (capacity - 1);
    if (!bytes(r, (uintptr_t)buckets + (uintptr_t)slot * 8, row, sizeof row)) return false;
    if (row[0] == empty) return refuse(r, "Entity absent");
    if (row[0] == id) {
      if (row[1] >= limit) return refuse(r, "Entity index out of bounds");
      *index = row[1];
      return true;
    }
  }
  return refuse(r, "Entity lookup bound reached");
}

static int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
/* _module('identity'), :86. The game image and its code pins. A pin that
 * cannot be read yet (packed first-person pages) is tolerated; a readable one
 * must match exactly. */
static bool module_identity(Reader *r) {
  const FpMemory *m = r->m;
  uint8_t mz[2], h[96];
  uint32_t pe = 0, stamp, size;
  if (m->game < TFP_LOW) return refuse(r, "game.dll unavailable");
  if (!bytes(r, m->game, mz, 2)) return false;
  if (mz[0] != 'M' || mz[1] != 'Z') return refuse(r, "Invalid game module");
  if (!u32(r, m->game + 0x3c, &pe)) return false;
  if (pe < 0x40 || pe > 0x1000) return refuse(r, "Invalid PE header");
  if (!bytes(r, m->game + pe, h, sizeof h)) return false;
  if (memcmp(h, "PE\0\0", 4) || h[4] != 0x64 || h[5] != 0x86 || h[24] != 0x0b || h[25] != 0x02)
    return refuse(r, "AMD64 PE32+ required");
  memcpy(&stamp, h + 8, 4);
  memcpy(&size, h + 80, 4);
  if (stamp != FP_GAME_STAMP || size != FP_GAME_SIZE) return refuse(r, "Unsupported game build");
  for (size_t i = 0; i < FP_WITNESS_COUNT; i++) {
    const FpWitness *w = fp_witnesses + i;
    uint8_t actual[64];
    size_t n = strlen(w->hex) / 2;
    if (n > sizeof actual || !fp_read(m, m->game + w->rva, actual, n)) continue;
    for (size_t j = 0; j < n; j++)
      if (actual[j] != (uint8_t)(hex_digit(w->hex[j * 2]) * 16 + hex_digit(w->hex[j * 2 + 1]))) {
        if (!*r->why) snprintf(r->why, sizeof r->why, "Native ADS fingerprint mismatch: %s", w->name);
        return false;
      }
  }
  return true;
}

bool tfp_module_identity(const FpMemory *m, char *error, size_t error_size) {
  Reader r = {m, ""};
  bool ok = m && module_identity(&r);
  if (!ok && error && error_size) snprintf(error, error_size, "%s", *r.why ? r.why : "game.dll unavailable");
  return ok;
}

bool tfp_snapshot(const FpMemory *m, TfpSnapshotScope scope, TfpSnapshot *out, char *error, size_t error_size) {
  Reader r = {m, ""};
  TfpSnapshot s = {0};
  uint32_t queue[2], request[5], d[6], count = 0, index = 0;
  uint8_t entry[20];
  bool ok = false;
  if (!m || !out) return false;
  do {
    if (!module_identity(&r)) break; /* :168 */
    s.base = m->game;
    /* _top, :157 */
    if (!ptr(&r, m->game + FP_CAMERA, &s.context) || !bytes(&r, s.context + 0x1f8, queue, sizeof queue)) break;
    if (queue[0] >= 32 || queue[1] >= 32) { refuse(&r, "Invalid camera request queue"); break; }
    s.head = queue[0];
    s.tail = queue[1];
    s.record = s.context + (s.head != s.tail ? 0x200 + ((s.tail + 31) % 32) * 0xe8 : 0x1f00);
    if (!bytes(&r, s.record, request, sizeof request) || !ptr(&r, s.context, &s.camera)) break;
    s.camera_kind = request[0] & 65535;
    s.request_avatar = request[1];
    s.request_weapon = request[2];
    s.request_body = request[3];
    /* :169-173 */
    if (!ptr(&r, m->game + FP_WORLD, &s.world) || !ptr(&r, m->game + FP_CONTROLLER, &s.controller) ||
        !bytes(&r, s.controller, s.controller_bytes, 6)) break;
    memcpy(&s.mask, s.controller_bytes, 4);
    s.controller_active = s.controller_bytes[4] != 0;
    s.blocked = s.mask != 0 || s.controller_bytes[5] != 0;
    /* local avatar, :174-181 */
    if (!ptr(&r, m->game + FP_PLAYER, &s.player) || !u32(&r, s.player + 0x88, &count)) break;
    if (!count) { refuse(&r, "Local player unavailable"); break; }
    if (!u32(&r, s.player + 0x3a8, &s.local_entity) ||
        !map(&r, s.world + FP_LIGHT_MAP, s.local_entity, 1048576, &index) ||
        !bytes(&r, s.world + FP_ENTITY_ROWS + (uintptr_t)index * 24, d, sizeof d)) break;
    if (d[4] != s.local_entity || !(d[5] & 1)) { refuse(&r, "Local player generation unavailable"); break; }
    if (!valid_id(d[2])) { refuse(&r, "Local avatar unavailable"); break; }
    s.avatar = d[2];
    memcpy(s.avatar_descriptor, d, 20);
    /* :182 */
    s.request_local = s.request_avatar == s.avatar;
    if (!s.request_local) { refuse(&r, "Camera is not owned by local player"); break; }
    /* avatar pool, :184-191 */
    if (!ptr(&r, m->game + FP_AVATAR, &s.avatar_owner) || !map(&r, s.avatar_owner + 0xf8, s.avatar, 8, &s.avatar_index) ||
        !u32(&r, s.avatar_owner + 0x6c, &count)) break;
    if (count > 8 || s.avatar_index >= count) { refuse(&r, "Local avatar retired"); break; }
    if (!ptr(&r, s.avatar_owner + 0x110 + (uintptr_t)s.avatar_index * 8, &s.avatar_entry) ||
        !bytes(&r, s.avatar_entry, entry, sizeof entry)) break;
    if (memcmp(entry, s.avatar_descriptor, 20)) { refuse(&r, "Avatar descriptor changed"); break; }
    memcpy(&s.body, entry + 0xc, 4);
    if (!valid_id(s.body)) { refuse(&r, "First-person body unavailable"); break; }
    /* flags and rig, :192-196 */
    s.flags_address = s.avatar_owner + FP_AVATAR_FLAGS + (uintptr_t)s.avatar_index * FP_AVATAR_STRIDE;
    if (!bytes(&r, s.flags_address, s.flags, 24) ||
        !u32(&r, s.avatar_owner + FP_AVATAR_RIG + (uintptr_t)s.avatar_index * FP_AVATAR_STRIDE, &s.viewmodel)) break;
    /* :198-224 */
    s.aiming = (s.flags[6] & 1) != 0;
    s.shoulder_aim = !s.controller_active && (s.flags[10] & 4) && (s.flags[15] & 32);
    s.seated = (s.flags[14] & 4) != 0;
    s.reloading = (s.flags[15] & 2) != 0;
    bool view = s.camera_kind == 4 || s.camera_kind == 6;
    s.safe = s.request_local && view && !s.seated && !s.blocked;
    s.weapon_transition = view && !s.seated && s.mask == 2 && !s.controller_bytes[5];
    s.head_camera_transition = s.camera_kind == 4 && !s.seated && s.mask == 0x20 && !s.controller_bytes[5];
    s.engine_safe = s.safe;
    if (scope == TFP_SNAPSHOT_PLAYER) { ok = true; break; } /* :233 */
    /* wielded weapon, :245-250 */
    uintptr_t wield = 0, rows = 0, owner = 0, pool = 0;
    uint32_t wd[6];
    if (!ptr(&r, m->game + FP_WIELD, &wield) || !map(&r, wield + 0x30, s.avatar, 4096, &index) ||
        !ptr(&r, wield + 0x48, &rows) || !ptr(&r, rows + (uintptr_t)index * 8, &owner) ||
        !bytes(&r, owner, entry, sizeof entry)) break;
    if (memcmp(entry, s.avatar_descriptor, 20)) { refuse(&r, "Wield ownership changed"); break; }
    if (!ptr(&r, wield + 0x60, &rows) || !u32(&r, rows + (uintptr_t)index * 0x1d0, &s.weapon)) break;
    if (!valid_id(s.weapon)) { refuse(&r, "Equip a firearm before starting the camera test"); break; }
    /* _descriptor, :151, then the weapon component, :253-261 */
    if (!map(&r, s.world + FP_ENTITY_MAP, s.weapon, 1048576, &index) ||
        !bytes(&r, s.world + FP_ENTITY_ROWS + (uintptr_t)index * 24, wd, sizeof wd)) break;
    if (wd[2] != s.weapon || !(wd[5] & 1)) { refuse(&r, "Entity generation unavailable"); break; }
    if (!ptr(&r, m->game + FP_WEAPONS, &pool) || !map(&r, pool + 0x30, s.weapon, 4096, &index) ||
        !ptr(&r, pool + 0x48, &rows) || !ptr(&r, rows + (uintptr_t)index * 8, &s.weapon_component_descriptor) ||
        !bytes(&r, s.weapon_component_descriptor, entry, sizeof entry)) break;
    if (memcmp(entry, wd, 20)) { refuse(&r, "Weapon component ownership changed"); break; }
    memcpy(&s.weapon_unit, entry + 0xc, 4);
    if (!valid_id(s.weapon_unit)) { refuse(&r, "Weapon unit unavailable"); break; }
    memcpy(s.weapon_descriptor, wd, 20);
    /* :262-265 */
    if (s.camera_kind == 6 && s.request_weapon != s.weapon) { refuse(&r, "ADS request belongs to another weapon"); break; }
    if (s.camera_kind == 6) { /* :264 reads the record again */
      uint32_t body = 0;
      if (!u32(&r, s.record + 0xc, &body)) break;
      if (body != s.body) { refuse(&r, "ADS request body changed"); break; }
    }
    s.weapon_ready = true; /* :270 */
    s.full = true;
    ok = true;
  } while (0);
  if (!ok) {
    if (error && error_size) snprintf(error, error_size, "%s", *r.why ? r.why : "Memory unavailable");
    return false;
  }
  if (error && error_size) *error = 0;
  *out = s;
  return true;
}

bool tfp_same_player(const TfpSnapshot *a, const TfpSnapshot *b) { /* player_identity, :209-214 */
  return a && b && a->base == b->base && a->context == b->context && a->camera == b->camera &&
         a->world == b->world && a->controller == b->controller && a->player == b->player &&
         a->local_entity == b->local_entity && a->avatar_owner == b->avatar_owner &&
         a->avatar_index == b->avatar_index && a->avatar == b->avatar && a->body == b->body &&
         a->avatar_entry == b->avatar_entry && !memcmp(a->avatar_descriptor, b->avatar_descriptor, 20);
}
bool tfp_same_identity(const TfpSnapshot *a, const TfpSnapshot *b) { /* identity, :266-269; a player snapshot's is its player identity, :233 */
  if (!a || !b || a->full != b->full) return false;
  if (!a->full) return tfp_same_player(a, b);
  return a->base == b->base && a->context == b->context && a->camera == b->camera && a->world == b->world &&
         a->controller == b->controller && a->avatar == b->avatar && a->body == b->body && a->weapon == b->weapon &&
         a->weapon_unit == b->weapon_unit && a->viewmodel == b->viewmodel && a->avatar_entry == b->avatar_entry &&
         a->weapon_component_descriptor == b->weapon_component_descriptor &&
         !memcmp(a->avatar_descriptor, b->avatar_descriptor, 20) && !memcmp(a->weapon_descriptor, b->weapon_descriptor, 20);
}
