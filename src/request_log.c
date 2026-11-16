#include "request_log.h"
#include <stdio.h>
#include <string.h>

bool fpc_request_changed(FpcRequestLog *log, const FpcRequestSnapshot *now, char *text, size_t size) {
  char line[sizeof log->last];
  size_t used = (size_t)snprintf(line, sizeof line, "queue=[");
  uint32_t count = now->count > FPC_REQUEST_ROWS ? FPC_REQUEST_ROWS : now->count;
  for (uint32_t i = 0; i < count; i++)
    used += (size_t)snprintf(line + used, sizeof line - used, "%s%u%s", i ? "," : "", (unsigned)now->kind[i],
                             now->owner[i] == now->local_owner ? "*" : "");
  snprintf(line + used, sizeof line - used, "] base=%u mask=%u active=%u suppress=%u aim=%u ragdoll=%u seat=%u",
           (unsigned)now->base_kind, (unsigned)now->mask, (unsigned)now->active, (unsigned)now->suppress,
           (unsigned)now->aiming, (unsigned)now->ragdoll, (unsigned)now->seated);
  if (log->primed && !strcmp(line, log->last)) return false;
  log->primed = true;
  memcpy(log->last, line, sizeof line);
  if (text && size) snprintf(text, size, "%s", line);
  return true;
}

bool fpc_request_line(FpcRequestLog *log, uint64_t milliseconds, const FpcRequestSnapshot *now,
                      char *text, size_t size) {
  char line[sizeof log->last];
  if (log->second != milliseconds / 1000) {
    log->second = milliseconds / 1000;
    log->written = 0;
  }
  if (!fpc_request_changed(log, now, line, sizeof line)) return false;
  if (log->written >= 30) {
    log->held++;
    log->primed = false; /* still owed: write this state when the cap allows */
    return false;
  }
  log->written++;
  if (text && size)
    snprintf(text, size, "Request: t=%llu %s held=%u", (unsigned long long)milliseconds, line, (unsigned)log->held);
  log->held = 0;
  return true;
}
