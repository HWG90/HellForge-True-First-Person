#include "controller.h"
#include <math.h>
#include "vectors/controller_vectors.h"
#include <stdio.h>
#include <string.h>

/* A backend that answers what the vector says and records what it was asked. */
static const ControllerVector *world;
static char trace[512];
static unsigned snapshots;
static TfpLease lease;

static void note(const char *name) {
  size_t used = strlen(trace);
  snprintf(trace + used, sizeof trace - used, "%s%s", used ? "," : "", name);
}
static const char *reason(int index) { return index > 0 ? controller_reasons[index] : ""; }
static void view(const ControllerView *v, bool player_level, TfpView *out) {
  memset(out, 0, sizeof *out);
  if (!v->ok) {
    snprintf(out->error, sizeof out->error, "%s", reason(v->err));
    return;
  }
  out->available = true;
  out->player_identity = 1000u + (unsigned)v->player;
  out->identity = player_level ? out->player_identity : (unsigned)v->identity;
  out->aiming = v->aiming;
  out->controller_active = v->active;
  out->camera_kind = (uint32_t)v->kind;
  out->safe = v->safe;
  out->record = (uint64_t)v->record;
  out->guard = (uint8_t)v->guard;
  out->reason = v->reason > 0 ? controller_reasons[v->reason] : NULL;
}
/* With `follow`, the game answers an entry with its native view and the end of a lease with the
 * normal one. Decided once, before the call, from the lease held then (as the Lua runner does). */
static int follow_kind, follow_record;
static bool follow;
static TfpController controller;
static void snapshot(void *user, TfpView *out) {
  (void)user;
  note("snapshot");
  ControllerView v = ++snapshots > 1 ? world->snap2 : world->snap;
  if (follow) v.kind = follow_kind, v.record = follow_record;
  view(&v, false, out);
}
static void player_snapshot(void *user, TfpView *out) {
  (void)user;
  note("player_snapshot");
  ControllerView v = world->psnap;
  if (follow) v.kind = follow_kind;
  view(&v, true, out);
}
static bool hammer(void *user, const TfpView *current) { (void)user; (void)current; note("hammer"); return world->hammer; }
static bool prepare(void *user, const TfpView *current, char *why, size_t size) {
  (void)user; (void)current;
  note("prepare");
  if (world->prepare == 0) return true;
  snprintf(why, size, "%s", reason(world->prepare));
  return false;
}
static TfpLease *enter(void *user, const TfpView *current, uint32_t remaining, bool blend, char *why, size_t size) {
  (void)user; (void)current; (void)remaining;
  note(blend ? "enter(blend)" : "enter");
  if (world->enter != 0) {
    snprintf(why, size, "%s", reason(world->enter));
    return NULL;
  }
  lease = (TfpLease){.request = (uint64_t)world->lease_request, .cleanup_only = world->lease_cleanup_only};
  return &lease;
}
static bool status(void *user, TfpLease *l, TfpPoseStats *out, char *why, size_t size) {
  (void)user;
  note("status");
  if (!world->status_ok) {
    if (world->retire) l->pose_owner_retired = true;
    snprintf(why, size, "%s", reason(world->status_why));
    return false;
  }
  *out = (TfpPoseStats){world->armed, world->expired, world->tw, world->blend, world->error,
                        world->has_wu, world->has_zr, world->wu, world->zr};
  l->has_pose_error = true;
  l->pose_error = world->error;
  l->has_pose_stats = true;
  l->pose_stats = *out;
  return true;
}
static bool release(void *user, TfpLease *l, char *why, size_t size) {
  (void)user;
  note("release");
  if (world->final_ok) { /* the backend records the helper's final statistics on the lease */
    l->has_pose_stats = true;
    l->pose_stats = (TfpPoseStats){.target_weight = world->final_tw, .blend = world->final_blend, .has_weapon_updates = true,
                                   .weapon_updates = world->final_wu};
  }
  snprintf(why, size, "%s", reason(world->release == 0 ? world->release_note : world->release));
  return world->release == 0;
}
static void packet(void *user, TfpPhasePacket *out) {
  (void)user;
  note("packet");
  *out = (TfpPhasePacket){world->packet_ok, (unsigned)world->packet_identity, 1000u + (unsigned)world->packet_player,
                          (unsigned)world->packet_epoch, world->packet_progress};
}
static bool set_transition(void *user, TfpLease *l, const TfpPhasePacket *p, char *why, size_t size) {
  (void)user; (void)l; (void)p;
  note("set_transition");
  if (world->set_transition == 0) return true;
  snprintf(why, size, "%s", reason(world->set_transition));
  return false;
}
static double clock_now(void *user) { (void)user; return world->now; }

/* The placement side of the backend. */
static void place(const ControllerPlace *from, TfpPlacementValue *out) {
  for (int i = 0; i < TFP_AXIS_N; i++) out->kind[i] = (TfpAxisKind)from->kind[i], out->v[i] = from->v[i];
}
static bool pose_placement(void *user, TfpPlacementValue *out) {
  (void)user;
  if (!world->bpose.ok) return false;
  place(&world->bpose, out);
  return true;
}
static const char *weapon_key(void *user) {
  static char key[16];
  (void)user;
  if (world->key <= 0) return NULL;
  snprintf(key, sizeof key, "weapon%d", world->key);
  return key;
}
static void six(char *out, size_t size, const TfpAxisKind *kind, const double *v) {
  size_t used = 0;
  out[0] = 0;
  for (int i = 0; i < TFP_AXIS_N && used < size; i++) {
    if (!kind || kind[i] == TFP_AXIS_NUMBER) used += (size_t)snprintf(out + used, size - used, "%s%.17g", i ? "," : "", v[i]);
    else used += (size_t)snprintf(out + used, size - used, "%s%s", i ? "," : "", kind[i] == TFP_AXIS_NIL ? "nil" : "other");
  }
}
static bool set_answer(const char *name, const double *value, char *why, size_t size) {
  char call[256], values[200];
  six(values, sizeof values, NULL, value);
  snprintf(call, sizeof call, "%s(%s)", name, values);
  note(call);
  if (world->set == 0) return true;
  snprintf(why, size, "%s", world->set == -2 ? reason(7) : reason(world->set)); /* -2: raised with that text */
  return false;
}
static bool lease_mismatch;
static bool set_pose_placement(void *user, TfpLease *l, const double *value, char *why, size_t size) {
  (void)user;
  if (l != controller.lease || !l) lease_mismatch = true;
  return set_answer("set_pose_placement", value, why, size);
}
static bool set_placement_defaults(void *user, const double *value, char *why, size_t size) {
  (void)user;
  return set_answer("set_placement_defaults", value, why, size);
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(CONTROLLER_VECTOR_COMMIT, commit) || strlen(CONTROLLER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "controller vectors were cut from retail %s, expected %s\n", CONTROLLER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpBackendApi backend = {NULL, snapshot, player_snapshot, hammer, prepare, enter, status, release,
                                        packet, set_transition, clock_now};
  static const TfpPlacementApi placement = {NULL, pose_placement, weapon_key, set_pose_placement, set_placement_defaults};
  unsigned count = sizeof controller_vectors / sizeof *controller_vectors, entries = 0, runs = 0, per_call[9] = {0};
  unsigned lease_sets = 0, default_sets = 0, refused = 0;
  int run = -1;
  for (unsigned i = 0; i < count; i++) {
    const ControllerVector *v = controller_vectors + i;
    if (v->run != run) {
      run = v->run;
      runs++;
      tfp_controller_init(&controller, &backend);
    }
    world = v;
    trace[0] = 0;
    snapshots = 0;
    follow = v->follow != 0;
    follow_kind = controller.lease ? 6 : 4;
    follow_record = controller.lease ? (int)controller.lease->request : 0;
    bool result = false;
    char text[256] = "", status_line[512];
    per_call[v->call]++;
    switch (v->call) {
    case CV_START: result = tfp_controller_start(&controller); break;
    case CV_STOP: result = tfp_controller_stop(&controller, v->arg > 0 ? controller_reasons[v->arg] : NULL); break;
    case CV_SHUTDOWN: result = tfp_controller_shutdown(&controller); break;
    case CV_SUSPEND: result = tfp_controller_suspend(&controller, v->arg > 0 ? controller_reasons[v->arg] : NULL); break;
    case CV_UPDATE: result = tfp_controller_update(&controller); break;
    case CV_RETRY_CLEANUP: result = tfp_controller_retry_cleanup(&controller); break;
    case CV_SET_PLACEMENT: {
      TfpPlacementValue value;
      place(&v->value, &value);
      result = tfp_controller_set_placement(&controller, &placement, v->value.ok ? &value : NULL, text, sizeof text);
      if (result && text[0]) { fprintf(stderr, "controller vector %u: an accepted placement left a text\n", i); return 1; }
      lease_sets += strstr(trace, "set_pose_placement") != NULL;
      default_sets += strstr(trace, "set_placement_defaults") != NULL;
      refused += !result;
      break;
    }
    case CV_GET_PLACEMENT: {
      TfpPlacementValue value;
      tfp_controller_get_placement(&controller, &placement, &value);
      six(text, sizeof text, value.kind, value.v);
      result = true;
      break;
    }
    case CV_PLACEMENT_TARGET: {
      const char *key = NULL;
      TfpLease *held = NULL;
      result = tfp_controller_placement_target(&controller, &placement, &key, &held);
      if (result) snprintf(text, sizeof text, "%s", key);
      if (result && held != controller.lease) lease_mismatch = true;
      break;
    }
    }
    tfp_controller_status(&controller, status_line, sizeof status_line);
    if (lease_mismatch) { fprintf(stderr, "controller vector %u: the placement call named another lease\n", i); return 1; }
    const TfpController *c = &controller;
    if (result != (v->result != 0) || (int)c->phase != v->phase || c->enabled != (v->enabled != 0) ||
        (c->lease != NULL) != (v->lease != 0) || (int)c->runs != v->runs || (int)c->enter_attempts != v->enter_attempts ||
        (int)c->release_attempts != v->release_attempts || (int)c->samples != v->samples ||
        (int)c->observed_native_without_aim != v->native_without_aim || (int)c->reacquisitions != v->reacquisitions ||
        (int)c->aim_transitions != v->aim_transitions || strcmp(trace, v->trace) || strcmp(c->label, v->label) ||
        strcmp(status_line, v->status) || strcmp(text, v->text)) {
      fprintf(stderr, "controller vector %u (run %d, call %d, now %.4f)\n"
                      "  C      result=%d phase=%d enabled=%d lease=%d runs=%u enter=%u release=%u samples=%u native=%u reacq=%u aim=%u\n"
                      "         trace=%s\n         label=%s\n         status=%s\n         text=%s\n"
                      "  retail result=%d phase=%d enabled=%d lease=%d runs=%d enter=%d release=%d samples=%d native=%d reacq=%d aim=%d\n"
                      "         trace=%s\n         label=%s\n         status=%s\n         text=%s\n",
              i, v->run, v->call, v->now, result, c->phase, c->enabled, c->lease != NULL, c->runs, c->enter_attempts,
              c->release_attempts, c->samples, c->observed_native_without_aim, c->reacquisitions, c->aim_transitions,
              trace, c->label, status_line, text, v->result, v->phase, v->enabled, v->lease, v->runs, v->enter_attempts,
              v->release_attempts, v->samples, v->native_without_aim, v->reacquisitions, v->aim_transitions, v->trace, v->label, v->status, v->text);
      return 1;
    }
    entries += strstr(trace, "enter") != NULL;
  }
  printf("Controller: %u operations in %u runs decided as retail's controller.lua (%s): same result, phase, backend calls, status text "
         "and status(); %u entries; placement: %u set (%u through the lease, %u as defaults, %u refused), %u get, %u target\n",
         count, runs, CONTROLLER_VECTOR_COMMIT, entries, per_call[CV_SET_PLACEMENT], lease_sets, default_sets, refused,
         per_call[CV_GET_PLACEMENT], per_call[CV_PLACEMENT_TARGET]);
  return 0;
}
