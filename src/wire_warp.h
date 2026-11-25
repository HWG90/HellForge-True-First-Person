/* The binder of the Warp Pack module (src/warp_glow.h; docs/design/specs/2026-11-24-warp-pack-glow.md), feel build only.
 * Called once a tick on the game thread from src/mod_view.c, after head visibility's update: it takes a player
 * snapshot and the backpack through the backend, says whether TFP owns the view, and hands the module the raw memory
 * host (TfpWire.memory). */
#ifndef TFP_WIRE_WARP_H
#define TFP_WIRE_WARP_H
#include "wire.h"
#include "warp_glow.h"

/* owns: the view is on and head visibility is active this tick. armed: warp_hide.txt. `say` gets each line (no '\n'). */
void tfp_wire_warp_tick(TfpWire *w, TfpWarp *warp, bool owns, bool armed, double now, void (*say_line)(const char *line));
/* The mod stops: the restore, if one is owed. */
void tfp_wire_warp_release(TfpWire *w, TfpWarp *warp, void (*say_line)(const char *line));
#endif
