/* See wire_warp.h. */
#include "wire_warp.h"
#include <string.h>

static void lines(char *text, void (*say_line)(const char *line)) {
  for (char *at = text; *at;) {
    char *end = strchr(at, '\n');
    if (end) *end = 0;
    if (*at) say_line(at);
    if (!end) break;
    at = end + 1;
  }
}
void tfp_wire_warp_tick(TfpWire *w, TfpWarp *warp, bool owns, bool armed, double now, void (*say_line)(const char *line)) {
  TfpWarpIn in;
  TfpOpsSnapshot s;
  TfpOpsText why;
  char text[1400];
  memset(&in, 0, sizeof in);
  in.owns = owns, in.armed = armed, in.now = now, in.backpack = -1;
  if (tfp_ops_player_snapshot(&w->ops, &s, &why) == TFP_OPS_TRUE) {
    TfpOpsBackpack pack;
    memset(&pack, 0, sizeof pack);
    in.player = true, in.avatar_owner = s.avatar_owner, in.avatar_index = s.avatar_index;
    TfpOpsResult r = tfp_ops_backpack(&w->ops, &s, &pack, &why);
    in.backpack = r == TFP_OPS_TRUE ? 1 : r == TFP_OPS_NIL ? 0 : -1;
    if (r == TFP_OPS_TRUE) in.pack = pack.entity, memcpy(in.pack_type, pack.descriptor, 8);
  }
  if (tfp_warp_tick(warp, &w->memory, &in, text, sizeof text)) lines(text, say_line);
}
void tfp_wire_warp_release(TfpWire *w, TfpWarp *warp, void (*say_line)(const char *line)) {
  if (tfp_warp_release(warp, &w->memory)) say_line("Warp: restored at the stop: the pack's cached mode word set to 0xffffffff (the game plays its effects again)");
}
