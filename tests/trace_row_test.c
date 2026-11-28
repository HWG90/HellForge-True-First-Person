/* The length of a Trace row and of its Turn and Held lines (src/trace_row.h): the log's line cuts what is longer than
 * TFP_LOG_LINE without saying so (round 49: 701 to 706 characters on a dive + swap's edge row lost dive=1). */
#include "trace_row.h"
#include <string.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
/* A row at places `far` from the origin with every optional field there. `every`: every number signed and at its
 * widest, every mark at once (more than any frame has), the clock at its widest: the widest row there can be. Else a
 * row as wide as a session writes: the signs of the longest logged row (session ac8bd22e, 22:15:29.635: 603 characters)
 * with all three places negative, a lease, the session's clock as the logs have it, and two marks. */
static TfpCameraTraceRow wide(float far, bool edge, bool every) {
  TfpCameraTraceRow r; memset(&r, 0, sizeof r);
  r.ms = every ? -1999.9f : -36.8f; r.kind = 4; r.source = every ? 3 : 1; r.step = 0.2163f; r.correction = every ? -0.1234f : 0.2962f; r.entry_ease = 1;
  static const float logged_axes[9] = {-0.319011f, 0.947751f, 0, -0.947751f, -0.319011f, -0.f, -0.f, 0, 1};
  for (int i = 0; i < 3; ++i) { r.eye[i] = r.wanted[i] = r.root[i] = r.game_cam[i] = -far; r.hold[i] = -0.1234f; r.game_turn[i] = -0.5774f;
    r.damp[i] = every || i != 1 ? -0.08067f : 0.02339f; r.view[i] = r.turn[i] = -0.5774f; r.veil[i] = every || i != 2 ? -0.03190f : 0; }
  for (int i = 0; i < 9; ++i) r.axes[i] = every ? -0.577350f : logged_axes[i];
  r.what = TFP_CAMERA_TRACE_LOOKED | TFP_CAMERA_TRACE_EDGE | TFP_CAMERA_TRACE_ROOT | TFP_CAMERA_TRACE_DAMPED;
  if (every) r.what |= TFP_CAMERA_TRACE_JUMP | TFP_CAMERA_TRACE_RESTART | TFP_CAMERA_TRACE_START | TFP_CAMERA_TRACE_CUT | TFP_CAMERA_TRACE_SETTLE | TFP_CAMERA_TRACE_QUIET |
                       TFP_CAMERA_TRACE_OFF_FOOT | TFP_CAMERA_TRACE_SELF;
  r.tick = every ? 4294967295.999 : 492434490.083; r.sock = every ? -0.12345f : 0; r.arms = 3; r.veil_head[0] = r.veil_head[1] = -0.1234f;
  r.head_shown[0] = r.head_shown[1] = -0.1234f; r.head_known = true; r.clear = every ? -0.1234f : 0.0451f;
  r.hold_known = true; r.dive = true; r.turn_carry = 119.99f;
  if (edge) { r.turn_edge = 119.99f; r.game_cam_known = true; }
  return r;
}
int main(void) {
  char line[1024], turn[TFP_LOG_LINE], held[TFP_LOG_LINE];
  static const float places[4] = {9.99f, 99.99f, 999.99f, 9999.99f};
  int longest = 0, longest_every = 0, longest_turn = 0, longest_held = 0;
  for (int p = 0; p < 4; ++p) for (int edge = 0; edge < 2; ++edge) for (int every = 0; every < 2; ++every) {
    const TfpCameraTraceRow r = wide(places[p], edge != 0, every != 0);
    const int length = tfp_trace_row_text(line, sizeof line, &r, every ? 0xfffffffffffull : 0x316b13568ull, true), said = tfp_trace_turn_text(turn, sizeof turn, &r),
              more = tfp_trace_held_text(held, sizeof held, &r);
    CHECK(length > 0 && (size_t)length == strlen(line) && !strncmp(line, "Trace: t=", 9));
    CHECK(length < TFP_LOG_LINE);                              /* no row is cut, not even one no frame can have */
    if (!every && p < 3) CHECK(length < TFP_TRACE_ROW_MOST);   /* and a session's rows have room left */
    CHECK(length > 13 && !strcmp(line + length - 13, every ? "clear=-0.1234" : " clear=0.0451")); /* the row's last field is whole */
    /* nothing of round 49 is on the row */
    CHECK(!strstr(line, "turn_carry=") && !strstr(line, "turn_edge=") && !strstr(line, "game_turn=") && !strstr(line, "game_cam=") && !strstr(line, "heading=") &&
          !strstr(line, "hold=") && !strstr(line, "dive="));
    CHECK(said > 0 && said < TFP_TRACE_ROW_MOST && (size_t)said == strlen(turn));
    if (every) {
      if (edge) CHECK(!strncmp(turn, "Turn: t=-1999.9 tick=4294967295.999 kind=4 turn_carry=119.99 turn_edge=119.99 game_turn=(-0.5774 -0.5774 -0.5774) game_cam=(", 124) &&
                      !strcmp(turn + said - 15, " heading=-135.0"));
      else CHECK(!strcmp(turn, "Turn: t=-1999.9 tick=4294967295.999 kind=4 turn_carry=119.99")); /* a frame of the ease: no probe */
      CHECK(!strcmp(held, "Held: t=-1999.9 tick=4294967295.999 kind=4 hold=(-0.1234 -0.1234 -0.1234) dive=1"));
    }
    CHECK(more > 0 && more < TFP_TRACE_ROW_MOST && (size_t)more == strlen(held));
    if (!every && p < 3 && length > longest) longest = length;
    if (every && length > longest_every) longest_every = length;
    if (said > longest_turn) longest_turn = said;
    if (more > longest_held) longest_held = more;
  }
  { /* a plain row says none of the optional fields and has neither line; a dive alone, a held eye alone; and where the
     * game's camera is not known the Turn line says so */
    TfpCameraTraceRow r; memset(&r, 0, sizeof r);
    CHECK(tfp_trace_row_text(line, sizeof line, &r, 0, false) > 0 && !strstr(line, "turn_carry"));
    CHECK(!strcmp(line, "Trace: t=0.0 kind=0 eye_world=(0.00000 0.00000 0.00000) step=0.0000 wanted=(0.00000 0.00000 0.00000) correction=0.0000 lease=0x0 pose_armed=0"
                        " entry_ease=0.00 source=- root=(0.00000 0.00000 0.00000) axes=(0.000000 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000 0.000000)"
                        " looked=0 taken=- quiet=0 tick=0.000 off_foot=0 damped=0 damp=(0.00000 0.00000 0.00000) view=(0.00000 0.00000 0.00000) sock=0.00000 arms=0"
                        " eye_edge=0 veil=0 turn=(0.0000 0.0000 0.0000) veil_head=0 head_shown=0 clear=0.0000"));
    CHECK(tfp_trace_turn_text(turn, sizeof turn, &r) == 0 && tfp_trace_held_text(held, sizeof held, &r) == 0 && held[0] == 0);
    r.dive = true; r.kind = 6; r.tick = 12.5; r.ms = 3;
    CHECK(tfp_trace_held_text(held, sizeof held, &r) > 0 && !strcmp(held, "Held: t=3.0 tick=12.500 kind=6 dive=1"));
    r.dive = false; r.hold_known = true; r.hold[1] = .25f;
    CHECK(tfp_trace_held_text(held, sizeof held, &r) > 0 && !strcmp(held, "Held: t=3.0 tick=12.500 kind=6 hold=(0.0000 0.2500 0.0000)"));
    r.turn_edge = 29; r.turn_carry = 29; r.axes[3] = 1;
    CHECK(tfp_trace_turn_text(turn, sizeof turn, &r) > 0 && !strcmp(turn, "Turn: t=3.0 tick=12.500 kind=6 turn_carry=29.00 turn_edge=29.00 game_turn=(0.0000 0.0000 0.0000) game_cam=- heading=90.0"));
  }
  printf("trace row: a session's widest row within 1 km %d characters, the widest there can be within 10 km %d, Turn line %d, Held line %d; the log's line holds %d; %d checks, %d failed\n",
         longest, longest_every, longest_turn, longest_held, TFP_LOG_LINE-1, checks, failed);
  return failed != 0;
}
