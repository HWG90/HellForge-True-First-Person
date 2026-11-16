#include "preferences.h"
#include "feature_contract.h"
#include "profile.h"
#include <string.h>
static bool same(const FpByteOwner *a, const FpByteOwner *b) {
  return a->at == b->at && a->pool == b->pool &&
         a->descriptor == b->descriptor && a->rows == b->rows &&
         a->entity == b->entity && !memcmp(a->bytes, b->bytes, 20);
}
static bool aim_location(const FpMemory *m, const FpSnapshot *s,
                         uint32_t weapon, uint32_t mode, FpByteOwner *r) {
  uintptr_t settings = 0;
  uint32_t actual = 0;
  memset(r, 0, sizeof *r);
  r->origin = *s;
  r->weapon = weapon;
  if (!fp_pointer(m, s->avatar_owner + 0x40, &settings) ||
      !fp_u32(m, settings + 0xf0, &actual) || actual > 3)
    return false;
  r->mode = mode <= 3 ? mode : actual;
  if (r->mode == 1 || r->mode == 3) {
    if (!fp_pointer(m, m->game + FP_WEAPONS, &r->pool) ||
        !fp_map(m, r->pool + 0x30, weapon, 4096, &r->index) ||
        !fp_pointer(m, r->pool + 0x48, &r->rows) ||
        !fp_pointer(m, r->rows + (uintptr_t)r->index * 8, &r->descriptor) ||
        !fp_read(m, r->descriptor, r->bytes, 20) ||
        !fp_pointer(m, r->pool + 0x50, &r->rows))
      return false;
    memcpy(&r->entity, r->bytes + 8, 4);
    if (r->entity != weapon)
      return false;
    r->at = r->rows + (uintptr_t)r->index * 2;
  } else {
    if (!fp_pointer(m, s->player + 0xe8, &r->descriptor) ||
        !fp_read(m, r->descriptor, r->bytes, 20))
      return false;
    memcpy(&r->entity, r->bytes + 8, 4);
    if (!fp_map(m, s->player + 0xd0, r->entity, 8, &r->index))
      return false;
    r->pool = s->player;
    r->at = s->player + 0x2ec + (uintptr_t)r->index * 0x38;
  }
  uint8_t v = 2;
  return fp_read(m, r->at, &v, 1) && v <= 1;
}
static bool movement_location(const FpMemory *m, const FpSnapshot *s,
                              FpByteOwner *r) {
  memset(r, 0, sizeof *r);
  r->origin = *s;
  uint32_t active = 0;
  if (!fp_pointer(m, m->game + 0x3326cf8, &r->pool) ||
      !fp_u32(m, r->pool + 12, &active) || !active || active > 16384 ||
      !fp_map(m, r->pool + 0x20, s->avatar, active, &r->index) ||
      !fp_pointer(m, r->pool + 0x38, &r->rows) ||
      !fp_pointer(m, r->rows + (uintptr_t)r->index * 8, &r->descriptor) ||
      r->descriptor != s->avatar_descriptor ||
      !fp_read(m, r->descriptor, r->bytes, 20) ||
      memcmp(r->bytes, s->descriptor, 20) ||
      !fp_pointer(m, r->pool + 0x50, &r->rows))
    return false;
  r->entity = s->avatar;
  r->at = r->rows + (uintptr_t)r->index * 0xa0 + 0x94;
  uint8_t v = 2;
  return fp_read(m, r->at, &v, 1) && v <= 1;
}
static bool stop(FpByteOwner *r, const FpMemory *m, FpByteWrite write,
                 void *user, bool aim) {
  if (!r->held)
    return true;
  FpSnapshot s;
  FpByteOwner current;
  if (!fp_snapshot(m, false, &s, NULL, 0))
    return false;
  if (!fp_same_player(&r->origin, &s)) {
    r->held = false;
    return true;
  }
  bool found = aim ? aim_location(m, &s, r->weapon, r->mode, &current)
                   : movement_location(m, &s, &current);
  if (!found)
    return false;
  if (!same(r, &current)) {
    r->held = false;
    return true;
  }
  uint8_t value = 2;
  if (!fp_read(m, r->at, &value, 1))
    return false;
  if (value == 1) {
    if (!write || !write(user, r->at, 1, 0))
      return false;
    if (!fp_read(m, r->at, &value, 1) || value != 0)
      return false;
  }
  r->held = false;
  return true;
}
bool fp_aim_preference(FpByteOwner *r, const FpMemory *m, bool enabled,
                       FpByteWrite write, void *user) {
  if (!enabled)
    return stop(r, m, write, user, true);
  FpSnapshot s;
  FpByteOwner current;
  if (!fp_feature_contract(m, FP_FEATURE_AIM) ||
      !fp_snapshot(m, true, &s, NULL, 0) || !s.safe)
    return true;
  if (!aim_location(m, &s, s.weapon, UINT32_MAX, &current))
    return true;
  if (r->held && (!fp_same_player(&r->origin, &s) || !same(r, &current))) {
    return stop(r, m, write, user, true);
  }
  uint8_t value = 2;
  if (!fp_read(m, current.at, &value, 1))
    return false;
  if (value == 1)
    return true;
  if (!r->held) {
    *r = current;
    r->held = true;
  }
  return write && write(user, r->at, 0, 1) && fp_read(m, r->at, &value, 1) &&
         value == 1;
}
bool fp_movement_preference(FpByteOwner *r, const FpMemory *m, bool enabled,
                            FpByteWrite write, void *user) {
  if (!enabled) {
    r->draining = false;
    return stop(r, m, write, user, false);
  }
  if (r->draining)
    return true;
  FpSnapshot s;
  FpByteOwner current;
  if (!fp_feature_contract(m, FP_FEATURE_MOVEMENT) ||
      !fp_snapshot(m, false, &s, NULL, 0) || !s.safe)
    return stop(r, m, write, user, false);
  if (!movement_location(m, &s, &current))
    return stop(r, m, write, user, false);
  if (r->held && !same(r, &current))
    return stop(r, m, write, user, false);
  uint8_t v = 2;
  if (!fp_read(m, current.at, &v, 1))
    return false;
  if (r->held) {
    if (v != 1) {
      r->held = false;
      r->draining = true;
    }
    return true;
  }
  if (v == 1)
    return true;
  *r = current;
  r->held = true;
  return write && write(user, r->at, 0, 1) && fp_read(m, r->at, &v, 1) &&
         v == 1;
}
