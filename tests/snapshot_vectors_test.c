#include "snapshot.h"
#include "vectors/snapshot_vectors.h"
#include <stdio.h>
#include <string.h>

/* Fake game memory made of a scene's records. */
typedef struct Scene { const SnapshotRecord *records; unsigned count; } Scene;
static bool scene_read(void *user, uintptr_t at, void *out, size_t size) {
  const Scene *scene = user;
  for (size_t j = 0; j < size; j++) {
    bool found = false;
    for (unsigned i = 0; i < scene->count && !found; i++) {
      const SnapshotRecord *r = scene->records + i;
      if (at + j >= r->at && at + j - r->at < r->size) {
        ((unsigned char *)out)[j] = r->bytes[at + j - r->at];
        found = true;
      }
    }
    if (!found) return false;
  }
  return true;
}
/* Retail's pointer refusal names the address, which differs between builds; the rest must be equal. */
static bool same_error(const char *c, const char *retail) {
  static const char prefix[] = "Pointer unavailable at0x";
  if (!strncmp(retail, prefix, sizeof prefix - 1)) {
    const char *cv = strstr(c, " (value0x"), *rv = strstr(retail, " (value0x");
    return !strncmp(c, prefix, sizeof prefix - 1) && cv && rv && !strcmp(cv, rv);
  }
  return !strcmp(c, retail);
}
static bool same(const TfpSnapshot *s, bool ok, const char *error, const SnapshotAnswer *a, bool full, uintptr_t context) {
  if (ok != (a->ok != 0)) return false;
  if (!ok) return same_error(error, a->error);
  return s->camera_kind == a->kind && s->head == a->head && s->tail == a->tail &&
         (long long)(s->record - context) == a->record_offset && s->request_avatar == a->request_avatar &&
         s->request_weapon == a->request_weapon && s->avatar == a->avatar && s->avatar_index == a->avatar_index &&
         s->body == a->body && s->viewmodel == a->viewmodel && s->aiming == (a->aiming != 0) &&
         s->shoulder_aim == (a->shoulder_aim != 0) && s->seated == (a->seated != 0) &&
         s->reloading == (a->reloading != 0) && s->controller_active == (a->controller_active != 0) &&
         s->blocked == (a->blocked != 0) && s->safe == (a->safe != 0) &&
         s->weapon_transition == (a->weapon_transition != 0) &&
         s->head_camera_transition == (a->head_camera_transition != 0) &&
         (!full || (s->weapon == a->weapon && s->weapon_unit == a->weapon_unit));
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(SNAPSHOT_VECTOR_COMMIT, commit) || strlen(SNAPSHOT_VECTOR_SHA256) != 64) {
    fprintf(stderr, "snapshot vectors were cut from retail %s, expected %s\n", SNAPSHOT_VECTOR_COMMIT, commit);
    return 2;
  }
  unsigned count = sizeof snapshot_vectors / sizeof *snapshot_vectors, taken = 0, failures = 0;
  for (unsigned i = 0; i < count; i++) {
    const SnapshotVector *v = snapshot_vectors + i;
    Scene scene = {v->records, v->count};
    FpMemory memory = {scene_read, &scene, (uintptr_t)v->base, 0};
    for (int scope = 0; scope < 2; scope++) {
      TfpSnapshot s;
      char error[200] = "";
      memset(&s, 0, sizeof s);
      bool ok = tfp_snapshot(&memory, scope ? TFP_SNAPSHOT_PLAYER : TFP_SNAPSHOT_FULL, &s, error, sizeof error);
      const SnapshotAnswer *a = scope ? &v->player : &v->full;
      if (!same(&s, ok, error, a, !scope, (uintptr_t)v->context)) {
        if (failures++ < 8)
          fprintf(stderr, "snapshot scene %s (%s): C ok=%d error=\"%s\" kind=%u avatar=%u index=%u body=%u rig=%u safe=%d blocked=%d aim=%d shoulder=%d seat=%d reload=%d weapon=%u unit=%u\n"
                          "  retail ok=%d error=\"%s\" kind=%u avatar=%u index=%u body=%u rig=%u safe=%d blocked=%d aim=%d shoulder=%d seat=%d reload=%d weapon=%u unit=%u\n",
                  v->name, scope ? "player" : "full", ok, error, s.camera_kind, s.avatar, s.avatar_index, s.body, s.viewmodel,
                  s.safe, s.blocked, s.aiming, s.shoulder_aim, s.seated, s.reloading, s.weapon, s.weapon_unit, a->ok, a->error,
                  a->kind, a->avatar, a->avatar_index, a->body, a->viewmodel, a->safe, a->blocked, a->aiming,
                  a->shoulder_aim, a->seated, a->reloading, a->weapon, a->weapon_unit);
      }
      taken += ok;
    }
  }
  if (failures) {
    fprintf(stderr, "%u of %u answers differ from retail\n", failures, count * 2);
    return 1;
  }
  printf("Snapshot parity: %u scenes answered as retail's backend.lua (%s), player and full; %u snapshots taken, the rest refused with the same text\n",
         count, SNAPSHOT_VECTOR_COMMIT, taken);
  return 0;
}
