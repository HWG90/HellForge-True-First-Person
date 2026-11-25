/* Round 28 (docs/design/specs/2026-11-25-performance.md): the once-a-second "Perf:" line of the feel build.
 *
 * Logging only. mod_view.c times the tick in sections with QueryPerformanceCounter (no allocation, no formatting in
 * the tick) and counts what the survey of round 27 could only estimate; once a second, beside the Cost line, it
 * fills a TfpPerfSecond and formats it here. Portable C so tests/perf_line_test.c can prove the line fits the
 * 700-byte buffer of say() with every field at its largest. */
#ifndef TFP_PERF_LINE_H
#define TFP_PERF_LINE_H
#include <stdint.h>
#include <stdio.h>

/* The tick's sections, in the order they run. Exclusive: they add up to the tick (rest is what is left).
 *  pre      the notes and locks at the top of the tick (gate, steady, ADS seat, quit guard, FOV lock, lock and FOV notes)
 *  frame    tfp_wire_frame without the two below (the runtime, the helpers' stats, the motion, the pose, the effects' glue)
 *  dof      the depth of field's own work (rt_dof_update / _stop / _shutdown) without the vignette
 *  vignette the vignette (dv_update / _stop / _shutdown: World.entities, the entity scan, the rows)
 *  notes    entry, head, warp, orphan and trace notes after the frame
 *  menus    the editor and panel ticks before the frame; the panel, menu, options and settings frames after it
 *  arms     arms_tick (the arm drive's flags, rest, grip, render hook and its once-a-second lines)
 *  rest     the tick less all of the above (stage checks, keys, the calls= text, the hook notes) */
enum { TFP_PERF_PRE, TFP_PERF_FRAME, TFP_PERF_DOF, TFP_PERF_VIGNETTE, TFP_PERF_NOTES, TFP_PERF_MENUS, TFP_PERF_ARMS, TFP_PERF_REST,
       TFP_PERF_SECTIONS };

typedef struct TfpPerfSecond {
  uint32_t ticks;                                      /* ticks in the second (the Cost line's count) */
  double mean_us[TFP_PERF_SECTIONS], most_us[TFP_PERF_SECTIONS]; /* per tick: mean and largest of each section */
  /* the vignette's last World.entities copy (src/wire_win_effects.c, tfp_win_effects_last_scan): entries, its
   * microseconds (the copy after World.entities and its length), the worst so far, and how many copies this second */
  uint32_t scan_entries, scan_us, scan_worst_us, scans;
  /* per second: host_write calls (the helpers' results into the mod's own structs), WriteProcessMemory calls
   * (whatever is still on it), VirtualQuery calls, protected engine calls (eng_call and eng_method), log lines
   * (say, the wire's log, the engine bridge's log) */
  uint32_t host_writes, process_writes, queries, engine_calls, log_lines;
} TfpPerfSecond;

/* The whole text handed to say("%s"). Each time is held to 0 .. 99,999,999 us (eight digits), so with every count at
 * UINT32_MAX the line is still about 520 bytes. Returns snprintf's answer: the length it needed (>= size: cut). */
static inline int tfp_perf_line(char *out, size_t size, const TfpPerfSecond *p) {
  double m[TFP_PERF_SECTIONS], x[TFP_PERF_SECTIONS];
  for (int i = 0; i < TFP_PERF_SECTIONS; i++) {
    m[i] = p->mean_us[i] > 0 ? (p->mean_us[i] < 99999999.0 ? p->mean_us[i] : 99999999.0) : 0;
    x[i] = p->most_us[i] > 0 ? (p->most_us[i] < 99999999.0 ? p->most_us[i] : 99999999.0) : 0;
  }
  return snprintf(out, size,
                  "Perf: per tick mean/largest us over %u ticks: pre %.0f/%.0f, frame %.0f/%.0f, dof %.0f/%.0f, vignette %.0f/%.0f, "
                  "notes %.0f/%.0f, menus %.0f/%.0f, arms %.0f/%.0f, rest %.0f/%.0f; scan %u entries %u us (worst %u us, %u this second); "
                  "per second: host_write %u, WriteProcessMemory %u, VirtualQuery %u, eng_call %u, log lines %u",
                  (unsigned)p->ticks, m[TFP_PERF_PRE], x[TFP_PERF_PRE], m[TFP_PERF_FRAME], x[TFP_PERF_FRAME], m[TFP_PERF_DOF], x[TFP_PERF_DOF],
                  m[TFP_PERF_VIGNETTE], x[TFP_PERF_VIGNETTE], m[TFP_PERF_NOTES], x[TFP_PERF_NOTES], m[TFP_PERF_MENUS], x[TFP_PERF_MENUS],
                  m[TFP_PERF_ARMS], x[TFP_PERF_ARMS], m[TFP_PERF_REST], x[TFP_PERF_REST], (unsigned)p->scan_entries, (unsigned)p->scan_us,
                  (unsigned)p->scan_worst_us, (unsigned)p->scans, (unsigned)p->host_writes, (unsigned)p->process_writes, (unsigned)p->queries,
                  (unsigned)p->engine_calls, (unsigned)p->log_lines);
}
#endif
