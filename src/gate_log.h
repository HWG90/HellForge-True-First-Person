/* The "Gate:" line: what the pose helper saw of the game's unzoom gate in one second, for the log only. Nothing here
 * changes what the mod does (docs/design/specs/2026-11-23-gate-flip.md).
 *
 * The game's kind-6 evaluator turns the lens towards the wide (third-person) target while either of two bytes is set:
 * the avatar's packed bit 0 (the evaluator's local at stack +0x44, frame[0] in src/pose_helper.c) or request+0x8f.
 * Seen in the game: one of them changing every frame for seconds, standing still, the rig eye going about 0.2 m back
 * and forth with it. The mod's own zoom-depth term is passed (nothing added) on every frame one of the two is set, so
 * it goes on and off with the gate. This counts, for every frame the helper writes the rig root: the two bytes and how
 * often each changed, whether the depth term was added, worked out as nothing or passed and why, the lens target and
 * the lens, and (where the helper is told to read it) the avatar's main row, whose byte +5 the game copies into the
 * packed bit.
 *
 * In the line: request+0x88 and +0x8d are as last read in the second (with the lens, before the evaluator goes on);
 * the main row's values are as they stood when the mod wrote the rig root; "nothing to add" is a frame in which the
 * term was worked out and came to nothing (the widest optic selected). The line is kept under 600 characters: the
 * longest the mod's log has carried is 628.
 *
 * The pose helper fills a TfpGateFrame per frame under its policy lock and calls tfp_gate_note; the binder takes the
 * second with tfp_gate_take and writes tfp_gate_line where tfp_gate_worth says the second is worth a line. */
#ifndef TFP_GATE_LOG_H
#define TFP_GATE_LOG_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Why the depth term added nothing in a frame: depth_pass's numbers in src/pose_helper.c, with 11 and 12 added here. */
enum { TFP_GATE_WORKED = 0,      /* the term was worked out (it can still be nothing: the widest optic) */
       TFP_GATE_FULL_AIM = 1, TFP_GATE_CODE = 2, TFP_GATE_NO_LENS = 3,
       TFP_GATE_GATED = 4,       /* request+0x8f or the avatar bit set, or the request's zoom state not one the term knows */
       TFP_GATE_RANGE = 5, TFP_GATE_NO_DATA = 6, TFP_GATE_NO_PREVIOUS = 7, TFP_GATE_TOO_FAR = 8, TFP_GATE_DATA_CHANGED = 9,
       TFP_GATE_ZOOM_POLICY = 10,
       TFP_GATE_SWITCHED_OFF = 11, /* depth_off.txt */
       TFP_GATE_NO_FRAME = 12,     /* the root was written without the evaluator's frame: the term is not reached */
       TFP_GATE_REASONS = 13 };
static const char *const TFP_GATE_REASON_TEXT[TFP_GATE_REASONS] = {
    "worked out", "full aim", "the evaluator's code is not this build's", "no lens read for the frame", "unzoom gate or request state",
    "values out of range", "weapon data not read", "previous distance not known", "over 1 m", "weapon data changed", "zoom policy",
    "depth_off.txt", "no evaluator frame"};

typedef struct TfpGateFrame {
  bool lens_read;                    /* the evaluator's lens locals and the request's zoom state were read in this frame */
  uint8_t avatar_bit, request_8f, request_8d; /* frame[0]; request+0x8f; request+0x8d */
  float request_88, target, lens;    /* request+0x88; the lens target and the lens (radians) */
  uint32_t reason;                   /* TFP_GATE_* */
  float correction;                  /* what the depth term added along the rig's forward axis (m); 0: nothing */
  bool row_asked, row_read;          /* the main row was to be read; it was */
  uint8_t row_5, row_3d; float row_11ac;
} TfpGateFrame;
typedef struct TfpGateSecond {
  uint64_t generation;               /* the lease the last frame belonged to */
  uint32_t frames, lens_frames;
  uint32_t avatar_set, avatar_changes, request_set, request_changes;
  uint32_t applied, nothing, passed[TFP_GATE_REASONS]; /* frames the term added something; worked out as nothing; passed, by reason */
  float correction_min, correction_max;          /* over the frames it added something */
  float target_min, target_max, lens_min, lens_max; /* radians, over the frames the lens was read */
  float request_88; uint8_t request_8d;          /* as last read */
  uint32_t row_asked, row_frames, row5_set, row5_changes, row3d_set; float row11ac_min, row11ac_max;
  uint32_t depth_off;                            /* the switch, as the helper has it */
} TfpGateSecond;
typedef struct TfpGateLog {
  TfpGateSecond now;
  bool have_bits, have_row; uint8_t last_avatar, last_8f, last_row5; /* the last frame's, kept across seconds: a change between two seconds counts */
} TfpGateLog;

static inline void tfp_gate_note(TfpGateLog *g, const TfpGateFrame *f) {
  TfpGateSecond *s = &g->now;
  ++s->frames;
  if (f->lens_read) {
    const uint8_t avatar = f->avatar_bit != 0, ability = f->request_8f != 0;
    if (g->have_bits) { s->avatar_changes += avatar != g->last_avatar; s->request_changes += ability != g->last_8f; }
    g->have_bits = true; g->last_avatar = avatar; g->last_8f = ability;
    s->avatar_set += avatar; s->request_set += ability;
    if (!s->lens_frames++) { s->target_min = s->target_max = f->target; s->lens_min = s->lens_max = f->lens; }
    if (f->target < s->target_min) s->target_min = f->target;
    if (f->target > s->target_max) s->target_max = f->target;
    if (f->lens < s->lens_min) s->lens_min = f->lens;
    if (f->lens > s->lens_max) s->lens_max = f->lens;
    s->request_88 = f->request_88; s->request_8d = f->request_8d;
  }
  if (f->correction != 0) {
    if (!s->applied++) s->correction_min = s->correction_max = f->correction;
    if (f->correction < s->correction_min) s->correction_min = f->correction;
    if (f->correction > s->correction_max) s->correction_max = f->correction;
  } else if (f->reason == TFP_GATE_WORKED) ++s->nothing;
  else ++s->passed[f->reason < TFP_GATE_REASONS ? f->reason : TFP_GATE_NO_FRAME];
  if (f->row_asked) ++s->row_asked;
  if (f->row_read) {
    const uint8_t five = f->row_5 != 0;
    if (g->have_row) s->row5_changes += five != g->last_row5;
    g->have_row = true; g->last_row5 = five;
    s->row5_set += five; s->row3d_set += f->row_3d != 0;
    if (!s->row_frames++) s->row11ac_min = s->row11ac_max = f->row_11ac;
    if (f->row_11ac < s->row11ac_min) s->row11ac_min = f->row_11ac;
    if (f->row_11ac > s->row11ac_max) s->row11ac_max = f->row_11ac;
  }
}
/* The second so far, and a new one begun. What the last frame's bytes were is kept. */
static inline void tfp_gate_take(TfpGateLog *g, TfpGateSecond *out) { *out = g->now; memset(&g->now, 0, sizeof g->now); }
static inline uint32_t tfp_gate_passed(const TfpGateSecond *s) {
  uint32_t n = 0; for (unsigned i = 1; i < TFP_GATE_REASONS; ++i) n += s->passed[i];
  return n;
}
/* A second is worth a line when a gate byte (or the main row's byte +5) changed in it, or the depth term added
 * something in some of its frames and was passed in others. A calm second is not. */
static inline bool tfp_gate_worth(const TfpGateSecond *s) {
  return s->frames && (s->avatar_changes || s->request_changes || s->row5_changes || (s->applied && tfp_gate_passed(s)));
}
/* The line. `entry`: it is written because a native entry began (the first frames of a lease), whatever they hold. */
static inline void tfp_gate_line(const TfpGateSecond *s, bool entry, char *text, size_t size) {
  char reasons[320] = "", lens[160], row[240], depth[96] = ""; size_t at = 0; const double deg = 57.29578;
  for (unsigned i = 1; i < TFP_GATE_REASONS && at < sizeof reasons; ++i) if (s->passed[i]) {
    const int n = snprintf(reasons+at, sizeof reasons-at, "%s%s %u", at ? ", " : " (", TFP_GATE_REASON_TEXT[i], s->passed[i]);
    if (n > 0) at += (size_t)n;
  }
  if (at && at < sizeof reasons-1) { reasons[at] = ')'; reasons[at+1] = 0; }
  if (s->applied) snprintf(depth, sizeof depth, " (%.4f to %.4f m)", (double)s->correction_min, (double)s->correction_max);
  if (s->lens_frames) snprintf(lens, sizeof lens, "lens target %.1f to %.1f degrees, lens %.1f to %.1f; request+0x88 = %.3f, +0x8d = %u",
                               s->target_min*deg, s->target_max*deg, s->lens_min*deg, s->lens_max*deg, (double)s->request_88, s->request_8d);
  else snprintf(lens, sizeof lens, "the lens was not read");
  if (s->row_frames) snprintf(row, sizeof row, "main row read in %u: +5 set in %u, changed %u times, +0x3d set in %u, +0x11ac %.4f to %.4f", s->row_frames, s->row5_set, s->row5_changes, s->row3d_set, (double)s->row11ac_min, (double)s->row11ac_max);
  else snprintf(row, sizeof row, s->row_asked ? "main row not readable" : "main row not read");
  snprintf(text, size, "Gate: %u frames of the rig root, the lens read in %u; avatar bit set in %u, changed %u times; request+0x8f set in %u, changed %u times; "
           "zoom depth added in %u%s, nothing to add in %u, passed in %u%s; %s; %s%s%s",
           s->frames, s->lens_frames, s->avatar_set, s->avatar_changes, s->request_set, s->request_changes, s->applied, depth, s->nothing,
           tfp_gate_passed(s), reasons, lens, row, s->depth_off ? "; depth_off.txt: the mod's zoom depth is forced to nothing" : "",
           entry ? "; the first second of a native entry" : "");
}
#endif
