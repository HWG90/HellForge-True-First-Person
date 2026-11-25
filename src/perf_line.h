/* Round 28 (docs/design/specs/2026-11-25-performance.md): the once-a-second "Perf:" line of the feel build.
 *
 * Logging only. mod_view.c times the tick in sections with QueryPerformanceCounter (no allocation, no formatting in
 * the tick) and counts what the survey of round 27 could only estimate; once a second, beside the Cost line, it
 * fills a TfpPerfSecond and formats it here. Portable C so tests/perf_line_test.c can prove the line fits the
 * 700-byte buffer of say() with every field at its largest. */
#ifndef TFP_PERF_LINE_H
#define TFP_PERF_LINE_H
#include <stdbool.h>
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

/* Round 32 (docs/design/specs/2026-11-25-performance.md, section 5): the frame section in laps, and what the backend did.
 * Two more lines, each once a second right after the Perf line; logging only, like it.
 *
 * The laps of tfp_wire_frame, gross (each includes what it calls; the dof lap includes the vignette, which the Perf
 * line's frame section leaves out). The two "other" slots are filled per tick by tfp_perf_laps_close: the part of
 * on_update outside the seven managed targets' update calls (the controller, recovery, motion, editor, menu, aim
 * markers, diagnostics), and the part of frame_extra outside its four modules (the Feel line). */
enum { TFP_LAP_UPDATE, TFP_LAP_FOV, TFP_LAP_BLUR, TFP_LAP_CAMERA, TFP_LAP_HEAD, TFP_LAP_MOVEMENT, TFP_LAP_NEAR, TFP_LAP_DOF,
       TFP_LAP_UPDATE_OTHER, TFP_LAP_EXTRA, TFP_LAP_POSE, TFP_LAP_MOTION, TFP_LAP_ENGINE, TFP_LAP_EFFECTS, TFP_LAP_EXTRA_OTHER,
       TFP_LAP_HOLD, TFP_LAP_VIEW_LOG, TFP_LAPS };
/* After a tick: the two "other" laps from their totals (never below 0). */
static inline void tfp_perf_laps_close(int64_t lap[TFP_LAPS]) {
  int64_t other = lap[TFP_LAP_UPDATE];
  for (int i = TFP_LAP_FOV; i <= TFP_LAP_DOF; i++) other -= lap[i];
  lap[TFP_LAP_UPDATE_OTHER] = other > 0 ? other : 0;
  other = lap[TFP_LAP_EXTRA];
  for (int i = TFP_LAP_POSE; i <= TFP_LAP_EFFECTS; i++) other -= lap[i];
  lap[TFP_LAP_EXTRA_OTHER] = other > 0 ? other : 0;
}
static inline double tfp_perf_clamp(double us) { return us > 0 ? (us < 99999999.0 ? us : 99999999.0) : 0; }

/* "Perf frame:": each lap's mean per tick and largest in the second, over the Perf line's ticks. */
static inline int tfp_perf_frame_line(char *out, size_t size, const double mean_us[TFP_LAPS], const double most_us[TFP_LAPS]) {
  double m[TFP_LAPS], x[TFP_LAPS];
  for (int i = 0; i < TFP_LAPS; i++) m[i] = tfp_perf_clamp(mean_us[i]), x[i] = tfp_perf_clamp(most_us[i]);
#define TFP_LAP_(i) m[i], x[i]
  return snprintf(out, size,
                  "Perf frame: per tick mean/largest us: on_update %.0f/%.0f (fov %.0f/%.0f, blur %.0f/%.0f, camera %.0f/%.0f, head %.0f/%.0f, "
                  "movement %.0f/%.0f, near %.0f/%.0f, dof %.0f/%.0f, other %.0f/%.0f); extra %.0f/%.0f (pose %.0f/%.0f, motion %.0f/%.0f, "
                  "engine %.0f/%.0f, effects %.0f/%.0f, other %.0f/%.0f); hold %.0f/%.0f; view log %.0f/%.0f",
                  TFP_LAP_(TFP_LAP_UPDATE), TFP_LAP_(TFP_LAP_FOV), TFP_LAP_(TFP_LAP_BLUR), TFP_LAP_(TFP_LAP_CAMERA), TFP_LAP_(TFP_LAP_HEAD),
                  TFP_LAP_(TFP_LAP_MOVEMENT), TFP_LAP_(TFP_LAP_NEAR), TFP_LAP_(TFP_LAP_DOF), TFP_LAP_(TFP_LAP_UPDATE_OTHER),
                  TFP_LAP_(TFP_LAP_EXTRA), TFP_LAP_(TFP_LAP_POSE), TFP_LAP_(TFP_LAP_MOTION), TFP_LAP_(TFP_LAP_ENGINE), TFP_LAP_(TFP_LAP_EFFECTS),
                  TFP_LAP_(TFP_LAP_EXTRA_OTHER), TFP_LAP_(TFP_LAP_HOLD), TFP_LAP_(TFP_LAP_VIEW_LOG));
#undef TFP_LAP_
}

/* Round 35 (docs/design/specs/2026-11-25-performance.md, section 6): on_update in exclusive parts that add up to it, for
 * the "Perf update:" line. The runtime marks where each part of its update begins (src/tfp_runtime.c PART, in the order
 * TFP_RT_PART_* of src/tfp_runtime.h, the first fourteen here); the seven managed targets' own update calls (the timed
 * wrappers of tfp_wire_laps_start) are one part, "targets", whatever part they are called from.
 *  frame         the backend's frame begin and end, what runs before the first mark, the frame's fault text
 *  entry         update_frame's start: the probe, compat, native aim, the Fird checks (their early outs too)
 *  owner         recovery's reset on an enable change and the new-owner check (a full snapshot; on a new body, every
 *                target's stop and clean poll)
 *  presentation  observe_presentation: a full (or pin) snapshot, the alignment lease, the transition
 *  recovery      the recovery policies' walk (action, proof with its full snapshot, revalidate) and blocks_view
 *  controller    controller:update (the controller's own snapshots and lease work), or its retry cleanup
 *  editor+motion the editor's update, the calibration check, motion:update
 *  stops         the idle check and, with a foreign camera, every target's stop
 *  managed       the managed updates around the targets: recovery's action, proof, success or failure for each target,
 *                its result, and its stop and clean poll on a cleanup or a failure
 *  clean poll    near:is_clean, the Fird warm check, the depth of field's result: whether the idle frame is clean
 *  menu          menu:update (wherever update_frame ends)
 *  ready, aim markers, diagnostics: update_body's three after the frame (the ready stance, aim markers, the record)
 *  targets       the seven targets' own update functions (as the Perf frame line's seven laps, exclusive here) */
enum { TFP_UPD_FRAME, TFP_UPD_ENTRY, TFP_UPD_OWNER, TFP_UPD_PRESENTATION, TFP_UPD_RECOVERY, TFP_UPD_CONTROLLER, TFP_UPD_EDITOR_MOTION,
       TFP_UPD_STOPS, TFP_UPD_MANAGED, TFP_UPD_CLEAN, TFP_UPD_MENU, TFP_UPD_READY, TFP_UPD_AIM_MARKERS, TFP_UPD_DIAGNOSTICS, TFP_UPD_TARGETS,
       TFP_UPDS };
/* "Perf update:": each part's mean per tick and largest in the second, and (gross, inside the parts and the other laps) the
 * time spent in the backend's public snapshot calls from every caller, served or taken. */
static inline int tfp_perf_update_line(char *out, size_t size, const double mean_us[TFP_UPDS], const double most_us[TFP_UPDS],
                                       double snapshots_mean_us, double snapshots_most_us) {
  double m[TFP_UPDS], x[TFP_UPDS];
  for (int i = 0; i < TFP_UPDS; i++) m[i] = tfp_perf_clamp(mean_us[i]), x[i] = tfp_perf_clamp(most_us[i]);
#define TFP_UPD_(i) m[i], x[i]
  return snprintf(out, size,
                  "Perf update: per tick mean/largest us, on_update in parts: targets %.0f/%.0f, controller %.0f/%.0f, managed %.0f/%.0f, "
                  "recovery %.0f/%.0f, owner %.0f/%.0f, presentation %.0f/%.0f, stops %.0f/%.0f, clean poll %.0f/%.0f, editor+motion %.0f/%.0f, "
                  "entry %.0f/%.0f, menu %.0f/%.0f, ready %.0f/%.0f, aim markers %.0f/%.0f, diagnostics %.0f/%.0f, frame %.0f/%.0f; "
                  "all snapshot calls %.0f/%.0f",
                  TFP_UPD_(TFP_UPD_TARGETS), TFP_UPD_(TFP_UPD_CONTROLLER), TFP_UPD_(TFP_UPD_MANAGED), TFP_UPD_(TFP_UPD_RECOVERY),
                  TFP_UPD_(TFP_UPD_OWNER), TFP_UPD_(TFP_UPD_PRESENTATION), TFP_UPD_(TFP_UPD_STOPS), TFP_UPD_(TFP_UPD_CLEAN),
                  TFP_UPD_(TFP_UPD_EDITOR_MOTION), TFP_UPD_(TFP_UPD_ENTRY), TFP_UPD_(TFP_UPD_MENU), TFP_UPD_(TFP_UPD_READY),
                  TFP_UPD_(TFP_UPD_AIM_MARKERS), TFP_UPD_(TFP_UPD_DIAGNOSTICS), TFP_UPD_(TFP_UPD_FRAME), tfp_perf_clamp(snapshots_mean_us),
                  tfp_perf_clamp(snapshots_most_us));
#undef TFP_UPD_
}

/* "Perf snapshots:": per second, what the backend (src/backend_ops.c TfpOpsPerf) did, and the memo. Modes in the order
 * full, player, pin. */
enum { TFP_PERF_MEMO_SHADOW, TFP_PERF_MEMO_LIVE, TFP_PERF_MEMO_TURNED_OFF };
typedef struct TfpPerfSnapshots {
  uint32_t reads;              /* guarded reads of the backend */
  uint32_t takes[3];           /* snapshots built, by mode (the hold's are player ones) */
  uint32_t tables;             /* snapshot tables handed to the runtime (w->snap_next) */
  uint32_t invokes, invoke_bodies;
  uint32_t calls[3], hits[3], mismatches[3]; /* the memo: public snapshot calls, those in the same frame epoch, those that differed */
  uint32_t moved;              /* hits whose own take changed the backend's state */
  /* Round 35: TFP_PERF_MEMO_SHADOW (no memo_on.txt: the line is round 32's, byte for byte), _LIVE, or _TURNED_OFF (a check
   * failed this session: the shadow again). With the switch: hits answered from the memo, live hits checked against a
   * fresh take, and the guarded reads the served hits did not make. */
  int memo;
  uint32_t served[3], checked[3], saved_reads;
} TfpPerfSnapshots;
static inline int tfp_perf_snapshots_line(char *out, size_t size, const TfpPerfSnapshots *p) {
  if (p->memo != TFP_PERF_MEMO_SHADOW)
    return snprintf(out, size,
                    "Perf snapshots: per second: guarded reads %u; snapshots full %u, player %u, pin %u; tables %u; invoke %u, invoke_body %u; "
                    "memo (%s) calls %u/%u/%u, hits %u/%u/%u, served %u/%u/%u, checked %u/%u/%u, mismatches %u/%u/%u, moved %u; "
                    "saved reads %u",
                    (unsigned)p->reads, (unsigned)p->takes[0], (unsigned)p->takes[1], (unsigned)p->takes[2], (unsigned)p->tables,
                    (unsigned)p->invokes, (unsigned)p->invoke_bodies,
                    p->memo == TFP_PERF_MEMO_LIVE ? "live, the first and every 16th hit checked" : "turned off by a check, shadow again",
                    (unsigned)p->calls[0], (unsigned)p->calls[1], (unsigned)p->calls[2], (unsigned)p->hits[0], (unsigned)p->hits[1],
                    (unsigned)p->hits[2], (unsigned)p->served[0], (unsigned)p->served[1], (unsigned)p->served[2], (unsigned)p->checked[0],
                    (unsigned)p->checked[1], (unsigned)p->checked[2], (unsigned)p->mismatches[0], (unsigned)p->mismatches[1],
                    (unsigned)p->mismatches[2], (unsigned)p->moved, (unsigned)p->saved_reads);
  return snprintf(out, size,
                  "Perf snapshots: per second: guarded reads %u; snapshots full %u, player %u, pin %u; tables %u; invoke %u, invoke_body %u; "
                  "memo (shadow) calls %u/%u/%u, hits %u/%u/%u, mismatches %u/%u/%u, moved %u",
                  (unsigned)p->reads, (unsigned)p->takes[0], (unsigned)p->takes[1], (unsigned)p->takes[2], (unsigned)p->tables,
                  (unsigned)p->invokes, (unsigned)p->invoke_bodies, (unsigned)p->calls[0], (unsigned)p->calls[1], (unsigned)p->calls[2],
                  (unsigned)p->hits[0], (unsigned)p->hits[1], (unsigned)p->hits[2], (unsigned)p->mismatches[0], (unsigned)p->mismatches[1],
                  (unsigned)p->mismatches[2], (unsigned)p->moved);
}
/* Once per session, at the first mismatch. field: a TfpOpsSnapshot field name (or "result", "why"); cut to 48 bytes. */
static inline int tfp_perf_mismatch_line(char *out, size_t size, int mode, const char *field, size_t offset, uint64_t epoch) {
  static const char *const modes[] = {"full", "player", "pin"};
  return snprintf(out, size,
                  "Memo: first mismatch: a %s snapshot taken again in the same frame epoch (%llu) differed from the one before it, first at "
                  "%.48s (+0x%llx); the memo stays a shadow (answers are always fresh)",
                  mode >= 0 && mode < 3 ? modes[mode] : "?", (unsigned long long)epoch, field ? field : "?", (unsigned long long)offset);
}
/* Round 35, once per session with memo_on.txt: a check turned the memo off. moved: the fresh take changed the backend's
 * state (a served hit skips that work) rather than answering differently. */
static inline int tfp_perf_memo_off_line(char *out, size_t size, int mode, bool moved, const char *field, size_t offset, uint64_t epoch) {
  static const char *const modes[] = {"full", "player", "pin"};
  const char *name = mode >= 0 && mode < 3 ? modes[mode] : "?";
  if (moved)
    return snprintf(out, size,
                    "Memo: off for the session: a check of a %s snapshot in frame epoch %llu found that a fresh take changed the backend's "
                    "own state, which a served hit skips; every answer is fresh from here (memo_on.txt is still read at the next start)",
                    name, (unsigned long long)epoch);
  return snprintf(out, size,
                  "Memo: off for the session: a check of a %s snapshot in frame epoch %llu found the stored answer differs from a fresh take, "
                  "first at %.48s (+0x%llx); every answer is fresh from here (memo_on.txt is still read at the next start)",
                  name, (unsigned long long)epoch, field ? field : "?", (unsigned long long)offset);
}
#endif
