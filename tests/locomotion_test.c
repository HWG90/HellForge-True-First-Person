/* src/locomotion.c replayed against the retail locomotion.lua's own answers
 * (tests/vectors/locomotion_vectors.h).
 *  - The pure functions: every number is a double in both and must be the same
 *    double, bit for bit; texts and booleans equal.
 *  - The sampler: the same logical scenes, there in retail's memory layout,
 *    here in build 25480438's. Every field of the sample, every refusal text,
 *    and the ordered list of memory reads made outside the player snapshot
 *    (count and hash) must be equal.
 * Built twice by tests/run-vectors-locomotion.sh: with the two open sprint
 * tests switched on (LOCOMOTION_OPEN_CONSTANTS, full comparison with retail),
 * and as shipped, where sprint must be retail's answer narrowed to l3 == 71. */
#include "locomotion.h"
#include "vectors/locomotion_vectors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LOCOMOTION_OPEN_CONSTANTS
#define SPRINT(retail, has_l3, l3) ((retail) != 0)
#else
#define SPRINT(retail, has_l3, l3) ((retail) != 0 && (has_l3) && (l3) == 71)
#endif

static bool same(double c, double retail) {
  if (c != c || retail != retail) return c != c && retail != retail;
  return !memcmp(&c, &retail, sizeof c);
}

/* ---- fake memory, as in action_state_test.c ---- */
typedef struct World {
  const LSImage *image;
  const LSStep *step;
  bool in_snapshot;
  unsigned reads;
  unsigned long long hash;
} World;
static const LSRecord *covering(const LSImage *image, uintptr_t at) {
  for (unsigned i = 0; i < image->count; i++) {
    const LSRecord *r = image->records + i;
    unsigned size = r->bytes ? r->size : 1;
    if (at >= r->at && at - r->at < size) return r;
  }
  return NULL;
}
static bool replaced(const LSImage *image, unsigned long long at) {
  for (unsigned i = 0; i < image->count; i++)
    if (image->records[i].at == at) return true;
  return false;
}
static bool world_read(void *user, uintptr_t at, void *out, size_t size) {
  World *w = user;
  if (!w->in_snapshot) {
    unsigned long long words[2] = {(unsigned long long)at, (unsigned long long)size};
    w->reads++;
    for (int t = 0; t < w->step->trigger_count; t++)
      if (w->step->triggers[t].read == w->reads) w->image = ls_images + w->step->triggers[t].image;
    for (int k = 0; k < 2; k++)
      for (int b = 0; b < 8; b++) w->hash = (w->hash ^ ((words[k] >> (b * 8)) & 0xff)) * 0x100000001b3ULL;
  }
  while (size) {
    const LSRecord *r = covering(w->image, at);
    if (!r && w->image->base >= 0) {
      r = covering(ls_images + w->image->base, at);
      if (r && replaced(w->image, r->at)) r = NULL;
    }
    if (!r || !r->bytes) return false;
    size_t from = at - r->at, n = r->size - from < size ? r->size - from : size;
    memcpy(out, r->bytes + from, n);
    out = (unsigned char *)out + n;
    at += n;
    size -= n;
  }
  return true;
}
/* The backend's four methods over the fake memory; the player snapshot's reads are not listed or counted. */
typedef struct Host { World *world; const FpMemory *memory; TfpActionStateReader *reader; TfpActionStateBackend actions; } Host;
static bool host_player_snapshot(void *user, TfpSnapshot *out, char *error, size_t error_size) {
  Host *h = user;
  h->world->in_snapshot = true;
  bool ok = tfp_snapshot(h->memory, TFP_SNAPSHOT_PLAYER, out, error, error_size);
  h->world->in_snapshot = false;
  return ok;
}
static bool host_alignment_context(void *user, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index,
                                   TfpAlignmentContext *out, char *error, size_t error_size) {
  return tfp_alignment_context(((Host *)user)->memory, viewmodel, avatar_owner, avatar_index, out, error, error_size);
}
static bool host_alignment_executable(void *user, uintptr_t *exe, char *error, size_t error_size) {
  return tfp_alignment_executable(((Host *)user)->memory, exe, error, error_size);
}
static bool host_action_state(void *user, const TfpSnapshot *s, TfpActionState *out) {
  Host *h = user;
  return tfp_action_state_sample(h->reader, &h->actions, s, NULL, 0, out, NULL, 0);
}
static bool host_on_foot(void *user, const TfpSnapshot *s, const TfpActionState *action) {
  return tfp_on_foot(((Host *)user)->memory, s, action);
}
static bool same_error(const char *c, const char *retail) {
  static const char prefix[] = "Pointer unavailable at0x";
  if (!strncmp(retail, prefix, sizeof prefix - 1)) {
    const char *cv = strstr(c, " (value0x"), *rv = strstr(retail, " (value0x");
    return !strncmp(c, prefix, sizeof prefix - 1) && cv && rv && !strcmp(cv, rv);
  }
  return !strcmp(c, retail);
}

static unsigned pure(unsigned *numbers) {
  unsigned count = sizeof lv_pure / sizeof *lv_pure, failures = 0;
  for (unsigned i = 0; i < count; i++) {
    const LVPure *v = lv_pure + i;
    char what[240] = "";
    bool bad = false;
    if (v->op == LV_SPRINTING) {
      const LVAction *av = lv_actions + v->action;
      const LVFlags *fv = lv_flags + v->flags;
      TfpActionState action;
      memset(&action, 0, sizeof action);
      action.resource_match = av->match != 0;
      action.layer_count = (uint32_t)av->count;
      static const int layers[3] = {1, 3, 14};
      for (int k = 0; k < 3; k++) { action.has[layers[k]] = av->has[k] != 0; action.states[layers[k]] = av->state[k]; }
      bool got = tfp_locomotion_sprinting(av->table ? &action : NULL, v->verified != 0, fv->size >= 0 ? fv->bytes : NULL,
                                          fv->size >= 0 ? (size_t)fv->size : 0);
      if (got != SPRINT(v->ok, av->has[1], av->state[1])) { bad = true; snprintf(what, sizeof what, "sprinting: C %d, retail %d", got, v->ok); }
    } else {
      const LVValue *vv = lv_values + v->value;
      TfpLocomotionValue value = {vv->v[0], vv->v[1], vv->v[2], vv->v[3], vv->v[4], vv->v[5], vv->present};
      const char *error = "not cleared";
      double out[7] = {0};
      bool ok;
      if (v->op == LV_ROTATION) {
        ok = tfp_locomotion_rotation_to_parent(&value, lv_matrices[v->camera], lv_matrices[v->parent], out + 3, &error);
      } else {
        TfpLocomotionParent parent;
        memset(&parent, 0, sizeof parent);
        ok = tfp_locomotion_to_parent(vv->table ? &value : NULL, lv_matrices[v->camera], lv_matrices[v->parent], &parent, &error);
        const double made[7] = {parent.dx, parent.dy, parent.dz, parent.qx, parent.qy, parent.qz, parent.qw};
        memcpy(out, made, sizeof out);
        /* the text may be left out, and a refusal leaves the caller's result alone */
        TfpLocomotionParent untouched, again;
        memset(&untouched, 0x5a, sizeof untouched);
        again = untouched;
        if (tfp_locomotion_to_parent(vv->table ? &value : NULL, lv_matrices[v->camera], lv_matrices[v->parent], &again, NULL) != ok ||
            (!ok && memcmp(&again, &untouched, sizeof again))) { bad = true; snprintf(what, sizeof what, "to_parent without a text differs"); }
      }
      if (ok != (v->ok != 0) || strcmp(error ? error : "", v->error)) {
        bad = true;
        snprintf(what, sizeof what, "C %s \"%s\", retail %s \"%s\"", ok ? "answered" : "refused", error ? error : "", v->ok ? "answered" : "refused", v->error);
      }
      for (int k = v->op == LV_ROTATION ? 3 : 0; k < 7 && ok && !bad; k++) {
        (*numbers)++;
        if (!same(out[k], v->out[k])) { bad = true; snprintf(what, sizeof what, "number %d: C %.17g retail %.17g", k, out[k], v->out[k]); }
      }
    }
    if (bad && failures++ < 8) fprintf(stderr, "locomotion pure operation %u (op %d): %s\n", i, v->op, what);
  }
  return failures;
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(LOCOMOTION_VECTOR_COMMIT, commit) || strlen(LOCOMOTION_VECTOR_SHA256) != 64) {
    fprintf(stderr, "locomotion vectors were cut from retail %s, expected %s\n", LOCOMOTION_VECTOR_COMMIT, commit);
    return 2;
  }
  if (strcmp(tfp_locomotion_sprint_evidence, ls_sprint_evidence)) {
    fprintf(stderr, "sprint evidence text differs from retail\n");
    return 1;
  }
  unsigned numbers = 0, failures = pure(&numbers);
  TfpActionStateReader *reader = malloc(sizeof *reader), *twin = malloc(sizeof *twin);
  if (!reader || !twin) return 3;
  memset(reader, 0xa5, sizeof *reader);
  memset(twin, 0x5a, sizeof *twin);
  unsigned count = sizeof ls_steps / sizeof *ls_steps, answered = 0, reads = 0, on_foot = 0;
  bool scripted = false;
  for (unsigned i = 0; i < count; i++) {
    const LSStep *v = ls_steps + i;
    World world = {ls_images + v->origin, v, true, 0, 0xcbf29ce484222325ULL};
    FpMemory memory = {world_read, &world, (uintptr_t)0x100000000ULL, (uintptr_t)v->exe};
    Host host = {&world, &memory, reader, {&memory, &host, host_player_snapshot, host_alignment_context}};
    TfpLocomotionBackend backend = {&memory, &host, host_player_snapshot, host_alignment_executable, host_action_state, host_on_foot};
    TfpSnapshot origin, fresh;
    TfpLocomotionSample out;
    char error[TFP_LOCOMOTION_ERROR] = "not cleared", what[1200] = "";
    bool bad = false;
    if (v->kind == 1) { /* backend:on_foot(s, action) by itself */
      TfpSnapshot s;
      TfpActionState action;
      world.image = ls_images + v->image;
      bool has = tfp_snapshot(&memory, TFP_SNAPSHOT_PLAYER, &s, NULL, 0);
      if (has && v->done == 1) s.avatar_index = 8;
      if (has && v->done == 2) s.flags_address += 8;
      memset(&action, 0, sizeof action);
      action.resource_match = v->verified != 0;
      action.layer_count = (uint32_t)v->l1;
      action.has[14] = v->has_l14 != 0;
      action.states[14] = v->l14;
      world.in_snapshot = false;
      bool got = tfp_on_foot(&memory, has ? &s : NULL, v->has_action ? &action : NULL);
      if (got != (v->ok != 0) || has != (v->owner != 0) || world.reads != v->reads || world.hash != v->reads_hash) {
        if (failures++ < 8)
          fprintf(stderr, "locomotion step %u (image %d): on_foot C %d after %u reads, retail %d after %u\n", i, v->image, got, world.reads, v->ok, v->reads);
      }
      reads += world.reads;
      on_foot++;
      continue;
    }
    if (v->first) { tfp_action_state_init(reader); tfp_action_state_init(twin); scripted = false; }
    if (v->trigger_count) scripted = true;
    /* the lease, made for the player of image `origin` */
    bool has_origin = tfp_snapshot(&memory, TFP_SNAPSHOT_PLAYER, &origin, NULL, 0);
    TfpLocomotionLease lease = {!v->other_lease, v->done != 0, v->cleanup_only != 0, has_origin ? &origin : NULL,
                                (uintptr_t)((has_origin ? (long long)origin.record : 0) + v->request_delta), (uintptr_t)v->rig_object};
    world.image = ls_images + v->image;
    world.in_snapshot = false;
    memset(&out, 0xa5, sizeof out);
    bool ok = tfp_locomotion_sample(&backend, v->no_lease ? NULL : &lease, &out, error, sizeof error);
    unsigned made = world.reads;
    unsigned long long hash = world.hash;
    if (ok != (v->ok != 0)) { bad = true; snprintf(what, sizeof what, "C %s \"%s\", retail %s \"%s\"", ok ? "answered" : "refused", ok ? "" : error, v->ok ? "answered" : "refused", v->error); }
    else if (!ok && !same_error(error, v->error)) { bad = true; snprintf(what, sizeof what, "C \"%s\", retail \"%s\"", error, v->error); }
    else if (ok) {
      world.in_snapshot = true;
      bool owner = tfp_snapshot(&memory, TFP_SNAPSHOT_PLAYER, &fresh, NULL, 0) && tfp_same_player(&out.player, &fresh);
      const double n[5] = {out.x, out.y, out.z, out.yaw, out.pitch};
      if (*error) { bad = true; snprintf(what, sizeof what, "error text not cleared"); }
      for (int k = 0; k < 5 && !bad; k++) {
        numbers++;
        if (!same(n[k], v->n[k])) { bad = true; snprintf(what, sizeof what, "number %d: C %.17g retail %.17g", k, n[k], v->n[k]); }
      }
      for (int k = 0; k < 16 && !bad; k++) {
        numbers += 2;
        if (!same(out.camera_basis[k], v->camera[k]) || !same(out.parent_basis[k], v->parent[k])) { bad = true; snprintf(what, sizeof what, "basis entry %d", k); }
      }
      if (!bad && (out.ads != (v->ads != 0) || out.shoulder_aim != (v->shoulder_aim != 0) || out.sprint != SPRINT(v->sprint, v->has_l3, v->l3) ||
                   out.reload != (v->reload != 0) || memcmp(out.stance_flags, v->flags, 24) || out.has_action != (v->has_action != 0) ||
                   (out.has_action && (strcmp(out.action_resource, v->resource) || out.action_verified != (v->verified != 0) ||
                                       out.has_l1 != (v->has_l1 != 0) || out.has_l3 != (v->has_l3 != 0) || out.has_l14 != (v->has_l14 != 0) ||
                                       (out.has_l1 && out.l1 != v->l1) || (out.has_l3 && out.l3 != v->l3) || (out.has_l14 && out.l14 != v->l14))) ||
                   (!out.has_action && (out.has_l1 || out.has_l3 || out.has_l14 || out.action_verified || *out.action_resource)) ||
                   out.body_object != v->body_object || owner != (v->owner != 0))) {
        bad = true;
        snprintf(what, sizeof what, "ads %d aim %d sprint %d (retail %d) reload %d action %d %s verified %d l1 %d l3 %d l14 %d object %llx owner %d",
                 out.ads, out.shoulder_aim, out.sprint, v->sprint, out.reload, out.has_action, out.action_resource, out.action_verified, out.l1,
                 out.l3, out.l14, (unsigned long long)out.body_object, owner);
      }
      answered++;
    }
    if (!bad && (made != v->reads || hash != v->reads_hash)) {
      bad = true;
      snprintf(what, sizeof what, "memory reads differ: C made %u, retail %u (or the same count in another order or size)", made, v->reads);
    }
    /* The same sample through the repository's own backend (tfp_locomotion_backend): the same answer. Only while no
     * swap has been scripted in this sequence: after one the two action-state caches may differ. */
    if (!scripted) {
      TfpLocomotionBackend own;
      TfpLocomotionHost own_host;
      TfpLocomotionSample second, untouched;
      char second_error[TFP_LOCOMOTION_ERROR] = "not cleared";
      world.image = ls_images + v->image;
      world.in_snapshot = false;
      memset(&second, 0x5a, sizeof second);
      untouched = second;
      tfp_locomotion_backend(&own, &own_host, &memory, twin);
      bool again = tfp_locomotion_sample(&own, v->no_lease ? NULL : &lease, &second, second_error, sizeof second_error);
      if (again != ok || (!ok && (strcmp(second_error, error) || memcmp(&second, &untouched, sizeof second))) ||
          (ok && (!same(second.x, out.x) || !same(second.yaw, out.yaw) || !same(second.pitch, out.pitch) || second.sprint != out.sprint ||
                  second.has_action != out.has_action || second.l14 != out.l14 || second.body_object != out.body_object ||
                  memcmp(second.camera_basis, out.camera_basis, sizeof out.camera_basis)))) {
        bad = true;
        snprintf(what, sizeof what, "the repository's own backend answers differently: %d \"%.200s\"", again, second_error);
      }
      if (!bad && (i % 4) == 0) { /* the error buffer may be left out, with or without a size */
        TfpLocomotionSample third;
        if (tfp_locomotion_sample(&own, v->no_lease ? NULL : &lease, &third, NULL, 64) != ok ||
            tfp_locomotion_sample(&own, v->no_lease ? NULL : &lease, &third, second_error, 0) != ok) {
          bad = true;
          snprintf(what, sizeof what, "a sample without an error buffer differs");
        }
      }
    }
    reads += made;
    if (bad && failures++ < 8) fprintf(stderr, "locomotion step %u (image %d): %s\n", i, v->image, what);
  }
  free(reader);
  free(twin);
  if (failures) {
    fprintf(stderr, "%u operations differ from retail\n", failures);
    return 1;
  }
#ifdef LOCOMOTION_OPEN_CONSTANTS
  const char *mode = "with the two open sprint tests switched on";
#else
  const char *mode = "as shipped (sprint: retail's answer narrowed to l3 == 71)";
#endif
  printf("Locomotion, %s: %u pure operations and %u samples answered as retail's locomotion.lua (%s): %u sampled, the rest "
         "refused with the same text; %u on-foot tests as retail's backend; %u doubles equal bit for bit; %u memory reads in retail's order\n",
         mode, (unsigned)(sizeof lv_pure / sizeof *lv_pure), count - on_foot, LOCOMOTION_VECTOR_COMMIT, answered, on_foot, numbers, reads);
  return 0;
}
