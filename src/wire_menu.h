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
  const char *name;         /* what the log calls this menu; NULL: DBF-MCM */
  int editor_key;           /* the key this host's "editor_key" control was given for retail's 119 (tfp_wire_menu_default); 0: not said */
} TfpMenuHost;
/* Features this build cannot offer: their controls are shown disabled and one log line says why. */
enum { TFP_MENU_NO_ARMS = 1u, TFP_MENU_NO_EDITOR = 2u };
/* The editor's toggle for the "Edit arms and weapon placement" button: NULL done, else what the footer shows. */
typedef const char *(*TfpMenuEditFn)(TfpWire *w);

/* The controls themselves, for a front end that draws them itself (src/wire_panel.c). TFP_MK_CHECK is a toggle
 * shown as a checkbox; TFP_MK_STATUS a row of text whose label changes (set_label). */
typedef enum TfpMenuKind { TFP_MK_BUTTON, TFP_MK_TOGGLE, TFP_MK_CHECK, TFP_MK_SLIDER, TFP_MK_CHOICE, TFP_MK_KEY, TFP_MK_STATUS } TfpMenuKind;
typedef struct TfpMenuControl {
  const char *id; TfpMenuKind kind; const char *label;
  double min, max, step;    /* TFP_MK_SLIDER */
  int menu;                 /* TfpSettingsMenuId the value is stored under, or -1 */
  const char *description;  /* NULL: none */
  int page;                 /* an index into tfp_wire_menu_pages */
} TfpMenuControl;
const TfpMenuControl *tfp_wire_menu_controls(int *count);
extern const char *const tfp_wire_menu_pages[2];    /* "First person", "Bob and sway" */
extern const char *const tfp_wire_menu_presets[3];  /* TFP_MK_CHOICE: the values 1, 2, 3 */
/* What a control shows before the user touches it, as the definition below states it: the store's value (a
 * slider's inside its range, the editor key's retail 119 as `editor_key`). false: a button or a status row,
 * or settings are not bound. */
bool tfp_wire_menu_default(TfpWire *w, const TfpMenuControl *c, int editor_key, TfpMenuValue *out);

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
/* The press of a bound key, for the caller's poll: true once when `down` turns true. A key that has just become the
 * bound one (the user pressed it to bind it, so it is still down) does nothing until it has been seen up once.
 * Zero-initialised to start. */
typedef struct TfpKeyEdge { int key; bool held; } TfpKeyEdge;
bool tfp_wire_menu_key_edge(TfpKeyEdge *edge, int key, bool down);
#endif
