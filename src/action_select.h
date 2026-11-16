/* Action selection and placement resolution per weapon key. Translation of
 * retail src/first_person/action_placement.lua at 0.5.41-smooth.66 (commit
 * 1d0d453); tests/vectors/action_placement_vectors.h holds that file's own
 * answers. Line numbers in action_select.c are the Lua file's.
 *
 * Pure policy: no game reads, timers or animation commands. The module-level
 * `weapons` table of the Lua file is a TfpActionCatalogue the caller owns.
 * Numbers are doubles, as Lua's, and are handed on unchanged.
 *
 * How Lua values are spelled in C:
 *  - a weapon key is a string (16 lower-case hex characters on the live path),
 *    NULL for nil; keys are compared as strings, so "__all" names the shared row;
 *  - a placement table is a TfpActionPlacement: a bit per field that is there,
 *    a value of a non-number type passed as NaN. For the fields retail reads as
 *    `p.yaw or 0`, false counts as absent; a pointer NULL is "not a table";
 *  - a map in the saved configuration is a list of rows with an id; a value
 *    that is not a table is left out of the list (retail skips it everywhere);
 *  - a sample field retail tests for truth is a bool holding Lua's truth
 *    (only nil and false are false; 0 and "" are true). */
#ifndef TFP_ACTION_SELECT_H
#define TFP_ACTION_SELECT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* M.actions, action_placement.lua:66-67, in that order. */
typedef enum TfpAct { TFP_ACT_IDLE, TFP_ACT_AIM, TFP_ACT_ADS, TFP_ACT_SPRINT, TFP_ACT_RELOAD, TFP_ACT_CROUCH, TFP_ACT_PRONE,
                      TFP_ACT_SLIDE, TFP_ACT_DIVE, TFP_ACT_COUNT, TFP_ACT_NONE = -1 /* nil */ } TfpAct;
const char *tfp_action_name(TfpAct action);  /* 'idle' ...; NULL outside the list */
const char *tfp_action_label(TfpAct action); /* 'Idle', 'Normal aim', ... */
TfpAct tfp_action_from_name(const char *name); /* allowed[name]; TFP_ACT_NONE when not allowed */
bool tfp_action_observed(TfpAct action);     /* M.observed_actions: crouch, prone, slide, dive */

/* ---- catalogue (lines 6-49) ---- */
typedef enum TfpActionFamily { TFP_FAMILY_NONE, TFP_FAMILY_RIFLE, TFP_FAMILY_COMPACT, TFP_FAMILY_SIDEARM, TFP_FAMILY_MARKSMAN,
                               TFP_FAMILY_SHOTGUN, TFP_FAMILY_ENERGY, TFP_FAMILY_SUPPORT, TFP_FAMILY_COUNT } TfpActionFamily;
const char *tfp_action_family_name(TfpActionFamily family); /* 'rifle' ...; NULL for none */
/* One row of the kit's loadout catalogue `gear`, the fields this module reads.
 * name NULL: not a string. type NULL: not a string. */
typedef struct TfpActionGearRow { int kind; const char *name; uint32_t id; bool has_id; const char *type; } TfpActionGearRow;
typedef struct TfpActionWeapon {
  char key[17];            /* the type's bytes in memory order, lower-case hex */
  bool ambiguous;          /* retail stores false: two rows disagree on name or family */
  const char *name;        /* the row's string; the caller keeps the rows alive */
  TfpActionFamily family;
  int kind;
  uint32_t item;
  bool has_item;
} TfpActionWeapon;
typedef struct TfpActionCatalogue { TfpActionWeapon *weapons; size_t capacity, count; } TfpActionCatalogue;
/* M.configure_catalogue(gear). rows NULL: gear is not a table. The catalogue is
 * replaced as a whole. false only when `capacity` is too small (retail has no
 * limit); the catalogue is then left empty. */
bool tfp_action_configure_catalogue(TfpActionCatalogue *catalogue, const TfpActionGearRow *rows, size_t count);
/* M.weapon_info(key): NULL for an unknown or ambiguous key. */
const TfpActionWeapon *tfp_action_weapon_info(const TfpActionCatalogue *catalogue, const char *key);

/* ---- placements ---- */
enum { TFP_PLACE_X = 1, TFP_PLACE_Y = 2, TFP_PLACE_Z = 4, TFP_PLACE_YAW = 8, TFP_PLACE_PITCH = 16, TFP_PLACE_ROLL = 32,
       TFP_PLACE_XYZ = 7, TFP_PLACE_ALL = 63 };
typedef struct TfpActionPlacement { double x, y, z, yaw, pitch, roll; unsigned present; } TfpActionPlacement;
/* M.family_baseline(key): false when the key has no family. */
bool tfp_action_family_baseline(const TfpActionCatalogue *catalogue, const char *key, TfpActionPlacement *out);
/* M.valid(p). p NULL: not a table. */
bool tfp_action_valid(const TfpActionPlacement *p);

typedef struct TfpActionProfile {  /* config.action_placements[id] */
  const char *id;
  unsigned has;                    /* bit (1 << action): that action holds a table */
  TfpActionPlacement action[TFP_ACT_COUNT];
} TfpActionProfile;
typedef struct TfpActionLegacy { const char *id; TfpActionPlacement placement; } TfpActionLegacy; /* config.weapon_placements[id] */
typedef struct TfpActionConfig {
  const TfpActionProfile *profiles; size_t profile_count;   /* action_placements; none: absent or {} */
  const TfpActionLegacy *legacy; size_t legacy_count;       /* weapon_placements */
  const TfpActionPlacement *placement;                      /* config.placement; NULL: not a table */
  bool family_placement_defaults;                           /* == true */
} TfpActionConfig;

/* M.resolve(config, key, action) -> placement, explicit. config NULL: nil.
 * action TFP_ACT_NONE: nil (idle). false with retail's text for an action
 * outside the list. The answer's `present` bits say which fields retail's
 * table has: the ADS and the last fallback carry x, y, z only. */
bool tfp_action_resolve(const TfpActionConfig *config, const TfpActionCatalogue *catalogue, const char *key, TfpAct action,
                        TfpActionPlacement *out, bool *is_explicit, const char **error);
/* M.default_placement(config, key, action). action is compared with 'ads' only. */
TfpActionPlacement tfp_action_default_placement(const TfpActionConfig *config, const TfpActionCatalogue *catalogue,
                                                const char *key, TfpAct action);
/* M.saved(config, key, action, value, all): the new action_placements, written
 * to out[0..*out_count). Every id of the old table is kept (a profile left
 * without a valid action stays as an empty row); actions that are not valid
 * are dropped. The rows' id pointers are the caller's (config's ids, key, or
 * the literal "__all"). false with retail's text; false with *error NULL when
 * out_capacity is too small (retail has no limit). Row order is not part of
 * the answer: retail builds a table. */
bool tfp_action_saved(const TfpActionConfig *config, const char *key, TfpAct action, const TfpActionPlacement *value, bool all,
                      TfpActionProfile *out, size_t out_capacity, size_t *out_count, const char **error);

/* ---- selection (lines 50-65, 122-138) ---- */
typedef enum TfpActionAds { TFP_ACTION_ADS_OTHER, TFP_ACTION_ADS_TRUE, TFP_ACTION_ADS_NUMBER } TfpActionAds;
typedef enum TfpActionVerified { TFP_VERIFIED_FALSY /* nil, false */, TFP_VERIFIED_TRUE, TFP_VERIFIED_TRUTHY /* any other value */
} TfpActionVerified;
typedef struct TfpActionSample {
  TfpActionAds ads_kind; double ads;
  const char *action_resource;            /* NULL: not a string */
  TfpActionVerified action_verified;
  const uint8_t *stance_flags; size_t stance_flags_size; /* NULL: not a string */
  bool reload, shoulder_aim, sprint;      /* Lua truth */
  bool has_l1, has_l3; int l1, l3;        /* nil prints as "nil" in the mapping key */
} TfpActionSample;
/* config.action_state_mapping[resource][l1 .. ':' .. l3] = action name. */
typedef struct TfpActionMapRow { const char *resource, *states, *action; } TfpActionMapRow;
typedef struct TfpActionMapping { const TfpActionMapRow *rows; size_t count; } TfpActionMapping;
/* M.select(sample, mapping). mapping NULL: nil. */
TfpAct tfp_action_select(const TfpActionSample *sample, const TfpActionMapping *mapping);
#endif
