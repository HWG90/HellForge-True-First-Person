#include "near_policy.h"
#include "native_contract.h"
#include "policy_owner.h"
#include "profile.h"
#include <intrin.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
typedef void (*NearSetter)(void *, float);
static _Atomic(FpNearPolicy *) active;
static _Atomic(NearSetter) native_setter;
static _Atomic uint64_t callback_generation;
static bool fail(char *out, size_t size, const char *why) {
  if (out && size)
    snprintf(out, size, "%s", why);
  return false;
}
static bool meters(float value) {
  return isfinite(value) && value >= 0 && value <= 1;
}
static bool native_domain(const FpSnapshot *s) {
  return s->local_request && s->kind == 6 && s->safe && !s->full_ads &&
         !s->seated && !s->mask && !s->controls[5];
}
/* -1 unreadable, 0 positively changed, 1 the same qualified camera graph. */
static int camera_graph(const FpNearPolicy *p, uintptr_t camera) {
  uintptr_t graph = 0;
  uint32_t node = UINT32_MAX;
  if (!fp_read(&p->memory, camera + 0x18, &graph, sizeof graph) ||
      !fp_u32(&p->memory, camera + 0x20, &node))
    return -1;
  return graph == p->camera_object + 0x60 && node == 0;
}
static bool owner(FpNearPolicy *p, FpSnapshot *s) {
  return fp_policy_dispatch(&p->memory, &p->lease) &&
         fp_snapshot(&p->memory, false, s, NULL, 0) &&
         fp_same_player(&p->origin, s) &&
         s->camera_unit == p->origin.camera_unit && native_domain(s) &&
         fp_unit_object(&p->memory, s->body) == p->body_object &&
         fp_unit_object(&p->memory, s->camera_unit) == p->camera_object &&
         camera_graph(p, s->camera) == 1 &&
         fp_policy_dispatch(&p->memory, &p->lease);
}
static void near_call(uintptr_t caller, void *camera, float value) {
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  NearSetter original =
      atomic_load_explicit(&native_setter, memory_order_acquire);
  FpNearPolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return;
  if (!p) {
    original(camera, value);
    return;
  }
#ifdef TFP_NEAR_TEST
  extern void fp_near_before_lock(void);
  fp_near_before_lock();
#endif
  AcquireSRWLockExclusive(&p->lock);
  float applied = value;
  FpSnapshot s;
  /* Every forwarded native write to this Camera supersedes our last property
   * write, including ADS/actions, a foreign caller and cached late callbacks.
   * Dynamic native FPS near values are not identified by one literal float. */
  bool same_camera = p == atomic_load_explicit(&active, memory_order_acquire) &&
                     (uintptr_t)camera == p->origin.camera;
  if (same_camera)
    p->property_owned = false;
  if (same_camera &&
      generation ==
          atomic_load_explicit(&callback_generation, memory_order_acquire) &&
      atomic_load_explicit(&p->lease.armed, memory_order_acquire) &&
      caller >= p->memory.game + 0x1000 &&
      caller < p->memory.game + FP_GAME_SIZE && meters(value) && value > 0 &&
      owner(p, &s)) {
    float target = s.aiming || s.shoulder_aim ? p->aim : p->hip;
    if (meters(target) && target > 0 && target != value) {
      applied = target;
      p->baseline = value;
      p->written = target;
      p->property_owner = s;
      p->property_owned = true;
    }
  }
  original(camera, applied);
  ReleaseSRWLockExclusive(&p->lock);
}
static __declspec(noinline) void near_setter(void *camera, float value) {
  near_call((uintptr_t)_ReturnAddress(), camera, value);
}
void fp_near_policy_init(FpNearPolicy *p) {
  memset(p, 0, sizeof *p);
  InitializeSRWLock(&p->lock);
  fp_dispatch_init(&p->lease);
  p->hip = .05f;
}
bool fp_near_policy_open(FpNearPolicy *p, const FpMemory *m,
                         const FpSnapshot *s, char *error, size_t size) {
  if (!p || !m || !s || p->token || atomic_load(&active))
    return fail(error, size, "Near clip owner is busy");
  if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
      !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE))
    return fail(error, size, "Near clip build differs");
  for (unsigned i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    const FpNativeSite *site = fp_native_sites + i;
    if (!fp_bytes(m, (site->exe ? m->exe : m->game) + site->rva, site->hex))
      return fail(error, size, site->name);
  }
  uintptr_t root = 0, api = 0, setter = 0, getter = 0;
  if (!fp_pointer(m, m->game + 0x3326308, &root) ||
      !fp_pointer(m, root + 0x20, &api) ||
      !fp_pointer(m, api + 0x68, &setter) || setter != m->exe + 0x30d960 ||
      !fp_pointer(m, api + 0x58, &getter) || getter != m->exe + 0x30d940)
    return fail(error, size, "Near clip dispatch differs");
  uintptr_t body_object = fp_unit_object(m, s->body),
            camera_object = fp_unit_object(m, s->camera_unit), graph = 0;
  uint32_t node = UINT32_MAX;
  if (!body_object || !camera_object ||
      !fp_pointer(m, s->camera + 0x18, &graph) ||
      graph != camera_object + 0x60 ||
      !fp_u32(m, s->camera + 0x20, &node) || node != 0)
    return fail(error, size, "Near clip camera ownership differs");
  HMODULE pinned = NULL;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_PIN,
                          (LPCWSTR)(uintptr_t)near_setter, &pinned))
    return fail(error, size, "Cannot retain near-clip callback lifetime");
  AcquireSRWLockExclusive(&p->lock);
  p->memory = *m;
  p->origin = *s;
  p->slot = api + 0x68;
  p->body_object = body_object;
  p->camera_object = camera_object;
  p->property_owned = false;
  p->platform =
      (FpSlotPlatform){m->exe, GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                              "wine_get_version") != NULL};
  atomic_fetch_add(&callback_generation, 1);
  atomic_store_explicit(&native_setter, (NearSetter)setter,
                        memory_order_release);
  atomic_store_explicit(&active, p, memory_order_release);
  FpDispatchSlot slot = {p->slot, setter, (uintptr_t)near_setter, false};
  unsigned result = fp_dispatch_begin(&p->lease, &slot, 1, fp_slot_read,
                                      fp_slot_cas, &p->platform, &p->token);
  if (!result)
    atomic_store(&active, NULL);
  ReleaseSRWLockExclusive(&p->lock);
  return result == 1 ? true
                     : fail(error, size,
                            result == 2 ? "Near clip restoration pending"
                                        : "Near clip slot changed");
}
bool fp_near_policy_set(FpNearPolicy *p, uint64_t token, float hip, float aim) {
  if (!p || !token || !meters(hip) || !meters(aim))
    return false;
  AcquireSRWLockExclusive(&p->lock);
  bool owned = p->token == token && fp_dispatch_owns(&p->lease, token);
  if (owned) {
    p->hip = hip;
    p->aim = aim;
  }
  ReleaseSRWLockExclusive(&p->lock);
  return owned;
}
bool fp_near_policy_close(FpNearPolicy *p) {
  if (!p || !p->token)
    return true;
  AcquireSRWLockExclusive(&p->lock);
  atomic_fetch_add(&callback_generation, 1);
  atomic_store_explicit(&p->lease.armed, false, memory_order_release);
  uintptr_t slot = 0;
  float current = 0;
  if (p->property_owned) {
    if (!fp_read(&p->memory, p->slot, &slot, sizeof slot)) {
      ReleaseSRWLockExclusive(&p->lock);
      return false;
    }
    if (slot == p->lease.slots[0].replacement) {
      FpSnapshot s;
      if (!fp_snapshot(&p->memory, false, &s, NULL, 0)) {
        ReleaseSRWLockExclusive(&p->lock);
        return false;
      }
      if (fp_same_player(&p->property_owner, &s) &&
          s.camera_unit == p->property_owner.camera_unit &&
          s.request == p->property_owner.request &&
          s.head == p->property_owner.head && s.tail == p->property_owner.tail &&
          s.request_avatar == p->property_owner.request_avatar &&
          s.request_weapon == p->property_owner.request_weapon &&
          native_domain(&s)) {
        uintptr_t body = fp_unit_object(&p->memory, s.body),
                  camera = fp_unit_object(&p->memory, s.camera_unit);
        if (!body || !camera) {
          ReleaseSRWLockExclusive(&p->lock);
          return false;
        }
        if (body == p->body_object && camera == p->camera_object) {
          int graph = camera_graph(p, s.camera);
          if (graph < 0) {
            ReleaseSRWLockExclusive(&p->lock);
            return false;
          }
          if (graph == 1 &&
              (!fp_read(&p->memory, s.camera + 0x28, &current, 4) ||
               !fp_read(&p->memory, p->slot, &slot, sizeof slot))) {
            ReleaseSRWLockExclusive(&p->lock);
            return false;
          }
          if (graph == 1 && slot == p->lease.slots[0].replacement &&
              current == p->written && meters(p->baseline) && p->baseline > 0) {
            NearSetter original = atomic_load(&native_setter);
            original((void *)s.camera, p->baseline);
            if (!fp_read(&p->memory, s.camera + 0x28, &current, 4)) {
              ReleaseSRWLockExclusive(&p->lock);
              return false;
            }
            /* A newer foreign property wins even if it raced the readback. */
          }
        }
      }
    }
  }
  p->property_owned = false;
  bool done = fp_dispatch_end(&p->lease, p->token, fp_slot_cas, &p->platform);
  if (done) {
    p->token = 0;
    if (atomic_load(&active) == p)
      atomic_store(&active, NULL);
  }
  ReleaseSRWLockExclusive(&p->lock);
  return done;
}
