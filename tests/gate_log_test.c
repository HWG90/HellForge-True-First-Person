/* The "Gate:" line (src/gate_log.h): what is counted, when a second is worth a line, and the words. */
#include "gate_log.h"
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool same(const char *a, const char *b) { if (strcmp(a, b)) printf("  got:  %s\n  want: %s\n", a, b); return !strcmp(a, b); }
/* A frame of the Scythe as the soak of 2026-11-23 has it (03:13:50 to 03:13:53): the lens target 60 degrees on a clear
 * frame and 90 on a gated one, the lens 63.8 and 67.6. Which byte it was, what the depth term added and what the main
 * row held are made up: the log of that build does not say. */
static TfpGateFrame scythe(bool gated, bool by_request, float depth) {
  TfpGateFrame f; memset(&f, 0, sizeof f);
  f.lens_read = true; f.avatar_bit = gated && !by_request; f.request_8f = gated && by_request; f.request_88 = 1;
  f.target = gated ? 1.5707964f : 1.0471976f; f.lens = gated ? 1.1138f : 1.18052f;
  f.reason = gated ? TFP_GATE_GATED : TFP_GATE_WORKED; f.correction = gated ? 0 : depth;
  return f;
}
int main(void) {
  TfpGateLog g; TfpGateSecond s; char text[1024];
  /* A calm second: every frame clear and the depth term added. Not worth a line. */
  memset(&g, 0, sizeof g);
  for (int i = 0; i < 30; ++i) { const TfpGateFrame f = scythe(false, false, -.2f); tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.frames == 30 && s.lens_frames == 30 && s.applied == 30 && s.avatar_changes == 0 && tfp_gate_passed(&s) == 0 && !tfp_gate_worth(&s));
  /* The avatar bit set every other frame: 30 frames, 15 set; 29 changes inside the second and one from the second
   * before (the last frame's byte is kept across the take). */
  for (int i = 0; i < 30; ++i) { const TfpGateFrame f = scythe((i & 1) == 0, false, i%4 == 1 ? -.2010f : -.1995f); tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.frames == 30 && s.avatar_set == 15 && s.avatar_changes == 30 && s.request_set == 0 && s.request_changes == 0);
  CHECK(s.applied == 15 && s.nothing == 0 && s.passed[TFP_GATE_GATED] == 15 && tfp_gate_passed(&s) == 15 && tfp_gate_worth(&s));
  tfp_gate_line(&s, false, text, sizeof text);
  CHECK(same(text, "Gate: 30 frames of the rig root, the lens read in 30; avatar bit set in 15, changed 30 times; request+0x8f set in 0, changed 0 times; "
                   "zoom depth added in 15 (-0.2010 to -0.1995 m), nothing to add in 0, passed in 15 (unzoom gate or request state 15); "
                   "lens target 60.0 to 90.0 degrees, lens 63.8 to 67.6; request+0x88 = 1.000, +0x8d = 0; main row not read"));
  /* The same by request+0x8f, the depth term worked out as nothing on the clear frames (the widest optic selected) and
   * the main row read: +5 steady, so the row is not what flips. */
  memset(&g, 0, sizeof g);
  for (int i = 0; i < 30; ++i) { TfpGateFrame f = scythe((i & 1) != 0, true, 0);
    f.row_asked = f.row_read = true; f.row_5 = 0; f.row_3d = 1; f.row_11ac = .25f+.01f*(float)(i & 1); tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.request_set == 15 && s.request_changes == 29 && s.avatar_changes == 0 && s.applied == 0 && s.nothing == 15 && tfp_gate_worth(&s));
  CHECK(s.row_frames == 30 && s.row5_set == 0 && s.row5_changes == 0 && s.row3d_set == 30);
  s.depth_off = 1;
  tfp_gate_line(&s, true, text, sizeof text);
  CHECK(same(text, "Gate: 30 frames of the rig root, the lens read in 30; avatar bit set in 0, changed 0 times; request+0x8f set in 15, changed 29 times; "
                   "zoom depth added in 0, nothing to add in 15, passed in 15 (unzoom gate or request state 15); "
                   "lens target 60.0 to 90.0 degrees, lens 63.8 to 67.6; request+0x88 = 1.000, +0x8d = 0; "
                   "main row read in 30: +5 set in 0, changed 0 times, +0x3d set in 30, +0x11ac 0.2500 to 0.2600; "
                   "depth_off.txt: the mod's zoom depth is forced to nothing; the first second of a native entry"));
  /* Worth a line without a change of byte: the depth term added in some frames and passed in others (full aim taken
   * and let go). Passed in all of them, or added in all, is calm. */
  memset(&g, 0, sizeof g);
  for (int i = 0; i < 30; ++i) { TfpGateFrame f = scythe(false, false, -.2f); if (i >= 20) { f.reason = TFP_GATE_FULL_AIM; f.correction = 0; } tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.applied == 20 && s.passed[TFP_GATE_FULL_AIM] == 10 && tfp_gate_worth(&s));
  for (int i = 0; i < 30; ++i) { TfpGateFrame f = scythe(false, false, 0); f.reason = TFP_GATE_FULL_AIM; tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.applied == 0 && s.passed[TFP_GATE_FULL_AIM] == 30 && !tfp_gate_worth(&s));
  /* The main row's byte +5 changing counts as a gate byte; a row that was asked for and could not be read says so. */
  for (int i = 0; i < 4; ++i) { TfpGateFrame f = scythe(false, false, 0); f.reason = TFP_GATE_FULL_AIM; f.row_asked = f.row_read = true; f.row_5 = i == 2; tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.row5_changes == 2 && s.row5_set == 1 && tfp_gate_worth(&s));
  /* Frames without the lens: counted as frames, the bytes not counted and no change made up across them. */
  memset(&g, 0, sizeof g);
  { TfpGateFrame f = scythe(true, false, 0); tfp_gate_note(&g, &f);
    memset(&f, 0, sizeof f); f.reason = TFP_GATE_NO_LENS; f.row_asked = true; tfp_gate_note(&g, &f); f.reason = TFP_GATE_NO_FRAME; tfp_gate_note(&g, &f);
    f = scythe(true, false, 0); tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.frames == 4 && s.lens_frames == 2 && s.avatar_set == 2 && s.avatar_changes == 0 && s.passed[TFP_GATE_NO_LENS] == 1 && s.passed[TFP_GATE_NO_FRAME] == 1);
  tfp_gate_line(&s, false, text, sizeof text);
  CHECK(strstr(text, "passed in 4 (no lens read for the frame 1, unzoom gate or request state 2, no evaluator frame 1)") && strstr(text, "; main row not readable"));
  /* A second with no lens at all, a reason out of range, and a line that does not fit: no overrun, cut short. */
  memset(&g, 0, sizeof g);
  { TfpGateFrame f; memset(&f, 0, sizeof f); f.reason = 99; tfp_gate_note(&g, &f); }
  tfp_gate_take(&g, &s);
  CHECK(s.passed[TFP_GATE_NO_FRAME] == 1 && !tfp_gate_worth(&s));
  tfp_gate_line(&s, false, text, sizeof text);
  CHECK(strstr(text, "; the lens was not read; main row not read"));
  for (unsigned i = 1; i < TFP_GATE_REASONS; ++i) s.passed[i] = 4000000000u;
  s.frames = s.lens_frames = 4000000000u;
  tfp_gate_line(&s, true, text, 64);
  CHECK(strlen(text) == 63);
  tfp_gate_line(&s, true, text, sizeof text);
  CHECK(strlen(text) < sizeof text-1 && strstr(text, "the first second of a native entry"));
  memset(&s, 0, sizeof s);
  CHECK(!tfp_gate_worth(&s)); /* no frames: nothing to say */
  printf("gate log: %d checks, %d failed\n", checks, failed);
  return failed != 0;
}
