/* src/wire_action.h: the pick-up mask for the head camera, and the aim button between ticks. */
#include <stdio.h>
#include "wire_action.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(void) {
  const uint8_t pickup[6] = {0x22, 0, 0, 0, 0, 0}, guarded[6] = {0x22, 0, 0, 0, 0, 1}, other[6] = {0x40, 0, 0, 0, 0, 0}, wide[6] = {0x22, 0, 1, 0, 0, 0};
  /* the head camera's flag: retail's answer stands; the pick-up mask adds to it on the ordinary camera only */
  CHECK(tfp_action_head_camera(true, 6, true, other));
  CHECK(tfp_action_head_camera(false, 4, false, pickup));
  CHECK(!tfp_action_head_camera(false, 6, false, pickup)); /* the native view is not the head camera */
  CHECK(!tfp_action_head_camera(false, 4, true, pickup));  /* seated */
  CHECK(!tfp_action_head_camera(false, 4, false, guarded)); /* the guard byte */
  CHECK(!tfp_action_head_camera(false, 4, false, other) && !tfp_action_head_camera(false, 4, false, wide));

  TfpAimWatch w;
  memset(&w, 0, sizeof w);
  /* not armed: no probe */
  tfp_aim_watch_frame(&w, 10.0, false, true);
  CHECK(!w.armed && !tfp_aim_watch_due(&w, 10.001));
  /* full aim begins: armed, but young */
  tfp_aim_watch_frame(&w, 10.010, true, true);
  tfp_aim_watch_note(&w, 1, 1); tfp_aim_watch_note(&w, 2, 0);
  CHECK(w.armed && tfp_aim_watch_due(&w, 10.011));
  CHECK(!tfp_aim_watch_probe(&w, 1, 0)); /* nothing changed */
  CHECK(!tfp_aim_watch_allowed(&w, 10.050) && w.guarded == 1); /* 40 ms into full aim: a late press, not a release */
  CHECK(tfp_aim_watch_allowed(&w, 10.200));
  /* the runtime's conditions fail (menu open, not running): not armed although aiming */
  tfp_aim_watch_frame(&w, 10.300, true, false);
  CHECK(!w.armed && !tfp_aim_watch_due(&w, 10.301));
  /* a release between two ticks: reported once */
  tfp_aim_watch_frame(&w, 10.400, true, true);
  tfp_aim_watch_note(&w, 1, 1);
  CHECK(tfp_aim_watch_due(&w, 10.4001) && tfp_aim_watch_probe(&w, 0, 0) && w.probe_edges == 1);
  CHECK(tfp_aim_watch_due(&w, 10.4020) && !tfp_aim_watch_probe(&w, 0, 0));
  tfp_aim_watch_frame(&w, 10.403, true, true);
  /* the press of the aim button is an edge too (click-to-aim); the aim-mode button only when pressed */
  CHECK(tfp_aim_watch_due(&w, 10.4040) && tfp_aim_watch_probe(&w, 1, 0));
  CHECK(tfp_aim_watch_due(&w, 10.4060) && tfp_aim_watch_probe(&w, 1, 1));
  tfp_aim_watch_frame(&w, 10.407, true, true);
  CHECK(tfp_aim_watch_due(&w, 10.4080) && !tfp_aim_watch_probe(&w, 1, 0));
  /* probes closer than a millisecond are skipped, and a frame has only a few */
  CHECK(!tfp_aim_watch_due(&w, 10.4083));
  unsigned done = w.frame_probes;
  for (double t = 10.410; done < TFP_ACTION_PROBES_PER_FRAME; t += 0.002, done++) CHECK(tfp_aim_watch_due(&w, t));
  CHECK(!tfp_aim_watch_due(&w, 10.500));
  tfp_aim_watch_frame(&w, 10.510, true, true);
  CHECK(tfp_aim_watch_due(&w, 10.5101)); /* the next tick opens the next frame's probes */
  /* no answer from the button: no edge, and the level is not forgotten */
  CHECK(!tfp_aim_watch_probe(&w, -1, -1) && w.level[1] == 1);
  /* full aim ended and began again: young again */
  tfp_aim_watch_frame(&w, 11.0, false, true);
  tfp_aim_watch_frame(&w, 11.010, true, true);
  CHECK(!tfp_aim_watch_allowed(&w, 11.020));
  /* a level the tick could not read is not a baseline: the first probe after it reports no edge */
  tfp_aim_watch_note(&w, 1, -1);
  CHECK(!tfp_aim_watch_probe(&w, 0, 0));

  if (failures) return 1;
  printf("wire action: %d checks passed (pick-up mask for the head camera; the aim button between ticks)\n", checks);
  return 0;
}
