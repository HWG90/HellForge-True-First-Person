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
static char definition[65536];

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
/* "storage": true. MCM keeps no file for this mod: load answers no values, so every control starts at the definition's
 * default (the store's value), and save has nothing to do: `changed` has put the value into the store, whose next save
 * frame writes settings.json or locked_view.json. */
static int storage_load(void *user, char *buffer, size_t size) { (void)user; (void)buffer; (void)size; return 0; }
static int storage_save(void *user, const char *text, size_t length) { (void)user; (void)text; (void)length; return 1; }

bool tfp_mod_menu_registered(void) { return registered != NULL; }
/* "The settings menu" for the placement editor is whichever is in use: DBF-MCM's window or the mod's own panel. */
static bool mcm_is_open(void) { return mcm && mcm->is_open && mcm->is_open() != 0; }
bool tfp_mod_menu_is_open(void) { return mcm_is_open() || tfp_mod_panel_is_open(); }
bool tfp_mod_menu_close(void) {
  if (mcm_is_open() && mcm->close) mcm->close();
  tfp_mod_panel_close();
  return true;   /* the editor checks is_open again an update later, as retail's handoff does */
}
TfpMcmState tfp_mod_menu_try(const HfApi *hf, HfMod *mod, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) {
  if (registered) return TFP_MCM_READY;
  mcm = mcm_connect();
  if (!mcm) return TFP_MCM_WAITING;   /* NULL until MCM's first update: the menu may load after this mod */
  size_t length = tfp_wire_menu_definition(w, TFP_EDITOR_KEY, definition, sizeof definition);
  if (!length || length >= sizeof definition) { hf->log(mod, "Refused: menu: the definition does not fit (%u bytes)", (unsigned)length); return TFP_MCM_REFUSED; }
  McmCallbacks callbacks = {.size = sizeof callbacks, .changed = changed, .activate = activate, .load = storage_load, .save = storage_save};
  registered = mcm->register_mod(definition, length, &callbacks);
  if (!registered) { hf->log(mod, "Refused: menu: DBF-MCM did not take the settings (%s)", mcm->error()); return TFP_MCM_REFUSED; }
  bound = w;
  const TfpMenuHost host = {NULL, host_get, host_set, host_set_disabled, host_set_label, NULL, TFP_EDITOR_KEY};
  if (!tfp_wire_menu_bind(w, &host, unavailable, edit)) {
    hf->log(mod, "Refused: menu: the binder could not be set up; taking the registration out again");
    tfp_mod_menu_release(hf, mod, w);
    return TFP_MCM_REFUSED;
  }
  hf->log(mod, "Ready: settings menu: DBF-MCM (goose.mcm) holds it as \"%s\"; F10 opens it on this page (DEL opens MCM as always); no panel of "
               "the mod's own is made", TFP_MENU_MOD_ID);
  return TFP_MCM_READY;
}
static char show_why[200];
const char *tfp_mod_menu_show(bool *opened) {
  McmStatus status = {.size = sizeof status};
  if (!registered || !mcm) return "the settings are not registered with DBF-MCM right now";
  if (mcm->status(&status) && status.editing) /* a key or a text being typed: F10 belongs to it */
    return "DBF-MCM is taking a key, a text or a colour (F10 belongs to that)";
  if (mcm->is_open()) {
    *opened = false;
    if (mcm->close()) return NULL;
    snprintf(show_why, sizeof show_why, "DBF-MCM did not close its window (%s)", mcm->error ? mcm->error() : "no reason given");
    return show_why;
  }
  *opened = true;   /* opens the window on this mod's first page */
  if (mcm->focus_page(TFP_MENU_MOD_ID, tfp_wire_menu_page_ids[0])) return NULL;
  snprintf(show_why, sizeof show_why, "DBF-MCM did not open this mod's page (%s)", mcm->error ? mcm->error() : "no reason given");
  return show_why;
}
bool tfp_mod_menu_release(const HfApi *hf, HfMod *mod, TfpWire *w) {
  if (!registered) return true;
  if (!mcm->unregister(registered)) { hf->log(mod, "Refused: menu: DBF-MCM did not release the settings (%s)", mcm->error()); return false; }
  registered = NULL, bound = NULL;
  tfp_wire_menu_unbind(w);   /* a refused live replace registers again: the panel tick asks once a second */
  return true;
}
