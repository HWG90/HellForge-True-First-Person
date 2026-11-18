/* The first-person pages of retail's menu (src/first_person/menu.lua:343-418) on Goose MCM for HellForge.
 *
 * No Windows and no MCM types here: src/mod_view.c wraps the loaded menu's McmApi in a TfpMenuHost and
 * forwards its callbacks. The control list, labels, ranges, steps and descriptions are retail's; what
 * differs on this build is in docs/guides/POSER-AND-MENU.md.
 *
 * Where a value lives: MCM keeps each control's value in its own file once the user has changed it.
 * The definition's defaults are the store's values (settings.json: the user's retail menu values on a
 * first run), and after registering every value MCM answers is put into the store, so the translated
 * units read the same value the menu shows. */
#ifndef TFP_WIRE_MENU_H
#define TFP_WIRE_MENU_H
#include "wire_settings.h"

#define TFP_MENU_MOD_ID "true_first_person"
enum { TFP_MENU_VALUE_NONE, TFP_MENU_VALUE_BOOL, TFP_MENU_VALUE_NUMBER };
typedef struct TfpMenuValue { int kind; bool boolean; double number; } TfpMenuValue;
typedef struct TfpMenuHost {
  void *user;
  bool (*get)(void *user, const char *id, TfpMenuValue *out);       /* the saved value */
  bool (*set)(void *user, const char *id, const TfpMenuValue *value); /* saves; MCM calls `changed` back */
  bool (*set_disabled)(void *user, const char *id, bool disabled);
  bool (*set_label)(void *user, const char *id, const char *label);
} TfpMenuHost;
/* Features this build cannot offer: their controls are shown disabled and one log line says why. */
enum { TFP_MENU_NO_ARMS = 1u, TFP_MENU_NO_EDITOR = 2u };
/* The editor's toggle for the "Edit arms and weapon placement" button: NULL done, else what the footer shows. */
typedef const char *(*TfpMenuEditFn)(TfpWire *w);

/* The retail default of the editor key is F8 (119), which is HellForge's own editor here: a stored 119
 * becomes `editor_key`. Returns the JSON's length; >= size: it did not fit. Settings must be bound. */
size_t tfp_wire_menu_definition(TfpWire *w, int editor_key, char *out, size_t size);
/* After MCM accepted the definition. false: out of memory or settings not bound. */
bool tfp_wire_menu_bind(TfpWire *w, const TfpMenuHost *host, unsigned unavailable, TfpMenuEditFn edit);
void tfp_wire_menu_unbind(TfpWire *w);   /* the registration is gone (live replace) */
bool tfp_wire_menu_bound(TfpWire *w);
/* MCM's callbacks, on the game thread. */
void tfp_wire_menu_changed(TfpWire *w, const char *id, const TfpMenuValue *value, const TfpMenuValue *previous);
const char *tfp_wire_menu_activate(TfpWire *w, const char *id);
/* Once per tick after the runtime's update: the status and weapon labels and the per-weapon clipping
 * drafts, each sent to MCM only when it changed. */
void tfp_wire_menu_frame(TfpWire *w);
/* The two keys (virtual-key codes) as the menu holds them. */
int tfp_wire_menu_toggle_key(TfpWire *w);
int tfp_wire_menu_editor_key(TfpWire *w, int fallback);
#endif
