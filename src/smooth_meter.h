#ifndef FPC_SMOOTH_METER_H
#define FPC_SMOOTH_METER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reports the frames on which the displayed view jumps. Values only: no
 * engine calls, no allocation. The eye is judged relative to the body and the
 * weapon socket in the view's frame, so walking, turning and a respawn are not
 * jumps. A channel reports the part of this frame's step that the previous
 * frame's velocity does not explain. */
enum { FPC_JUMP_EYE=1, FPC_JUMP_TURN=2, FPC_JUMP_FOV=4, FPC_JUMP_NEAR=8, FPC_JUMP_SOCKET=16,
       FPC_JUMP_GAP=32 /* not from the meter: the caller could not sample this frame */ };
typedef struct FpcMeterSample {
  double dt;                     /* seconds since the previous rendered frame */
  uint64_t player_generation;
  float eye[3], rotation[4];     /* displayed camera, world; quaternion xyzw */
  float vertical_fov, near_clip; /* radians, metres */
  float body[3];                 /* body root position, world */
  float socket[3];               /* weapon socket, world */
  bool socket_valid;
} FpcMeterSample;
typedef struct FpcJump { uint32_t channels; float eye_m, turn_deg, fov_deg, near_m, socket_m; } FpcJump;
typedef struct FpcMeter {
  bool primed, moving;
  uint32_t settling; /* channels reported on the previous frame */
  FpcMeterSample last;
  float eye_velocity[3], socket_velocity[3], turn_rate, fov_rate;
} FpcMeter;
/* true when this frame is a jump; *out is always filled. */
bool fpc_meter_step(FpcMeter *meter, const FpcMeterSample *sample, FpcJump *out);

/* The session log line for one jump, at most 20 a second; what was held back
 * is counted in the next line written. tools/smooth_report.py parses it:
 *   Smooth: t=1234 ch=ET eye=0.250 turn=46.0 fov=0.0 near=0.000 socket=0.000 owned=0 rig=1 dropped=0
 * ch letters: Eye, Turn, Field of view, Near clip, weapon Socket, and G for a
 * frame the view could not be sampled (a jump across it would go unseen). */
typedef struct FpcSmoothLog { uint64_t second; uint32_t written, dropped; } FpcSmoothLog;
bool fpc_smooth_line(FpcSmoothLog *log, uint64_t milliseconds, const FpcJump *jump,
                     bool owned, bool rig, char *text, size_t size);
#endif
