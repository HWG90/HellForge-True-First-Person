/* Which menu a session gets, and the mod's own settings panel.
 *
 * The rule (tfp_wire_panel_tick). DBF-MCM first: while goose.mcm has not answered, it is tried once a second for 15 s;
 * once it takes the registration, the whole menu (src/wire_menu.c: every option, three pages) is there and no panel of
 * the mod's own is made. F10 then opens MCM's window on this mod's first page, or closes it. When MCM is not loaded or
 * not ready within those 15 s, or refuses the registration, the same pages and options go to the mod's own panel,
 * drawn through NEST (Goose Overlay Host), with its own search of 15 s; F10 is the panel's key. A file panel_on.txt
 * beside the DLL (TfpPanelPlatform.panel_forced) skips MCM: the panel at once. With neither, one line says that no
 * menu is available and that the settings files can be edited by hand.
 *
 * How a change travels. The panel is a TfpMenuHost, as src/mod_menu.c is for DBF-MCM: it keeps what each
 * control shows, and hands what the user did to tfp_wire_menu_changed / tfp_wire_menu_activate. So a change
 * made here is applied and saved by the same code as a change made in MCM, the locked view's options included
 * (they are controls of the list's third page). The store counts a change and tfp_wire_settings_save_frame
 * writes the file once.
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

/* One try at DBF-MCM's registration (src/mod_menu.c). WAITING: MCM is not loaded, or not ready yet (no McmApi
 * before its first update); READY: registered (also when it already was); REFUSED: MCM is there and did not take it
 * (its own line says why), and it is not tried again. */
typedef enum TfpMcmState { TFP_MCM_WAITING, TFP_MCM_READY, TFP_MCM_REFUSED } TfpMcmState;
/* What the tick decided. */
typedef enum TfpMenuChoice { TFP_CHOICE_DECIDING, TFP_CHOICE_MCM, TFP_CHOICE_PANEL, TFP_CHOICE_NONE } TfpMenuChoice;
enum { TFP_MENU_CONNECT_MS = 15000, TFP_MENU_TRY_MS = 1000 };   /* each search: a try a second for 15 s */

typedef struct TfpPanelPlatform {
  void *user;
  void (*lock)(void *user);                    /* one lock, not taken twice by one thread */
  void (*unlock)(void *user);
  /* The host's export (ov_api) asked for API `version`: NULL while the host is not loaded, or is older than that.
   * Not ov_connect(), which asks for the header's newest API and so gets NULL from every older host. */
  const OvApi *(*connect)(void *user, uint32_t version);
  /* DBF-MCM's registration, one try (tfp_mod_menu_try). NULL: there is no MCM, the panel at once. */
  TfpMcmState (*mcm_try)(void *user, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit);
  bool panel_forced;                           /* the test file panel_on.txt: MCM is not tried */
  const char *panel_id;                        /* the same at every game start: the host keeps the window's place and size, and a hotkey override, by it */
  /* Unique per load, or NULL. The host cannot take a panel out and refuses a second one with the same id, and a
   * replaced build of this mod leaves its panel behind: when the host does not take panel_id, this one is tried. */
  const char *panel_id_reload;
  int editor_key;                              /* what a stored 119 (retail's F8) means here: Page Up */
  /* While MCM holds the menu: F10 (TFP_PANEL_KEY), polled, opens MCM on this mod's page or closes it. Either NULL:
   * F10 does nothing then. */
  bool (*key_down)(void *user, int virtual_key);
  /* Round 50: MCM's window opened on this mod's page (*opened true) or closed (false): NULL. Not done: why. Each F10
   * press handled gets one log line, so a log shows the request was seen:
   *   Menu: F10: opening DBF-MCM on this page / Menu: F10: closing DBF-MCM / Menu: F10: refused: <why> */
  const char *(*mcm_show)(void *user, bool *opened);
  /* Round 50, may be NULL: the key is up now but was pressed and let go since the previous update (src/key_tap.h). */
  bool (*key_tapped)(void *user, int virtual_key);
} TfpPanelPlatform;

/* Every game update while the mode runs, before tfp_wire_panel_frame: the rule above. now_ms: any millisecond clock
 * that does not go back. While MCM holds the menu, MCM is asked again once a second (a released registration, after a
 * refused live replace, is made again); a REFUSED answer then hands the menu to the panel. */
void tfp_wire_panel_tick(TfpWire *w, const TfpPanelPlatform *p, uint64_t now_ms, unsigned unavailable, TfpMenuEditFn edit);
TfpMenuChoice tfp_wire_panel_choice(TfpWire *w);
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
