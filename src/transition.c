#include "transition.h"
#include <math.h>
#include <string.h>

static bool finite_number(double n) { return n == n && fabs(n) < HUGE_VAL; }

bool tfp_transition_native_ads(bool controller_active, bool seated, const uint8_t b[6]) { /* :9-15 */
  if (!controller_active || seated || !b || b[5] != 0 || b[4] == 0) return false;
  uint32_t mask = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
  return mask == 0 || mask == 2;
}

void tfp_transition_init(TfpTransition *t) {
  memset(t, 0, sizeof *t);
  t->duration = .18;                 /* :18 */
  t->max_gap = .25;                  /* :19 */
  t->max_step = t->duration / 3;     /* :22 one slow frame spends at most a third of a blend */
  t->merge_until = .5;               /* :26 */
  t->enter_duration = t->duration;   /* round 42: retail's until tfp_transition_set_enter */
}
void tfp_transition_set_enter(TfpTransition *t, double seconds) { /* round 42, not in retail */
  if (!finite_number(seconds) || seconds > 1) return;
  t->enter_duration = seconds > 0 ? seconds : 0;
}
static void retire(TfpTransition *t) { t->has_packet = t->has_previous = false; } /* :33 */
void tfp_transition_reset(TfpTransition *t) { retire(t); }

static bool mergeable(TfpTransitionReason reason) { /* :31 */
  return reason == TFP_REASON_ZOOM || reason == TFP_REASON_SHOULDER || reason == TFP_REASON_ACTION;
}
static bool joins(const TfpTransition *t, TfpTransitionReason reason, bool ads) { /* :37 */
  return t->has_packet && mergeable(reason) && t->packet.continuous && t->packet.ads == ads &&
         t->packet.progress < t->merge_until;
}
static void advance(TfpTransition *t, double now) { /* :41 */
  /* Round 42: the packet's own duration (an ADS_ENTER's may be shorter than t->duration); with t->duration's own packets
   * this is retail's sum to the bit (packet.duration == t->duration, t->max_step == duration / 3). A packet of no duration
   * began at 1 and is not divided by. */
  const double duration = t->packet.duration;
  if (!(duration > 0)) { t->packet.progress = 1; return; }
  double elapsed = fmax(0, fmin(now - t->previous.time, duration / 3)); /* t->max_step for t->duration's packets */
  t->packet.progress = fmax(t->packet.progress, fmin(1, t->packet.progress + elapsed / duration));
}
static void begin(TfpTransition *t, TfpTransitionReason reason, bool ads, uint64_t identity, uint64_t player,
                  double now, double progress, bool continuous) {
  t->epoch++;
  /* Round 42: an ADS_ENTER takes the aim-in time; at 0 it begins done. */
  const double duration = reason == TFP_REASON_ADS_ENTER ? t->enter_duration : t->duration;
  if (!(duration > 0)) progress = 1;
  t->packet = (TfpTransitionPacket){.epoch = t->epoch, .progress = progress, .started = now,
      .duration = duration, .ads = ads, .continuous = continuous, .reason = reason,
      .merged = TFP_REASON_NONE, .identity = identity, .player_identity = player};
  t->has_packet = true;
}

bool tfp_transition_update(TfpTransition *t, const TfpTransitionInput *s, double now, bool enabled,
                           bool blocked, TfpTransitionPacket *out) { /* :46 */
  if (!enabled || blocked || !finite_number(now) || !s) {
    retire(t);
    return false;
  }
  if (t->has_previous && (now < t->previous.time || now - t->previous.time > t->max_gap ||
                          s->player_identity != t->previous.player_identity))
    retire(t); /* a new world or body, a time gap or a clock rollback is not a visual handoff */
  bool ads = s->controller_active;
  TfpTransitionReason reason = TFP_REASON_NONE;
  if (!t->has_previous) reason = TFP_REASON_INITIAL;
  else if (s->identity != t->previous.identity) reason = TFP_REASON_WEAPON;
  else if (ads != t->previous.ads) reason = ads ? TFP_REASON_ADS_ENTER : TFP_REASON_ADS_LEAVE;
  else if (s->zoom_identity != TFP_TRANSITION_NONE && t->previous.zoom_identity != TFP_TRANSITION_NONE &&
           s->zoom_identity != t->previous.zoom_identity) reason = TFP_REASON_ZOOM;
  else if (s->shoulder_aim != t->previous.shoulder_aim) reason = TFP_REASON_SHOULDER;
  else if (s->action != TFP_TRANSITION_NONE && t->previous.action != TFP_TRANSITION_NONE &&
           s->action != t->previous.action) reason = TFP_REASON_ACTION;
  else if (s->seated != t->previous.seated) reason = TFP_REASON_SEAT;
  else if (s->camera_kind != t->previous.camera_kind &&
           !((s->camera_kind == 4 || s->camera_kind == 6) &&
             (t->previous.camera_kind == 4 || t->previous.camera_kind == 6))) reason = TFP_REASON_CAMERA;
  if (reason != TFP_REASON_NONE && joins(t, reason, ads)) {
    advance(t, now);
    t->packet.merged = reason;
  } else if (reason != TFP_REASON_NONE) {
    begin(t, reason, ads, s->identity, s->player_identity, now, reason == TFP_REASON_INITIAL ? 1 : 0,
          reason != TFP_REASON_INITIAL && reason != TFP_REASON_WEAPON);
  } else if (t->has_packet) advance(t, now);
  int action = s->action != TFP_TRANSITION_NONE ? s->action
               : t->has_previous ? t->previous.action : TFP_TRANSITION_NONE;
  int zoom = s->zoom_identity != TFP_TRANSITION_NONE ? s->zoom_identity
             : t->has_previous ? t->previous.zoom_identity : TFP_TRANSITION_NONE;
  t->previous.identity = s->identity;
  t->previous.player_identity = s->player_identity;
  t->previous.ads = ads;
  t->previous.zoom_identity = zoom;
  t->previous.action = action;
  t->previous.shoulder_aim = s->shoulder_aim;
  t->previous.seated = s->seated;
  t->previous.camera_kind = s->camera_kind;
  t->previous.time = now;
  t->has_previous = true;
  if (!t->has_packet) return false;
  t->packet.ads = ads;
  if (out) *out = t->packet;
  return true;
}

bool tfp_transition_action(TfpTransition *t, int action, double now, TfpTransitionPacket *out) { /* :85 */
  if (!t->has_packet || !t->has_previous || action == TFP_TRANSITION_NONE || !finite_number(now) ||
      now < t->previous.time || now - t->previous.time > t->max_gap) return false;
  if (t->previous.action != TFP_TRANSITION_NONE && t->previous.action != action) {
    if (joins(t, TFP_REASON_ACTION, t->previous.ads)) t->packet.merged = TFP_REASON_ACTION;
    else begin(t, TFP_REASON_ACTION, t->previous.ads, t->previous.identity, t->previous.player_identity, now, 0, true);
  }
  t->previous.action = action;
  if (out) *out = t->packet;
  return true;
}

bool tfp_transition_resume(TfpTransition *t, uint64_t identity, uint64_t player, double now,
                           TfpTransitionPacket *out) { /* :97 */
  if (!t->has_packet || !t->has_previous || t->packet.identity != identity || t->packet.player_identity != player ||
      !finite_number(now) || now < t->previous.time || now - t->previous.time > t->max_gap) return false;
  begin(t, TFP_REASON_REACQUIRE, t->previous.ads, identity, player, now, 0, true);
  if (out) *out = t->packet;
  return true;
}

bool tfp_transition_result(const TfpTransition *t, TfpTransitionPacket *out) {
  if (!t->has_packet) return false;
  if (out) *out = t->packet;
  return true;
}
