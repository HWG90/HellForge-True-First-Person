/* Camera lock maths (src/camera_lock.h): the kick the view takes from the head's own movement. */
#include "camera_lock.h"
#include <stdio.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static TfpQ about(float x, float y, float z, float degrees) {
  const float h = degrees*TFP_PI/360, s = sinf(h); return tfp_q(x*s, y*s, z*s, cosf(h));
}
/* Damping: the head's pitch against a steady view `ms` after the start of a script: 1 a step of 6 degrees at 100 ms,
 * 2 six degrees there and back within 100 ms (a half sine from 100 to 200 ms). */
static float pitch_at(int which, double ms) {
  if (which == 1) return ms >= 100 ? 6.0f : 0;
  return ms > 100 && ms < 200 ? 6.0f*sinf((float)(ms-100)/100*TFP_PI) : 0;
}
typedef struct Played { float peak, change, last, at; bool sound; } Played;
/* Plays a script at `fps` for `until` ms: the largest kick, the largest change of the kick from one frame to the
 * next, the kick of the last frame and of the frame at `sample` ms (all in degrees), and that every rotation
 * returned was a unit one. */
static Played play(int which, float damp_ms, double fps, double until, double sample) {
  const TfpKickTuning t = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = damp_ms};
  const TfpQ view = about(0, 0, 1, 37);
  TfpKick k; tfp_kick_reset(&k);
  Played p = {0, 0, 0, 0, true}; TfpQ before = tfp_q_identity();
  for (int i = 0; i*1000.0/fps <= until; ++i) {
    const double ms = i*1000.0/fps;
    const TfpQ out = tfp_kick_step(&k, view, tfp_q_mul(view, about(1, 0, 0, pitch_at(which, ms))), (float)(1/fps), &t);
    if (!tfp_q_valid(out) || fabsf(sqrtf(tfp_q_dot(out, out))-1) > 1e-4f) p.sound = false;
    const float size = tfp_degrees_between(out, tfp_q_identity()), change = tfp_degrees_between(out, before);
    if (size > p.peak) p.peak = size;
    if (change > p.change) p.change = change;
    if (ms <= sample) p.at = size;
    p.last = size; before = out;
  }
  return p;
}
int main(void) {
  const TfpKickTuning t = {.gain = .5f, .max_degrees = 10, .settle_ms = 300};
  TfpKick k; tfp_kick_reset(&k);
  const TfpQ camera = about(0, 0, 1, 40);            /* looking 40 degrees to the side */
  const TfpQ head0 = tfp_q_mul(camera, about(1, 0, 0, 5));
  /* The first frame and a still head: no kick. */
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, head0, .016f, &t), tfp_q_identity()) < .001f);
  for (int i = 0; i < 20; ++i) CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, head0, .016f, &t), tfp_q_identity()) < .001f);
  /* Turning the view with the head following exactly: no kick (mouse look is not a body movement). */
  for (int i = 1; i <= 30; ++i) {
    const TfpQ turned = about(0, 0, 1, 40 + 3.0f*i);
    CHECK(tfp_degrees_between(tfp_kick_step(&k, turned, tfp_q_mul(turned, about(1, 0, 0, 5)), .016f, &t), tfp_q_identity()) < .01f);
  }
  /* The head pitches 8 degrees in one frame: half of it reaches the view, about the same axis, in the camera's frame. */
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  const TfpQ kicked = tfp_kick_step(&k, camera, tfp_q_mul(camera, tfp_q_mul(about(1, 0, 0, 8), about(1, 0, 0, 5))), .016f, &t);
  CHECK(tfp_degrees_between(kicked, about(1, 0, 0, 4 * expf(-.016f/.3f))) < .05f);
  /* It settles: after two seconds of the head staying there the kick is gone. */
  TfpQ last = kicked;
  for (int i = 0; i < 125; ++i) last = tfp_kick_step(&k, camera, tfp_q_mul(camera, tfp_q_mul(about(1, 0, 0, 8), about(1, 0, 0, 5))), .016f, &t);
  CHECK(tfp_degrees_between(last, tfp_q_identity()) < .05f);
  /* A violent movement (the head thrown 120 degrees) is limited to the maximum. */
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  const TfpQ thrown = tfp_kick_step(&k, camera, tfp_q_mul(camera, about(0, 1, 0, 120)), .016f, &t);
  CHECK(fabsf(tfp_degrees_between(thrown, tfp_q_identity()) - 10) < .05f);
  /* Gain 0 switches it off; a long gap or a bad value starts again without a kick. */
  const TfpKickTuning off = {.gain = 0, .max_degrees = 10, .settle_ms = 300};
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &off);
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, tfp_q_mul(camera, about(0, 1, 0, 60)), .016f, &off), tfp_q_identity()) < .001f);
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, tfp_q_mul(camera, about(0, 1, 0, 60)), 0, &t), tfp_q_identity()) < .001f);
  CHECK(tfp_degrees_between(tfp_kick_step(&k, camera, tfp_q(NAN, 0, 0, 1), .016f, &t), tfp_q_identity()) < .001f);
  /* Seen in the game (2026-11-22): the head does not follow the view rigidly, so a turn of the mouse or the recoil
   * of a shot moved the view against the head and came back as a kick. While the view itself turns fast
   * (60 degrees a second and more) nothing is added, and nothing is left over when it stops. */
  tfp_kick_reset(&k); tfp_kick_step(&k, camera, head0, .016f, &t);
  float worst = 0;
  for (int i = 1; i <= 40; ++i) { /* 2 degrees a frame = 125 degrees a second; the head stays where it was */
    const float size = tfp_degrees_between(tfp_kick_step(&k, tfp_q_mul(camera, about(1, 0, 0, 2.0f*i)), head0, .016f, &t), tfp_q_identity());
    if (size > worst) worst = size;
  }
  CHECK(worst < .01f);
  for (int i = 0; i < 20; ++i) { /* the view stops: no kick appears afterwards */
    const float size = tfp_degrees_between(tfp_kick_step(&k, tfp_q_mul(camera, about(1, 0, 0, 80)), head0, .016f, &t), tfp_q_identity());
    if (size > worst) worst = size;
  }
  CHECK(worst < .01f);
  /* Applying: the view is the game's rotation turned by the kick in its own frame. */
  const TfpQ applied = tfp_kick_apply(camera, about(1, 0, 0, 4));
  CHECK(tfp_degrees_between(applied, tfp_q_mul(camera, about(1, 0, 0, 4))) < .001f);
  /* The same kick at any frame rate: a view that is still but carries rounding in its last bit (rebuilt every frame
   * from a matrix) is still. Measured through acos, one step below 1 reads as 0.04 degrees, which at 2 ms a frame is
   * 20 degrees a second of "turning": the kick came out weaker and gone in three frames at 500 frames a second. */
  {
    const TfpKickTuning tune = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f};
    float sizes[2];
    for (int noisy = 0; noisy < 2; ++noisy) {
      TfpKick kick; tfp_kick_reset(&kick);
      const TfpQ steady = about(0, 0, 1, 37);
      TfpQ rough = steady; /* the nearest rotation the acos measure tells apart from `steady` */
      for (int n = 0; n < 4096 && !(tfp_degrees_between(steady, rough) > 0); ++n) { rough.w = nextafterf(rough.w, 0); rough.x = nextafterf(rough.x, 1); }
      if (!noisy) CHECK(tfp_degrees_between(steady, rough) > .03f);
      const TfpQ at_rest = tfp_q_mul(steady, about(1, 0, 0, 0)), dipped = tfp_q_mul(steady, about(1, 0, 0, 6));
      for (int i = 0; i < 50; ++i) tfp_kick_step(&kick, noisy && (i & 1) ? rough : steady, at_rest, .002f, &tune);
      TfpQ out = tfp_q_identity();
      for (int i = 0; i < 25; ++i) out = tfp_kick_step(&kick, noisy && (i & 1) ? rough : steady, dipped, .002f, &tune); /* 50 ms into a 6 degree dip */
      sizes[noisy] = tfp_degrees_between(out, tfp_q_identity());
    }
    CHECK(sizes[0] > 4.5f && sizes[0] < 5.5f);            /* 6 degrees less 50 ms of a 300 ms settle */
    CHECK(fabsf(sizes[1] - sizes[0]) < .15f);
  }
  /* Damping (damp_ms): the kick is low-passed in time before it is returned. */
  {
    /* 0: the undamped kick, bit for bit, through a step, a turn of the view, a frame that cannot be read and a reset. */
    const TfpKickTuning plain = {.gain = .7f, .max_degrees = 8.0f, .settle_ms = 300.0f}, none = {.gain = .7f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = 0};
    TfpKick a, b; tfp_kick_reset(&a); tfp_kick_reset(&b);
    for (int i = 0; i < 240; ++i) {
      const TfpQ view = about(0, 0, 1, 37 + (i > 120 && i < 130 ? .2f*(i-120) : 0));
      TfpQ head = tfp_q_mul(view, about(1, 0, 0, pitch_at(i < 100 ? 1 : 2, i*7.0) + (i > 150 ? 3.0f*sinf(i*.3f) : 0)));
      if (i == 60) head.x = NAN;
      if (i == 200) { tfp_kick_reset(&a); tfp_kick_reset(&b); }
      const TfpQ raw = tfp_kick_raw(&a, view, head, .007f, &plain), out = tfp_kick_step(&b, view, head, .007f, &none);
      CHECK(!memcmp(&raw, &out, sizeof raw));
    }
    static const double rates[] = {30, 60, 144};
    Played step[3], brief[3];
    for (int r = 0; r < 3; ++r) {
      /* 60 % (180 ms). A 6 degree step of the head: undamped it is in the view in one frame; damped, no frame turns
       * the view by more than the time constant allows, and it ends where the undamped one ends (settled). */
      const Played raw = play(1, 0, rates[r], 4000, 350);
      step[r] = play(1, 180, rates[r], 4000, 350);
      CHECK(raw.sound && step[r].sound);
      CHECK(raw.change > 5.0f && raw.peak > 5.0f); /* less what the head's rest takes in that frame */
      CHECK(step[r].peak > 1.5f && step[r].peak < raw.peak*.6f);
      CHECK(step[r].change < 6.0f*(float)(1000/rates[r]/180)*1.02f + .01f);
      CHECK(raw.last < .02f && step[r].last < .02f);
      /* A brief movement of the head (100 ms there and back) reaches the view at well under half its undamped size. */
      const Played jerk = play(2, 0, rates[r], 1500, 250);
      brief[r] = play(2, 180, rates[r], 1500, 250);
      CHECK(jerk.sound && brief[r].sound && jerk.peak > 4.5f && brief[r].peak > .3f && brief[r].peak < jerk.peak*.45f);
    }
    for (int r = 0; r < 3; ++r) printf("damping 60 %%, %g frames a second: step peak %.2f, at 250 ms %.2f, largest change in a frame %.3f; brief peak %.2f\n", rates[r], step[r].peak, step[r].at, step[r].change, brief[r].peak);
    /* The same at every frame rate: the kick 250 ms after the step, and the peaks. */
    for (int r = 0; r < 3; ++r) {
      CHECK(step[r].at > 1.5f && fabsf(step[r].at - step[1].at) < .2f && fabsf(step[r].peak - step[1].peak) < .2f);
      CHECK(fabsf(brief[r].peak - brief[1].peak) < .2f);
    }
    /* More damping is calmer: 100 % (300 ms) passes less of the brief movement than 60 %. */
    CHECK(play(2, 300, 60, 1500, 250).peak < brief[1].peak*.8f);
    /* A reset forgets the damped kick; a frame that cannot be read gives none and the next ones are sound. */
    const TfpKickTuning damped = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = 180};
    const TfpQ view = about(0, 0, 1, 37), dipped = tfp_q_mul(view, about(1, 0, 0, 6));
    TfpKick k; tfp_kick_reset(&k);
    tfp_kick_step(&k, view, view, .016f, &damped);
    TfpQ out = tfp_q_identity();
    for (int i = 0; i < 6; ++i) out = tfp_kick_step(&k, view, dipped, .016f, &damped);
    CHECK(tfp_degrees_between(out, tfp_q_identity()) > 1.0f);
    tfp_kick_reset(&k);
    CHECK(tfp_degrees_between(tfp_kick_step(&k, view, dipped, .016f, &damped), tfp_q_identity()) == 0);
    CHECK(tfp_degrees_between(tfp_kick_step(&k, view, dipped, .016f, &damped), tfp_q_identity()) < .001f); /* the head rests there now */
    for (int i = 0; i < 6; ++i) out = tfp_kick_step(&k, view, view, .016f, &damped);
    CHECK(tfp_degrees_between(out, tfp_q_identity()) > 1.0f);
    TfpQ bad = view; bad.w = NAN;
    out = tfp_kick_step(&k, view, bad, .016f, &damped);
    CHECK(out.x == 0 && out.y == 0 && out.z == 0 && out.w == 1);
    out = tfp_kick_step(&k, view, view, .016f, &damped);
    CHECK(out.x == 0 && out.y == 0 && out.z == 0 && out.w == 1);
    const TfpKickTuning odd = {.gain = 1.0f, .max_degrees = 8.0f, .settle_ms = 300.0f, .damp_ms = NAN};
    for (int i = 0; i < 6; ++i) { out = tfp_kick_step(&k, view, i & 1 ? dipped : view, .016f, &odd); CHECK(tfp_q_valid(out) && fabsf(sqrtf(tfp_q_dot(out, out))-1) < 1e-4f); }
  }
  /* A head that turns sideways against the view (the body twisting in a strafe or a walk backwards) reaches the view
   * by a fifth of what a nod or a tilt does: a kick about the view's up axis turns the aim. Seen in the game
   * 2026-11-22: 5 to 8 degrees of kick from walking backwards and strafing, most of it sideways. */
  {
    const TfpKickTuning tune = {1.0f, 20.0f, 300.0f, 0};
    const TfpQ steady = about(0, 0, 1, 37);
    float size[3];
    const TfpQ turns[3] = {about(0, 0, 1, 10), about(1, 0, 0, 10), about(0, 1, 0, 10)}; /* sideways, a nod, a tilt */
    for (int i = 0; i < 3; ++i) {
      TfpKick kick; tfp_kick_reset(&kick);
      for (int n = 0; n < 10; ++n) tfp_kick_step(&kick, steady, steady, .016f, &tune);
      size[i] = tfp_degrees_between(tfp_kick_step(&kick, steady, tfp_q_mul(steady, turns[i]), .016f, &tune), tfp_q_identity());
    }
    CHECK(size[1] > 9 && size[1] < 10 && size[2] > 9 && size[2] < 10);
    CHECK(size[0] > 1.6f && size[0] < 2.0f);
    /* a mixed turn keeps its nod whole and loses four fifths of its sideways part */
    TfpKick kick; tfp_kick_reset(&kick);
    for (int n = 0; n < 10; ++n) tfp_kick_step(&kick, steady, steady, .016f, &tune);
    const TfpQ out = tfp_kick_step(&kick, steady, tfp_q_mul(steady, tfp_q_mul(about(0, 0, 1, 10), about(1, 0, 0, 6))), .016f, &tune);
    const TfpQ want = tfp_q_mul(about(0, 0, 1, 2), about(1, 0, 0, 6));
    CHECK(tfp_degrees_between(out, want) < 1.0f);
  }
  /* The camera style (src/camera_damp.h TfpCameraStyle; the Bodycam option). The sliders as the options' defaults:
   * 35 % up to 8 degrees on foot, 70 % up to 35 thrown, settling on 300 ms, camera damping 60 % (180 ms). */
  {
    const float gain = .35f, max = 8.0f, thrown_gain = .7f, thrown_max = 35.0f, settle = 300.0f, damping = 180.0f;
    /* Bodycam: the sliders as they are, on foot and thrown. */
    const TfpCameraStyle bodycam = tfp_camera_style(true, damping);
    const TfpKickTuning body_foot = tfp_kick_style_tuning(&bodycam, false, gain, max, thrown_gain, thrown_max, settle);
    const TfpKickTuning body_thrown = tfp_kick_style_tuning(&bodycam, true, gain, max, thrown_gain, thrown_max, settle);
    CHECK(body_foot.gain == gain && body_foot.max_degrees == max && body_foot.settle_ms == settle && body_foot.damp_ms == damping);
    CHECK(body_thrown.gain == thrown_gain && body_thrown.max_degrees == thrown_max && body_thrown.settle_ms == settle && body_thrown.damp_ms == damping);
    /* Stable: nothing on foot; thrown, half the thrown slider with the limit unchanged, low-passed on 250 ms whatever
     * the camera damping option says. */
    const TfpCameraStyle stable = tfp_camera_style(false, damping), stable_raw = tfp_camera_style(false, 0);
    const TfpKickTuning foot = tfp_kick_style_tuning(&stable, false, gain, max, thrown_gain, thrown_max, settle);
    const TfpKickTuning thrown = tfp_kick_style_tuning(&stable, true, gain, max, thrown_gain, thrown_max, settle);
    CHECK(foot.gain == 0 && foot.max_degrees == max && foot.settle_ms == settle && foot.damp_ms == 250.0f);
    CHECK(thrown.gain == .35f && thrown.max_degrees == thrown_max && thrown.settle_ms == settle && thrown.damp_ms == 250.0f);
    CHECK(tfp_kick_style_tuning(&stable_raw, true, gain, max, thrown_gain, thrown_max, settle).damp_ms == 250.0f);
    /* The head nods 20 degrees in one frame and stays (a landing, or the body thrown), 144 frames a second.
     * [0] Bodycam on foot, [1] Bodycam thrown, [2] Stable on foot, [3] Stable thrown: the largest kick, the largest
     * change of it in one frame, and whether every frame was exactly no rotation. */
    const TfpKickTuning *const cases[4] = {&body_foot, &body_thrown, &foot, &thrown};
    float peak[4] = {0}, change[4] = {0}, end[4] = {0}; bool none[4] = {true, true, true, true};
    const TfpQ view = about(0, 0, 1, 37);
    for (int c = 0; c < 4; ++c) {
      TfpKick kick; tfp_kick_reset(&kick);
      TfpQ before = tfp_q_identity();
      for (int i = 0; i < 432; ++i) { /* 3 s */
        const float nod = i >= 20 ? 20.0f : 3.0f*sinf(i*.9f); /* the head is never still before it either */
        const TfpQ out = tfp_kick_step(&kick, view, tfp_q_mul(view, about(1, 0, 0, nod)), 1.0f/144, cases[c]);
        if (out.x != 0 || out.y != 0 || out.z != 0 || out.w != 1) none[c] = false;
        const float size = tfp_degrees_between(out, tfp_q_identity()), moved = tfp_degrees_between(out, before);
        if (size > peak[c]) peak[c] = size;
        if (moved > change[c]) change[c] = moved;
        end[c] = size; before = out;
      }
    }
    printf("a 20 degree nod in one frame: Bodycam on foot %.2f degrees (%.3f in one frame), Bodycam thrown %.2f (%.3f); "
           "Stable on foot %.2f (%.3f), Stable thrown %.2f (%.3f)\n", peak[0], change[0], peak[1], change[1], peak[2], change[2], peak[3], change[3]);
    CHECK(none[2] && peak[2] == 0);                 /* Stable on foot: exactly no kick, in every frame */
    CHECK(!none[0] && peak[0] > 1.0f);              /* where Bodycam has one */
    CHECK(peak[3] > 1.0f && peak[3] < peak[1]*.5f); /* Stable thrown: still a kick, under half of Bodycam's */
    CHECK(change[3] < change[1]*.5f && change[3] < 20.0f*.35f*(1000.0f/144/250)*1.02f); /* a sway: no frame turns it more than 250 ms allows */
    CHECK(end[3] < .05f && end[1] < .05f);          /* and it settles */
    /* A thrown body that is on foot again: Stable's kick is eased out on the same 250 ms, not dropped in a frame. */
    TfpKick kick; tfp_kick_reset(&kick);
    TfpQ out = tfp_q_identity();
    for (int i = 0; i < 40; ++i) out = tfp_kick_step(&kick, view, tfp_q_mul(view, about(1, 0, 0, i >= 10 ? 20.0f : 0)), 1.0f/144, &thrown);
    const float carried = tfp_degrees_between(out, tfp_q_identity());
    CHECK(carried > 1.0f);
    float last = carried, most = 0;
    for (int i = 0; i < 288; ++i) {
      out = tfp_kick_step(&kick, view, tfp_q_mul(view, about(1, 0, 0, 20.0f + 4.0f*sinf(i*.7f))), 1.0f/144, &foot);
      const float size = tfp_degrees_between(out, tfp_q_identity());
      if (last-size > most) most = last-size;
      CHECK(size <= last+1e-4f);
      last = size;
    }
    CHECK(most < carried*(1000.0f/144/250)*1.05f && last < .01f);
    printf("Stable, thrown and then on foot: %.2f degrees carried, let go by at most %.3f in one frame\n", carried, most);
  }
  /* A change of camera style (or of the camera damping option) that switches the kick's damping off while a kick is
   * being eased: the kick shown goes on from where it was and reaches the raw one over TFP_KICK_BLEND_MS, not in one
   * frame, and is the raw one bit for bit after that. The head of a thrown body nods 10 degrees in one frame and
   * stays; 100 ms later the option changes. [0] Stable to Bodycam with camera damping 0, [1] Bodycam at 60 % to
   * Bodycam at 0, [2] the reverse of [0]: Bodycam with damping 0 to Stable, which goes on under Stable's low-pass. */
  {
    const float gain = .35f, max = 8.0f, thrown_gain = .7f, thrown_max = 35.0f, settle = 300.0f;
    const TfpCameraStyle stable = tfp_camera_style(false, 0), raw_body = tfp_camera_style(true, 0), damped_body = tfp_camera_style(true, 180);
    const TfpKickTuning stable_thrown = tfp_kick_style_tuning(&stable, true, gain, max, thrown_gain, thrown_max, settle);
    const TfpKickTuning raw_thrown = tfp_kick_style_tuning(&raw_body, true, gain, max, thrown_gain, thrown_max, settle);
    const TfpKickTuning damped_thrown = tfp_kick_style_tuning(&damped_body, true, gain, max, thrown_gain, thrown_max, settle);
    const TfpKickTuning *const from[3] = {&stable_thrown, &damped_thrown, &raw_thrown}, *const to[3] = {&raw_thrown, &raw_thrown, &stable_thrown};
    static const double rates[] = {30, 60, 144};
    const TfpQ view = about(0, 0, 1, 37);
    for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) {
      const float dt = (float)(1/rates[r]);
      const int nod_at = (int)(rates[r]/3), change_at = nod_at + (int)(rates[r]/10 + .5); /* 100 ms after the nod */
      /* `k` is the camera; `there` the camera that had the new option all along, whose head rest is the same. */
      TfpKick k, there; tfp_kick_reset(&k); tfp_kick_reset(&there);
      TfpQ before = tfp_q_identity();
      float most = 0, step_at_change = 0, apart_at_change = 0, apart_400 = 9, apart_end = 9; bool same_after = true, nearer = true;
      for (int i = 0; i < change_at + (int)(rates[r]*2); ++i) {
        const TfpQ head = tfp_q_mul(view, about(1, 0, 0, i >= nod_at ? 10.0f : 0));
        const TfpQ out = tfp_kick_step(&k, view, head, dt, i < change_at ? from[c] : to[c]);
        const TfpQ wanted = tfp_kick_step(&there, view, head, dt, to[c]);
        CHECK(tfp_q_valid(out) && fabsf(sqrtf(tfp_q_dot(out, out))-1) < 1e-4f);
        const float apart = tfp_degrees_between(out, wanted), moved = tfp_degrees_between(out, before);
        const double ms = (i-change_at+1)*1000.0/rates[r]; /* since the option changed */
        if (i == change_at-1) apart_at_change = apart;
        if (i == change_at) step_at_change = moved;
        if (i >= change_at) {
          if (moved > most) most = moved;
          if (apart > apart_end+.15f) nearer = false; /* (the raw kick moves on meanwhile, and a share of that is taken each frame) */
          if (ms >= 400 && apart_400 == 9) apart_400 = apart;
          if (ms >= 400 && c < 2 && memcmp(&out, &wanted, sizeof out)) same_after = false;
        }
        apart_end = apart; before = out;
      }
      static const char *const names[3] = {"Stable to Bodycam with damping 0", "Bodycam 60 % to damping 0", "Bodycam with damping 0 to Stable"};
      printf("%s, %g frames a second: %.2f degrees apart when the option changes, %.3f in the first frame, at most %.3f in one frame, %.4f apart after 400 ms\n",
             names[c], rates[r], apart_at_change, step_at_change, most, apart_400);
      CHECK(apart_at_change > 2.0f);           /* there was something to snap to */
      CHECK(most <= 1.5f && nearer);           /* no frame turns the view by more, and the kick does not swing away from the new one */
      if (c < 2) CHECK(apart_400 < .05f && same_after); /* the raw kick itself, bit for bit, from 400 ms on */
      else CHECK(apart_end < .05f);            /* Stable's low-pass takes it there on its 250 ms */
    }
  }
  printf("camera lock: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
