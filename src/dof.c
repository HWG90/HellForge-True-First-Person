#include "dof.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A raised Lua error: its text, or the error value nil. */
typedef struct Raised { bool is_nil; char text[TFP_DOF_TEXT]; } Raised;

/* assert(false, message) or a runtime error at retail line `line`: Lua puts the position before the text. */
static bool fail(Raised *e, int line, const char *message) {
  e->is_nil = false;
  snprintf(e->text, sizeof e->text, "%s:%d: %s", TFP_DOF_LUA_CHUNK, line, message);
  return false;
}
/* An error raised by the host passes through unchanged. */
static bool raised(Raised *e, const char *text) {
  e->is_nil = text == NULL;
  if (text) snprintf(e->text, sizeof e->text, "%s", text);
  return false;
}
static const char *text_of(const Raised *e) { return e->is_nil ? "nil" : e->text; }

static const char *message_of(const TfpDofSetting *s) { return s->has_message ? s->message : NULL; }
static const char *issue_of(const TfpDofRecord *r) { return r->has_issue ? r->issue : NULL; }
static TfpDofPhase stage_phase(TfpDofStage stage) {
  /* NONE cannot arrive here: a record without a stage is always cancelled (see update). */
  return stage == TFP_DOF_STAGE_APPLYING ? TFP_DOF_APPLYING : stage == TFP_DOF_STAGE_RESTORING ? TFP_DOF_RESTORING : TFP_DOF_ACTIVE;
}
static TfpDofPhase record_phase(const TfpDofRecord *r) { return r->cancel ? TFP_DOF_CLEANUP_PENDING : stage_phase(r->stage); }

/* retail report(value, why), lines 21-25. */
static void report(TfpDofSetting *s, TfpDofPhase phase, const char *why) {
  char text[TFP_DOF_TEXT], line[TFP_DOF_TEXT + 32];
  s->phase = phase;
  if (why) {
    snprintf(text, sizeof text, "%s", why);
    if (!(s->has_message && strcmp(text, s->message) == 0) && s->host->log) {
      snprintf(line, sizeof line, "FPV %s: %s", s->label, text);
      s->host->log(s->host->user, line);
    }
    memcpy(s->message, text, sizeof text);
  }
  s->has_message = why != NULL;
}

/* retail now(), lines 26-33. */
static bool now(TfpDofSetting *s, double *out, Raised *e) {
  const TfpDofHost *h = s->host;
  const char *error = NULL;
  double value = 0;
  if (!h->clock(h->user, &value, &error)) return raised(e, error);
  if (!(value == value && fabs(value) < HUGE_VAL)) return fail(e, 27, "DoF clock unavailable");
  if (s->has_last_time && value < s->last_time) {
    s->next_attempt = 0;
    if (s->has_record) {
      s->record.next_poll = value; s->record.has_next_poll = true;
      s->record.sent = value;
    }
  }
  s->last_time = value; s->has_last_time = true;
  *out = value;
  return true;
}

/* retail context(), lines 34-40: false is `nil, 'Renderer DoF setting API unavailable'`. */
static bool context(TfpDofSetting *s, TfpDofBinding *out) {
  memset(out, 0, sizeof *out);
  return s->host->application_binding(s->host->user, out);
}
static bool same(const TfpDofBinding *a, const TfpDofBinding *b) { return a->app == b->app && a->get == b->get && a->set == b->set; }
/* same(context(), r) */
static bool owns(TfpDofSetting *s, const TfpDofBinding *r) {
  TfpDofBinding c;
  return context(s, &c) && same(&c, r);
}

/* retail foreign(), lines 42-49: whether another camera holds the view, and the reason (always a
 * text when held). `why` has TFP_DOF_TEXT bytes. */
static bool foreign(TfpDofSetting *s, char *why) {
  const TfpDofHost *h = s->host;
  TfpDofForeign out = {false, NULL, NULL};
  const char *error = NULL, *reason;
  bool ok, held;
  why[0] = 0;
  if (s->compat && s->compat->foreign) ok = s->compat->foreign(s->compat->user, &out, &error);
  else if (h->backend_foreign_camera) ok = h->backend_foreign_camera(h->user, &out, &error);
  else return false;
  held = !ok || !out.is_false;
  if (!ok) reason = error ? error : "nil";
  else reason = out.why ? out.why : out.value_text ? out.value_text : "nil";
  snprintf(why, TFP_DOF_TEXT, "%s", reason);
  return held;
}

/* retail check(c), lines 50-66. */
static bool check(TfpDofSetting *s, const TfpDofBinding *c, Raised *e) {
  const TfpDofHost *h = s->host;
  const char *error = NULL;
  if (s->has_verified && same(c, &s->verified)) return true;
  if (h->verify) {
    bool is_true = false;
    if (!h->verify(h->user, c->app, &is_true, &error)) return raised(e, error);
    if (!is_true) return fail(e, 52, "Renderer DoF binding contract changed");
  } else {
    uint64_t exe = 0;
    uint8_t bytes[8];
    if (!h->backend_profile_pose) return fail(e, 60, "Renderer executable profile unavailable");
    if (!h->backend_alignment_executable(h->user, &exe, &error)) return raised(e, error);
    if (!h->backend_read(h->user, exe + TFP_DOF_GET_THUNK, 8, bytes, &error)) return raised(e, error);
    if (memcmp(bytes, TFP_DOF_GET_BYTES, 8) != 0) return fail(e, 62, "Renderer DoF SDK contract changed");
    if (!h->backend_read(h->user, exe + TFP_DOF_SET_THUNK, 8, bytes, &error)) return raised(e, error);
    if (memcmp(bytes, TFP_DOF_SET_BYTES, 8) != 0) return fail(e, 62, "Renderer DoF SDK contract changed");
  }
  s->verified = *c; s->has_verified = true;
  return true;
}

/* retail current(r, monitor), lines 67-78. */
static bool current(TfpDofSetting *s, const TfpDofBinding *binding, bool monitor, bool *value, Raised *e) {
  const TfpDofHost *h = s->host;
  const TfpDofBinding r = *binding;
  TfpDofBinding c;
  TfpDofValue got = TFP_DOF_VALUE_OTHER;
  const char *error = NULL;
  if (!context(s, &c)) return fail(e, 68, "Renderer DoF setting API unavailable");
  if (!same(&c, &r)) return fail(e, 68, "Renderer DoF binding ownership changed");
  if (monitor && h->backend_renderer_dof_value) {
    got = h->backend_renderer_dof_value(h->user, &r, s->key);
    if (!owns(s, &r)) return fail(e, 71, "Renderer DoF binding ownership changed");
    if (got != TFP_DOF_VALUE_OTHER) {
      s->reads++; s->cached_reads++;
      *value = got == TFP_DOF_VALUE_TRUE;
      return true;
    }
    s->cache_fallbacks++;
  }
  if (!h->application_render_setting(h->user, &r, s->key, &got, &error)) return raised(e, error);
  s->reads++;
  if (got == TFP_DOF_VALUE_OTHER) return fail(e, 76, "Renderer DoF value is unavailable");
  *value = got == TFP_DOF_VALUE_TRUE;
  return true;
}

/* retail retire(), lines 79-81. */
static void retire(TfpDofSetting *s) {
  s->has_record = false; s->next_attempt = 0; s->restores++;
  report(s, TFP_DOF_IDLE, NULL);
}

/* retail command(r, value, time), lines 82-99; r is the record. */
static bool command(TfpDofSetting *s, bool value, double time, Raised *e) {
  const TfpDofHost *h = s->host;
  TfpDofRecord *r = &s->record;
  char why[TFP_DOF_TEXT];
  const char *error = NULL;
  TfpDofSet sent;
  bool observed = false;
  if (!owns(s, &r->binding)) return fail(e, 83, "Renderer DoF binding ownership changed");
  if (foreign(s, why)) return fail(e, 84, why);
  /* Own the uncertain command before calling a binding which can throw after enqueueing it. */
  r->stage = value ? TFP_DOF_STAGE_RESTORING : TFP_DOF_STAGE_APPLYING;
  r->next_poll = time + TFP_DOF_INTERVAL; r->has_next_poll = true;
  r->sent = time;
  sent = h->application_set_render_setting(h->user, &r->binding, s->key, value, &error);
  s->writes++;
  if (sent != TFP_DOF_SET_DONE) {
    r->cancel = true;
    snprintf(r->issue, sizeof r->issue, "%s", sent == TFP_DOF_SET_REFUSED ? "Renderer DoF command was refused" : error ? error : "nil");
    r->has_issue = true;
  }
  if (foreign(s, why)) { r->cancel = true; return true; }
  if (!current(s, &r->binding, false, &observed, e)) return false;
  if (observed == value) {
    if (value) retire(s);
    else r->stage = TFP_DOF_STAGE_ACTIVE;
  }
  return true;
}

/* retail advance(time, force), lines 100-137. *done is its first result; its second is the
 * message when *done is false and nil when true. */
static bool advance(TfpDofSetting *s, double time, bool force, bool *done, Raised *e) {
  TfpDofRecord *r = &s->record;
  char why[TFP_DOF_TEXT];
  const char *reason;
  bool value = false;
  double waited;
  *done = true;
  if (!s->has_record) return true;
  *done = false;
  if (foreign(s, why)) { r->cancel = true; report(s, TFP_DOF_CLEANUP_PENDING, why); return true; }
  if (!owns(s, &r->binding)) return fail(e, 104, "Renderer DoF binding ownership changed; original restoration remains pending");
  if (!force) {
    if (!r->has_next_poll) return fail(e, 105, "attempt to compare number with nil");
    if (time < r->next_poll) { report(s, record_phase(r), issue_of(r)); return true; }
  }
  /* Only unchanged, acknowledged ownership uses the nonblocking cached observer. */
  if (!current(s, &r->binding, r->stage == TFP_DOF_STAGE_ACTIVE && !r->cancel, &value, e)) return false;
  r->next_poll = time + TFP_DOF_INTERVAL; r->has_next_poll = true;
  if (r->stage == TFP_DOF_STAGE_RESTORING) {
    if (value) { retire(s); *done = true; return true; }
  } else if (r->stage == TFP_DOF_STAGE_APPLYING) {
    if (!value) r->stage = TFP_DOF_STAGE_ACTIVE;
  } else if (value) {
    s->foreign_changes++;
    if (s->mandatory && !r->cancel) {
      /* The owned gameplay camera still wants this effect suppressed. */
      s->reassertions++;
      if (!command(s, false, time, e)) return false;
      if (s->has_record) report(s, record_phase(r), issue_of(r));
      return true;
    }
    s->has_record = false; s->yielded = true;
    snprintf(why, sizeof why, "Renderer %s was changed externally", s->label);
    report(s, TFP_DOF_FOREIGN, why);
    *done = true;
    return true;
  }
  if (r->cancel && r->stage == TFP_DOF_STAGE_ACTIVE) {
    if (!command(s, true, time, e)) return false;
    if (!s->has_record) { *done = true; return true; }
  }
  reason = issue_of(r);
  waited = time - r->sent;
  if (waited > 2 && r->stage != TFP_DOF_STAGE_ACTIVE && !reason) reason = "Renderer DoF command acknowledgement pending";
  report(s, record_phase(r), reason);
  return true;
}

/* retail actual_view(controller, camera), lines 138-149. */
static bool actual_view(TfpDofSetting *s, bool *view, Raised *e) {
  const TfpDofHost *h = s->host;
  TfpDofControllerState state = {false, false};
  const char *error = NULL;
  bool is_true = false;
  *view = false;
  if (h->active_view) {
    if (!h->active_view(h->user, &is_true, &error)) return raised(e, error);
    *view = is_true;
    return true;
  }
  if (!h->controller_is_enabled || !h->controller_is_enabled(h->user)) return true;
  h->controller_result(h->user, &state);
  if (!state.pose_offset) return true;
  if (state.running) { *view = true; return true; }
  if (h->fallback_ready) {
    if (!h->fallback_ready(h->user, &is_true, &error)) return raised(e, error);
    *view = is_true;
  }
  return true;
}

void tfp_dof_setting_init(TfpDofSetting *s, const TfpDofHost *host, const char *setting) {
  memset(s, 0, sizeof *s);
  s->host = host;
  snprintf(s->key, sizeof s->key, "%s", setting ? setting : "dof_enabled");
  s->label = strcmp(s->key, "dof_enabled") == 0 ? "DoF" : "vignette";
  /* Weapon/optic transitions can enable either effect during the same FP view. */
  s->mandatory = strcmp(s->key, "dof_enabled") == 0 || strcmp(s->key, "vignette_enabled") == 0;
  s->phase = TFP_DOF_IDLE;
}

bool tfp_dof_setting_is_suppressed(TfpDofSetting *s) {
  const TfpDofRecord *r = &s->record;
  char why[TFP_DOF_TEXT];
  if (s->closing || !s->has_record || r->stage != TFP_DOF_STAGE_ACTIVE || r->cancel || s->phase != TFP_DOF_ACTIVE) return false;
  if (!owns(s, &r->binding)) return false;
  return !foreign(s, why);
}

static bool answer(const TfpDofSetting *s, bool value, bool with_message, const char **why) {
  if (why) *why = with_message ? message_of(s) : NULL;
  return value;
}

bool tfp_dof_setting_stop(TfpDofSetting *s, const char **why) {
  Raised e;
  double time = 0;
  bool first, done = false;
  s->session = false; s->preserved = false; s->yielded = false; s->next_attempt = 0;
  if (!s->has_record) { report(s, TFP_DOF_IDLE, NULL); return answer(s, true, false, why); }
  first = !s->record.cancel;
  s->record.cancel = true;
  if (!(now(s, &time, &e) && advance(s, time, first, &done, &e))) {
    report(s, TFP_DOF_CLEANUP_PENDING, text_of(&e));
    return answer(s, false, true, why);
  }
  return answer(s, done, !done, why);
}
bool tfp_dof_setting_shutdown(TfpDofSetting *s, const char **why) {
  s->closing = true;
  return tfp_dof_setting_stop(s, why);
}

/* The protected body of update(), retail lines 173-208. */
static bool attempt(TfpDofSetting *s, Raised *e) {
  TfpDofBinding c;
  char held_reason[TFP_DOF_TEXT], why[TFP_DOF_TEXT];
  double time = 0;
  bool held, wanted = false, done = false, value = false, view = false;
  if (!now(s, &time, e)) return false;
  held = foreign(s, held_reason);
  if (!s->closing && !held && !actual_view(s, &wanted, e)) return false;
  if (!wanted) {
    s->session = false; s->preserved = false; s->yielded = false; s->next_attempt = 0;
    if (s->has_record) {
      bool first = !s->record.cancel;
      s->record.cancel = true;
      /* The view/foreign gate is immediate; renderer polling and pending cleanup are bounded. */
      return advance(s, time, !held && first, &done, e);
    }
    report(s, held ? TFP_DOF_FOREIGN : TFP_DOF_IDLE, held ? held_reason : NULL);
    return true;
  }
  if (s->has_record) return advance(s, time, false, &done, e);
  if (!context(s, &c)) {
    if (time >= s->next_attempt) {
      report(s, TFP_DOF_UNAVAILABLE, "Renderer DoF setting API unavailable");
      s->next_attempt = time + TFP_DOF_INTERVAL;
    }
    return true;
  }
  if (s->session && !(s->has_verified && same(&c, &s->verified))) { s->session = false; s->preserved = false; s->yielded = false; }
  if ((s->preserved || s->yielded) && !s->mandatory) return true;
  if (time < s->next_attempt) return true;
  s->next_attempt = time + TFP_DOF_INTERVAL;
  if (!check(s, &c, e)) return false;
  s->session = true;
  if (!current(s, &c, s->mandatory && s->preserved, &value, e)) return false;
  if (!value) { s->preserved = true; report(s, TFP_DOF_ALREADY_DISABLED, NULL); return true; }
  /* A cached monitor is enough to observe an initially false value; reacquisition still fences
   * through the SDK getter before write. */
  if (s->mandatory && s->preserved && !current(s, &c, false, &value, e)) return false;
  if (!value) { report(s, TFP_DOF_ALREADY_DISABLED, NULL); return true; }
  s->preserved = false; s->yielded = false;
  /* Recheck a synchronously changed view/foreign lease before write. */
  if (foreign(s, why)) return true;
  if (!actual_view(s, &view, e)) return false;
  if (!view) return true;
  memset(&s->record, 0, sizeof s->record);
  s->record.binding = c; s->record.before = value; s->record.cancel = false; s->record.sent = time;
  s->has_record = true;
  if (!command(s, false, time, e)) return false;
  if (s->has_record) report(s, record_phase(&s->record), issue_of(&s->record));
  return true;
}
bool tfp_dof_setting_update(TfpDofSetting *s, const TfpDofCompat *camera_compat, const char **message) {
  Raised e;
  if (camera_compat) s->compat = camera_compat;
  if (!attempt(s, &e)) {
    report(s, s->has_record ? TFP_DOF_CLEANUP_PENDING : TFP_DOF_UNAVAILABLE, text_of(&e));
    if (s->has_record) s->record.cancel = true;
  }
  /* Missing settings and deferred restoration are auxiliary capability states. */
  return answer(s, true, true, message);
}

void tfp_dof_setting_result(const TfpDofSetting *s, TfpDofSettingResult *out) {
  out->phase = s->phase;
  out->owned = s->has_record;
  out->cleanup_pending = s->has_record && s->record.cancel;
  out->command_pending = s->has_record && s->record.stage != TFP_DOF_STAGE_ACTIVE;
  out->disabled = s->has_record && s->record.stage == TFP_DOF_STAGE_ACTIVE;
  out->reads = s->reads; out->writes = s->writes; out->restores = s->restores; out->foreign_changes = s->foreign_changes;
  out->cached_reads = s->cached_reads; out->cache_fallbacks = s->cache_fallbacks; out->reassertions = s->reassertions;
  out->error_message = message_of(s);
  out->closing = s->closing;
}
const char *tfp_dof_setting_status(TfpDofSetting *s) {
  snprintf(s->status, sizeof s->status, "%s; %s disabled=%s; %s", tfp_dof_phase_name(s->phase), s->label,
           s->has_record && s->record.stage == TFP_DOF_STAGE_ACTIVE ? "true" : "false", s->has_message ? s->message : "");
  return s->status;
}
const char *tfp_dof_phase_name(TfpDofPhase phase) {
  static const char *const names[] = {"idle", "applying", "restoring", "active", "cleanup_pending", "foreign", "unavailable", "already_disabled"};
  return names[phase];
}
const char *tfp_dof_stage_name(TfpDofStage stage) {
  static const char *const names[] = {NULL, "applying", "restoring", "active"};
  return names[stage];
}

/* retail Dof.new, lines 231-265. */
void tfp_dof_init(TfpDof *d, const TfpDofHost *host, const char *setting, TfpDofVignette kind) {
  memset(d, 0, sizeof *d);
  d->host = host; d->kind = kind;
  tfp_dof_setting_init(&d->dof, host, setting);
  if (kind == TFP_DOF_VIGNETTE_SETTING) tfp_dof_setting_init(&d->vignette, host, "vignette_enabled");
}
static TfpDofAnswer truth(bool value) { return value ? TFP_DOF_TRUE : TFP_DOF_FALSE; }
/* `first and other, why or problem` */
static TfpDofAnswer both(bool first, const char *why, TfpDofAnswer other, const char *problem, const char **out) {
  if (other == TFP_DOF_RAISED) { if (out) *out = problem; return other; }
  if (out) *out = why ? why : problem;
  return first ? other : TFP_DOF_FALSE;
}
TfpDofAnswer tfp_dof_update(TfpDof *d, const TfpDofCompat *camera_compat, const char **why) {
  const char *message = NULL, *problem = NULL;
  bool ready = tfp_dof_setting_update(&d->dof, camera_compat, &message);
  TfpDofAnswer other;
  if (d->kind == TFP_DOF_VIGNETTE_NONE) { if (why) *why = message; return truth(ready); }
  if (d->kind == TFP_DOF_VIGNETTE_SETTING) other = truth(tfp_dof_setting_update(&d->vignette, camera_compat, &problem));
  else other = d->host->vignette_update(d->host->user, camera_compat, &problem);
  return both(ready, message, other, problem, why);
}
TfpDofAnswer tfp_dof_stop(TfpDof *d, const char **why) {
  const char *message = NULL, *problem = NULL;
  bool done = tfp_dof_setting_stop(&d->dof, &message);
  TfpDofAnswer other;
  if (d->kind == TFP_DOF_VIGNETTE_NONE) { if (why) *why = message; return truth(done); }
  if (d->kind == TFP_DOF_VIGNETTE_SETTING) other = truth(tfp_dof_setting_stop(&d->vignette, &problem));
  else other = d->host->vignette_stop(d->host->user, &problem);
  return both(done, message, other, problem, why);
}
TfpDofAnswer tfp_dof_shutdown(TfpDof *d, const char **why) {
  const char *message = NULL, *problem = NULL;
  bool done = tfp_dof_setting_shutdown(&d->dof, &message);
  TfpDofAnswer other;
  if (d->kind == TFP_DOF_VIGNETTE_NONE) { if (why) *why = message; return truth(done); }
  if (d->kind == TFP_DOF_VIGNETTE_SETTING) other = truth(tfp_dof_setting_shutdown(&d->vignette, &problem));
  else other = d->host->vignette_shutdown(d->host->user, &problem);
  return both(done, message, other, problem, why);
}
bool tfp_dof_is_clean(TfpDof *d) {
  if (!tfp_dof_setting_is_clean(&d->dof)) return false;
  if (d->kind == TFP_DOF_VIGNETTE_NONE) return true;
  if (d->kind == TFP_DOF_VIGNETTE_SETTING) return tfp_dof_setting_is_clean(&d->vignette);
  return d->host->vignette_is_clean(d->host->user);
}
bool tfp_dof_is_suppressed(TfpDof *d) { return tfp_dof_setting_is_suppressed(&d->dof); }
/* `a or b` */
static TfpDofAnswer either(bool a, TfpDofAnswer b) { return a ? TFP_DOF_TRUE : b; }
void tfp_dof_result(TfpDof *d, TfpDofResult *out) {
  memset(out, 0, sizeof *out);
  tfp_dof_setting_result(&d->dof, &out->dof);
  out->kind = d->kind;
  out->owned = truth(out->dof.owned); out->cleanup_pending = truth(out->dof.cleanup_pending); out->command_pending = truth(out->dof.command_pending);
  if (d->kind == TFP_DOF_VIGNETTE_NONE) return;
  if (d->kind == TFP_DOF_VIGNETTE_SETTING) {
    tfp_dof_setting_result(&d->vignette, &out->vignette_setting);
    out->vignette.owned = truth(out->vignette_setting.owned); out->vignette.cleanup_pending = truth(out->vignette_setting.cleanup_pending);
    out->vignette.command_pending = truth(out->vignette_setting.command_pending);
  } else d->host->vignette_result(d->host->user, &out->vignette);
  out->owned = either(out->dof.owned, out->vignette.owned);
  out->cleanup_pending = either(out->dof.cleanup_pending, out->vignette.cleanup_pending);
  out->command_pending = either(out->dof.command_pending, out->vignette.command_pending);
}
const char *tfp_dof_status(TfpDof *d) {
  const char *dof = tfp_dof_setting_status(&d->dof), *vignette;
  if (d->kind == TFP_DOF_VIGNETTE_NONE) return dof;
  vignette = d->kind == TFP_DOF_VIGNETTE_SETTING ? tfp_dof_setting_status(&d->vignette) : d->host->vignette_status(d->host->user);
  snprintf(d->status, sizeof d->status, "%s; vignette %s", dof, vignette);
  return d->status;
}
