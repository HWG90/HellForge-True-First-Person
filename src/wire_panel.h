/* The mod's own settings panel, drawn through NEST (Goose Overlay Host): every control of the menu
 * (src/wire_menu.c: retail's two pages) and the locked view's options (TFP_SETTINGS_LOCKED), on three tabs.
 * The mod no longer needs DBF-MCM; that registration stays behind a switch (TfpPanelPlatform.mcm_wanted).
 *
 * How a change travels. The panel is a TfpMenuHost, as src/mod_menu.c is for DBF-MCM: it keeps what each
 * control shows, and hands what the user did to tfp_wire_menu_changed / tfp_wire_menu_activate. So a change
 * made here is applied and saved by the same code as a change made in MCM. The locked-view options are
 * not menu controls; they go to tfp_wire_settings_locked_set. Either way the store counts a change and
 * tfp_wire_settings_save_frame writes the file once.
 *
 * Two threads. The host calls the draw callback on its render thread; everything else here is the game
 * thread's. They share the shown values and a short queue of what the user did, under the platform's lock.
 * The draw callback touches neither the settings store nor the runtime: tfp_wire_panel_frame does, at the
 * next game update. A slider is taken when it is let go, not on every step of a drag.
 *
 * No Windows here: src/mod_panel.c supplies the lock, the clock and the host's export; tests/wire_panel_test.c a
 * stand-in host. Colours come from ov->theme() only (HD2-UI-Style/DESIGN.md). */
#ifndef TFP_WIRE_PANEL_H
#define TFP_WIRE_PANEL_H
#include "wire_menu.h"
#include "goose_overlay.h"

enum { TFP_PANEL_KEY = 0x79, /* F10 (VK_F10): OvPanelDef.hotkey; the host's own config can override it */
       TFP_PANEL_API = 6 };  /* the oldest host API the panel draws with (host 0.4.0: key_cap, button_primary) */

typedef struct TfpPanelPlatform {
  void *user;
  void (*lock)(void *user);                    /* one lock, not taken twice by one thread */
  void (*unlock)(void *user);
  /* The host's export (ov_api) asked for API `version`: NULL while the host is not loaded, or is older than that.
   * Not ov_connect(), which asks for the header's newest API and so gets NULL from every older host. */
  const OvApi *(*connect)(void *user, uint32_t version);
  /* DBF-MCM's registration (tfp_mod_menu_tick). Called only when mcm_wanted; may be NULL. */
  void (*mcm_tick)(void *user, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit);
  bool mcm_wanted;                             /* the switch: the menu goes to DBF-MCM and no panel is made. Default: false */
  const char *panel_id;                        /* the same at every game start: the host keeps the window's place and size, and a hotkey override, by it */
  /* Unique per load, or NULL. The host cannot take a panel out and refuses a second one with the same id, and a
   * replaced build of this mod leaves its panel behind: when the host does not take panel_id, this one is tried. */
  const char *panel_id_reload;
  int editor_key;                              /* what a stored 119 (retail's F8) means here: Page Up */
} TfpPanelPlatform;

/* Every game update while the mode runs, before tfp_wire_panel_frame. Without the switch: looks for the host
 * (a try a second for 15 s, then one line and no more tries) and binds once; a refusal is one line and final.
 * With it: only p->mcm_tick. now_ms: any millisecond clock that does not go back. */
void tfp_wire_panel_tick(TfpWire *w, const TfpPanelPlatform *p, uint64_t now_ms, unsigned unavailable, TfpMenuEditFn edit);
/* What the tick does once it has the host; `unavailable` and `edit` as tfp_wire_menu_bind takes them. false,
 * with one `Refused: settings panel: ...` line: the host is older than TFP_PANEL_API or lacks a function, the
 * settings are not bound, the menu is already bound elsewhere, or the host did not take the panel. */
bool tfp_wire_panel_bind(TfpWire *w, const OvApi *ov, const TfpPanelPlatform *p, unsigned unavailable, TfpMenuEditFn edit);
bool tfp_wire_panel_bound(TfpWire *w);
/* Once per game update, before tfp_wire_menu_frame: what the user did in the panel since the last one. */
void tfp_wire_panel_frame(TfpWire *w);
/* The window itself, for the placement editor (it closes other menus before it takes the cursor). */
bool tfp_wire_panel_is_open(TfpWire *w);
void tfp_wire_panel_close(TfpWire *w);
/* A live replace: closed, and drawing nothing from here on (the host keeps the registration). Any thread. */
void tfp_wire_panel_retire(TfpWire *w);
/* Frees the state. Only when the host can no longer call the draw callback (tests; never in the game). */
void tfp_wire_panel_free(TfpWire *w);
#endif
