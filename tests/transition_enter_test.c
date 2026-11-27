/* Round 42, "Aim-in time" (locked_view.json ads_transition_ms): the shared clock's ADS_ENTER duration
 * (src/transition.h tfp_transition_set_enter). tests/transition_test.c is retail's answers at the clock's own 0.18 s (the
 * vectors); this one is the setting: 180 is those answers to the bit, 90 reaches 1 in 90 ms, 0 is in aim in the first
 * frame, aim-out and the other reasons keep 0.18, and nothing is ever not a number. tests/run-transition-enter.sh. */
#include "transition.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

static TfpTransitionInput hip(void) {
  return (TfpTransitionInput){.player_identity = 7, .identity = 11, .controller_active = false, .seated = false,
                              .shoulder_aim = true, .camera_kind = 6, .action = TFP_TRANSITION_NONE, .zoom_identity = TFP_TRANSITION_NONE};
}
static bool step(TfpTransition *t, bool ads, double now, TfpTransitionPacket *p) {
  TfpTransitionInput s = hip();
  s.controller_active = ads;
  memset(p, 0, sizeof *p);
  return tfp_transition_update(t, &s, now, true, false, p);
}
static bool same(const TfpTransitionPacket *a, const TfpTransitionPacket *b) {
  return a->epoch == b->epoch && a->progress == b->progress && a->started == b->started && a->duration == b->duration &&
         a->ads == b->ads && a->reason == b->reason && a->merged == b->merged && a->continuous == b->continuous;
}
/* A hip start, `frames` of hip at `dt`, then aim in for `frames`, then out for `frames`. Every packet into `out`. */
static unsigned run(TfpTransition *t, double dt, unsigned frames, TfpTransitionPacket *out, unsigned room) {
  unsigned n = 0;
  double now = 10;
  for (unsigned phase = 0; phase < 3; phase++)
    for (unsigned i = 0; i < frames && n < room; i++, now += dt) {
      TfpTransitionPacket p;
      const bool has = step(t, phase == 1, now, &p);
      out[n++] = has ? p : (TfpTransitionPacket){0};
    }
  return n;
}

int main(void) {
  /* ---- 180: the clock never set, and set to 180, answer the same, frame for frame, at four frame times ---- */
  const double times[] = {1.0 / 30, 1.0 / 60, 1.0 / 144, .09};
  for (unsigned k = 0; k < sizeof times / sizeof *times; k++) {
    TfpTransition plain, set;
    tfp_transition_init(&plain);
    tfp_transition_init(&set);
    tfp_transition_set_enter(&set, .18);
    static TfpTransitionPacket a[300], b[300];
    const unsigned na = run(&plain, times[k], 40, a, 300), nb = run(&set, times[k], 40, b, 300);
    CHECK(na == nb);
    bool all = true;
    for (unsigned i = 0; i < na; i++) all = all && same(&a[i], &b[i]);
    CHECK(all);
  }
  /* ---- 90 ms: reaches 1 after 90 ms of aim, not before ---- */
  {
    TfpTransition t;
    TfpTransitionPacket p;
    tfp_transition_init(&t);
    tfp_transition_set_enter(&t, .09);
    double now = 10;
    for (int i = 0; i < 10; i++, now += .01) step(&t, false, now, &p);
    CHECK(step(&t, true, now, &p) && p.reason == TFP_REASON_ADS_ENTER && p.progress == 0 && p.duration == .09 && p.ads);
    const double entered = now;
    double done_at = -1;
    for (int i = 0; i < 20; i++) {
      now += .01;
      CHECK(step(&t, true, now, &p) && isfinite(p.progress) && p.progress >= 0 && p.progress <= 1);
      if (now - entered < .085) CHECK(p.progress < 1);
      if (p.progress == 1 && done_at < 0) done_at = now - entered;
    }
    CHECK(done_at > .085 && done_at < .105);
    /* a long frame spends retail's fixed step at most (0.06 s, a third of its 0.18), not a third of a shorter packet */
    tfp_transition_init(&t);
    tfp_transition_set_enter(&t, .09);
    now = 10;
    step(&t, false, now, &p);
    now += .016;
    CHECK(step(&t, true, now, &p) && p.progress == 0);
    now += .2; /* within the clock's 0.25 s gap */
    CHECK(step(&t, true, now, &p) && fabs(p.progress - 2.0 / 3) < 1e-12);
  }
  /* ---- short times at real frame rates: done within one frame of the set time (review: not held to three frames) ---- */
  {
    const double rates[] = {1.0 / 30, 1.0 / 60};
    const double sets[] = {.01, .05, .09};
    for (unsigned r = 0; r < 2; r++)
      for (unsigned k = 0; k < 3; k++) {
        TfpTransition t;
        TfpTransitionPacket p;
        tfp_transition_init(&t);
        tfp_transition_set_enter(&t, sets[k]);
        double now = 10;
        for (int i = 0; i < 5; i++, now += rates[r]) step(&t, false, now, &p);
        CHECK(step(&t, true, now, &p) && p.reason == TFP_REASON_ADS_ENTER && p.progress == 0);
        const double entered = now;
        double done_at = -1;
        for (int i = 0; i < 20 && done_at < 0; i++) {
          now += rates[r];
          CHECK(step(&t, true, now, &p) && isfinite(p.progress));
          if (p.progress == 1) done_at = now - entered;
        }
        CHECK(done_at >= sets[k] - 1e-9 && done_at < sets[k] + rates[r] + 1e-9);
      }
  }
  /* ---- 0: the motion module's AIM to ADS action in the entry tick joins the instant packet (review blocker) ---- */
  {
    TfpTransition t;
    TfpTransitionPacket p;
    tfp_transition_init(&t);
    tfp_transition_set_enter(&t, 0);
    double now = 10;
    for (int i = 0; i < 5; i++, now += .016) { step(&t, false, now, &p); tfp_transition_action(&t, 1, now, &p); }
    CHECK(step(&t, true, now, &p) && p.reason == TFP_REASON_ADS_ENTER && p.progress == 1);
    const uint64_t epoch = p.epoch;
    CHECK(tfp_transition_action(&t, 2, now, &p) && p.epoch == epoch && p.progress == 1 && p.merged == TFP_REASON_ACTION &&
          p.reason == TFP_REASON_ADS_ENTER);
    now += .016;
    CHECK(step(&t, true, now, &p) && p.epoch == epoch && p.progress == 1);
    /* a second action change in aim begins its own 0.18 packet, as before */
    CHECK(tfp_transition_action(&t, 3, now, &p) && p.epoch == epoch + 1 && p.reason == TFP_REASON_ACTION && p.duration == .18 &&
          p.progress == 0);
    /* an action change in a later tick of an instant aim-in, with none in its first: its own 0.18 packet, as before */
    {
      TfpTransition u;
      TfpTransitionPacket q;
      tfp_transition_init(&u);
      tfp_transition_set_enter(&u, 0);
      double at = 10;
      for (int i = 0; i < 5; i++, at += .016) { step(&u, false, at, &q); tfp_transition_action(&u, 1, at, &q); }
      CHECK(step(&u, true, at, &q) && q.progress == 1);
      const uint64_t began = q.epoch;
      at += .016;
      CHECK(step(&u, true, at, &q) && q.epoch == began);
      CHECK(tfp_transition_action(&u, 2, at, &q) && q.epoch == began + 1 && q.reason == TFP_REASON_ACTION && q.duration == .18 &&
            q.progress == 0);
    }
    /* and a later zoom level in aim begins its own 0.18 packet */
    TfpTransitionInput s = hip();
    s.controller_active = true; s.zoom_identity = 1;
    now += .3; /* the action packet done */
    for (int i = 0; i < 3; i++, now += .016) tfp_transition_update(&t, &s, now, true, false, &p);
    s.zoom_identity = 2;
    CHECK(tfp_transition_update(&t, &s, now, true, false, &p) && p.reason == TFP_REASON_ZOOM && p.duration == .18 && p.progress == 0);
    /* at 180 the same action joins the entering packet as retail's: one packet */
    tfp_transition_init(&t);
    now = 10;
    for (int i = 0; i < 5; i++, now += .016) { step(&t, false, now, &p); tfp_transition_action(&t, 1, now, &p); }
    CHECK(step(&t, true, now, &p) && p.reason == TFP_REASON_ADS_ENTER && p.progress == 0);
    const uint64_t at180 = p.epoch;
    CHECK(tfp_transition_action(&t, 2, now, &p) && p.epoch == at180 && p.merged == TFP_REASON_ACTION && p.duration == .18);
  }
  /* ---- 0: progress 1 in the first frame of aim; never not a number ---- */
  {
    TfpTransition t;
    TfpTransitionPacket p;
    tfp_transition_init(&t);
    tfp_transition_set_enter(&t, 0);
    double now = 10;
    for (int i = 0; i < 5; i++, now += .016) step(&t, false, now, &p);
    CHECK(step(&t, true, now, &p) && p.reason == TFP_REASON_ADS_ENTER && p.progress == 1 && p.duration == 0 && p.ads && p.continuous);
    for (int i = 0; i < 5; i++) { now += .016; CHECK(step(&t, true, now, &p) && p.progress == 1 && isfinite(p.progress)); }
    /* the same frame time twice (no time passed) and a zoom level merged into it: still 1, not a number nowhere */
    CHECK(step(&t, true, now, &p) && p.progress == 1);
    TfpTransitionInput s = hip();
    s.controller_active = true; s.zoom_identity = 1;
    now += .016;
    CHECK(tfp_transition_update(&t, &s, now, true, false, &p) && isfinite(p.progress));
    s.zoom_identity = 2;
    now += .016;
    CHECK(tfp_transition_update(&t, &s, now, true, false, &p) && p.reason == TFP_REASON_ZOOM && p.duration == .18 && p.progress == 0);
    /* ---- aim-out keeps 0.18 with the aim-in time at 0 ---- */
    s.controller_active = false;
    now += .016;
    CHECK(tfp_transition_update(&t, &s, now, true, false, &p) && p.reason == TFP_REASON_ADS_LEAVE && p.progress == 0 && p.duration == .18 && !p.ads);
    now += .018;
    CHECK(tfp_transition_update(&t, &s, now, true, false, &p) && fabs(p.progress - .1) < 1e-9);
    /* an action resumed and the clock's own result: unchanged and finite */
    CHECK(tfp_transition_result(&t, &p) && isfinite(p.progress));
    CHECK(tfp_transition_resume(&t, 11, 7, now + .01, &p) && p.reason == TFP_REASON_REACQUIRE && p.duration == .18 && p.progress == 0);
    /* in again at 0: in the first frame again */
    s.controller_active = true;
    now += .03;
    CHECK(tfp_transition_update(&t, &s, now, true, false, &p) && p.reason == TFP_REASON_ADS_ENTER && p.progress == 1);
  }
  /* ---- the setter: a value not a number, or above a second, is ignored; below 0 is 0 ---- */
  {
    TfpTransition t;
    tfp_transition_init(&t);
    CHECK(t.enter_duration == .18);
    tfp_transition_set_enter(&t, NAN);
    tfp_transition_set_enter(&t, INFINITY);
    tfp_transition_set_enter(&t, 5);
    CHECK(t.enter_duration == .18);
    tfp_transition_set_enter(&t, -1);
    CHECK(t.enter_duration == 0);
    tfp_transition_set_enter(&t, .3);
    CHECK(t.enter_duration == .3 && t.duration == .18 && t.max_step == .06);
  }
  if (failed) { printf("transition enter: %u of %u checks failed\n", failed, checks); return 1; }
  printf("transition enter: %u checks passed (aim-in time 180 = the clock never set at four frame times; 90 ms; 0 in one frame; aim-out 0.18)\n", checks);
  return 0;
}
