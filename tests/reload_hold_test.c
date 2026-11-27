/* The eye held through a reload (src/reload_hold.h, round 39): the evaluator's code read for C, the share of C in the
 * rig's vector, made-up reloads through a model of the evaluator (game+0x1287a44..0x1287c10) at 8, 16.7, 33.3 and 50 ms
 * a frame, and a replay of the user's reload of 2026-11-25 (0.6.0-feel.56, tests/reference/reload/).
 *
 *   reload_hold_test [LOG]                 the tests; LOG: the reference window (the replay is skipped without it)
 *   reload_hold_test --rewrite IN OUT      the window's Trace lines with the eye as the hold puts it (for the harness's
 *                                          matrix judge: tests/test_harness_reload.py)
 *   reload_hold_test --edges IN OUT MODE   round 40: a window written by the round 39 hold (feel.58), its frames the game
 *                                          scaled by main row +0x11ac put where round 40 holds them. MODE "read": the
 *                                          scale read (the second's largest +0x11ac of its Gate: line); "none": not read
 *   reload_hold_test [LOG] [LOG58]         LOG58: the feel.58 window (tests/reference/reload/feel58-L4150-4266.log)
 *   reload_hold_test [LOG] [LOG58] [LOG60] round 49e: LOG60: the feel.60 window of a sidearm's reload the hold did not
 *                                          keep still (tests/reference/reload/feel60-L3545-3625.log): what the log has of
 *                                          it, and the edges looked at (the "Reload rest/start/end:" lines)
 *
 * TFP_GAME_IMAGE=path: the game.dll image (file offset = RVA) the pinned bytes and C's address are checked against too. */
#define _POSIX_C_SOURCE 200809L
#include "reload_hold.h"
#include "depth_steady.h"
#include <stdio.h>
#include <stdlib.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool near(double a, double b, double e) { return fabs(a-b) <= e; }
static const float DT[4] = {8, 16.7f, 33.3f, 50};

/* ---- the evaluator's code: the pattern with this build's displacements put in (game-pre1020.dll) ---- */
static void build_code(uint8_t *code, int32_t shift) {
  for (size_t i = 0; i < TFP_RELOAD_CODE_SIZE; ++i) {
    const char hi = TFP_RELOAD_PATTERN[2*i], lo = TFP_RELOAD_PATTERN[2*i+1];
    code[i] = hi == 'x' ? 0x5a : (uint8_t)(tfp_reload_nibble(hi) << 4 | tfp_reload_nibble(lo));
  }
  /* C at game+0x37cd668 in this build, as the nine instructions name it; `shift`: the data moved by that much */
  for (size_t i = 0; i < 9; ++i) {
    const TfpReloadSite *s = &TFP_RELOAD_SITES[i];
    const int32_t disp = (int32_t)(UINT32_C(0x37cd668)+4u*s->part-(s->disp+4u))+shift;
    memcpy(code+(s->disp-TFP_RELOAD_CODE_RVA), &disp, 4);
  }
}

/* ---- the evaluator, as the listing has it: the rig's forward value request+0x4c for v and C (forward parts) ---- */
typedef struct Eval { float w; uint8_t blend, ability, bit; float scale; } Eval; /* bit: the avatar's (and [rsp+0x45] clear); scale: main row +0x11ac */
/* One frame: the weight stepped by dt/75 ms where it is under 1 (game+0x1287aa4..0x1287aeb), then the vector. Returns
 * request+0x4c; `before` and `after` the weight as the frame found and left it. */
static float eval_frame(Eval *e, float v, float c, float dt, float *before, float *after) {
  *before = e->w;
  float y = v;
  if (e->blend) {
    if (e->w < 1) {
      e->w += dt/75.0f; if (e->w > 1) e->w = 1;
      y = e->ability ? (1-e->w)*v+e->w*c : e->w*v+(1-e->w)*c;
      if (e->bit) y *= e->scale; /* round 40: game+0x1287b76..0x1287b9b, before C is added the second time */
    } else if (e->ability) y = c;
  }
  *after = e->w;
  return y+c; /* C added a second time (0x1287bbf..0x1287beb) */
}
/* The byte changes: the game starts the weight again at 0 (as the log's 75 ms ramps say), or (`turn`) turns the blend
 * round from where it is: w' = 1-w, the same mix of v and C. */
static void eval_edge(Eval *e, uint8_t ability, bool turn) { e->blend = 1; e->ability = ability; e->w = turn ? 1-e->w : 0; }

typedef struct Run { TfpReloadHold h; float eye_last, step_most, eye_least, eye_most, delta_most; int ended; TfpReloadEnd end; bool started; uint32_t refused; float read_scale; int no_read; } Run;
/* read_scale: what the mod reads of main row +0x11ac where it is not the game's (0: the game's); no_read: not read */
/* One frame of the mod: the hold's delta added to the game's value (what the eye's offset from the root along the view
 * then is, with the root still). `share`: the hip share; `added`: the held depth's own term at rest. */
static float mod_frame(Run *r, Eval *e, float v, float c, float dt, float share, float added, uint64_t lease) {
  float before = 0, after = 0;
  const float game = eval_frame(e, v, c, dt, &before, &after);
  TfpReloadInput in; memset(&in, 0, sizeof in);
  in.blend = e->blend; in.ability = e->ability; in.bit = e->bit; in.mix_before = before; in.mix_after = after; in.argument = game; in.constant = c;
  float r_now = 0; /* the held depth's own term goes with its gain, 1-r (tfp_steady_gain): `added` is its value at rest */
  const uint32_t kind = tfp_reload_share(e->blend, e->ability, e->bit, 0, before, after, &r_now);
  if (kind == TFP_RELOAD_ADDED || kind == TFP_RELOAD_SCALED) added *= 1-r_now;
  in.constant_known = true; in.added = added; in.share = share; in.on = true;
  if (kind == TFP_RELOAD_SCALED) { in.scale_known = !r->no_read; in.scale = r->read_scale != 0 ? r->read_scale : e->scale; }
  float delta = 7; TfpReloadEnd end;
  const uint32_t why = tfp_reload_frame(&r->h, &in, lease, &delta, &end);
  if (why != TFP_RELOAD_NONE && why != TFP_RELOAD_ADDED) ++r->refused;
  if (end.ended) { ++r->ended; r->end = end; }
  /* a frame the game scales: the held depth passes it (its term is in `delta` where the hold took the frame) */
  const float eye = kind == TFP_RELOAD_SCALED ? game+(why == TFP_RELOAD_ADDED ? delta : 0) : game+added+delta;
  if (r->started) { const float step = fabsf(eye-r->eye_last); if (step > r->step_most) r->step_most = step; if (eye < r->eye_least) r->eye_least = eye; if (eye > r->eye_most) r->eye_most = eye; }
  else { r->eye_least = r->eye_most = eye; r->started = true; }
  if (fabsf(delta) > r->delta_most) r->delta_most = fabsf(delta);
  r->eye_last = eye;
  return eye;
}

/* ---- the replay: the Trace rows of the user's window ---- */
typedef struct Row { char line[2048]; double tick; double eye[3], wanted[3], root[3], view[3]; bool has_wanted; } Row;
static bool numbers(const char *line, const char *key, double out[3]) {
  const char *p = strstr(line, key);
  return p && sscanf(p+strlen(key), "%lf %lf %lf", &out[0], &out[1], &out[2]) == 3;
}
static int read_rows(const char *path, Row **out, char ***all, int *lines) {
  FILE *f = fopen(path, "r"); if (!f) return -1;
  int n = 0, cap = 256, lcap = 512; Row *rows = calloc((size_t)cap, sizeof *rows); char **text = calloc((size_t)lcap, sizeof *text); char buffer[2048];
  *lines = 0;
  while (fgets(buffer, sizeof buffer, f)) {
    if (*lines == lcap) { lcap *= 2; text = realloc(text, (size_t)lcap*sizeof *text); }
    text[(*lines)++] = strdup(buffer);
    if (!strstr(buffer, " Trace: ")) continue;
    if (n == cap) { cap *= 2; rows = realloc(rows, (size_t)cap*sizeof *rows); }
    Row *r = &rows[n]; memset(r, 0, sizeof *r); snprintf(r->line, sizeof r->line, "%s", buffer);
    const char *t = strstr(buffer, " tick=");
    if (!numbers(buffer, "eye_world=(", r->eye) || !numbers(buffer, "root=(", r->root) || !numbers(buffer, "view=(", r->view) || !t || sscanf(t+6, "%lf", &r->tick) != 1) continue;
    r->has_wanted = numbers(buffer, "wanted=(", r->wanted);
    ++n;
  }
  fclose(f); *out = rows; *all = text;
  return n;
}
/* The eye's offset from the body root along the level view (the view's horizontal part, made a unit). */
static double level_forward(const Row *r, const double eye[3]) {
  const double l = sqrt(r->view[0]*r->view[0]+r->view[1]*r->view[1]);
  return ((eye[0]-r->root[0])*r->view[0]+(eye[1]-r->root[1])*r->view[1])/l;
}
/* The replay. The log has the eye, the root and the view of every traced frame, not the request's bytes; they are
 * put back from the eye as the evaluator would have made it (the eye's offset from the root along the view is the
 * rig's forward value, as its 0.233 m at rest and its place on the root in the reload say):
 *   - request+0x8f set from the first frame of the slide back to the last before the slide forward; +0x8d set;
 *   - the weight: the share of the slide made, in each frame of the two slides (0 to 1), 1 between and after;
 *   - C's forward part: half the offset in the first frame after the slide back (the rig's value is then 2C);
 *   - request+0x4c: the offset in each frame.
 * Each row's eye then goes `delta` along the view (the hold's pair moves the rig's forward value, which the eye
 * follows). `delta_out`: per row. */
static int replay(Row *rows, int n, double *delta_out, double *c_out) {
  double F[512]; if (n > 512) return -1;
  for (int k = 0; k < n; ++k) F[k] = level_forward(&rows[k], rows[k].eye);
  int i0 = -1, i1 = -1, i2 = -1, i3 = -1;
  for (int k = 1; k < n && i0 < 0; ++k) if (F[k-1]-F[k] > .01) i0 = k;
  for (int k = i0+1; i0 > 0 && k < n && i1 < 0; ++k) if (F[k-1]-F[k] < .01) i1 = k;
  for (int k = i1+1; i1 > 0 && k < n && i2 < 0; ++k) if (F[k]-F[k-1] > .01) i2 = k;
  for (int k = i2+1; i2 > 0 && k < n && i3 < 0; ++k) if (F[k]-F[k-1] < .01) i3 = k;
  if (i3 < 0) return -1;
  const double pre = F[i0-1], low = F[i1], high = F[i3], end_low = F[i2-1], c = low/2;
  *c_out = c;
  TfpReloadHold h; memset(&h, 0, sizeof h); float mix = 1;
  for (int k = 0; k < n; ++k) {
    TfpReloadInput in; memset(&in, 0, sizeof in);
    in.blend = 1; in.constant = (float)c; in.constant_known = true; in.share = 1; in.on = true; in.argument = (float)F[k];
    float before = mix;
    if (k >= i0 && k < i1) { in.ability = 1; if (k == i0) before = 0; mix = (float)((pre-F[k])/(pre-F[i1-1])); }
    else if (k >= i1 && k < i2) { in.ability = 1; mix = 1; }
    else if (k >= i2 && k < i3) { in.ability = 0; if (k == i2) before = 0; mix = (float)(1-(high-F[k])/(high-end_low)); }
    else mix = 1;
    mix = mix < 0 ? 0 : mix > 1 ? 1 : mix;
    in.mix_before = before; in.mix_after = mix;
    float delta = 0; TfpReloadEnd end;
    tfp_reload_frame(&h, &in, 1, &delta, &end);
    delta_out[k] = delta;
  }
  return i0;
}
static void moved(const Row *r, double delta, double eye[3]) {
  const double l = sqrt(r->view[0]*r->view[0]+r->view[1]*r->view[1]+r->view[2]*r->view[2]);
  for (int i = 0; i < 3; ++i) eye[i] = r->eye[i]+delta*r->view[i]/l;
}
/* The Trace line with eye_world= and wanted= moved by `delta` along the view (the rest as it was). */
static void rewrite_line(const Row *r, double delta, FILE *out) {
  if (delta == 0) { fputs(r->line, out); return; }
  double eye[3], wanted[3]; moved(r, delta, eye);
  const double l = sqrt(r->view[0]*r->view[0]+r->view[1]*r->view[1]+r->view[2]*r->view[2]);
  for (int i = 0; i < 3; ++i) wanted[i] = r->wanted[i]+delta*r->view[i]/l;
  const char *e = strstr(r->line, "eye_world=("), *ee = strchr(e, ')'), *w = strstr(r->line, "wanted=("), *we = w ? strchr(w, ')') : NULL;
  fwrite(r->line, 1, (size_t)(e-r->line), out);
  fprintf(out, "eye_world=(%.5f %.5f %.5f", eye[0], eye[1], eye[2]);
  if (w && we && r->has_wanted) {
    fwrite(ee, 1, (size_t)(w-ee), out);
    fprintf(out, "wanted=(%.5f %.5f %.5f", wanted[0], wanted[1], wanted[2]);
    fputs(we, out);
  } else fputs(ee, out);
}

/* ---- round 40: the edges of a window the round 39 hold wrote (0.6.0-feel.58) ----
 * The log has the eye, the root and the view of every traced frame, the "Reload:" line (how far the eye would have
 * slid: the rig's own vector less C, V) and the "Gate:" lines (the least and largest of main row +0x11ac in their
 * second). The round 39 hold held every frame of the reload but the ones the game scaled, so the eye's offset F from
 * the root along the view (view_forward) sits at its rest value F0 except in those frames. Put back:
 *   - C's forward part 0 (the settled reload's argument is 0.0000 m in the Steady: lines, and it is 2C), the rig's
 *     own vector V; the argument is F - K with K = F0 - V (the eye hangs on the root: the offset is the argument plus
 *     the root's place);
 *   - the scaled frames: those off the rest by more than 1 cm, in two runs (the reload's start, +0x8f set, and its
 *     end, +0x8f clear); in each the game's s is taken as the largest +0x11ac of the Gate: line of its second, and
 *     the share r of C is solved from the frame's argument s((1-r) V + r C) + C; the weight before the first frame of
 *     a run 0, before the others the last frame's after; the avatar's bit set in them and [rsp+0x45] clear;
 *   - the frames between: the settled reload (weight 1, +0x8f set), argument 2C, which the round 39 hold took (their
 *     F is checked against what it puts back);
 *   - after the end's run: weight 1, +0x8f clear (the bit cleared there: the Gate: line has it change twice).
 * Each scaled frame's eye then goes `delta` along the view (the hold's pair) from where the log has it; every other
 * row is left as written. `mode_read`: the scale read (else not read: the eye's offset). Returns the number of scaled
 * frames, -1 where the window is not such a reload. `checked`: the largest the root's place K moved across an edge. */
static double gate_scale_most(char **text, int lines, const char *stamp) {
  /* the "Gate:" line of the second a frame stamped `stamp` (hh:mm:ss.mmm) is counted in: the first after it */
  double most = -1;
  for (int i = 0; i < lines; ++i) {
    if (strncmp(text[i], stamp, 12) < 0 || !strstr(text[i], " Gate: ")) continue;
    const char *p = strstr(text[i], "+0x11ac "); double a = 0, b = 0;
    if (p && sscanf(p+8, "%lf to %lf", &a, &b) == 2) most = b;
    break;
  }
  return most;
}
/* The eye's offset from the root along the view itself (not its level part): the hold's pair moves the eye along the
 * view, by its whole length; the harness looks down in this phase, so the level part is some 6 % shorter. */
static double view_forward(const Row *r, const double eye[3]) {
  const double l = sqrt(r->view[0]*r->view[0]+r->view[1]*r->view[1]+r->view[2]*r->view[2]);
  return ((eye[0]-r->root[0])*r->view[0]+(eye[1]-r->root[1])*r->view[1]+(eye[2]-r->root[2])*r->view[2])/l;
}
static int replay_edges(Row *rows, int n, char **text, int lines, bool mode_read, double *delta_out, double *checked, double *scales) {
  double F[512]; if (n > 512 || n < 8) return -1;
  for (int k = 0; k < n; ++k) F[k] = view_forward(&rows[k], rows[k].eye);
  double V = -1;
  for (int i = 0; i < lines; ++i) { const char *p = strstr(text[i], "Reload: eye held through the reload (would have slid "); if (p && sscanf(p+54, "%lf", &V) == 1) break; }
  if (!(V > .05)) return -1;
  const double F0 = F[0], C = 0;
  int run[2][2] = {{-1, -1}, {-1, -1}}, runs = 0;
  for (int k = 0; k < n && runs < 2; ++k) {
    const bool off = fabs(F[k]-F0) > .01 && fabs(F[k]-F0) > .5*V; /* a scaled frame: most of the way off the rest */
    if (off && run[runs][0] < 0) run[runs][0] = k;
    if (!off && run[runs][0] >= 0) { run[runs][1] = k; ++runs; }
  }
  if (runs < 2) return -1;
  for (int e = 0; e < 2; ++e) {
    char stamp[16]; snprintf(stamp, sizeof stamp, "%.12s", rows[run[e][0]].line);
    scales[e] = gate_scale_most(text, lines, stamp);
    if (!(scales[e] > TFP_RELOAD_SCALE_LEAST)) return -1;
    if (run[e][0] < 1) return -1;
  }
  /* The root's place along the view, K: F - V in every frame not scaled (at rest the argument is V; in the settled
   * reload it is 2C and the round 39 hold put V - C back). It moves as the view turns up or down (the root is the
   * body's, a metre and more below the eye), so in a scaled frame it is taken between its two neighbours, by the tick. */
  double K[512]; *checked = 0;
  for (int k = 0; k < n; ++k) K[k] = F[k]-V;
  for (int e = 0; e < 2; ++e) {
    const int a = run[e][0]-1, b = run[e][1];
    for (int k = run[e][0]; k < b; ++k) K[k] = K[a]+(K[b]-K[a])*(rows[k].tick-rows[a].tick)/(rows[b].tick-rows[a].tick);
    *checked = fmax(*checked, fabs(K[b]-K[a]));
  }
  TfpReloadHold h; memset(&h, 0, sizeof h); float mix = 1; uint8_t ability = 0; int scaled = 0;
  for (int k = 0; k < n; ++k) {
    TfpReloadInput in; memset(&in, 0, sizeof in);
    in.blend = 1; in.constant = (float)C; in.constant_known = true; in.share = 1; in.on = true;
    const int e = k >= run[0][0] && k < run[0][1] ? 0 : k >= run[1][0] && k < run[1][1] ? 1 : -1;
    float before = mix;
    if (e >= 0) {
      const double s = scales[e], a = F[k]-K[k], keep = (a-C)/s/(V-C); /* (1-r): the share of the rig's own vector */
      const double r = keep < 0 ? 1 : keep > 1 ? 0 : 1-keep;
      ability = e == 0; if (k == run[e][0]) before = 0;
      mix = (float)(e == 0 ? r : 1-r);
      if (mix < before) mix = before; /* the weight only goes up */
      in.bit = 1; in.argument = (float)a; in.scale = (float)s; in.scale_known = mode_read;
      ++scaled;
    } else if (k >= run[0][1] && k < run[1][0]) { ability = 1; before = mix = 1; in.argument = (float)(2*C); }
    else { ability = 0; before = mix = 1; in.argument = (float)V; }
    in.ability = ability; in.mix_before = before; in.mix_after = mix;
    float delta = 0; TfpReloadEnd end;
    const uint32_t why = tfp_reload_frame(&h, &in, 1, &delta, &end);
    delta_out[k] = e >= 0 && why == TFP_RELOAD_ADDED ? delta : 0; /* only the scaled frames move: the rest is as written */
    if (e >= 0 && why != TFP_RELOAD_ADDED) return -1;
  }
  return scaled;
}

/* ---- round 49e: the sidearm of 0.6.0-feel.60 (optic distance 0.2467 m, the rig's forward value -0.125 m at rest) ----
 * What the log has (tests/reference/reload/feel60-L3545-3625.log): the hold put back the whole 0.125 m in every frame
 * and the eye still went 0.064 m forward over the reload's first three frames, stayed there, and came back over its
 * last four, passing 0.0155 m behind where it rests. Something other than the share of C moved it. The model here is
 * the listing's: the eye hangs on the root at the rig eye node's local place (node 50, read at game+0x1287a41 into
 * the rig's own vector v = e + the optic's term), the root at minus v' along the view:
 *     the eye's place along the view = -(v' ) + e as drawn - what the hold put on node 5,   v' = request+0x4c - C
 * with a mover X of 0.0645 m over 100 ms as a reload begins and back as it ends, either in e (`in_eye`: then it is in
 * v as the evaluator reads it, a frame late where `late`) or in the anchor request+0x60 (not in v). Nothing is held by
 * any of it: it says what the "Reload start:" and "Reload end:" lines will show for each. */
#define PISTOL_V   (-.125f)
#define PISTOL_X   .0645f
#define PISTOL_E0  (-.3717f) /* node 50's local forward part at rest: the rig's value less the optic distance */
typedef struct Pistol { Eval e; TfpReloadHold h; TfpReloadWatch w; double t, edge_at; int direction; float x_shown, x_read; bool in_eye, late, watched;
                        int reports; TfpReloadEdges report; float place, place_least, place_most; bool placed; uint32_t delta_bits_sum; } Pistol;
static float pistol_mover(const Pistol *p, double t) {
  if (!p->direction) return 0;
  double u = (t-p->edge_at)/100.0; u = u < 0 ? 0 : u > 1 ? 1 : u;
  return (float)(p->direction > 0 ? u : 1-u)*PISTOL_X;
}
/* One frame; returns the eye's place along the view less its place at rest (N in the notes). */
static float pistol_frame(Pistol *p, float dt, float share, uint64_t lease) {
  p->t += dt;
  const float shown = pistol_mover(p, p->t);
  p->x_read = p->late ? p->x_shown : shown; /* what the evaluator reads of it: last frame's where the animation is written after it */
  p->x_shown = shown;
  float before = 0, after = 0;
  const float v = PISTOL_V+(p->in_eye ? p->x_read : 0);
  const float game = eval_frame(&p->e, v, 0, dt, &before, &after);
  TfpReloadInput in; memset(&in, 0, sizeof in);
  in.blend = p->e.blend; in.ability = p->e.ability; in.mix_before = before; in.mix_after = after; in.argument = game; in.constant = 0;
  in.constant_known = true; in.share = share; in.on = true;
  float put = 7; TfpReloadEnd end;
  const uint32_t why = tfp_reload_frame(&p->h, &in, lease, &put, &end);
  uint32_t bits; memcpy(&bits, &put, 4); p->delta_bits_sum = p->delta_bits_sum*31u+bits;
  if (p->watched) {
    TfpReloadProbe probe; tfp_reload_probe(&probe, &in, why, put, (uint64_t)(p->t+.5));
    probe.eye[0] = .01f; probe.eye[1] = PISTOL_E0+(p->in_eye ? p->x_read : 0); probe.eye[2] = .02f; probe.eye_known = true;
    probe.anchor[0] = 0; probe.anchor[1] = p->in_eye ? 0 : p->x_read; probe.anchor[2] = 0; probe.anchor_known = true;
    TfpReloadEdges out;
    if (tfp_reload_watch_frame(&p->w, &p->h, &probe, lease, &out)) { ++p->reports; p->report = out; }
  }
  const float place = -(game-0)+shown-put-(-PISTOL_V); /* less the rest: -v at rest */
  if (!p->placed) { p->place_least = p->place_most = place; p->placed = true; }
  if (place < p->place_least) p->place_least = place;
  if (place > p->place_most) p->place_most = place;
  p->place = place;
  return place;
}
static void pistol_begin(Pistol *p, bool in_eye, bool late, bool watched) {
  memset(p, 0, sizeof *p); p->e = (Eval){1, 1, 0, 0, 1}; p->in_eye = in_eye; p->late = late; p->watched = watched;
}
static void pistol_edge(Pistol *p, uint8_t ability) { eval_edge(&p->e, ability, false); p->direction = ability ? 1 : -1; p->edge_at = p->t; }
/* The eye's place along the view in a traced row, less the first row's, and its part across the view (up, in the view's
 * vertical plane). */
static void along_view(const Row *rows, int k, double *along, double *across) {
  const double *v = rows[k].view; const double h = sqrt(v[0]*v[0]+v[1]*v[1]);
  double d[3]; for (int i = 0; i < 3; ++i) d[i] = rows[k].eye[i]-rows[0].eye[i];
  *along = d[0]*v[0]+d[1]*v[1]+d[2]*v[2];
  *across = -d[0]*v[0]*v[2]/h-d[1]*v[1]*v[2]/h+d[2]*h;
}

int main(int argc, char **argv) {
  if (argc == 5 && !strcmp(argv[1], "--edges")) {
    Row *rows = NULL; char **text = NULL; int lines = 0; const int n = read_rows(argv[2], &rows, &text, &lines);
    double delta[512], checked = 0, scales[2] = {0, 0};
    if (n <= 0 || replay_edges(rows, n, text, lines, !strcmp(argv[4], "read"), delta, &checked, scales) <= 0) { fprintf(stderr, "no scaled reload edges in %s\n", argv[2]); return 2; }
    FILE *out = fopen(argv[3], "w"); if (!out) return 2;
    int k = 0;
    for (int i = 0; i < lines; ++i) {
      if (k < n && !strcmp(text[i], rows[k].line)) { rewrite_line(&rows[k], delta[k], out); ++k; }
      else fputs(text[i], out);
    }
    fclose(out);
    return k == n ? 0 : 3;
  }
  if (argc == 4 && !strcmp(argv[1], "--rewrite")) {
    Row *rows = NULL; char **text = NULL; int lines = 0; const int n = read_rows(argv[2], &rows, &text, &lines);
    double delta[512], c = 0;
    if (n <= 0 || replay(rows, n, delta, &c) < 0) { fprintf(stderr, "no reload in %s\n", argv[2]); return 2; }
    FILE *out = fopen(argv[3], "w"); if (!out) return 2;
    int k = 0;
    for (int i = 0; i < lines; ++i) {
      if (k < n && !strcmp(text[i], rows[k].line)) { rewrite_line(&rows[k], delta[k], out); ++k; }
      else fputs(text[i], out);
    }
    fclose(out);
    return k == n ? 0 : 3;
  }

  /* ---- C read from the evaluator's code ---- */
  {
    uint8_t code[TFP_RELOAD_CODE_SIZE]; uint32_t rva = 0;
    build_code(code, 0);
    CHECK(tfp_reload_code(code, &rva) && rva == UINT32_C(0x37cd668));
    build_code(code, 0x1000); /* the data moved, every instruction following it (as HellForge 0.5 moves what it rewrites) */
    CHECK(tfp_reload_code(code, &rva) && rva == UINT32_C(0x37ce668));
    build_code(code, 0); code[0] ^= 1; CHECK(!tfp_reload_code(code, &rva));         /* not the pinned code: refused */
    build_code(code, 0); code[0x1287ba2-TFP_RELOAD_CODE_RVA+12] = 0x90; CHECK(!tfp_reload_code(code, &rva)); /* the settled reload's movaps */
    build_code(code, 0); code[TFP_RELOAD_CODE_SIZE-1] ^= 0x10; CHECK(!tfp_reload_code(code, &rva));  /* the store of +0x50 */
    for (int s = 0; s < 9; ++s) { /* one instruction naming another place: refused */
      build_code(code, 0); int32_t d; memcpy(&d, code+(TFP_RELOAD_SITES[s].disp-TFP_RELOAD_CODE_RVA), 4); d += 4;
      memcpy(code+(TFP_RELOAD_SITES[s].disp-TFP_RELOAD_CODE_RVA), &d, 4);
      CHECK(!tfp_reload_code(code, &rva));
    }
    build_code(code, 2); CHECK(!tfp_reload_code(code, &rva));                      /* not aligned */
    build_code(code, 0); code[0x1287ac0-TFP_RELOAD_CODE_RVA] ^= 0xff; CHECK(tfp_reload_code(code, &rva)); /* a displacement that is not C's: not compared */
    CHECK(!tfp_reload_code(NULL, &rva));
    const float good[3] = {0, -.01f, .02f}, bad[3] = {0, NAN, 0}, far[3] = {0, 5, 0};
    CHECK(tfp_reload_constant_ok(good) && !tfp_reload_constant_ok(bad) && !tfp_reload_constant_ok(far));
    const char *image = getenv("TFP_GAME_IMAGE");
    FILE *f = image ? fopen(image, "rb") : NULL;
    if (f) {
      uint8_t real[TFP_RELOAD_CODE_SIZE]; float c[3] = {9, 9, 9};
      CHECK(fseek(f, TFP_RELOAD_CODE_RVA, SEEK_SET) == 0 && fread(real, 1, sizeof real, f) == sizeof real);
      CHECK(tfp_reload_code(real, &rva) && rva == UINT32_C(0x37cd668));
      CHECK(fseek(f, (long)rva, SEEK_SET) == 0 && fread(c, 4, 3, f) == 3 && tfp_reload_constant_ok(c));
      printf("  the image: the evaluator as pinned, C at game+0x%x, (%g, %g, %g) in the file (the game's data section; the game's value is read in the game)\n",
             rva, (double)c[0], (double)c[1], (double)c[2]);
      fclose(f);
    } else printf("  the image: not given (TFP_GAME_IMAGE); the pattern alone\n");
  }
  /* ---- the share of C: the evaluator's, and 1 less the held depth's gain ---- */
  {
    float r = -1;
    CHECK(tfp_reload_share(0, 1, 0, 0, 1, 1, &r) == TFP_RELOAD_NONE && r == 0);    /* request+0x8d clear: the game does nothing */
    CHECK(tfp_reload_share(1, 0, 0, 0, 1, 1, &r) == TFP_RELOAD_NONE && r == 0);    /* settled, +0x8f clear: the rig's own */
    CHECK(tfp_reload_share(1, 1, 0, 0, 1, 1, &r) == TFP_RELOAD_ADDED && r == 1);   /* settled in a reload: C alone */
    CHECK(tfp_reload_share(1, 1, 0, 0, .25f, .5f, &r) == TFP_RELOAD_ADDED && r == .5f);
    CHECK(tfp_reload_share(1, 0, 0, 0, .25f, .5f, &r) == TFP_RELOAD_ADDED && r == .5f);
    CHECK(tfp_reload_share(1, 0, 0, 0, .75f, 1, &r) == TFP_RELOAD_NONE && r == 0); /* the blend back at its end */
    CHECK(tfp_reload_share(1, 1, 0, 0, 0, 0, &r) == TFP_RELOAD_NONE && r == 0);    /* the first frame of a reload, nothing of C yet */
    CHECK(tfp_reload_share(1, 1, 1, 0, .25f, .5f, &r) == TFP_RELOAD_SCALED && r == .5f); /* the avatar's bit in the blend: scaled by main row +0x11ac (r still given) */
    CHECK(tfp_reload_share(1, 0, 1, 0, .25f, 1, &r) == TFP_RELOAD_SCALED && r == 0);  /* the blend back's last frame: scaled, with nothing of C */
    CHECK(tfp_reload_share(1, 1, 1, 1, .25f, .5f, &r) == TFP_RELOAD_ADDED && r == .5f); /* [rsp+0x45] set: the evaluator skips the scaling */
    CHECK(tfp_reload_share(1, 1, 1, 0, 1, 1, &r) == TFP_RELOAD_ADDED && r == 1);   /* settled: no scaling there */
    CHECK(tfp_reload_share(1, 1, 0, 0, NAN, .5f, &r) == TFP_RELOAD_RANGE && tfp_reload_share(1, 1, 0, 0, .5f, 1.5f, &r) == TFP_RELOAD_RANGE);
    srand(39);
    for (int i = 0; i < 20000; ++i) {
      TfpSteadyInput in; memset(&in, 0, sizeof in);
      in.blend = (uint8_t)(rand() & 1); in.ability = (uint8_t)(rand() & 1); in.bit = (uint8_t)(rand()%4 == 0); in.unscaled = (uint8_t)(rand()%3 == 0);
      in.mix_before = rand()%3 == 0 ? 1 : (float)rand()/(float)RAND_MAX; in.mix_after = rand()%3 == 0 ? 1 : (float)rand()/(float)RAND_MAX;
      float gain = -1; const bool ok = tfp_steady_gain(&in, &gain); const uint32_t why = tfp_reload_share(in.blend, in.ability, in.bit, in.unscaled, in.mix_before, in.mix_after, &r);
      CHECK(ok == (why != TFP_RELOAD_SCALED && why != TFP_RELOAD_RANGE));
      if (ok) CHECK(near(r, 1-gain, 1e-7));
      if (why == TFP_RELOAD_SCALED) { /* the share is the one the frame would have unscaled */
        TfpSteadyInput u = in; u.unscaled = 1; float g = -1; CHECK(tfp_steady_gain(&u, &g) && near(r, 1-g, 1e-7));
      }
    }
  }
  /* ---- made-up reloads through the evaluator ---- */
  printf("  made up, at 8 / 16.7 / 33.3 / 50 ms a frame: the eye's largest step along the view, and its place (m)\n");
  for (int k = 0; k < 4; ++k) {
    const float dt = DT[k]; const int second = (int)(1000.0f/dt);
    /* 1. The Liberator: v 0.233 m, C 0; and a weapon whose D is 0.08 m more (0.313), with a C that is not 0. Calm,
     * the reload begins, three seconds of it, it ends, calm. The eye stays where it was, to float rounding. Without
     * the hold (share 0, as full aim) the game's own slide, the whole of v - C. */
    const float V[2] = {.233f, .313f}, C[2] = {0, -.0107f};
    for (int wpn = 0; wpn < 2; ++wpn) for (int hip = 1; hip >= 0; --hip) {
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0, 0, 1};
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, V[wpn], C[wpn], dt, (float)hip, 0, 1);
      const float rest = r.eye_last;
      CHECK(r.delta_most == 0 && r.ended == 0);
      eval_edge(&e, 1, false);
      for (int i = 0; i < 3*second; ++i) mod_frame(&r, &e, V[wpn], C[wpn], dt, (float)hip, 0, 1);
      eval_edge(&e, 0, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, V[wpn], C[wpn], dt, (float)hip, 0, 1);
      printf("    %4.1f ms  v %.3f, C %.4f, %s: step %.6f, place %.6f to %.6f (rest %.6f)\n", (double)dt, (double)V[wpn], (double)C[wpn], hip ? "hip" : "full aim",
             (double)r.step_most, (double)r.eye_least, (double)r.eye_most, (double)rest);
      if (hip) {
        CHECK(r.step_most < 1e-6f && near(r.eye_least, rest, 1e-6) && near(r.eye_most, rest, 1e-6) && r.ended == 1 && r.refused == 0);
        CHECK(near(r.end.would, V[wpn]-C[wpn], 1e-6) && near(r.end.most, V[wpn]-C[wpn], 1e-6) && r.end.missed == 0 && r.end.frames > 0);
      } else {
        CHECK(near(r.eye_most-r.eye_least, V[wpn]-C[wpn], 1e-5) && r.delta_most == 0); /* full aim: the game's own reload, untouched */
        CHECK(r.ended == 1 && near(r.end.would, V[wpn]-C[wpn], 1e-6) && r.end.most == 0);
      }
    }
    /* 2. Cancelled half way: +0x8f clears with the weight at about 0.5, the game starting its weight again (a step of its
     * own: 0.5 (v-C) in one frame) or turning the blend round. Held, either way: no step. */
    for (int turn = 0; turn < 2; ++turn) {
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0, 0, 1};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 1, false);
      while (e.w < .45f) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 0, turn);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      CHECK(r.step_most < 1e-6f && near(r.eye_least, .233, 1e-6) && near(r.eye_most, .233, 1e-6) && r.ended == 1 && r.end.missed == 0);
    }
    /* 3. A weapon swapped in the reload: the rig's vector is the new weapon's (0.31) under C from then on. The eye
     * stays at the old one's (0.233) to the end of the reload and goes to the new one's over the blend back: never
     * further than a frame's share of the difference in a frame, never past either. */
    {
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0, 0, 1};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 1, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      float least = 1, most = 0;
      for (int i = 0; i < second; ++i) { const float eye = mod_frame(&r, &e, .31f, 0, dt, 1, 0, 1); if (eye < least) least = eye; if (eye > most) most = eye; }
      CHECK(near(least, .233, 1e-6) && near(most, .233, 1e-6));
      eval_edge(&e, 0, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .31f, 0, dt, 1, 0, 1);
      const float share = dt/75.0f > 1 ? 1 : dt/75.0f;
      CHECK(near(r.eye_last, .31, 1e-6) && r.eye_least >= .233f-1e-6f && r.eye_most <= .31f+1e-6f && r.step_most <= (.31f-.233f)*share+1e-6f);
    }
    /* 4. The hip share eased in the reload (an aim begun in it): the hold eased with it, the eye between the two */
    {
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0, 0, 1};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 1, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      for (int i = 0; i <= second/4; ++i) { const float share = 1-(float)i/(float)(second/4); const float eye = mod_frame(&r, &e, .233f, 0, dt, share, 0, 1); CHECK(near(eye, .233*share, 1e-6)); }
    }
    /* 5. The held depth adds 0.02 m to node 5 at rest: held with it, so the eye stays where the two put it */
    {
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0, 0, 1};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, .02f, 1);
      eval_edge(&e, 1, false);
      for (int i = 0; i < 2*second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, .02f, 1);
      eval_edge(&e, 0, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, .02f, 1);
      CHECK(r.step_most < 1e-6f && near(r.eye_least, .253, 1e-6) && near(r.eye_most, .253, 1e-6));
    }
  }
  /* ---- what is not held ---- */
  {
    TfpReloadHold h; memset(&h, 0, sizeof h); TfpReloadInput in; memset(&in, 0, sizeof in); float delta = 7; TfpReloadEnd end;
    in.blend = 1; in.ability = 1; in.mix_before = in.mix_after = 1; in.argument = 0; in.constant_known = true; in.share = 1; in.on = true;
    CHECK(tfp_reload_frame(&h, &in, 1, &delta, &end) == TFP_RELOAD_NO_HELD && delta == 0); /* a lease begun in a reload: nothing to hold */
    in.ability = 0; in.argument = .233f;
    CHECK(tfp_reload_frame(&h, &in, 1, &delta, &end) == TFP_RELOAD_NONE && delta == 0 && end.ended && end.missed == 1 && end.frames == 1 && end.why == TFP_RELOAD_NO_HELD);
    char line[200]; tfp_reload_line(&end, line, sizeof line);
    CHECK(!strcmp(line, "Reload: eye not held through the reload (1 frames): the reload began before a frame of the view without it"));
    in.ability = 1;
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_NO_HELD && delta == 0); /* a new lease forgets what the last one held */
    in.ability = 0; tfp_reload_frame(&h, &in, 2, &delta, &end);
    in.ability = 1; in.constant_known = false;
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_NO_CODE && delta == 0);
    in.constant_known = true; in.on = false;
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_OFF && delta == 0);
    in.on = true; in.mix_before = .5f; in.bit = 1; in.argument = NAN; /* round 40: a scaled frame is held unless nothing can be worked out */
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_SCALED && delta == 0);
    in.argument = .233f; in.bit = 0; in.mix_before = in.mix_after = 1;
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_ADDED && near(delta, .233, 1e-7)); /* held where a frame of it comes */
    in.ability = 0; CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_NONE && end.ended && end.missed == 3 && end.frames == 4);
    tfp_reload_line(&end, line, sizeof line);
    CHECK(!strcmp(line, "Reload: eye held through the reload (would have slid 0.233 m), but not in 3 of its 4 frames: the game scaled the rig by main row +0x11ac"));
    in.ability = 1; tfp_reload_frame(&h, &in, 2, &delta, &end); in.ability = 0; tfp_reload_frame(&h, &in, 2, &delta, &end);
    tfp_reload_line(&end, line, sizeof line);
    CHECK(!strcmp(line, "Reload: eye held through the reload (would have slid 0.233 m)"));
    /* the switch off at rest forgets the vector: it is never put back from before the switch */
    in.ability = 0; in.on = false; tfp_reload_frame(&h, &in, 2, &delta, &end); in.on = true; in.ability = 1;
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_NO_HELD);
    /* the line's second: four kept, all counted */
    TfpReloadSecond s; memset(&s, 0, sizeof s); TfpReloadEnd e; memset(&e, 0, sizeof e);
    tfp_reload_note(&s, &e); CHECK(s.count == 0);
    e.ended = true; for (int i = 0; i < 6; ++i) { e.frames = (uint32_t)i; tfp_reload_note(&s, &e); }
    CHECK(s.count == 6 && s.ends[3].frames == 3);
  }
  /* ---- frames with nothing of C: nothing added, to the bit, whatever the vector does ---- */
  {
    TfpReloadHold h; memset(&h, 0, sizeof h); srand(7);
    for (int i = 0; i < 5000; ++i) {
      TfpReloadInput in; memset(&in, 0, sizeof in); float delta = 7; TfpReloadEnd end;
      in.blend = (uint8_t)(rand() & 1); in.ability = 0; in.mix_before = in.mix_after = 1; in.argument = (float)rand()/(float)RAND_MAX;
      in.constant_known = true; in.share = 1; in.on = true; in.added = .01f;
      CHECK(tfp_reload_frame(&h, &in, 3, &delta, &end) == TFP_RELOAD_NONE && delta == 0 && !end.ended);
    }
  }
  /* ---- round 40: the edges the game scales by main row +0x11ac. The evaluator with the avatar's bit set through the
   * reload: in every frame with the weight under 1 the rig's vector is s v' + C. s at several values, read as the
   * game has it or not read; the hip and full aim; the Liberator and a longer vector with a C that is not zero. ---- */
  printf("  round 40, made up: edges scaled by main row +0x11ac (the eye's place at the hip, least to most, against its rest)\n");
  {
    static const float SCALES[8] = {0, .0034f, .02f, .1f, .2895f, .5f, 1, 1.25f};
    for (int k = 0; k < 4; ++k) for (int si = 0; si < 8; ++si) for (int read = 1; read >= 0; --read) {
      const float dt = DT[k], scale = SCALES[si]; const int second = (int)(1000.0f/dt);
      const float V[2] = {.233f, .313f}, C[2] = {0, -.0107f};
      int edge_frames = 0; { float w = 0; while (w < 1) { w += dt/75.0f; ++edge_frames; } } /* frames of one edge with the weight under 1 at their start */
      for (int wpn = 0; wpn < 2; ++wpn) for (int hip = 1; hip >= 0; --hip) {
        Run r; memset(&r, 0, sizeof r); r.no_read = !read; Eval e = {1, 1, 0, 0, scale};
        for (int i = 0; i < second; ++i) mod_frame(&r, &e, V[wpn], C[wpn], dt, (float)hip, 0, 1);
        const float rest = r.eye_last;
        eval_edge(&e, 1, false); e.bit = 1;
        for (int i = 0; i < 3*second; ++i) mod_frame(&r, &e, V[wpn], C[wpn], dt, (float)hip, 0, 1);
        eval_edge(&e, 0, false);
        for (int i = 0; i < second; ++i) mod_frame(&r, &e, V[wpn], C[wpn], dt, (float)hip, 0, 1);
        const bool offset = !read || scale < TFP_RELOAD_SCALE_LEAST;
        CHECK(r.ended == 1 && r.refused == 0 && r.end.missed == 0 && (int)r.end.scaled == 2*edge_frames); /* every frame of both edges, the blend back's last (r = 0) included */
        if (hip) {
          if (r.step_most > 2e-6f || !near(r.eye_least, rest, 2e-6) || !near(r.eye_most, rest, 2e-6))
            printf("    %4.1f ms s %.4f %s v %.3f: step %.7f place %.7f to %.7f rest %.7f\n", (double)dt, (double)scale, read ? "read" : "not read", (double)V[wpn],
                   (double)r.step_most, (double)r.eye_least, (double)r.eye_most, (double)rest);
          CHECK(r.step_most < 2e-6f && near(r.eye_least, rest, 2e-6) && near(r.eye_most, rest, 2e-6)); /* held, edges and all */
          CHECK((int)r.end.by_offset == (offset ? 2*edge_frames : 0));
        } else CHECK(r.delta_most == 0 && r.end.most == 0); /* full aim: the game's own reload, its scaling with it */
      }
    }
    /* A weapon swapped in the reload (the rig's vector 0.31 under C from then on), the end scaled. With s read: the eye
     * eases from the old vector to the new over the blend back, as unscaled; s not read or 0: it stays on the old one
     * through the scaled frames and steps to the new at the first frame after them, by the difference and no more. */
    for (int k = 0; k < 4; ++k) for (int si = 0; si < 3; ++si) {
      const float dt = DT[k], scale = si == 0 ? .2895f : si == 1 ? 0 : .5f; const int second = (int)(1000.0f/dt);
      Run r; memset(&r, 0, sizeof r); r.no_read = si == 2; Eval e = {1, 1, 0, 0, scale};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 1, false); e.bit = 1;
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .31f, 0, dt, 1, 0, 1);
      eval_edge(&e, 0, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .31f, 0, dt, 1, 0, 1);
      const float share = dt/75.0f > 1 ? 1 : dt/75.0f;
      CHECK(near(r.eye_last, .31, 1e-6) && r.eye_least >= .233f-1e-6f && r.eye_most <= .31f+1e-6f);
      if (si == 0) CHECK(r.step_most <= (.31f-.233f)*share+1e-6f && r.end.by_offset == 0);
      else CHECK(near(r.step_most, .31-.233, 1e-6) && r.end.by_offset == r.end.scaled);
    }
    /* The held depth adds 0.02 m to node 5 at rest, the edges scaled: held with it (its own term goes in unscaled) */
    for (int k = 0; k < 4; ++k) for (int si = 0; si < 2; ++si) {
      const float dt = DT[k]; const int second = (int)(1000.0f/dt);
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0, 0, si ? 0 : .2895f};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, .02f, 1);
      eval_edge(&e, 1, false); e.bit = 1;
      for (int i = 0; i < 2*second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, .02f, 1);
      eval_edge(&e, 0, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, .02f, 1);
      CHECK(r.step_most < 1e-6f && near(r.eye_least, .253, 1e-6) && near(r.eye_most, .253, 1e-6) && r.end.missed == 0 && r.end.scaled > 0);
    }
    /* A read s that is not the game's: within (1-r) 0.3 m of the hold's own it is used (the eye then off by what the
     * two disagree), further it is refused and the offset holds the eye. The game's s 0.2895: read 0.02 (the frame's
     * vector 14 times its size: refused), read 0.5 (used). */
    for (int si = 0; si < 2; ++si) {
      const float dt = 16.7f; const int second = 60;
      Run r; memset(&r, 0, sizeof r); r.read_scale = si == 0 ? .02f : .5f; Eval e = {1, 1, 0, 0, .2895f};
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 1, false); e.bit = 1;
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 0, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      if (si == 0) CHECK(r.step_most < 1e-6f && r.end.by_offset > 0 && r.end.scaled > 0); /* (the settled frames' C alone fits any s) */
      else { CHECK(r.end.by_offset == 0 && r.eye_least >= .233f-(1-.2895f/.5f)*.233f-1e-6f && r.eye_most <= .233f+1e-6f);
             printf("    a read s 0.5 where the game's is 0.2895: the eye %.4f to %.4f m (rest 0.233; the game's own 0.0000 to 0.233)\n", (double)r.eye_least, (double)r.eye_most); }
    }
    /* What cannot be held in a scaled frame: a new lease begun in a reload, with s not read (nothing of the rig's own
     * vector is known); a value that is no number. Counted, and the line says so. */
    {
      TfpReloadHold h; memset(&h, 0, sizeof h); TfpReloadInput in; memset(&in, 0, sizeof in); float delta = 7; TfpReloadEnd end;
      in.blend = 1; in.ability = 1; in.bit = 1; in.mix_before = .4f; in.mix_after = .6f; in.argument = .02f; in.constant_known = true; in.share = 1; in.on = true;
      CHECK(tfp_reload_frame(&h, &in, 9, &delta, &end) == TFP_RELOAD_NO_HELD && delta == 0);
      in.ability = 0; in.mix_before = .4f; in.mix_after = 1; /* r = 0, scaled: the scaled space alone, s not read: nothing */
      CHECK(tfp_reload_frame(&h, &in, 9, &delta, &end) == TFP_RELOAD_SCALED && delta == 0);
      in.scale_known = true; in.scale = .25f; in.argument = .05f; /* read: the frame's own vector 0.2, the eye put there */
      CHECK(tfp_reload_frame(&h, &in, 9, &delta, &end) == TFP_RELOAD_ADDED && near(delta, .15, 1e-7));
      in.bit = 0; in.mix_before = in.mix_after = 1;
      CHECK(tfp_reload_frame(&h, &in, 9, &delta, &end) == TFP_RELOAD_NONE && end.ended && end.frames == 3 && end.missed == 2 && end.scaled == 1 && end.by_offset == 0);
      char line[240]; tfp_reload_line(&end, line, sizeof line);
      CHECK(!strcmp(line, "Reload: eye held through the reload (would have slid 0.150 m), but not in 2 of its 3 frames: the game scaled the rig by main row +0x11ac; 1 frames the game scaled by main row +0x11ac held, 0 of them by the eye's offset"));
      end.missed = 0; end.would = .202f; end.scaled = 5; end.by_offset = 2; tfp_reload_line(&end, line, sizeof line);
      CHECK(!strcmp(line, "Reload: eye held through the reload (would have slid 0.202 m); 5 frames the game scaled by main row +0x11ac held, 2 of them by the eye's offset"));
    }
    /* Frames with nothing of C, the game not scaling, whatever the bit, [rsp+0x45], the read scale: nothing added, to
     * the bit (the scale is not even looked at). */
    {
      TfpReloadHold h; memset(&h, 0, sizeof h); srand(40);
      for (int i = 0; i < 5000; ++i) {
        TfpReloadInput in; memset(&in, 0, sizeof in); float delta = 7; TfpReloadEnd end;
        in.blend = (uint8_t)(rand() & 1); in.ability = 0; in.bit = (uint8_t)(rand() & 1); in.unscaled = (uint8_t)(rand() & 1);
        in.mix_before = in.mix_after = 1; in.argument = (float)rand()/(float)RAND_MAX; in.scale = (float)rand()/(float)RAND_MAX; in.scale_known = rand() & 1;
        in.constant_known = true; in.share = 1; in.on = true; in.added = .01f;
        CHECK(tfp_reload_frame(&h, &in, 3, &delta, &end) == TFP_RELOAD_NONE && delta == 0 && !end.ended);
      }
    }
  }
  /* ---- the replay of the user's reload (0.6.0-feel.56, 2026-11-25 18:47:03 to 18:47:07) ---- */
  if (argc > 1) {
    Row *rows = NULL; char **text = NULL; int lines = 0; const int n = read_rows(argv[1], &rows, &text, &lines);
    double delta[512], c = 0;
    CHECK(n > 50);
    const int i0 = n > 0 ? replay(rows, n, delta, &c) : -1;
    CHECK(i0 > 0);
    if (i0 > 0) {
      const double rest = level_forward(&rows[i0-1], rows[i0-1].eye);
      double least = 1, most = -1, main_least = 1, main_most = -1, step_most = 0; int moved_rows = 0, unmoved_same = 1;
      for (int k = 0; k < n; ++k) {
        double eye[3]; moved(&rows[k], delta[k], eye);
        const double f = level_forward(&rows[k], eye), f0 = level_forward(&rows[k], rows[k].eye);
        least = fmin(least, f); most = fmax(most, f); main_least = fmin(main_least, f0); main_most = fmax(main_most, f0);
        moved_rows += delta[k] != 0;
        if (k < i0-1 && delta[k] != 0) unmoved_same = 0;
        if (k && rows[k].tick-rows[k-1].tick <= 80) {
          double prior[3], step = 0, root = 0; moved(&rows[k-1], delta[k-1], prior);
          for (int i = 0; i < 3; ++i) { step += (eye[i]-prior[i])*(eye[i]-prior[i]); root += (rows[k].root[i]-rows[k-1].root[i])*(rows[k].root[i]-rows[k-1].root[i]); }
          if (sqrt(root) < .005 && sqrt(step) > step_most) step_most = sqrt(step);
        }
      }
      printf("  replay of %s: %d traced frames, %d moved; C's forward part put back from the log %.4f m; the eye's offset from the root along the level view\n"
             "    as the game had it %.3f to %.3f m (rest %.3f), held %.3f to %.3f m; the largest step of the eye with the root still %.4f m\n",
             argv[1], n, moved_rows, c, main_least, main_most, rest, least, most, step_most);
      CHECK(main_most-main_least > .2);                           /* the user's slide is in the window */
      CHECK(near(rest, .233, .005) && least >= .233-.02 && most <= .233+.02); /* held within 2 cm of 0.233 m all the way through */
      CHECK(step_most <= .01 && unmoved_same && moved_rows > 10);
    }
  } else printf("  replay: no log given\n");
  /* ---- round 40: the replay of the harness's feel.58 reload (23:05:31 to 23:05:34), its scaled edges ---- */
  if (argc > 2) {
    Row *rows = NULL; char **text = NULL; int lines = 0; const int n = read_rows(argv[2], &rows, &text, &lines);
    CHECK(n > 40);
    for (int mode = 1; mode >= 0 && n > 40; --mode) {
      double delta[512], checked = 0, scales[2] = {0, 0};
      const int scaled = replay_edges(rows, n, text, lines, mode == 1, delta, &checked, scales);
      CHECK(scaled == 5); /* 3 at the start, 2 at the end: the "Reload:" line's 5 of 70 */
      if (scaled <= 0) break;
      const double rest = level_forward(&rows[0], rows[0].eye);
      double least = 1, most = -1, main_least = 1, main_most = -1, step_most = 0, own_most = 0; int moved_rows = 0;
      for (int k = 0; k < n; ++k) {
        double eye[3]; moved(&rows[k], delta[k], eye);
        const double f = level_forward(&rows[k], eye), f0 = level_forward(&rows[k], rows[k].eye);
        least = fmin(least, f); most = fmax(most, f); main_least = fmin(main_least, f0); main_most = fmax(main_most, f0);
        moved_rows += delta[k] != 0;
        if (k && rows[k].tick-rows[k-1].tick <= 80) {
          double prior[3], step = 0; moved(&rows[k-1], delta[k-1], prior);
          for (int i = 0; i < 3; ++i) step += (eye[i]-prior[i])*(eye[i]-prior[i]);
          step_most = fmax(step_most, sqrt(step));
          /* the game's own motion: a step between two frames neither of which was scaled nor next to one */
          bool near_scaled = false;
          for (int j = k-2; j <= k+1; ++j) if (j >= 0 && j < n && delta[j] != 0) near_scaled = true;
          if (!near_scaled) { double own = 0; for (int i = 0; i < 3; ++i) own += (rows[k].eye[i]-rows[k-1].eye[i])*(rows[k].eye[i]-rows[k-1].eye[i]); own_most = fmax(own_most, sqrt(own)); }
        }
      }
      printf("  replay of %s, s %s (%.4f at the start, %.4f at the end): %d traced frames, %d moved (the scaled ones);\n"
             "    the root's place along the view moved %.5f m across an edge; the eye's offset from the root along the level view as written %.3f to %.3f m (rest %.3f), held %.3f to %.3f m; the largest step %.4f m (the game's own motion away from the edges: up to %.4f m)\n",
             argv[2], mode ? "read" : "not read", scales[0], scales[1], n, moved_rows, checked, main_least, main_most, rest, least, most, step_most, own_most);
      CHECK(checked < .01);                                 /* the view turned little across the edges */
      CHECK(main_most-main_least > .15 && moved_rows == 5); /* the slide as the harness saw it, and only the scaled frames move */
      CHECK(least >= rest-.005 && most <= rest+.03 && step_most <= fmax(.01, own_most)); /* held: within the game's own motion after it (0.023 m) */
    }
  }

  /* ---- round 49e: the edges of a reload looked at (TfpReloadWatch): a diagnostic; the hold's numbers are not its ---- */
  printf("  round 49e, the sidearm's reload as the listing models it: the eye's place along the view less its rest (m)\n");
  for (int k = 0; k < 4; ++k) {
    const float dt = DT[k]; const int second = (int)(1000.0f/dt);
    /* 1. nothing reloads: nothing is reported, whatever the frames hold */
    {
      Pistol p; pistol_begin(&p, true, true, true);
      for (int i = 0; i < 3*second; ++i) pistol_frame(&p, dt, 1, 1);
      CHECK(p.reports == 0 && p.place_least == 0 && p.place_most == 0);
    }
    /* 2. the hold's own numbers are the same to the bit with the watch fed or not (it reads the hold, and writes nothing of it) */
    {
      Pistol a, b; pistol_begin(&a, true, true, true); pistol_begin(&b, true, true, false);
      for (int pass = 0; pass < 2; ++pass) {
        Pistol *p = pass ? &b : &a;
        for (int i = 0; i < second; ++i) pistol_frame(p, dt, 1, 1);
        pistol_edge(p, 1); for (int i = 0; i < 2*second; ++i) pistol_frame(p, dt, 1, 1);
        pistol_edge(p, 0); for (int i = 0; i < second; ++i) pistol_frame(p, dt, 1, 1);
      }
      CHECK(a.delta_bits_sum == b.delta_bits_sum && !memcmp(&a.h, &b.h, sizeof a.h) && a.reports == 1 && b.reports == 0);
    }
    /* 3. the mover in node 50 (read a frame late or not) or in the anchor: the eye is not kept still by today's hold in
     * any of them (the log's 0.064 m), and the report says which it was */
    for (int variant = 0; variant < 3; ++variant) {
      const bool in_eye = variant < 2, late = variant == 0;
      Pistol p; pistol_begin(&p, in_eye, late, true);
      for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      CHECK(p.place == 0);
      pistol_edge(&p, 1); for (int i = 0; i < 2*second; ++i) pistol_frame(&p, dt, 1, 1);
      CHECK(near(p.place, PISTOL_X, 1e-6) && p.reports == 0);       /* settled in the reload: the mover, whole, whichever it is */
      pistol_edge(&p, 0); for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      CHECK(near(p.place, 0, 1e-6) && p.reports == 1);
      printf("    %4.1f ms  mover in %s: the eye %.4f to %.4f m; reported %u frames of the start, %u of the end\n", (double)dt,
             in_eye ? (late ? "node 50, read a frame late" : "node 50") : "request+0x60", (double)p.place_least, (double)p.place_most, p.report.starts, p.report.ends);
      /* an anchor's mover never takes the eye behind its rest; node 50's does at the end unless read in its own frame at r = 0 */
      if (!in_eye) CHECK(p.place_least > -1e-6f);
      if (in_eye && late) CHECK(p.place_least < -.005f);
      const TfpReloadEdges *e = &p.report;
      CHECK(e->ready && e->rest_known && e->held_known && near(e->held, PISTOL_V, 1e-6) && e->constant_known && e->constant == 0);
      CHECK(e->rest.r == 0 && near(e->rest.argument, PISTOL_V, 1e-6) && near(e->rest.eye[1], PISTOL_E0, 1e-6) && e->rest.anchor[1] == 0);
      CHECK(e->starts >= 3 && e->starts <= TFP_RELOAD_EDGE_FRAMES && e->ends >= 3 && e->ends <= TFP_RELOAD_EDGE_FRAMES);
      /* the start: its first frame has C in it; the share only rises; it runs to 150 ms or the frames kept */
      CHECK(e->start[0].r > 0 && e->start[0].tick > e->rest.tick);
      for (uint32_t i = 1; i < e->starts; ++i) CHECK(e->start[i].r >= e->start[i-1].r && e->start[i].tick > e->start[i-1].tick);
      const TfpReloadProbe *last = &e->start[e->starts-1];
      CHECK(e->starts == TFP_RELOAD_EDGE_FRAMES || (last->r == 1 && last->tick-e->start[0].tick >= TFP_RELOAD_EDGE_MS));
      for (uint32_t i = 0; i < e->starts; ++i) {
        const TfpReloadProbe *f = &e->start[i];
        CHECK(near(f->put, f->r*PISTOL_V, 1e-6) && f->own == 0 && f->share == 1 && !f->scale_known && f->why == TFP_RELOAD_ADDED);
        /* the listing's vector, from the line's own columns: request+0x4c - C = (1-r) (v + node 50's move as read) + r C */
        if (in_eye) CHECK(near(f->argument, (1-f->r)*(PISTOL_V+(f->eye[1]-e->rest.eye[1])), 2e-6));
        else CHECK(near(f->argument, (1-f->r)*PISTOL_V, 2e-6) && f->eye[1] == e->rest.eye[1]);
        if (in_eye) CHECK(f->anchor[1] == 0);
      }
      /* which moved: the column that did, by most of the mover where the frames kept reach its end */
      const float moved_eye = last->eye[1]-e->rest.eye[1], moved_anchor = last->anchor[1]-e->rest.anchor[1];
      if (e->starts < TFP_RELOAD_EDGE_FRAMES) CHECK(near(in_eye ? moved_eye : moved_anchor, PISTOL_X, late ? PISTOL_X*dt/100+1e-6 : 1e-6));
      else CHECK((in_eye ? moved_eye : moved_anchor) > .5f*PISTOL_X);
      CHECK((in_eye ? moved_anchor : moved_eye) == 0);
      /* the end: from the last frame with all of C through the blend back and after it */
      CHECK(e->end[0].r == 1 && e->end[1].r < 1);
      for (uint32_t i = 1; i < e->ends; ++i) CHECK(e->end[i].r <= e->end[i-1].r && e->end[i].tick > e->end[i-1].tick);
      CHECK(e->end[e->ends-1].r == 0 && e->end[e->ends-1].why == TFP_RELOAD_NONE && e->end[e->ends-1].put == 0);
      CHECK(e->ends == TFP_RELOAD_EDGE_FRAMES || e->end[e->ends-1].tick-e->end[1].tick >= TFP_RELOAD_EDGE_MS);
    }
    /* 4. a reload cancelled inside its first frames: one report, its end begun where the share turned */
    {
      Pistol p; pistol_begin(&p, true, true, true);
      for (int i = 0; i < second/2; ++i) pistol_frame(&p, dt, 1, 1);
      pistol_edge(&p, 1); pistol_frame(&p, dt, 1, 1);
      pistol_edge(&p, 0); for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      CHECK(p.reports == 1 && p.report.ready && p.report.starts >= 1 && p.report.rest_known);
    }
    /* 5. a second reload begun before the first one's end was all taken: the first is reported at that frame, the second after it */
    {
      Pistol p; pistol_begin(&p, true, true, true);
      for (int i = 0; i < second/2; ++i) pistol_frame(&p, dt, 1, 1);
      pistol_edge(&p, 1); for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      pistol_edge(&p, 0);
      int frames = 0; while (p.h.active || frames == 0) { pistol_frame(&p, dt, 1, 1); ++frames; } /* to the first frame with nothing of C */
      CHECK(p.reports == 0);
      pistol_edge(&p, 1); pistol_frame(&p, dt, 1, 1);
      CHECK(p.reports == 1 && p.report.ends >= 2 && p.report.end[p.report.ends-1].r == 0);
      for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      pistol_edge(&p, 0); for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      CHECK(p.reports == 2 && p.report.rest_known && p.report.rest.r == 0);
    }
    /* 6. another lease in the reload: what was taken of the old one is dropped; the new lease's reload has no rest frame */
    {
      Pistol p; pistol_begin(&p, true, true, true);
      for (int i = 0; i < second/2; ++i) pistol_frame(&p, dt, 1, 1);
      pistol_edge(&p, 1); for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 1);
      for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 2);
      pistol_edge(&p, 0); for (int i = 0; i < second; ++i) pistol_frame(&p, dt, 1, 2);
      CHECK(p.reports == 1 && !p.report.rest_known && !p.report.held_known && p.report.starts >= 1 && p.report.ends >= 2);
      char line[640]; CHECK(tfp_reload_edges_line(&p.report, 0, line, sizeof line) && strstr(line, "Reload rest: not seen"));
    }
  }
  /* the lines: a reload at 33.3 ms, the mover in node 50 read a frame late (what the log's rows fit best) */
  {
    Pistol p; pistol_begin(&p, true, true, true);
    for (int i = 0; i < 30; ++i) pistol_frame(&p, 33.3f, 1, 1);
    pistol_edge(&p, 1); for (int i = 0; i < 60; ++i) pistol_frame(&p, 33.3f, 1, 1);
    pistol_edge(&p, 0); for (int i = 0; i < 30; ++i) pistol_frame(&p, 33.3f, 1, 1);
    CHECK(p.reports == 1);
    p.report.sidearm = true;
    char line[640]; uint32_t lines = 0, starts = 0, ends = 0;
    while (tfp_reload_edges_line(&p.report, lines, line, sizeof line)) {
      printf("    %s\n", line);
      CHECK(strlen(line) < 600);
      if (lines == 0) CHECK(!strcmp(line, "Reload rest: tick 999; node 50 at (0.0100 -0.3717 0.0200), request+0x60 (0.0000 0.0000 0.0000); argument -0.1250 m, C 0.0000 m, "
                                           "the rig's own vector held -0.1250 m; hip share 1.00; sidearm 1"));
      starts += !strncmp(line, "Reload start: ", 14); ends += !strncmp(line, "Reload end: ", 12);
      ++lines;
    }
    CHECK(lines == 1+starts+ends && starts == (p.report.starts+TFP_RELOAD_EDGE_LINE-1)/TFP_RELOAD_EDGE_LINE && ends == (p.report.ends+TFP_RELOAD_EDGE_LINE-1)/TFP_RELOAD_EDGE_LINE);
    const char *first = "Reload start: +33 ms r 0.444 arg -0.0695 own +0.0000 put -0.0555 n50 (+0.0000 +0.0000 +0.0000) a60 (+0.0000 +0.0000 +0.0000); "
                        "+67 ms r 0.888 arg -0.0116 own +0.0000 put -0.1110 n50 (+0.0000 +0.0215 +0.0000) a60 (+0.0000 +0.0000 +0.0000); ";
    CHECK(tfp_reload_edges_line(&p.report, 1, line, sizeof line) && !strncmp(line, first, strlen(first)));
    CHECK(!tfp_reload_edges_line(&p.report, lines, line, sizeof line) && !tfp_reload_edges_line(NULL, 0, line, sizeof line));
    /* the longest a line can be: every frame kept, scaled, eased by the hip share, not held, far ticks */
    TfpReloadEdges worst = p.report; worst.starts = worst.ends = TFP_RELOAD_EDGE_FRAMES;
    for (uint32_t i = 0; i < TFP_RELOAD_EDGE_FRAMES; ++i) {
      TfpReloadProbe *f = &worst.start[i]; memset(f, 0, sizeof *f);
      f->tick = worst.rest.tick+99999; f->r = .123f; f->argument = f->own = f->put = -1.2345f; f->share = .5f; f->scale = .3103f; f->scale_known = true; f->kind = TFP_RELOAD_SCALED;
      f->why = TFP_RELOAD_SCALED; f->eye_known = f->anchor_known = true; for (int a = 0; a < 3; ++a) { f->eye[a] = -3.9999f; f->anchor[a] = -1234.5678f; }
      worst.end[i] = *f;
    }
    for (uint32_t i = 0; tfp_reload_edges_line(&worst, i, line, sizeof line); ++i) CHECK(strlen(line) < 639);
    worst.start[0].eye_known = worst.start[0].anchor_known = false; worst.start[0].scale_known = false;
    CHECK(tfp_reload_edges_line(&worst, 1, line, sizeof line) && strstr(line, "n50 ? a60 ?") && strstr(line, " x?") && strstr(line, " hip 0.50") && strstr(line, " not held"));
    /* the second's: two kept, all counted */
    TfpReloadSecond s; memset(&s, 0, sizeof s); TfpReloadEdges none; memset(&none, 0, sizeof none);
    tfp_reload_note_edges(&s, &none); CHECK(s.edge_count == 0);
    for (int i = 0; i < 3; ++i) { p.report.starts = (uint32_t)i+1; tfp_reload_note_edges(&s, &p.report); }
    CHECK(s.edge_count == 3 && s.edges[TFP_RELOAD_EDGES_LOGGED-1].starts == TFP_RELOAD_EDGES_LOGGED);
  }
  /* ---- round 49e: the sidearm's reload of 0.6.0-feel.60 (19:36:08.7 to 19:36:11.5), as the log has it ---- */
  if (argc > 3) {
    Row *rows = NULL; char **text = NULL; int lines = 0; const int n = read_rows(argv[3], &rows, &text, &lines);
    CHECK(n == 42);
    if (n == 42) {
      double along[42], across[42], root_most = 0;
      for (int k = 0; k < n; ++k) { along_view(rows, k, &along[k], &across[k]); for (int i = 0; i < 3; ++i) root_most = fmax(root_most, fabs(rows[k].root[i]-rows[0].root[i])); }
      /* the edges: the start's three frames (rows 7 to 9), the end's four (rows 27 to 30) */
      int s0 = -1, e0 = -1;
      for (int k = 1; k < n && s0 < 0; ++k) if (along[k]-along[k-1] > .01) s0 = k;
      for (int k = s0+4; s0 > 0 && k < n && e0 < 0; ++k) if (along[k]-along[k-1] < -.01) e0 = k;
      CHECK(s0 == 7 && e0 == 27 && root_most == 0);
      if (s0 == 7 && e0 == 27) {
        const double in = along[s0+2], settled = along[e0-1], rest = along[e0+3];
        printf("  the sidearm's reload of feel.60 (%s): held by the line (%s), and the eye went %.4f m along the view over three frames (%.4f, %.4f, %.4f),\n"
               "    %.4f m across it in three equal steps; at the end %.4f, %.4f, %.4f, %.4f m: %.4f m behind its rest, then forward to it\n",
               argv[3], strstr(text[lines-2], "Reload: ") ? "Reload: ... would have slid 0.125 m" : "?", in, along[s0]-along[s0-1], along[s0+1]-along[s0], along[s0+2]-along[s0+1],
               across[s0+2]-across[s0-1], along[e0]-along[e0-1], along[e0+1]-along[e0], along[e0+2]-along[e0+1], along[e0+3]-along[e0+2], rest-along[e0+2]);
        bool said = false; for (int i = 0; i < lines; ++i) if (strstr(text[i], "Reload: eye held through the reload (would have slid 0.125 m)\n")) said = true;
        CHECK(said);                                                       /* every frame held, by the hold's own count */
        CHECK(near(in, .064, .002) && near(settled, in, .004));            /* and the eye 0.064 m off for the whole reload (the view turns a little in it) */
        /* not a share of the hold: the blend's shares are 0.44, 0.89, 1 of it (33 ms frames, 75 ms), the eye's thirds */
        for (int i = 0; i < 3; ++i) CHECK(near((along[s0+i]-along[s0-1])/in, (i+1)/3.0, .04));
        CHECK(fabs(along[s0]/in-.445) > .08);
        /* a part across the view, in equal steps: no forward value makes it (the hold's pair and the game's root go along the view) */
        for (int i = 0; i < 3; ++i) CHECK(near(across[s0+i]-across[s0+i-1], -.0029, .0003));
        for (int i = 0; i < 3; ++i) CHECK(near(across[e0+i]-across[e0+i-1], +.0029, .0003));
        /* the end goes behind the rest and comes forward again: the rig's own vector had the mover in it as the blend
         * went back (a mover outside the vector cannot: case 3 above) */
        CHECK(along[e0+2] < rest-.012 && near(rest, 0, .004) && near(along[e0+3]-along[e0+2], .0155, .002));
        /* the model with the mover in node 50, read a frame late, against the rows: the start to 2.5 mm */
        Pistol p; pistol_begin(&p, true, true, false);
        for (int i = 0; i < 30; ++i) pistol_frame(&p, 33.3f, 1, 1);
        pistol_edge(&p, 1);
        double worst = 0; float model[3];
        for (int i = 0; i < 3; ++i) { model[i] = pistol_frame(&p, (float)(rows[s0+i].tick-rows[s0+i-1].tick), 1, 1); worst = fmax(worst, fabs(model[i]-(along[s0+i]-along[s0-1]))); }
        printf("    the model (the mover in node 50, read a frame late) at the start: %.4f, %.4f, %.4f m against %.4f, %.4f, %.4f\n",
               (double)model[0], (double)model[1], (double)model[2], along[s0]-along[s0-1], along[s0+1]-along[s0-1], along[s0+2]-along[s0-1]);
        CHECK(worst < .0025);
        /* read in its own frame it would not fit: the first frame would show 0.44 of a third less */
        Pistol q; pistol_begin(&q, true, false, false);
        for (int i = 0; i < 30; ++i) pistol_frame(&q, 33.3f, 1, 1);
        pistol_edge(&q, 1);
        CHECK(fabs(pistol_frame(&q, (float)(rows[s0].tick-rows[s0-1].tick), 1, 1)-(along[s0]-along[s0-1])) > .008);
      }
    }
  } else printf("  the sidearm's reload of feel.60: no log given\n");
  printf("Reload hold: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
