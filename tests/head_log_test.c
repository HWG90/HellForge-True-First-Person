/* The "Head:" line (src/head_log.h): which ticks are worth a line, what is counted, and the words. */
#include "head_log.h"
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)
static bool same(const char *a, const char *b) { if (strcmp(a, b)) printf("  got:  %s\n  want: %s\n", a, b); return !strcmp(a, b); }
/* A tick of the first-person view: the body as the sessions of 2026-11-24 have it once the module owns everything
 * (34 rows; how they split into head, torso and shoulders is made up: that log does not say). */
static TfpHeadTick tick(unsigned kind, bool active, unsigned head, unsigned torso, unsigned shoulders) {
  TfpHeadTick t; memset(&t, 0, sizeof t);
  t.bound = t.view = true; t.kind = kind; t.active = active; t.phase = active ? "active" : "idle";
  t.head = head, t.torso = torso, t.shoulders = shoulders; t.rows = head + torso + shoulders + (active ? 3 : 0);
  t.full = true; t.camera_rows = active ? 31 : 0;
  return t;
}
int main(void) {
  TfpHeadLog g; char text[700]; TfpHeadTick t;
  memset(&g, 0, sizeof g);
  /* Not bound, or the view off and nothing owned: never a line. */
  t = tick(0, false, 0, 0, 0); t.bound = false;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  t = tick(0, false, 0, 0, 0); t.view = false;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text) && !tfp_head_log_step(&g, &t, text, sizeof text));
  /* The view comes on in the native view: the rig's two pads are new, the body's meshes were hidden already (the
   * game's own doing, as it seems): one line for the change of what is owned. */
  t = tick(6, true, 0, 0, 2); t.on_new = 2; t.turned_off = 2;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text));
  CHECK(same(text, "Head: kind=6 active: owns head 0, torso 0, shoulders 2 (5 rows; was head 0, torso 0, shoulders 0); found drawn 2 new, 0 again (0 of the "
                   "head), 0 still; switched off 2, left drawn 0, gave back 0; so far drawn again on 0 ticks (0 meshes), view on without hiding on 0 ticks"
                   "; veil: 0 leaves, 0 frames, eye moved clear on 0, left behind the head on 0, largest move 0.000 m, opens at the first head-camera frame"));
  /* The same again, on the full path and on the steady path (which reads no row): nothing to say. */
  t = tick(6, true, 0, 0, 2);
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  t.full = false; t.camera_rows = 0;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  /* The view leaves for the head camera and the tick finds 31 meshes drawn that it last saw hidden: the line, and the count. */
  t = tick(4, true, 9, 14, 8); t.on_again = 29; t.on_again_head = 9; t.turned_off = 29;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text));
  CHECK(same(text, "Head: kind=4 active: owns head 9, torso 14, shoulders 8 (34 rows; was head 0, torso 0, shoulders 2); found drawn 0 new, 29 again (9 of the "
                   "head), 0 still; switched off 29, left drawn 0, gave back 0; so far drawn again on 1 ticks (29 meshes), view on without hiding on 0 ticks"
                   "; veil: 0 leaves, 0 frames, eye moved clear on 0, left behind the head on 0, largest move 0.000 m, opens at the first head-camera frame"));
  CHECK(g.again_ticks == 1 && g.again_meshes == 29);
  /* Drawn again with nothing else changed is still a line: that is the tick this log is for. */
  t = tick(4, true, 9, 14, 8); t.on_again = 31; t.on_again_head = 9; t.turned_off = 31;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "31 again (9 of the head)") && strstr(text, "on 2 ticks (60 meshes)"));
  /* Left drawn: a line when the number changes, none while it stays, a steady tick between does not count as "none left". */
  t = tick(4, true, 0, 14, 8); t.on_again = 9; t.on_again_head = 9; t.left_on = 9;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "left drawn 9") && strstr(text, "owns head 0, torso 14, shoulders 8"));
  t = tick(4, true, 0, 14, 8); t.on_still = 9; t.left_on = 9;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  t.full = false; t.camera_rows = 0; t.on_still = t.left_on = 0;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  t = tick(6, true, 9, 14, 8); t.on_still = 9; t.turned_off = 9;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "9 still; switched off 9, left drawn 0"));
  /* Hiding stops while the view is on: a line, the ticks are counted, and the line that says it is back carries them. */
  t = tick(4, false, 0, 0, 0); t.put_back = 34;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text));
  CHECK(same(text, "Head: kind=4 idle: owns head 0, torso 0, shoulders 0 (0 rows; was head 9, torso 14, shoulders 8); found drawn 0 new, 0 again (0 of the "
                   "head), 0 still; switched off 0, left drawn 0, gave back 34; so far drawn again on 3 ticks (69 meshes), view on without hiding on 1 ticks"
                   "; veil: 0 leaves, 0 frames, eye moved clear on 0, left behind the head on 0, largest move 0.000 m, opens at the first head-camera frame"));
  t = tick(4, false, 0, 0, 0);
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text) && !tfp_head_log_step(&g, &t, text, sizeof text) && g.off_ticks == 3);
  t = tick(4, true, 9, 14, 8); t.on_again = 31; t.on_again_head = 9; t.turned_off = 31;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "kind=4 active") && strstr(text, "view on without hiding on 3 ticks"));
  /* The view goes off: the module stops, which is as it should be; one line for the rows given back, and silence after. */
  t = tick(0, false, 0, 0, 0); t.view = false; t.put_back = 34;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "gave back 34") && g.off_ticks == 3);
  t.put_back = 0;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  /* Rows that cannot be given back are tried again on every tick ("cleanup pending"): one line, not one a tick; a
   * different number is a line again. */
  t = tick(0, false, 0, 0, 0); t.view = false; t.phase = "cleanup_pending"; t.rows = 2; t.put_back = 2;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "kind=0 cleanup_pending") && strstr(text, "gave back 2"));
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text) && !tfp_head_log_step(&g, &t, text, sizeof text));
  t.put_back = 1; t.rows = 1;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "gave back 1"));
  /* Round 14, the veil (src/camera_helper.h tfp_camera_set_veil): its counts close the line; a frame left behind the
   * head while the body was drawn is a line by itself, whatever else the tick did. */
  t = tick(4, true, 9, 14, 8); t.on_again = 31; t.on_again_head = 5; t.turned_off = 31;
  t.veil_leaves = 3; t.veil_frames = 4; t.veil_moved = 2; t.veil_behind = 0;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "; veil: 3 leaves, 4 frames, eye moved clear on 2, left behind the head on 0"));
  t = tick(4, true, 9, 14, 8); t.veil_leaves = 3; t.veil_frames = 5; t.veil_moved = 2; t.veil_behind = 0;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  t.veil_frames = 6; t.veil_behind = 1;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "6 frames, eye moved clear on 2, left behind the head on 1"));
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  /* Round 19: the largest move the veil made (round 18: up to 0.32 m forward, and no line said it), and which way it
   * opens: at the first head-camera frame (the default) or at the tick that sees the leave (veil_second.txt). Neither
   * makes a line by itself. */
  t.veil_largest_m = .3214f;
  CHECK(!tfp_head_log_step(&g, &t, text, sizeof text));
  t.veil_behind = 2;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "left behind the head on 2, largest move 0.321 m, opens at the first head-camera frame"));
  t.veil_second = true; t.veil_behind = 3;
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strstr(text, "left behind the head on 3, largest move 0.321 m, opens at the tick that sees the leave (veil_second.txt)"));
  /* Round 24: with a clock, the line that says hiding is back says how long it was let go (a backpack's attach,
   * 20:32:09.585 to 09.783 in the user's log of 2026-11-24); without one (every tick above) nothing is added. */
  {
    TfpHeadLog h; memset(&h, 0, sizeof h);
    TfpHeadTick a = tick(4, true, 5, 11, 12); a.now_ms = 1000;
    CHECK(tfp_head_log_step(&h, &a, text, sizeof text) && !strstr(text, "let go for"));
    TfpHeadTick off = tick(6, false, 0, 0, 0); off.now_ms = 1585; off.phase = "unavailable";
    CHECK(tfp_head_log_step(&h, &off, text, sizeof text) && !strstr(text, "let go for"));
    off.now_ms = 1700; (void)tfp_head_log_step(&h, &off, text, sizeof text);
    a.now_ms = 1783; a.on_new = 37;
    CHECK(tfp_head_log_step(&h, &a, text, sizeof text) && strstr(text, "; hiding was let go for 198 ms with the view on"));
    a.now_ms = 1800; a.on_new = 0; a.on_again = 1;
    CHECK(tfp_head_log_step(&h, &a, text, sizeof text) && !strstr(text, "let go for"));
  }
  /* A line never runs past its buffer, and stays under the 700 characters say() writes (src/mod_view.c). */
  t = tick(4, true, 4000000000u, 4000000000u, 4000000000u); t.on_again = t.on_again_head = t.on_new = t.on_still = t.turned_off = t.left_on = t.put_back = 4000000000u;
  g.again_ticks = g.again_meshes = g.off_ticks = 18446744073709551615ull;
  t.veil_leaves = t.veil_frames = t.veil_moved = t.veil_behind = 18446744073709551615ull; t.veil_largest_m = 1e30f; t.veil_second = true;
  char small[40];
  CHECK(tfp_head_log_step(&g, &t, small, sizeof small) && strlen(small) == sizeof small - 1);
  CHECK(tfp_head_log_step(&g, &t, text, sizeof text) && strlen(text) < 700);
  if (failed) { printf("head log: %d of %d checks failed\n", failed, checks); return 1; }
  printf("head log: %d checks: when a tick is worth a \"Head:\" line, what it counts and its words\n", checks);
  return 0;
}
