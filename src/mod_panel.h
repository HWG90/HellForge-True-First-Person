/* The feel build's Windows half of the settings menu's choice and of the mod's own panel (src/wire_panel.c): the lock
 * between the game thread and the overlay host's render thread, the clock, the host's export asked for the API the
 * panel needs, DBF-MCM's try (src/mod_menu.c) and F10. DBF-MCM first, the panel without it: src/wire_panel.h,
 * docs/guides/POSER-AND-MENU.md. */
#ifndef TFP_MOD_PANEL_H
#define TFP_MOD_PANEL_H
#include "hellforge_mod.h"
#include "wire_menu.h"

/* Every tick while the mode runs, once the placement editor's search for the host is over (so the menu knows
 * whether to offer the editor). panel_forced: the test switch panel_on.txt beside the DLL. Game thread. */
void tfp_mod_panel_tick(const HfApi *hf, HfMod *mod, TfpWire *w, bool panel_forced, unsigned unavailable, TfpMenuEditFn edit);
/* The panel's window, for the placement editor. Game thread. */
bool tfp_mod_panel_is_open(void);
void tfp_mod_panel_close(void);
/* A live replace: the panel is closed and draws nothing from here on. Any thread. */
void tfp_mod_panel_retire(void);
#endif
