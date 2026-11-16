#ifndef FPC_REQUEST_LOG_H
#define FPC_REQUEST_LOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One text line each time the game's camera request queue or the aiming
 * controller's state changes. Values only: no engine calls. The line is what
 * tools/smooth_report.py parses:
 *   queue=[4*,6*] base=4 mask=2 active=1 suppress=0 aim=0 ragdoll=0 seat=0
 * Kinds are listed oldest first; '*' marks a request owned by the local
 * player; base is the kind of the fallback row used while the queue is empty. */
#define FPC_REQUEST_ROWS 32
typedef struct FpcRequestSnapshot {
  uint32_t head, tail, count;          /* ring indices as stored; count rows follow */
  uint16_t kind[FPC_REQUEST_ROWS];     /* oldest first */
  uint32_t owner[FPC_REQUEST_ROWS];    /* row word 1 */
  uint32_t local_owner;                /* the local player's value for that word */
  uint32_t base_kind, mask;
  uint8_t active, suppress;
  bool aiming, ragdoll, seated;
} FpcRequestSnapshot;
typedef struct FpcRequestLog { bool primed; char last[512]; uint64_t second; uint32_t written, held; } FpcRequestLog;
/* true, with the line in text (cut to size), when it differs from the last
 * one. The ring moving on with the same rows is not a change, and neither is
 * one foreign owner replacing another. */
bool fpc_request_changed(FpcRequestLog *log, const FpcRequestSnapshot *now, char *text, size_t size);
/* The session log line, "Request: t=<ms> <line> held=<n>", at most 30 a second.
 * A change held back by the cap is not forgotten: the state is written as soon
 * as the cap allows, even if nothing changed again, and held counts what was
 * left out in between. */
bool fpc_request_line(FpcRequestLog *log, uint64_t milliseconds, const FpcRequestSnapshot *now,
                      char *text, size_t size);
#endif
