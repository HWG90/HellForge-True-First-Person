/* The settings store and the placement profiles behind the runtime host (the feel build).
 *
 *   mod.config, mod.set                      -> src/tfp_settings.c (loaded from the text of settings.json)
 *   Actions.* (action_placement.lua)         -> src/action_select.c over the saved placements and the
 *                                               catalogue rows of src/action_placement_data.h
 *   menu:fird_person_arms / show_torso / native_ads / ads_mode / near_options / motion_options
 *                                            -> the stored control values, as menu.lua computes them
 *
 * tfp_wire_settings_bind is called after tfp_wire_init and before the runtime's on_enable; its state
 * is w->ext[TFP_WIRE_EXT_SETTINGS]. No Windows: the caller reads the file and hands its bytes over.
 * docs/wiring/WIRING-FEEL.md lists every binding. */
#ifndef TFP_WIRE_SETTINGS_H
#define TFP_WIRE_SETTINGS_H
#include "wire.h"
#include "tfp_settings.h"

/* json: the bytes of settings.json (need not end in a NUL; not kept); NULL or length 0: there is no
 * file. origin: what the log calls the file (may be NULL). Logs `Ready: settings ...`, and before it
 * `Refused: settings: <why>` when the text was missing or wrong and retail's defaults are used.
 * false: out of memory; nothing was bound and the view build's answers stay. */
bool tfp_wire_settings_bind(TfpWire *w, const char *json, size_t length, const char *origin);
/* Frees the store (after the runtime's on_disable; optional at process end). */
void tfp_wire_settings_free(TfpWire *w);

/* ---- for the other binders ---- */
TfpSettings *tfp_wire_settings(TfpWire *w);                          /* NULL: not bound */
const TfpActionCatalogue *tfp_wire_settings_catalogue(TfpWire *w);   /* NULL: not bound */
/* A placement as a value the runtime can carry: the ref stays valid for the next 63 placements made
 * here (the runtime passes a placement on within the call that got it and keeps copies). */
TfpRtValue tfp_wire_settings_placement(TfpWire *w, const TfpActionPlacement *p);
/* Any placement value (one of this binder's, a saved one, the runtime's own two tables). */
bool tfp_wire_settings_placement_of(TfpWire *w, TfpRtValue value, TfpActionPlacement *out);
/* The saved value behind a ref the store handed out (config.aim_markers and the like), else NULL. */
const TfpSettingsValue *tfp_wire_settings_value_of(TfpWire *w, TfpRtValue value);
/* config.action_state_mapping as the motion runtime takes it; NULL: the value is not that table. */
const TfpActionMapping *tfp_wire_settings_mapping_of(TfpWire *w, TfpRtValue value);
/* The table menu:motion_options() answered. false: the value is not that table. */
bool tfp_wire_settings_options_of(TfpWire *w, TfpRtValue value, TfpMotionOptions *out);
/* A new saved table `key -> true` for mod.set('aim_markers', ...). nil: it could not be made. */
TfpRtValue tfp_wire_settings_markers(TfpWire *w, const char *const *keys, size_t count);
/* Actions.resolve(mod.config, key, action) typed. false: an action outside the list. */
bool tfp_wire_settings_resolve(TfpWire *w, const char *key, TfpAct action, TfpActionPlacement *out, bool *is_explicit);
/* Where that placement comes from, for the log: "imported(own)", "imported(__all)", "idle>imported(own)",
 * "imported(older own)", "imported(older __all)", "family(rifle)", "imported(global)", "shipped default",
 * "ads default". */
const char *tfp_wire_settings_source(TfpWire *w, const char *key, TfpAct action, char *out, size_t size);
/* Once per tick after on_update: one `Weapon: key=... placement=...` line whenever the placement
 * weapon key changes. (tfp_wire_motion_frame calls it.) */
void tfp_wire_settings_frame(TfpWire *w);

/* The locked view's options have their own file, locked_view.json beside settings.json. json: its bytes (not
 * kept); NULL or length 0: there is no such file, and the options are the defaults or what a settings.json of
 * 0.6.0-feel.20 to feel.24 held under "locked_view". Call after tfp_wire_settings_bind and before
 * tfp_wire_settings_disk. A file that names them wins over settings.json. Logs one `Note:` line when names this
 * build does not know were skipped, and `Refused: locked view options: <why>` when the file could not be used
 * (the options then stay as they were; settings.json is never refused for anything in either place). */
void tfp_wire_settings_bind_locked(TfpWire *w, const char *json, size_t length, const char *origin);

/* ---- saving (docs/guides/POSER-AND-MENU.md) ---------------------------------------------------------
 * Two files in the user's folder: settings.json (first_person and menu: readable by every build since
 * 0.6.0-feel.1) and locked_view.json (the locked view's options). Each is written when what it holds changed,
 * never on a timer: tfp_wire_settings_save_frame compares the store's change counters with what was last
 * written and writes a file once per change. (One compare a tick instead of a callback in every unit that
 * calls mod.set.) A failed write leaves that file unsaved until its next change, so a failing disk is tried
 * once per change and not once a frame.
 *
 * No file the user had is ever written over without a copy, in two cases:
 *   - a file that was REFUSED at load (the store runs on defaults, so a save would replace the user's values):
 *     before the session's first write of it, it is copied to <name>.refused (or .refused-2 .. .refused-9 when
 *     that name is taken; a taken name is never written over). The copy failing fails that save.
 *   - a settings.json that still holds "locked_view" (written by 0.6.0-feel.20 to feel.24, which 0.6.0-feel.19
 *     and older refuse whole): see tfp_wire_settings_disk. */
typedef enum TfpSettingsFile { TFP_SETTINGS_FILE, TFP_LOCKED_FILE } TfpSettingsFile;
typedef enum TfpSettingsCopy {
  TFP_COPY_DONE,    /* the copy was made */
  TFP_COPY_NONE,    /* there is no such file to copy */
  TFP_COPY_EXISTS,  /* a file of the copy's name is already there; it was left as it is */
  TFP_COPY_FAILED   /* `why` says what went wrong */
} TfpSettingsCopy;
typedef struct TfpSettingsDisk {
  void *user;
  /* Replaces the file with this text atomically (a temporary file, then a rename). false: `why`. */
  bool (*write)(void *user, TfpSettingsFile file, const char *text, size_t length, char *why, size_t why_size);
  /* Copies the file as it is on disk to <its name><suffix>, never over an existing file. */
  TfpSettingsCopy (*copy)(void *user, TfpSettingsFile file, const char *suffix, char *why, size_t why_size);
} TfpSettingsDisk;
#define TFP_SETTINGS_BEFORE_SPLIT ".before-feel25"
/* Where the two files are kept; NULL: nothing is ever written. Not kept: the struct is copied. user_file: the text
 * bound as settings.json was the user's own file, the one `write` replaces (false: the one shipped beside the DLL,
 * or none). What is loaded counts as what the files hold, with one exception that is done here, at once: when the
 * user's settings.json held "locked_view", (1) locked_view.json is written from the loaded options unless that file
 * was there and read, (2) settings.json is copied to settings.json.before-feel25 (an existing copy of that name is
 * kept: it is the older one), (3) settings.json is written again without the member, everything else as loaded.
 * A step that fails logs one `Refused:` line and ends the conversion; the session runs on the loaded values either
 * way. Until it has been done, no save of settings.json skips steps 1 and 2: a later save does them first and fails
 * with them, and the next start tries again. */
void tfp_wire_settings_disk(TfpWire *w, const TfpSettingsDisk *disk, bool user_file);
void tfp_wire_settings_save_frame(TfpWire *w);                                /* once per tick, after the runtime's update */
bool tfp_wire_settings_unsaved(TfpWire *w);                                   /* a change a file does not hold */
/* A menu control's value (the MCM binder's changed callback). false: not bound, or the wrong kind. */
bool tfp_wire_settings_menu_set(TfpWire *w, TfpSettingsMenuId id, bool is_bool, bool boolean, double number);

/* ---- the locked view's options (TFP_SETTINGS_LOCKED in src/tfp_settings.h) ---------------------------
 * The own settings panel writes them (src/wire_panel.c); a change is saved to locked_view.json by the next
 * save frame. false: no store bound, not an option, the wrong kind. */
bool tfp_wire_settings_locked_set(TfpWire *w, TfpSettingsLockedId id, bool is_bool, bool boolean, double number);
/* The options by name, for the units that act on them. With no store bound each answers its default.
 * Plain reads of one stored value each: no game call, no lock, so a read from another thread than the
 * game thread sees the old or the new value of a change in flight, never a torn one on x86-64.
 * The two strengths are percentages (0 .. 100), the two angles degrees. */
bool tfp_locked_view_camera_lock(TfpWire *w);
bool tfp_locked_view_fov_lock(TfpWire *w);
double tfp_locked_view_fov_degrees(TfpWire *w);
bool tfp_locked_view_fov_zoom_when_aiming(TfpWire *w);
double tfp_locked_view_kick_strength(TfpWire *w);
double tfp_locked_view_kick_max_degrees(TfpWire *w);
double tfp_locked_view_thrown_kick_strength(TfpWire *w);
/* The three that switch a feature live (src/wire_options.h decides with them and the test files). */
bool tfp_locked_view_animated_arms(TfpWire *w);
bool tfp_locked_view_put_away(TfpWire *w);
bool tfp_locked_view_smooth_eye(TfpWire *w);
/* Stored and shown greyed; nothing acts on it: the behaviour comes from the character's own animation while
 * animated_arms is on. */
bool tfp_locked_view_one_handed_pistols(TfpWire *w);

/* Actions.actions (action_placement.lua:66-67) is handed to the editor as this value: a ref to
 * tfp_wire_settings_actions, TFP_ACT_COUNT rows of {name, label}. */
extern const char *const tfp_wire_settings_actions[TFP_ACT_COUNT][2];
#endif
