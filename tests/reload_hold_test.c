/* The eye held through a reload (src/reload_hold.h, round 39): the evaluator's code read for C, the share of C in the
 * rig's vector, made-up reloads through a model of the evaluator (game+0x1287a44..0x1287c10) at 8, 16.7, 33.3 and 50 ms
 * a frame, and a replay of the user's reload of 2026-11-25 (0.6.0-feel.56, tests/reference/reload/).
 *
 *   reload_hold_test [LOG]                 the tests; LOG: the reference window (the replay is skipped without it)
 *   reload_hold_test --rewrite IN OUT      the window's Trace lines with the eye as the hold puts it (for the harness's
 *                                          matrix judge: tests/test_harness_reload.py)
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
typedef struct Eval { float w; uint8_t blend, ability; } Eval;
/* One frame: the weight stepped by dt/75 ms where it is under 1 (game+0x1287aa4..0x1287aeb), then the vector. Returns
 * request+0x4c; `before` and `after` the weight as the frame found and left it. */
static float eval_frame(Eval *e, float v, float c, float dt, float *before, float *after) {
  *before = e->w;
  float y = v;
  if (e->blend) {
    if (e->w < 1) {
      e->w += dt/75.0f; if (e->w > 1) e->w = 1;
      y = e->ability ? (1-e->w)*v+e->w*c : e->w*v+(1-e->w)*c;
    } else if (e->ability) y = c;
  }
  *after = e->w;
  return y+c; /* C added a second time (0x1287bbf..0x1287beb) */
}
/* The byte changes: the game starts the weight again at 0 (as the log's 75 ms ramps say), or (`turn`) turns the blend
 * round from where it is: w' = 1-w, the same mix of v and C. */
static void eval_edge(Eval *e, uint8_t ability, bool turn) { e->blend = 1; e->ability = ability; e->w = turn ? 1-e->w : 0; }

typedef struct Run { TfpReloadHold h; float eye_last, step_most, eye_least, eye_most, delta_most; int ended; TfpReloadEnd end; bool started; uint32_t refused; } Run;
/* One frame of the mod: the hold's delta added to the game's value (what the eye's offset from the root along the view
 * then is, with the root still). `share`: the hip share; `added`: the held depth's own term at rest. */
static float mod_frame(Run *r, Eval *e, float v, float c, float dt, float share, float added, uint64_t lease) {
  float before = 0, after = 0;
  const float game = eval_frame(e, v, c, dt, &before, &after);
  TfpReloadInput in; memset(&in, 0, sizeof in);
  in.blend = e->blend; in.ability = e->ability; in.mix_before = before; in.mix_after = after; in.argument = game; in.constant = c;
  float r_now = 0; /* the held depth's own term goes with its gain, 1-r (tfp_steady_gain): `added` is its value at rest */
  if (tfp_reload_share(e->blend, e->ability, 0, before, after, &r_now) == TFP_RELOAD_ADDED) added *= 1-r_now;
  in.constant_known = true; in.added = added; in.share = share; in.on = true;
  float delta = 7; TfpReloadEnd end;
  const uint32_t why = tfp_reload_frame(&r->h, &in, lease, &delta, &end);
  if (why != TFP_RELOAD_NONE && why != TFP_RELOAD_ADDED) ++r->refused;
  if (end.ended) { ++r->ended; r->end = end; }
  const float eye = game+added+delta;
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

int main(int argc, char **argv) {
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
    CHECK(tfp_reload_share(0, 1, 0, 1, 1, &r) == TFP_RELOAD_NONE && r == 0);    /* request+0x8d clear: the game does nothing */
    CHECK(tfp_reload_share(1, 0, 0, 1, 1, &r) == TFP_RELOAD_NONE && r == 0);    /* settled, +0x8f clear: the rig's own */
    CHECK(tfp_reload_share(1, 1, 0, 1, 1, &r) == TFP_RELOAD_ADDED && r == 1);   /* settled in a reload: C alone */
    CHECK(tfp_reload_share(1, 1, 0, .25f, .5f, &r) == TFP_RELOAD_ADDED && r == .5f);
    CHECK(tfp_reload_share(1, 0, 0, .25f, .5f, &r) == TFP_RELOAD_ADDED && r == .5f);
    CHECK(tfp_reload_share(1, 0, 0, .75f, 1, &r) == TFP_RELOAD_NONE && r == 0); /* the blend back at its end */
    CHECK(tfp_reload_share(1, 1, 0, 0, 0, &r) == TFP_RELOAD_NONE && r == 0);    /* the first frame of a reload, nothing of C yet */
    CHECK(tfp_reload_share(1, 1, 1, .25f, .5f, &r) == TFP_RELOAD_SCALED);       /* the avatar's bit in the blend: scaled by main row +0x11ac */
    CHECK(tfp_reload_share(1, 1, 1, 1, 1, &r) == TFP_RELOAD_ADDED && r == 1);   /* settled: no scaling there */
    CHECK(tfp_reload_share(1, 1, 0, NAN, .5f, &r) == TFP_RELOAD_RANGE && tfp_reload_share(1, 1, 0, .5f, 1.5f, &r) == TFP_RELOAD_RANGE);
    srand(39);
    for (int i = 0; i < 20000; ++i) {
      TfpSteadyInput in; memset(&in, 0, sizeof in);
      in.blend = (uint8_t)(rand() & 1); in.ability = (uint8_t)(rand() & 1); in.bit = (uint8_t)(rand()%4 == 0);
      in.mix_before = rand()%3 == 0 ? 1 : (float)rand()/(float)RAND_MAX; in.mix_after = rand()%3 == 0 ? 1 : (float)rand()/(float)RAND_MAX;
      float gain = -1; const bool ok = tfp_steady_gain(&in, &gain); const uint32_t why = tfp_reload_share(in.blend, in.ability, in.bit, in.mix_before, in.mix_after, &r);
      CHECK(ok == (why != TFP_RELOAD_SCALED && why != TFP_RELOAD_RANGE));
      if (ok) CHECK(near(r, 1-gain, 1e-7));
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
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0};
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
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0};
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
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0};
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
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0};
      for (int i = 0; i < second/2; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      eval_edge(&e, 1, false);
      for (int i = 0; i < second; ++i) mod_frame(&r, &e, .233f, 0, dt, 1, 0, 1);
      for (int i = 0; i <= second/4; ++i) { const float share = 1-(float)i/(float)(second/4); const float eye = mod_frame(&r, &e, .233f, 0, dt, share, 0, 1); CHECK(near(eye, .233*share, 1e-6)); }
    }
    /* 5. The held depth adds 0.02 m to node 5 at rest: held with it, so the eye stays where the two put it */
    {
      Run r; memset(&r, 0, sizeof r); Eval e = {1, 1, 0};
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
    in.on = true; in.mix_before = .5f; in.bit = 1;
    CHECK(tfp_reload_frame(&h, &in, 2, &delta, &end) == TFP_RELOAD_SCALED && delta == 0);
    in.bit = 0; in.mix_before = in.mix_after = 1;
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
  printf("Reload hold: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
