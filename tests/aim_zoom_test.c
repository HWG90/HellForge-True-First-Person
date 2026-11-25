/* Round 20: full aim with 'zoom when aiming', an optic at several zoom levels and the unzoom gate flipping
 * (docs/design/specs/2026-11-23-gate-flip.md "Round 20"). A model of the game's side, frame by frame at 60, 116 and
 * 144 frames a second (frame times +-15 %), run through the mod's two rules of the frame: the field of view the lock
 * glides to (src/fov_lock.h) and the rig's depth (src/depth_steady.h). Each scene runs twice: as feel.51 has the
 * rules (the lock glides to the game's composed value; the held depth fades to nothing in full aim) and as they are
 * now. What is judged: the shown field of view, as a change of magnification from one frame to the next, and the
 * rig's depth along the view (the eye hangs on it), as a step from one frame to the next.
 *
 * The game's side, as read in the listing (game+0x1286a46..0x1286d09, game+0x173cb30, game+0x12873b3..0x1287423):
 *   target = third-person field of view / magnification of the zoom level   (gate clear)
 *          = third-person field of view                                    (gate set)
 *   the lens moves toward it by clamp(10 |target - lens|, 0.1, 2) rad/s * frame time, times the magnification while
 *   the gate is clear, and never past it
 *   D = (1-t) request+0x90 + t request+0x9c, t = min(1, |target - lens| / |target - request+0xa0|)
 * Made up: the magnifications (the optic data at +0x74/+0x78/+0x7c was never read; 1.5 is the Scythe's, read in the
 * logs as a 60 degree target under a 90 degree wide one), the distances (0.24 m and 0, round 9's model), the request
 * built at the mod's locked 80 degrees, and the composition of the setter's value from the lens (a constant factor in
 * the tangent: 60 degrees of lens gave 61.5 in the user's log).
 * Round 20c: "now" is the rules with the test switch zoom_steady.txt; without it the mod's rules are feel.51's (the
 * runs named so here). The line is the one "Aim:" line of src/aim_log.h: its frames half is checked here. */
#include "fov_lock.h"
#include "depth_steady.h"
#include "aim_log.h"
#include <string.h>
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static const float RAD = .01745329238474369f;

typedef struct Game { float wide, mag, lens, prior, current, previous, k; } Game;
static Game game(float mag) {
  const Game g = {90*RAD, mag, 90*RAD/mag, 80*RAD, .24f, 0, tanf(61.5f*RAD*.5f)/tanf(60*RAD*.5f)};
  return g;
}
/* One evaluation: the lens step of game+0x1286ca8 with game+0x173cb30. Returns the target it chose. */
static float game_step(Game *g, bool gated, float dt) {
  const float target = gated ? g->wide : g->wide/g->mag, way = target-g->lens;
  float rate = 10*fabsf(way);
  rate = rate < .1f ? .1f : rate > 2 ? 2 : rate;
  if (!gated) rate *= g->mag;
  const float step = (way < 0 ? -1.0f : 1.0f)*rate*dt;
  g->lens = fabsf(step) >= fabsf(way) ? target : g->lens+step;
  return target;
}
static float composed(const Game *g) { return 2*atanf(tanf(g->lens*.5f)*g->k); }
/* Review 24, IMPORTANT 2: the hooked call's value built from the lens of the frame before (1), or the pose helper's
 * note a frame late (2). */
static int lag;
/* Review 24, IMPORTANT 3: every so many gated frames the game scales the rig (TFP_STEADY_SCALED), a frame the hold
 * passes. 0: none. */
static int scaled_every;
/* Review 24, IMPORTANT 4: the hip's resting distance away from the game's own (the mod's reference for the optic) */
static float hip_offset;

typedef struct Run {
  float shown_most, shown_least;   /* degrees, after the first second (half a second in the shorter scenes) */
  float zoom_step;                 /* the largest change of magnification between two frames, in % */
  float eye_step;                  /* the largest step of the rig's depth between two frames, m */
  float game_step;                 /* the same for the game's own depth */
  float last_shown, last_eye;
  float aim_eye;                   /* the eye at 1.4 s (in full aim in the in-and-out scene) */
} Run;
typedef bool (*Pattern)(int frame, int second);
typedef float (*Weight)(double ms);
typedef float (*Level)(double ms);

/* One scene of `ms` milliseconds at `fps`. `now`: the rules as they are now, else as feel.51 has them. */
static Run scene(float fps, double ms, Pattern gate, Level level, Weight weight, bool now, uint32_t seed) {
  Game g = game(level(0)); g.lens = g.wide/g.mag;
  TfpFovLock lock; tfp_fov_lock_reset(&lock); TfpAimLens aim; tfp_aim_lens_reset(&aim); TfpSteady steady; memset(&steady, 0, sizeof steady);
  Run r; memset(&r, 0, sizeof r); r.shown_least = 1e9f;
  /* the field of view is judged from here: the hold comes in once the gate is seen to flip (a set stretch from a
   * lens at rest at once, else after the first short set stretch and TFP_AIM_EASE_IN_MS) */
  const double settle = ms > 2500 ? 1000 : 500;
  const float locked = 80*RAD; const int second = (int)fps;
  double tick = 10000, call = 0; float shown_before = 0, eye_before = 0, depth_before = 0; bool first = true;
  for (int i = 0; tick < 10000+ms; ++i) {
    seed = seed*1664525u+1013904223u;
    const double dt = 1000.0/fps*(0.85+0.3*((seed >> 8)/16777216.0));
    tick += dt;
    const float mag = level(tick-10000);
    if (mag != g.mag) g.mag = mag;   /* a zoom level changed: the request's distances as they were (only the target moves) */
    const bool gated = gate(i, second);
    const float lens_before = g.lens;
    const float target = game_step(&g, gated, (float)(dt*.001));
    /* the depth: the pose helper's frame of node 5 */
    TfpSteadyInput in; memset(&in, 0, sizeof in);
    in.target = target; in.lens = g.lens; in.prior = g.prior; in.current = g.current; in.previous = g.previous;
    in.bit = gated; in.blend = 1; in.mix_before = in.mix_after = 1; in.on = true;
    const float w = weight(tick-10000);
    if (now) tfp_steady_aim(&in, w, g.current+hip_offset); else { in.share = w; in.settled = g.current+hip_offset; }
    float delta = 0, depth = 0, held = 0;
    uint32_t reason;
    if (scaled_every && gated && i % scaled_every == 0) { /* the game scales the rig this frame: the hold passes it (the pose helper's steady_frame) */
      TfpSteadyInput scaled = in; scaled.blend = 1; scaled.mix_before = .5f; scaled.bit = 1;
      reason = tfp_steady_frame(&steady, &scaled, 1, tick, &delta, &depth, &held);
      CHECK(reason == TFP_STEADY_SCALED);
      if (now && !tfp_steady_carry(&steady, &scaled, reason, w < .5f, 1, tick, depth, &delta)) delta = 0;
    } else {
      reason = tfp_steady_frame(&steady, &in, 1, tick, &delta, &depth, &held);
      CHECK(reason == TFP_STEADY_WORKED);
    }
    const float eye = depth+delta;
    /* the field of view: the note from that frame, then the hooked call */
    static bool late_gated; static float late_target, late_lens;
    if (lag != 2) tfp_aim_lens_note(&aim, gated, target, g.lens, tick+.2);
    else { if (i) tfp_aim_lens_note(&aim, late_gated, late_target, late_lens, tick+.2); late_gated = gated; late_target = target; late_lens = g.lens; }
    Game was = g; was.lens = lens_before;
    const float applied = composed(lag == 1 ? &was : &g);
    bool clear = false;
    const float goal = now ? tfp_aim_lens_goal(&aim, applied, tick+.5, call, &clear) : applied;
    call = tick+.5;
    const float shown = tfp_fov_lock_step(&lock, locked, true, w < .5f, goal, tick+.5);
    if (!first) {
      const float change = 100*fabsf(tanf(shown_before*.5f)/tanf(shown*.5f)-1);
      if (tick-10000 > settle && change > r.zoom_step) r.zoom_step = change;
      /* the first half second is left out of both: a lease that begins inside an episode begins at the game's depth
       * of its first frame and eases home (round 9, "Left out: beginning again at the settled distance") */
      if (tick-10000 > 500 && fabsf(eye-eye_before) > r.eye_step) r.eye_step = fabsf(eye-eye_before);
      if (tick-10000 > 500 && fabsf(depth-depth_before) > r.game_step) r.game_step = fabsf(depth-depth_before);
    }
    if (tick-10000 > settle) {
      if (shown/RAD > r.shown_most) r.shown_most = shown/RAD;
      if (shown/RAD < r.shown_least) r.shown_least = shown/RAD;
    }
    if (tick-10000 <= 1400) r.aim_eye = eye;
    shown_before = shown; eye_before = eye; depth_before = depth; first = false;
  }
  r.last_shown = shown_before/RAD; r.last_eye = eye_before;
  return r;
}

static bool never(int i, int s) { (void)i; (void)s; return false; }
static bool flipping(int i, int s) { (void)s; return i & 1; }
static bool slow_cycle(int i, int s) { (void)s; return i%10 < 8; }
static bool bursts(int i, int s) { return (i % s) < s/4 && (i & 1); } /* a quarter second of every-frame flips, once a second */
/* Review 24, BLOCKING 1: the gate set for a long stretch, clear for one frame (a ledge, a rock right in front) */
static bool set600(int i, int s) { return i % (s*6/10+1) != 0; }
static bool set330(int i, int s) { return i % (s*33/100+1) != 0; }
static bool set260(int i, int s) { return i % (s*26/100+1) != 0; }
static float aimed(double ms) { (void)ms; return 0; }
static float lvl15(double ms) { (void)ms; return 1.5f; }
static float lvl3(double ms) { (void)ms; return 3; }
static float lvl6(double ms) { (void)ms; return 6; }
static float lvl12(double ms) { (void)ms; return 12; }
static float levels(double ms) { return ms < 800 ? 3 : ms < 1600 ? 6 : ms < 2400 ? 12 : 3; } /* 25, 50, 100, back to 25 */
/* Hip, aim in at 700 ms over 150 ms (the placement's blend, eased), aim out at 1500 ms the same way. */
static float in_out(double ms) {
  const double u = ms < 1100 ? (ms-700)/150 : 1-(ms-1500)/150;
  const double t = u < 0 ? 0 : u > 1 ? 1 : u;
  return (float)(1-t*t*(3-2*t));
}

int main(void) {
  const float FPS[3] = {60, 116, 144};
  static const Level LEVEL[4] = {lvl15, lvl3, lvl6, lvl12};
  static const char *const NAME[4] = {"x1.5 (the Scythe)", "x3   (made up: 25 m)", "x6   (made up: 50 m)", "x12  (made up: 100 m)"};
  printf("  full aim, zoom when aiming, the lock at 80 degrees; feel.51 -> now. zoom: the largest change of magnification between\n"
         "  two frames; eye: the largest step of the rig's depth between two frames (the eye hangs on it)\n");
  for (int f = 0; f < 3; ++f) for (int z = 0; z < 4; ++z) {
    const float fps = FPS[f];
    /* calm: the gate clear. Nothing changes, to the bit, against feel.51. */
    {
      const Run a = scene(fps, 2000, never, LEVEL[z], aimed, false, 7), b = scene(fps, 2000, never, LEVEL[z], aimed, true, 7);
      CHECK(a.zoom_step == b.zoom_step && a.shown_most == b.shown_most && a.shown_least == b.shown_least && a.eye_step == b.eye_step && b.eye_step == 0);
    }
    const Pattern P[3] = {flipping, slow_cycle, bursts}; const char *const PN[3] = {"every frame", "8 set / 2 clear", "bursts"};
    for (int p = 0; p < 3; ++p) {
      const Run a = scene(fps, 3000, P[p], LEVEL[z], aimed, false, 11), b = scene(fps, 3000, P[p], LEVEL[z], aimed, true, 11);
      const float rest = 90/LEVEL[z](0);
      printf("    %3.0f fps %s gate %-15s shown %6.2f..%6.2f -> %6.2f..%6.2f deg, zoom %6.2f%% -> %6.3f%%, eye %.4f -> %.6f m\n",
             (double)fps, NAME[z], PN[p], (double)a.shown_least, (double)a.shown_most, (double)b.shown_least, (double)b.shown_most,
             (double)a.zoom_step, (double)b.zoom_step, (double)a.eye_step, (double)b.eye_step);
      /* now: the shown field of view stays on the optic's own zoom, its magnification moves under 0.5 % a frame, and
       * the eye does not step */
      const float settled = 2*atanf(tanf(rest*RAD*.5f)*tanf(61.5f*RAD*.5f)/tanf(60*RAD*.5f))/RAD;
      CHECK(b.zoom_step < .5f && b.shown_least > settled-.2f && b.shown_most < settled+.2f);
      CHECK(b.eye_step < 1e-3f); /* under a millimetre a frame (what is left is the tail of a lease begun inside the episode) */
      /* the model reproduces the report on feel.51 for a flipping gate */
      if (p == 0) CHECK(a.eye_step > .05f && a.shown_most > settled+.2f);
    }
  }
  /* Review 24, BLOCKING 1: a long set stretch with a lone clear frame is the game's own unzoom, shown as feel.51 shows
   * it, at every frame rate and zoom level, whether the value lags or not (the branch before this snapped to full zoom for 250 ms on each lone clear frame: 59.6 % a frame at x12). */
  printf("  the gate set for long with one clear frame (600 / 330 / 260 ms): feel.51 -> now, the largest change of magnification\n");
  for (int f = 0; f < 3; ++f) for (int z = 0; z < 4; ++z) {
    const Pattern P[3] = {set600, set330, set260};
    for (int p = 0; p < 3; ++p) for (lag = 0; lag < 3; ++lag) {
      const Run a = scene(FPS[f], 3000, P[p], LEVEL[z], aimed, false, 13), b = scene(FPS[f], 3000, P[p], LEVEL[z], aimed, true, 13);
      if (lag == 0 && z == 3) printf("    %3.0f fps %s %s: %6.2f%% -> %6.2f%%, shown %.2f..%.2f -> %.2f..%.2f\n", (double)FPS[f], NAME[z], p == 0 ? "600" : p == 1 ? "330" : "260",
                                     (double)a.zoom_step, (double)b.zoom_step, (double)a.shown_least, (double)a.shown_most, (double)b.shown_least, (double)b.shown_most);
      /* the same as feel.51 (to under 0.01 %: what is left of a hold at the beginning of the scene, the lens at rest) */
      CHECK(fabsf(b.zoom_step-a.zoom_step) < .01f && fabsf(b.shown_least-a.shown_least) < .01f && fabsf(b.shown_most-a.shown_most) < .01f);
    }
    lag = 0;
  }
  /* Review 24, IMPORTANT 2: the value a frame behind its lens, or the note a frame late: still held, never worse than
   * feel.51 (the branch before this: 6.1 % against 3.3 % with the gate flipping every frame at x12, 60 fps). */
  printf("  the value a frame behind the lens (lag 1) or the note a frame late (lag 2): feel.51 -> now\n");
  for (int f = 0; f < 3; ++f) for (int z = 0; z < 4; ++z) for (lag = 1; lag < 3; ++lag) {
    const Pattern P[3] = {flipping, slow_cycle, bursts}; const char *const PN[3] = {"every frame", "8 set / 2 clear", "bursts"};
    for (int p = 0; p < 3; ++p) {
      const Run a = scene(FPS[f], 3000, P[p], LEVEL[z], aimed, false, 11), b = scene(FPS[f], 3000, P[p], LEVEL[z], aimed, true, 11);
      if (z == 3) printf("    %3.0f fps %s lag %d gate %-15s zoom %6.2f%% -> %6.3f%%\n", (double)FPS[f], NAME[z], lag, PN[p], (double)a.zoom_step, (double)b.zoom_step);
      /* bursts with the note a frame late: the first set frame of a burst cannot be told until the next note, so its
       * kick is feel.51's (4.6 % of the game's 25 % at x12, 60 fps); the rest of the burst is held */
      if (lag == 2 && p == 2) CHECK(b.zoom_step < a.zoom_step+.01f);
      else CHECK(b.zoom_step < .5f && b.zoom_step < a.zoom_step);
    }
  }
  lag = 0;
  /* Review 24, IMPORTANT 3: a frame the hold passes in full aim (the game scaling the rig every fifth gated frame) is
   * carried: the eye does not go back to the game's depth for it (the branch before this: 0.10 m steps). */
  printf("  the game scaling the rig every fifth gated frame in full aim: the largest step of the eye, feel.51 -> now\n");
  for (scaled_every = 5; scaled_every; scaled_every = 0) for (int f = 0; f < 3; ++f) for (int z = 0; z < 4; ++z) {
    const Pattern P[2] = {flipping, slow_cycle}; const char *const PN[2] = {"every frame", "8 set / 2 clear"};
    for (int p = 0; p < 2; ++p) {
      const Run a = scene(FPS[f], 3000, P[p], LEVEL[z], aimed, false, 11), b = scene(FPS[f], 3000, P[p], LEVEL[z], aimed, true, 11);
      if (z == 3) printf("    %3.0f fps %s gate %-15s eye %.4f -> %.6f m\n", (double)FPS[f], NAME[z], PN[p], (double)a.eye_step, (double)b.eye_step);
      CHECK(b.eye_step < 1e-3f);
    }
  }
  /* A zoom level changed in full aim (25 -> 50 -> 100 -> 25 m, gate clear): as feel.51 (the game's lens travel, the
   * lock's 80 ms glide behind it), so it glides: no frame takes a third of the smallest change (x2 in magnification). */
  printf("  zoom level changed in full aim (x3 -> x6 -> x12 -> x3), gate clear: feel.51 -> now\n");
  for (int f = 0; f < 3; ++f) {
    const float fps = FPS[f];
    const Run a = scene(fps, 3500, never, levels, aimed, false, 5), b = scene(fps, 3500, never, levels, aimed, true, 5);
    printf("    %3.0f fps zoom %6.2f%% -> %6.2f%% a frame at most (a pop would be 100%% or more), ends %.3f -> %.3f deg\n",
           (double)fps, (double)a.zoom_step, (double)b.zoom_step, (double)a.last_shown, (double)b.last_shown);
    CHECK(b.zoom_step == a.zoom_step && log(1+b.zoom_step/100) < log(2)/3 && b.zoom_step > 1);
    CHECK(fabsf(b.last_shown-a.last_shown) < 1e-3f);
  }
  /* Aim in and out, the gate clear and then flipping: no step of the eye at either hand-over. */
  printf("  aim in at 0.7 s and out at 1.5 s (150 ms blend), x6: the largest step of the eye, feel.51 -> now\n");
  for (int f = 0; f < 3; ++f) {
    const float fps = FPS[f];
    const Run a = scene(fps, 2500, never, lvl6, in_out, false, 3), b = scene(fps, 2500, never, lvl6, in_out, true, 3);
    const Run c = scene(fps, 2500, flipping, lvl6, in_out, false, 3), d = scene(fps, 2500, flipping, lvl6, in_out, true, 3);
    printf("    %3.0f fps gate clear %.6f -> %.6f m (the game's own %.4f), gate flipping %.4f -> %.6f m\n",
           (double)fps, (double)a.eye_step, (double)b.eye_step, (double)b.game_step, (double)c.eye_step, (double)d.eye_step);
    CHECK(b.eye_step < 1e-4f && d.eye_step < 1e-4f);
    /* the hip resting 9 cm nearer than the game's own distance: at the hip the eye rests there, in full aim at the
     * game's own, and the way between is a glide, never a step (under 2 cm a frame, as the held distance's ease) */
    hip_offset = -.09f;
    const Run e = scene(fps, 2500, never, lvl6, in_out, true, 3), h = scene(fps, 2500, flipping, lvl6, in_out, true, 3);
    hip_offset = 0;
    printf("    %3.0f fps the hip 9 cm nearer: eye step %.4f (flipping %.4f) m, in full aim %.4f, back at the hip %.4f m\n",
           (double)fps, (double)e.eye_step, (double)h.eye_step, (double)e.aim_eye, (double)e.last_eye);
    CHECK(e.eye_step < .02f && h.eye_step < .02f && e.eye_step > 1e-3f);
    CHECK(fabsf(e.aim_eye-.24f) < 1e-3f && fabsf(e.last_eye-(.24f-.09f)) < 1e-3f && fabsf(h.aim_eye-.24f) < 1e-3f);
  }
  /* The "Aim:" line, and the rig's depth counted for it. */
  {
    TfpSteadyLog log; memset(&log, 0, sizeof log);
    tfp_steady_aim_note(&log, true, true, .24f); tfp_steady_aim_note(&log, true, true, .0f); tfp_steady_aim_note(&log, true, true, .24f);
    tfp_steady_aim_note(&log, false, true, .5f); tfp_steady_aim_note(&log, true, true, .25f);  /* hip between: no step counted across it */
    TfpSteadySecond s; tfp_steady_take(&log, &s);
    CHECK(s.aim_frames == 4 && s.aim_depth.least == 0 && s.aim_depth.most == .25f && s.aim_step == .24f);
    { /* nor across a frame not worked out, nor across the hip: the next second's step is its own */
      TfpSteadyLog other; memset(&other, 0, sizeof other); TfpSteadySecond t;
      tfp_steady_aim_note(&other, true, true, .5f); tfp_steady_aim_note(&other, false, true, .1f); tfp_steady_aim_note(&other, true, true, .1f);
      tfp_steady_aim_note(&other, true, false, .9f); tfp_steady_aim_note(&other, true, true, .2f); tfp_steady_aim_note(&other, true, true, .25f);
      tfp_steady_take(&other, &t);
      CHECK(t.aim_frames == 4 && fabsf(t.aim_step-.05f) < 1e-6f);
    }
    TfpFovAimSecond f; memset(&f, 0, sizeof f);
    f.frames = 116; f.noted = 116; f.gated = 58; f.changes = 115; f.corrected = 115; f.third_person = 90;
    f.clear_least = f.clear_most = 7.5f*RAD; f.lens_least = 7.5f*RAD; f.lens_most = 8.6f*RAD;
    f.game_least = 7.73f*RAD; f.game_most = 8.86f*RAD; f.shown_least = f.shown_most = 7.73f*RAD; f.step_most = .00001f;
    f.factor_least = f.factor_most = 1.0272f; f.factored = 116;
    TfpAimLog al; tfp_aim_log_reset(&al); tfp_aim_log_add_rig(&al, &s);
    char line[1024]; tfp_aim_counts(&f, &al.rig, line, sizeof line);
    printf("  %s\n", line);
    CHECK(strstr(line, "116 frames: gate set 58 of 116 noted, changed 115 times; optic target 7.50-7.50 deg (x12.00-x12.00 of 90.0); lens 7.50-8.60; "
                       "game 7.73-8.86, shown 7.73-7.73 (corrected 115); step 0.001 %; game/lens x1.0272-x1.0272 (116); "
                       "rig depth 0.0000-0.2500 m (4), step 0.2400 m") == line);
    f.noted = 0; f.gated = 0; f.changes = 0; f.corrected = 0; f.clear_least = f.clear_most = 0; f.lens_least = f.lens_most = 0; memset(&s, 0, sizeof s);
    tfp_aim_log_reset(&al);
    tfp_aim_counts(&f, &al.rig, line, sizeof line);
    printf("  %s\n", line);
    f.factored = 0;
    tfp_aim_counts(&f, &al.rig, line, sizeof line);
    CHECK(strstr(line, "lens not noted (zoom level not known)") && strstr(line, "rig depth not worked out") && strstr(line, "game/lens not seen"));
  }
  printf("aim zoom: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
