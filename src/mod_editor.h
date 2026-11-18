/* The feel build's Windows half of the placement editor: the panel, the cursor and the mouse come from
 * Goose Overlay Host, the settings menu's open / close from DBF-MCM (src/mod_menu.c). The editor itself is
 * src/placement_editor.c behind src/wire_editor.c; nothing here decides anything about a placement.
 * docs/guides/POSER-AND-MENU.md. */
#ifndef TFP_MOD_EDITOR_H
#define TFP_MOD_EDITOR_H
#include "hellforge_mod.h"
#include "wire_editor.h"
#include "wire_effects.h"

/* The platform handed to tfp_wire_feel_bind. `window`: the effects platform, for stingray.Window's two questions. */
const TfpEditorPlatform *tfp_mod_editor_platform(const TfpFxPlatform *window);
/* Every tick: looks for the overlay host (a try a second for 15 s, then one line) and registers the panel once.
 * editor_key: the key that opens and closes the editor, which the editor itself must not see as "depth up". */
void tfp_mod_editor_tick(const HfApi *hf, HfMod *mod, int editor_key);
bool tfp_mod_editor_available(void);
bool tfp_mod_editor_settled(void);   /* the panel is registered, or the search for the host is over */
/* A live replace: the panel is closed and draws nothing from here on (the overlay host cannot take it out). Any thread. */
void tfp_mod_editor_retire(void);
#endif
