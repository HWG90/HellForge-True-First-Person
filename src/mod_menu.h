/* The feel build's Windows glue to DBF-MCM (goose.mcm): finds the loaded menu, registers the whole menu of
 * src/wire_menu.c (every option, three pages, "storage": true) and forwards its callbacks. Whether it is tried at all,
 * and how often: src/wire_panel.h (tfp_wire_panel_tick). Game thread only. */
#ifndef TFP_MOD_MENU_H
#define TFP_MOD_MENU_H
#include "hellforge_mod.h"
#include "wire_menu.h"
#include "wire_panel.h"

/* The default key of the placement editor: Page Up (VK_PRIOR). Retail's F8 is HellForge's own editor here. */
#define TFP_EDITOR_KEY 0x21
/* One try: TFP_MCM_READY registered (also when it already was), TFP_MCM_WAITING MCM not loaded or not ready yet,
 * TFP_MCM_REFUSED MCM did not take it (one Refused line). */
TfpMcmState tfp_mod_menu_try(const HfApi *hf, HfMod *mod, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit);
/* F10 while MCM holds the menu: MCM's window on this mod's first page (*opened true), or closed when it is open
 * (*opened false): NULL. Not done (MCM is taking a key or text, nothing is registered, MCM refused): why, for the
 * "Menu: F10: refused: ..." line (round 50). */
const char *tfp_mod_menu_show(bool *opened);
/* Round 50c: DBF-MCM is taking a key, a text or a colour now (its status's editing); false without MCM. */
bool tfp_mod_menu_editing(void);
/* Takes the registration out of MCM (a live replace). true: nothing is registered any more. */
bool tfp_mod_menu_release(const HfApi *hf, HfMod *mod, TfpWire *w);
bool tfp_mod_menu_registered(void);
/* DBF-MCM's own window, for the placement editor: closed (true also when MCM is absent), and whether it is open. */
bool tfp_mod_menu_close(void);
bool tfp_mod_menu_is_open(void);
#endif
