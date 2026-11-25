/* Round 38 (docs/design/specs/2026-11-25-exosuit-seat.md): which exosuit the local diver sits in, by guarded reads
 * alone. Pure: memory comes through a read callback that fails instead of faulting; no engine call.
 *
 * Every EXO model is the one unit content/fac_helldivers/vehicles/combat_walker/combat_walker (its weapons are units of
 * their own); its resource name hash is at unit +0x358 (the same field that names the first-person rig,
 * fp_cha_avatar, in src/head_visibility.c). The diver sits on its node attach_driver.
 *
 * A unit is the suit the diver is in when it is alive in the unit registry (its generation byte), names itself at +8,
 * carries the suit's resource, is in the body's world (+0x18), and either the body is linked to it (body +0x1D0 is
 * its object) or its attach_driver node is within TFP_COCKPIT_SEAT_M of the body's root. Another player's suit is
 * never within a metre of the local body's root while that body is seated; two candidates that both pass are refused
 * (none is taken). The scene graph: count at unit +0x70, world matrices at +0x88 (64 bytes, position at +0x30), node
 * name hashes at +0xA0 (src/camera_helper.c L_UNIT_*). */
#ifndef TFP_COCKPIT_FIND_H
#define TFP_COCKPIT_FIND_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define TFP_COCKPIT_SEAT_M      1.0f
#define TFP_COCKPIT_ATTACH_HASH UINT32_C(0xF1687755)   /* attach_driver */
#define TFP_COCKPIT_MAX_NODES   1024u
static const uint8_t TFP_COCKPIT_RESOURCE[8] = {0xe3, 0x45, 0x5e, 0xda, 0xd2, 0xb3, 0xe4, 0x79};

typedef bool (*TfpCockpitRead)(void *user, uint64_t at, void *out, size_t size);
typedef struct TfpCockpitMemory { TfpCockpitRead read; void *user; uint64_t units, generations; uint32_t capacity; } TfpCockpitMemory;
/* The body the suit is looked for around: its unit object, world and root position. */
typedef struct TfpCockpitBody { uint32_t ref; uint64_t object, world, parent; float root[3]; } TfpCockpitBody;
typedef enum TfpCockpitHow { TFP_COCKPIT_NONE, TFP_COCKPIT_LINKED, TFP_COCKPIT_NEAR } TfpCockpitHow;
typedef struct TfpCockpitFound { uint32_t ref; uint64_t object; TfpCockpitHow how; float distance; uint32_t looked, suits; } TfpCockpitFound;

static inline bool tfp_cockpit_ptr(const TfpCockpitMemory *m, uint64_t at, uint64_t *out) {
  return m->read(m->user, at, out, 8) && *out >= 65536 && *out < (UINT64_C(1) << 47);
}
/* The unit object of `ref`, alive now. */
static inline bool tfp_cockpit_object(const TfpCockpitMemory *m, uint32_t ref, uint64_t *object) {
  const uint32_t index = ref % 0x400000u, generation = ref / 0x400000u % 256u;
  uint8_t now = 0, again = 0; uint64_t at = 0, at_again = 0; uint32_t self = 0;
  if (!ref || ref == UINT32_MAX || index >= m->capacity) return false;
  if (!m->read(m->user, m->generations + index, &now, 1) || now != generation || !tfp_cockpit_ptr(m, m->units + (uint64_t)index*8, &at)) return false;
  if (!m->read(m->user, at + 8, &self, 4) || self != ref) return false;
  if (!m->read(m->user, m->generations + index, &again, 1) || again != generation || !tfp_cockpit_ptr(m, m->units + (uint64_t)index*8, &at_again)
      || at_again != at) return false;
  *object = at;
  return true;
}
/* The body: its object, world, parent link and root (node 0) position. */
static inline bool tfp_cockpit_body(const TfpCockpitMemory *m, uint32_t ref, TfpCockpitBody *out) {
  TfpCockpitBody b; memset(&b, 0, sizeof b); b.ref = ref;
  uint64_t worlds = 0; uint32_t count = 0;
  if (!tfp_cockpit_object(m, ref, &b.object) || !tfp_cockpit_ptr(m, b.object + 0x18, &b.world) || !m->read(m->user, b.object + 0x1D0, &b.parent, 8)
      || !m->read(m->user, b.object + 0x70, &count, 4) || count == 0 || count > TFP_COCKPIT_MAX_NODES || !tfp_cockpit_ptr(m, b.object + 0x88, &worlds)
      || !m->read(m->user, worlds + 0x30, b.root, 12) || !isfinite(b.root[0]) || !isfinite(b.root[1]) || !isfinite(b.root[2])) return false;
  *out = b;
  return true;
}
/* Is `ref` the suit `body` sits in? `found` gets how and the distance from its seat node to the body's root. */
static inline bool tfp_cockpit_check(const TfpCockpitMemory *m, const TfpCockpitBody *body, uint32_t ref, TfpCockpitFound *found) {
  uint64_t object = 0, world = 0, hashes = 0, worlds = 0; uint8_t resource[8]; uint32_t count = 0;
  if (ref == body->ref || !tfp_cockpit_object(m, ref, &object) || !m->read(m->user, object + 0x358, resource, 8)
      || memcmp(resource, TFP_COCKPIT_RESOURCE, 8) || !tfp_cockpit_ptr(m, object + 0x18, &world) || world != body->world) return false;
  float distance = INFINITY;
  if (m->read(m->user, object + 0x70, &count, 4) && count > 0 && count <= TFP_COCKPIT_MAX_NODES && tfp_cockpit_ptr(m, object + 0xA0, &hashes)
      && tfp_cockpit_ptr(m, object + 0x88, &worlds)) {
    uint32_t names[TFP_COCKPIT_MAX_NODES];
    if (m->read(m->user, hashes, names, (size_t)count*4)) {
      for (uint32_t i = 0; i < count; ++i) if (names[i] == TFP_COCKPIT_ATTACH_HASH) {
        float at[3];
        if (m->read(m->user, worlds + (uint64_t)i*64 + 0x30, at, 12) && isfinite(at[0]) && isfinite(at[1]) && isfinite(at[2])) {
          const float d[3] = {at[0]-body->root[0], at[1]-body->root[1], at[2]-body->root[2]};
          distance = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        }
        break;
      }
    }
  }
  const bool linked = body->parent == object;
  if (!linked && !(distance <= TFP_COCKPIT_SEAT_M)) return false;
  found->ref = ref; found->object = object; found->how = linked ? TFP_COCKPIT_LINKED : TFP_COCKPIT_NEAR; found->distance = distance;
  return true;
}
/* The suit among `refs` (the engine's unit inventory, as references), the body's link tried first. True: exactly
 * one suit passed (or the link named one). `found->looked` and `->suits`: units read, suits among them. */
static inline bool tfp_cockpit_search(const TfpCockpitMemory *m, const TfpCockpitBody *body, const uint32_t *refs, size_t count, TfpCockpitFound *found) {
  TfpCockpitFound best; memset(&best, 0, sizeof best);
  uint32_t looked = 0, suits = 0, passed = 0;
  if (body->parent) { /* the link first: the parent's own reference */
    uint32_t ref = 0;
    if (m->read(m->user, body->parent + 8, &ref, 4) && tfp_cockpit_check(m, body, ref, &best) && best.how == TFP_COCKPIT_LINKED) {
      best.looked = 1; best.suits = 1; *found = best; return true;
    }
    memset(&best, 0, sizeof best);
  }
  for (size_t i = 0; i < count; ++i) {
    uint64_t object = 0; uint8_t resource[8];
    if (refs[i] == body->ref || !tfp_cockpit_object(m, refs[i], &object)) continue;
    ++looked;
    if (!m->read(m->user, object + 0x358, resource, 8) || memcmp(resource, TFP_COCKPIT_RESOURCE, 8)) continue;
    ++suits;
    TfpCockpitFound one;
    if (tfp_cockpit_check(m, body, refs[i], &one) && one.ref != best.ref) { ++passed; best = one; }
  }
  best.looked = looked; best.suits = suits;
  *found = best;
  if (passed != 1) { found->ref = 0; found->object = 0; found->how = TFP_COCKPIT_NONE; return false; }
  return true;
}
#endif
