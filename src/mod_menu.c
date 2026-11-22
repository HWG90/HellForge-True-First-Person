/* See mod_menu.h. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "mod_menu.h"
#include "mod_panel.h"
#include "goose_mcm.h"

static const McmApi *mcm;
static McmMod *registered;
static TfpWire *bound;
static ULONGLONG first_try, next_try;
static bool gave_up;
static char definition[24576];

static void to_value(const McmValue *in, TfpMenuValue *out) {
  out->kind = in->type == MCM_BOOL ? TFP_MENU_VALUE_BOOL : in->type == MCM_NUMBER ? TFP_MENU_VALUE_NUMBER : TFP_MENU_VALUE_NONE;
  out->boolean = in->boolean != 0, out->number = in->number;
}
static bool host_get(void *user, const char *id, TfpMenuValue *out) {
  McmValue value;
  (void)user;
  if (!registered || !mcm->get(registered, id, &value)) return false;
  to_value(&value, out);
  return out->kind != TFP_MENU_VALUE_NONE;
}
static bool host_set(void *user, const char *id, const TfpMenuValue *value) {
  McmValue v = {value->kind == TFP_MENU_VALUE_BOOL ? MCM_BOOL : MCM_NUMBER, value->boolean, value->number, NULL};
  (void)user;
  return registered && mcm->set(registered, id, &v);
}
static bool host_set_disabled(void *user, const char *id, bool disabled) {
  (void)user;
  if (!registered) return false;
  return mcm->set_disabled(registered, id, disabled) || !*mcm->error(); /* 0 with no error: it was so already */
}
static bool host_set_label(void *user, const char *id, const char *label) { (void)user; return registered && mcm->set_label(registered, id, label); }

static void changed(void *user, const char *id, const McmValue *value, const McmValue *previous) {
  TfpMenuValue now, before = {TFP_MENU_VALUE_NONE, false, 0};
  (void)user;
  if (!bound || !value) return;
  to_value(value, &now);
  if (previous) to_value(previous, &before);
  tfp_wire_menu_changed(bound, id, &now, &before);
}
static const char *activate(void *user, const char *id) { (void)user; return bound ? tfp_wire_menu_activate(bound, id) : "The mod is not ready"; }

bool tfp_mod_menu_registered(void) { return registered != NULL; }
/* "The settings menu" for the placement editor is whichever is in use: DBF-MCM's window or the mod's own panel. */
static bool mcm_is_open(void) { return mcm && mcm->is_open && mcm->is_open() != 0; }
bool tfp_mod_menu_is_open(void) { return mcm_is_open() || tfp_mod_panel_is_open(); }
bool tfp_mod_menu_close(void) {
  if (mcm_is_open() && mcm->close) mcm->close();
  tfp_mod_panel_close();
  return true;   /* the editor checks is_open again an update later, as retail's handoff does */
}
void tfp_mod_menu_tick(const HfApi *hf, HfMod *mod, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) {
  ULONGLONG now = GetTickCount64();
  if (registered || gave_up || now < next_try) return;
  if (!first_try) first_try = now;
  next_try = now + 1000;
  mcm = mcm_connect();
  if (!mcm) {
    if (now - first_try < 15000) return; /* NULL until MCM's first update: the menu may load after this mod */
    gave_up = true;
    hf->log(mod, "Menu: DBF-MCM (goose.mcm) is not loaded, so there is no settings menu; the settings in settings.json apply");
    return;
  }
  size_t length = tfp_wire_menu_definition(w, TFP_EDITOR_KEY, definition, sizeof definition);
  if (!length || length >= sizeof definition) { gave_up = true; hf->log(mod, "Refused: menu: the definition does not fit (%u bytes)", (unsigned)length); return; }
  McmCallbacks callbacks = {.size = sizeof callbacks, .changed = changed, .activate = activate};
  registered = mcm->register_mod(definition, length, &callbacks);
  if (!registered) { gave_up = true; hf->log(mod, "Refused: menu: DBF-MCM did not take the settings (%s); the settings in settings.json apply", mcm->error()); return; }
  bound = w;
  const TfpMenuHost host = {NULL, host_get, host_set, host_set_disabled, host_set_label, NULL};
  if (!tfp_wire_menu_bind(w, &host, unavailable, edit)) {
    hf->log(mod, "Refused: menu: the binder could not be set up; taking the registration out again");
    tfp_mod_menu_release(hf, mod, w);
    gave_up = true;
  }
}
bool tfp_mod_menu_release(const HfApi *hf, HfMod *mod, TfpWire *w) {
  if (!registered) return true;
  if (!mcm->unregister(registered)) { hf->log(mod, "Refused: menu: DBF-MCM did not release the settings (%s)", mcm->error()); return false; }
  registered = NULL, bound = NULL;
  tfp_wire_menu_unbind(w);
  first_try = next_try = 0, gave_up = false; /* a refused live replace registers again */
  return true;
}
