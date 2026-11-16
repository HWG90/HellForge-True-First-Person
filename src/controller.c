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
static bool pose_stats(TfpController *c, TfpPoseStats *out, char *why, size_t size) { /* :92 */
  return c->backend->pose_status(c->backend->user, c->lease, out, why, size);
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
  c->pose_aiming = current->aiming || current->controller_active;
  c->observed_native_without_aim = c->samples = 0;
  c->has_stop_reason = false;
  c->wait_reason[0] = 0;
}

/* A gameplay or camera owner change yields only our exact former lease. :333 */
static bool manual_wait(TfpController *c, const char *reason) {
  snprintf(c->wait_reason, sizeof c->wait_reason, "%s", first(reason, "Waiting for a safe released firearm camera"));
  if (c->lease) {
    if (!tfp_controller_retry_cleanup(c)) return false;
  } else c->phase = TFP_PHASE_WAITING;
  reportf(c, "First person enabled; waiting: ", c->wait_reason);
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
    return manual_wait(c, first(player->reason, "Gameplay action or camera owns the view"));
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
  if (c->lease) return manual_wait(c, "Native camera request or firearm changed");
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
