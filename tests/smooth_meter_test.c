#include "smooth_meter.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition)                                                       \
  do {                                                                         \
    checks++;                                                                  \
    if (!(condition)) {                                                        \
      fprintf(stderr, "smooth meter check failed: %s:%d: %s\n", __FILE__,      \
              __LINE__, #condition);                                           \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

#define PI 3.14159265358979323846
static const float DEG = (float)(PI / 180);

/* View turned by yaw about up (z), quaternion xyzw. */
static void yaw_rotation(float yaw, float out[4]) {
  out[0] = 0;
  out[1] = 0;
  out[2] = sinf(yaw / 2);
  out[3] = cosf(yaw / 2);
}

/* A player standing at the origin, looking along +y, gun in front of the eye. */
static FpcMeterSample still(void) {
  FpcMeterSample s = {.dt = 0.01, .player_generation = 7, .eye = {0, 0.1f, 1.6f},
                      .vertical_fov = 60 * DEG, .near_clip = 0.05f, .socket = {0.15f, 0.5f, 1.45f},
                      .socket_valid = true};
  yaw_rotation(0, s.rotation);
  return s;
}

/* Turns the view and carries the socket with it, as a held gun does. */
static void look(FpcMeterSample *s, float yaw) {
  FpcMeterSample base = still();
  float dx = base.socket[0] - base.eye[0], dy = base.socket[1] - base.eye[1];
  yaw_rotation(yaw, s->rotation);
  s->socket[0] = s->eye[0] + cosf(yaw) * dx - sinf(yaw) * dy;
  s->socket[1] = s->eye[1] + sinf(yaw) * dx + cosf(yaw) * dy;
  s->socket[2] = s->eye[2] + base.socket[2] - base.eye[2];
}

static FpcMeter settled(FpcMeterSample *s) {
  FpcMeter meter = {0};
  FpcJump jump;
  *s = still();
  for (unsigned i = 0; i < 10; i++)
    CHECK(!fpc_meter_step(&meter, s, &jump));
  return meter;
}

static void unprimed_never_reports(void) {
  FpcMeter meter = {0};
  FpcJump jump;
  FpcMeterSample s = still();
  s.eye[0] = 500;
  s.vertical_fov = 20 * DEG;
  CHECK(!fpc_meter_step(&meter, &s, &jump) && jump.channels == 0);
}

static void steady_motion_is_silent(void) {
  FpcMeter meter = {0};
  FpcJump jump;
  FpcMeterSample s = still();
  for (unsigned i = 0; i < 200; i++) {
    float t = (float)i * 0.01f;
    s.body[1] = 3 * t;
    s.eye[1] = 0.1f + 3 * t;
    look(&s, 90 * DEG * t);
    CHECK(!fpc_meter_step(&meter, &s, &jump));
  }
}

static void eye_step(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.eye[0] += 0.25f;
  s.socket[0] += 0.25f;
  CHECK(fpc_meter_step(&meter, &s, &jump));
  CHECK(jump.channels == FPC_JUMP_EYE && fabsf(jump.eye_m - 0.25f) < 0.005f);
}

static void one_jump_one_report(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.eye[0] += 0.25f;
  s.socket[0] += 0.25f;
  CHECK(fpc_meter_step(&meter, &s, &jump));
  for (unsigned i = 0; i < 5; i++)
    CHECK(!fpc_meter_step(&meter, &s, &jump));
}

static void turn_step(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  look(&s, 46 * DEG);
  CHECK(fpc_meter_step(&meter, &s, &jump));
  CHECK(jump.channels == FPC_JUMP_TURN && fabsf(jump.turn_deg - 46) < 0.5f);
}

static void fast_turn_is_not_a_jump(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  for (unsigned i = 1; i <= 60; i++) { /* 8 degrees in the first frame, then 600 deg/s */
    look(&s, (8 + 6 * (float)(i - 1)) * DEG);
    CHECK(!fpc_meter_step(&meter, &s, &jump));
  }
}

static void fov_step(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.vertical_fov = 90 * DEG;
  CHECK(fpc_meter_step(&meter, &s, &jump));
  CHECK(jump.channels == FPC_JUMP_FOV && fabsf(jump.fov_deg - 30) < 0.01f);
  meter = settled(&s);
  for (unsigned i = 1; i <= 18; i++) {
    float t = (float)i / 18;
    s.vertical_fov = (60 + 30 * t * t * (3 - 2 * t)) * DEG;
    CHECK(!fpc_meter_step(&meter, &s, &jump));
  }
}

static void near_step(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.near_clip = 0.22f;
  CHECK(fpc_meter_step(&meter, &s, &jump));
  CHECK(jump.channels == FPC_JUMP_NEAR && fabsf(jump.near_m - 0.17f) < 0.001f);
}

static void socket_step(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.socket[2] -= 0.371f;
  CHECK(fpc_meter_step(&meter, &s, &jump));
  CHECK(jump.channels == FPC_JUMP_SOCKET && fabsf(jump.socket_m - 0.371f) < 0.001f);
  meter = settled(&s);
  s.socket_valid = false; /* gun put away, then back somewhere else */
  CHECK(!fpc_meter_step(&meter, &s, &jump));
  s.socket_valid = true;
  s.socket[0] += 1;
  CHECK(!fpc_meter_step(&meter, &s, &jump));
}

static void hitch_resets(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.dt = 0.4;
  s.eye[0] += 2;
  CHECK(!fpc_meter_step(&meter, &s, &jump));
  s.dt = 0.01;
  CHECK(!fpc_meter_step(&meter, &s, &jump));
  s.dt = 0;
  s.eye[0] += 2;
  CHECK(!fpc_meter_step(&meter, &s, &jump));
}

static void respawn_resets(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  s.player_generation = 8;
  s.body[0] = 300;
  s.eye[0] = 300.4f;
  look(&s, 120 * DEG);
  CHECK(!fpc_meter_step(&meter, &s, &jump));
  CHECK(!fpc_meter_step(&meter, &s, &jump));
}

static void body_turn_is_silent(void) {
  FpcMeter meter = {0};
  FpcJump jump;
  FpcMeterSample s = still();
  for (unsigned i = 0; i < 100; i++) { /* body spins 300 deg/s, eye rides 0.2 m ahead of the root */
    float yaw = 300 * DEG * (float)i * 0.01f;
    s.eye[0] = -sinf(yaw) * 0.2f;
    s.eye[1] = cosf(yaw) * 0.2f;
    look(&s, yaw);
    CHECK(!fpc_meter_step(&meter, &s, &jump));
  }
}

/* Review finding: a motion that starts abruptly is one report, not one per frame. */
static void flick_reports_once(void) {
  FpcMeterSample s;
  FpcMeter meter = settled(&s);
  FpcJump jump;
  unsigned reports = 0;
  s.dt = 1.0 / 60;
  for (unsigned i = 1; i <= 32; i++) { /* 900 deg/s reached in one frame */
    look(&s, 15 * (float)i * DEG);
    reports += fpc_meter_step(&meter, &s, &jump);
  }
  CHECK(reports == 1);
}

/* Review finding: a camera that is not rigid with the body's heading (the
 * game's third-person orbit, a lagging root) must not report ordinary turns. */
static void orbit_turn_is_silent(void) {
  FpcMeter meter = {0};
  FpcJump jump;
  FpcMeterSample s = still();
  s.dt = 1.0 / 60;
  s.socket_valid = false;
  for (unsigned i = 0; i < 50; i++) {
    float yaw = 120 * DEG * (float)i / 60;
    s.eye[0] = sinf(yaw) * 3;
    s.eye[1] = -cosf(yaw) * 3;
    yaw_rotation(yaw, s.rotation);
    CHECK(!fpc_meter_step(&meter, &s, &jump));
  }
}

static void smooth_line(void) {
  char text[160];
  FpcSmoothLog log = {0};
  FpcJump jump = {.channels = FPC_JUMP_EYE | FPC_JUMP_TURN, .eye_m = 0.25f, .turn_deg = 46};
  /* smooth_line_format */
  CHECK(fpc_smooth_line(&log, 1234, &jump, false, true, text, sizeof text));
  CHECK(!strcmp(text, "Smooth: t=1234 ch=ET eye=0.250 turn=46.0 fov=0.0 near=0.000 socket=0.000 owned=0 rig=1 dropped=0"));
  jump.channels = FPC_JUMP_FOV | FPC_JUMP_NEAR | FPC_JUMP_SOCKET;
  CHECK(fpc_smooth_line(&log, 1300, &jump, true, false, text, sizeof text));
  CHECK(strstr(text, " ch=FNS ") && strstr(text, " owned=1 rig=0 "));
  FpcJump gap = {.channels = FPC_JUMP_GAP};
  CHECK(fpc_smooth_line(&log, 1400, &gap, false, false, text, sizeof text) && strstr(text, " ch=G eye=0.000 "));
  /* rate_limit: 20 lines in any one second, the rest counted on the next line written */
  log = (FpcSmoothLog){0};
  unsigned written = 0;
  for (unsigned i = 0; i < 100; i++)
    written += fpc_smooth_line(&log, 5000 + i * 9, &jump, true, true, text, sizeof text);
  CHECK(written == 20);
  CHECK(fpc_smooth_line(&log, 6000, &jump, true, true, text, sizeof text));
  CHECK(strstr(text, " dropped=80") && !strstr(text, " dropped=800"));
  CHECK(fpc_smooth_line(&log, 6001, &jump, true, true, text, sizeof text));
  CHECK(strstr(text, " dropped=0"));
  /* line_truncation */
  char small[24];
  memset(small, 'x', sizeof small);
  log = (FpcSmoothLog){0};
  CHECK(fpc_smooth_line(&log, 1, &jump, true, true, small, 16));
  CHECK(strlen(small) == 15 && small[16] == 'x');
}

int main(void) {
  unprimed_never_reports();
  steady_motion_is_silent();
  eye_step();
  one_jump_one_report();
  turn_step();
  fast_turn_is_not_a_jump();
  fov_step();
  near_step();
  socket_step();
  hitch_resets();
  respawn_resets();
  body_turn_is_silent();
  flick_reports_once();
  orbit_turn_is_silent();
  smooth_line();
  printf("Smoothness meter: %u checks; eye, turn, field of view, near clip and weapon steps, resets and fast turns passed\n", checks);
  return 0;
}
