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

/* Actions.actions (action_placement.lua:66-67) is handed to the editor as this value: a ref to
 * tfp_wire_settings_actions, TFP_ACT_COUNT rows of {name, label}. */
extern const char *const tfp_wire_settings_actions[TFP_ACT_COUNT][2];
#endif
