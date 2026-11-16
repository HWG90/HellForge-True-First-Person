/* Fresh native branch-5 offsets: the arm/hand/weapon hierarchy stays intact.
 * This never pins an individual hand or gun or replays an animated frame. */
#include "pose_policy.h"
#include "native_contract.h"
#include "policy_owner.h"
#include "profile.h"
#include "scene.h"
#include <intrin.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef void (*UnitSetter)(uint32_t, uint32_t, const float *);
typedef const float *(*UnitGetter)(uint32_t, uint32_t);
static _Atomic(UnitGetter) original_getter;
static _Atomic(FpPosePolicy *) active;
static _Atomic(UnitSetter) original_position, original_rotation;
static _Atomic uint64_t callback_generation;
#ifdef TFP_POLICY_TEST
static uint64_t test_now;
static void (*test_before_lock)(unsigned family);
static uint64_t pose_now(void) { return test_now; }
#else
static uint64_t pose_now(void) { return GetTickCount64(); }
#endif
static bool fresh(uint64_t at, uint64_t now) {
  return now >= at && now - at <= 500;
}
static float ease(uint64_t start, uint64_t now) {
  float u = now >= start ? (float)(now - start) / 150 : 0;
  float t = fminf(1, fmaxf(0, u));
  return t * t * (3 - 2 * t);
}
static bool depth_response_valid(const FpPoseDepthResponse *r, uint64_t now) {
  return (unsigned)r->phase <= FP_POSE_DEPTH_RELEASE && isfinite(r->source) &&
         fabsf(r->source) <= 1 && isfinite(r->bias_start) &&
         fabsf(r->bias_start) <= 2.00001f && isfinite(r->value) &&
         fabsf(r->value) <= 3.00001f &&
         (!r->sampled || (now >= r->sample_at && now >= r->start));
}
/* A missing sample never requalifies the old reference. Its last scalar is
 * held for at most500 ms, then released over150 ms. Recovery eases only the
 * availability bias: every subsequent valid delta change remains immediate
 * so native zoom-depth changes can still be cancelled on the same frame. */
static float depth_response(FpPoseDepthResponse *r, uint64_t now,
                            bool available, float source, float hip_weight) {
  if (!depth_response_valid(r, now))
    *r = (FpPoseDepthResponse){0};
  if (r->sampled && !fresh(r->sample_at, now)) {
    if (r->phase != FP_POSE_DEPTH_RELEASE && r->phase != FP_POSE_DEPTH_EMPTY) {
      /* Expiry is at least500 ms after the last recovery sample; its150 ms
       * bias has already completed, so source is the exact boundary value. */
      r->phase = FP_POSE_DEPTH_RELEASE;
      r->start = r->sample_at + 500; /* now-sample_at>500 proves no overflow */
      r->bias_start = r->source;
    }
    if (r->phase == FP_POSE_DEPTH_RELEASE) {
      float e = ease(r->start, now);
      r->value = r->bias_start * (1 - e);
      if (e == 1) {
        r->phase = FP_POSE_DEPTH_EMPTY;
        r->value = 0;
      }
    }
  } else if (r->phase == FP_POSE_DEPTH_RECOVER) {
    float e = ease(r->start, now);
    r->value = r->source + r->bias_start * (1 - e);
    if (e == 1)
      r->phase = FP_POSE_DEPTH_LIVE;
  } else if (r->phase == FP_POSE_DEPTH_LIVE) {
    r->value = r->source;
  }
  if (available) {
    bool supported = r->sampled && fresh(r->sample_at, now) &&
                     r->phase != FP_POSE_DEPTH_RELEASE &&
                     r->phase != FP_POSE_DEPTH_EMPTY;
    if (!supported) {
      r->bias_start = hip_weight == 0 ? 0 : r->value - source;
      r->start = now;
      r->phase =
          r->bias_start == 0 ? FP_POSE_DEPTH_LIVE : FP_POSE_DEPTH_RECOVER;
    }
    /* Keeping the existing recovery segment/bias lets fresh native-coupled
     * source changes pass instantly instead of low-passing their cancellation.
     */
    r->source = source;
    r->sample_at = now;
    r->sampled = true;
    r->value = source + (r->phase == FP_POSE_DEPTH_RECOVER
                             ? r->bias_start * (1 - ease(r->start, now))
                             : 0);
  }
  return r->value;
}
static bool same_weapon(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && a->weapon == b->weapon &&
         a->weapon_unit == b->weapon_unit && a->viewmodel == b->viewmodel &&
         a->camera_unit == b->camera_unit &&
         a->weapon_descriptor == b->weapon_descriptor &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, 20);
}
static void transition_reset(FpPosePolicy *p) {
  p->weight = p->target = p->start_weight = 0;
  p->ads_weight = p->start_ads_weight = 0;
  p->transition_ready = p->depth_ready = p->applied_ready = false;
  p->applied_at = p->depth_at = 0;
  p->hip_depth = 0;
  p->depth_response = (FpPoseDepthResponse){0};
}
/* Sample the previous segment before reversing it. A newly attached rig starts
 * at exactly zero; eligible handoff seeds only the previous applied scalar. */
static void transition_update(FpPosePolicy *p, bool ads, uint64_t now) {
  float target = ads ? 0 : 1;
  if (!p->transition_ready) {
    p->start = now;
    p->start_weight = p->weight;
    p->start_ads_weight = p->ads_weight;
    p->target = target;
    p->transition_ready = true;
  }
  float u = now >= p->start ? (float)(now - p->start) / 150 : 0;
  float t = fminf(1, fmaxf(0, u)), e = t * t * (3 - 2 * t);
  p->weight = p->start_weight + (p->target - p->start_weight) * e;
  p->ads_weight =
      p->start_ads_weight + (1 - p->target - p->start_ads_weight) * e;
  if (target != p->target) {
    p->start_weight = p->weight;
    p->start_ads_weight = p->ads_weight;
    p->target = target;
    p->start = now;
  }
}
static bool fail(char *out, size_t size, const char *why) {
  if (out && size)
    snprintf(out, size, "%s", why);
  return false;
}
static bool owned(FpPosePolicy *p, FpSnapshot *s, FpGraph *g) {
  uint32_t sight = 0;
  return fp_policy_dispatch(&p->memory, &p->lease) &&
         fp_snapshot(&p->memory, true, s, NULL, 0) &&
         fp_same_player(&p->origin, s) && s->weapon == p->origin.weapon &&
         s->weapon_descriptor == p->origin.weapon_descriptor &&
         !memcmp(s->weapon_bytes, p->origin.weapon_bytes, 20) &&
         s->viewmodel == p->origin.viewmodel && p->weapon_object &&
         fp_unit_object(&p->memory, s->weapon_unit) == p->weapon_object &&
         fp_u32(&p->memory, s->request + 0x18, &sight) && sight != 0 &&
         s->kind == 6 && s->request == p->request &&
         s->request_weapon == s->weapon && !s->mask && !s->controls[5] &&
         !s->seated && fp_rig_attachments(&p->memory, s, g) &&
         g->object == p->object;
}
static bool sole(const FpSnapshot *s) {
  return ((s->tail + 32 - s->head) % 32) == 1 &&
         s->request == s->context + 0x200 + (uintptr_t)s->head * 0xe8;
}
static bool native_vector(const FpMemory *m, const float *input, float out[4]) {
  return fp_read(m, (uintptr_t)input, out, 16) && isfinite(out[0]) &&
         isfinite(out[1]) && isfinite(out[2]);
}
static bool motion_parent(FpPosePolicy *p, const FpGraph *g,
                          float translation[3], FpQuat *rotation,
                          const FpPlacement *authored, float motion_weight) {
  float update[6];
  FpQuat view;
  float matrix[16];
  FpQuat parent;
  if (!fp_read(&p->memory, g->worlds + 4 * 64, matrix, 64) ||
      !fp_q_matrix(matrix, &parent))
    return false;
  memset(p->motion, 0, sizeof p->motion);
  p->view = parent;
  if (p->camera && fp_camera_policy_read_frame(p->camera, update, &view)) {
    memcpy(p->motion, update, sizeof update);
    p->view = view;
  }
  float local[3] = {p->motion[0] * motion_weight, p->motion[1] * motion_weight,
                    p->motion[2] * motion_weight},
        world[3];
  for (unsigned i = 0; i < 3; i++)
    if (!isfinite(local[i]) || fabsf(local[i]) > .15f)
      return false;
  fp_q_rotate(p->view, local, world);
  fp_q_rotate(fp_q_inverse(parent), world, translation);
  const float radians = .01745329251994329577f;
  float pitch = (float)authored->pitch * radians + p->motion[3] * motion_weight,
        yaw = (float)authored->yaw * radians + p->motion[4] * motion_weight,
        roll = (float)authored->roll * radians + p->motion[5] * motion_weight;
  if (!isfinite(pitch) || !isfinite(yaw) || !isfinite(roll) ||
      fabsf(pitch) > 3.14159265f || fabsf(yaw) > 3.14159265f ||
      fabsf(roll) > 3.14159265f)
    return false;
  FpQuat camera_rotation = fp_q_mul(
      fp_q_look(yaw, pitch), (FpQuat){0, sinf(roll / 2), 0, cosf(roll / 2)});
  FpQuat frame = fp_q_mul(fp_q_inverse(parent), p->view);
  *rotation = fp_q_mul(fp_q_mul(frame, camera_rotation), fp_q_inverse(frame));
  if (rotation->w < 0)
    *rotation =
        (FpQuat){-rotation->x, -rotation->y, -rotation->z, -rotation->w};
  float angle = 2 * acosf(fmaxf(-1, fminf(1, rotation->w))),
        max_angle = 2.0943951023931954923f - .00001f;
  if (angle > max_angle) {
    float length = sqrtf(rotation->x * rotation->x + rotation->y * rotation->y +
                         rotation->z * rotation->z);
    if (length < .00001f)
      return false;
    float scale = sinf(max_angle / 2) / length;
    rotation->x *= scale;
    rotation->y *= scale;
    rotation->z *= scale;
    rotation->w = cosf(max_angle / 2);
  }
  return fp_q_valid(*rotation);
}
static const float *getter_call_epoch(uint64_t generation, uintptr_t caller,
                                      uintptr_t stack, uint32_t unit,
                                      uint32_t node) {
  UnitGetter original =
      atomic_load_explicit(&original_getter, memory_order_acquire);
  FpPosePolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return NULL;
  if (!p || !atomic_load(&p->lease.armed) || node != 50)
    return original(unit, node);
#ifdef TFP_POLICY_TEST
  if (test_before_lock)
    test_before_lock(0);
#endif
  AcquireSRWLockExclusive(&p->lock);
  if (p != atomic_load_explicit(&active, memory_order_acquire) ||
      generation !=
          atomic_load_explicit(&callback_generation, memory_order_acquire) ||
      !atomic_load(&p->lease.armed) || caller != p->memory.game + 0x1287a44 ||
      unit != p->origin.viewmodel) {
    const float *result = original(unit, node);
    ReleaseSRWLockExclusive(&p->lock);
    return result;
  }
  const float *result = original(unit, node);
  FpSnapshot s;
  FpGraph g;
  p->depth.observation.ready = false;
  p->depth.carrier_ready = false;
  if (atomic_load(&p->lease.armed) && owned(p, &s, &g))
    fp_depth_observe(&p->depth, &p->memory, p->request, stack, p->token,
                     GetCurrentThreadId());
  ReleaseSRWLockExclusive(&p->lock);
  return result;
}
static void position_call_epoch(uint64_t generation, uintptr_t caller,
                                uintptr_t stack, uint32_t unit, uint32_t node,
                                const float *input) {
  UnitSetter original =
      atomic_load_explicit(&original_position, memory_order_acquire);
  FpPosePolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return;
  if (!p || !atomic_load_explicit(&p->lease.armed, memory_order_acquire) ||
      node != 5) {
    original(unit, node, input);
    return;
  }
#ifdef TFP_POLICY_TEST
  if (test_before_lock)
    test_before_lock(1);
#endif
  AcquireSRWLockExclusive(&p->lock);
  if (p != atomic_load_explicit(&active, memory_order_acquire) ||
      generation !=
          atomic_load_explicit(&callback_generation, memory_order_acquire) ||
      !atomic_load(&p->lease.armed) || unit != p->origin.viewmodel ||
      caller != p->memory.game + 0x1288424) {
    original(unit, node, input);
    ReleaseSRWLockExclusive(&p->lock);
    return;
  }
  FpSnapshot s;
  FpGraph g;
  float native[4], shifted[4];
  bool change = false;
  if (atomic_load_explicit(&p->lease.armed, memory_order_acquire) &&
      owned(p, &s, &g) && native_vector(&p->memory, input, native)) {
    uint64_t now = pose_now();
    transition_update(p, s.full_ads, now);
    float motion[3] = {0};
    FpQuat rotation;
    FpPlacement neutral = {0};
    motion_parent(p, &g, motion, &rotation, &neutral, p->weight);
    memcpy(shifted, native, 16);
    float placement[3] = {(float)p->placement.x, (float)p->placement.y,
                          (float)p->placement.z};
    float ads[3] = {(float)p->ads.x, (float)p->ads.y, (float)p->ads.z};
    for (unsigned i = 0; i < 3; i++)
      shifted[i] +=
          placement[i] * p->weight + motion[i] + ads[i] * p->ads_weight;
    /* The depth helper returns zero for both refusal and valid zero depth.
     * Its pending observation is published only on a successful calculation,
     * so clear that publication before asking for this frame's hip sample. */
    p->depth.pending = (FpDepthObservation){0};
    float correction = fp_depth_delta(&p->depth, &p->memory, &s, stack,
                                      p->token, GetCurrentThreadId(), now);
    bool available = !s.full_ads && p->depth.pending.ready &&
                     p->depth.pending.request == p->request &&
                     p->depth.pending.token == p->token &&
                     p->depth.pending.stack == stack &&
                     p->depth.pending.thread == GetCurrentThreadId() &&
                     isfinite(correction) && fabsf(correction) <= 1;
    correction = depth_response(&p->depth_response, now, available, correction,
                                p->weight) *
                 p->weight;
    p->hip_depth = p->depth_response.source;
    p->depth_at = p->depth_response.sample_at;
    p->depth_ready = p->depth_response.sampled && fresh(p->depth_at, now);
    p->depth.carrier_ready = false;
    shifted[1] += correction;
    FpSnapshot after;
    FpGraph after_graph;
    change = owned(p, &after, &after_graph) && after.request == s.request &&
             after.full_ads == s.full_ads &&
             !memcmp(&after_graph, &g, sizeof g);
    if (!change)
      p->depth.carrier_ready = false;
    if (change) {
      if (correction != 0)
        p->depth.applied++;
      p->applied_owner = after;
      p->applied_rig = after_graph;
      p->applied_at = now;
      p->applied_ready = true;
      memcpy(p->baseline, native, 16);
      p->shifted = memcmp(shifted, native, 16) != 0;
      p->positions++;
    }
  }
  if (!change) {
    p->shifted = false;
    transition_reset(p);
  }
  original(unit, node, change ? shifted : input);
  ReleaseSRWLockExclusive(&p->lock);
}
static void rotation_call_epoch(uint64_t generation, uintptr_t caller,
                                uint32_t unit, uint32_t node,
                                const float *input) {
  UnitSetter original =
      atomic_load_explicit(&original_rotation, memory_order_acquire);
  FpPosePolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return;
  if (!p || !atomic_load_explicit(&p->lease.armed, memory_order_acquire) ||
      node != 5) {
    original(unit, node, input);
    return;
  }
#ifdef TFP_POLICY_TEST
  if (test_before_lock)
    test_before_lock(2);
#endif
  AcquireSRWLockExclusive(&p->lock);
  if (p != atomic_load_explicit(&active, memory_order_acquire) ||
      generation !=
          atomic_load_explicit(&callback_generation, memory_order_acquire) ||
      !atomic_load(&p->lease.armed) || unit != p->origin.viewmodel ||
      caller != p->memory.game + 0x12890df) {
    original(unit, node, input);
    ReleaseSRWLockExclusive(&p->lock);
    return;
  }
  FpSnapshot s;
  FpGraph g;
  FpQuat native, delta, shifted;
  bool change = false;
  float coupling = 1;
  if (atomic_load_explicit(&p->lease.armed, memory_order_acquire) &&
      owned(p, &s, &g) && sole(&s) &&
      fp_read(&p->memory, s.request + 0x98, &coupling, 4) && coupling == 0 &&
      fp_read(&p->memory, (uintptr_t)input, &native, 16) &&
      fp_q_valid(native)) {
    /* Use the position callback's last qualified scalar for the whole branch.
     * Native rotation/coupling remains the baseline on every evaluation. */
    if (p->applied_ready && fresh(p->applied_at, pose_now())) {
      FpPlacement authored = {0};
      authored.yaw = p->placement.yaw * p->weight + p->ads.yaw * p->ads_weight;
      authored.pitch =
          p->placement.pitch * p->weight + p->ads.pitch * p->ads_weight;
      authored.roll =
          p->placement.roll * p->weight + p->ads.roll * p->ads_weight;
      float translation[3];
      if (motion_parent(p, &g, translation, &delta, &authored, p->weight)) {
        shifted = fp_q_mul(delta, native);
        FpSnapshot after;
        FpGraph after_graph;
        change = fp_q_valid(shifted) && owned(p, &after, &after_graph) &&
                 after.request == s.request && after.full_ads == s.full_ads;
        if (change) {
          memcpy(p->rotation_baseline, &native, 16);
          p->rotated = memcmp(&shifted, &native, 16) != 0;
          p->rotations++;
        }
      }
    }
  }
  if (!change)
    p->rotated = false;
  original(unit, node, change ? (const float *)&shifted : input);
  ReleaseSRWLockExclusive(&p->lock);
}
#ifdef TFP_POLICY_TEST
static const float *getter_call(uintptr_t caller, uintptr_t stack,
                                uint32_t unit, uint32_t node) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  return getter_call_epoch(generation, caller, stack, unit, node);
}
static void position_call_stack(uintptr_t caller, uintptr_t stack,
                                uint32_t unit, uint32_t node,
                                const float *input) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  position_call_epoch(generation, caller, stack, unit, node, input);
}
static void position_call(uintptr_t caller, uint32_t unit, uint32_t node,
                          const float *input) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  position_call_epoch(generation, caller, 0, unit, node, input);
}
static void rotation_call(uintptr_t caller, uint32_t unit, uint32_t node,
                          const float *input) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  rotation_call_epoch(generation, caller, unit, node, input);
}
#endif
static __declspec(noinline) void position_setter(uint32_t unit, uint32_t node,
                                                 const float *input) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  position_call_epoch(generation, (uintptr_t)_ReturnAddress(),
                      (uintptr_t)_AddressOfReturnAddress() + 8, unit, node,
                      input);
}
static __declspec(noinline) const float *position_getter(uint32_t unit,
                                                         uint32_t node) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  return getter_call_epoch(generation, (uintptr_t)_ReturnAddress(),
                           (uintptr_t)_AddressOfReturnAddress() + 8, unit,
                           node);
}
static __declspec(noinline) void rotation_setter(uint32_t unit, uint32_t node,
                                                 const float *input) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  rotation_call_epoch(generation, (uintptr_t)_ReturnAddress(), unit, node,
                      input);
}
void fp_pose_policy_init(FpPosePolicy *p) {
  memset(p, 0, sizeof *p);
  InitializeSRWLock(&p->lock);
  fp_dispatch_init(&p->lease);
  p->placement = (FpPlacement){.15, 0, -.12, 0, 0, 0};
  p->view = fp_q_identity();
}
static bool open_locked(FpPosePolicy *p, const FpMemory *m, const FpSnapshot *s,
                        FpCameraPolicy *camera, char *error, size_t size) {
  if (!p || !m || !s || p->token || atomic_load(&active))
    return fail(error, size, "Arms/weapon placement owner is busy");
  if (!s->safe || s->kind != 4 || s->aiming || s->full_ads || !s->weapon_ready)
    return fail(error, size,
                "Release native aiming before arms placement starts");
  if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
      !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE))
    return fail(error, size, "Arms placement build differs");
  for (unsigned i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    const FpNativeSite *site = fp_native_sites + i;
    if (!fp_bytes(m, (site->exe ? m->exe : m->game) + site->rva, site->hex))
      return fail(error, size, site->name);
  }
  FpGraph rig;
  uintptr_t root = 0, api = 0, position = 0, rotation = 0, getter = 0;
  uintptr_t weapon_object = fp_unit_object(m, s->weapon_unit);
  if (!weapon_object || !fp_graph_rig(m, s->viewmodel, &rig) ||
      !fp_pointer(m, m->game + 0x3326308, &root) ||
      !fp_pointer(m, root + 0x18, &api) ||
      !fp_pointer(m, api + 0x38, &position) || position != m->exe + 0x1fc720 ||
      !fp_pointer(m, api + 0x40, &rotation) || rotation != m->exe + 0x1fc7a0 ||
      !fp_pointer(m, api, &getter) || getter != m->exe + 0x1fc450)
    return fail(error, size, "Arms rig or native dispatch differs");
  HMODULE pinned = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_PIN,
                          (LPCWSTR)(uintptr_t)position_setter, &pinned))
    return fail(error, size, "Cannot retain arms callback lifetime");
  p->memory = *m;
  p->origin = *s;
  p->camera = camera;
  p->object = rig.object;
  p->weapon_object = weapon_object;
  p->request = s->context + 0x200 + (uintptr_t)s->tail * 0xe8;
  p->platform =
      (FpSlotPlatform){m->exe, GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                              "wine_get_version") != NULL};
  uint64_t now = pose_now();
  FpPoseCarry carry = p->carry;
  p->carry.valid = false;
  transition_reset(p);
  if (carry.valid && fresh(carry.applied_at, now) &&
      same_weapon(&carry.owner, s) && !memcmp(&carry.rig, &rig, sizeof rig) &&
      carry.weapon_object == weapon_object && isfinite(carry.weight) &&
      carry.weight >= 0 && carry.weight <= 1 && isfinite(carry.ads_weight) &&
      carry.ads_weight >= 0 && carry.ads_weight <= 1 &&
      carry.weight + carry.ads_weight <= 1.00001f) {
    p->weight = p->start_weight = carry.weight;
    p->ads_weight = p->start_ads_weight = carry.ads_weight;
    if (depth_response_valid(&carry.depth_response, now)) {
      p->depth_response = carry.depth_response;
      p->hip_depth = carry.depth_response.source;
      p->depth_at = carry.depth_response.sample_at;
      p->depth_ready = carry.depth_response.sampled && fresh(p->depth_at, now);
    }
  }
  p->start = now;
  p->shifted = p->rotated = false;
  memset(&p->depth, 0, sizeof p->depth);
  atomic_fetch_add_explicit(&callback_generation, 1, memory_order_acq_rel);
  atomic_store(&original_getter, (UnitGetter)getter);
  atomic_store_explicit(&original_position, (UnitSetter)position,
                        memory_order_release);
  atomic_store_explicit(&original_rotation, (UnitSetter)rotation,
                        memory_order_release);
  atomic_store_explicit(&active, p, memory_order_release);
  FpDispatchSlot slots[] = {
      {api + 0x38, position, (uintptr_t)position_setter, false},
      {api + 0x40, rotation, (uintptr_t)rotation_setter, false},
      {api, getter, (uintptr_t)position_getter, false}};
  unsigned result = fp_dispatch_begin(&p->lease, slots, 3, fp_slot_read,
                                      fp_slot_cas, &p->platform, &p->token);
  if (!result)
    atomic_store(&active, NULL);
  return result == 1 ? true
                     : fail(error, size,
                            result == 2 ? "Arms placement restoration pending"
                                        : "Arms placement slots changed");
}
bool fp_pose_policy_open(FpPosePolicy *p, const FpMemory *m,
                         const FpSnapshot *s, FpCameraPolicy *camera,
                         char *error, size_t size) {
  if (!p || !m || !s)
    return fail(error, size, "Missing arms placement owner");
  AcquireSRWLockExclusive(&p->lock);
  bool result = open_locked(p, m, s, camera, error, size);
  ReleaseSRWLockExclusive(&p->lock);
  return result;
}
bool fp_pose_policy_place(FpPosePolicy *p, uint64_t token,
                          const FpPlacement *placement,
                          const FpPlacement *ads) {
  if (!p || !token || !fp_placement_valid(placement) ||
      !fp_placement_valid(ads))
    return false;
  AcquireSRWLockExclusive(&p->lock);
  bool owned = p->token == token && fp_dispatch_owns(&p->lease, token);
  if (owned) {
    p->placement = *placement;
    p->ads = *ads;
  }
  ReleaseSRWLockExclusive(&p->lock);
  return owned;
}
static bool close_policy(FpPosePolicy *p, bool handoff) {
  if (!p)
    return true;
  AcquireSRWLockExclusive(&p->lock);
  if (!p->token) {
    if (!handoff)
      p->carry.valid = false;
    ReleaseSRWLockExclusive(&p->lock);
    return true;
  }
  atomic_fetch_add_explicit(&callback_generation, 1, memory_order_acq_rel);
  if (!handoff || atomic_load(&p->lease.armed))
    p->carry.valid = false;
  FpSnapshot s;
  FpGraph g;
  if (handoff && p->applied_ready && fresh(p->applied_at, pose_now()) &&
      fp_policy_dispatch(&p->memory, &p->lease) &&
      fp_snapshot(&p->memory, true, &s, NULL, 0) &&
      same_weapon(&p->applied_owner, &s) && s.safe && !s.seated && !s.mask &&
      !s.controls[5] && s.kind == 4 &&
      fp_graph_rig(&p->memory, s.viewmodel, &g) &&
      !memcmp(&p->applied_rig, &g, sizeof g) &&
      fp_unit_object(&p->memory, s.weapon_unit) == p->weapon_object) {
    p->carry = (FpPoseCarry){.owner = s,
                             .rig = g,
                             .weapon_object = p->weapon_object,
                             .applied_at = p->applied_at,
                             .depth_at = p->depth_at,
                             .weight = p->weight,
                             .ads_weight = p->ads_weight,
                             .hip_depth = p->hip_depth,
                             .valid = true,
                             .depth_ready = p->depth_ready,
                             .depth_response = p->depth_response};
  }
  atomic_store_explicit(&p->lease.armed, false, memory_order_release);
  /* Restore only a still-owned current branch on explicit hard close. A
   * retired request owns no pose, and root0 is always native passthrough. */
  if (!handoff && (p->shifted || p->rotated) && owned(p, &s, &g)) {
    if (p->shifted) {
      UnitSetter setter = atomic_load(&original_position);
      setter(s.viewmodel, 5, p->baseline);
      p->restored++;
    }
    if (p->rotated && sole(&s)) {
      UnitSetter setter = atomic_load(&original_rotation);
      setter(s.viewmodel, 5, p->rotation_baseline);
      p->restored++;
    }
  }
  p->shifted = p->rotated = false;
  p->depth.carrier_ready = false;
  transition_reset(p);
  bool done = fp_dispatch_end(&p->lease, p->token, fp_slot_cas, &p->platform);
  if (done) {
    p->token = 0;
    if (atomic_load(&active) == p)
      atomic_store(&active, NULL);
  }
  ReleaseSRWLockExclusive(&p->lock);
  return done;
}
bool fp_pose_policy_close(FpPosePolicy *p) { return close_policy(p, false); }
bool fp_pose_policy_handoff(FpPosePolicy *p) { return close_policy(p, true); }
