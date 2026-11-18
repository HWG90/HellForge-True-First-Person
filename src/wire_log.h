/* The session log of the view build: one line per state change and one line a second while the mode
 * is on, so a jarring frame can be found afterwards. Values only: no game reads, no Windows.
 *
 *   State: enter requested (F11)
 *   Status: First person enabled until manually stopped        (the controller's label, retail's own log line)
 *   State: entered the native view request=0x1f2a3b4c kind=6
 *   State: aim in / aim out / aim kept (1) / swap / seated camera detached
 *   State: left the native view: <why the controller waits>
 *   State: exit requested (F11) / exit complete
 *   Drop: <gate> | <action> | <camera>     with "State: left the native view": what the snapshot, the animation layers
 *                                           and the head-camera helper said on that frame
 *   Wait: <gate> | <action> | <camera>     while the view waits: each change of the gate (7 a stretch, then one line saying more follow)
 *   Return: <gate> | <action> | <camera>   with "State: entered the native view"
 *   Cut: largest camera jump 0.312 m in the 0.30 s after leaving / entering the native view   (always said, small or not)
 *   Aim: release not kept (aim button from engine): <gate>      an aim release the aim-view keeper did not catch
 *   Refused: camera: <text>     Cleared: camera
 *   View: t=12.000 frames=60 gaps=0 on=1 phase=running kind=6 ads=0 safe=1 eye_step=0.0123 eye_jump=0.0040 fov=1.1345
 *         fov_native=1.0472 fov_jump=0.0000 near=0.050 <extra>
 *
 * eye_step: the largest move of the rendered camera between two frames of that second (metres).
 * eye_jump: the largest part of such a move that the frame before does not explain (|step - previous
 * step|): walking and turning give millimetres, a cut gives the size of the cut. fov_jump: the same
 * for the applied field of view (radians). gaps: frames whose camera position could not be read. */
#ifndef TFP_WIRE_LOG_H
#define TFP_WIRE_LOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TFP_VIEW_FAULTS = 4 }; /* fov, blur, camera, near */
typedef struct TfpViewSample {
  double time;                       /* seconds, the frame clock */
  bool enabled, has_lease, safe, controller_active, has_identity, has_eye, has_fov, has_near;
  const char *phase, *label, *wait;  /* controller phase, label, why it waits (may be empty) */
  uint64_t request, identity;
  uint32_t camera_kind;
  unsigned aim_keeps, seated_detaches;
  const char *fault[TFP_VIEW_FAULTS]; /* NULL or empty: none */
  double eye[3], fov_native, fov_applied, near_applied;
  const char *extra;                 /* appended to the View line; may be NULL */
  /* What decides whether the native view may be held, for the Drop / Wait / Return lines. NULL or
   * empty: those lines are not written. gate: its changes are logged; action and camera ride along. */
  const char *gate, *action, *camera;
  unsigned aim_probe_keeps, aim_guarded; /* of aim_keeps: made at the script call; changes ignored as full aim began */
  const char *aim_source;            /* where the aim button's level came from ("engine", "windows"); may be NULL */
} TfpViewSample;
typedef struct TfpViewLog {
  void (*write)(void *user, const char *text);
  void *user;
  bool primed, enabled, has_lease, controller_active, has_identity;
  uint64_t identity;
  unsigned aim_keeps, seated_detaches;
  char label[256], fault[TFP_VIEW_FAULTS][256];
  /* the running second */
  bool second_open, has_eye, has_step, has_fov;
  double second_start, eye[3], step[3], fov, fov_step;
  unsigned frames, gaps;
  double step_max, jump_max, fov_jump_max;
  /* the gate while waiting, the cut after an edge, the last aim keep */
  char gate[320];
  unsigned wait_notes;
  bool edge_open, edge_entering, has_keep;
  double edge_start, edge_jump, keep_time;
} TfpViewLog;
extern const char *const tfp_view_fault_names[TFP_VIEW_FAULTS];
void tfp_view_log_init(TfpViewLog *log, void (*write)(void *user, const char *text), void *user);
void tfp_view_log_event(TfpViewLog *log, const char *format, ...);
/* Once per game update, after the runtime order ran. */
void tfp_view_log_frame(TfpViewLog *log, const TfpViewSample *sample);
#endif
