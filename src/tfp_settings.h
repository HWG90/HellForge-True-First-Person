/* The saved settings of the first-person mode: what retail's `mod.config` and `mod.set` answer
 * (src/first_person.lua:12-24 of the kit at 0.5.41-smooth.66, commit 1d0d453), plus the values of
 * the first-person menu controls that retail reads through the menu framework's `handle.get`
 * (menu.lua:144-242). Loaded from the text of `settings.json`, which tools/convert_settings.py
 * makes from a retail settings snapshot at build time.
 *
 * No Windows, no game, no dependency: the JSON reader is in tfp_settings.c. A missing or invalid
 * text leaves retail's defaults. Writes go to memory only; tfp_settings_write gives the same JSON
 * shape back, and src/wire_settings.c hands it to whoever keeps the file.
 *
 * A saved table is a TfpSettingsValue the store made and owns. A value never changes once made: a
 * write swaps the field's pointer and the old value stays readable until tfp_settings_free, as a
 * Lua table someone still holds would (runtime.lua:806-810 keeps the old tables to put them back).
 * (src/settings.h is the older 0.5.34 store and is unrelated.) */
#ifndef TFP_SETTINGS_H
#define TFP_SETTINGS_H
#include "action_select.h"
#include "motion_math.h"

enum { TFP_SETTINGS_ID = 32,     /* a key's text with its NUL */
       TFP_SETTINGS_ROWS = 256,  /* most rows of one saved table */
       TFP_SETTINGS_MAX = 1 << 20 /* most bytes of settings.json */ };

typedef enum TfpSettingsKind {
  TFP_SETTINGS_BOOL, TFP_SETTINGS_NUMBER,
  TFP_SETTINGS_PLACEMENT,   /* {x, y, z, yaw, pitch, roll}, each there or not */
  TFP_SETTINGS_PLACEMENTS,  /* key -> placement */
  TFP_SETTINGS_PROFILES,    /* key -> action -> placement */
  TFP_SETTINGS_MAPPING,     /* resource -> 'l1:l3' -> action name */
  TFP_SETTINGS_MARKERS,     /* key -> true */
  TFP_SETTINGS_NEAR         /* key -> {hip_cm, aim_cm} */
} TfpSettingsKind;

typedef struct TfpSettingsNear { bool has_hip, has_aim; double hip_cm, aim_cm; } TfpSettingsNear;
typedef struct TfpSettingsValue {
  TfpSettingsKind kind;
  struct TfpSettingsValue *next;     /* the store's list of everything it made */
  bool boolean;
  double number;
  TfpActionPlacement placement;
  size_t count;                      /* rows of a table kind */
  char (*ids)[TFP_SETTINGS_ID];      /* PLACEMENTS, PROFILES, MARKERS, NEAR: the row keys, sorted */
  TfpActionLegacy *placements;       /* PLACEMENTS: `id` points into ids */
  TfpActionProfile *profiles;        /* PROFILES: likewise */
  TfpSettingsNear *near_clip;        /* NEAR */
  TfpActionMapRow *rows;             /* MAPPING: count rows; their three texts live in `texts` */
  char *texts;
  TfpActionMapping mapping;          /* MAPPING: {rows, count}, as tfp_action_select takes it */
} TfpSettingsValue;

/* config.first_person: X(name, kind, retail default). Defaults: src/first_person.lua:9-11. The three
 * without one are nil until saved (aim_markers.lua:134-143, action_placement.lua:98, menu.lua:138-143).
 * grab_offset / grab_offsets are in saved files but no file of retail 1d0d453 reads them. */
#define TFP_SETTINGS_FIELDS(X) \
  X(placement, PLACEMENT, 1) X(motion_enabled, BOOL, 1) X(motion_scale, NUMBER, 1) X(weapon_placements, PLACEMENTS, 1) \
  X(action_placements, PROFILES, 1) X(action_state_mapping, MAPPING, 1) X(fird_head_camera, PLACEMENT, 1) \
  X(camera_kick_enabled, BOOL, 1) X(aim_markers, MARKERS, 1) X(show_aim_markers, BOOL, 0) \
  X(family_placement_defaults, BOOL, 0) X(near_clip_profiles, NEAR, 0) X(grab_offset, PLACEMENT, 0) X(grab_offsets, PLACEMENTS, 0)
typedef enum TfpSettingsField {
#define TFP_SETTINGS_FIELD_ID(name, kind, has_default) TFP_SET_##name,
  TFP_SETTINGS_FIELDS(TFP_SETTINGS_FIELD_ID)
#undef TFP_SETTINGS_FIELD_ID
  TFP_SET_COUNT
} TfpSettingsField;

/* The first-person menu controls whose value retail reads: X(id, 1 Boolean / 0 number, registered default)
 * (menu.lua:21-27, 343-418). A control nobody touched reads as its default (src/first_person.lua:58-61). */
#define TFP_SETTINGS_MENU(X) \
  X(true_native_ads, 1, 0) X(show_torso, 1, 0) X(fird_person_arms, 1, 1) X(motion_enabled, 1, 1) X(motion_bob_enabled, 1, 1) \
  X(motion_sway_enabled, 1, 1) X(motion_sprint_enabled, 1, 1) X(near_clip_aim, 1, 0) X(ads_mode, 0, 1) X(motion_preset, 0, 1) \
  X(motion_strength, 0, 100) X(near_clip_cm, 0, 15) X(weapon_near_hip_cm, 0, 15) X(weapon_near_aim_cm, 0, 5) \
  X(weapon_bob_strength, 0, 100) X(weapon_bob_speed, 0, 100) X(weapon_sway_strength, 0, 100) X(weapon_sway_speed, 0, 100) \
  X(sprint_motion_strength, 0, 100) X(editor_key, 0, 119) X(toggle_key, 0, 122)
typedef enum TfpSettingsMenuId {
#define TFP_SETTINGS_MENU_ID(name, is_bool, value) TFP_MENU_##name,
  TFP_SETTINGS_MENU(TFP_SETTINGS_MENU_ID)
#undef TFP_SETTINGS_MENU_ID
  TFP_MENU_COUNT
} TfpSettingsMenuId;
typedef struct TfpSettingsMenuValue { bool is_bool, boolean; double number; } TfpSettingsMenuValue;

/* The locked view's options (docs/design/specs/2026-11-22-locked-view-design.md), which retail does not have:
 * X(id, 1 Boolean / 0 number, default, least, most, step). The two strengths are percentages. They are stored
 * in their own file, locked_view.json beside settings.json (tfp_settings_write_locked / tfp_settings_load_locked):
 * 0.6.0-feel.19 and older refuse a settings.json with a member they do not know, and their saver then writes
 * their defaults over it. 0.6.0-feel.20 to feel.24 kept them under "locked_view" in settings.json; that member is
 * still read (src/wire_settings.h converts such a file once). No file: the defaults. The store only
 * holds them: what they do is the camera's and the arm rig's business (the getters are in src/wire_settings.h).
 * animated_arms, put_away and smooth_eye switch a feature live (src/wire_options.h); one_handed_pistols sets nothing. */
#define TFP_SETTINGS_LOCKED(X) \
  X(camera_lock, 1, 1, 0, 1, 1) X(fov_lock, 1, 1, 0, 1, 1) X(fov_degrees, 0, 80, 50, 120, 1) X(fov_zoom_when_aiming, 1, 1, 0, 1, 1) \
  X(kick_strength, 0, 35, 0, 100, 5) X(kick_max_degrees, 0, 8, 0, 20, 1) X(thrown_kick_strength, 0, 70, 0, 100, 5) \
  X(animated_arms, 1, 1, 0, 1, 1) X(one_handed_pistols, 1, 1, 0, 1, 1) X(put_away, 1, 1, 0, 1, 1) X(smooth_eye, 1, 1, 0, 1, 1)
/* "holster_animation" (an option until 0.6.0-feel.23 that did nothing) is read as put_away when the file has no
 * put_away of its own, and is never written: the next save holds put_away only. */
#define TFP_SETTINGS_LOCKED_OLD_PUT_AWAY "holster_animation"
typedef enum TfpSettingsLockedId {
#define TFP_SETTINGS_LOCKED_ID(name, is_bool, value, least, most, step) TFP_LOCKED_##name,
  TFP_SETTINGS_LOCKED(TFP_SETTINGS_LOCKED_ID)
#undef TFP_SETTINGS_LOCKED_ID
  TFP_LOCKED_COUNT
} TfpSettingsLockedId;
typedef struct TfpSettingsLockedInfo { const char *name; bool is_bool; double value, min, max, step; } TfpSettingsLockedInfo;

typedef struct TfpSettings {
  const TfpSettingsValue *field[TFP_SET_COUNT]; /* NULL: nil */
  TfpSettingsMenuValue menu[TFP_MENU_COUNT];
  TfpSettingsValue *made;                       /* everything the store made, newest first */
  bool from_file;                               /* the last load took a text (false: defaults) */
  uint64_t writes;                              /* tfp_settings_set calls that changed a field */
  uint64_t menu_writes;                         /* tfp_settings_set_menu calls that changed a control's value */
  TfpSettingsMenuValue locked[TFP_LOCKED_COUNT];
  uint64_t locked_writes;                       /* tfp_settings_set_locked calls that changed an option */
  bool locked_in_settings;                      /* the settings.json last loaded had a "locked_view" member (feel.20 to feel.24) */
  uint32_t locked_named;                        /* bit per TfpSettingsLockedId: the option that member named and gave a value this build took */
  char locked_skipped[160];                     /* what the last load of either file skipped in the locked view's options; "": nothing */
} TfpSettings;

const char *tfp_settings_field_name(TfpSettingsField field);
TfpSettingsKind tfp_settings_field_kind(TfpSettingsField field);
int tfp_settings_field(const char *name);          /* -1: not a key of this store */
const char *tfp_settings_menu_name(TfpSettingsMenuId id);
int tfp_settings_menu(const char *name);           /* -1 */
const TfpSettingsLockedInfo *tfp_settings_locked_info(TfpSettingsLockedId id); /* NULL: not an option */
int tfp_settings_locked(const char *name);         /* -1 */

/* Retail's defaults into *s. *s must be zeroed or freed. false: out of memory (nothing is kept). */
bool tfp_settings_defaults(TfpSettings *s);
/* The text of settings.json. true: loaded; a key of first_person the text does not name is nil, as in
 * a saved table that lacks it (retail lays no defaults under a saved table when reading it). false:
 * `why` says what was wrong (a line of text) and *s holds retail's defaults. json NULL or n 0: "no settings file". *s must be zeroed or freed. */
bool tfp_settings_load(TfpSettings *s, const char *json, size_t n, char *why, size_t why_size);
void tfp_settings_free(TfpSettings *s);
/* The text of locked_view.json, {"format": 1, "locked_view": {option: value, ...}}, over what *s holds (a loaded
 * store or the defaults). An option the text does not name keeps its value; a name this build does not know is
 * skipped (s->locked_skipped lists them). false: `why` says what was wrong (not JSON, no "locked_view" object, a
 * known option of the wrong type) and no option changed. settings.json's own "locked_view" member is read with the
 * same rules, except that nothing in it refuses settings.json: a wrong type is skipped as well. */
bool tfp_settings_load_locked(TfpSettings *s, const char *json, size_t n, char *why, size_t why_size);
/* The locked view's options as locked_view.json text; lengths as tfp_settings_write. */
size_t tfp_settings_write_locked(const TfpSettings *s, char *out, size_t size);
/* The store as settings.json text (sorted keys), without the locked view's options: its top-level members are
 * "first_person", "format" and "menu", which every build since 0.6.0-feel.1 reads. Returns the length without the NUL; when that is
 * >= size the text was cut. out may be NULL with size 0 to measure. */
size_t tfp_settings_write(const TfpSettings *s, char *out, size_t size);

/* ---- values: made by the store, owned by it, never changed. NULL: out of memory, or a row count
 * above TFP_SETTINGS_ROWS, or a key that does not fit TFP_SETTINGS_ID. Rows are copied and sorted by key. */
const TfpSettingsValue *tfp_settings_new_bool(TfpSettings *s, bool value);
const TfpSettingsValue *tfp_settings_new_number(TfpSettings *s, double value);
const TfpSettingsValue *tfp_settings_new_placement(TfpSettings *s, const TfpActionPlacement *value);
const TfpSettingsValue *tfp_settings_new_placements(TfpSettings *s, const TfpActionLegacy *rows, size_t count);
const TfpSettingsValue *tfp_settings_new_profiles(TfpSettings *s, const TfpActionProfile *rows, size_t count);
const TfpSettingsValue *tfp_settings_new_mapping(TfpSettings *s, const TfpActionMapRow *rows, size_t count);
const TfpSettingsValue *tfp_settings_new_markers(TfpSettings *s, const char *const *keys, size_t count);
const TfpSettingsValue *tfp_settings_new_near(TfpSettings *s, const char *const *keys, const TfpSettingsNear *rows, size_t count);
/* An empty table of a table kind ({}), an all-absent placement for PLACEMENT. */
const TfpSettingsValue *tfp_settings_new_empty(TfpSettings *s, TfpSettingsKind kind);
/* The value at this address when the store made it, else NULL (the address is not read). */
const TfpSettingsValue *tfp_settings_owned(const TfpSettings *s, const void *address);

/* mod.set(key, value). value NULL: nil. false with a text when the value's kind is not the field's
 * (retail's table would take anything; this store cannot hold it) or the store did not make it.
 * As retail's (first_person.lua:18-21), a write also gives every other key that was nil and has a
 * default its default. */
bool tfp_settings_set(TfpSettings *s, TfpSettingsField field, const TfpSettingsValue *value, const char **why);
bool tfp_settings_set_menu(TfpSettings *s, TfpSettingsMenuId id, bool is_bool, bool boolean, double number);
/* A locked-view option. false: not an option, the wrong kind, or not a number. A number outside the option's
 * range is stored as the nearest end of it. */
bool tfp_settings_set_locked(TfpSettings *s, TfpSettingsLockedId id, bool is_bool, bool boolean, double number);

/* ---- typed reads ---- */
/* mod.config as action_placement.lua takes it: the view stays valid until the next write to one of
 * its four fields is followed by tfp_settings_free. */
void tfp_settings_action_config(const TfpSettings *s, TfpActionConfig *out);
const TfpActionMapping *tfp_settings_mapping(const TfpSettings *s); /* config.action_state_mapping; NULL: nil */
/* The row of a keyed table: its index, or -1. */
int tfp_settings_row(const TfpSettingsValue *table, const char *key);
/* A Boolean field: 1 true, 0 false, -1 nil. */
int tfp_settings_flag(const TfpSettings *s, TfpSettingsField field);
/* handle.get('<id>') of the menu: a Boolean control's value, a number control's value. */
bool tfp_settings_menu_flag(const TfpSettings *s, TfpSettingsMenuId id);
double tfp_settings_menu_number(const TfpSettings *s, TfpSettingsMenuId id);
bool tfp_settings_locked_flag(const TfpSettings *s, TfpSettingsLockedId id);
double tfp_settings_locked_number(const TfpSettings *s, TfpSettingsLockedId id);

/* menu:motion_options(defaults), menu.lua:150-169, on the stored control values. default_enabled /
 * default_scale are the runtime's defaults (runtime.lua:818), used only where retail falls back to
 * them. Every field retail's table has is written; the rest are absent. */
void tfp_settings_motion_options(const TfpSettings *s, bool default_enabled, double default_scale, TfpMotionOptions *out);
/* menu:near_options(), menu.lua:144-149, 172-182: the two distances in metres (0: the game's own)
 * for the weapon `key` (NULL: no current weapon). *profile: a saved clipping profile supplied them. */
void tfp_settings_near_options(const TfpSettings *s, const char *key, double *hip, double *aim, bool *profile);
#endif
