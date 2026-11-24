/* THE RIG'S DEPTH, HELD STEADY (docs/design/specs/2026-11-23-gate-flip.md, "Round 9").
 *
 * The game's kind-6 evaluator puts the first-person rig's root at -rotate(q, +Y*y) and the rig's node 5 at +y on the
 * rig's forward axis, and a part of y is a depth D it blends between two distances of the request
 * (game+0x12873b3..0x1287423):
 *
 *     span = |target - request+0xa0|
 *     D    = request+0x90                                   where span is nothing
 *     D    = (1-t)*request+0x90 + t*request+0x9c            else, t = min(1, |target - lens| / span)
 *
 * `target` is the lens target, which the unzoom gate has just switched (the weapon's own while the gate is clear, the
 * wide one while it is set); `lens` is the lens on its way there; +0x90 is the distance of the selected optic, +0x9c
 * the one before, +0xa0 the lens when the two were set. With the lens on its target t is 0 and D is +0x90, gate set or
 * clear: the two settled places are the same place. Only on the way does D differ, and a gate that changes throws the
 * target to the other end, t jumps, and D with it: the eye (which hangs on the root) goes forward or back along the
 * view in one frame and creeps home as the lens travels. A gate that flips every frame does that every frame.
 *
 * In the hip view the mod locks the shown field of view, so the lens the depth follows is not shown at all. This
 * header says what to add to the game's y so that the depth is the settled distance instead, on every frame of the
 * hip view: the game's own D worked out as the game does (tfp_steady_depth), how much of it reaches the rig
 * (tfp_steady_gain), and a held distance that begins at the game's own D when a lease begins and goes to the settled
 * one in a critically damped ease of its own (nine tenths in 216 ms), again whenever the settled distance
 * changes (tfp_steady_held). What is added is share * gain * (held - D): nothing, to the bit, while the lens rests on
 * its target. `share` is the hip share (1 at the hip, 0 in full aim, eased by the caller), so in full aim the game's
 * own depth is shown whole and the hand-over has no step.
 *
 * It is keyed on nothing but the game's own numbers of the same evaluation: not on the gate bit, not on a motion
 * pattern. It cannot flatten recoil or any other motion: those are other parts of y and pass through.
 *
 * Not undone: in the 75 ms after a reload state changed (request+0x8d set, the weight at +0x88 under 1) with the gate
 * set, the game scales the rig's whole vector by the float at main row +0x11ac (game+0x1287b76). That is passed
 * (TFP_STEADY_SCALED) and counted.
 *
 * The second half is the "Steady:" line's counting, as src/gate_log.h does it for "Gate:". */
#ifndef TFP_DEPTH_STEADY_H
#define TFP_DEPTH_STEADY_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TFP_STEADY_RATE 18.0        /* 1/s: nine tenths of the way in 216 ms, at rest in about 520; the game's own lens takes 170 to 260 ms over 30 degrees */
#define TFP_STEADY_GAP_MS 250.0     /* longer without a frame: begin again at the game's own depth */
#define TFP_STEADY_MOST 1.0f        /* m: a shift over this is not added (the zoom depth term's own limit); its ranges alone would allow 8 */
#define TFP_STEADY_ALONE 2u         /* frames running in which node 5 was moved and the root did not follow: the held depth stops for the lease */
#define TFP_STEADY_REST .0002f      /* m: this near the settled distance ... */
#define TFP_STEADY_REST_SPEED .005f /* m/s: ... and this slow, the held distance is the settled one, to the bit */

enum { TFP_STEADY_WORKED = 0, /* the shift was worked out (it is nothing while the lens rests on its target) */
       TFP_STEADY_OFF = 1,    /* depth_steady_off.txt */
       TFP_STEADY_NO_LENS = 2, /* the evaluator's lens was not read for this frame, or the request changed under it */
       TFP_STEADY_RANGE = 3,  /* a value that is no number or out of range */
       TFP_STEADY_SCALED = 4, /* the game scales the rig's vector by main row +0x11ac in this frame */
       TFP_STEADY_NO_CODE = 5, /* the evaluator's code is not the pinned one: its stack locals are not read as a lens */
       TFP_STEADY_NO_ROOT = 6, /* stopped for the lease: the root did not follow node 5 (TFP_STEADY_ALONE) */
       TFP_STEADY_REASONS = 7 };
static const char *const TFP_STEADY_REASON_TEXT[TFP_STEADY_REASONS] = {
    "worked out", "depth_steady_off.txt", "no lens read for the frame", "values out of range", "the game scales the rig by main row +0x11ac",
    "the evaluator's code is not the pinned one", "the root did not follow"};

typedef struct TfpSteadyInput {
  float target, lens, prior;     /* radians: the evaluator's lens target and lens; request+0xa0 */
  float current, previous;       /* m: request+0x90, request+0x9c */
  float settled;                 /* m: the distance to rest at (request+0x90, or the mod's own reference for the optic) */
  uint8_t bit, blend, ability;   /* the avatar's packed bit 0; request+0x8d; request+0x8f */
  float mix_before, mix_after;   /* request+0x88 when the lens was read, and now (the evaluator has stepped it in between) */
  float share;                   /* 0 to 1: the hip share */
  bool on;                       /* false: depth_steady_off.txt */
} TfpSteadyInput;
typedef struct TfpSteady { bool known; uint64_t lease; double tick; float held, speed; } TfpSteady;

/* A finite number, told by its bits: these values are the game's memory, and comparing a NaN inside the game's own
 * call would leave the processor's invalid-operation flag set behind it (as src/gate_log.h says). */
static inline bool tfp_steady_finite(float v) { uint32_t bits; memcpy(&bits, &v, sizeof bits); return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000); }
static inline bool tfp_steady_within(float v, float least, float most) { return tfp_steady_finite(v) && v >= least && v <= most; }

/* The game's own D for the frame. False: a value it cannot be worked out from (the ranges are the zoom depth term's). */
static inline bool tfp_steady_depth(const TfpSteadyInput *in, float *depth) {
  if (!tfp_steady_within(in->target, .0002f, 2.96f) || !tfp_steady_within(in->lens, .0002f, 2.96f) || !tfp_steady_within(in->prior, 0, 2.96f)
      || !tfp_steady_within(in->current, -4, 4) || !tfp_steady_within(in->previous, -4, 4)) return false;
  const float span = fabsf(in->target-in->prior);
  if (!(span > 1.1920928955078125e-7f)) { *depth = in->current; return true; }
  float t = fabsf(in->target-in->lens)/span;
  if (t > 1) t = 1;
  const float keep = 1-t, rest = 1-keep; /* as the game: 1-(1-t), not t */
  *depth = rest*in->previous+keep*in->current;
  return true;
}
/* How much of D is in the rig's vector this frame (game+0x1287a44..0x1287bb4). False: the weights are no numbers, or
 * it is a frame in which the game goes on to scale the whole vector by main row +0x11ac. */
static inline bool tfp_steady_gain(const TfpSteadyInput *in, float *gain) {
  if (!in->blend) { *gain = 1; return true; }
  if (!tfp_steady_within(in->mix_before, 0, 1) || !tfp_steady_within(in->mix_after, 0, 1)) return false;
  if (in->mix_before < 1) { /* the blend runs, with the weight as stepped */
    if (in->bit) return false;
    *gain = in->ability ? 1-in->mix_after : in->mix_after;
    return true;
  }
  *gain = in->ability ? 0 : 1;
  return true;
}
/* The held distance after this frame. A new lease, a pause, time going back: it begins at the game's own depth (what
 * is added is then nothing, so there is no step into it). Else a critically damped ease towards `settled`, worked out
 * exactly for the time since the last frame, whatever the frame time. The same instant again changes nothing. */
static inline float tfp_steady_held(TfpSteady *s, uint64_t lease, double tick, float depth, float settled) {
  if (!s->known || lease != s->lease || tick < s->tick || tick-s->tick > TFP_STEADY_GAP_MS) {
    s->known = true; s->lease = lease; s->tick = tick; s->held = depth; s->speed = 0;
    return s->held;
  }
  const double dt = (tick-s->tick)*.001;
  s->tick = tick;
  if (dt > 0 && (s->held != settled || s->speed != 0)) {
    const double away = (double)s->held-(double)settled, part = ((double)s->speed+TFP_STEADY_RATE*away)*dt, fade = exp(-TFP_STEADY_RATE*dt);
    s->speed = (float)(((double)s->speed-TFP_STEADY_RATE*part)*fade);
    s->held = (float)((double)settled+(away+part)*fade);
    if (fabsf(s->held-settled) < TFP_STEADY_REST && fabsf(s->speed) < TFP_STEADY_REST_SPEED) { s->held = settled; s->speed = 0; }
  }
  return s->held;
}
/* One frame. Returns a TFP_STEADY_* reason; `*delta` is what to add to the rig's node 5 on its forward axis (and to
 * take from the root along the view's forward), 0 on every pass. `*depth` is the game's own D where it could be
 * worked out (0 else), `*held` the held distance where the shift was worked out. A pass leaves the state as it was.
 * A shift of more than TFP_STEADY_MOST is a pass ("values out of range"): each value can be in its own range and the
 * two distances still 8 m apart. */
static inline uint32_t tfp_steady_frame(TfpSteady *s, const TfpSteadyInput *in, uint64_t lease, double tick, float *delta, float *depth, float *held) {
  float gain = 0;
  *delta = 0; *depth = 0; *held = 0;
  if (!tfp_steady_depth(in, depth)) { *depth = 0; return TFP_STEADY_RANGE; }
  if (!in->on) { s->known = false; return TFP_STEADY_OFF; }
  if (!tfp_steady_within(in->settled, -4, 4) || !tfp_steady_within(in->share, -1, 2)
      || (in->blend && (!tfp_steady_within(in->mix_before, 0, 1) || !tfp_steady_within(in->mix_after, 0, 1)))) return TFP_STEADY_RANGE;
  if (!tfp_steady_gain(in, &gain)) return TFP_STEADY_SCALED;
  const float share = in->share < 0 ? 0 : in->share > 1 ? 1 : in->share;
  TfpSteady before; memcpy(&before, s, sizeof before);
  *held = tfp_steady_held(s, lease, tick, *depth, in->settled);
  *delta = share*gain*(*held-*depth);
  if (!(fabsf(*delta) <= TFP_STEADY_MOST)) { /* more than the zoom depth term itself would ever add: not a distance of an optic */
    memcpy(s, &before, sizeof before); *delta = 0; *held = 0;
    return TFP_STEADY_RANGE;
  }
  return TFP_STEADY_WORKED;
}

/* ---- the pair: node 5 never keeps moving without its root ----
 * The shift is two writes of one evaluation: node 5 forward, the root back along the view. The root's is a second
 * call of the game's with proofs of its own, and it can be refused or not come at all; node 5 then moves alone and
 * the gun goes against the eye, which is worse than holding nothing. So it is counted: a frame in which node 5's
 * float was changed owes a root, and after TFP_STEADY_ALONE such frames running without one the held depth stops for
 * the lease. A frame in which the root followed begins the count again; one in which nothing was moved changes
 * nothing. Only a new lease begins it again. */
typedef struct TfpSteadyPair { uint64_t lease; uint32_t alone; bool owed, stopped; } TfpSteadyPair;
/* The beginning of a frame of node 5, before anything is worked out. True: stopped for this lease, nothing is to be
 * added. `*now`: it stopped with this frame (to be said once). */
static inline bool tfp_steady_pair_frame(TfpSteadyPair *p, uint64_t lease, bool *now) {
  *now = false;
  if (lease != p->lease) { memset(p, 0, sizeof *p); p->lease = lease; }
  if (p->owed) {
    p->owed = false;
    if (++p->alone >= TFP_STEADY_ALONE && !p->stopped) { p->stopped = true; *now = true; }
  }
  return p->stopped;
}
/* Node 5's float was changed by the held depth in this frame. */
static inline void tfp_steady_pair_moved(TfpSteadyPair *p) { p->owed = true; }
/* The root's half of the same frame was done. */
static inline void tfp_steady_pair_root(TfpSteadyPair *p) { p->owed = false; p->alone = 0; }

/* ---- the "Steady:" line: one second of it ---- */
typedef struct TfpSteadyRange { uint32_t count; float least, most; } TfpSteadyRange;
typedef struct TfpSteadySecond {
  uint64_t lease;                          /* the lease the last frame belonged to */
  uint32_t frames, worked;                 /* frames of the rig's node 5; of those, the frames the game's depth was worked out in */
  uint32_t shifted, roots;                 /* frames node 5's float was changed; of those, the frames the root's half was done in */
  uint32_t passed[TFP_STEADY_REASONS];
  TfpSteadyRange gated, clear;             /* the game's own depth, with the gate bit set and clear */
  TfpSteadyRange held, delta;              /* the held distance; what node 5's float took, over the frames it was changed */
  TfpSteadyRange argument_gated, argument_clear; /* the game's own forward value for node 5 (request+0x4c), all of it */
  uint32_t stops; uint64_t stop_lease;     /* the held depth stopped for a lease in this second (the root did not follow), and which */
  uint32_t off;                            /* the switch, as the helper has it */
} TfpSteadySecond;
typedef struct TfpSteadyLog { TfpSteadySecond now; } TfpSteadyLog;

static inline void tfp_steady_range(TfpSteadyRange *r, float v) {
  if (!tfp_steady_finite(v)) return;
  if (!r->count++) r->least = r->most = v;
  if (v < r->least) r->least = v;
  if (v > r->most) r->most = v;
}
/* One frame of the rig's node 5. `depth`, `held`, `delta` as tfp_steady_frame gave them; `argument` the game's own
 * forward value for the node. A shift worked out is not yet a frame moved: tfp_steady_moved says that. */
static inline void tfp_steady_note(TfpSteadyLog *g, uint32_t reason, bool bit, float depth, float held, float delta, float argument) {
  (void)delta;
  TfpSteadySecond *s = &g->now;
  if (reason >= TFP_STEADY_REASONS) reason = TFP_STEADY_NO_LENS;
  ++s->frames;
  if (reason != TFP_STEADY_WORKED) ++s->passed[reason];
  if (reason == TFP_STEADY_NO_LENS || reason == TFP_STEADY_RANGE || reason == TFP_STEADY_NO_CODE) return; /* the gate bit is not known either */
  ++s->worked;
  tfp_steady_range(bit ? &s->gated : &s->clear, depth);
  tfp_steady_range(bit ? &s->argument_gated : &s->argument_clear, argument);
  if (reason != TFP_STEADY_WORKED) return;
  tfp_steady_range(&s->held, held);
}
/* The root's half was done in this frame: the same was taken from it along the view (to the float). */
static inline void tfp_steady_root(TfpSteadyLog *g) { ++g->now.roots; }
/* Node 5's float was changed in this frame, by `added` (what the float took of the shift). Only such a frame asks
 * for the root's half, so "moved" and "its root with it" count the same frames. */
static inline void tfp_steady_moved(TfpSteadyLog *g, float added) { ++g->now.shifted; tfp_steady_range(&g->now.delta, added); }
/* The held depth stopped for `lease` with this frame (tfp_steady_pair_frame). */
static inline void tfp_steady_stopped(TfpSteadyLog *g, uint64_t lease) { ++g->now.stops; g->now.stop_lease = lease; }
/* The line that says it, once: true where the second holds a stop. */
static inline bool tfp_steady_stop_line(const TfpSteadySecond *s, char *text, size_t size) {
  if (!s->stops) return false;
  snprintf(text, size, "Steady: stopped for lease %llu: the rig was moved in %u frames running and its root did not follow, so the gun moved against the eye; "
           "nothing more is held in this lease, and it begins again at the next", (unsigned long long)s->stop_lease, TFP_STEADY_ALONE);
  return true;
}
static inline void tfp_steady_take(TfpSteadyLog *g, TfpSteadySecond *out) { *out = g->now; memset(&g->now, 0, sizeof g->now); }
static inline uint32_t tfp_steady_passed(const TfpSteadySecond *s) {
  uint32_t n = 0; for (unsigned i = 1; i < TFP_STEADY_REASONS; ++i) n += s->passed[i];
  return n;
}
/* Worth a line: something was added, or a frame was passed for its values or for the game's scaling, or more than two
 * frames went by without the evaluator's lens (one or two are a lease beginning) or without the pinned code, or the
 * held depth stopped for a lease, or (switched off, or stopped for the lease) the game's own depth moved by more than
 * a millimetre, which is what would have been held. A calm second is not. */
static inline bool tfp_steady_worth(const TfpSteadySecond *s) {
  if (!s->frames) return false;
  if (s->shifted || s->stops || s->passed[TFP_STEADY_RANGE] || s->passed[TFP_STEADY_SCALED] || s->passed[TFP_STEADY_NO_LENS] > 2 || s->passed[TFP_STEADY_NO_CODE] > 2) return true; /* more than two frames without the lens: it is not being held, and the log has to say so */
  if ((!s->off && !s->passed[TFP_STEADY_NO_ROOT]) || !(s->gated.count+s->clear.count)) return false;
  float least = s->gated.count ? s->gated.least : s->clear.least, most = s->gated.count ? s->gated.most : s->clear.most;
  if (s->clear.count && s->clear.least < least) least = s->clear.least;
  if (s->clear.count && s->clear.most > most) most = s->clear.most;
  return most-least > .001f;
}
static inline void tfp_steady_span(const TfpSteadyRange *r, const char *after, const char *none, bool count, char *text, size_t size) {
  if (!r->count) snprintf(text, size, "%s", none);
  else if (count) snprintf(text, size, "%.4f to %.4f m %s (%u%s)", (double)r->least, (double)r->most, after, r->count, after[0] == 'w' ? " frames" : "");
  else snprintf(text, size, "%.4f to %.4f m %s", (double)r->least, (double)r->most, after);
}
static inline void tfp_steady_line(const TfpSteadySecond *s, char *text, size_t size) {
  char gated[80], clear[64], held[64] = "nothing held", by[64] = "", argument_gated[64], argument_clear[64], reasons[360] = ""; size_t at = 0;
  tfp_steady_span(&s->gated, "with the gate set", "not seen with the gate set", true, gated, sizeof gated);
  tfp_steady_span(&s->clear, "clear", "not seen clear", true, clear, sizeof clear);
  tfp_steady_span(&s->argument_gated, "gated", "not seen gated", false, argument_gated, sizeof argument_gated);
  tfp_steady_span(&s->argument_clear, "clear", "not seen clear", false, argument_clear, sizeof argument_clear);
  if (s->held.count) snprintf(held, sizeof held, "held at %.4f to %.4f m", (double)s->held.least, (double)s->held.most);
  if (s->delta.count) snprintf(by, sizeof by, ", by %.4f to %.4f m", (double)s->delta.least, (double)s->delta.most);
  for (unsigned i = 1; i < TFP_STEADY_REASONS && at < sizeof reasons; ++i) if (s->passed[i]) {
    const int n = snprintf(reasons+at, sizeof reasons-at, "%s%s %u", at ? ", " : " (", TFP_STEADY_REASON_TEXT[i], s->passed[i]);
    if (n > 0) at += (size_t)n;
  }
  if (at && at < sizeof reasons-1) { reasons[at] = ')'; reasons[at+1] = 0; }
  snprintf(text, size, "Steady: %u frames of the rig, the game's depth worked out in %u: %s, %s; %s; the rig moved to it in %u frames, its root with it in %u%s; "
           "the rig's depth argument %s, %s; passed in %u%s%s",
           s->frames, s->worked, gated, clear, held, s->shifted, s->roots, by, argument_gated, argument_clear, tfp_steady_passed(s), reasons,
           s->off ? "; depth_steady_off.txt: nothing is held" : "");
}
#endif
