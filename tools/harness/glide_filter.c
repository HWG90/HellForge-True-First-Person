/* The swap's eye glide (src/camera_glide.h) through the head camera's damping (src/camera_damp.h) and the continuity
 * filter (src/camera_continuity.h), in the order the camera helper applies them: retail's glide and the one under
 * the camera lock, side by side. Never a game.
 *
 *   cc -std=c11 -O1 -ffp-contract=off -I src -I tests tools/harness/glide_filter.c -lm -o work/glide_filter
 *   work/glide_filter              made-up swaps, one line each for the two glides
 *   work/glide_filter trace STYLE SIZE SPEED FRAME   one of them frame by frame (STYLE: stable or bodycam)
 *   work/glide_filter logged       the logged swaps of tests/reference/soak-19-glide.h with their glides exchanged
 * What was tried and not kept (src/camera_glide.h, GATE) is seen with -DTFP_EYE_GLIDE_MAX_MS=300.0f (the gate at
 * 0.378 m) and with -DGLIDE_UNGATED beside it (the gentle glide for every way).
 *
 * The made-up swap: the body root goes straight on at SPEED m/s (1.5: a walk, the head bobbing by 0.015 m at 1.8 Hz;
 * 5: a sprint, 0.035 m at 2.8 Hz; both made up), its axes not turning. The native view on an ideal rig (the eye
 * where the head camera's eye is, plus the outgoing weapon's difference) for 300 ms; then the head camera, which
 * holds that eye for two frames (the frame of the change and the one in which the game changes the weapon identity,
 * as in the soak of tests/reference/soak-19-glide.h); then the glide to the incoming weapon's eye, SIZE metres away,
 * for the 300 ms the swap leaves (the frames whose times come nearest: 38, 18, 9 and 6 at 8, 16.7, 33.3 and 50 ms a
 * frame); then the native view again, the rig at the incoming weapon's eye, for 1.5 s. The head camera's eye is
 * damped as the helper damps it (Stable: 350 ms and 0.25 m; Bodycam: the camera damping option at its default 60 %,
 * 180 ms and 0.10 m), after the glide and before the filter; jumps are looked for as smooth_detect says (600 ms from
 * a change of camera or weapon, and to the end of a run). Retail's own smoothing of the head is left out.
 *
 * For each glide, from the last frame the eye is held in:
 *   wanted   the most the wanted eye's step changes from one frame to the next while the head camera has the view
 *            (as a vector, in the root's frame: what the filter is given)
 *   jumps    frames the filter takes up as jumps (the two changes of camera are not counted)
 *   carried  the largest correction carried
 *   over     ms in all that more than 0.05 m is carried, and (until) ms from the glide's start to the last such frame
 *   shown    the most the presented eye's step changes from one frame to the next
 *   left     what the glide still had to go when the native view came back
 *
 * The logged swaps: the logged rows through the filter from nothing at row 0 (so what it carries in the first rows
 * is not what the log carried there), once as logged and once with retail's glide taken out of the wanted eye of the
 * head camera's rows and the lock's put in. The glide's way is taken from its first frame's step (retail's share of
 * it at that frame's time: 0.385 at 34 ms); the damping is not run again, and the native view comes back where the
 * log has it. */
#include "camera_glide.h"
#include "camera_damp.h"
#include "camera_continuity.h"
#include <stdio.h>
#include <stdlib.h>
#include "reference/soak-6-steps.h"
#include "reference/soak-19-glide.h"

#ifdef GLIDE_UNGATED /* the gentle glide for every way */
#define GLIDE tfp_eye_glide_step
#else
#define GLIDE tfp_eye_glide_lock_step
#endif
typedef struct Result { float wanted, carried, shown, left; double over, until; int jumps; } Result;
static float length(const float *v) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }

static Result swap(bool gentle, bool bodycam, float size, double speed, double frame, bool trace) {
  const TfpContinuityTuning tuning = TFP_CONTINUITY_DEFAULT; const float axes[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  const TfpCameraStyle style = tfp_camera_style(bodycam, tfp_camera_damping_ms(60));
  const double bob = speed > 3 ? .035 : .015, hertz = speed > 3 ? 2.8 : 1.8;
  const float outgoing[3] = {0, .05f, 0}, incoming[3] = {size*.36f, .05f+size*.80f, -size*.48f};
  const int glide_frames = (int)(300/frame+.5);
  TfpContinuity c; memset(&c, 0, sizeof c); TfpEyeDamp damp; tfp_eye_damp_reset(&damp); TfpEyeGlide glide; tfp_eye_glide_reset(&glide);
  Result r = {0, 0, 0, 0, 0, 0, 0};
  float delta[3] = {0, 0, 0}, native_eye[3] = {0, 0, 0}, last_wanted[3] = {0, 0, 0}, last_shown[3] = {0, 0, 0}, wanted_step[3] = {0, 0, 0}, shown_step[3] = {0, 0, 0};
  int head_frames = 0, native_again = 0; double watch = 0, begun = -1; bool was_head = false;
  for (int i = 0; native_again*frame < 1500; ++i) {
    const double ms = i*frame, tick = 10000+ms;
    const float root[3] = {0, (float)(speed*ms/1000), 0}, head[3] = {0, .10f, (float)(1.55+bob*sin(2*TFP_PI*hertz*ms/1000))};
    const float desired[3] = {head[0], head[1]+.10f, head[2]+.08f};
    const bool head_camera = ms >= 300 && head_frames < 2+glide_frames;
    float eye[3], world[3], out[3]; bool weapon_changed = false;
    if (head_camera) {
      if (!head_frames++) for (unsigned k = 0; k < 3; ++k) delta[k] = native_eye[k]-desired[k]; /* the eye of the native view, held */
      if (head_frames > 2) { /* a different weapon is coming up: the glide */
        weapon_changed = head_frames == 3;
        if (gentle) GLIDE(&glide, delta, incoming, (float)(frame/1000)); else tfp_eye_glide_retail(delta, incoming, (float)(frame/1000));
      } else begun = ms;
      for (unsigned k = 0; k < 3; ++k) eye[k] = desired[k]+delta[k];
      float damped[3];
      if (tfp_eye_damp_follow(&damp, head, tick, 250, style.eye_damp_ms, style.eye_lag_m, damped) > 0) for (unsigned k = 0; k < 3; ++k) eye[k] += damped[k]-head[k];
    } else {
      if (was_head) r.left = (float)sqrt((incoming[0]-delta[0])*(incoming[0]-delta[0])+(incoming[1]-delta[1])*(incoming[1]-delta[1])+(incoming[2]-delta[2])*(incoming[2]-delta[2]));
      if (head_frames) ++native_again;
      for (unsigned k = 0; k < 3; ++k) native_eye[k] = eye[k] = desired[k]+(head_frames ? incoming[k] : outgoing[k]);
      tfp_eye_damp_reset(&damp);
    }
    if (head_camera != was_head || weapon_changed) watch = tick+600; /* smooth_detect */
    const bool in_run = c.run && tick >= c.run_started && tick-c.run_started <= TFP_CONTINUITY_RUN_MS;
    for (unsigned k = 0; k < 3; ++k) world[k] = root[k]+eye[k];
    const uint64_t jumps = c.jumps, edges = c.edges;
    const float carried = tfp_continuity_step_in(&c, eye, world, axes, head_camera ? 1 : 2, tick < watch || in_run, false, tick, 250, &tuning, out);
    float step[3], shown[3], wanted_change[3], shown_change[3];
    for (unsigned k = 0; k < 3; ++k) { step[k] = eye[k]-last_wanted[k]; shown[k] = out[k]+root[k]-last_shown[k];
      wanted_change[k] = step[k]-wanted_step[k]; shown_change[k] = shown[k]-shown_step[k]; }
    if (begun >= 0 && ms > begun) {
      if (head_camera && length(wanted_change) > r.wanted) r.wanted = length(wanted_change);
      if (length(shown_change) > r.shown) r.shown = length(shown_change);
      if (carried > r.carried) r.carried = carried;
      if (carried > .05f) { r.over += frame; r.until = ms-begun; }
      r.jumps += (int)(c.jumps-jumps);
      if (trace) printf("%7.1f %s wanted step %.3f change %.3f | carried %.3f shown step %.3f change %.3f%s%s\n", ms-begun, head_camera ? "head  " : "native",
                        length(step), length(wanted_change), carried, length(shown), length(shown_change), c.jumps != jumps ? " jump" : "", c.edges != edges ? " edge" : "");
    }
    for (unsigned k = 0; k < 3; ++k) { last_wanted[k] = eye[k]; last_shown[k] = out[k]+root[k]; wanted_step[k] = step[k]; shown_step[k] = shown[k]; }
    was_head = head_camera;
  }
  return r;
}

/* Logged rows through the filter, as logged and with the glide exchanged; `held`: the row in which the weapon
 * identity changes with the eye still held, `last`: the head camera's last row. Prints every row from `held`-1 on. */
static void logged(const char *name, const SoakFrame *rows, int count, int held, int last) {
  const TfpContinuityTuning tuning = TFP_CONTINUITY_DEFAULT;
  float carried[2][64] = {{0}}, shown_change[2][64] = {{0}}, way_length = 0; int taken[2][64] = {{0}};
  for (int exchanged = 0; exchanged < 2; ++exchanged) {
    TfpContinuity c; memset(&c, 0, sizeof c); TfpEyeGlide glide; tfp_eye_glide_reset(&glide);
    float way[3] = {0, 0, 0}, old_delta[3] = {0, 0, 0}, new_delta[3] = {0, 0, 0}, last_eye[3] = {0, 0, 0};
    float now[3] = {0, 0, 0}, before[3] = {0, 0, 0}, earlier[3] = {0, 0, 0}; int frames = 0; /* the presented eye of this frame and the two before it */
    for (int row = 0; row < count && row < 64; ++row) {
      const SoakFrame *f = &rows[row]; float eye[3] = {0, 0, 0}, world[3], out[3];
      for (unsigned a = 0; a < 3; ++a) for (unsigned k = 0; k < 3; ++k) eye[a] += (f->wanted[k]-f->root[k])*f->axes[a*3+k];
      if (row == held+1) { /* the whole way from its first step */
        const float part = 1-expf(-(f->ms-rows[held].ms)/70);
        for (unsigned a = 0; a < 3; ++a) way[a] = (eye[a]-last_eye[a])/part;
        way_length = length(way);
      }
      if (row > held && row <= last) {
        const float dt = (f->ms-rows[row-1].ms)/1000;
        tfp_eye_glide_retail(old_delta, way, dt); GLIDE(&glide, new_delta, way, dt);
      }
      if (row > last) for (unsigned a = 0; a < 3; ++a) old_delta[a] = new_delta[a] = 0; /* the native view: the rig's own eye */
      for (unsigned a = 0; a < 3; ++a) last_eye[a] = eye[a];
      if (exchanged) for (unsigned a = 0; a < 3; ++a) eye[a] += new_delta[a]-old_delta[a];
      for (unsigned k = 0; k < 3; ++k) { world[k] = f->root[k]; for (unsigned a = 0; a < 3; ++a) world[k] += eye[a]*f->axes[a*3+k]; }
      const uint64_t jumps = c.jumps, edges = c.edges;
      carried[exchanged][row] = tfp_continuity_step_in(&c, eye, world, f->axes, (uint32_t)f->source, f->looked != 0, false, 5000+f->ms, 250, &tuning, out);
      taken[exchanged][row] = (c.jumps != jumps ? 1 : 0)|(c.edges != edges ? 2 : 0);
      /* Two rows of one frame (the game set the camera twice: under 5 ms apart) are one frame, the later what is drawn. */
      float shown[3], change[3];
      for (unsigned k = 0; k < 3; ++k) { shown[k] = f->root[k]; for (unsigned a = 0; a < 3; ++a) shown[k] += out[a]*f->axes[a*3+k]; }
      if (!row || f->ms-rows[row-1].ms >= 5) { ++frames; for (unsigned k = 0; k < 3; ++k) { earlier[k] = before[k]; before[k] = now[k]; } }
      for (unsigned k = 0; k < 3; ++k) { now[k] = shown[k]; change[k] = now[k]-before[k]-(before[k]-earlier[k]); }
      shown_change[exchanged][row] = frames > 2 ? length(change) : 0;
    }
  }
  printf("%s: a glide of %.3f m, %s\n", name, way_length, way_length > TFP_EYE_GLIDE_FAR_M ? "retail's under the lock too (GATE)" : "the gentle one under the lock");
  printf("row     ms camera | as logged: carried, shown change, taken | glide exchanged: carried, shown change, taken\n");
  float most[2] = {0, 0}, change[2] = {0, 0}, head_most[2] = {0, 0}; double over[2] = {0, 0}; int jumps[2] = {0, 0};
  for (int row = held-1; row < count && row < 64; ++row) {
    const char *names[4] = {"-", "jump", "edge", "jump+edge"};
    printf("%3d %6.1f %s | %.3f %.3f %-9s | %.3f %.3f %s\n", row, rows[row].ms, rows[row].source == 1 ? "head  " : "native",
           carried[0][row], shown_change[0][row], names[taken[0][row]], carried[1][row], shown_change[1][row], names[taken[1][row]]);
    for (int g = 0; g < 2; ++g) {
      if (carried[g][row] > most[g]) most[g] = carried[g][row];
      if (row <= last && carried[g][row] > head_most[g]) head_most[g] = carried[g][row];
      if (shown_change[g][row] > change[g]) change[g] = shown_change[g][row];
      if (carried[g][row] > .05f && row+1 < count) over[g] += rows[row+1].ms-rows[row].ms;
      if (row <= last) jumps[g] += taken[g][row] & 1;
    }
  }
  for (int g = 0; g < 2; ++g)
    printf("%s: %d jumps in the head camera and %.3f m carried at most there, %.3f m in all the rows; more than 0.05 m for %.0f ms; "
           "the presented step changed by %.3f m at most\n", g ? "glide exchanged" : "as logged", jumps[g], head_most[g], most[g], over[g], change[g]);
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "logged")) {
    logged("soak19_glide", soak19_glide, (int)(sizeof soak19_glide/sizeof *soak19_glide), 12, 21);
    logged("soak19_glide_sprint", soak19_glide_sprint, (int)(sizeof soak19_glide_sprint/sizeof *soak19_glide_sprint), 18, 27);
    logged("soak19_glide_standing", soak19_glide_standing, (int)(sizeof soak19_glide_standing/sizeof *soak19_glide_standing), 18, 35);
    return 0;
  }
  if (argc == 6 && !strcmp(argv[1], "trace")) {
    for (int gentle = 0; gentle < 2; ++gentle) {
      printf("%s\n", gentle ? "the glide under the lock" : "retail's glide");
      swap(gentle, !strcmp(argv[2], "bodycam"), (float)atof(argv[3]), atof(argv[4]), atof(argv[5]), true);
    }
    return 0;
  }
  const double frames[4] = {8, 16.7, 33.3, 50}, speeds[2] = {1.5, 5}; const float sizes[6] = {.07f, .15f, .25f, .26f, .37f, .47f};
  int less = 0, same_carried = 0, more = 0, shown_more = 0, cells = 0, jumps[2] = {0, 0}; float shown_most = 0, carried_most = 0;
  printf("style   size speed frame | retail: wanted jumps carried over (until) shown  left | lock: wanted jumps carried over (until) shown  left\n");
  for (int bodycam = 0; bodycam < 2; ++bodycam) for (unsigned s = 0; s < 6; ++s) for (unsigned v = 0; v < 2; ++v) for (unsigned f = 0; f < 4; ++f) {
    const Result a = swap(false, bodycam, sizes[s], speeds[v], frames[f], false), b = swap(true, bodycam, sizes[s], speeds[v], frames[f], false);
    printf("%-7s %.2f  %.1f  %4.1f | %.3f %2d %.3f %3.0f (%3.0f) %.3f %.3f | %.3f %2d %.3f %3.0f (%3.0f) %.3f %.3f\n", bodycam ? "Bodycam" : "Stable", sizes[s], speeds[v],
           frames[f], a.wanted, a.jumps, a.carried, a.over, a.until, a.shown, a.left, b.wanted, b.jumps, b.carried, b.over, b.until, b.shown, b.left);
    ++cells;
    jumps[0] += a.jumps; jumps[1] += b.jumps;
    if (b.carried < a.carried-.0005f) ++less; else if (b.carried > a.carried+.0005f) { ++more; if (b.carried-a.carried > carried_most) carried_most = b.carried-a.carried; }
    else ++same_carried;
    if (b.shown > a.shown+.0005f) { ++shown_more; if (b.shown-a.shown > shown_most) shown_most = b.shown-a.shown; }
  }
  printf("%d swaps: %d frames taken up as jumps with retail's glide, %d with the lock's; less is carried in %d, the same in %d, more in %d "
         "(by %.3f m more at most); the presented step changes by more in %d (by %.3f m more at most)\n",
         cells, jumps[0], jumps[1], less, same_carried, more, carried_most, shown_more, shown_most);
  return 0;
}
