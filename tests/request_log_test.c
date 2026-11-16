#include "request_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      fprintf(stderr, "request log check failed: %s:%d: %s\n", __FILE__,       \
              __LINE__, #condition);                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

enum { LOCAL = 77, OTHER = 91 };

static FpcRequestSnapshot body_only(void) {
  FpcRequestSnapshot s = {.head = 3, .tail = 4, .count = 1, .local_owner = LOCAL, .base_kind = 4, .active = 1};
  s.kind[0] = 4;
  s.owner[0] = LOCAL;
  return s;
}

int main(void) {
  char text[160];
  FpcRequestLog log = {0};
  FpcRequestSnapshot s = body_only();

  /* first_snapshot_reports */
  CHECK(fpc_request_changed(&log, &s, text, sizeof text));
  CHECK(!strcmp(text, "queue=[4*] base=4 mask=0 active=1 suppress=0 aim=0 ragdoll=0 seat=0"));

  /* same_content_is_silent: the ring advanced, the rows are the same */
  s.head += 3;
  s.tail += 3;
  CHECK(!fpc_request_changed(&log, &s, text, sizeof text));

  /* kind_change */
  s.count = 2;
  s.kind[1] = 6;
  s.owner[1] = LOCAL;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text));
  CHECK(!strcmp(text, "queue=[4*,6*] base=4 mask=0 active=1 suppress=0 aim=0 ragdoll=0 seat=0"));
  CHECK(!fpc_request_changed(&log, &s, text, sizeof text));

  /* mask_change, and each flag on its own */
  s.mask = 2;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text) && strstr(text, " mask=2 "));
  s.suppress = 1;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text) && strstr(text, " suppress=1 "));
  s.aiming = true;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text) && strstr(text, " aim=1 "));
  s.ragdoll = true;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text) && strstr(text, " ragdoll=1 "));
  s.seated = true;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text) && strstr(text, " seat=1"));
  s.base_kind = 9;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text) && strstr(text, " base=9 "));

  /* foreign_owner_unstarred: same kinds, another player's request on top */
  s.owner[1] = OTHER;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text));
  CHECK(!strncmp(text, "queue=[4*,6] ", 13));
  s.owner[1] = OTHER + 1; /* still foreign: not a change */
  CHECK(!fpc_request_changed(&log, &s, text, sizeof text));

  /* empty queue */
  s = body_only();
  s.count = 0;
  CHECK(fpc_request_changed(&log, &s, text, sizeof text));
  CHECK(!strncmp(text, "queue=[] base=4 ", 16));

  /* truncation: reported, terminated, nothing written past the size */
  char small[24];
  memset(small, 'x', sizeof small);
  log = (FpcRequestLog){0};
  s = body_only();
  CHECK(fpc_request_changed(&log, &s, small, 16));
  CHECK(strlen(small) == 15 && small[16] == 'x' && !strncmp(small, "queue=[4*] base", 15));

  /* count_clamped */
  log = (FpcRequestLog){0};
  s = body_only();
  s.count = 40;
  for (unsigned i = 0; i < FPC_REQUEST_ROWS; i++) s.kind[i] = 4, s.owner[i] = OTHER;
  char big[400];
  CHECK(fpc_request_changed(&log, &s, big, sizeof big));
  unsigned commas = 0;
  for (char *c = big; *c; c++) commas += *c == ',';
  CHECK(commas == FPC_REQUEST_ROWS - 1);

  /* Review finding: a change held back by the cap is still written later. */
  log = (FpcRequestLog){0};
  s = body_only();
  unsigned lines = 0;
  for (unsigned i = 0; i < 40; i++) { /* the queue flips every frame for 40 frames */
    s.mask = i & 1 ? 2 : 0;
    lines += fpc_request_line(&log, 9000 + i * 10, &s, text, sizeof text);
  }
  CHECK(lines == 30 && s.mask == 2);
  CHECK(!fpc_request_line(&log, 9500, &s, text, sizeof text)); /* same second: still capped */
  CHECK(fpc_request_line(&log, 10000, &s, text, sizeof text));  /* unchanged since, but owed */
  CHECK(!strcmp(text, "Request: t=10000 queue=[4*] base=4 mask=2 active=1 suppress=0 aim=0 ragdoll=0 seat=0 held=11"));
  CHECK(!fpc_request_line(&log, 10016, &s, text, sizeof text));
  s.mask = 0;
  CHECK(fpc_request_line(&log, 10032, &s, text, sizeof text) && strstr(text, " held=0"));

  printf("Request recorder: %u checks; first report, silent repeats, kinds, owners, masks, flags, truncation and clamping passed\n", checks);
  return 0;
}
