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
  CHECK(seat.has && seat.sight == .35f && seat.depth == .40f);                                /* round 20c: the aim's pair is kept (it says the same) */
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
  /* ---- round 20c (review 27, B1): the pull goes down no faster than the game's depth comes in ---- */
  {
    TfpAdsPull p; TfpAdsFall f; tfp_ads_pull_reset(&p); tfp_ads_fall_reset(&f);
    double at = 5000;
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .20f, .25f, .40f, .15f, true, at), .20f, 1e-6));                 /* the first frame: the target */
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, .40f, .15f, true, at += 16), .20f, 1e-6));          /* new level's limit, depth still: held */
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, .39f, .15f, true, at += 16), .19f, 1e-6));          /* depth 1 cm in: 1 cm down */
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, .30f, .15f, true, at += 16), .10f, 1e-6));          /* 9 cm in: 9 cm down */
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, .31f, .15f, true, at += 16), .10f, 1e-6));          /* depth back out: nothing down */
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, .20f, .15f, true, at += 16), .07f, 1e-6));          /* in by more than is left: at the limit */
    /* no depth: at the glide's own pace, never at once */
    tfp_ads_pull_reset(&p); tfp_ads_fall_reset(&f); at = 7000;
    tfp_ads_pull_frame(&p, &f, .35f, .20f, .25f, NAN, .15f, true, at);
    const float glided = tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, NAN, .15f, true, at += 16);
    CHECK(glided < .20f && glided > .07f && near(glided, .20f - .13f*(1 - expf(-16.f/80)), 1e-5));
    /* at the hip (share 0): the limit at once, nothing is shown of it */
    tfp_ads_pull_reset(&p); tfp_ads_fall_reset(&f); at = 9000;
    tfp_ads_pull_frame(&p, &f, .35f, .20f, .25f, .40f, .15f, false, at);
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .07f, .07f, .40f, .15f, false, at += 16), .07f, 1e-6));
    /* the player asks for a farther sight mid-aim (the menu): glided down though the depth stands still */
    {
      TfpAdsPull q; TfpAdsFall g; tfp_ads_pull_reset(&q); tfp_ads_fall_reset(&g); double when = 11000;
      tfp_ads_pull_frame(&q, &g, .35f, .20f, .25f, .40f, .15f, true, when);
      const float down = tfp_ads_pull_frame(&q, &g, .35f, .10f, .25f, .40f, .25f, true, when += 16);
      CHECK(near(down, .20f - .10f*(1 - expf(-16.f/80)), 1e-5));
      for (int i = 0; i < 60; i++) tfp_ads_pull_frame(&q, &g, .35f, .10f, .25f, .40f, .25f, true, when += 16);
      CHECK(q.shown == .10f && !g.asked);
      /* and then a nearer zoom level's limit with the depth still: held again */
      CHECK(tfp_ads_pull_frame(&q, &g, .35f, .02f, .02f, .40f, .25f, true, when += 16) == .10f);
    }
    /* review 28, I1: nothing left to pull for mid-aim, the depth standing still: glided down to nothing, never held
     * until the aim ends. The wanted distance 15 -> 0 and 5 -> 0 cm (the menu), and the sight no longer handed. */
    {
      static const struct { float sight, wanted, pull, sight_after, wanted_after; } CASES[4] = {
        {.35f, .15f, .20f, .35f, 0}, {.35f, .05f, .25f, .35f, 0}, {.35f, .15f, .20f, 0, .15f}, {.35f, .15f, .20f, .05f, .15f}};
      for (int k = 0; k < 4; k++) {
        TfpAdsPull q; TfpAdsFall g; tfp_ads_pull_reset(&q); tfp_ads_fall_reset(&g); double when = 13000;
        const float s0 = CASES[k].sight, w0 = CASES[k].wanted, s1 = CASES[k].sight_after, w1 = CASES[k].wanted_after;
        for (int i = 0; i < 20; i++) tfp_ads_pull_frame(&q, &g, s0, tfp_ads_pull_for(s0, w0), tfp_ads_pull_most(s0, w0), .40f, w0, true, when += 16);
        CHECK(near(q.shown, CASES[k].pull, 1e-6));
        const float most = tfp_ads_pull_most(s1, w1); /* the pose helper's least: the sight itself, or nothing */
        const float first = tfp_ads_pull_frame(&q, &g, s1, tfp_ads_pull_for(s1, w1), most, .40f, w1, true, when += 16);
        CHECK(near(first, CASES[k].pull*expf(-16.f/80), 1e-5)); /* the glide's first step, not at once */
        float before = first, step_most = CASES[k].pull - first;
        for (int i = 0; i < 62; i++) {
          const float now = tfp_ads_pull_frame(&q, &g, s1, tfp_ads_pull_for(s1, w1), most, .40f, w1, true, when += 16);
          CHECK(now <= before); if (before - now > step_most) step_most = before - now; before = now;
        }
        CHECK(q.shown == 0 && step_most < .05f); /* gone within a second, by at most 5 cm a frame */
      }
      /* a nearer zoom level (the sight nearer than the wanted distance) is still the game's move: held */
      TfpAdsPull q; TfpAdsFall g; tfp_ads_pull_reset(&q); tfp_ads_fall_reset(&g); double when = 15000;
      for (int i = 0; i < 20; i++) tfp_ads_pull_frame(&q, &g, .35f, .20f, .20f, .40f, .15f, true, when += 16);
      CHECK(tfp_ads_pull_frame(&q, &g, .12f, 0, 0, .40f, .15f, true, when += 16) == .20f);
    }
    /* going up: the glide, held by the limit, as before */
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .20f, .08f, .40f, .15f, true, at += 16), .08f, 1e-6));
    CHECK(near(tfp_ads_pull_frame(&p, &f, .35f, .20f, .25f, .40f, .15f, true, at += 16), .08f + .12f*(1 - expf(-16.f/80)), 1e-5));
  }
  /* ---- the near clip's lead (the review's B1): coming in, three times the last step ahead; going out, the gun itself ---- */
  TfpAdsLead lead; memset(&lead, 0, sizeof lead);
  CHECK(tfp_ads_lead(&lead, 0, 100) == 0);
  CHECK(near(tfp_ads_lead(&lead, .01f, 116), .04f + TFP_ADS_SNAP, 1e-7));                   /* 1 cm in: led by 3 cm more (and a snap) */
  CHECK(near(tfp_ads_lead(&lead, .02f, 132), .05f + TFP_ADS_SNAP, 1e-7));
  CHECK(near(tfp_ads_lead(&lead, .02f, 148), .02f, 1e-7));                                   /* standing: the gun itself */
  CHECK(near(tfp_ads_lead(&lead, .015f, 164), .015f, 1e-7));                                 /* going out: the gun itself */
  CHECK(near(tfp_ads_lead(&lead, .05f, 600), .05f, 1e-7));                                   /* after a pause: no step is known */
  CHECK(tfp_ads_lead(&lead, NAN, 616) == 0 && tfp_ads_lead(&lead, -.1f, 632) == 0);
  /* round 42: "Aim-in time" 0, the frame the gun comes in all at once is not led; the frame after it measures from it */
  memset(&lead, 0, sizeof lead);
  CHECK(tfp_ads_lead_snap(&lead, 0, 100, false) == 0);
  CHECK(near(tfp_ads_lead_snap(&lead, .073f, 116, true), .073f, 1e-7));                    /* in at once: the gun itself */
  CHECK(near(tfp_ads_lead_snap(&lead, .073f, 132, false), .073f, 1e-7));                   /* standing after it: nothing to lead */
  CHECK(near(tfp_ads_lead_snap(&lead, .083f, 148, false), .113f + TFP_ADS_SNAP, 1e-6));    /* the pull's glide after it: led as before */
  CHECK(tfp_ads_lead_snap(&lead, NAN, 164, true) == 0);
  /* and snap false is tfp_ads_lead, to the bit */
  {
    TfpAdsLead a1, b1; memset(&a1, 0, sizeof a1); memset(&b1, 0, sizeof b1);
    bool all = true; float c = 0;
    for (int i = 0; i < 40; i++) { c = i < 20 ? c + .004f*(float)(i % 3) : c - .003f; all = all && tfp_ads_lead(&a1, c, 100 + 16.0*i) == tfp_ads_lead_snap(&b1, c, 100 + 16.0*i, false); }
    CHECK(all);
  }
  /* ---- the "Aim:" line ---- */
  TfpAimLog log; tfp_aim_log_reset(&log);
  char line[700];
  TfpAimSample s; memset(&s, 0, sizeof s);
  s.weapon = "LAS-5 Scythe"; s.game_fov = DEG(61.5); s.shown_fov = DEG(105); s.aim_field = TFP_FOV_AIM_ZOOM; s.sight = .173f; s.wanted = .10f;
  s.pull = 0; s.closer = 0; s.near_clip = .10f; s.depth = NAN; s.now_ms = 5000; s.aim_in_ms = 180; /* round 42: the Aim-in time said */
  CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line));                                       /* at the hip: nothing */
  s.full_ads = true; s.now_ms += 16;
  CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line));                                       /* in, settling: not yet */
  for (int i = 0; i < 30; i++) { s.now_ms += 16; s.pull = .073f; s.closer = .073f; s.shown_fov = DEG(61.5); s.near_clip = .10f; s.depth = .2102f;
    if (tfp_aim_log_step(&log, &s, line, sizeof line)) break; }
  CHECK(strstr(line, "Aim: in: \"LAS-5 Scythe\"; fov 61.5 shown 61.5 (Aim field of view: the game's zoom); "
                     "sight 0.173 m (near clip), wanted 0.100: pull 0.073, closer 0.073 (ADS fwd +0.000), near clip 0.100 m; depth 0.210 m; editor closed; aim-in 180 ms | "
                     "0 frames counted; game/lens not seen; rig depth not worked out") == line);
  for (int i = 0; i < 20; i++) { s.now_ms += 16; CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line)); }   /* steady: nothing more */
  /* a zoom level: the game's lens and sight change; said once it has settled */
  s.game_fov = DEG(25.7); s.sight = .35f;
  bool said = false;
  for (int i = 0; i < 60 && !said; i++) { s.now_ms += 16; s.pull = .25f; s.closer = .25f; s.near_clip = .10f; said = tfp_aim_log_step(&log, &s, line, sizeof line); }
  CHECK(said && strstr(line, "Aim: changed: \"LAS-5 Scythe\"; fov 25.7 shown 61.5 (Aim field of view: the game's zoom)") == line);
  CHECK(strstr(line, "sight 0.350 m (near clip)"));
  /* the editor opened while aiming: said, with its action */
  s.editor = true; s.editor_action = "ads"; s.ads_forward = -.02f; s.closer = .27f;
  said = false;
  for (int i = 0; i < 60 && !said; i++) { s.now_ms += 16; said = tfp_aim_log_step(&log, &s, line, sizeof line); }
  CHECK(said && strstr(line, "(ADS fwd -0.020)") && strstr(line, "editor open on \"ads\""));
  /* round 20c: once a second while aiming, with the frames of that second (the field-of-view helper's and the rig's) */
  {
    TfpFovAimSecond f; memset(&f, 0, sizeof f);
    f.frames = 58; f.noted = 58; f.gated = 29; f.changes = 57; f.third_person = 90;
    f.clear_least = f.clear_most = DEG(30); f.lens_least = DEG(30); f.lens_most = DEG(31); f.game_least = DEG(30.8); f.game_most = DEG(31.8);
    f.shown_least = DEG(30.8); f.shown_most = DEG(31.8); f.step_most = .012f; f.factor_least = 1.02f; f.factor_most = 1.03f; f.factored = 58;
    TfpSteadySecond rig; memset(&rig, 0, sizeof rig); rig.aim_frames = 58; rig.aim_depth.least = .21f; rig.aim_depth.most = .45f; rig.aim_step = .24f;
    said = false; int ticks = 0;
    for (; ticks < 80 && !said; ticks++) {
      s.now_ms += 16;
      if (ticks == 3) { tfp_aim_log_add_fov(&log, &f); tfp_aim_log_add_fov(&log, &f); tfp_aim_log_add_rig(&log, &rig); } /* two takes add up */
      said = tfp_aim_log_step(&log, &s, line, sizeof line);
    }
    printf("  %s\n", line);
    CHECK(said && ticks > 55 && strstr(line, "Aim: second: \"LAS-5 Scythe\"; fov 25.7") == line);
    CHECK(strstr(line, "| 116 frames: gate set 58 of 116 noted, changed 114 times; optic target 30.00-30.00 deg (x3.00-x3.00 of 90.0); lens 30.00-31.00; "
                       "game 30.80-31.80, shown 30.80-31.80 (corrected 0); step 1.200 %; game/lens x1.0200-x1.0300 (116); rig depth 0.2100-0.4500 m (58), step 0.2400 m"));
    for (int i = 0; i < 20; i++) { s.now_ms += 16; CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line)); }   /* the frames were said: the next second's are its own */
  }
  /* out: how long, and each zoom level seen: the game's sight and depth there, and the optic's own magnification */
  s.full_ads = false; s.now_ms += 16;
  CHECK(tfp_aim_log_step(&log, &s, line, sizeof line));
  printf("  %s\n", line);
  CHECK(strstr(line, "Aim: out after 2.") == line && strstr(line, "s; zoom levels (game fov: sight m, depth m, optic): 61.5: sight 0.173, depth 0.210; "
                                                                   "25.7: sight 0.350, depth 0.210, optic 30.00 deg x3.00 | 0 frames counted"));
  /* a tap under the settling time: one line at the out, with what was seen */
  s.full_ads = true; s.now_ms += 16; CHECK(!tfp_aim_log_step(&log, &s, line, sizeof line));
  s.full_ads = false; s.now_ms += 16; CHECK(tfp_aim_log_step(&log, &s, line, sizeof line) && strstr(line, "Aim: out after 0.0") && strstr(line, "(a tap: not settled)"));
  /* nothing known: words, not numbers */
  tfp_aim_log_reset(&log); memset(&s, 0, sizeof s); s.sight = NAN; s.depth = NAN; s.game_fov = NAN; s.shown_fov = NAN; s.near_clip = NAN; s.now_ms = 100; s.aim_field = DEG(50);
  s.full_ads = true;
  for (int i = 0; i < 40 && !tfp_aim_log_step(&log, &s, line, sizeof line); i++) s.now_ms += 16;
  CHECK(strstr(line, "Aim: in: no weapon known; fov not read shown not read (Aim field of view: a chosen 50.0 degrees)") == line);
  CHECK(strstr(line, "sight not known, wanted 0.000") && strstr(line, "depth not read"));
  CHECK(strstr(line, "editor closed; aim-in 0 ms | "));                                           /* round 42: 0 (instant) is said too */
  tfp_aim_log_reset(&log); s.aim_in_ms = NAN; s.full_ads = false; s.now_ms += 16; tfp_aim_log_step(&log, &s, line, sizeof line); s.full_ads = true;
  for (int i = 0; i < 40 && !tfp_aim_log_step(&log, &s, line, sizeof line); i++) s.now_ms += 16;
  CHECK(strstr(line, "Aim: in: ") == line && !strstr(line, "aim-in") && strstr(line, "editor closed | "));   /* not known: not said */
  /* the longest line: under 700 characters (src/mod_view.c say()) */
  {
    TfpAimLog big; tfp_aim_log_reset(&big);
    TfpAimSample w; memset(&w, 0, sizeof w);
    w.weapon = "A weapon name of sixty characters, the most the line takes...xxxxxxxxxx"; w.game_fov = DEG(123.4); w.shown_fov = DEG(111.1);
    w.aim_field = DEG(88.8); w.sight = .999f; w.wanted = .30f; w.pull = -.25f; w.closer = .25f; w.ads_forward = -1.234f; w.near_clip = 1.234f;
    w.depth = -3.999f; w.editor = true; w.editor_action = "an action name long"; w.full_ads = true; w.now_ms = 100;
    w.aim_in_ms = 300;
    TfpFovAimSecond f; memset(&f, 0, sizeof f);
    f.frames = 4000000000u; f.noted = 4000000000u; f.gated = 1; f.changes = 4000000000u; f.corrected = 4000000000u; f.third_person = 179.9f;
    f.clear_least = DEG(.1); f.clear_most = DEG(179.9); f.lens_least = DEG(.1); f.lens_most = DEG(179.9); f.game_least = DEG(.1); f.game_most = DEG(179.9);
    f.shown_least = DEG(.1); f.shown_most = DEG(179.9); f.step_most = 99.99f; f.factor_least = .5f; f.factor_most = 1.99f; f.factored = 4000000000u;
    TfpSteadySecond rig; memset(&rig, 0, sizeof rig); rig.aim_frames = 4000000000u; rig.aim_depth.least = -3.9999f; rig.aim_depth.most = -3.9998f; rig.aim_step = 7.9999f;
    char longest[2048]; size_t most = 0;
    for (int i = 0; i < 300; i++) {
      w.now_ms += 16; if (i % 25 == 0) w.game_fov += DEG(1.1); /* new zoom levels: the out line's list */
      tfp_aim_log_add_fov(&big, &f); big.fov.frames = f.frames; big.fov.noted = f.noted; big.fov.changes = f.changes; big.fov.corrected = f.corrected; big.fov.factored = f.factored;
      tfp_aim_log_add_rig(&big, &rig); big.rig.frames = rig.aim_frames;
      if (tfp_aim_log_step(&big, &w, longest, sizeof longest) && strlen(longest) > most) most = strlen(longest);
    }
    w.full_ads = false; w.now_ms += 16;
    tfp_aim_log_add_fov(&big, &f); tfp_aim_log_add_rig(&big, &rig);
    if (tfp_aim_log_step(&big, &w, longest, sizeof longest) && strlen(longest) > most) most = strlen(longest);
    printf("  the longest \"Aim:\" line: %zu characters\n", most);
    CHECK(most > 400 && most < TFP_AIM_LINE);
  }
  printf("ads seat: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
