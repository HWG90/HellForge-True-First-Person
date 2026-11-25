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
       TFP_RELOAD_SCALED = 5,   /* the game scales the whole vector by main row +0x11ac in this frame (src/depth_steady.h) */
       TFP_RELOAD_RANGE = 6,    /* a value that is no number or out of range */
       TFP_RELOAD_REASONS = 7 };
static const char *const TFP_RELOAD_REASON_TEXT[TFP_RELOAD_REASONS] = {
    "nothing of the game's reload vector in the frame", "held", "the held depth is switched off or stopped for the lease",
    "the game's reload vector could not be read (the evaluator is not the pinned one)", "the reload began before a frame of the view without it",
    "the game scaled the rig by main row +0x11ac", "values out of range"};

/* The share r of C in the rig's vector this frame, as the evaluator puts it: `blend` request+0x8d, `ability`
 * request+0x8f, `bit` the avatar's packed bit (the evaluator's [rsp+0x44]), `mix_before` request+0x88 when the frame
 * began and `mix_after` as the evaluator stepped it. Returns TFP_RELOAD_NONE (r = 0), TFP_RELOAD_ADDED (r > 0), or
 * TFP_RELOAD_SCALED / TFP_RELOAD_RANGE (r unknown or not this alone). It is 1 - tfp_steady_gain. */
static inline uint32_t tfp_reload_share(uint8_t blend, uint8_t ability, uint8_t bit, float mix_before, float mix_after, float *r) {
  *r = 0;
  if (!blend) return TFP_RELOAD_NONE;
  if (!tfp_reload_finite(mix_before) || !tfp_reload_finite(mix_after) || mix_before < 0 || mix_before > 1 || mix_after < 0 || mix_after > 1) return TFP_RELOAD_RANGE;
  if (mix_before < 1) {
    if (bit) return TFP_RELOAD_SCALED;
    *r = ability ? mix_after : 1-mix_after;
  } else *r = ability ? 1.0f : 0.0f;
  return *r > 0 ? TFP_RELOAD_ADDED : TFP_RELOAD_NONE;
}

typedef struct TfpReloadInput {
  uint8_t blend, ability, bit;    /* request+0x8d, request+0x8f, the avatar's packed bit */
  float mix_before, mix_after;    /* request+0x88 at the frame's start and as stepped */
  float argument;                 /* request+0x4c as the game wrote it: the forward part of v' + C */
  float constant;                 /* C's forward part (request+0x4c's float of C); */
  bool constant_known;            /* false: C was not read (TFP_RELOAD_NO_CODE) */
  float added;                    /* what the mod adds to node 5's forward part in this frame without this (the held depth) */
  float share;                    /* 0 to 1: the hip share */
  bool on;                        /* the held depth on and not stopped for the lease */
} TfpReloadInput;
typedef struct TfpReloadHold {
  uint64_t lease;
  bool known; float held;         /* the forward part of the rig's own vector in the last frame without C, with what the mod added */
  bool active;                    /* a reload is under way: a frame with r > 0 since the last with r = 0 */
  float would, most;              /* the largest the eye would have slid in it (r (held - C)) and what was put back of it */
  uint32_t frames, missed, why;   /* its frames with r > 0; of those, not put back, and the last reason */
} TfpReloadHold;
/* One reload, from its first frame with r > 0 to the next with r = 0. */
typedef struct TfpReloadEnd { bool ended; float would, most; uint32_t frames, missed, why; } TfpReloadEnd;

/* One frame of node 5. `*delta`: what to add to its forward part (and to take from the root along the view): 0 on
 * every frame with r = 0, so such a frame is the same as without this, to the bit. `*end`: filled where a reload ended
 * with this frame. Returns a TFP_RELOAD_* reason. */
static inline uint32_t tfp_reload_frame(TfpReloadHold *h, const TfpReloadInput *in, uint64_t lease, float *delta, TfpReloadEnd *end) {
  *delta = 0; memset(end, 0, sizeof *end);
  if (lease != h->lease) { memset(h, 0, sizeof *h); h->lease = lease; }
  float r = 0;
  uint32_t why = tfp_reload_share(in->blend, in->ability, in->bit, in->mix_before, in->mix_after, &r);
  if (why == TFP_RELOAD_NONE) {
    if (h->active) { end->ended = true; end->would = h->would; end->most = h->most; end->frames = h->frames; end->missed = h->missed; end->why = h->why; }
    h->active = false; h->would = h->most = 0; h->frames = h->missed = 0; h->why = TFP_RELOAD_NONE;
    if (!in->on) { h->known = false; return TFP_RELOAD_OFF; }
    const float held = in->argument-(in->constant_known ? in->constant : 0)+in->added;
    if (in->constant_known && tfp_reload_finite(held) && fabsf(held) <= 4) { h->held = held; h->known = true; }
    else h->known = false;
    return TFP_RELOAD_NONE;
  }
  if (!h->active) { h->active = true; h->would = h->most = 0; h->frames = h->missed = 0; h->why = TFP_RELOAD_NONE; }
  ++h->frames;
  if (why == TFP_RELOAD_ADDED) {
    if (!in->on) why = TFP_RELOAD_OFF;
    else if (!in->constant_known) why = TFP_RELOAD_NO_CODE;
    else if (!h->known) why = TFP_RELOAD_NO_HELD;
    else if (!tfp_reload_finite(in->share) || !tfp_reload_finite(in->constant)) why = TFP_RELOAD_RANGE;
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
/* The line for the `i`th reload of the second. "Held" where every frame of it was put back. */
static inline void tfp_reload_line(const TfpReloadEnd *e, char *text, size_t size) {
  if (!e->missed)
    snprintf(text, size, "Reload: eye held through the reload (would have slid %.3f m)", (double)fabsf(e->would));
  else if (e->missed == e->frames)
    snprintf(text, size, "Reload: eye not held through the reload (%u frames): %s", e->frames,
             TFP_RELOAD_REASON_TEXT[e->why < TFP_RELOAD_REASONS ? e->why : TFP_RELOAD_RANGE]);
  else
    snprintf(text, size, "Reload: eye held through the reload (would have slid %.3f m), but not in %u of its %u frames: %s", (double)fabsf(e->would),
             e->missed, e->frames, TFP_RELOAD_REASON_TEXT[e->why < TFP_RELOAD_REASONS ? e->why : TFP_RELOAD_RANGE]);
}
#endif
