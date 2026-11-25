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

  printf("Perf line: the sample second formats as documented; the largest line is %d of 700 bytes\n", largest);
  return 0;
}
