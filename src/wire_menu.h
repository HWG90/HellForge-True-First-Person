/* The settings menu: one list of every user-facing option (TfpMenuControl, three pages), the single source of both
 * menus that show it: DBF-MCM for HellForge (src/mod_menu.c registers it as a JSON definition) and the mod's own
 * panel through NEST (src/wire_panel.c draws it). Which of the two a session gets: src/wire_panel.h.
 *
 * Retail's first-person pages (src/first_person/menu.lua:343-418) are the first two pages, with retail's labels,
 * ranges, steps and descriptions; the third, "Locked view", holds the options of locked_view.json
 * (TFP_SETTINGS_LOCKED), which retail does not have. What differs on this build is in docs/guides/POSER-AND-MENU.md.
 *
 * No Windows and no MCM types here: src/mod_view.c wraps the loaded menu's McmApi in a TfpMenuHost and
 * forwards its callbacks; the panel is a TfpMenuHost of its own.
 *
 * Where a value lives: in this mod's files only, settings.json (the "menu" member, and first_person for the two
 * controls retail saves there) and locked_view.json. MCM is registered with "storage": true and keeps no file of
 * its own for this mod: its load answers "no values", so every control starts at the definition's default, which is
 * the store's value, and its save is the store's next save frame. */
#ifndef TFP_WIRE_MENU_H
#define TFP_WIRE_MENU_H
#include "wire_settings.h"

/* The MCM id. Not "goose_true_first_person" (an old Lua experiment's page, whose saved values MCM may still hold),
 * not "true_first_person" (the id builds up to 0.6.0-feel.50 used behind mcm_on.txt, with two pages) and not
 * "first_person" (retail's): with "storage": true MCM reads no file for it anyway, and a new id keeps any old file,
 * and any registration still made under an old id, apart from this page. */
#define TFP_MENU_MOD_ID "goose_tfp"
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

/* The controls themselves. TFP_MK_CHECK is a toggle shown as a checkbox; TFP_MK_STATUS a row of text whose label
 * changes (set_label). Where a control's value is stored is one of three, never two: `menu` (settings.json "menu"),
 * `locked` (locked_view.json) or `config` (settings.json "first_person", through the runtime's own save); a button
 * and a status row store nothing. */
typedef enum TfpMenuKind { TFP_MK_BUTTON, TFP_MK_TOGGLE, TFP_MK_CHECK, TFP_MK_SLIDER, TFP_MK_CHOICE, TFP_MK_KEY, TFP_MK_STATUS } TfpMenuKind;
enum { TFP_MENU_PAGES = 3 };
typedef struct TfpMenuControl {
  const char *id; TfpMenuKind kind; const char *label;
  double min, max, step;    /* TFP_MK_SLIDER */
  int menu;                 /* TfpSettingsMenuId the value is stored under, or -1 */
  const char *description;  /* NULL: none */
  int page;                 /* an index into tfp_wire_menu_pages */
  int locked;               /* TfpSettingsLockedId the value is stored under, or -1 */
  int config;               /* TfpSettingsField (first_person) the runtime saves it under, or -1 */
  const char *const *choices; int choice_count; /* TFP_MK_CHOICE: the values 1 .. choice_count. Over a Boolean option: 1 off, 2 on */
  /* A line shown under the row: while the row is disabled (note_when 0), or while a choice shows value note_when.
   * MCM has no such line: there an inert control's note, and a choice's, ends the description. */
  const char *note; int note_when;
  bool bodycam_only;        /* disabled while the camera style is Stable (src/camera_damp.h) */
  bool aim_chosen_only;     /* disabled unless Aim field of view is A chosen value (round 20) */
  bool inert;               /* stored, and nothing on this build acts on it: always disabled, with its note */
  bool see_through_only;    /* round 45: disabled while See through the exosuit cockpit is off */
  bool frame_only;          /* round 45: disabled unless the cockpit is seen through and Cockpit frame is on */
} TfpMenuControl;
const TfpMenuControl *tfp_wire_menu_controls(int *count);
extern const char *const tfp_wire_menu_pages[TFP_MENU_PAGES];     /* "First person", "Bob and sway", "Locked view" */
extern const char *const tfp_wire_menu_page_ids[TFP_MENU_PAGES];  /* MCM's page ids */
extern const char *const tfp_wire_menu_page_intro[TFP_MENU_PAGES]; /* a line at the top of the page, or NULL */
extern const char *const tfp_wire_menu_aim_fields[3]; /* fov_aim's choices: the values 1, 2, 3 */
extern const char *const tfp_wire_menu_presets[3];  /* motion_preset's choices: the values 1, 2, 3 */
/* Every key of settings.json and locked_view.json that is no control, and why: placements the editor writes, values
 * retail's runtime keeps, and what no unit of this build reads. Together with the controls this names every key of
 * both files once. file: "settings.json first_person", "settings.json menu" or "locked_view.json". */
typedef struct TfpMenuNotOffered { const char *file, *key, *why; } TfpMenuNotOffered;
const TfpMenuNotOffered *tfp_wire_menu_not_offered(int *count);
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
/* MCM's callbacks, on the game thread. A stored control's value goes to its store at once (src/wire_settings.h:
 * tfp_wire_settings_menu_set / tfp_wire_settings_locked_set); the two `config` toggles and the buttons are done at the
 * next tfp_wire_menu_frame. */
void tfp_wire_menu_changed(TfpWire *w, const char *id, const TfpMenuValue *value, const TfpMenuValue *previous);
const char *tfp_wire_menu_activate(TfpWire *w, const char *id);
/* Once per tick after the runtime's update: the status and weapon labels, the per-weapon clipping drafts and the
 * Bodycam-only sliders' disabled state, each sent to the host only when it changed. */
void tfp_wire_menu_frame(TfpWire *w);
/* The two keys (virtual-key codes) as the menu holds them. The toggle key: a stored 122 (F11, retail's registered default
 * and what a settings.json of round 35 and older holds for a player who never changed it) is TFP_TOGGLE_KEY, U, since
 * HellForge's Armor LUT editor has F11; no store: U too. 0: unbound. toggle_moved: the stored key is that old F11. */
int tfp_wire_menu_toggle_key(TfpWire *w);
bool tfp_wire_menu_toggle_moved(TfpWire *w);
/* A key's name as the menus show it ("U", "F11", "PAGE UP", "MOUSE 4", "KEY 200"). */
void tfp_wire_menu_key_name(int key, char out[16]);
int tfp_wire_menu_editor_key(TfpWire *w, int fallback);
/* The press of a bound key, for the caller's poll: true once when `down` turns true. A key that has just become the
 * bound one (the user pressed it to bind it, so it is still down) does nothing until it has been seen up once.
 * Zero-initialised to start. */
typedef struct TfpKeyEdge { int key; bool held; } TfpKeyEdge;
bool tfp_wire_menu_key_edge(TfpKeyEdge *edge, int key, bool down);
#endif
