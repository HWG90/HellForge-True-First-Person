/* Build25480438 weapon depth. The old build has 0x4d0 definitions and 730
 * resource buckets; the current Steam build's 0x4d8/746 layout is not reused.
 */
#include "depth.h"
#include "profile.h"
#include <math.h>
#include <string.h>
static uint32_t u32(const uint8_t *p, size_t o) {
  uint32_t v;
  memcpy(&v, p + o, 4);
  return v;
}
static uintptr_t u64(const uint8_t *p, size_t o) {
  uint64_t v;
  memcpy(&v, p + o, 8);
  return (uintptr_t)v;
}
static float number(const uint8_t *p, size_t o) {
  float v;
  memcpy(&v, p + o, 4);
  return v;
}
static bool address(uintptr_t p) {
  return p >= 65536 && p < UINT64_C(0x800000000000);
}
static uint64_t retry_time(uint64_t now) {
  return now > UINT64_MAX - 500 ? UINT64_MAX : now + 500;
}
static bool map(const FpMemory *m, const uint8_t *header, uint32_t key,
                FpDepthProbe *p, uint32_t *index) {
  p->buckets = u64(header, 0);
  p->cap = u32(header, 8);
  uint32_t empty = u32(header, 12), mult = u32(header, 16);
  if (!address(p->buckets) || !p->cap || p->cap > 1048576 ||
      (p->cap & (p->cap - 1)) || key == empty)
    return false;
  p->start = (key * mult) & (p->cap - 1);
  p->count = 0;
  *index = UINT32_MAX;
  while (p->count < p->cap && p->count < 64) {
    uint32_t at = (p->start + p->count) & (p->cap - 1);
    uint64_t row = 0;
    if (!fp_read(m, p->buckets + (uintptr_t)at * 8, &row, 8))
      return false;
    p->rows[p->count++] = row;
    if ((uint32_t)row == key) {
      *index = (uint32_t)(row >> 32);
      return true;
    }
    if ((uint32_t)row == empty)
      return true;
  }
  return false;
}
static bool map_valid(const FpMemory *m, const FpDepthProbe *p) {
  if (!p->count || p->count > 64)
    return false;
  for (uint32_t i = 0; i < p->count; i++) {
    uint64_t row = 0;
    uint32_t at = (p->start + i) & (p->cap - 1);
    if (!fp_read(m, p->buckets + (uintptr_t)at * 8, &row, 8) ||
        row != p->rows[i])
      return false;
  }
  return true;
}
static bool data_valid(const FpMemory *m, const FpDepthData *d) {
  uintptr_t p = 0;
  uint8_t bytes[0xdc];
  float degrees = 0;
  if (!fp_pointer(m, m->game + FP_WEAPONS, &p) || p != d->pool ||
      !fp_pointer(m, m->game + FP_WORLD, &p) || p != d->world ||
      !fp_pointer(m, m->game + 0x3326340, &p) || p != d->settings ||
      !fp_read(m, d->pool + 0x30, bytes, sizeof d->pool_bytes) ||
      memcmp(bytes, d->pool_bytes, sizeof d->pool_bytes) ||
      !map_valid(m, &d->primary) || !map_valid(m, &d->overrides) ||
      !fp_read(m, d->parameters, bytes, 16) ||
      memcmp(bytes, d->parameter_bytes, 16) ||
      !fp_read(m, d->data + 0x74, bytes, sizeof d->data_bytes) ||
      memcmp(bytes, d->data_bytes, sizeof d->data_bytes) ||
      !fp_read(m, d->settings + 0xac408, &degrees, 4) || degrees != d->degrees)
    return false;
  if (d->resource_row &&
      (!fp_pointer(m, d->world + 0xf12bd8, &p) || p != d->resource_table ||
       !fp_read(m, d->resource_row, bytes, 16) ||
       memcmp(bytes, d->resource_bytes, 16)))
    return false;
  return true;
}
static bool data(const FpMemory *m, const FpSnapshot *s, FpDepthData *d,
                 uint32_t *selected) {
  memset(d, 0, sizeof *d);
  if (!fp_pointer(m, m->game + FP_WEAPONS, &d->pool) ||
      !fp_pointer(m, m->game + FP_WORLD, &d->world) ||
      !fp_pointer(m, m->game + 0x3326340, &d->settings) ||
      !fp_read(m, d->pool + 0x30, d->pool_bytes, sizeof d->pool_bytes))
    return false;
  uint32_t wi = UINT32_MAX, di = UINT32_MAX;
  if (!map(m, d->pool_bytes, s->weapon, &d->primary, &wi) || wi >= 4096 ||
      !map(m, d->pool_bytes + 0x40, s->weapon, &d->overrides, &di))
    return false;
  uintptr_t descriptors = u64(d->pool_bytes, 0x18), descriptor = 0;
  if (!address(descriptors) ||
      !fp_pointer(m, descriptors + (uintptr_t)wi * 8, &descriptor) ||
      descriptor != s->weapon_descriptor)
    return false;
  if (di != UINT32_MAX) {
    uintptr_t rows = u64(d->pool_bytes, 0x80);
    if (!address(rows) || di >= 4096)
      return false;
    d->data = rows + (uintptr_t)di * 0x4d0;
  } else {
    uint64_t resource = 0;
    memcpy(&resource, s->weapon_bytes, 8);
    if (!resource || !fp_pointer(m, d->world + 0xf12bd8, &d->resource_table))
      return false;
    uint32_t bucket = (uint32_t)(resource % 730);
    for (uint32_t n = 0; n < 730; n++) {
      uintptr_t at = d->resource_table + (uintptr_t)((bucket + n) % 730) * 16;
      uint8_t row[16];
      if (!fp_read(m, at, row, 16))
        return false;
      uint64_t key = u64(row, 0);
      if (!key)
        return false;
      if (key == resource) {
        di = u32(row, 8);
        if (di >= 730)
          return false;
        d->resource_row = at;
        memcpy(d->resource_bytes, row, 16);
        break;
      }
    }
    if (!d->resource_row)
      return false;
    d->data = d->resource_table + 0x2da0 + (uintptr_t)di * 0x4d0;
  }
  uintptr_t parameters = u64(d->pool_bytes, 0x28),
            packed = u64(d->pool_bytes, 0x30);
  if (!address(parameters) || !address(packed))
    return false;
  d->parameters = parameters + (uintptr_t)wi * 0x3f0 + 0x350;
  d->packed = packed + (uintptr_t)wi * 12 + 4;
  uint32_t bits = 0;
  if (!fp_read(m, d->parameters, d->parameter_bytes, 16) ||
      !fp_u32(m, d->packed, &bits) ||
      !fp_read(m, d->data + 0x74, d->data_bytes, sizeof d->data_bytes) ||
      !fp_read(m, d->settings + 0xac408, &d->degrees, 4))
    return false;
  unsigned matches = 0;
  for (unsigned i = 0; i < 4; i++)
    if (u32(d->parameter_bytes, i * 4) == 1)
      matches++;
  if (matches != 1)
    return false;
  *selected = (bits >> 4) & 3;
  if (*selected > 2)
    return false;
  for (unsigned i = 0; i < 3; i++) {
    d->zoom[i] = number(d->data_bytes, i * 4);
    d->coefficient[i] = number(d->data_bytes, 0xc8 + i * 4);
    if (!isfinite(d->zoom[i]) || d->zoom[i] < .1f || d->zoom[i] > 100 ||
        !isfinite(d->coefficient[i]) || d->coefficient[i] < .0001f ||
        d->coefficient[i] > 100)
      return false;
  }
  d->near_term[0] = number(d->data_bytes, 0xd4);
  d->near_term[1] = number(d->data_bytes, 0xd8);
  if (!isfinite(d->degrees) || d->degrees < 10 || d->degrees > 170 ||
      !isfinite(d->near_term[0]) || !isfinite(d->near_term[1]) ||
      fabsf(d->near_term[0]) > 1 || fabsf(d->near_term[1]) > 1)
    return false;
  return data_valid(m, d);
}
static bool code(const FpMemory *m) {
  return fp_bytes(m, m->game + 0x509ae2,
                  "4869c0d0040000490383b00000004883c428c3") &&
         fp_bytes(m, m->game + 0x50957f,
                  "4c8b90d82bf10048b8379f711660f31967") &&
         fp_bytes(m, m->game + 0x5095a3, "69c0da020000442bc0") &&
         fp_bytes(m, m->game + 0x5095f1,
                  "4869c1d00400004805a02d00004903c2c3") &&
         fp_bytes(m, m->game + 0x178759d,
                  "4869c2f003000048055003000049034358") &&
         /* Native presentation-distance calculation: these members did not
          * move when the definition stride changed between game builds. */
         fp_bytes(m, m->game + 0x1278f68, "f30f59964c010000") &&
         fp_bytes(m, m->game + 0x1278f78, "f30f59b648010000") &&
         fp_bytes(m, m->game + 0x1278f95, "f30f5984863c010000") &&
         fp_bytes(m, m->game + 0x12873b3, "f30f1044245cf30f1015df4b1401") &&
         fp_bytes(m, m->game + 0x12873d9, "f30f5c4424600f54c2f30f5ec1") &&
         fp_bytes(m, m->game + 0x1287a91, "f3450f58fb") &&
         fp_bytes(m, m->game + 0x1287b43, "f30f59faf3410f59cff3") &&
         fp_bytes(m, m->game + 0x1287c04, "f3410f11774cf3450f114750");
}
bool fp_depth_reference(const FpDepthData *d, uint32_t selected, float distance,
                        float *reference) {
  if (!d || !reference || selected > 2)
    return false;
  unsigned widest = 0;
  for (unsigned i = 1; i < 3; i++)
    if (d->zoom[i] < d->zoom[widest])
      widest = i;
  const float rad = .01745329238474369f;
  float angle = ((d->degrees / d->zoom[selected]) * rad) * .5f,
        base = ((d->degrees / d->zoom[widest]) * rad) * .5f;
  if (angle < .0001f || angle > 1.48f || base < .0001f || base > 1.48f ||
      !isfinite(distance) || fabsf(distance) > 4)
    return false;
  if (selected == widest) {
    *reference = distance;
    return true;
  }
  float denominator = tanf(base) * d->coefficient[widest],
        current = tanf(angle) * d->coefficient[selected];
  float u = (d->degrees - 45) / 45,
        base_term = (1 - u) * d->near_term[0] + u * d->near_term[1];
  *reference = base_term + (distance - base_term) * (current / denominator);
  return isfinite(*reference) && fabsf(*reference) <= 4;
}
void fp_depth_observe(FpDepth *d, const FpMemory *m, uintptr_t request,
                      uintptr_t stack, uint64_t token, uint32_t thread) {
  d->observation = (FpDepthObservation){
      .stack = stack, .request = request, .token = token, .thread = thread};
  d->carrier_ready = false;
  if (stack)
    d->observation.ready = fp_read(m, stack + 0x44, d->observation.frame,
                                   sizeof d->observation.frame) &&
                           fp_read(m, request + 0x80, d->observation.state,
                                   sizeof d->observation.state);
}
bool fp_depth_state_same(const FpMemory *m, const FpDepthObservation *o) {
  uint8_t r[0x24];
  return fp_read(m, o->request + 0x80, r, sizeof r) &&
         !memcmp(r, o->state, 8) &&
         !memcmp(r + 12, o->state + 12, sizeof r - 12);
}
float fp_depth_delta(FpDepth *d, const FpMemory *m, const FpSnapshot *s,
                     uintptr_t stack, uint64_t token, uint32_t thread,
                     uint64_t now) {
  FpDepthObservation o = d->observation;
  d->observation.ready = false;
  d->carrier_ready = false;
  if (s->full_ads || !stack || !o.ready || o.stack != stack ||
      o.request != s->request || o.token != token || o.thread != thread)
    return 0;
  if (!d->checked) {
    d->code_ok = code(m);
    d->checked = true;
  }
  if (!d->code_ok || !fp_depth_state_same(m, &o))
    return 0;
  uint8_t request[0x24];
  if (!fp_read(m, s->request + 0x80, request, sizeof request))
    return 0;
  float current = number(request, 0x10), previous = number(request, 0x1c),
        prior = number(request, 0x20), target = number(o.frame, 0x18),
        lens = number(o.frame, 0x1c), weight = number(request, 8);
  if (request[13] > 1 || request[15] || o.frame[0] || o.frame[1] > 1 ||
      number(request, 0x18) != 0 || u32(request, 4) != u32(o.frame, 0x1c) ||
      !isfinite(target) || target < .0002f || target > 2.96f ||
      !isfinite(lens) || lens < .0002f || lens > 2.96f || !isfinite(prior) ||
      prior < 0 || prior > 2.96f || !isfinite(current) || !isfinite(previous) ||
      fabsf(current) > 4 || fabsf(previous) > 4 ||
      (request[13] && (!isfinite(weight) || weight < 0 || weight > 1)))
    return 0;
  float span = fabsf(target - prior),
        t = span > 1.1920928955078125e-7f
                ? fminf(1, fabsf(target - lens) / span)
                : 0;
  uint32_t signature[] = {u32(request, 0x10), u32(request, 0x1c),
                          u32(request, 0x20), 0};
  if (!d->signature_set || memcmp(signature, d->signature, sizeof signature) ||
      (!d->valid && now >= d->retry_at)) {
    bool old_valid = d->valid, old_previous_known = d->previous_known;
    float old_current = d->current, old_previous = d->previous,
          old_reference = d->current_reference,
          old_previous_reference = d->previous_reference;
    d->signature_set = true;
    memcpy(d->signature, signature, sizeof signature);
    d->valid = d->previous_known = false;
    d->current = current;
    d->previous = previous;
    d->retry_at = retry_time(now);
    FpDepthData next;
    uint32_t selected = 0;
    if (!data(m, s, &next, &selected) ||
        !fp_depth_reference(&next, selected, current, &d->current_reference))
      return 0;
    if (old_valid && previous == old_current) {
      d->previous_reference = old_reference;
      d->previous_known = true;
    } else if (old_valid && old_previous_known && previous == old_previous) {
      d->previous_reference = old_previous_reference;
      d->previous_known = true;
    } else if (previous == 0) {
      d->previous_reference = 0;
      d->previous_known = true;
    }
    d->data = next;
    d->next_check = retry_time(now);
    d->valid = true;
  } else if (d->valid && now >= d->next_check) {
    d->next_check = retry_time(now);
    if (!data_valid(m, &d->data)) {
      d->valid = false;
      /* Transient protected-read failures and changed definitions need a
       * bounded fresh discovery, not permanent refusal for this signature. */
      d->retry_at = retry_time(now);
      return 0;
    }
  }
  if (!d->valid || (t != 0 && !d->previous_known))
    return 0;
  float native = (1 - t) * current + t * previous,
        reference = (1 - t) * d->current_reference +
                    (t == 0 ? 0 : t * d->previous_reference);
  float delta = (reference - native) * (request[13] ? weight : 1);
  if (!isfinite(delta) || fabsf(delta) > 1)
    return 0;
  d->pending = o;
  d->delta = delta;
  d->carrier_ready = delta != 0;
  return delta;
}
