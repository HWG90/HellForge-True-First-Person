/* The feel build's Windows glue to DBF-MCM (goose.mcm): finds the loaded menu, registers the first-person
 * pages of src/wire_menu.c and forwards its callbacks. Game thread only. */
#ifndef TFP_MOD_MENU_H
#define TFP_MOD_MENU_H
#include "hellforge_mod.h"
#include "wire_menu.h"

/* The default key of the placement editor: Page Up (VK_PRIOR). Retail's F8 is HellForge's own editor here. */
#define TFP_EDITOR_KEY 0x21
/* Every tick while the mode runs, before the binder's frame: connects (a try a second for 15 s, then one
 * line and no more tries) and registers once. */
void tfp_mod_menu_tick(const HfApi *hf, HfMod *mod, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit);
/* Takes the registration out of MCM (a live replace). true: nothing is registered any more. */
bool tfp_mod_menu_release(const HfApi *hf, HfMod *mod, TfpWire *w);
bool tfp_mod_menu_registered(void);
#endif
