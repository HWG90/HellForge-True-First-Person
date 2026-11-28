/* The text of a Trace row and of its Turn line (src/mod_view.c trace_note, where the fields are explained). Here so
 * that their length can be tested: the log's line holds TFP_LOG_LINE bytes and cuts what is longer without saying so.
 * Round 49's probe put 701 to 706 characters on the row of a dive + swap's edge frame, which lost dive=1 and part of
 * hold=, the fields the row was there for. The row itself has little room: the longest of the six last sessions' logs
 * is 603 characters before any of round 49's fields, at places within 90 m of the origin, and a row with more signs
 * and a lease at 1 km comes to 634. So the row carries nothing of round 49: it is main's, character for character,
 * and what round 49 says is on two lines of their own after it, each joined to its row by t=, tick= and kind=:
 *   Turn:  turn_carry= in every frame something is carried in, and the probe on the frame the carry began;
 *   Held:  hold= and dive=1, only for a row that has either.
 * tests/trace_row_test.c: the row is under TFP_TRACE_ROW_MOST for a row as wide as a session writes within 1 km of the
 * origin, and under the log's line for the widest there can be within 10 km. No engine reads. */
#ifndef TFP_TRACE_ROW_H
#define TFP_TRACE_ROW_H
#include <math.h>
#include <stdio.h>
#include "camera_helper.h"

#define TFP_LOG_LINE 700       /* src/mod_view.c say(): one line of the log, with its end */
#define TFP_TRACE_ROW_MOST 640 /* a Trace row is shorter than this */

/* The Trace row. Returns what snprintf does: the length the whole row has, also where `size` is less. */
static inline int tfp_trace_row_text(char *out, size_t size, const TfpCameraTraceRow *r, unsigned long long lease, bool pose_armed) {
  static const char *const sources[] = {"-", "head", "native", "repeat"};
  char taken[64] = "";
  snprintf(taken, sizeof taken, "%s%s%s%s%s%s%s", r->what & TFP_CAMERA_TRACE_JUMP ? "+jump" : "", r->what & TFP_CAMERA_TRACE_EDGE ? "+edge" : "",
           r->what & TFP_CAMERA_TRACE_ROOT ? "+root" : "", r->what & TFP_CAMERA_TRACE_RESTART ? "+restart" : "",
           r->what & TFP_CAMERA_TRACE_START ? "+start" : "", r->what & TFP_CAMERA_TRACE_CUT ? "+cut" : "",
           r->what & TFP_CAMERA_TRACE_SETTLE ? "+settle" : "");
  char veil[64] = "0";
  if (r->veil[0] != 0 || r->veil[1] != 0 || r->veil[2] != 0)
    snprintf(veil, sizeof veil, "(%.5f %.5f %.5f)", (double)r->veil[0], (double)r->veil[1], (double)r->veil[2]);
  char veil_head[48] = "0";
  if (r->veil_head[0] != 0 || r->veil_head[1] != 0) snprintf(veil_head, sizeof veil_head, "(%.4f %.4f)", (double)r->veil_head[0], (double)r->veil_head[1]);
  char head_shown[48] = "0";
  if (r->head_known) snprintf(head_shown, sizeof head_shown, "(%.4f %.4f)", (double)r->head_shown[0], (double)r->head_shown[1]);
  return snprintf(out, size,
      "Trace: t=%.1f kind=%u eye_world=(%.5f %.5f %.5f) step=%.4f wanted=(%.5f %.5f %.5f) correction=%.4f lease=0x%llx pose_armed=%d entry_ease=%.2f source=%s"
      " root=(%.5f %.5f %.5f) axes=(%.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f) looked=%d taken=%s quiet=%d tick=%.3f"
      " off_foot=%d damped=%d damp=(%.5f %.5f %.5f) view=(%.5f %.5f %.5f) sock=%.5f arms=%u eye_edge=%d veil=%s turn=(%.4f %.4f %.4f) veil_head=%s"
      " head_shown=%s clear=%.4f",
      (double)r->ms, r->kind, (double)r->eye[0], (double)r->eye[1], (double)r->eye[2], (double)r->step, (double)r->wanted[0], (double)r->wanted[1],
      (double)r->wanted[2], (double)r->correction, lease, (int)pose_armed, (double)r->entry_ease, sources[r->source < 4 ? r->source : 0],
      (double)r->root[0], (double)r->root[1], (double)r->root[2], (double)r->axes[0], (double)r->axes[1], (double)r->axes[2], (double)r->axes[3],
      (double)r->axes[4], (double)r->axes[5], (double)r->axes[6], (double)r->axes[7], (double)r->axes[8],
      (int)((r->what & TFP_CAMERA_TRACE_LOOKED) != 0), taken[0] ? taken + 1 : "-", (int)((r->what & TFP_CAMERA_TRACE_QUIET) != 0), r->tick,
      (int)((r->what & TFP_CAMERA_TRACE_OFF_FOOT) != 0), (int)((r->what & TFP_CAMERA_TRACE_DAMPED) != 0), (double)r->damp[0], (double)r->damp[1],
      (double)r->damp[2], (double)r->view[0], (double)r->view[1], (double)r->view[2], (double)r->sock, (unsigned)r->arms,
      (int)((r->what & TFP_CAMERA_TRACE_SELF) != 0), veil, (double)r->turn[0], (double)r->turn[1], (double)r->turn[2], veil_head,
      head_shown, (double)r->clear);
}
/* The Turn line of a row: turn_carry= in every frame something is carried in, and on the frame a change of camera
 * began the carry (turn_edge is not 0) the probe after it. Returns 0 and writes nothing for any other row. t, tick,
 * kind: the row's own, which the line is joined to its row by. */
static inline int tfp_trace_turn_text(char *out, size_t size, const TfpCameraTraceRow *r) {
  if (size) out[0] = 0;
  if (!(r->turn_carry != 0) && !(r->turn_edge != 0)) return 0;
  char probe[192] = "";
  if (r->turn_edge != 0) {
    char camera[64] = "-";
    if (r->game_cam_known) snprintf(camera, sizeof camera, "(%.3f %.3f %.3f)", (double)r->game_cam[0], (double)r->game_cam[1], (double)r->game_cam[2]);
    snprintf(probe, sizeof probe, " turn_edge=%.2f game_turn=(%.4f %.4f %.4f) game_cam=%s heading=%.1f", (double)r->turn_edge, (double)r->game_turn[0],
             (double)r->game_turn[1], (double)r->game_turn[2], camera, atan2((double)r->axes[3], (double)r->axes[4])*180/3.14159265358979323846);
  }
  return snprintf(out, size, "Turn: t=%.1f tick=%.3f kind=%u turn_carry=%.2f%s", (double)r->ms, r->tick, r->kind, (double)r->turn_carry, probe);
}
/* The Held line of a row: hold= and dive=1 (round 49), only for a row that has either. Returns 0 and writes nothing
 * for any other row. */
static inline int tfp_trace_held_text(char *out, size_t size, const TfpCameraTraceRow *r) {
  if (size) out[0] = 0;
  if (!r->hold_known && !r->dive) return 0;
  char held[64] = "";
  if (r->hold_known) snprintf(held, sizeof held, " hold=(%.4f %.4f %.4f)", (double)r->hold[0], (double)r->hold[1], (double)r->hold[2]);
  return snprintf(out, size, "Held: t=%.1f tick=%.3f kind=%u%s%s", (double)r->ms, r->tick, r->kind, held, r->dive ? " dive=1" : "");
}
#endif
