/* THE EYE HELD THROUGH A RELOAD (round 39; docs/design/specs/2026-11-24-residual-jumps.md class C, open item 6).
 *
 * What the user saw (0.6.0-feel.56): "on some weapons, the weapon moves forward and then jerks back on reloads". What
 * the log shows: on every reload the native eye slides 0.21 to 0.26 m straight back along the view onto the body root
 * in about 75 ms, stays there for the whole reload, and slides forward the same at its end; source native, nothing
 * carried, the root still. The drawn gun is the body's (src/arm_drive.c) and stays where it is, so on screen the gun
 * goes forward and then jerks back.
 *
 * The game's kind-6 evaluator does it (game+0x1287a44..0x1287c10, build 25480438). With v the rig's seating vector
 * (the rig eye node plus the socket term plus the optic depth D, src/depth_steady.h) and C a constant vector of the
 * game's data (read at 0x1287a4c, 0x1287a55, 0x1287a67: three floats one after another), while request+0x8d is set:
 *
 *     the weight w = request+0x88 steps towards 1 (game+0x1287aa4..0x1287aeb) while it is under 1, and then
 *         request+0x8f set:    v' = (1-w) v + w C      (0x1287af6..0x1287b32: a reload begins)
 *         request+0x8f clear:  v' = w v + (1-w) C      (0x1287b34..0x1287b71: a reload ends)
 *     with the weight at 1:
 *         request+0x8f set:    v' = C                  (0x1287ba2..0x1287bb4: the reload, settled)
 *         request+0x8f clear:  v' = v
 *     and then, in every frame, the rig's vector request+0x48..0x50 = v' + C (0x1287bbf, 0x1287bdc, 0x1287beb: C is
 *     added a second time), node 5 at its forward part and the root at minus it along the view.
 *
 * So in every frame v' = (1-r) v + r C, where r (tfp_reload_share) is the share of C: nothing outside a reload, the
 * weight as the reload begins, all of it in the reload, and less of it as it ends. The eye hangs on the root, so it
 * goes r (v - C) back along the view. The mod's held depth weights its own term by 1-r (tfp_steady_gain), so it adds
 * nothing through a reload either, and the continuity filter takes nothing: the steps are the eye's own.
 *
 * What this header adds: the seating vector's forward part as it stood in the last frame in which none of C was in it
 * (r = 0: the game's vector is v, the rig's own), with what the mod itself added to node 5 in that frame (the held
 * depth): `held`. In a frame with r > 0 it adds r (held - C) to the forward part, node 5 forward and the root back,
 * the held depth's own pair (src/pose_helper.c). The eye then stands at (1-r) v + r held + C along the view: the
 * rig's own vector blended with the one it held, never C. Both ends of every blend are a vector the rig had, so
 * nothing steps: a reload cancelled half way (the game turns its blend round from wherever the weight is), a weapon
 * swapped during the reload (the new weapon's vector comes in as the old one's goes out), a weapon whose D differs.
 *
 * Only the forward part: the root's half moves along the view alone (carrier_position_locked). The game's C also moves
 * the eye up by about 2 cm at the Liberator's reload; that is left as the game has it.
 *
 * Weighted by the hip share: in full aim the drawn gun is the game's own aim pose (src/arm_weight.h), which hangs on
 * the same root, and the game's own first-person aim reload takes the eye back off the sight by r (v - C) on purpose,
 * the sight's own placement. That is left alone; between the hip and full aim it is eased by the hip share, as the
 * placement is. Never a write of request+0x8f: that byte is also the unzoom gate's switch.
 *
 * THE EDGES (round 40). While the weight is still under 1 (the 75 ms as a reload begins and as it ends) with the
 * avatar's bit set ([rsp+0x44]) and [rsp+0x45] clear, the evaluator also multiplies v' by the float at the avatar's
 * main row +0x11ac (game+0x1287b76..0x1287b9b: xmm7, xmm15, xmm8 by [rsi+r13+0x53ea5c], before C is added the second
 * time), so request+0x4c = s v' + C. The game had s from 0 to 0.31 in those frames (feel.58, 23:05:32 and 23:05:34),
 * so for two or three frames the eye went most of the way onto the root and back: 0.15 to 0.21 m, the slide the
 * matrix's reload judge found. Round 39 refused those frames. Now they are held too, in the share of the hip:
 *   - in the scaled space where s is read (main row +0x11ac, one guarded read in such a frame only) and is at least
 *     TFP_RELOAD_SCALE_LEAST: v' = (request+0x4c - C) / s, the frame's unscaled vector, and the eye is put where it
 *     would stand unscaled with the hold, (1-r) v + r held + C. The rig's own vector comes from the frame, so a weapon
 *     swapped in the reload still eases in over the blend back. Refused (and held by the offset instead) where the
 *     vector so worked out is more than (1-r) TFP_RELOAD_APART from what the hold expects of it (only that share of it
 *     is the frame's own): a read s that is not the game's would put the eye anywhere;
 *   - by the eye's offset where s is not read, or too small to divide by (the game's s is often 0 there: then
 *     request+0x4c is C and says nothing of v): the eye kept at held + C, the rig's own vector as it stood in the last
 *     frame without C. At a reload's start that is the frame before; at its end it is the vector from before the
 *     reload, so a weapon swapped in it steps by the difference at the first frame after the blend.
 * Nothing changes in a frame the game does not scale: the same numbers as round 39, to the bit.
 *
 * C is read from the game's code, never taken as a number: the nine instructions that load or multiply by it name it
 * by their rip-relative displacements, and the 460 bytes of the evaluator around them are checked first (all but
 * those displacements and five others that are not this header's: TFP_RELOAD_PATTERN). They must all name the same
 * three floats; else nothing is held. HellForge 0.5 reads the displacement where src/entity_fields.h does: from the
 * instruction, after checking the bytes before it. Header only; no state of the game is written. */
#ifndef TFP_RELOAD_HOLD_H
#define TFP_RELOAD_HOLD_H
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TFP_RELOAD_CODE_RVA  UINT32_C(0x1287a44) /* game+: the first instruction checked (cmp byte [r15+0x8d],0) */
#define TFP_RELOAD_CODE_SIZE 460u                /* to game+0x1287c10, the last store of the rig's vector */
#define TFP_RELOAD_MOST      1.0f                /* m: more than this is not a reload's vector (the held depth's own limit) */
#define TFP_RELOAD_LOGGED    4u                  /* reloads a second whose line is kept */
#define TFP_RELOAD_SCALE_LEAST .02f              /* main row +0x11ac under this: not divided by (the eye held by its offset) */
#define TFP_RELOAD_SCALE_MOST  4.0f              /* over this: not a scale of the game's (the logs have 0 to 1) */
#define TFP_RELOAD_APART     .3f                 /* m: the scaled-space vector this far from the hold's (times 1-r): s is not the game's */

/* The evaluator from game+0x1287a44, two hex digits a byte; "xxxxxxxx": a displacement that is read, not compared:
 * the nine of C (TFP_RELOAD_SITES) and five that are not this header's (the divisor at 0x1287ac0, the avatar row at
 * 0x1287b8e, the API table at 0x1287bbb, the two limits at 0x1287bd0 and 0x1287be4). */
static const char TFP_RELOAD_PATTERN[] =
    "4180bf8d00000000f3440f100dxxxxxxxxf30f1025xxxxxxxxf20f10008b48080f28f8f30f102dxxxxxxxxf3410f58ff"
    "440f28f8898d28010000f3440f108528010000450fc6ff55f3450f58c4f3450f58fbf20f1185200100000f8414010000"
    "f3410f108f88000000440f2ff10f86eb000000f30f1045a0f30f5e05xxxxxxxxf30f58c1440f2fe876050f57d2eb0841"
    "0f28d6f30f5dd04180bf8f00000000410f28def30f5cdaf3410f119788000000743e0f28c2f30f59fbf30f5905xxxxxx"
    "xx0f28cbf3410f59cff30f58f8f3440f59c30f28c2f30f5915xxxxxxxxf30f5905xxxxxxxxf3440f58c2f30f58c8eb42"
    "f30f1005xxxxxxxx0f28caf30f59c3f30f59faf3410f59cff30f58f8f3440f59c2f30f1005xxxxxxxxf30f59c3f30f58"
    "c8f30f1005xxxxxxxxf30f59c3f3440f58c0807c244400440f28f97437807c2445007530f3420f10842exxxxxxxxf30f"
    "59f8f3440f59f8f3440f59c0eb164180bf8f00000000740c410f28f9440f28fc440f28c5488b05xxxxxxxxf3440f58c5"
    "498b0c24410f28cff30f580dxxxxxxxx410f28f7488b5020f30f58f4f30f5f0dxxxxxxxxff5268f3410f58f94c8d8550"
    "010000488d9560010000f3410f117f48f3410f11774cf3450f114750";

/* Where each displacement of C is (game+), and which of its three floats the instruction takes. The instruction ends
 * with its displacement, so it addresses the four bytes after it plus the displacement. */
typedef struct TfpReloadSite { uint32_t disp; uint8_t part; } TfpReloadSite;
static const TfpReloadSite TFP_RELOAD_SITES[9] = {
    {0x1287a51, 0}, {0x1287a59, 1}, {0x1287a6b, 2}, /* the loads: movss xmm9 / xmm4 / xmm5, [rip+disp] */
    {0x1287b01, 0}, {0x1287b1d, 2}, {0x1287b25, 1}, /* the blend as a reload begins: mulss by each */
    {0x1287b38, 0}, {0x1287b59, 1}, {0x1287b69, 2}, /* the blend as it ends: movss each */
};

static inline int tfp_reload_nibble(char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; }
/* `code`: the TFP_RELOAD_CODE_SIZE bytes at game+TFP_RELOAD_CODE_RVA, as read. True: they are the pinned evaluator
 * and all nine sites name the same three floats, one after another: `*rva` is the first (game+). */
static inline bool tfp_reload_code(const uint8_t *code, uint32_t *rva) {
  if (!code || sizeof TFP_RELOAD_PATTERN-1 != 2*TFP_RELOAD_CODE_SIZE) return false;
  for (size_t i = 0; i < TFP_RELOAD_CODE_SIZE; ++i) {
    const char hi = TFP_RELOAD_PATTERN[2*i], lo = TFP_RELOAD_PATTERN[2*i+1];
    if (hi == 'x') continue;
    if (code[i] != (uint8_t)(tfp_reload_nibble(hi) << 4 | tfp_reload_nibble(lo))) return false;
  }
  uint64_t part[3] = {0, 0, 0}; bool seen[3] = {false, false, false};
  for (size_t i = 0; i < sizeof TFP_RELOAD_SITES/sizeof TFP_RELOAD_SITES[0]; ++i) {
    const TfpReloadSite *s = &TFP_RELOAD_SITES[i];
    int32_t disp; memcpy(&disp, code+(s->disp-TFP_RELOAD_CODE_RVA), 4);
    const int64_t target = (int64_t)s->disp+4+disp;
    if (target <= 0 || target > INT64_C(0xffffffff)) return false;
    if (seen[s->part] && part[s->part] != (uint64_t)target) return false; /* two instructions name two places */
    part[s->part] = (uint64_t)target; seen[s->part] = true;
  }
  if (part[0]%4 || part[1] != part[0]+4 || part[2] != part[0]+8) return false;
  *rva = (uint32_t)part[0];
  return true;
}
static inline bool tfp_reload_finite(float v) { uint32_t bits; memcpy(&bits, &v, sizeof bits); return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000); }
/* C as read: three numbers each within a few metres. */
static inline bool tfp_reload_constant_ok(const float c[3]) {
  for (int i = 0; i < 3; ++i) if (!tfp_reload_finite(c[i]) || fabsf(c[i]) > 4) return false;
  return true;
}

enum { TFP_RELOAD_NONE = 0,     /* r = 0: the game's vector is the rig's own; it is held */
       TFP_RELOAD_ADDED = 1,    /* r > 0, and the held vector put back */
       TFP_RELOAD_OFF = 2,      /* the held depth is switched off or stopped for the lease: nothing is held */
       TFP_RELOAD_NO_CODE = 3,  /* the evaluator is not the pinned one, or C could not be read */
       TFP_RELOAD_NO_HELD = 4,  /* r > 0 and no frame of this lease had r = 0 before it */
       TFP_RELOAD_SCALED = 5,   /* the game scales the whole vector by main row +0x11ac in this frame; as a reason not
                                 * to hold (round 40): neither the scaled space nor the offset could be used */
       TFP_RELOAD_RANGE = 6,    /* a value that is no number or out of range */
       TFP_RELOAD_REASONS = 7 };
static const char *const TFP_RELOAD_REASON_TEXT[TFP_RELOAD_REASONS] = {
    "nothing of the game's reload vector in the frame", "held", "the held depth is switched off or stopped for the lease",
    "the game's reload vector could not be read (the evaluator is not the pinned one)", "the reload began before a frame of the view without it",
    "the game scaled the rig by main row +0x11ac", "values out of range"};

/* The share r of C in the rig's vector this frame, as the evaluator puts it: `blend` request+0x8d, `ability`
 * request+0x8f, `bit` the avatar's packed bit (the evaluator's [rsp+0x44]), `unscaled` the byte after it ([rsp+0x45]:
 * set, the evaluator skips the scaling), `mix_before` request+0x88 when the frame began and `mix_after` as the
 * evaluator stepped it. Returns TFP_RELOAD_NONE (r = 0), TFP_RELOAD_ADDED (r > 0), TFP_RELOAD_SCALED (the game goes on
 * to scale v' by main row +0x11ac: r is still given, and may be 0) or TFP_RELOAD_RANGE (r unknown). It is
 * 1 - tfp_steady_gain where that has a gain. */
static inline uint32_t tfp_reload_share(uint8_t blend, uint8_t ability, uint8_t bit, uint8_t unscaled, float mix_before, float mix_after, float *r) {
  *r = 0;
  if (!blend) return TFP_RELOAD_NONE;
  if (!tfp_reload_finite(mix_before) || !tfp_reload_finite(mix_after) || mix_before < 0 || mix_before > 1 || mix_after < 0 || mix_after > 1) return TFP_RELOAD_RANGE;
  if (mix_before < 1) {
    *r = ability ? mix_after : 1-mix_after;
    if (bit && !unscaled) return TFP_RELOAD_SCALED; /* game+0x1287b76..0x1287b9b */
  } else *r = ability ? 1.0f : 0.0f;
  return *r > 0 ? TFP_RELOAD_ADDED : TFP_RELOAD_NONE;
}

typedef struct TfpReloadInput {
  uint8_t blend, ability, bit;    /* request+0x8d, request+0x8f, the avatar's packed bit */
  uint8_t unscaled;               /* [rsp+0x45]: set, the evaluator does not scale even with the bit set (round 40) */
  float mix_before, mix_after;    /* request+0x88 at the frame's start and as stepped */
  float argument;                 /* request+0x4c as the game wrote it: the forward part of v' + C (s v' + C where scaled) */
  float constant;                 /* C's forward part (request+0x4c's float of C); */
  bool constant_known;            /* false: C was not read (TFP_RELOAD_NO_CODE) */
  float added;                    /* what the mod adds to node 5's forward part in this frame without this (the held depth);
                                   * in a frame the game scales, what the held depth would add to it unscaled */
  float share;                    /* 0 to 1: the hip share */
  bool on;                        /* the held depth on and not stopped for the lease */
  float scale;                    /* round 40: main row +0x11ac as read in a frame the game scales */
  bool scale_known;               /* it was read */
} TfpReloadInput;
typedef struct TfpReloadHold {
  uint64_t lease;
  bool known; float held;         /* the forward part of the rig's own vector in the last frame without C, with what the mod added */
  bool active;                    /* a reload is under way: a frame with r > 0 since the last with r = 0 */
  float would, most;              /* the largest the eye would have slid in it (r (held - C)) and what was put back of it */
  uint32_t frames, missed, why;   /* its frames with r > 0; of those, not put back, and the last reason */
  uint32_t scaled, by_offset;     /* round 40: its frames the game scaled by main row +0x11ac that were held; of those, by the eye's offset */
} TfpReloadHold;
/* One reload, from its first frame with r > 0 to the next with r = 0. */
typedef struct TfpReloadEnd { bool ended; float would, most; uint32_t frames, missed, why, scaled, by_offset; } TfpReloadEnd;

/* Round 40: a frame the game scales by main row +0x11ac (TFP_RELOAD_SCALED), `r` its share of C. True: `*delta` is the
 * whole of what goes to node 5's forward part (the held depth's own unscaled term, in->added, included), `*would` how
 * far the eye would have gone from where the hold puts it, `*offset` whether it was held by the eye's offset. */
static inline bool tfp_reload_scaled(const TfpReloadHold *h, const TfpReloadInput *in, float r, float share, float *delta, float *would, bool *offset) {
  const float a = in->argument, c = in->constant, added = in->added;
  if (!tfp_reload_finite(a) || !tfp_reload_finite(added) || !(fabsf(added) <= TFP_RELOAD_MOST)) return false;
  /* the scaled space: request+0x4c = s v' + C, v' = (1-r) v + r C */
  if (in->scale_known && tfp_reload_finite(in->scale) && in->scale >= TFP_RELOAD_SCALE_LEAST && in->scale <= TFP_RELOAD_SCALE_MOST) {
    const float blended = (a-c)/in->scale; /* v': the frame's own vector, unscaled */
    bool fits = tfp_reload_finite(blended) && fabsf(blended) <= TFP_RELOAD_MOST;
    /* the hold's own expectation of it: only its (1-r) share is the frame's own vector, so it can differ by that share */
    if (fits && h->known) fits = fabsf(blended+added-((1-r)*h->held+r*c)) <= (1-r)*TFP_RELOAD_APART+1e-4f;
    if (fits && (r == 0 || h->known)) {
      const float target = blended+c+(r > 0 ? r*(h->held-c) : 0); /* where the eye stands unscaled with the hold (less the held depth's own) */
      *would = target-a; *delta = added+share*(target-a); *offset = false;
      return true;
    }
  }
  /* the eye's offset: the rig's own vector as the last frame without C had it */
  if (!h->known) return false;
  const float target = h->held+c-added;
  *would = target-a; *delta = added+share*(target-a); *offset = true;
  return true;
}

/* One frame of node 5. `*delta`: what to add to its forward part (and to take from the root along the view): 0 on
 * every frame with r = 0 the game does not scale, so such a frame is the same as without this, to the bit. In a frame
 * the game scales (round 40) it is the whole of what node 5 takes, the held depth's own term (in->added) included.
 * `*end`: filled where a reload ended with this frame. Returns a TFP_RELOAD_* reason: TFP_RELOAD_ADDED for every frame
 * held, a scaled one included. */
static inline uint32_t tfp_reload_frame(TfpReloadHold *h, const TfpReloadInput *in, uint64_t lease, float *delta, TfpReloadEnd *end) {
  *delta = 0; memset(end, 0, sizeof *end);
  if (lease != h->lease) { memset(h, 0, sizeof *h); h->lease = lease; }
  float r = 0;
  uint32_t why = tfp_reload_share(in->blend, in->ability, in->bit, in->unscaled, in->mix_before, in->mix_after, &r);
  if (why == TFP_RELOAD_NONE) {
    if (h->active) { end->ended = true; end->would = h->would; end->most = h->most; end->frames = h->frames; end->missed = h->missed; end->why = h->why;
                     end->scaled = h->scaled; end->by_offset = h->by_offset; }
    h->active = false; h->would = h->most = 0; h->frames = h->missed = 0; h->why = TFP_RELOAD_NONE; h->scaled = h->by_offset = 0;
    if (!in->on) { h->known = false; return TFP_RELOAD_OFF; }
    const float held = in->argument-(in->constant_known ? in->constant : 0)+in->added;
    if (in->constant_known && tfp_reload_finite(held) && fabsf(held) <= 4) { h->held = held; h->known = true; }
    else h->known = false;
    return TFP_RELOAD_NONE;
  }
  if (!h->active) { h->active = true; h->would = h->most = 0; h->frames = h->missed = 0; h->why = TFP_RELOAD_NONE; h->scaled = h->by_offset = 0; }
  ++h->frames;
  if (why == TFP_RELOAD_ADDED || why == TFP_RELOAD_SCALED) {
    const uint32_t kind = why;
    if (!in->on) why = TFP_RELOAD_OFF;
    else if (!in->constant_known) why = TFP_RELOAD_NO_CODE;
    else if (!h->known && (kind == TFP_RELOAD_ADDED || r > 0)) why = TFP_RELOAD_NO_HELD;
    else if (!tfp_reload_finite(in->share) || !tfp_reload_finite(in->constant)) why = TFP_RELOAD_RANGE;
    if (why == kind && kind == TFP_RELOAD_SCALED) {
      const float share = in->share < 0 ? 0 : in->share > 1 ? 1 : in->share;
      float out = 0, would = 0; bool offset = false;
      if (!tfp_reload_scaled(h, in, r, share, &out, &would, &offset)) { ++h->missed; h->why = TFP_RELOAD_SCALED; return TFP_RELOAD_SCALED; }
      if (!(fabsf(out) <= TFP_RELOAD_MOST) || !(fabsf(would) <= TFP_RELOAD_MOST)) { ++h->missed; h->why = TFP_RELOAD_RANGE; return TFP_RELOAD_RANGE; }
      if (fabsf(would) > fabsf(h->would)) h->would = would;
      if (fabsf(out-in->added) > fabsf(h->most)) h->most = out-in->added;
      ++h->scaled; h->by_offset += offset;
      *delta = out;
      return TFP_RELOAD_ADDED;
    }
  }
  if (why != TFP_RELOAD_ADDED) { ++h->missed; h->why = why; return why; }
  const float share = in->share < 0 ? 0 : in->share > 1 ? 1 : in->share;
  const float would = r*(h->held-in->constant), add = share*would;
  if (!(fabsf(add) <= TFP_RELOAD_MOST)) { ++h->missed; h->why = TFP_RELOAD_RANGE; return TFP_RELOAD_RANGE; }
  if (fabsf(would) > fabsf(h->would)) h->would = would;
  if (fabsf(add) > fabsf(h->most)) h->most = add;
  *delta = add;
  return TFP_RELOAD_ADDED;
}

/* ---- the "Reload:" line: once a reload, in the second it ended ---- */
typedef struct TfpReloadSecond { uint32_t count; TfpReloadEnd ends[TFP_RELOAD_LOGGED]; } TfpReloadSecond;
static inline void tfp_reload_note(TfpReloadSecond *s, const TfpReloadEnd *end) {
  if (!end->ended) return;
  if (s->count < TFP_RELOAD_LOGGED) s->ends[s->count] = *end;
  ++s->count;
}
/* The line for the `i`th reload of the second. "Held" where every frame of it was put back. Round 40: where frames the
 * game scaled by main row +0x11ac were held, how many, and how many of them by the eye's offset. */
static inline void tfp_reload_line(const TfpReloadEnd *e, char *text, size_t size) {
  char scaled[120] = "";
  if (e->scaled) snprintf(scaled, sizeof scaled, "; %u frames the game scaled by main row +0x11ac held, %u of them by the eye's offset", e->scaled, e->by_offset);
  if (!e->missed)
    snprintf(text, size, "Reload: eye held through the reload (would have slid %.3f m)%s", (double)fabsf(e->would), scaled);
  else if (e->missed == e->frames)
    snprintf(text, size, "Reload: eye not held through the reload (%u frames): %s", e->frames,
             TFP_RELOAD_REASON_TEXT[e->why < TFP_RELOAD_REASONS ? e->why : TFP_RELOAD_RANGE]);
  else
    snprintf(text, size, "Reload: eye held through the reload (would have slid %.3f m), but not in %u of its %u frames: %s%s", (double)fabsf(e->would),
             e->missed, e->frames, TFP_RELOAD_REASON_TEXT[e->why < TFP_RELOAD_REASONS ? e->why : TFP_RELOAD_RANGE], scaled);
}
#endif
