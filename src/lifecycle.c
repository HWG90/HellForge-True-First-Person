/* The game owns request construction and destruction. Reserve the exact queue
 * slot before entry and never exit a successor, another firearm or real ADS. */
#include "lifecycle.h"
#include "profile.h"
#include <stdio.h>
#include <string.h>
static bool fail(char *out, size_t size, const char *why) {
  if (out && size)
    snprintf(out, size, "%s", why);
  return false;
}
static bool firearm(const FpSnapshot *a, const FpSnapshot *b) {
  return fp_same_player(a, b) && a->weapon == b->weapon &&
         a->weapon_descriptor == b->weapon_descriptor &&
         a->viewmodel == b->viewmodel &&
         !memcmp(a->weapon_bytes, b->weapon_bytes, 20);
}
bool fp_native_entry_ready(const FpMemory *m) {
  if (!fp_image(m, m->game, FP_GAME_STAMP, FP_GAME_SIZE) ||
      !fp_image(m, m->exe, FP_EXE_STAMP, FP_EXE_SIZE))
    return false;
  const unsigned sites[] = {0, 1, 9, 10};
  for (unsigned i = 0; i < sizeof sites / sizeof *sites; i++) {
    const FpWitness *s = fp_witnesses + sites[i];
    if (!fp_bytes(m, m->game + s->rva, s->hex))
      return false;
  }
  return true;
}
bool fp_native_begin(const FpMemory *m, const FpSnapshot *expected,
                     FpNativeLease *lease, FpNativeInvoke invoke, void *user,
                     char *error, size_t size) {
  if (!m || !expected || !lease || lease->held || !invoke)
    return fail(error, size, "Native camera lease is busy");
  FpSnapshot s;
  if (!fp_snapshot(m, true, &s, error, size) || !firearm(expected, &s) ||
      !s.safe || s.kind != 4 || s.aiming || s.full_ads)
    return fail(error, size, "Waiting for a released firearm camera");
  if (!fp_native_entry_ready(m))
    return fail(error, size, "Native camera entry code is unavailable");
  bool allowed = false;
  if (!invoke(user, FP_NATIVE_ALLOWED, m, &s, &allowed) || !allowed)
    return fail(error, size, "Weapon does not support native first person");
  FpSnapshot current;
  if (!fp_snapshot(m, true, &current, error, size) || !firearm(&s, &current) ||
      !current.safe || current.kind != 4 || current.aiming ||
      current.full_ads || current.request != s.request ||
      current.tail != s.tail)
    return fail(error, size, "Native camera changed during preflight");
  memset(lease, 0, sizeof *lease);
  lease->origin = current;
  lease->request = current.context + 0x200 + (uintptr_t)current.tail * 0xe8;
  lease->held = true;
  /* Even an interrupted call may have appended the request. The caller must
   * keep this ledger until release positively observes retirement. */
  lease->called = true;
  bool ignored = false;
  bool called = invoke(user, FP_NATIVE_ENTER, m, &current, &ignored);
  FpSnapshot after;
  if (fp_snapshot(m, false, &after, NULL, 0) &&
      fp_same_player(&current, &after)) {
    lease->aim_changed = after.aiming != current.aiming;
    lease->control_changed = memcmp(after.controls, current.controls, 6) != 0;
    if (called && after.kind == 6 && after.request == lease->request &&
        after.request_avatar == current.avatar &&
        after.request_weapon == current.weapon)
      return true;
  }
  return fail(error, size,
              called ? "Native camera entry acknowledgement pending"
                     : "Native camera call was interrupted; cleanup retained");
}
bool fp_native_release(const FpMemory *m, FpNativeLease *lease,
                       FpNativeInvoke invoke, void *user, char *error,
                       size_t size) {
  if (!lease || !lease->held)
    return true;
  FpSnapshot s;
  if (!fp_snapshot(m, false, &s, error, size))
    return false;
  const FpSnapshot *o = &lease->origin;
  if (!fp_same_player(o, &s) || s.kind != 6 || s.request != lease->request ||
      s.request_avatar != o->avatar || s.request_weapon != o->weapon) {
    lease->held = false;
    return true;
  }
  /* A before/after flag change is not proof we own actual aiming input.
   * Native ADS always takes over its own request and cleanup. */
  if (s.aiming || s.full_ads) {
    lease->held = false;
    return true;
  }
  if (!s.safe)
    return fail(error, size, "A native action owns camera cleanup");
  /* Native exit resolves request+8, not the newly wielded firearm. Requiring
   * current-wield equality here strands a raw hip-FPS request after a swap:
   * its native ADS controller is inactive and will not retire it for us.
   * Keep the original outgoing descriptor/Unit proof independently. Missing
   * outgoing resources remain a refusal: native exit has nullable paths that
   * are unsafe under some avatar flags. */
  FpWeaponRecord old;
  if (!fp_weapon_record(m, s.world, o->weapon, &old) ||
      old.descriptor != o->weapon_descriptor || old.unit != o->weapon_unit ||
      memcmp(old.bytes, o->weapon_bytes, sizeof old.bytes) ||
      s.viewmodel != o->viewmodel)
    return fail(error, size, "Waiting for valid outgoing weapon cleanup resources");
  uint32_t request[5];
  FpSnapshot w;
  if (!fp_read(m, s.request, request, sizeof request) ||
      request[3] != o->body ||
      !fp_snapshot(m, false, &w, error, size) ||
      !fp_same_player(&s, &w) || w.request != s.request || w.kind != 6 ||
      w.head != s.head || w.tail != s.tail ||
      w.request_avatar != o->avatar || w.request_weapon != o->weapon ||
      memcmp(w.controls, s.controls, sizeof s.controls) ||
      memcmp(w.flags, s.flags, sizeof s.flags) || !w.safe ||
      fp_unit_object(m, old.unit) != old.object)
    return fail(error, size, "Outgoing native camera changed during cleanup");
  /* Only EXIT uses this copied snapshot and only its context argument is
   * forwarded. It is deliberately not marked weapon_ready or used for entry. */
  w.weapon = old.entity;
  w.weapon_unit = old.unit;
  w.weapon_descriptor = old.descriptor;
  memcpy(w.weapon_bytes, old.bytes, sizeof old.bytes);
  if (!fp_bytes(m, m->game + FP_EXIT, fp_witnesses[2].hex))
    return fail(error, size, "Native camera exit code differs");
  FpWeaponRecord closing_old;
  if (!fp_weapon_record(m, s.world, o->weapon, &closing_old) ||
      closing_old.descriptor != old.descriptor ||
      closing_old.object != old.object || closing_old.unit != old.unit ||
      memcmp(closing_old.bytes, old.bytes, sizeof old.bytes))
    return fail(error, size, "Outgoing weapon changed before native cleanup");
  uint32_t final_request[5], final_queue[2];
  uintptr_t final_context = 0;
  if (!fp_pointer(m, m->game + FP_CAMERA, &final_context) ||
      final_context != s.context ||
      !fp_read(m, s.context + 0x1f8, final_queue, sizeof final_queue) ||
      final_queue[0] != s.head || final_queue[1] != s.tail ||
      !fp_read(m, s.request, final_request, sizeof final_request) ||
      memcmp(final_request, request, sizeof request))
    return fail(error, size, "Outgoing request changed before native cleanup");
  bool ignored = false;
  if (!invoke || !invoke(user, FP_NATIVE_EXIT, m, &w, &ignored))
    return fail(error, size, "Native camera exit acknowledgement pending");
  FpSnapshot after;
  if (!fp_snapshot(m, false, &after, error, size))
    return false;
  if (fp_same_player(o, &after) && after.kind == 6 &&
      after.request == lease->request && after.request_avatar == o->avatar &&
      after.request_weapon == o->weapon)
    return fail(error, size, "Owned native camera request is still present");
  lease->held = false;
  return true;
}
