/* See mod_panel.h. */
#include <windows.h>
#include <stdio.h>
#include "mod_panel.h"
#include "mod_menu.h"
#include "wire_panel.h"

static SRWLOCK lock = SRWLOCK_INIT;
static const HfApi *hf_api;
static HfMod *hf_mod;
static TfpWire *bound;
static char panel_id[96];

static void p_lock(void *u) { (void)u; AcquireSRWLockExclusive(&lock); }
static void p_unlock(void *u) { (void)u; ReleaseSRWLockExclusive(&lock); }
static const OvApi *p_connect(void *u) { (void)u; return ov_connect(); }
static void p_mcm(void *u, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) { (void)u; tfp_mod_menu_tick(hf_api, hf_mod, w, unavailable, edit); }

void tfp_mod_panel_tick(const HfApi *hf, HfMod *mod, TfpWire *w, bool mcm_wanted, unsigned unavailable, TfpMenuEditFn edit) {
  ULONGLONG now = GetTickCount64();
  hf_api = hf, hf_mod = mod, bound = w;
  /* The host cannot take a panel out and refuses a second one with the same id, and a replaced build of this
   * mod leaves its panel behind: every load registers under an id of its own (as src/mod_editor.c does). */
  if (!panel_id[0])
    snprintf(panel_id, sizeof panel_id, "goose.true-first-person/settings@%lx-%llx", (unsigned long)GetCurrentProcessId(), (unsigned long long)now);
  const TfpPanelPlatform platform = {NULL, p_lock, p_unlock, p_connect, p_mcm, mcm_wanted, panel_id, TFP_EDITOR_KEY};
  tfp_wire_panel_tick(w, &platform, now, unavailable, edit);
}
bool tfp_mod_panel_is_open(void) { return bound && tfp_wire_panel_is_open(bound); }
void tfp_mod_panel_close(void) { if (bound) tfp_wire_panel_close(bound); }
void tfp_mod_panel_retire(void) { if (bound) tfp_wire_panel_retire(bound); }
