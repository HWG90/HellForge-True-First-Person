/* src/action_state.c replayed against the retail action_state.lua's own
 * answers (tests/vectors/action_state_vectors.h): the same logical scenes,
 * there in retail's memory layout, here in build 25480438's. Every field of
 * the answer, every refusal text, and the ordered list of memory reads made
 * outside the player snapshot (count and hash) must be equal. */
#include "action_state.h"
#include "vectors/action_state_vectors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct World {
  const ASImage *image;
  const ASStep *step;
  bool in_snapshot;
  unsigned reads;
  unsigned long long hash;
} World;

static const ASRecord *covering(const ASImage *image, uintptr_t at) {
  for (unsigned i = 0; i < image->count; i++) {
    const ASRecord *r = image->records + i;
    unsigned size = r->bytes ? r->size : 1;
    if (at >= r->at && at - r->at < size) return r;
  }
  return NULL;
}
static bool replaced(const ASImage *image, unsigned long long at) {
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
      if (w->step->triggers[t].read == w->reads) w->image = as_images + w->step->triggers[t].image;
    for (int k = 0; k < 2; k++)
      for (int b = 0; b < 8; b++) w->hash = (w->hash ^ ((words[k] >> (b * 8)) & 0xff)) * 0x100000001b3ULL;
  }
  while (size) {
    const ASRecord *r = covering(w->image, at);
    if (!r && w->image->base >= 0) {
      r = covering(as_images + w->image->base, at);
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
/* The backend's two methods: the player snapshot is the already-proven part (its reads are not listed or counted). */
typedef struct Host { World *world; const FpMemory *memory; } Host;
static bool host_player_snapshot(void *user, TfpSnapshot *out, char *error, size_t error_size) {
  Host *h = user;
  h->world->in_snapshot = true;
  bool ok = tfp_snapshot(h->memory, TFP_SNAPSHOT_PLAYER, out, error, error_size);
  h->world->in_snapshot = false;
  return ok;
}
static bool host_alignment_context(void *user, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index,
                                   TfpAlignmentContext *out, char *error, size_t error_size) {
  Host *h = user;
  return tfp_alignment_context(h->memory, viewmodel, avatar_owner, avatar_index, out, error, error_size);
}
/* Retail's pointer refusal names the address, which differs between builds for executable addresses; the value must be equal. */
static bool same_error(const char *c, const char *retail) {
  static const char prefix[] = "Pointer unavailable at0x";
  if (!strncmp(retail, prefix, sizeof prefix - 1)) {
    const char *cv = strstr(c, " (value0x"), *rv = strstr(retail, " (value0x");
    return !strncmp(c, prefix, sizeof prefix - 1) && cv && rv && !strcmp(cv, rv);
  }
  return !strcmp(c, retail);
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(ACTION_STATE_VECTOR_COMMIT, commit) || strlen(ACTION_STATE_VECTOR_SHA256) != 64) {
    fprintf(stderr, "action state vectors were cut from retail %s, expected %s\n", ACTION_STATE_VECTOR_COMMIT, commit);
    return 2;
  }
  if (strcmp(tfp_action_state_sprint_evidence, as_sprint_evidence)) {
    fprintf(stderr, "sprint evidence text differs from retail\n");
    return 1;
  }
  TfpActionStateReader *reader = malloc(sizeof *reader), *twin = malloc(sizeof *twin);
  if (!reader || !twin) return 3;
  memset(reader, 0xa5, sizeof *reader); /* init must not depend on what the memory held */
  memset(twin, 0x5a, sizeof *twin);
  unsigned count = sizeof as_steps / sizeof *as_steps, answered = 0, failures = 0, reads = 0, contexts = 0;
  for (unsigned i = 0; i < count; i++) {
    const ASStep *v = as_steps + i;
    World world = {as_images + v->image, v, true, 0, 0xcbf29ce484222325ULL};
    FpMemory memory = {world_read, &world, (uintptr_t)0x100000000ULL, (uintptr_t)v->exe};
    Host host = {&world, &memory};
    TfpActionStateBackend backend = {&memory, &host, host_player_snapshot, host_alignment_context};
    TfpSnapshot expected, reference;
    TfpActionState out;
    char error[TFP_ACTION_STATE_ERROR] = "not cleared", what[1200] = "";
    bool has_expected = false, has_reference = false, bad = false;
    if (v->kind == 1) { /* the two backend methods by themselves */
      TfpAlignmentContext c;
      char executable_error[TFP_ACTION_STATE_ERROR] = "not cleared";
      uintptr_t exe = 1;
      memset(&c, 0xa5, sizeof c);
      world.in_snapshot = false;
      bool ok = tfp_alignment_context(&memory, v->edit_body, 7, 3, &c, error, sizeof error);
      bool found = tfp_alignment_executable(&memory, &exe, executable_error, sizeof executable_error);
      unsigned made = world.reads;
      unsigned long long hash = world.hash;
      if (ok != (v->ok != 0) || (!ok && !same_error(error, v->error)) || found != !*v->resource ||
          (!found && strcmp(executable_error, v->resource)) || (found && exe != (uintptr_t)v->exe) || (!found && exe != 1) ||
          (ok && (c.exe != (uintptr_t)v->exe || c.unit != v->body || c.index != v->layers_out || c.generation != (uint32_t)v->match ||
                  c.object != v->body_object || c.registry != v->graph || c.units != v->instance || c.generations != v->generations ||
                  c.owner != 7 || c.avatar_index != 3))) {
        bad = true;
        snprintf(what, sizeof what, "context of %u: C %d \"%s\" exe \"%s\", retail %d \"%s\" exe \"%s\"", v->edit_body, ok, ok ? "" : error,
                 found ? "" : executable_error, v->ok, v->error, v->resource);
      } else if (made != v->reads || hash != v->reads_hash) {
        bad = true;
        snprintf(what, sizeof what, "context reads differ: C made %u, retail %u", made, v->reads);
      }
      /* the optional answers may be left out, and a refusal leaves the caller's context alone */
      TfpAlignmentContext untouched;
      memset(&untouched, 0x5a, sizeof untouched);
      TfpAlignmentContext again = untouched;
      if (v->trigger_count == 0) {
        world.image = as_images + v->image;
        if (tfp_alignment_context(&memory, v->edit_body, 7, 3, &again, NULL, 0) != ok || (!ok && memcmp(&again, &untouched, sizeof again)) ||
            tfp_alignment_executable(&memory, NULL, NULL, 0) != found || tfp_alignment_context(&memory, v->edit_body, 7, 3, &again, NULL, 64) != ok ||
            tfp_alignment_context(&memory, v->edit_body, 7, 3, &again, error, 0) != ok || tfp_alignment_executable(&memory, NULL, NULL, 64) != found) {
          bad = true;
          snprintf(what, sizeof what, "context without the optional answers differs");
        }
      }
      reads += made;
      contexts++;
      if (bad && failures++ < 8) fprintf(stderr, "action state step %u (image %d): %s\n", i, v->image, what);
      continue;
    }
    if (v->first) { tfp_action_state_init(reader); tfp_action_state_init(twin); }
    if (v->expected >= 0) { /* an expected snapshot taken from another (or the same) scene */
      world.image = as_images + v->expected;
      has_expected = tfp_snapshot(&memory, TFP_SNAPSHOT_PLAYER, &expected, NULL, 0);
      if (has_expected) {
        reference = expected;
        has_reference = true;
        if (v->edit == 1) expected.body = v->edit_body;
        else if (v->edit == 2) expected.local_entity ^= 1; /* another identity: any of its parts */
      }
      world.image = as_images + v->image;
    }
    world.in_snapshot = false;
    memset(&out, 0xa5, sizeof out);
    bool ok = tfp_action_state_sample(reader, &backend, has_expected ? &expected : NULL, v->layer_count < 0 ? NULL : v->layers,
                                      v->layer_count < 0 ? 0 : (size_t)v->layer_count, &out, error, sizeof error);
    unsigned made = world.reads;
    unsigned long long hash = world.hash;
    if (ok != (v->ok != 0)) { bad = true; snprintf(what, sizeof what, "C %s \"%s\", retail %s \"%s\"", ok ? "answered" : "refused", ok ? "" : error, v->ok ? "answered" : "refused", v->error); }
    else if (!ok && !same_error(error, v->error)) { bad = true; snprintf(what, sizeof what, "C \"%s\", retail \"%s\"", error, v->error); }
    else if (ok) {
      if (*error) { bad = true; snprintf(what, sizeof what, "error text not cleared"); }
      if (!has_reference) {
        world.in_snapshot = true;
        has_reference = tfp_snapshot(&memory, TFP_SNAPSHOT_PLAYER, &reference, NULL, 0);
      }
      bool owner = has_reference && tfp_same_player(&out.player, &reference);
      if (out.body != v->body || out.body_object != v->body_object || out.graph != v->graph || out.instance != v->instance ||
          out.layer_count != v->layers_out || strcmp(out.resource, v->resource) || out.resource_match != (v->match != 0) ||
          out.sprint_hypothesis != (v->sprint != 0) || owner != (v->owner != 0) || out.unknown_count != (unsigned)v->unknown_count) {
        bad = true;
        snprintf(what, sizeof what, "body %u object %llx graph %llx layers %u resource %s match %d sprint %d owner %d unknown %u", out.body,
                 (unsigned long long)out.body_object, (unsigned long long)out.graph, out.layer_count, out.resource, out.resource_match,
                 out.sprint_hypothesis, owner, out.unknown_count);
      }
      for (int layer = 0; layer <= 32 && !bad; layer++)
        if (out.has[layer] != (v->has[layer] != 0) || (out.has[layer] && out.states[layer] != v->states[layer])) {
          bad = true;
          snprintf(what, sizeof what, "layer %d: C %s%d, retail %s%d", layer, out.has[layer] ? "" : "nil ", out.states[layer],
                   v->has[layer] ? "" : "nil ", v->states[layer]);
        }
      for (int k = 0; k < v->unknown_count && !bad; k++)
        if (out.unknown_layers[k] != v->unknown[k]) { bad = true; snprintf(what, sizeof what, "unknown layer %d", k); }
      answered++;
    }
    if (!bad && (made != v->reads || hash != v->reads_hash)) {
      bad = true;
      snprintf(what, sizeof what, "memory reads differ: C made %u, retail %u (or the same count in another order or size)", made, v->reads);
    }
    /* The same sample through the repository's own backend (tfp_action_state_backend): the same answer. Only where
     * no swap is scripted: that backend's snapshot reads are counted like any other. */
    {
      TfpActionStateBackend own;
      TfpActionState second;
      char second_error[TFP_ACTION_STATE_ERROR] = "not cleared";
      bool scripted = false;
      for (unsigned j = i; ; j--) { /* any swap earlier in this sequence leaves the two readers' caches apart */
        if (as_steps[j].trigger_count) scripted = true;
        if (as_steps[j].first || j == 0) break;
      }
      if (!scripted) {
        world.image = as_images + v->image;
        memset(&second, 0x5a, sizeof second);
        tfp_action_state_backend(&own, &memory);
        bool again = tfp_action_state_sample(twin, &own, has_expected ? &expected : NULL, v->layer_count < 0 ? NULL : v->layers,
                                             v->layer_count < 0 ? 0 : (size_t)v->layer_count, &second, second_error, sizeof second_error);
        if (again != ok || (!ok && strcmp(second_error, error)) ||
            (ok && (second.body != out.body || second.body_object != out.body_object || second.graph != out.graph ||
                    second.instance != out.instance || second.layer_count != out.layer_count || strcmp(second.resource, out.resource) ||
                    second.resource_match != out.resource_match || second.sprint_hypothesis != out.sprint_hypothesis ||
                    memcmp(second.has, out.has, sizeof out.has) || second.unknown_count != out.unknown_count ||
                    !tfp_same_player(&second.player, &out.player)))) {
          bad = true;
          snprintf(what, sizeof what, "the repository's own backend answers differently: %d \"%s\"", again, second_error);
        }
        for (int layer = 1; layer <= 32 && ok && again && !bad; layer++)
          if (out.has[layer] && second.states[layer] != out.states[layer]) { bad = true; snprintf(what, sizeof what, "own backend, layer %d", layer); }
        /* error may be left out */
        if (!bad && ok && (i % 5) == 1) { /* ... also when it answers, with a size but no buffer or a buffer but no size */
          TfpActionState third;
          if (!tfp_action_state_sample(twin, &own, has_expected ? &expected : NULL, v->layer_count < 0 ? NULL : v->layers,
                                       v->layer_count < 0 ? 0 : (size_t)v->layer_count, &third, NULL, 64) ||
              !tfp_action_state_sample(twin, &own, has_expected ? &expected : NULL, v->layer_count < 0 ? NULL : v->layers,
                                       v->layer_count < 0 ? 0 : (size_t)v->layer_count, &third, second_error, 0)) {
            bad = true;
            snprintf(what, sizeof what, "an answer without an error buffer refused");
          }
        }
        if (!bad && !ok && (i % 5) == 0) {
          TfpActionState third;
          memset(&third, 0x5a, sizeof third);
          TfpActionState untouched = third;
          if (tfp_action_state_sample(twin, &own, has_expected ? &expected : NULL, v->layer_count < 0 ? NULL : v->layers,
                                      v->layer_count < 0 ? 0 : (size_t)v->layer_count, &third, NULL, 0) || memcmp(&third, &untouched, sizeof third)) {
            bad = true;
            snprintf(what, sizeof what, "a refusal without an error buffer answered or touched the result");
          }
        }
      }
    }
    reads += made;
    if (bad && failures++ < 8) fprintf(stderr, "action state step %u (image %d): %s\n", i, v->image, what);
  }
  free(reader);
  free(twin);
  if (failures) {
    fprintf(stderr, "%u of %u samples differ from retail\n", failures, count);
    return 1;
  }
  printf("Action state: %u samples answered as retail's action_state.lua (%s) on this build's layout: %u read, the rest refused with the "
         "same text; %u unit lookups as retail's backend; %u memory reads in retail's order\n", count - contexts,
         ACTION_STATE_VECTOR_COMMIT, answered, contexts, reads);
  return 0;
}
