#include "visibility_policy.h"
#include "policy_owner.h"
#include "profile.h"
#include "scene.h"
#include <intrin.h>
#include <stdio.h>
#include <string.h>

/* Original UnitAPI+658, EXE205680: (UnitRef,int visible,bool extra systems).
 * Its queued op15 sets ALL local mesh pass bits. Never turn hide into show:
 * that would expose authored-hidden variants and restore shadow casting. */
typedef void (*VisibilitySetter)(uint32_t, int, bool);
static _Atomic(FpVisibilityPolicy *) active;
static _Atomic(VisibilitySetter) native_setter;
static _Atomic uint64_t callback_generation;
static SRWLOCK publication = SRWLOCK_INIT;

static bool fail(char *out, size_t size, const char *why) {
  if (out && size)
    snprintf(out, size, "%s", why);
  return false;
}
static bool domain(const FpSnapshot *s) {
  return s && s->weapon_ready && s->local_request && s->kind == 6 &&
         !s->seated && !s->controls[5];
}
static bool same(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && a->head == b->head && a->tail == b->tail &&
         a->request == b->request && a->kind == b->kind &&
         a->request_avatar == b->request_avatar &&
         a->request_weapon == b->request_weapon &&
         a->camera_unit == b->camera_unit && a->viewmodel == b->viewmodel &&
         a->weapon == b->weapon && a->weapon_unit == b->weapon_unit &&
         a->weapon_descriptor == b->weapon_descriptor &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, sizeof a->weapon_bytes) &&
         !memcmp(a->controls, b->controls, sizeof a->controls) &&
         !memcmp(a->flags, b->flags, sizeof a->flags);
}
static bool same_weapon(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && a->camera_unit == b->camera_unit &&
         a->viewmodel == b->viewmodel && a->weapon == b->weapon &&
         a->weapon_unit == b->weapon_unit &&
         a->weapon_descriptor == b->weapon_descriptor &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, sizeof a->weapon_bytes);
}
static bool eligible(FpVisibilityPolicy *p, FpSnapshot *s, FpGraph *rig) {
  return fp_policy_dispatch(&p->memory, &p->lease) &&
         fp_snapshot(&p->memory, true, s, NULL, 0) && domain(s) &&
         fp_same_player(&p->origin, s) &&
         s->camera_unit == p->origin.camera_unit &&
         fp_unit_object(&p->memory, s->body) == p->body_object &&
         fp_unit_object(&p->memory, s->camera_unit) == p->camera_object &&
         fp_rig_attachments(&p->memory, s, rig);
}
static bool visit(const FpMemory *m, FpVisibilityUnit *rows, unsigned *count,
                  uint32_t ref, uintptr_t parent, uintptr_t world,
                  unsigned depth) {
  if (depth > 8 || *count == FP_VISIBILITY_UNITS)
    return false;
  uintptr_t object = fp_unit_object(m, ref);
  FpVisibilityUnit row = {.ref = ref, .object = object};
  if (!object || !fp_pointer(m, object + 0x1d0, &row.parent) ||
      row.parent != parent || !fp_pointer(m, object + 0x18, &row.world) ||
      row.world != world || !fp_read(m, object + 0x1d8, &row.child, 8) ||
      !fp_read(m, object + 0x1e0, &row.next, 8))
    return false;
  for (unsigned i = 0; i < *count; i++)
    if (rows[i].ref == ref || rows[i].object == object)
      return false;
  rows[(*count)++] = row;
  uintptr_t child = row.child;
  unsigned siblings = 0;
  while (child) {
    uint32_t child_ref = 0;
    uintptr_t next = 0;
    if (++siblings > 32 || !fp_u32(m, child + 8, &child_ref) ||
        fp_unit_object(m, child_ref) != child ||
        !fp_read(m, child + 0x1e0, &next, 8) ||
        !visit(m, rows, count, child_ref, object, world, depth + 1))
      return false;
    child = next;
  }
  return true;
}
static bool rows_current(const FpMemory *m, const FpVisibilityUnit *rows,
                         unsigned count) {
  if (!count || count > FP_VISIBILITY_UNITS)
    return false;
  for (unsigned i = 0; i < count; i++) {
    const FpVisibilityUnit *r = rows + i;
    uintptr_t parent = 0, world = 0, child = 0, next = 0;
    if (fp_unit_object(m, r->ref) != r->object ||
        !fp_pointer(m, r->object + 0x1d0, &parent) || parent != r->parent ||
        !fp_pointer(m, r->object + 0x18, &world) || world != r->world ||
        !fp_read(m, r->object + 0x1d8, &child, 8) || child != r->child ||
        !fp_read(m, r->object + 0x1e0, &next, 8) || next != r->next)
      return false;
  }
  return true;
}
static bool tree(FpVisibilityPolicy *p, const FpSnapshot *s,
                 const FpGraph *rig, FpVisibilityUnit *rows, unsigned *count) {
  *count = 0;
  return visit(&p->memory, rows, count, s->weapon_unit, rig->object,
               rig->world, 0) && rows_current(&p->memory, rows, *count);
}
static void clear(FpVisibilityPolicy *p) {
  atomic_store_explicit(&p->candidate_count, 0, memory_order_release);
  p->wanted = false;
  p->count = 0;
}
static bool candidate(const FpVisibilityPolicy *p, uint32_t ref) {
  unsigned count =
      atomic_load_explicit(&p->candidate_count, memory_order_acquire);
  if (count > FP_VISIBILITY_UNITS)
    return false;
  for (unsigned i = 0; i < count; i++)
    if (atomic_load_explicit(p->candidate_refs + i, memory_order_relaxed) == ref)
      return true;
  return false;
}
static bool owned_unit(FpVisibilityPolicy *p, uint32_t ref) {
  FpSnapshot s, closing;
  FpGraph rig, closing_rig;
  FpVisibilityUnit fresh[FP_VISIBILITY_UNITS];
  unsigned count = 0, published = p->count;
  if (!p->wanted || !published || published > FP_VISIBILITY_UNITS ||
      !eligible(p, &s, &rig) || !same_weapon(&p->current, &s) ||
      !tree(p, &s, &rig, fresh, &count))
    return false;
  const FpVisibilityUnit *before = NULL, *now = NULL;
  for (unsigned i = 0; i < published; i++)
    if (p->units[i].ref == ref)
      before = p->units + i;
  for (unsigned i = 0; i < count; i++)
    if (fresh[i].ref == ref)
      now = fresh + i;
  if (!before || !now || before->object != now->object ||
      before->parent != now->parent || before->world != now->world)
    return false;
  return eligible(p, &closing, &closing_rig) && same(&s, &closing) &&
         rig.object == closing_rig.object && rig.world == closing_rig.world &&
         rows_current(&p->memory, fresh, count) &&
         fp_snapshot(&p->memory, true, &closing, NULL, 0) && same(&s, &closing) &&
         fp_policy_dispatch(&p->memory, &p->lease);
}
static void visibility_call(uint64_t generation, uintptr_t caller,
                            uint32_t ref, int visible, bool extra) {
  VisibilitySetter original =
      atomic_load_explicit(&native_setter, memory_order_acquire);
  FpVisibilityPolicy *p = atomic_load_explicit(&active, memory_order_acquire);
  if (!original)
    return;
  if (!p) {
    original(ref, visible, extra);
    return;
  }
  /* This prefilter uses only atomics, never mutable owner/slot metadata. A
   * mixed publication can only forward: every suppression is re-proved below. */
  if (visible != 0 || !candidate(p, ref)) {
    atomic_fetch_add_explicit(&p->forwarded, 1, memory_order_relaxed);
    original(ref, visible, extra);
    return;
  }
#ifdef TFP_VISIBILITY_TEST
  extern void fp_visibility_before_lock(void);
  fp_visibility_before_lock();
#endif
  AcquireSRWLockExclusive(&p->lock);
  bool suppress =
      p == atomic_load_explicit(&active, memory_order_acquire) &&
      generation ==
          atomic_load_explicit(&callback_generation, memory_order_acquire) &&
      atomic_load_explicit(&p->lease.armed, memory_order_acquire) &&
      ((caller >= p->memory.game + 0x1000 &&
        caller < p->memory.game + FP_GAME_SIZE) ||
       (caller >= p->memory.exe + 0x1000 &&
        caller < p->memory.exe + FP_EXE_SIZE)) && owned_unit(p, ref);
  if (suppress)
    atomic_fetch_add_explicit(&p->suppressed, 1, memory_order_relaxed);
  else {
    atomic_fetch_add_explicit(&p->forwarded, 1, memory_order_relaxed);
    original(ref, visible, extra);
  }
  ReleaseSRWLockExclusive(&p->lock);
}
static __declspec(noinline) void visibility_setter(uint32_t ref, int visible,
                                                  bool extra) {
  /* Capture before active/original loads or waiting on a lease lock. */
  uint64_t generation =
      atomic_load_explicit(&callback_generation, memory_order_acquire);
  visibility_call(generation, (uintptr_t)_ReturnAddress(), ref, visible, extra);
}
void fp_visibility_policy_init(FpVisibilityPolicy *p) {
  memset(p, 0, sizeof *p);
  InitializeSRWLock(&p->lock);
  fp_dispatch_init(&p->lease);
  atomic_init(&p->candidate_count, 0);
  atomic_init(&p->suppressed, 0);
  atomic_init(&p->forwarded, 0);
  for (unsigned i = 0; i < FP_VISIBILITY_UNITS; i++)
    atomic_init(p->candidate_refs + i, 0);
}
bool fp_visibility_policy_open(FpVisibilityPolicy *p, const FpMemory *m,
                               const FpSnapshot *s, char *error, size_t size) {
  if (!p || !m || !s)
    return fail(error, size, "Missing weapon visibility owner");
  AcquireSRWLockExclusive(&publication);
  AcquireSRWLockExclusive(&p->lock);
  const char *why = NULL;
  uintptr_t root = 0, api = 0, setter = 0, body = 0, camera = 0;
  FpSnapshot fresh;
  if (p->token || atomic_load_explicit(&active, memory_order_acquire))
    why = "Weapon visibility owner is busy";
  else if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
           !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE) ||
           !fp_bytes(m, m->exe + 0x205680,
                     "48895c24084889742410574883ec20410fb6f08bfa8bd9e82482e9ff"
                     "4c8b5018b801000000f0410fc182380017008bc04c6bc858"
                     "43c784118800010015000000") ||
           !fp_bytes(m, m->exe + 0x12f64c,
                     "8b0fe86de2f6ff488bc84885c00f8417f9ffff837f0800488b00"
                     "440fb64f040f95c2440fb6470cff90d0000000"))
    why = "Weapon visibility build or ABI differs";
  else if (!fp_pointer(m, m->game + 0x3326308, &root) ||
           !fp_pointer(m, root + 0x18, &api) ||
           !fp_pointer(m, api + 0x658, &setter) ||
           setter != m->exe + 0x205680)
    why = "Weapon visibility dispatch differs";
  else if ((s->kind != 4 && s->kind != 6) || !s->local_request || s->seated ||
           s->controls[5] || !s->weapon_ready ||
           !fp_snapshot(m, true, &fresh, NULL, 0) || !same(s, &fresh) ||
           !(body = fp_unit_object(m, s->body)) ||
           !(camera = fp_unit_object(m, s->camera_unit)))
    why = "Weapon visibility local owner differs";
  HMODULE pinned = NULL;
  if (!why && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                     GET_MODULE_HANDLE_EX_FLAG_PIN,
                                 (LPCWSTR)(uintptr_t)visibility_setter, &pinned))
    why = "Cannot retain weapon visibility callback lifetime";
  unsigned result = 0;
  if (!why) {
    p->memory = *m;
    p->origin = *s;
    p->slot = api + 0x658;
    p->body_object = body;
    p->camera_object = camera;
    clear(p);
    p->platform =
        (FpSlotPlatform){m->exe, GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                "wine_get_version") != NULL};
    atomic_fetch_add_explicit(&callback_generation, 1, memory_order_acq_rel);
    atomic_store_explicit(&native_setter, (VisibilitySetter)setter,
                          memory_order_release);
    atomic_store_explicit(&active, p, memory_order_release);
    FpDispatchSlot slot = {p->slot, setter, (uintptr_t)visibility_setter, false};
    result = fp_dispatch_begin(&p->lease, &slot, 1, fp_slot_read, fp_slot_cas,
                                &p->platform, &p->token);
    if (!result)
      atomic_store_explicit(&active, NULL, memory_order_release);
    if (result != 1)
      why = result == 2 ? "Weapon visibility restoration pending"
                        : "Weapon visibility slot changed";
  }
  ReleaseSRWLockExclusive(&p->lock);
  ReleaseSRWLockExclusive(&publication);
  return why ? fail(error, size, why) : true;
}
bool fp_visibility_policy_update(FpVisibilityPolicy *p, uint64_t token,
                                 const FpSnapshot *s, bool wanted) {
  if (!p || !token)
    return false;
  AcquireSRWLockExclusive(&p->lock);
  bool owned = p->token == token && fp_dispatch_owns(&p->lease, token) &&
               atomic_load_explicit(&p->lease.armed, memory_order_acquire) &&
               p == atomic_load_explicit(&active, memory_order_acquire) &&
               fp_policy_dispatch(&p->memory, &p->lease);
  if (owned) {
    FpSnapshot fresh, closing;
    FpGraph rig, closing_rig;
    FpVisibilityUnit rows[FP_VISIBILITY_UNITS];
    unsigned count = 0;
    if (wanted && domain(s) && eligible(p, &fresh, &rig) && same(s, &fresh) &&
        tree(p, &fresh, &rig, rows, &count) &&
        eligible(p, &closing, &closing_rig) && same(&fresh, &closing) &&
        rig.object == closing_rig.object && rig.world == closing_rig.world &&
        rows_current(&p->memory, rows, count) &&
        fp_policy_dispatch(&p->memory, &p->lease)) {
      memcpy(p->units, rows, count * sizeof *rows);
      p->count = count;
      p->current = closing;
      p->wanted = true;
      for (unsigned i = 0; i < count; i++)
        atomic_store_explicit(p->candidate_refs + i, rows[i].ref,
                              memory_order_relaxed);
      atomic_store_explicit(&p->candidate_count, count, memory_order_release);
    } else
      clear(p);
  } else if (p->token == token)
    clear(p);
  ReleaseSRWLockExclusive(&p->lock);
  return owned;
}
bool fp_visibility_policy_close(FpVisibilityPolicy *p) {
  if (!p)
    return true;
  AcquireSRWLockExclusive(&publication);
  AcquireSRWLockExclusive(&p->lock);
  bool done = true;
  if (p->token) {
    atomic_fetch_add_explicit(&callback_generation, 1, memory_order_acq_rel);
    clear(p);
    atomic_store_explicit(&p->lease.armed, false, memory_order_release);
    done = fp_dispatch_end(&p->lease, p->token, fp_slot_cas, &p->platform);
    if (done) {
      p->token = 0;
      if (atomic_load_explicit(&active, memory_order_acquire) == p)
        atomic_store_explicit(&active, NULL, memory_order_release);
    }
  }
  ReleaseSRWLockExclusive(&p->lock);
  ReleaseSRWLockExclusive(&publication);
  return done;
}
