/* See controller.h. Line references are to retail
 * src/first_person/controller.lua at 0.5.41-smooth.66. */
#include "controller.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char HAMMER[] = "Breaching hammer: TPV animations; first-person head camera";

static void report(TfpController *c, const char *text) { snprintf(c->label, sizeof c->label, "%s", text); } /* :28 */
static void reportf(TfpController *c, const char *prefix, const char *text) {
  char line[sizeof c->label];
  snprintf(line, sizeof line, "%s%s", prefix, text);
  report(c, line);
}
/* Lua's `a or b`: the first text that is there. */
static const char *first(const char *a, const char *b) { return a && *a ? a : b; }
static bool now(TfpController *c, double *out) { /* :45 */
  double value = c->backend->clock(c->backend->user);
  if (value != value || value == HUGE_VAL || value == -HUGE_VAL) return false;
  *out = value;
  return true;
}
/* Lua's tostring of a number (LuaJIT: "%.14g"; nan and inf without C's variants). */
static void number_text(char out[32], double value) {
  if (value != value) snprintf(out, 32, "nan");
  else if (value == HUGE_VAL || value == -HUGE_VAL) snprintf(out, 32, value > 0 ? "inf" : "-inf");
  else snprintf(out, 32, "%.14g", value);
}
static void accumulate_count(bool has, double value, double *seen, double *sum) { /* :84-92 */
  if (!has || value != value || !(value >= 0) || !(value < HUGE_VAL)) return;
  if (value > *seen) {
    *sum = *sum + value - *seen;
    *seen = value;
  }
}
static void accumulate_pose_stats(TfpController *c, const TfpPoseStats *stats) { /* :81 */
  accumulate_count(stats->has_weapon_updates, stats->weapon_updates, &c->seen_weapon_updates, &c->weapon_updates);
  accumulate_count(stats->has_zero_restores, stats->zero_restores, &c->seen_zero_restores, &c->zero_restores);
  c->blend = stats->blend, c->target_weight = stats->target_weight;
  c->has_blend = true;
}
static void forget_pose_seen(TfpController *c) { c->seen_weapon_updates = c->seen_zero_restores = 0; } /* pose_seen = nil */
static bool pose_stats(TfpController *c, TfpPoseStats *out, char *why, size_t size) { /* :96 */
  memset(out, 0, sizeof *out);
  if (!c->backend->pose_status(c->backend->user, c->lease, out, why, size)) return false;
  accumulate_pose_stats(c, out);
  return true;
}
static void observe(TfpController *c, const TfpView *s) { /* :66 */
  c->samples++;
  if (s->camera_kind == 6 && !s->aiming) c->observed_native_without_aim++;
  bool aiming = s->aiming || s->controller_active;
  if (aiming != c->pose_aiming) c->aim_transitions++;
  c->pose_aiming = aiming;
}

void tfp_controller_init(TfpController *c, const TfpBackendApi *backend) {
  memset(c, 0, sizeof *c);
  c->backend = backend;
  report(c, "Idle; manual start required"); /* :11 */
  c->placement.kind[TFP_AXIS_X] = c->placement.kind[TFP_AXIS_Y] = c->placement.kind[TFP_AXIS_Z] = TFP_AXIS_NUMBER; /* :23 */
  c->placement.v[TFP_AXIS_X] = 0.15, c->placement.v[TFP_AXIS_Y] = 0, c->placement.v[TFP_AXIS_Z] = -0.12;
}

/* ---- the action hold (not in retail; see controller.h). Nothing here runs while it is off:
 * `holding` is then never set, and hold_refusal answers before it asks the backend anything. ---- */
void tfp_controller_set_hold(TfpController *c, bool enabled) { c->hold_enabled = enabled; }
static void hold_over(TfpController *c, const char *why) {
  if (!c->holding) return;
  c->holding = c->hold_empty = false;
  c->hold_exits++;
  snprintf(c->hold_why, sizeof c->hold_why, "%s", why);
}
/* NULL: the exit is held back. Otherwise why not (the switch being off answers "", and writes nothing).
 * full: this update's full snapshot when it is available, else NULL. */
static const char *hold_refusal(TfpController *c, double timestamp, const TfpView *full) {
  const TfpBackendApi *b = c->backend;
  TfpHoldView v;
  if (!c->hold_enabled) return "";
  if (!b->hold_view) return "the backend has no hold view";
  if (!c->lease || c->phase != TFP_PHASE_RUNNING || c->lease->cleanup_only) return "no running lease";
  memset(&v, 0, sizeof v);
  b->hold_view(b->user, c->lease, &v);
  if (!v.available) return "the local player or its camera request is not proven";
  if (v.player_identity != c->initial.player_identity) return "another local player";
  if (v.camera_kind != 6) return "the camera request is no longer kind 6";
  if (!c->lease->request || v.record != c->lease->request) return "the camera request record is not the lease's";
  if (v.ragdoll) return "ragdoll";
  if (v.seated) return "seated";
  if (v.full_aim) return "full aim";
  if (v.foreign_camera) return "another camera owns the view";
  /* The action's own flags are gone and another weapon is in the hands: this lease's view is not coming back. */
  if (v.safe && v.weapon == TFP_HOLD_WEAPON_OTHER) return "another weapon is in the hands";
  /* No action flag at the player's level, yet the full snapshot, which is there, is not safe: what took `safe` away
   * is not an action's flags (the character is off the verified on-foot lifecycle: a dive, prone). Today's exit. */
  if (v.safe && full && !full->safe) return "the full snapshot is unsafe with no action flag (not the on-foot lifecycle)";
  /* No action flag and nothing in the hands: the middle of a stim (about 0.65 s), or a weapon that was dropped.
   * Held for TFP_HOLD_EMPTY_CAP on end, so that a dropped weapon does not keep the native view for the whole cap. */
  if (v.safe && v.weapon == TFP_HOLD_WEAPON_NONE) {
    if (!c->hold_empty) c->hold_empty = true, c->hold_empty_since = timestamp;
    else if (timestamp - c->hold_empty_since >= TFP_HOLD_EMPTY_CAP) return "empty hands with no action flag for 1000 ms";
  } else c->hold_empty = false;
  if (c->holding && timestamp - c->hold_started >= TFP_HOLD_CAP) return "the cap was reached (4000 ms)";
  return NULL;
}

void tfp_controller_get_placement(const TfpController *c, const TfpPlacementApi *api, TfpPlacementValue *out) { /* :102 */
  TfpPlacementValue backend;
  const TfpPlacementValue *value = api->pose_placement(api->user, &backend) ? &backend : &c->placement;
  *out = *value;
  for (int axis = TFP_AXIS_YAW; axis < TFP_AXIS_N; axis++)
    if (value->kind[axis] == TFP_AXIS_NIL) out->kind[axis] = TFP_AXIS_NUMBER, out->v[axis] = 0; /* `or 0` */
}
bool tfp_controller_placement_target(const TfpController *c, const TfpPlacementApi *api, const char **key, TfpLease **lease) { /* :106 */
  const char *weapon = c->phase == TFP_PHASE_RUNNING && c->lease ? api->placement_weapon_key(api->user) : NULL;
  if (!weapon) return false;
  *key = weapon, *lease = c->lease;
  return true;
}
bool tfp_controller_set_placement(TfpController *c, const TfpPlacementApi *api, const TfpPlacementValue *value, char *why, size_t size) { /* :110 */
  TfpPlacementValue next;
  char problem[160] = "";
  bool accepted;
  if (!value) { snprintf(why, size, "Placement requires XYZ"); return false; }
  for (int axis = TFP_AXIS_X; axis <= TFP_AXIS_Z; axis++) {
    double number = value->v[axis];
    if (value->kind[axis] != TFP_AXIS_NUMBER || number != number || number < -TFP_PLACEMENT_REACH || number > TFP_PLACEMENT_REACH) {
      snprintf(why, size, "Placement must be finite XYZ within 0.5 metres");
      return false;
    }
    next.kind[axis] = TFP_AXIS_NUMBER, next.v[axis] = number;
  }
  for (int axis = TFP_AXIS_YAW; axis < TFP_AXIS_N; axis++) {
    double number = value->kind[axis] == TFP_AXIS_NIL ? 0 : value->v[axis]; /* value[axis] or 0 */
    if (value->kind[axis] == TFP_AXIS_OTHER || number != number || fabs(number) > TFP_PLACEMENT_ANGLE) {
      snprintf(why, size, "Angles must be within90degrees"); /* retail's text, as written */
      return false;
    }
    next.kind[axis] = TFP_AXIS_NUMBER, next.v[axis] = number;
  }
  /* :127 refuses a running lease of another test mode with 'Placement requires the arms/weapon
   * test'; this controller starts in the arms / weapon mode only, so that line cannot be reached. */
  if (c->phase == TFP_PHASE_RUNNING && c->lease) accepted = api->set_pose_placement(api->user, c->lease, next.v, problem, sizeof problem);
  else accepted = api->set_placement_defaults(api->user, next.v, problem, sizeof problem);
  if (!accepted) { snprintf(why, size, "%s", first(problem, "Placement was not accepted")); return false; }
  c->placement = next;
  return true;
}
int tfp_controller_status(const TfpController *c, char *out, size_t size) { /* :138 */
  char updates[32], blend[32];
  /* summary.mode is nil until the first session begins and 'pose_offset' from then on. */
  if (!c->runs) return snprintf(out, size, "%s; camera6/aimfalse observations=%u", c->label, c->observed_native_without_aim);
  number_text(updates, c->weapon_updates);
  number_text(blend, c->has_blend ? c->blend : 0);
  return snprintf(out, size, "%s; camera6/aimfalse observations=%u; arms/weapon offset updates=%s; hip blend=%s; aim transitions=%u",
                  c->label, c->observed_native_without_aim, updates, blend, c->aim_transitions);
}

bool tfp_controller_retry_cleanup(TfpController *c) { /* :156 */
  char why[160] = "";
  if (!c->lease) return true;
  c->phase = TFP_PHASE_CLEANUP_PENDING;
  c->release_attempts++;
  if (!c->backend->release(c->backend->user, c->lease, why, sizeof why)) {
    reportf(c, "Cleanup pending: ", first(why, "Native exit has not acknowledged restoration"));
    return false;
  }
  if (c->lease->has_pose_stats) accumulate_pose_stats(c, &c->lease->pose_stats); /* :165 */
  forget_pose_seen(c);
  if (c->manual && c->enabled) {
    c->lease = NULL;
    c->phase = TFP_PHASE_WAITING;
    report(c, "First person enabled; waiting for a safe released firearm camera");
    return true;
  }
  c->lease = NULL;
  c->has_ads_return = c->has_pose = false;
  c->phase = c->closing ? TFP_PHASE_DISABLED : TFP_PHASE_IDLE;
  reportf(c, "Stopped: ", c->has_stop_reason ? c->stop_reason : first(why, "manual stop"));
  return true;
}

bool tfp_controller_stop(TfpController *c, const char *reason) { /* :182 */
  hold_over(c, first(reason, "manual stop"));
  c->enabled = false;
  c->has_ads_return = c->has_pose = false;
  if (c->lease) {
    TfpPoseStats stats;
    char why[160];
    pose_stats(c, &stats, why, sizeof why);
  }
  snprintf(c->stop_reason, sizeof c->stop_reason, "%s", first(reason, "manual stop"));
  c->has_stop_reason = true;
  if (!c->lease) {
    forget_pose_seen(c);
    c->phase = c->closing ? TFP_PHASE_DISABLED : TFP_PHASE_IDLE;
    reportf(c, "Stopped: ", c->stop_reason);
    return true;
  }
  return tfp_controller_retry_cleanup(c);
}
bool tfp_controller_shutdown(TfpController *c) { /* :203 */
  c->closing = true;
  return tfp_controller_stop(c, "mod disabled");
}

static void begin_session(TfpController *c, const TfpView *current, double timestamp, TfpLease *lease) { /* :207 */
  c->lease = lease;
  c->phase = lease ? TFP_PHASE_RUNNING : TFP_PHASE_WAITING;
  c->initial.identity = current->identity;
  c->initial.player_identity = current->player_identity;
  c->initial.aiming = current->aiming;
  c->enabled = c->manual = true;
  c->next_manual_entry = timestamp + TFP_FIRST_ENTRY_DELAY;
  c->guard_until = 0;
  c->has_ads_return = false;
  c->has_pose = lease != NULL;
  c->started = timestamp;
  c->runs++;
  c->aim_transitions = c->reacquisitions = 0;
  c->weapon_updates = c->zero_restores = 0;
  c->has_blend = false;
  forget_pose_seen(c);
  c->pose_aiming = current->aiming || current->controller_active;
  c->observed_native_without_aim = c->samples = 0;
  c->has_stop_reason = false;
  c->wait_reason[0] = 0;
}

/* A gameplay or camera owner change yields only our exact former lease. :333 */
static bool manual_wait(TfpController *c, const char *reason) {
  hold_over(c, first(reason, "the lease was given up"));
  snprintf(c->wait_reason, sizeof c->wait_reason, "%s", first(reason, "Waiting for a safe released firearm camera"));
  if (c->lease) {
    if (!tfp_controller_retry_cleanup(c)) return false;
  } else c->phase = TFP_PHASE_WAITING;
  reportf(c, "First person enabled; waiting: ", c->wait_reason);
  return true;
}

/* An exit of manual_update that an action alone can cause: held back when the hold allows it, taken as before otherwise. */
static bool hold_or_wait(TfpController *c, double timestamp, const TfpView *full, const char *reason) {
  const char *refusal = hold_refusal(c, timestamp, full);
  if (refusal) {
    if (c->holding) hold_over(c, refusal);
    else if (*refusal) snprintf(c->hold_why, sizeof c->hold_why, "not held: %s", refusal);
    c->hold_empty = false;
    return manual_wait(c, reason);
  }
  if (!c->holding) {
    c->holding = true;
    c->hold_started = timestamp;
    c->holds++;
    report(c, "First person enabled; holding the native view through an action");
  }
  snprintf(c->hold_why, sizeof c->hold_why, "%s", first(reason, "Gameplay action or camera owns the view"));
  return true;
}

bool tfp_controller_suspend(TfpController *c, const char *reason) { /* :345 */
  if (c->closing || !c->enabled || !c->manual) return false;
  snprintf(c->suspend_reason, sizeof c->suspend_reason, "%s", first(reason, "Temporary capability unavailable"));
  return manual_wait(c, c->suspend_reason);
}

/* true: nothing to publish or it was accepted. :352 */
static bool publish_transition(TfpController *c, const TfpView *current, char *why, size_t size) {
  if (!c->lease || !c->backend->presentation_transition || !c->backend->set_pose_transition) return true;
  TfpPhasePacket packet = {0};
  c->backend->presentation_transition(c->backend->user, &packet);
  if (!packet.available || packet.player_identity != current->player_identity || packet.identity != current->identity)
    return true;
  char problem[160] = "";
  if (!c->backend->set_pose_transition(c->backend->user, c->lease, &packet, problem, sizeof problem)) {
    snprintf(why, size, "%s", first(problem, "Presentation phase was refused"));
    return false;
  }
  return true;
}

static bool manual_update(TfpController *c, double timestamp) { /* :364 */
  const TfpBackendApi *b = c->backend;
  TfpView current, other, *player = &current;
  char why[160] = "";
  b->snapshot(b->user, &current);
  if (!current.available) {
    b->player_snapshot(b->user, &other);
    player = &other;
  }
  if (!player->available)
    return manual_wait(c, first(other.error, first(current.error, "Local player or camera context unavailable")));
  /* An ADS request may retire before the released camera is ready. Keep only
   * its placement-entry intent; another player or weapon never inherits it. */
  if (c->has_ads_return && (player->player_identity != c->ads_return.player_identity ||
                            (current.available && current.identity != c->ads_return.identity)))
    c->has_ads_return = false;
  if (c->has_pose && current.available && current.controller_active && current.identity == c->initial.identity &&
      current.player_identity == c->initial.player_identity) {
    c->has_ads_return = true;
    c->ads_return.identity = current.identity;
    c->ads_return.player_identity = current.player_identity;
  }
  if (c->lease && player->player_identity != c->initial.player_identity)
    return manual_wait(c, "Local player or camera context changed");
  if (player->guard) c->guard_until = timestamp + TFP_GUARD_HOLD;
  if (!player->safe || (player->camera_kind != 4 && player->camera_kind != 6))
    return hold_or_wait(c, timestamp, current.available ? &current : NULL, first(player->reason, "Gameplay action or camera owns the view"));
  if (current.available) {
    observe(c, &current);
    if (!publish_transition(c, &current, why, sizeof why)) return manual_wait(c, why);
  }
  if (current.available && b->camera_only_weapon && b->camera_only_weapon(b->user, &current)) {
    /* Retire an outgoing firearm lease before the head fallback owns the view. */
    if (c->lease) return manual_wait(c, "Switching to hammer head camera");
    c->phase = TFP_PHASE_WAITING;
    snprintf(c->wait_reason, sizeof c->wait_reason, "%s", HAMMER);
    report(c, HAMMER);
    return true;
  }
  if (c->phase == TFP_PHASE_RUNNING && current.available && current.safe && current.identity == c->initial.identity &&
      current.camera_kind == 6 && (!current.record || !c->lease->request || current.record == c->lease->request)) {
    if (c->lease->cleanup_only) return tfp_controller_stop(c, "Pose helper entry requires cleanup");
    if (c->holding) { /* the same weapon, the same request, safe again: the lease simply goes on */
      c->holding = c->hold_empty = false;
      c->hold_resumes++;
      snprintf(c->hold_why, sizeof c->hold_why, "resumed: safe again with the same weapon and request");
      report(c, "First person enabled; native view kept through an action");
    }
    TfpPoseStats stats = {0};
    char problem[160] = "";
    bool have = pose_stats(c, &stats, problem, sizeof problem);
    if (!have || !stats.armed || stats.expired) {
      /* Ownership can change between the snapshot and the helper poll. A fresh
       * safe snapshot may authorize waiting, never reusing the former lease. */
      TfpView fresh, fallback, *stable = &fresh;
      b->snapshot(b->user, &fresh);
      if (!fresh.available) {
        b->player_snapshot(b->user, &fallback);
        stable = &fallback;
      }
      /* lease.pose_error or final_stats.error: the last error the helper reported, if any. */
      bool has_error = c->lease->has_pose_error || have;
      int retirement_error = c->lease->has_pose_error ? c->lease->pose_error : stats.error;
      bool retiring_owner = c->lease->pose_owner_retired || (has_error && (retirement_error == 7 || retirement_error == 8));
      if (!stable->available || stable->player_identity != c->initial.player_identity || !stable->safe ||
          (stable->camera_kind != 4 && stable->camera_kind != 6) || !fresh.available ||
          fresh.identity != c->initial.identity || fresh.camera_kind == 4 ||
          (fresh.record && c->lease->request && fresh.record != c->lease->request) || retiring_owner)
        return manual_wait(c, first(problem, "Native pose owner changed"));
      return tfp_controller_stop(c, first(problem, "Pose helper is no longer armed"));
    }
    /* A retained request can finish its own ADS return without a new lease. */
    if (!current.controller_active && stats.target_weight == 1 && stats.blend == 1) c->has_ads_return = false;
    return true;
  }
  if (c->lease) return hold_or_wait(c, timestamp, current.available ? &current : NULL, "Native camera request or firearm changed");
  c->phase = TFP_PHASE_WAITING;
  if (!current.available || !current.safe || current.camera_kind != 4 || current.aiming || current.controller_active ||
      timestamp < c->next_manual_entry || timestamp < c->guard_until) return true;
  /* Limit entry churn without limiting how many genuine swaps or ADS cycles a session may have. */
  c->next_manual_entry = timestamp + TFP_ENTRY_SPACING;
  if (!b->prepare_pose(b->user, &current, why, sizeof why)) {
    reportf(c, "First person enabled; waiting: ", first(why, "Firearm pose is not ready"));
    return true;
  }
  c->enter_attempts++;
  bool blend_entry = c->has_ads_return && c->ads_return.identity == current.identity &&
                     c->ads_return.player_identity == current.player_identity;
  TfpLease *lease = b->enter_pose(b->user, &current, 0, blend_entry, why, sizeof why);
  if (!lease) {
    reportf(c, "First person enabled; waiting: ", first(why, "Native entry was not accepted"));
    return true;
  }
  c->lease = lease;
  c->phase = TFP_PHASE_RUNNING;
  forget_pose_seen(c);
  c->has_ads_return = false;
  c->has_pose = true;
  /* Rebase only after the former lease acknowledged retirement and a later,
   * complete safe snapshot accepted entry. */
  c->initial.identity = current.identity;
  c->initial.player_identity = current.player_identity;
  c->wait_reason[0] = 0;
  c->reacquisitions++;
  if (lease->cleanup_only) return manual_wait(c, "Pose helper entry requires cleanup");
  if (!publish_transition(c, &current, why, sizeof why)) return manual_wait(c, why);
  report(c, "First person enabled; native camera reacquired");
  return true;
}

bool tfp_controller_update(TfpController *c) { /* :484 */
  if (c->phase == TFP_PHASE_CLEANUP_PENDING) return tfp_controller_retry_cleanup(c);
  if (c->phase != TFP_PHASE_RUNNING && c->phase != TFP_PHASE_WAITING) return true; /* never an entry or exit while idle */
  double timestamp;
  if (!now(c, &timestamp)) return tfp_controller_stop(c, "Monotonic clock unavailable");
  if (timestamp < c->started) return tfp_controller_stop(c, "Monotonic clock moved backwards");
  return manual_update(c, timestamp);
}

bool tfp_controller_start(TfpController *c) { /* :231, seconds 0, mode "pose_offset" */
  const TfpBackendApi *b = c->backend;
  TfpView current, other, *player = &current;
  char why[160] = "";
  double timestamp;
  if (c->closing) return false;                                  /* "Mod is stopping" */
  if (c->lease || c->phase != TFP_PHASE_IDLE) return false;      /* "A test or its cleanup is already active" */
  b->snapshot(b->user, &current);
  if (!current.available || !current.safe || current.camera_kind != 4 || current.aiming || current.controller_active) {
    if (!current.available) {
      b->player_snapshot(b->user, &other);
      player = &other;
    }
    if (!player->available) {
      reportf(c, "Start refused: ", first(other.error, first(current.error, "Stable local player unavailable")));
      return false;
    }
    if (!now(c, &timestamp)) {
      report(c, "Monotonic clock unavailable");
      return false;
    }
    /* Turning the feature on expresses intent. Actions, real ADS, seated
     * cameras and a missing firearm never authorize entry here. */
    begin_session(c, player, timestamp, NULL);
    snprintf(c->wait_reason, sizeof c->wait_reason, "%s", first(player->reason, "Waiting for a safe released firearm camera"));
    reportf(c, "First person enabled; waiting: ", c->wait_reason);
    return true;
  }
  if (b->camera_only_weapon && b->camera_only_weapon(b->user, &current)) {
    if (!now(c, &timestamp)) return false;
    begin_session(c, &current, timestamp, NULL);
    snprintf(c->wait_reason, sizeof c->wait_reason, "%s", HAMMER);
    report(c, HAMMER);
    return true;
  }
  if (!b->prepare_pose(b->user, &current, why, sizeof why)) {
    reportf(c, "Start refused before camera entry: ", first(why, "Pose helper preflight failed"));
    return false;
  }
  if (!now(c, &timestamp)) {
    report(c, "Monotonic clock unavailable");
    return false;
  }
  c->enter_attempts++;
  TfpLease *lease = b->enter_pose(b->user, &current, 0, false, why, sizeof why);
  if (!lease) {
    reportf(c, "Start refused: ", first(why, "Native entry was not accepted"));
    return false;
  }
  begin_session(c, &current, timestamp, lease);
  /* Observe entry first: an immediate owner change or a real aim takeover ends it here. */
  tfp_controller_update(c);
  if (!c->enabled) return false;
  report(c, "First person enabled until manually stopped");
  return true;
}
