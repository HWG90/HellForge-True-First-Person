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

/* ---- the action hold (not in retail): scenarios written by hand over a small scripted backend ---- */
enum { H_PLAYER = 1001, H_WEAPON = 7, H_OTHER_WEAPON = 8, H_RECORD = 0x5000 };
static struct {
  TfpView current, player;
  TfpHoldView hold;
  double now;
  unsigned snapshots, releases, enters, statuses, hold_views;
  bool release_ok;
  TfpLease lease;
} hw;
static void h_snapshot(void *u, TfpView *out) { (void)u; hw.snapshots++; *out = hw.current; }
static void h_player_snapshot(void *u, TfpView *out) { (void)u; *out = hw.player; }
static bool h_prepare(void *u, const TfpView *v, char *why, size_t size) { (void)u; (void)v; (void)why; (void)size; return true; }
static void h_native(uint64_t record) { /* the game shows its native view for this request */
  hw.current.camera_kind = hw.player.camera_kind = hw.hold.camera_kind = 6;
  hw.current.record = hw.player.record = hw.hold.record = record;
}
static TfpLease *h_enter(void *u, const TfpView *v, uint32_t ms, bool blend, char *why, size_t size) {
  (void)u; (void)v; (void)ms; (void)blend; (void)why; (void)size;
  hw.enters++;
  hw.lease = (TfpLease){.request = H_RECORD};
  h_native(H_RECORD);
  return &hw.lease;
}
static bool h_status(void *u, TfpLease *l, TfpPoseStats *out, char *why, size_t size) {
  (void)u; (void)l; (void)why; (void)size;
  hw.statuses++;
  *out = (TfpPoseStats){.armed = true, .target_weight = 1, .blend = 1};
  return true;
}
static bool h_release(void *u, TfpLease *l, char *why, size_t size) {
  (void)u; (void)l; (void)why; (void)size;
  hw.releases++;
  if (hw.release_ok) { /* the game's normal camera returns */
    hw.current.camera_kind = hw.player.camera_kind = hw.hold.camera_kind = 4;
    hw.current.record = hw.player.record = hw.hold.record = 0x4000;
  }
  return hw.release_ok;
}
static double h_clock(void *u) { (void)u; return hw.now; }
static void h_hold_view(void *u, const TfpLease *l, TfpHoldView *out) { (void)u; (void)l; hw.hold_views++; *out = hw.hold; }
static const TfpBackendApi hold_backend = {NULL, h_snapshot, h_player_snapshot, NULL, h_prepare, h_enter, h_status, h_release, NULL, NULL,
                                           h_clock, h_hold_view};
/* A running lease on the native view at t=10, as after F11 and one steady update. */
static bool h_begin(TfpController *c, bool hold) {
  memset(&hw, 0, sizeof hw);
  hw.release_ok = true;
  hw.now = 10;
  hw.current = (TfpView){.available = true, .identity = H_WEAPON, .player_identity = H_PLAYER, .safe = true, .camera_kind = 4, .record = 0x4000};
  hw.player = hw.current, hw.player.identity = H_PLAYER;
  hw.hold = (TfpHoldView){.available = true, .player_identity = H_PLAYER, .camera_kind = 4, .record = 0x4000, .safe = true,
                          .weapon = TFP_HOLD_WEAPON_SAME};
  tfp_controller_init(c, &hold_backend);
  tfp_controller_set_hold(c, hold);
  if (!tfp_controller_start(c)) return false;
  hw.now += 0.016;
  return tfp_controller_update(c) && c->phase == TFP_PHASE_RUNNING && c->lease == &hw.lease && hw.enters == 1 && !c->holding;
}
static void h_action(void) { hw.current.safe = hw.player.safe = hw.hold.safe = false; } /* an action took `safe` away */
static void h_safe(void) { hw.current.safe = hw.player.safe = hw.hold.safe = true; }
static bool h_step(TfpController *c) { hw.now += 0.016; return tfp_controller_update(c); }
static bool h_held(const TfpController *c) {
  return c->holding && c->phase == TFP_PHASE_RUNNING && c->lease == &hw.lease && hw.releases == 0 && hw.enters == 1;
}
static bool h_left(const TfpController *c) { return !c->holding && !c->lease && c->phase == TFP_PHASE_WAITING && hw.releases == 1 && c->enabled; }
#define H_CHECK(what) do { if (!(what)) { fprintf(stderr, "action hold, %s: failed: %s (why=\"%s\", label=\"%s\")\n", name, #what, c.hold_why, c.label); return false; } } while (0)
static bool hold_tests(unsigned *count) {
  TfpController c;
  const char *name;

  name = "off: an action ends the lease as before";
  H_CHECK(h_begin(&c, false));
  h_action();
  H_CHECK(h_step(&c) && h_left(&c) && hw.hold_views == 0 && c.holds == 0 && !c.hold_why[0]);
  H_CHECK(!strcmp(c.label, "First person enabled; waiting: Gameplay action or camera owns the view"));
  ++*count;

  name = "enter: an action with the lease's own kind-6 request is held";
  H_CHECK(h_begin(&c, true) && tfp_controller_hold_armed(&c));
  unsigned polls = hw.statuses;
  h_action();
  H_CHECK(h_step(&c) && h_held(&c) && c.holds == 1 && hw.statuses == polls); /* the pose helper is not polled: its poll retires an unsafe lease */
  H_CHECK(!strcmp(c.label, "First person enabled; holding the native view through an action"));
  for (int i = 0; i < 30; i++) H_CHECK(h_step(&c) && h_held(&c) && c.holds == 1);
  ++*count;

  name = "enter with empty hands: the full snapshot fails, the player's is unsafe";
  H_CHECK(h_begin(&c, true));
  h_action();
  hw.current = (TfpView){.available = false, .error = "Equip a firearm before starting the camera test"};
  hw.hold.weapon = TFP_HOLD_WEAPON_NONE;
  H_CHECK(h_step(&c) && h_held(&c));
  h_safe(); /* the action's flags are gone, the hands are still empty: still the action */
  H_CHECK(h_step(&c) && h_held(&c));
  ++*count;

  name = "empty hands without an action flag: held for 1000 ms on end, then today's exit";
  H_CHECK(h_begin(&c, true));
  h_action();
  hw.current = (TfpView){.available = false, .error = "Equip a firearm before starting the camera test"};
  hw.hold.weapon = TFP_HOLD_WEAPON_NONE;
  H_CHECK(h_step(&c) && h_held(&c));
  h_safe(); /* a stim: the flags go for about 0.65 s with nothing in the hands */
  H_CHECK(h_step(&c) && h_held(&c));
  double emptied = hw.now;
  hw.now = emptied + 0.65;
  H_CHECK(tfp_controller_update(&c) && h_held(&c));
  h_action(); /* the flags are back (the draw): the count starts again */
  H_CHECK(h_step(&c) && h_held(&c));
  h_safe();
  H_CHECK(h_step(&c) && h_held(&c));
  emptied = hw.now;
  hw.now = emptied + 0.98;
  H_CHECK(tfp_controller_update(&c) && h_held(&c));
  hw.now = emptied + 1.0; /* a dropped weapon: nothing is coming */
  H_CHECK(tfp_controller_update(&c) && h_left(&c) && c.hold_exits == 1 && strstr(c.hold_why, "empty hands"));
  ++*count;

  name = "the full snapshot is unsafe with no action flag (not the on-foot lifecycle): not held";
  H_CHECK(h_begin(&c, true));
  hw.current.safe = false, hw.current.reason = "Character is not in the verified on-foot lifecycle"; /* the player level stays safe */
  H_CHECK(h_step(&c) && h_left(&c) && c.holds == 0 && strstr(c.hold_why, "not held") && strstr(c.hold_why, "no action flag"));
  H_CHECK(!strcmp(c.label, "First person enabled; waiting: Character is not in the verified on-foot lifecycle"));
  ++*count;

  name = "a hold whose action flags end off the on-foot lifecycle ends there";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  hw.player.safe = hw.hold.safe = true; /* the full snapshot stays unsafe */
  hw.current.reason = "Character is not in the verified on-foot lifecycle";
  H_CHECK(h_step(&c) && h_left(&c) && c.hold_exits == 1 && strstr(c.hold_why, "no action flag"));
  ++*count;

  name = "resume: safe again with the same weapon and record, no exit and no entry";
  H_CHECK(h_begin(&c, true));
  h_action();
  for (int i = 0; i < 20; i++) H_CHECK(h_step(&c) && h_held(&c));
  h_safe();
  unsigned reacquired = c.reacquisitions;
  H_CHECK(h_step(&c) && !c.holding && c.lease == &hw.lease && c.phase == TFP_PHASE_RUNNING && hw.releases == 0 && hw.enters == 1);
  H_CHECK(c.hold_resumes == 1 && c.hold_exits == 0 && c.reacquisitions == reacquired && hw.statuses == polls + 1);
  H_CHECK(!strncmp(c.hold_why, "resumed", 7));
  h_action(); /* and a second action is held again */
  H_CHECK(h_step(&c) && h_held(&c) && c.holds == 2);
  ++*count;

  name = "different weapon: once the action is over with another weapon in the hands, today's exit";
  H_CHECK(h_begin(&c, true));
  h_action();
  hw.hold.weapon = TFP_HOLD_WEAPON_OTHER; /* mid-swap: the hands change while the action still runs */
  hw.current = (TfpView){.available = false, .error = "ADS request belongs to another weapon"};
  H_CHECK(h_step(&c) && h_held(&c));
  h_safe();
  H_CHECK(h_step(&c) && h_left(&c) && c.hold_exits == 1);
  H_CHECK(!strcmp(c.label, "First person enabled; waiting: Native camera request or firearm changed"));
  ++*count;

  name = "different weapon identity in a safe full snapshot: today's exit";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  h_safe();
  hw.current.identity = H_OTHER_WEAPON, hw.hold.weapon = TFP_HOLD_WEAPON_OTHER;
  H_CHECK(h_step(&c) && h_left(&c));
  ++*count;

  name = "replaced record: another kind-6 request is not the lease's";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  h_native(0x5100);
  H_CHECK(h_step(&c) && h_left(&c) && strstr(c.hold_why, "record"));
  ++*count;

  name = "kind no longer 6";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  hw.current.camera_kind = hw.player.camera_kind = hw.hold.camera_kind = 4;
  H_CHECK(h_step(&c) && h_left(&c) && strstr(c.hold_why, "kind 6"));
  ++*count;

  name = "cap: a hold ends after 4000 ms";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  double began = hw.now;
  hw.now = began + 3.9;
  H_CHECK(tfp_controller_update(&c) && h_held(&c));
  hw.now = began + 4.0;
  H_CHECK(tfp_controller_update(&c) && h_left(&c) && strstr(c.hold_why, "4000 ms"));
  ++*count;

  static const struct { const char *name; int field; } refusals[] = {
    {"ragdoll is not held", 0}, {"seated is not held", 1}, {"full aim is not held", 2}, {"another camera owner is not held", 3},
    {"another player is not held", 4}, {"an unproven player is not held", 5}};
  for (unsigned i = 0; i < sizeof refusals / sizeof *refusals; i++) {
    name = refusals[i].name;
    H_CHECK(h_begin(&c, true));
    h_action();
    switch (refusals[i].field) {
    case 0: hw.hold.ragdoll = true; break;
    case 1: hw.hold.seated = true; break;
    case 2: hw.hold.full_aim = true; break;
    case 3: hw.hold.foreign_camera = true; break;
    case 4: hw.hold.player_identity = H_PLAYER + 1; break;
    default: hw.hold.available = false; break;
    }
    H_CHECK(h_step(&c) && h_left(&c) && c.holds == 0 && c.hold_why[0]);
    ++*count;
  }

  name = "F11 during a hold releases the lease";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  H_CHECK(tfp_controller_stop(&c, NULL) && !c.holding && !c.lease && c.phase == TFP_PHASE_IDLE && hw.releases == 1 && !c.enabled);
  ++*count;

  name = "a suspension during a hold releases the lease";
  H_CHECK(h_begin(&c, true));
  h_action();
  H_CHECK(h_step(&c) && h_held(&c));
  H_CHECK(tfp_controller_suspend(&c, "Recovering camera: test") && h_left(&c) && strstr(c.hold_why, "Recovering camera"));
  ++*count;

  name = "no hold view in the backend: as off";
  {
    static TfpBackendApi bare;
    H_CHECK(h_begin(&c, true));
    bare = hold_backend, bare.hold_view = NULL;
    c.backend = &bare;
    h_action();
    H_CHECK(h_step(&c) && h_left(&c) && hw.hold_views == 0);
    ++*count;
  }
  return true;
}

int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(CONTROLLER_VECTOR_COMMIT, commit) || strlen(CONTROLLER_VECTOR_SHA256) != 64) {
    fprintf(stderr, "controller vectors were cut from retail %s, expected %s\n", CONTROLLER_VECTOR_COMMIT, commit);
    return 2;
  }
  static const TfpBackendApi backend = {NULL, snapshot, player_snapshot, hammer, prepare, enter, status, release,
                                        packet, set_transition, clock_now, NULL};
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
  unsigned held = 0;
  if (!hold_tests(&held)) return 1;
  printf("Controller, action hold (not in retail): %u scenarios: off as before, enter, empty hands and their 1000 ms, not on foot, resume without exit, "
         "different weapon, replaced record, kind, cap, six refusals, stop and suspension\n", held);
  return 0;
}
