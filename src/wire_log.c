/* See wire_log.h. */
#include "wire_log.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *const tfp_view_fault_names[TFP_VIEW_FAULTS] = {"fov", "blur", "camera", "near"};

void tfp_view_log_init(TfpViewLog *log, void (*write)(void *user, const char *text), void *user) {
  memset(log, 0, sizeof *log);
  log->write = write;
  log->user = user;
}
void tfp_view_log_event(TfpViewLog *log, const char *format, ...) {
  char line[640];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  if (log->write) log->write(log->user, line);
}
static const char *text(const char *value) { return value ? value : ""; }

static void close_second(TfpViewLog *log, const TfpViewSample *s) {
  char fov[96] = "fov=- fov_native=- fov_jump=-", near_text[32] = "near=-";
  if (s->has_fov) snprintf(fov, sizeof fov, "fov=%.4f fov_native=%.4f fov_jump=%.4f", s->fov_applied, s->fov_native, log->fov_jump_max);
  if (s->has_near) snprintf(near_text, sizeof near_text, "near=%.3f", s->near_applied);
  tfp_view_log_event(log, "View: t=%.3f frames=%u gaps=%u on=%d phase=%s kind=%u ads=%d safe=%d eye_step=%.4f eye_jump=%.4f %s %s%s%s",
                     s->time, log->frames, log->gaps, (int)s->enabled, text(s->phase), (unsigned)s->camera_kind,
                     (int)s->controller_active, (int)s->safe, log->step_max, log->jump_max, fov, near_text, s->extra ? " " : "",
                     text(s->extra));
  log->frames = log->gaps = 0;
  log->step_max = log->jump_max = log->fov_jump_max = 0;
  log->second_start = s->time;
}

enum { WAIT_NOTES = 8 };
static const double EDGE_SECONDS = 0.30, KEEP_SECONDS = 0.25;
static void close_edge(TfpViewLog *log, double now) {
  if (!log->edge_open) return;
  double length = now - log->edge_start;
  log->edge_open = false;
  tfp_view_log_event(log, "Cut: largest camera jump %.3f m in the %.2f s after %s the native view", log->edge_jump,
                     length < EDGE_SECONDS ? length : EDGE_SECONDS, log->edge_entering ? "entering" : "leaving");
}
static void open_edge(TfpViewLog *log, double now, bool entering) {
  close_edge(log, now);
  log->edge_open = true, log->edge_entering = entering, log->edge_start = now, log->edge_jump = 0;
}
static void gate_line(TfpViewLog *log, const char *name, const TfpViewSample *s) {
  tfp_view_log_event(log, "%s: %s | %s | %s", name, s->gate, text(s->action), text(s->camera));
  snprintf(log->gate, sizeof log->gate, "%s", s->gate);
}

void tfp_view_log_frame(TfpViewLog *log, const TfpViewSample *s) {
  const bool has_gate = s->gate && *s->gate;
  /* ---- state changes ---- */
  if (strncmp(log->label, text(s->label), sizeof log->label - 1)) {
    snprintf(log->label, sizeof log->label, "%s", text(s->label));
    if (log->primed) tfp_view_log_event(log, "Status: %s", log->label);
  }
  if (log->primed) {
    if (s->has_lease && !log->has_lease)
      tfp_view_log_event(log, "State: entered the native view request=0x%llx kind=%u", (unsigned long long)s->request, (unsigned)s->camera_kind);
    if (s->has_lease && !log->has_lease) {
      if (has_gate) { gate_line(log, "Return", s); open_edge(log, s->time, true); }
    }
    if (!s->has_lease && log->has_lease && s->enabled) {
      tfp_view_log_event(log, "State: left the native view: %s", *text(s->wait) ? s->wait : text(s->label));
      if (has_gate) { gate_line(log, "Drop", s); log->wait_notes = 0; open_edge(log, s->time, false); }
    } else if (!s->has_lease && !log->has_lease && s->enabled && log->enabled && has_gate && strncmp(log->gate, s->gate, sizeof log->gate - 1)) {
      /* On the change only, and a bounded number of times: a gate that flickers must not fill the log. */
      if (log->wait_notes < WAIT_NOTES - 1) gate_line(log, "Wait", s);
      else {
        if (log->wait_notes == WAIT_NOTES - 1) tfp_view_log_event(log, "Wait: more gate changes are not logged until the view returns");
        snprintf(log->gate, sizeof log->gate, "%s", s->gate);
      }
      if (log->wait_notes < WAIT_NOTES) log->wait_notes++;
    }
    if (!s->enabled && log->enabled && !s->has_lease) tfp_view_log_event(log, "State: exit complete");
    if (!s->enabled && !s->has_lease && log->has_lease && !log->enabled) tfp_view_log_event(log, "State: exit complete (cleanup finished)");
    if (s->enabled && s->controller_active != log->controller_active)
      tfp_view_log_event(log, "State: aim %s", s->controller_active ? "in" : "out");
    if (s->enabled && log->enabled && s->has_identity && log->has_identity && s->identity != log->identity)
      tfp_view_log_event(log, "State: swap (weapon identity changed)");
    if (s->aim_keeps != log->aim_keeps) {
      tfp_view_log_event(log, "State: aim kept (%u; %u of them at the script call, %u late presses ignored)", s->aim_keeps, s->aim_probe_keeps,
                         s->aim_guarded);
      log->has_keep = true, log->keep_time = s->time;
    }
    /* The game left full aim and the keeper had not acted just before: the release was seen too late, or not at all. */
    if (s->enabled && log->controller_active && !s->controller_active && has_gate
        && !(log->has_keep && s->time >= log->keep_time && s->time - log->keep_time <= KEEP_SECONDS))
      tfp_view_log_event(log, "Aim: release not kept (aim button from %s): %s", *text(s->aim_source) ? s->aim_source : "?", s->gate);
    if (s->seated_detaches != log->seated_detaches) tfp_view_log_event(log, "State: seated camera detached (%u)", s->seated_detaches);
  }
  for (int i = 0; i < TFP_VIEW_FAULTS; i++) {
    const char *now = text(s->fault[i]);
    if (!strncmp(log->fault[i], now, sizeof log->fault[i] - 1)) continue;
    snprintf(log->fault[i], sizeof log->fault[i], "%s", now);
    if (*now) tfp_view_log_event(log, "Refused: %s: %s", tfp_view_fault_names[i], log->fault[i]);
    else tfp_view_log_event(log, "Cleared: %s", tfp_view_fault_names[i]);
  }
  bool was_on = log->enabled || log->has_lease;
  log->primed = true;
  log->enabled = s->enabled, log->has_lease = s->has_lease, log->controller_active = s->controller_active;
  if (s->has_identity) log->identity = s->identity;
  log->has_identity = s->has_identity;
  log->aim_keeps = s->aim_keeps, log->seated_detaches = s->seated_detaches;

  /* ---- the running second ---- */
  bool on = s->enabled || s->has_lease;
  if (!on && !was_on) {
    close_edge(log, s->time);
    log->second_open = log->has_eye = log->has_step = log->has_fov = false;
    return;
  }
  if (!log->second_open) {
    log->second_open = true;
    log->second_start = s->time;
    log->frames = log->gaps = 0;
    log->step_max = log->jump_max = log->fov_jump_max = 0;
  }
  log->frames++;
  if (s->has_eye) {
    if (log->has_eye) {
      double step[3], length = 0, jump = 0;
      for (int i = 0; i < 3; i++) {
        step[i] = s->eye[i] - log->eye[i];
        length += step[i] * step[i];
        if (log->has_step) jump += (step[i] - log->step[i]) * (step[i] - log->step[i]);
      }
      length = sqrt(length), jump = sqrt(jump);
      if (length > log->step_max) log->step_max = length;
      if (log->has_step && jump > log->jump_max) log->jump_max = jump;
      if (log->edge_open && log->has_step && jump > log->edge_jump) log->edge_jump = jump;
      memcpy(log->step, step, sizeof step);
      log->has_step = true;
    }
    memcpy(log->eye, s->eye, sizeof log->eye);
    log->has_eye = true;
  } else {
    log->gaps++;
    log->has_eye = log->has_step = false;
  }
  if (s->has_fov) {
    if (log->has_fov) {
      double step = s->fov_applied - log->fov, jump = fabs(step - log->fov_step);
      if (jump > log->fov_jump_max) log->fov_jump_max = jump;
      log->fov_step = step;
    } else log->fov_step = 0;
    log->fov = s->fov_applied;
    log->has_fov = true;
  } else log->has_fov = false;
  if (log->edge_open && (s->time - log->edge_start >= EDGE_SECONDS || s->time < log->edge_start || !on)) close_edge(log, s->time);
  if (s->time - log->second_start >= 1 || s->time < log->second_start || !on) close_second(log, s);
  if (!on) log->second_open = log->has_eye = log->has_step = log->has_fov = false;
}
