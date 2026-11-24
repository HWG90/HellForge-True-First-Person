/* Round 20: the sight brought closer in full aim (src/ads_seat.h) and the "Aim:" line (src/aim_log.h).
 * docs/design/specs/2026-11-22-locked-view-design.md "Round 20". No game. */
#include "../src/ads_seat.h"
#include "../src/aim_log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool near(double a, double b, double e) { return fabs(a - b) <= e; }
#define DEG(d) ((float)((d)*3.14159265358979/180))

int main(void) {
  /* ---- the pull: from the sight the game sets the near clip to, to the wanted distance ---- */
  CHECK(tfp_ads_pull_for(.173f, .10f) > .0729f && tfp_ads_pull_for(.173f, .10f) < .0731f);   /* the Scythe's sight (soak and the user's log) */
  CHECK(near(tfp_ads_pull_for(.35f, .10f), .25f, 1e-6));                                     /* a scope's far zoom level (0.35 m, live): the most */
  CHECK(near(tfp_ads_pull_for(.60f, .10f), TFP_ADS_PULL_MOST, 0));                           /* never more than the most */
  CHECK(tfp_ads_pull_for(.066f, .10f) == 0);                                                 /* the Nova's scope is already closer: left */
  CHECK(tfp_ads_pull_for(.05f, .10f) == 0 && tfp_ads_pull_for(.05f, .01f) == 0);             /* the game's own 0.05: no sight, nothing to pull */
  CHECK(tfp_ads_pull_for(.173f, 0) == 0);                                                    /* 0 cm: the game's own seat */
  CHECK(tfp_ads_pull_for(NAN, .10f) == 0 && tfp_ads_pull_for(-1, .10f) == 0 && tfp_ads_pull_for(1.5f, .10f) == 0 && tfp_ads_pull_for(.2f, NAN) == 0);
  /* ---- the glide: 80 ms, from where it stood; the first frame (or after a pause) is the target at once, since the
   * ADS share itself eases the pull in from nothing ---- */
  TfpAdsPull p; tfp_ads_pull_reset(&p);
  double t = 1000;
  CHECK(tfp_ads_pull_step(&p, .073f, t) == .073f);
  float last = .073f, worst = 0;
  for (int i = 0; i < 60; i++) { const float now = tfp_ads_pull_step(&p, .25f, t += 8); if (now - last > worst) worst = now - last; CHECK(now >= last && now <= .25f); last = now; }
  CHECK(near(last, .25f, 1e-3) && worst < .02f);                                              /* another zoom level: glided, no step over 2 cm a frame */
  CHECK(tfp_ads_pull_step(&p, .25f, t) == last);                                              /* time standing still: the same */
  for (int i = 0; i < 60; i++) last = tfp_ads_pull_step(&p, 0, t += 16);
  CHECK(last == 0);                                                                           /* a glide to nothing ends at nothing */
  tfp_ads_pull_step(&p, .25f, t += 16);
  CHECK(tfp_ads_pull_step(&p, 0, t += 400) == 0);                                             /* a pause over 250 ms: at once */
  CHECK(tfp_ads_pull_step(&p, NAN, t += 8) == 0 && isfinite(tfp_ads_pull_step(&p, .1f, t - 50)));
  /* ---- how much closer the gun is drawn: the pull by the ADS share, less the ADS placement's forward ---- */
  CHECK(near(tfp_ads_closer(.073f, 1, 0), .073f, 1e-7) && tfp_ads_closer(.073f, 0, 0) == 0 && near(tfp_ads_closer(.073f, .5f, 0), .0365f, 1e-7));
  CHECK(near(tfp_ads_closer(.073f, 1, -.02f), .093f, 1e-7));                                  /* the editor's ADS forward -2 cm: 2 cm more */
  CHECK(tfp_ads_closer(0, 1, .03f) == 0);                                                      /* pushed away: nothing closer, the near is the game's */
  /* ---- the near clip in full aim: the game's own, moved in with the gun, never under the game's 0.05 ---- */
  CHECK(near(tfp_ads_near(.173f, .073f), .10f, 1e-6) && tfp_ads_near(.173f, 0) == .173f);
  CHECK(tfp_ads_near(.066f, .03f) == .05f && tfp_ads_near(.05f, .2f) == .05f && tfp_ads_near(.35f, .25f) > .0999f);
  CHECK(isnan(tfp_ads_near(NAN, .1f)) && tfp_ads_near(.2f, NAN) == .2f);
  /* ---- a wanted distance inside the game's own 0.05: the sight goes no nearer than 0.05 (a near clip never does) ---- */
  CHECK(near(tfp_ads_pull_for(.173f, .01f), .123f, 1e-6) && near(tfp_ads_pull_for(.173f, .05f), .123f, 1e-6));
  /* ---- where the sight can be (the review's B2): the near clip less what the game's depth lacks of its rest, and the
   * last settled pair moved by the depth ---- */
  TfpAdsSeat seat; tfp_ads_seat_reset(&seat);
  t = 2000;
  CHECK(tfp_ads_sight_least(&seat, .35f, NAN, NAN, t) == .35f);                              /* no depth: the near clip */
  CHECK(near(tfp_ads_sight_least(&seat, .35f, .30f, .40f, t += 8), .25f, 1e-6));             /* D 10 cm short of its rest: 10 cm nearer */
  CHECK(tfp_ads_sight_least(&seat, .35f, .45f, .40f, t += 8) == .35f);                        /* D beyond its rest: the near clip */
  for (int i = 0; i < 6; i++) tfp_ads_sight_least(&seat, .35f, .40f, .40f, t += 8);
  CHECK(!seat.has);                                                                           /* 40 ms at rest: not yet a pair */
  for (int i = 0; i < 2; i++) tfp_ads_sight_least(&seat, .35f, .40f, .40f, t += 8);
  CHECK(seat.has && seat.sight == .35f && seat.depth == .40f);                                /* 56 ms: the pair */
  /* the zoom level changes and the game reseats at once; the near clip the pose helper has is still the old one */
  CHECK(near(tfp_ads_sight_least(&seat, .35f, .27f, .27f, t += 8), .22f, 1e-6));
  CHECK(near(tfp_ads_sight_least(&seat, .22f, .27f, .27f, t += 8), .22f, 1e-6));              /* a tick later: the same */
  for (int i = 0; i < 8; i++) tfp_ads_sight_least(&seat, .22f, .27f, .27f, t += 8);
  CHECK(seat.has && seat.sight == .22f && seat.depth == .27f);                                /* paired anew */
  /* the other way, the game reseating slowly: the sight with D, not at the new near clip; and the near clip still the
   * old one (a tick late) against the new rest counts nothing twice: the gun does not jump back out */
  CHECK(near(tfp_ads_sight_least(&seat, .22f, .27f, .40f, t += 8), .22f, 1e-6));
  CHECK(near(tfp_ads_sight_least(&seat, .35f, .30f, .40f, t += 8), .25f, 1e-6));
  CHECK(tfp_ads_sight_least(&seat, .05f, .30f, .40f, t += 8) == .05f && !seat.has);          /* no sight: nothing known */
  /* the most the gun may be pulled: the sight no nearer than the wanted distance, nor than 0.05 */
  CHECK(near(tfp_ads_pull_most(.22f, .10f), .12f, 1e-6) && tfp_ads_pull_most(.05f, .10f) == 0 && tfp_ads_pull_most(1, .10f) == TFP_ADS_PULL_MOST);
  CHECK(near(tfp_ads_pull_most(.22f, .02f), .17f, 1e-6) && tfp_ads_pull_most(.22f, 0) == TFP_ADS_PULL_MOST && tfp_ads_pull_most(NAN, .1f) == TFP_ADS_PULL_MOST);
  TfpAdsPull held; tfp_ads_pull_reset(&held);
  tfp_ads_pull_step(&held, .25f, 100);
  CHECK(tfp_ads_pull_limit(&held, .13f) == .13f && tfp_ads_pull_limit(&held, .2f) == .13f && tfp_ads_pull_limit(&held, -1) == 0);
  CHECK(tfp_ads_pull_step(&held, .25f, 108) > 0 && tfp_ads_pull_step(&held, .25f, 116) < .25f);   /* from where it was held, glided */
  /* ---- the near clip's lead (the review's B1): coming in, three times the last step ahead; going out, the gun itself ---- */
  TfpAdsLead lead; memset(&lead, 0, sizeof lead);
  CHECK(tfp_ads_lead(&lead, 0, 100) == 0);
  CHECK(near(tfp_ads_lead(&lead, .01f, 116), .04f + TFP_ADS_SNAP, 1e-7));                   /* 1 cm in: led by 3 cm more (and a snap) */
  CHECK(near(tfp_ads_lead(&lead, .02f, 132), .05f + TFP_ADS_SNAP, 1e-7));
  CHECK(near(tfp_ads_lead(&lead, .02f, 148), .02f, 1e-7));                                   /* standing: the gun itself */
  CHECK(near(tfp_ads_lead(&lead, .015f, 164), .015f, 1e-7));                                 /* going out: the gun itself */
  CHECK(near(tfp_ads_lead(&lead, .05f, 600), .05f, 1e-7));                                   /* after a pause: no step is known */
  CHECK(tfp_ads_lead(&lead, NAN, 616) == 0 && tfp_ads_lead(&lead, -.1f, 632) == 0);
  /* ---- the "Aim:" line ---- */
  TfpAimLog log; tfp_aim_log_reset(&log);
  char line[700];
  TfpAimSample s; memset(&s, 0, sizeof s);
  s.weapon = "LAS-5 Scythe"; s.game_fov = DEG(61.5); s.shown_fov = DEG(105); s.aim_field = TFP_FOV_AIM_ZOOM; s.sight = .173f; s.wanted = .10f;
  s.pull = 0; s.closer = 0; s.near_clip = .10f; s.depth = NAN; s.now_ms = 5000;
  CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line));                                       /* at the hip: nothing */
  s.full_ads = true; s.now_ms += 16;
  CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line));                                       /* in, settling: not yet */
  for (int i = 0; i < 30; i++) { s.now_ms += 16; s.pull = .073f; s.closer = .073f; s.shown_fov = DEG(61.5); s.near_clip = .10f; s.depth = .2102f;
    if (tfp_aim_log_step(&log, &s, line, sizeof line)) break; }
  CHECK(strstr(line, "Aim: in: \"LAS-5 Scythe\"; field of view 61.5 degrees (Aim field of view: the game's zoom; the game asked for 61.5)") == line);
  CHECK(strstr(line, "the game's sight at 0.173 m (its near clip), sight distance in aim 0.100 m: the gun 0.073 m closer (pull 0.073 m, ADS placement forward +0.000 m), near clip 0.100 m"));
  CHECK(strstr(line, "the game's depth 0.210 m; placement editor closed"));
  for (int i = 0; i < 20; i++) { s.now_ms += 16; CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line)); }   /* steady: nothing more */
  /* a zoom level: the game's lens and sight change; said once it has settled */
  s.game_fov = DEG(25.7); s.sight = .35f;
  bool said = false;
  for (int i = 0; i < 60 && !said; i++) { s.now_ms += 16; s.pull = .25f; s.closer = .25f; s.near_clip = .10f; said = tfp_aim_log_step(&log, &s, line, sizeof line); }
  CHECK(said && strstr(line, "Aim: changed: \"LAS-5 Scythe\"; field of view 61.5 degrees (Aim field of view: the game's zoom; the game asked for 25.7)"));
  CHECK(strstr(line, "the game's sight at 0.350 m"));
  /* the editor opened while aiming: said, with its action */
  s.editor = true; s.editor_action = "ads"; s.ads_forward = -.02f; s.closer = .27f;
  said = false;
  for (int i = 0; i < 60 && !said; i++) { s.now_ms += 16; said = tfp_aim_log_step(&log, &s, line, sizeof line); }
  CHECK(said && strstr(line, "ADS placement forward -0.020 m") && strstr(line, "placement editor open on \"ads\""));
  /* out: how long, and the zoom levels seen */
  s.full_ads = false; s.now_ms += 16;
  CHECK(tfp_aim_log_step(&log, &s, line, sizeof line));
  CHECK(strstr(line, "Aim: out after 1.") == line && strstr(line, "s; zoom levels seen (the game's field of view): 61.5, 25.7 degrees"));
  /* a tap under the settling time: one line at the out, with what was seen */
  s.full_ads = true; s.now_ms += 16; CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line));
  s.full_ads = false; s.now_ms += 16; CHECK(tfp_aim_log_step(&log, &s, line, sizeof line) && strstr(line, "Aim: out after 0.0") && strstr(line, "(a tap: not settled)"));
  /* nothing known: words, not numbers */
  tfp_aim_log_reset(&log); memset(&s, 0, sizeof s); s.sight = NAN; s.depth = NAN; s.game_fov = NAN; s.shown_fov = NAN; s.near_clip = NAN; s.now_ms = 100; s.aim_field = DEG(50);
  s.full_ads = true;
  for (int i = 0; i < 40 && !tfp_aim_log_step(&log, &s, line, sizeof line); i++) s.now_ms += 16;
  CHECK(strstr(line, "Aim: in: no weapon known; field of view not read (Aim field of view: a chosen 50.0 degrees; the game asked for not read)"));
  CHECK(strstr(line, "the game's sight not known (no near clip of a sight seen)") && strstr(line, "the game's depth not read"));
  printf("ads seat: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
