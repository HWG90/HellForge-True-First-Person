#include "controller.h"
#include "vectors/controller_vectors.h"
#include <stdio.h>
#include <string.h>

/* A backend that answers what the vector says and records what it was asked. */
static const ControllerVector *world;
static char trace[256];
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
  *out = (TfpPoseStats){world->armed, world->expired, world->tw, world->blend, world->error};
  l->has_pose_error = true;
  l->pose_error = world->error;
  return true;
}
static bool release(void *user, TfpLease *l, char *why, size_t size) {
  (void)user; (void)l;
  note("release");
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

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(CONTROLLER_VECTOR_COMMIT, commit) || strlen(CONTROLLER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "controller vectors were cut from retail %s, expected %s\n", CONTROLLER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpBackendApi backend = {NULL, snapshot, player_snapshot, hammer, prepare, enter, status, release,
                                        packet, set_transition, clock_now};
  unsigned count = sizeof controller_vectors / sizeof *controller_vectors, entries = 0, runs = 0;
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
    switch (v->call) {
    case CV_START: result = tfp_controller_start(&controller); break;
    case CV_STOP: result = tfp_controller_stop(&controller, v->arg > 0 ? controller_reasons[v->arg] : NULL); break;
    case CV_SHUTDOWN: result = tfp_controller_shutdown(&controller); break;
    case CV_SUSPEND: result = tfp_controller_suspend(&controller, v->arg > 0 ? controller_reasons[v->arg] : NULL); break;
    case CV_UPDATE: result = tfp_controller_update(&controller); break;
    case CV_RETRY_CLEANUP: result = tfp_controller_retry_cleanup(&controller); break;
    }
    const TfpController *c = &controller;
    if (result != (v->result != 0) || (int)c->phase != v->phase || c->enabled != (v->enabled != 0) ||
        (c->lease != NULL) != (v->lease != 0) || (int)c->runs != v->runs || (int)c->enter_attempts != v->enter_attempts ||
        (int)c->release_attempts != v->release_attempts || (int)c->samples != v->samples ||
        (int)c->observed_native_without_aim != v->native_without_aim || (int)c->reacquisitions != v->reacquisitions ||
        (int)c->aim_transitions != v->aim_transitions || strcmp(trace, v->trace) || strcmp(c->label, v->label)) {
      fprintf(stderr, "controller vector %u (run %d, call %d, now %.4f)\n"
                      "  C      result=%d phase=%d enabled=%d lease=%d runs=%u enter=%u release=%u samples=%u native=%u reacq=%u aim=%u\n"
                      "         trace=%s\n         label=%s\n"
                      "  retail result=%d phase=%d enabled=%d lease=%d runs=%d enter=%d release=%d samples=%d native=%d reacq=%d aim=%d\n"
                      "         trace=%s\n         label=%s\n",
              i, v->run, v->call, v->now, result, c->phase, c->enabled, c->lease != NULL, c->runs, c->enter_attempts,
              c->release_attempts, c->samples, c->observed_native_without_aim, c->reacquisitions, c->aim_transitions,
              trace, c->label, v->result, v->phase, v->enabled, v->lease, v->runs, v->enter_attempts,
              v->release_attempts, v->samples, v->native_without_aim, v->reacquisitions, v->aim_transitions, v->trace, v->label);
      return 1;
    }
    entries += strstr(trace, "enter") != NULL;
  }
  printf("Controller: %u operations in %u runs decided as retail's controller.lua (%s): same result, phase, backend calls and status text; %u entries\n",
         count, runs, CONTROLLER_VECTOR_COMMIT, entries);
  return 0;
}
