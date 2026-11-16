#include "smooth_meter.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define METER_EYE_M 0.02f
#define METER_SOCKET_M 0.03f
#define METER_FOV_DEG 1.5f
#define METER_NEAR_M 0.02f
#define METER_TURN_DEG 12.0f
#define METER_MAX_DT 0.25
#define METER_DEGREES 57.29577951308232f

/* The eye relative to the body's root, in world axes. The heading is left in
 * on purpose: the view turns about the eye, not about the root, so removing
 * the root's heading made every ordinary turn look like eye travel. */
static void eye_in_body(const FpcMeterSample *s, float out[3]) {
  for (unsigned i = 0; i < 3; i++) out[i] = s->eye[i] - s->body[i];
}

/* The socket as the view sees it: relative to the eye, view rotation removed. */
static void socket_in_view(const FpcMeterSample *s, float out[3]) {
  const float *q = s->rotation;
  float v[3] = {s->socket[0] - s->eye[0], s->socket[1] - s->eye[1], s->socket[2] - s->eye[2]};
  float u[3] = {-q[0], -q[1], -q[2]}, w = q[3];
  float t[3] = {2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
  out[0] = v[0] + w * t[0] + u[1] * t[2] - u[2] * t[1];
  out[1] = v[1] + w * t[1] + u[2] * t[0] - u[0] * t[2];
  out[2] = v[2] + w * t[2] + u[0] * t[1] - u[1] * t[0];
}

static float turn_degrees(const float a[4], const float b[4]) {
  float dot = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
  return dot >= 1 ? 0 : 2 * acosf(dot) * METER_DEGREES;
}

/* Length of step - velocity*dt; leaves step/dt in velocity. The frame after
 * a report cannot tell a single step (the view now rests) from the start of a
 * motion (it carries on at the new speed), so there either one explains it. */
static float unexplained(const float now[3], const float before[3], float velocity[3], float dt, bool settling) {
  float moving = 0, resting = 0;
  for (unsigned i = 0; i < 3; i++) {
    float step = now[i] - before[i], rest = step - velocity[i] * dt;
    moving += rest * rest;
    resting += step * step;
    velocity[i] = step / dt;
  }
  return sqrtf(settling && resting < moving ? resting : moving);
}
static float unexplained_rate(float step, float *rate, float dt, bool settling) {
  float moving = fabsf(step - *rate * dt), resting = fabsf(step);
  *rate = step / dt;
  return settling && resting < moving ? resting : moving;
}

bool fpc_meter_step(FpcMeter *meter, const FpcMeterSample *sample, FpcJump *out) {
  memset(out, 0, sizeof *out);
  if (!meter->primed || !(sample->dt > 0) || sample->dt > METER_MAX_DT ||
      sample->player_generation != meter->last.player_generation) {
    bool primed = sample->dt == sample->dt; /* a NaN step primes nothing */
    memset(meter, 0, sizeof *meter);
    meter->primed = primed;
    meter->last = *sample;
    return false;
  }
  const FpcMeterSample *last = &meter->last;
  float dt = (float)sample->dt, now[3], before[3];
  bool measuring = meter->moving; /* the second sample only measures speed */

  eye_in_body(sample, now);
  eye_in_body(last, before);
  out->eye_m = unexplained(now, before, meter->eye_velocity, dt, meter->settling & FPC_JUMP_EYE);
  if (out->eye_m > METER_EYE_M) out->channels |= FPC_JUMP_EYE;

  float turn = turn_degrees(last->rotation, sample->rotation);
  out->turn_deg = unexplained_rate(turn, &meter->turn_rate, dt, meter->settling & FPC_JUMP_TURN);
  if (out->turn_deg > METER_TURN_DEG) out->channels |= FPC_JUMP_TURN;

  float fov = (sample->vertical_fov - last->vertical_fov) * METER_DEGREES;
  out->fov_deg = unexplained_rate(fov, &meter->fov_rate, dt, meter->settling & FPC_JUMP_FOV);
  if (out->fov_deg > METER_FOV_DEG) out->channels |= FPC_JUMP_FOV;

  out->near_m = fabsf(sample->near_clip - last->near_clip);
  if (out->near_m > METER_NEAR_M) out->channels |= FPC_JUMP_NEAR;

  if (sample->socket_valid && last->socket_valid) {
    socket_in_view(sample, now);
    socket_in_view(last, before);
    out->socket_m = unexplained(now, before, meter->socket_velocity, dt, meter->settling & FPC_JUMP_SOCKET);
    if (out->socket_m > METER_SOCKET_M) out->channels |= FPC_JUMP_SOCKET;
  } else memset(meter->socket_velocity, 0, sizeof meter->socket_velocity);

  if (!(out->eye_m == out->eye_m && out->turn_deg == out->turn_deg && out->fov_deg == out->fov_deg &&
        out->near_m == out->near_m && out->socket_m == out->socket_m)) {
    memset(meter, 0, sizeof *meter); /* unreadable input: start again, report nothing */
    memset(out, 0, sizeof *out);
    return false;
  }
  if (!measuring) memset(out, 0, sizeof *out); /* no speed known yet: nothing can be judged */
  meter->moving = true;
  meter->settling = out->channels;
  meter->last = *sample;
  return out->channels != 0;
}

bool fpc_smooth_line(FpcSmoothLog *log, uint64_t milliseconds, const FpcJump *jump,
                     bool owned, bool rig, char *text, size_t size) {
  uint64_t second = milliseconds / 1000;
  if (second != log->second) {
    log->second = second;
    log->written = 0;
  }
  if (log->written >= 20) {
    log->dropped++;
    return false;
  }
  char letters[7];
  unsigned count = 0;
  for (unsigned i = 0; i < 6; i++)
    if (jump->channels & (1u << i)) letters[count++] = "ETFNSG"[i];
  letters[count] = 0;
  if (text && size)
    snprintf(text, size, "Smooth: t=%llu ch=%s eye=%.3f turn=%.1f fov=%.1f near=%.3f socket=%.3f owned=%u rig=%u dropped=%u",
             (unsigned long long)milliseconds, letters, (double)jump->eye_m, (double)jump->turn_deg,
             (double)jump->fov_deg, (double)jump->near_m, (double)jump->socket_m, (unsigned)owned,
             (unsigned)rig, (unsigned)log->dropped);
  log->written++;
  log->dropped = 0;
  return true;
}
