#include "probe_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      fprintf(stderr, "probe log check failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

int main(void) {
  TfpProbeLog log = {0};
  char text[320];
  TfpSnapshot s = {.camera_kind = 6, .safe = true, .body = 4203180, .viewmodel = 4203186, .weapon = 912,
                   .weapon_unit = 4203232, .full = true};
  CHECK(tfp_probe_line(&log, 1000, &s, NULL, NULL, text, sizeof text));
  CHECK(!strcmp(text, "Snapshot: kind=6 safe=1 active=0 mask=0 guard=0 aim=0 shoulder=0 seated=0 reload=0 body=4203180 rig=4203186 weapon=912 unit=4203232 held=0"));
  CHECK(!tfp_probe_line(&log, 1016, &s, NULL, NULL, text, sizeof text)); /* nothing changed */
  s.camera_kind = 4;
  s.safe = false;
  s.mask = 2;
  s.controller_bytes[5] = 1;
  s.controller_active = s.aiming = s.shoulder_aim = s.seated = s.reloading = true;
  CHECK(tfp_probe_line(&log, 1032, &s, NULL, NULL, text, sizeof text));
  CHECK(!strcmp(text, "Snapshot: kind=4 safe=0 active=1 mask=2 guard=1 aim=1 shoulder=1 seated=1 reload=1 body=4203180 rig=4203186 weapon=912 unit=4203232 held=0"));
  /* the player part only: no weapon */
  s.full = false;
  s.weapon = s.weapon_unit = 0;
  CHECK(tfp_probe_line(&log, 1048, &s, NULL, "Equip a firearm before starting the camera test", text, sizeof text));
  CHECK(strstr(text, " rig=4203186 weapon=- why=Equip a firearm before starting the camera test held=0"));
  /* error line */
  CHECK(tfp_probe_line(&log, 1064, NULL, "Camera is not owned by local player", NULL, text, sizeof text));
  CHECK(!strcmp(text, "Snapshot: error=Camera is not owned by local player held=0"));
  CHECK(!tfp_probe_line(&log, 1080, NULL, "Camera is not owned by local player", NULL, text, sizeof text));
  CHECK(tfp_probe_line(&log, 1096, NULL, NULL, NULL, text, sizeof text) && !strcmp(text, "Snapshot: error=unknown held=0"));

  /* probe_rate_limit: 50 changes in one second give 10 lines; the state that was
   * held back is written when the next second begins, with the count. */
  log = (TfpProbeLog){0};
  unsigned written = 0;
  s.full = true;
  for (unsigned i = 0; i < 50; i++) {
    s.weapon = 100 + i;
    written += tfp_probe_line(&log, 5000 + i * 10, &s, NULL, NULL, text, sizeof text);
  }
  CHECK(written == 10);
  CHECK(tfp_probe_line(&log, 6000, &s, NULL, NULL, text, sizeof text)); /* unchanged since, but owed */
  CHECK(strstr(text, " weapon=149 ") && strstr(text, " held=40"));
  CHECK(!tfp_probe_line(&log, 6010, &s, NULL, NULL, text, sizeof text));
  /* truncation */
  char small[24];
  memset(small, 'x', sizeof small);
  s.weapon = 7;
  CHECK(tfp_probe_line(&log, 6020, &s, NULL, NULL, small, 16) && strlen(small) == 15 && small[16] == 'x');
  printf("Probe log: %u checks; line text, silent repeats, player-only and error lines, rate limit and truncation passed\n", checks);
  return 0;
}
