/* Round 28: the once-a-second "Perf:" line (src/perf_line.h).
 *  1. a sample second gives exactly the documented text (docs/design/specs/2026-11-25-performance.md);
 *  2. with every count at UINT32_MAX and every time past the clamp (and NaN, negative, infinite) the line still fits
 *     say()'s 700-byte buffer with room to spare, uncut. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "perf_line.h"

int main(void) {
  char line[700];
  TfpPerfSecond p;
  memset(&p, 0, sizeof p);
  p.ticks = 49;
  const double mean[TFP_PERF_SECTIONS] = {12.4, 210.6, 35, 980.2, 18, 41, 103, 25};
  const double most[TFP_PERF_SECTIONS] = {40, 900, 80, 5210.7, 60, 300, 816, 90};
  memcpy(p.mean_us, mean, sizeof mean), memcpy(p.most_us, most, sizeof most);
  p.scan_entries = 5123, p.scan_us = 410, p.scan_worst_us = 2290, p.scans = 1;
  p.host_writes = 392, p.process_writes = 392, p.queries = 98, p.engine_calls = 21560, p.log_lines = 14;
  int n = tfp_perf_line(line, sizeof line, &p);
  const char *want = "Perf: per tick mean/largest us over 49 ticks: pre 12/40, frame 211/900, dof 35/80, vignette 980/5211, notes 18/60, "
                     "menus 41/300, arms 103/816, rest 25/90; scan 5123 entries 410 us (worst 2290 us, 1 this second); "
                     "per second: host_write 392, WriteProcessMemory 392, VirtualQuery 98, eng_call 21560, log lines 14";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  assert(n == (int)strlen(want));

  /* the largest line */
  p.ticks = UINT32_MAX;
  for (int i = 0; i < TFP_PERF_SECTIONS; i++) p.mean_us[i] = 1e300, p.most_us[i] = INFINITY;
  p.scan_entries = p.scan_us = p.scan_worst_us = p.scans = UINT32_MAX;
  p.host_writes = p.process_writes = p.queries = p.engine_calls = p.log_lines = UINT32_MAX;
  n = tfp_perf_line(line, sizeof line, &p);
  assert(n > 0 && n < (int)sizeof line && (size_t)n == strlen(line));
  assert(strstr(line, "pre 99999999/99999999,") && strstr(line, "log lines 4294967295"));
  const int largest = n;

  /* times that are not numbers or below zero print as 0 */
  p.mean_us[TFP_PERF_PRE] = NAN, p.most_us[TFP_PERF_PRE] = -5;
  n = tfp_perf_line(line, sizeof line, &p);
  assert(n > 0 && n < (int)sizeof line && strstr(line, "pre 0/0,"));

  /* Round 32: the laps of the frame and their two "other" slots */
  int64_t lap[TFP_LAPS];
  memset(lap, 0, sizeof lap);
  lap[TFP_LAP_UPDATE] = 100, lap[TFP_LAP_FOV] = 10, lap[TFP_LAP_CAMERA] = 30, lap[TFP_LAP_DOF] = 20;
  lap[TFP_LAP_EXTRA] = 40, lap[TFP_LAP_POSE] = 25, lap[TFP_LAP_EFFECTS] = 30; /* clock noise: the parts exceed the whole */
  tfp_perf_laps_close(lap);
  assert(lap[TFP_LAP_UPDATE_OTHER] == 40 && lap[TFP_LAP_EXTRA_OTHER] == 0);
  double lap_mean[TFP_LAPS], lap_most[TFP_LAPS];
  for (int i = 0; i < TFP_LAPS; i++) lap_mean[i] = i + 0.4, lap_most[i] = 10 * i;
  n = tfp_perf_frame_line(line, sizeof line, lap_mean, lap_most);
  want = "Perf frame: per tick mean/largest us: on_update 0/0 (fov 1/10, blur 2/20, camera 3/30, head 4/40, movement 5/50, near 6/60, "
         "dof 7/70, other 8/80); extra 9/90 (pose 10/100, motion 11/110, engine 12/120, effects 13/130, other 14/140); hold 15/150; view log 16/160";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  for (int i = 0; i < TFP_LAPS; i++) lap_mean[i] = 1e300, lap_most[i] = INFINITY;
  n = tfp_perf_frame_line(line, sizeof line, lap_mean, lap_most);
  assert(n > 0 && n < (int)sizeof line && (size_t)n == strlen(line) && strstr(line, "view log 99999999/99999999"));
  const int largest_frame = n;
  lap_mean[TFP_LAP_HOLD] = NAN, lap_most[TFP_LAP_HOLD] = -1;
  tfp_perf_frame_line(line, sizeof line, lap_mean, lap_most);
  assert(strstr(line, "hold 0/0;"));

  /* the backend's counts and the shadow memo */
  TfpPerfSnapshots s;
  memset(&s, 0, sizeof s);
  s.reads = 7310, s.takes[0] = 610, s.takes[1] = 240, s.takes[2] = 30, s.tables = 700, s.invokes = 2, s.invoke_bodies = 1;
  s.calls[0] = 540, s.calls[1] = 180, s.calls[2] = 30, s.hits[0] = 480, s.hits[1] = 120, s.hits[2] = 0, s.mismatches[0] = 3, s.moved = 1;
  n = tfp_perf_snapshots_line(line, sizeof line, &s);
  want = "Perf snapshots: per second: guarded reads 7310; snapshots full 610, player 240, pin 30; tables 700; invoke 2, invoke_body 1; "
         "memo (shadow) calls 540/180/30, hits 480/120/0, mismatches 3/0/0, moved 1";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  memset(&s, 0xff, sizeof s);
  s.memo = TFP_PERF_MEMO_SHADOW; /* memo_off.txt */
  n = tfp_perf_snapshots_line(line, sizeof line, &s);
  assert(n > 0 && n < (int)sizeof line && (size_t)n == strlen(line) && strstr(line, "moved 4294967295") && strstr(line, "memo (shadow)"));
  const int largest_shadow = n;
  int largest_snapshots = n;

  /* the first mismatch, once */
  n = tfp_perf_mismatch_line(line, sizeof line, 0, "controller_bytes", 0x8c, 1234);
  want = "Memo: first mismatch: a full snapshot taken again in the same frame epoch (1234) differed from the one before it, first at "
         "controller_bytes (+0x8c); the memo stays a shadow (answers are always fresh)";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  char long_field[200];
  memset(long_field, 'x', sizeof long_field - 1), long_field[sizeof long_field - 1] = 0;
  n = tfp_perf_mismatch_line(line, sizeof line, 7, long_field, SIZE_MAX, UINT64_MAX);
  assert(n > 0 && n < (int)sizeof line && strstr(line, "a ? snapshot"));
  const int largest_mismatch = n;

  /* Round 35: the memo live, then turned off; the shadow's text above is round 32's byte for byte */
  memset(&s, 0, sizeof s);
  s.memo = TFP_PERF_MEMO_LIVE;
  s.reads = 2100, s.takes[0] = 60, s.takes[1] = 75, s.takes[2] = 30, s.tables = 120;
  s.calls[0] = 540, s.calls[1] = 180, s.calls[2] = 30, s.hits[0] = 480, s.hits[1] = 120, s.served[0] = 450, s.served[1] = 105;
  s.checked[0] = 30, s.checked[1] = 15, s.saved_reads = 52000;
  n = tfp_perf_snapshots_line(line, sizeof line, &s);
  want = "Perf snapshots: per second: guarded reads 2100; snapshots full 60, player 75, pin 30; tables 120; invoke 0, invoke_body 0; "
         "memo (live, the first and every 16th hit checked) calls 540/180/30, hits 480/120/0, served 450/105/0, checked 30/15/0, "
         "mismatches 0/0/0, moved 0; saved reads 52000";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  s.memo = TFP_PERF_MEMO_TURNED_OFF;
  n = tfp_perf_snapshots_line(line, sizeof line, &s);
  assert(strstr(line, "memo (turned off by a check, shadow again) calls 540/180/30,"));
  memset(&s, 0xff, sizeof s);
  for (int state = TFP_PERF_MEMO_LIVE; state <= TFP_PERF_MEMO_TURNED_OFF; state++) {
    s.memo = state;
    n = tfp_perf_snapshots_line(line, sizeof line, &s);
    assert(n > 0 && n < (int)sizeof line && (size_t)n == strlen(line) && strstr(line, "saved reads 4294967295"));
    if (n > largest_snapshots) largest_snapshots = n;
  }
  n = tfp_perf_memo_off_line(line, sizeof line, 0, false, "controller_bytes", 0x8c, 1234);
  want = "Memo: off for the session: a check of a full snapshot in frame epoch 1234 found the stored answer differs from a fresh take, "
         "first at controller_bytes (+0x8c); every answer is fresh from here (on again at the next start unless memo_off.txt is beside the DLL)";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  n = tfp_perf_memo_off_line(line, sizeof line, 1, true, "state", 0, 7);
  want = "Memo: off for the session: a check of a player snapshot in frame epoch 7 found that a fresh take changed the backend's own "
         "state, which a served hit skips; every answer is fresh from here (on again at the next start unless memo_off.txt is beside the DLL)";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  int largest_off = 0;
  for (int moved = 0; moved < 2; moved++) {
    n = tfp_perf_memo_off_line(line, sizeof line, 9, moved, long_field, SIZE_MAX, UINT64_MAX);
    assert(n > 0 && n < (int)sizeof line && (size_t)n == strlen(line) && strstr(line, "a ? snapshot"));
    if (n > largest_off) largest_off = n;
  }

  /* Round 40: the start's line: the memo on by default, memo_off.txt switches it off, memo_on.txt changes nothing */
  int largest_start = 0;
  n = tfp_perf_memo_start_line(line, sizeof line, false, false);
  want = "Ready: snapshot memo on: a snapshot asked for again in the same frame epoch answers the one already taken; the first and "
         "every 16th such answer is still taken fresh and compared, and a difference turns the memo off for the session; "
         "memo_off.txt beside the DLL switches it off";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  n = tfp_perf_memo_start_line(line, sizeof line, false, true);
  if (strncmp(line, want, strlen(want)) || strcmp(line + strlen(want), " (memo_on.txt is no longer needed and changes nothing)")) {
    printf("got:  %s\n", line);
    return 1;
  }
  if (n > largest_start) largest_start = n;
  n = tfp_perf_memo_start_line(line, sizeof line, true, false);
  want = "Test: memo_off.txt: the snapshot memo is off; every snapshot is taken fresh and the memo only compares (the shadow, "
         "\"memo (shadow)\" in the Perf snapshots line)";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  n = tfp_perf_memo_start_line(line, sizeof line, true, true);
  if (strncmp(line, want, strlen(want)) || strcmp(line + strlen(want), "; memo_on.txt is no longer needed and does not turn it on")) {
    printf("got:  %s\n", line);
    return 1;
  }
  if (n > largest_start) largest_start = n;
  assert(n > 0 && (size_t)n == strlen(line) && n < 700);

  /* Round 35: on_update in parts */
  double upd_mean[TFP_UPDS], upd_most[TFP_UPDS];
  for (int i = 0; i < TFP_UPDS; i++) upd_mean[i] = i + 0.4, upd_most[i] = 10 * i;
  n = tfp_perf_update_line(line, sizeof line, upd_mean, upd_most, 61.5, 140);
  want = "Perf update: per tick mean/largest us, on_update in parts: targets 14/140, controller 5/50, managed 8/80, recovery 4/40, "
         "owner 2/20, presentation 3/30, stops 7/70, clean poll 9/90, editor+motion 6/60, entry 1/10, menu 10/100, ready 11/110, "
         "aim markers 12/120, diagnostics 13/130, frame 0/0; all snapshot calls 62/140";
  if (strcmp(line, want)) { printf("got:  %s\nwant: %s\n", line, want); return 1; }
  for (int i = 0; i < TFP_UPDS; i++) upd_mean[i] = 1e300, upd_most[i] = INFINITY;
  n = tfp_perf_update_line(line, sizeof line, upd_mean, upd_most, INFINITY, 1e300);
  assert(n > 0 && n < (int)sizeof line && (size_t)n == strlen(line) && strstr(line, "all snapshot calls 99999999/99999999"));
  const int largest_update = n;
  upd_mean[TFP_UPD_MENU] = NAN, upd_most[TFP_UPD_MENU] = -1;
  tfp_perf_update_line(line, sizeof line, upd_mean, upd_most, NAN, -3);
  assert(strstr(line, "menu 0/0,") && strstr(line, "all snapshot calls 0/0"));

  printf("Perf line: the sample second formats as documented; the largest line is %d of 700 bytes\n", largest);
  printf("Perf frame / Perf snapshots / Memo lines (round 32): as documented; the largest are %d, %d and %d of 700 bytes\n", largest_frame,
         largest_shadow, largest_mismatch);
  printf("Perf update / the memo's Perf snapshots and Memo off lines (round 35): as documented; the largest are %d, %d and %d of 700 bytes\n",
         largest_update, largest_snapshots, largest_off);
  printf("Memo start line (round 40, on by default, memo_off.txt): as documented; the largest is %d of 700 bytes\n", largest_start);
  return 0;
}
