#include "probe_log.h"
#include <stdio.h>
#include <string.h>

bool tfp_probe_line(TfpProbeLog *log, uint64_t milliseconds, const TfpSnapshot *s, const char *error,
                    const char *why, char *text, size_t size) {
  char line[sizeof log->last];
  if (!s) snprintf(line, sizeof line, "Snapshot: error=%s", error && *error ? error : "unknown");
  else {
    int used = snprintf(line, sizeof line,
        "Snapshot: kind=%u safe=%u active=%u mask=%u guard=%u aim=%u shoulder=%u seated=%u reload=%u body=%u rig=%u",
        (unsigned)s->camera_kind, (unsigned)s->safe, (unsigned)s->controller_active, (unsigned)s->mask,
        (unsigned)s->controller_bytes[5], (unsigned)s->aiming, (unsigned)s->shoulder_aim, (unsigned)s->seated,
        (unsigned)s->reloading, (unsigned)s->body, (unsigned)s->viewmodel);
    if (s->full) snprintf(line + used, sizeof line - (size_t)used, " weapon=%u unit=%u", (unsigned)s->weapon, (unsigned)s->weapon_unit);
    else snprintf(line + used, sizeof line - (size_t)used, " weapon=- why=%s", why && *why ? why : "unknown");
  }
  if (log->second != milliseconds / 1000) {
    log->second = milliseconds / 1000;
    log->written = 0;
  }
  if (log->primed && !strcmp(line, log->last)) return false;
  if (log->written >= 10) {
    log->held++;
    log->primed = false; /* still owed: write this state when the cap allows */
    return false;
  }
  log->primed = true;
  memcpy(log->last, line, sizeof line);
  log->written++;
  if (text && size) snprintf(text, size, "%s held=%u", line, log->held);
  log->held = 0;
  return true;
}
