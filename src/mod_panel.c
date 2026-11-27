/* See mod_panel.h. */
#include <windows.h>
#include <stdio.h>
#include "mod_panel.h"
#include "mod_menu.h"
#include "wire_panel.h"
#include "wire_win.h"

static SRWLOCK lock = SRWLOCK_INIT;
static const HfApi *hf_api;
static HfMod *hf_mod;
static TfpWire *bound;
static const char PANEL_ID[] = "goose.true-first-person/settings";
static char panel_id[96];   /* PANEL_ID@<process>-<tick>: this load's own */

static void p_lock(void *u) { (void)u; AcquireSRWLockExclusive(&lock); }
static void p_unlock(void *u) { (void)u; ReleaseSRWLockExclusive(&lock); }
/* As ov_connect(), but asking for the API the panel draws with: ov_connect() asks for the header's newest, and
 * the host answers NULL to a request above its own API, so hosts 0.4.0 and 0.5.0 would give no panel at all. */
static const OvApi *p_connect(void *u, uint32_t version) {
  (void)u;
  HMODULE host = GetModuleHandleA(OV_HOST_DLL);
  OvApiFn get = host ? (OvApiFn)(void *)GetProcAddress(host, "ov_api") : NULL;
  return get ? get(version) : NULL;
}
static TfpMcmState p_mcm(void *u, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) { (void)u; return tfp_mod_menu_try(hf_api, hf_mod, w, unavailable, edit); }
/* round 50: F10 through the tap reader (src/key_tap.h); the tap itself is only answered with the game in front */
static bool p_key(void *u, int vk) { (void)u; return tfp_win_focused() && tfp_win_key_level(vk); }
static bool p_tapped(void *u, int vk) { (void)u; return tfp_win_key_tapped(vk); }
static const char *p_show(void *u, bool *opened) { (void)u; return tfp_mod_menu_show(opened); }

void tfp_mod_panel_tick(const HfApi *hf, HfMod *mod, TfpWire *w, bool panel_forced, unsigned unavailable, TfpMenuEditFn edit) {
  ULONGLONG now = GetTickCount64();
  hf_api = hf, hf_mod = mod, bound = w;
  /* The panel's id is the same at every game start, so the host keeps the window's place and size and its config
   * can give the panel another key (hotkey.<id> in overlay-host.ini). But the host cannot take a panel out and
   * refuses a second one with the same id, and a replaced build of this mod leaves its panel behind: for that
   * case every load also has an id of its own, tried when the host does not take the first. */
  if (!panel_id[0])
    snprintf(panel_id, sizeof panel_id, "%s@%lx-%llx", PANEL_ID, (unsigned long)GetCurrentProcessId(), (unsigned long long)now);
  const TfpPanelPlatform platform = {NULL, p_lock, p_unlock, p_connect, p_mcm, panel_forced, PANEL_ID, panel_id, TFP_EDITOR_KEY, p_key, p_show, p_tapped};
  tfp_wire_panel_tick(w, &platform, now, unavailable, edit);
}
bool tfp_mod_panel_is_open(void) { return bound && tfp_wire_panel_is_open(bound); }
void tfp_mod_panel_close(void) { if (bound) tfp_wire_panel_close(bound); }
void tfp_mod_panel_retire(void) { if (bound) tfp_wire_panel_retire(bound); }
