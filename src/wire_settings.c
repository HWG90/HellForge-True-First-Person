/* See wire_settings.h. Signature adapters between the runtime host (tagged values) and the settings
 * store / action_select.c (typed C). Line references "rt:N" are retail runtime.lua, "ap:N" retail
 * action_placement.lua, "menu:N" retail menu.lua, "fp:N" retail src/first_person.lua, all at
 * 0.5.41-smooth.66 (1d0d453). No Windows. */
#include "wire_settings.h"
#include "wire_off.h"
#include "action_placement_data.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef TfpRtValue V;
#define nil tfp_wire_nil
#define vbool tfp_wire_bool
#define num tfp_wire_number
enum { RING = 64, TEXTS = 8, TEXT = 64 };

typedef struct NearAnswer { double hip, aim; bool profile; } NearAnswer;
typedef struct Settings {
  TfpSettingsDisk disk;
  uint64_t saved_revision[2], tried_revision[2]; /* per TfpSettingsFile: its change count last written, and last tried */
  bool refused[2];           /* per file: the text was refused at load, and no copy of the file has been kept yet */
  bool unread[2];            /* per file: no byte of the user's file was bound (it is not there, or it could not be opened),
                              * and it has not been looked for since: what is there is copied aside before the first write */
  bool loaded, locked_loaded; /* settings.json / locked_view.json was read and taken */
  bool convert;              /* the user's settings.json still holds "locked_view": see convert_first */
  TfpSettingsMenuValue member[TFP_LOCKED_COUNT]; /* the locked options as settings.json's own member left them, before locked_view.json was read */
  bool locked_stale;         /* that member's values replaced what locked_view.json says: the file is copied aside, then written */
  char split_kept[24];       /* this session's copy of the settings.json that held the member: its suffix; "": not made yet */
  char stale_kept[24];       /* the suffix of the copy of the locked_view.json whose values were replaced; "": none made */
  TfpWire *w;
  TfpSettings store;
  TfpActionCatalogue catalogue;
  TfpActionWeapon weapons[TFP_ACTION_GEAR_COUNT];
  bool (*previous_decode)(TfpWire *w, V value, TfpOpsPlacement *out);
  TfpActionPlacement ring[RING];
  unsigned ring_next;
  char texts[TEXTS][TEXT];
  unsigned text_next;
  TfpMotionOptions options;  /* the table menu:motion_options() last answered */
  NearAnswer near_clip;      /* the table menu:near_options() last answered */
  bool has_weapon;           /* the weapon key of the last `Weapon:` line */
  char weapon[17];
} Settings;
static Settings *state(TfpWire *w) { return w ? w->ext[TFP_WIRE_EXT_SETTINGS] : NULL; }
#define S(h) state((TfpWire *)(h)->user)

const char *const tfp_wire_settings_actions[TFP_ACT_COUNT][2] = { /* ap:66-67 */
  {"idle", "Idle"}, {"aim", "Normal aim"}, {"ads", "ADS"}, {"sprint", "Sprinting"}, {"reload", "Reloading"},
  {"crouch", "Crouching"}, {"prone", "Prone"}, {"slide", "Sliding"}, {"dive", "Diving"}};

/* ---- values ------------------------------------------------------------------------------- */
/* A Lua string as a C string: NULL for anything that is not a string or holds a NUL. A longer one
 * is cut (no key of the store is that long, so it matches nothing either way). */
static const char *text_of(Settings *st, V v) {
  if (v.kind != TFP_RT_TEXT || memchr(v.text, 0, v.length)) return NULL;
  char *out = st->texts[st->text_next++ % TEXTS];
  size_t n = v.length < TEXT - 1 ? v.length : TEXT - 1;
  memcpy(out, v.text, n);
  out[n] = 0;
  return out;
}
static TfpAct action_of(Settings *st, V v) {
  const char *name = text_of(st, v);
  return name ? tfp_action_from_name(name) : TFP_ACT_NONE;
}
static void config_of(const Settings *st, TfpActionConfig *out) { tfp_settings_action_config(&st->store, out); }
TfpRtValue tfp_wire_settings_placement(TfpWire *w, const TfpActionPlacement *p) {
  Settings *st = state(w);
  if (!st || !p) return nil();
  TfpActionPlacement *slot = &st->ring[st->ring_next++ % RING];
  *slot = *p;
  return tfp_wire_ref(slot);
}
static const TfpActionPlacement *ring_of(const Settings *st, const void *ref) {
  const char *p = ref, *first = (const char *)st->ring;
  if (p < first || p >= first + sizeof st->ring || (size_t)(p - first) % sizeof st->ring[0]) return NULL;
  return ref;
}
static bool own_placement(const Settings *st, V value, TfpActionPlacement *out) {
  if (value.kind != TFP_RT_REF) return false;
  const TfpActionPlacement *made = ring_of(st, value.ref);
  if (made) { *out = *made; return true; }
  const TfpSettingsValue *saved = tfp_settings_owned(&st->store, value.ref);
  if (saved && saved->kind == TFP_SETTINGS_PLACEMENT) { *out = saved->placement; return true; }
  if (value.ref == tfp_rt_camera_baseline || value.ref == tfp_rt_default_placement) { /* {x=0,y=0,z=.04}, {x=.15,y=0,z=-.12} */
    const double *xyz = value.ref;
    *out = (TfpActionPlacement){xyz[0], xyz[1], xyz[2], 0, 0, 0, TFP_PLACE_XYZ};
    return true;
  }
  return false;
}
/* w->placement_decode: TfpActionPlacement and TfpOpsPlacement spell the six fields and their bits alike. */
static bool decode(TfpWire *w, V value, TfpOpsPlacement *out) {
  Settings *st = state(w);
  TfpActionPlacement p;
  if (!st) return false;
  if (!own_placement(st, value, &p)) return st->previous_decode && st->previous_decode(w, value, out);
  *out = (TfpOpsPlacement){p.x, p.y, p.z, p.yaw, p.pitch, p.roll, p.present};
  return true;
}
bool tfp_wire_settings_placement_of(TfpWire *w, V value, TfpActionPlacement *out) {
  TfpOpsPlacement p;
  if (!tfp_wire_placement_of(w, value, &p)) return false;
  *out = (TfpActionPlacement){p.x, p.y, p.z, p.yaw, p.pitch, p.roll, p.present};
  return true;
}
const TfpSettingsValue *tfp_wire_settings_value_of(TfpWire *w, V value) {
  Settings *st = state(w);
  return st && value.kind == TFP_RT_REF ? tfp_settings_owned(&st->store, value.ref) : NULL;
}
const TfpActionMapping *tfp_wire_settings_mapping_of(TfpWire *w, V value) {
  const TfpSettingsValue *v = tfp_wire_settings_value_of(w, value);
  return v && v->kind == TFP_SETTINGS_MAPPING ? &v->mapping : NULL;
}
bool tfp_wire_settings_options_of(TfpWire *w, V value, TfpMotionOptions *out) {
  Settings *st = state(w);
  if (!st || value.kind != TFP_RT_REF || value.ref != &st->options) return false;
  *out = st->options;
  return true;
}
TfpRtValue tfp_wire_settings_markers(TfpWire *w, const char *const *keys, size_t count) {
  Settings *st = state(w);
  const TfpSettingsValue *v = st ? tfp_settings_new_markers(&st->store, keys, count) : NULL;
  return v ? tfp_wire_ref(v) : nil();
}
TfpSettings *tfp_wire_settings(TfpWire *w) { Settings *st = state(w); return st ? &st->store : NULL; }
const TfpActionCatalogue *tfp_wire_settings_catalogue(TfpWire *w) { Settings *st = state(w); return st ? &st->catalogue : NULL; }
bool tfp_wire_settings_resolve(TfpWire *w, const char *key, TfpAct action, TfpActionPlacement *out, bool *is_explicit) {
  Settings *st = state(w);
  TfpActionConfig config;
  const char *error = NULL;
  bool explicit_row = false;
  if (!st) return false;
  config_of(st, &config);
  bool done = tfp_action_resolve(&config, &st->catalogue, key, action, out, &explicit_row, &error);
  if (is_explicit) *is_explicit = explicit_row;
  return done;
}

/* ---- Actions (rt:155-158, 776-794, 1060-1065) ------------------------------------------------ */
static bool rt_configure_catalogue(TfpRtHost *h) { /* rt:658 Actions.configure_catalogue(gear) */
  Settings *st = S(h);
  if (!tfp_action_configure_catalogue(&st->catalogue, tfp_action_gear, TFP_ACTION_GEAR_COUNT))
    return tfp_wire_raise(h, "Weapon catalogue does not fit");
  return true;
}
static bool rt_resolve(TfpRtHost *h, V key, V action, V *o1, V *o2) { /* Actions.resolve(mod.config, key, action) */
  Settings *st = S(h);
  TfpActionConfig config;
  TfpActionPlacement p;
  bool explicit_row = false;
  const char *error = NULL;
  *o1 = *o2 = nil();
  config_of(st, &config);
  /* ap:80: action = action or 'idle'; then allowed[action] or the assertion. */
  TfpAct act = action.kind == TFP_RT_NIL || action.kind == TFP_RT_FALSE ? TFP_ACT_IDLE : action_of(st, action);
  if (act == TFP_ACT_NONE) return tfp_wire_raise(h, "Unknown placement action");
  if (!tfp_action_resolve(&config, &st->catalogue, text_of(st, key), act, &p, &explicit_row, &error))
    return tfp_wire_raise(h, error ? error : "Unknown placement action");
  *o1 = tfp_wire_settings_placement(st->w, &p);
  *o2 = vbool(explicit_row);
  return true;
}
static bool rt_default_placement(TfpRtHost *h, V key, V action, V *o1, V *o2) { /* ap:103-107: action is compared with 'ads' only */
  Settings *st = S(h);
  TfpActionConfig config;
  config_of(st, &config);
  TfpActionPlacement p = tfp_action_default_placement(&config, &st->catalogue, text_of(st, key),
                                                      action_of(st, action) == TFP_ACT_ADS ? TFP_ACT_ADS : TFP_ACT_IDLE);
  *o1 = tfp_wire_settings_placement(st->w, &p);
  *o2 = nil();
  return true;
}
static bool rt_saved(TfpRtHost *h, V key, V action, V value, V all_weapons, V *o1) { /* ap:108-121 */
  Settings *st = S(h);
  TfpActionConfig config;
  TfpActionPlacement p;
  const char *error = NULL, *id = key.kind == TFP_RT_TEXT && key.length == 16 ? text_of(st, key) : NULL;
  size_t count = 0;
  *o1 = nil();
  config_of(st, &config);
  bool is_placement = tfp_wire_settings_placement_of(st->w, value, &p);
  bool all = all_weapons.kind != TFP_RT_NIL && all_weapons.kind != TFP_RT_FALSE;
  TfpActionProfile *rows = calloc(TFP_SETTINGS_ROWS + 1, sizeof *rows);
  if (!rows) return tfp_wire_raise(h, "Out of memory for the saved placements");
  bool done = tfp_action_saved(&config, id, action_of(st, action), is_placement ? &p : NULL, all, rows, TFP_SETTINGS_ROWS + 1, &count, &error);
  const TfpSettingsValue *made = done ? tfp_settings_new_profiles(&st->store, rows, count) : NULL;
  free(rows);
  if (!done) return tfp_wire_raise(h, error ? error : "Saved placements do not fit");
  if (!made) return tfp_wire_raise(h, "Saved placements do not fit");
  *o1 = tfp_wire_ref(made);
  return true;
}
static bool rt_weapon_info(TfpRtHost *h, V key, TfpRtTable *o1) { /* ap:44-47 */
  Settings *st = S(h);
  const TfpActionWeapon *info = tfp_action_weapon_info(&st->catalogue, text_of(st, key));
  memset(o1, 0, sizeof *o1);
  if (!info) return true;
  o1->present = true;
  o1->ref = info;
  o1->f[TFP_RT_WINFO_NAME] = tfp_wire_text(info->name);
  o1->f[TFP_RT_WINFO_FAMILY] = tfp_wire_text(tfp_action_family_name(info->family));
  return true;
}
static V rt_observed(TfpRtHost *h, V action) { /* Actions.observed_actions[action]: true or nil */
  return tfp_action_observed(action_of(S(h), action)) ? vbool(true) : nil();
}
static V rt_actions(TfpRtHost *h) { (void)h; return tfp_wire_ref(tfp_wire_settings_actions); }

/* ---- mod.config, mod.set (fp:12-24) ------------------------------------------------------------ */
static V rt_config_table(TfpRtHost *h) { return tfp_wire_ref(&S(h)->store); }
static V field_value(const TfpSettingsValue *v) {
  if (!v) return nil();
  if (v->kind == TFP_SETTINGS_BOOL) return vbool(v->boolean);
  if (v->kind == TFP_SETTINGS_NUMBER) return num(v->number);
  return tfp_wire_ref(v);
}
static V rt_config_field(TfpRtHost *h, const char *name) {
  int field = tfp_settings_field(name);
  return field < 0 ? nil() : field_value(S(h)->store.field[field]);
}
static V rt_config_action_mapped(TfpRtHost *h, V action) { /* rt:772-774: does any mapping name this action */
  Settings *st = S(h);
  const TfpActionMapping *mapping = tfp_settings_mapping(&st->store);
  const char *name = text_of(st, action);
  for (size_t i = 0; name && mapping && i < mapping->count; i++)
    if (!strcmp(mapping->rows[i].action, name)) return vbool(true);
  return vbool(false);
}
#define REFUSED_KIND "Settings value has a type this build cannot store for that setting"
static bool rt_mod_set(TfpRtHost *h, const char *key, V value, V *o1, V *o2) { /* fp:17-23 */
  Settings *st = S(h);
  const TfpSettingsValue *made = NULL;
  const char *why = NULL;
  int field = tfp_settings_field(key);
  *o1 = vbool(false), *o2 = nil();
  if (field < 0) { *o2 = tfp_wire_text("Unknown first-person setting"); return true; }
  TfpSettingsKind kind = tfp_settings_field_kind((TfpSettingsField)field);
  switch (value.kind) {
    case TFP_RT_NIL: break;
    case TFP_RT_TRUE: case TFP_RT_FALSE:
      if (kind != TFP_SETTINGS_BOOL) why = REFUSED_KIND; else made = tfp_settings_new_bool(&st->store, value.kind == TFP_RT_TRUE);
      break;
    case TFP_RT_NUMBER:
      if (kind != TFP_SETTINGS_NUMBER) why = REFUSED_KIND; else made = tfp_settings_new_number(&st->store, value.number);
      break;
    case TFP_RT_REF: {
      TfpActionPlacement p;
      made = tfp_settings_owned(&st->store, value.ref);
      if (made) { if (made->kind != kind) why = REFUSED_KIND; break; }
      if (kind == TFP_SETTINGS_BOOL || kind == TFP_SETTINGS_NUMBER) why = REFUSED_KIND;
      else if (value.ref == &tfp_rt_empty_table) made = tfp_settings_new_empty(&st->store, kind); /* {} */
      else if (kind == TFP_SETTINGS_PLACEMENT && tfp_wire_settings_placement_of(st->w, value, &p)) made = tfp_settings_new_placement(&st->store, &p);
      else why = REFUSED_KIND;
      break;
    }
    default: why = REFUSED_KIND; break;
  }
  if (!why && value.kind != TFP_RT_NIL && !made) why = "Out of memory for the setting";
  if (!why && !tfp_settings_set(&st->store, (TfpSettingsField)field, made, &why) && !why) why = REFUSED_KIND;
  if (why) { *o2 = tfp_wire_text(why); return true; }
  *o1 = vbool(true);
  return true;
}

/* ---- the menu's option methods (menu:144-242) on the stored control values ------------------- */
static bool rt_menu_native_ads(TfpRtHost *h, V *o1) { *o1 = vbool(tfp_settings_menu_flag(&S(h)->store, TFP_MENU_true_native_ads)); return true; }
static bool rt_menu_show_torso(TfpRtHost *h, V *o1) { *o1 = vbool(tfp_settings_menu_flag(&S(h)->store, TFP_MENU_show_torso)); return true; }
static bool rt_menu_arms(TfpRtHost *h, V *o1) { *o1 = vbool(tfp_settings_menu_flag(&S(h)->store, TFP_MENU_fird_person_arms)); return true; }
static bool rt_menu_ads_mode(TfpRtHost *h, V *o1) { /* menu:240-242 */
  *o1 = tfp_wire_text(tfp_settings_menu_number(&S(h)->store, TFP_MENU_ads_mode) == 2 ? "native" : "fird");
  return true;
}
static bool rt_menu_motion_options(TfpRtHost *h, const V *defaults, V *o1) { /* menu:150-169 */
  Settings *st = S(h);
  V scale = defaults[TFP_RT_DEFAULTS_SCALE];
  tfp_settings_motion_options(&st->store, defaults[TFP_RT_DEFAULTS_ENABLED].kind != TFP_RT_FALSE,
                              scale.kind == TFP_RT_NUMBER ? scale.number : 1, &st->options);
  *o1 = tfp_wire_ref(&st->options);
  return true;
}
/* current_weapon() of menu:132-137: the runtime's own function, its key only when it is 16 hex characters
 * and it came with an identity. */
static const char *current_weapon(Settings *st) {
  V r[5] = {nil(), nil(), nil(), nil(), nil()};
  if (tfp_rt_call(&st->w->rt, TFP_RT_E_current_weapon, NULL, r) != TFP_RT_OK || r[0].kind != TFP_RT_TRUE || r[2].kind == TFP_RT_NIL) return NULL;
  return text_of(st, r[1]);
}
static bool rt_menu_near_options(TfpRtHost *h, V *o1) { /* menu:172-182 */
  Settings *st = S(h);
  tfp_settings_near_options(&st->store, current_weapon(st), &st->near_clip.hip, &st->near_clip.aim, &st->near_clip.profile);
  *o1 = tfp_wire_ref(&st->near_clip);
  return true;
}
/* What the near-clip adapter reads (rt:713-716): the menu's table when the menu answered, else the
 * runtime's {hip=0, aim=0}. Replaces the view build's reader, which knew no menu table. */
static void near_options(void *user, TfpNearOptions *out) {
  TfpWire *w = user;
  Settings *st = state(w);
  V r[3] = {nil(), nil(), nil()};
  memset(out, 0, sizeof *out);
  if (tfp_rt_call(&w->rt, TFP_RT_E_near_options, NULL, r) != TFP_RT_OK) return;
  if (r[0].kind == TFP_RT_NIL) {
    out->is_table = true;
    out->hip_is_number = r[1].kind == TFP_RT_NUMBER, out->hip = r[1].number;
    out->aim_is_number = r[2].kind == TFP_RT_NUMBER, out->aim = r[2].number;
  } else if (st && r[0].kind == TFP_RT_REF && r[0].ref == &st->near_clip) {
    out->is_table = out->hip_is_number = out->aim_is_number = true;
    out->hip = st->near_clip.hip, out->aim = st->near_clip.aim;
  }
}

/* ---- the session log --------------------------------------------------------------------------- */
static bool standard(const TfpActionPlacement *p) { /* ap:96-97 */
  return p->x == .15 && p->y == 0 && p->z == -.12 && (p->present & TFP_PLACE_YAW ? p->yaw : 0) == 0 &&
         (p->present & TFP_PLACE_PITCH ? p->pitch : 0) == 0 && (p->present & TFP_PLACE_ROLL ? p->roll : 0) == 0;
}
const char *tfp_wire_settings_source(TfpWire *w, const char *key, TfpAct action, char *out, size_t size) { /* the order of ap:79-102 */
  Settings *st = state(w);
  TfpActionConfig config;
  if (!st || !size) return "";
  config_of(st, &config);
  const TfpSettingsValue *profiles = st->store.field[TFP_SET_action_placements], *older = st->store.field[TFP_SET_weapon_placements];
  int own = key ? tfp_settings_row(profiles, key) : -1, shared = tfp_settings_row(profiles, "__all");
  const char *prefix = "";
  for (;;) {
    if ((unsigned)action < TFP_ACT_COUNT && own >= 0 && (profiles->profiles[own].has & 1u << action) &&
        tfp_action_valid(&profiles->profiles[own].action[action])) { snprintf(out, size, "%simported(own)", prefix); return out; }
    if ((unsigned)action < TFP_ACT_COUNT && shared >= 0 && (profiles->profiles[shared].has & 1u << action) &&
        tfp_action_valid(&profiles->profiles[shared].action[action])) { snprintf(out, size, "%simported(__all)", prefix); return out; }
    if (action == TFP_ACT_ADS) { snprintf(out, size, "ads default"); return out; }
    if (action == TFP_ACT_IDLE) break;
    action = TFP_ACT_IDLE, prefix = "idle>";
  }
  int row = key ? tfp_settings_row(older, key) : -1;
  if (row >= 0 && tfp_action_valid(&older->placements[row].placement)) { snprintf(out, size, "%simported(older own)", prefix); return out; }
  row = tfp_settings_row(older, "__all");
  if (row >= 0 && tfp_action_valid(&older->placements[row].placement)) { snprintf(out, size, "%simported(older __all)", prefix); return out; }
  bool valid = tfp_action_valid(config.placement);
  const TfpActionWeapon *info = tfp_action_weapon_info(&st->catalogue, key);
  TfpActionPlacement baseline;
  if (config.family_placement_defaults && (!valid || standard(config.placement)) && tfp_action_family_baseline(&st->catalogue, key, &baseline))
    snprintf(out, size, "%sfamily(%s)", prefix, info ? tfp_action_family_name(info->family) : "?");
  else snprintf(out, size, "%s%s", prefix, valid && !standard(config.placement) ? "imported(global)" : "shipped default");
  return out;
}
void tfp_wire_settings_frame(TfpWire *w) {
  Settings *st = state(w);
  if (!st) return;
  const char *key = w->ops.has_placement_weapon_key ? w->ops.placement_weapon_key : NULL;
  if (!key || (st->has_weapon && !strcmp(st->weapon, key))) return;
  snprintf(st->weapon, sizeof st->weapon, "%s", key);
  st->has_weapon = true;
  TfpActionPlacement p = {0, 0, 0, 0, 0, 0, 0};
  char line[400], source[48];
  double hip, aim;
  bool profile;
  const TfpActionWeapon *info = tfp_action_weapon_info(&st->catalogue, key);
  tfp_wire_settings_resolve(w, key, TFP_ACT_IDLE, &p, NULL);
  tfp_settings_near_options(&st->store, key, &hip, &aim, &profile);
  snprintf(line, sizeof line, "Weapon: key=%s name=\"%s\" family=%s placement=%s x=%.4f y=%.4f z=%.4f yaw=%.2f pitch=%.2f roll=%.2f near=%s hip=%.2fm aim=%.2fm",
           key, info ? info->name : "?", info ? tfp_action_family_name(info->family) : "none",
           tfp_wire_settings_source(w, key, TFP_ACT_IDLE, source, sizeof source), p.x, p.y, p.z,
           p.present & TFP_PLACE_YAW ? p.yaw : 0, p.present & TFP_PLACE_PITCH ? p.pitch : 0, p.present & TFP_PLACE_ROLL ? p.roll : 0,
           profile ? "imported(own)" : "global", hip, aim);
  tfp_wire_say(w, line);
}

/* ---- joining ------------------------------------------------------------------------------------ */
bool tfp_wire_settings_bind(TfpWire *w, const char *json, size_t length, const char *origin) {
  Settings *st = calloc(1, sizeof *st);
  char why[220], line[420];
  if (!st) { tfp_wire_say(w, "Refused: settings: out of memory; the view build's answers stay"); return false; }
  st->w = w;
  bool loaded = tfp_settings_load(&st->store, json, length, why, sizeof why);
  if (!loaded && !st->store.field[TFP_SET_placement]) { /* not even the defaults could be made */
    free(st);
    tfp_wire_say(w, "Refused: settings: out of memory; the view build's answers stay");
    return false;
  }
  st->loaded = loaded, st->refused[TFP_SETTINGS_FILE] = !loaded;
  st->unread[TFP_LOCKED_FILE] = true; /* until tfp_wire_settings_bind_locked is handed the file's bytes */
  st->catalogue = (TfpActionCatalogue){st->weapons, TFP_ACTION_GEAR_COUNT, 0};
  tfp_action_configure_catalogue(&st->catalogue, tfp_action_gear, TFP_ACTION_GEAR_COUNT);
  w->ext[TFP_WIRE_EXT_SETTINGS] = st;
  st->previous_decode = w->placement_decode;
  w->placement_decode = decode;

  TfpRtHost *h = &w->host;
  h->actions_configure_catalogue = rt_configure_catalogue;
  h->actions_resolve = rt_resolve;
  h->actions_saved = rt_saved;
  h->actions_default_placement = rt_default_placement;
  h->actions_weapon_info = rt_weapon_info;
  h->actions_observed = rt_observed;
  h->actions_actions = rt_actions;
  h->config_table = rt_config_table;
  h->config_field = rt_config_field;
  h->config_action_mapped = rt_config_action_mapped;
  h->mod_set = rt_mod_set;
  h->menu_native_ads = rt_menu_native_ads;
  h->menu_ads_mode = rt_menu_ads_mode;
  h->menu_show_torso = rt_menu_show_torso;
  h->menu_fird_person_arms = rt_menu_arms;
  h->menu_motion_options = rt_menu_motion_options;
  h->menu_near_options = rt_menu_near_options;
  w->near_io.options = near_options;

  if (!loaded) {
    snprintf(line, sizeof line, "Refused: settings: %s%s%s; retail's defaults are used", origin ? origin : "", origin ? ": " : "", why);
    tfp_wire_say(w, line);
  }
  const TfpSettingsValue *profiles = st->store.field[TFP_SET_action_placements], *older = st->store.field[TFP_SET_weapon_placements],
                         *clips = st->store.field[TFP_SET_near_clip_profiles];
  size_t weapons = 0, placements = 0, shared = 0;
  for (size_t i = 0; profiles && i < profiles->count; i++) {
    size_t n = 0;
    for (int action = 0; action < TFP_ACT_COUNT; action++) n += profiles->profiles[i].has >> action & 1;
    if (!strcmp(profiles->ids[i], "__all")) shared = n; else weapons++, placements += n;
  }
  double hip, aim;
  TfpMotionOptions options;
  tfp_settings_near_options(&st->store, NULL, &hip, &aim, NULL);
  tfp_settings_motion_options(&st->store, true, 1, &options);
  snprintf(line, sizeof line,
           "Ready: settings %s%s: %zu weapon placement profiles (%zu action placements), %zu shared (__all) action placements, "
           "%zu older per-weapon placements, family defaults %s, %zu weapon clipping profiles, near clip hip %.2f m aim %.2f m, "
           "motion %s scale %.2f sprint %.2f, catalogue %zu weapons; kept in memory only",
           loaded ? "imported from " : "are retail's defaults", loaded ? (origin ? origin : "the settings text") : "", weapons, placements, shared,
           older ? older->count : 0, tfp_settings_flag(&st->store, TFP_SET_family_placement_defaults) == 1 ? "on" : "off",
           clips ? clips->count : 0, hip, aim, options.enabled == TFP_MOTION_TRUE ? "on" : "off", options.scale.value,
           options.setting[TFP_MOTION_SPRINT_STRENGTH].value, st->catalogue.count);
  tfp_wire_say(w, line);
  return true;
}
void tfp_wire_settings_bind_locked(TfpWire *w, const char *json, size_t length, const char *origin) {
  Settings *st = state(w);
  char line[500], why[200] = "";
  if (!st) return;
  if (!origin) origin = "locked_view.json";
  if (st->store.locked_skipped[0]) { /* from settings.json's own "locked_view" member */
    snprintf(line, sizeof line, "Note: settings: the \"locked_view\" member held what this build does not read; skipped: %s", st->store.locked_skipped);
    tfp_wire_say(w, line);
  }
  if (!json || !length) return;
  st->unread[TFP_LOCKED_FILE] = false;
  memcpy(st->member, st->store.locked, sizeof st->member); /* tfp_wire_settings_disk decides between the two */
  st->locked_loaded = tfp_settings_load_locked(&st->store, json, length, why, sizeof why);
  st->refused[TFP_LOCKED_FILE] = !st->locked_loaded;
  if (!st->locked_loaded)
    snprintf(line, sizeof line, "Refused: locked view options: %s: %s; they stay %s", origin, why,
             st->store.locked_in_settings ? "as settings.json has them" : "at their defaults");
  else if (st->store.locked_skipped[0])
    snprintf(line, sizeof line, "Note: locked view options: %s names what this build does not know; skipped: %s", origin, st->store.locked_skipped);
  else snprintf(line, sizeof line, "Ready: locked view options read from %s", origin);
  tfp_wire_say(w, line);
}
/* ---- saving ---- */
static const char *const file_names[2] = {"settings.json", "locked_view.json"};
static uint64_t revision(const Settings *st, TfpSettingsFile file) {
  return file == TFP_LOCKED_FILE ? st->store.locked_writes : st->store.writes + st->store.menu_writes;
}
/* Before the session's first write of a file: the file as it is on disk is copied aside when the store does not hold
 * what it says. Two cases: it was refused at load (<name>.refused), or no byte of it was bound (<name>.unread: it
 * could not be opened at the start, and what is loaded came from the file beside the DLL or is the defaults). In the
 * second case there is usually no file at all (the first run), which the copy answers. false: `why`; no write. */
static bool keep_aside(Settings *st, TfpSettingsFile file, char *why, size_t why_size) {
  const bool unread = st->unread[file];
  const char *const stem = unread ? ".unread" : ".refused", *const what = unread ? "could not be opened at the start" : "could not be read";
  char suffix[24], line[300];
  if (!unread && !st->refused[file]) return true;
  if (!st->disk.copy) { snprintf(why, why_size, "a %s that %s cannot be copied aside first", file_names[file], what); return false; }
  snprintf(suffix, sizeof suffix, "%s", stem);
  for (int n = 2;; n++) {
    TfpSettingsCopy answer = st->disk.copy(st->disk.user, file, suffix, why, why_size);
    if (answer == TFP_COPY_DONE) {
      snprintf(line, sizeof line, "Kept: the %s that %s is now %s%s; nothing of it was changed", file_names[file], what, file_names[file], suffix);
      tfp_wire_say(st->w, line);
    }
    if (answer == TFP_COPY_DONE || answer == TFP_COPY_NONE) { st->unread[file] = st->refused[file] = false; return true; }
    if (answer == TFP_COPY_FAILED) {
      char reason[160];
      snprintf(reason, sizeof reason, "%s", why);
      snprintf(why, why_size, "the %s that %s could not be copied to %s%s first (%s)", file_names[file], what, file_names[file], suffix, reason);
      return false;
    }
    if (n > 9) { snprintf(why, why_size, "%s%s to %s-9 are all taken; move them away", file_names[file], stem, stem); return false; }
    snprintf(suffix, sizeof suffix, "%s-%d", stem, n);
  }
}
/* The file as it is on disk to <name><stem>, or to <name><stem>-2 .. -9 when that name is taken: the first free one,
 * and a taken name is never written over. DONE: `suffix` is the one used. NONE: there is no such file. FAILED: `why`
 * (also when all nine names are taken, or the disk has no copy). Never EXISTS. */
static TfpSettingsCopy copy_free_name(Settings *st, TfpSettingsFile file, const char *stem, char *suffix, size_t suffix_size, char *why, size_t why_size) {
  char reason[160] = "";
  if (!st->disk.copy) { snprintf(why, why_size, "%s cannot be copied on this disk", file_names[file]); return TFP_COPY_FAILED; }
  snprintf(suffix, suffix_size, "%s", stem);
  for (int n = 2;; n++) {
    TfpSettingsCopy answer = st->disk.copy(st->disk.user, file, suffix, reason, sizeof reason);
    if (answer == TFP_COPY_DONE || answer == TFP_COPY_NONE) return answer;
    if (answer == TFP_COPY_FAILED) {
      snprintf(why, why_size, "%s could not be copied to %s%s: %s", file_names[file], file_names[file], suffix, reason[0] ? reason : "no reason given");
      return TFP_COPY_FAILED;
    }
    if (n > 9) { snprintf(why, why_size, "%s%s to %s-9 are all taken; move them away", file_names[file], stem, stem); return TFP_COPY_FAILED; }
    snprintf(suffix, suffix_size, "%s-%d", stem, n);
  }
}
static bool write_file(Settings *st, TfpSettingsFile file, size_t *written, char *why, size_t why_size);
/* Before a settings.json that holds "locked_view" (0.6.0-feel.20 to feel.24) is replaced by one without it: the
 * options are in locked_view.json, and the file as it is has a copy of its own (.before-feel25, or the next free of
 * .before-feel25-2 .. -9 when an earlier conversion took that name: an older build wrote the member again since, and
 * the file as found now is not the one the earlier copy holds). Whatever replaces that settings.json goes through
 * here, at the start or later, so neither can be skipped. false: `why`, and settings.json is not written. */
static bool convert_first(Settings *st, char *why, size_t why_size) {
  char reason[240] = "";
  size_t length = 0;
  /* locked_view.json is written when it was not there, not opened or refused (the last two: copied aside first, as
   * before any write of it), and when the member's newer values replaced its own (write_file copies it aside first). */
  if (!st->locked_loaded || st->locked_stale) {
    if (!write_file(st, TFP_LOCKED_FILE, &length, reason, sizeof reason)) { snprintf(why, why_size, "locked_view.json could not be written: %s", reason); return false; }
    st->locked_loaded = true;
  }
  if (st->split_kept[0]) return true; /* this session's copy is made: a save that failed after it does not make another */
  char suffix[sizeof st->split_kept];
  TfpSettingsCopy copy = copy_free_name(st, TFP_SETTINGS_FILE, TFP_SETTINGS_BEFORE_SPLIT, suffix, sizeof suffix, why, why_size);
  if (copy == TFP_COPY_DONE) { memcpy(st->split_kept, suffix, sizeof suffix); return true; }
  if (copy == TFP_COPY_NONE) snprintf(why, why_size, "settings.json could not be copied to settings.json" TFP_SETTINGS_BEFORE_SPLIT ": the file is not there");
  return false;
}
/* One file from the store. true: written (and counted as saved). false: `why`. */
static bool write_file(Settings *st, TfpSettingsFile file, size_t *written, char *why, size_t why_size) {
  size_t (*const text_of_store)(const TfpSettings *, char *, size_t) = file == TFP_LOCKED_FILE ? tfp_settings_write_locked : tfp_settings_write;
  const uint64_t now = revision(st, file);
  st->tried_revision[file] = now;
  if (!st->disk.write) { snprintf(why, why_size, "there is nowhere to write to"); return false; }
  if (!keep_aside(st, file, why, why_size)) return false;
  if (file == TFP_SETTINGS_FILE && st->convert && !convert_first(st, why, why_size)) return false;
  if (file == TFP_LOCKED_FILE && st->locked_stale && !st->stale_kept[0]) {
    /* The file still says what settings.json's newer member replaced: it is not written over without a copy. */
    char suffix[sizeof st->stale_kept], line[300];
    TfpSettingsCopy copy = copy_free_name(st, file, TFP_LOCKED_REPLACED, suffix, sizeof suffix, why, why_size);
    if (copy == TFP_COPY_FAILED) return false;
    if (copy == TFP_COPY_DONE) {
      memcpy(st->stale_kept, suffix, sizeof suffix);
      snprintf(line, sizeof line, "Kept: the locked_view.json whose values settings.json's newer \"locked_view\" member replaced is now locked_view.json%s; nothing of it was changed", suffix);
      tfp_wire_say(st->w, line);
    }
  }
  size_t length = text_of_store(&st->store, NULL, 0);
  char *text = length < TFP_SETTINGS_MAX ? malloc(length + 1) : NULL;
  bool ok = false;
  if (!text) snprintf(why, why_size, "the text is %zu bytes (most: %d)", length, TFP_SETTINGS_MAX);
  else if (text_of_store(&st->store, text, length + 1) != length) snprintf(why, why_size, "the store changed while it was written");
  else ok = st->disk.write(st->disk.user, file, text, length, why, why_size);
  free(text);
  if (ok) {
    st->saved_revision[file] = now, *written = length;
    if (file == TFP_SETTINGS_FILE) st->convert = false;
    else st->locked_stale = false;
  } else if (!why[0]) snprintf(why, why_size, "no reason given");
  return ok;
}
void tfp_wire_settings_disk(TfpWire *w, const TfpSettingsDisk *disk, bool user_file) {
  Settings *st = state(w);
  char line[900], why[400] = "";
  size_t length = 0;
  if (!st) return;
  if (disk) st->disk = *disk; else memset(&st->disk, 0, sizeof st->disk);
  for (int file = 0; file < 2; file++) st->saved_revision[file] = st->tried_revision[file] = revision(st, (TfpSettingsFile)file); /* what is loaded is what the files hold */
  /* Not the user's file: whatever was refused was not it either, and what is in its place has not been seen. */
  st->unread[TFP_SETTINGS_FILE] = !user_file;
  if (!user_file) st->refused[TFP_SETTINGS_FILE] = false;
  st->convert = disk && disk->write && user_file && st->loaded && st->store.locked_in_settings;
  if (!st->convert) return;
  /* The user's settings.json is one that 0.6.0-feel.20 to feel.24 wrote. Converted now, not at the next change.
   * When locked_view.json was there and read as well, an older build has run since this one made it (that build
   * does not read locked_view.json and writes the member on every save): the member is the newer of the two, and
   * the options it names are taken from it. What it does not name stays as locked_view.json has it. */
  const bool had_locked = st->locked_loaded;
  for (int id = 0; had_locked && id < TFP_LOCKED_COUNT; id++) {
    if (!(st->store.locked_named >> id & 1)) continue;
    if (st->store.locked[id].boolean != st->member[id].boolean || st->store.locked[id].number != st->member[id].number) st->locked_stale = true;
    st->store.locked[id] = st->member[id];
  }
  const bool replaced = st->locked_stale;
  char note[200] = "";
  if (write_file(st, TFP_SETTINGS_FILE, &length, why, sizeof why)) {
    if (replaced)
      snprintf(note, sizeof note, " (that file was already there with other values; settings.json's are the newer ones and are used, and the file as it was is "
               "kept as locked_view.json%s)", st->stale_kept[0] ? st->stale_kept : TFP_LOCKED_REPLACED);
    else if (had_locked) snprintf(note, sizeof note, " (that file was already there and says the same)");
    snprintf(line, sizeof line, "Converted: settings.json was written by 0.6.0-feel.20 to feel.24 and held the locked view's options; they are in "
             "locked_view.json now%s, settings.json is as it was without them (0.6.0-feel.19 and older read it again), and the file as it was is "
             "kept as settings.json%s", note, st->split_kept);
  }
  else
    snprintf(line, sizeof line, "Refused: settings.json still holds the locked view's options, which 0.6.0-feel.19 and older cannot read: %.300s; "
             "this session uses the values as loaded, and the conversion is tried again at the next save or the next start", why);
  tfp_wire_say(w, line);
}
uint64_t tfp_wire_settings_mirror_begin(TfpWire *w) {
  Settings *st = state(w);
  return st ? revision(st, TFP_SETTINGS_FILE) : 0;
}
void tfp_wire_settings_mirror_end(TfpWire *w, uint64_t mark) {
  Settings *st = state(w);
  if (!st) return;
  const uint64_t now = revision(st, TFP_SETTINGS_FILE);
  /* Only when nothing was waiting at the mark: a user's change made before it is still written, mirror and all. */
  if (now != mark && st->saved_revision[TFP_SETTINGS_FILE] == mark)
    st->saved_revision[TFP_SETTINGS_FILE] = st->tried_revision[TFP_SETTINGS_FILE] = now;
}
bool tfp_wire_settings_unsaved(TfpWire *w) {
  Settings *st = state(w);
  return st && st->disk.write && (revision(st, TFP_SETTINGS_FILE) != st->saved_revision[TFP_SETTINGS_FILE] ||
                                  revision(st, TFP_LOCKED_FILE) != st->saved_revision[TFP_LOCKED_FILE]);
}
bool tfp_wire_settings_menu_set(TfpWire *w, TfpSettingsMenuId id, bool is_bool, bool boolean, double number) {
  Settings *st = state(w);
  return st && tfp_settings_set_menu(&st->store, id, is_bool, boolean, number);
}
bool tfp_wire_settings_locked_set(TfpWire *w, TfpSettingsLockedId id, bool is_bool, bool boolean, double number) {
  Settings *st = state(w);
  return st && tfp_settings_set_locked(&st->store, id, is_bool, boolean, number);
}
#define LOCKED_GETTER_1(name, value) \
  bool tfp_locked_view_##name(TfpWire *w) { Settings *st = state(w); return st ? tfp_settings_locked_flag(&st->store, TFP_LOCKED_##name) : (value) != 0; }
#define LOCKED_GETTER_0(name, value) \
  double tfp_locked_view_##name(TfpWire *w) { Settings *st = state(w); return st ? tfp_settings_locked_number(&st->store, TFP_LOCKED_##name) : (value); }
#define LOCKED_GETTER(name, is_bool, value, least, most, step) LOCKED_GETTER_##is_bool(name, value)
TFP_SETTINGS_LOCKED(LOCKED_GETTER)
#undef LOCKED_GETTER
#undef LOCKED_GETTER_0
#undef LOCKED_GETTER_1
void tfp_wire_settings_save_frame(TfpWire *w) {
  static const char *const what[2] = {"settings", "locked view options"};
  Settings *st = state(w);
  if (!st || !st->disk.write) return;
  for (int file = 0; file < 2; file++) {
    const uint64_t now = revision(st, (TfpSettingsFile)file);
    if (now == st->tried_revision[file]) continue;
    char line[500], why[300] = "";
    size_t length = 0;
    if (write_file(st, (TfpSettingsFile)file, &length, why, sizeof why))
      snprintf(line, sizeof line, "Saved: %s (%s, %zu bytes, change %llu)", what[file], file_names[file], length, (unsigned long long)now);
    else
      snprintf(line, sizeof line, "Refused: %s were not saved: %s; they stay as they are for this session and are tried again at the next change", what[file], why);
    tfp_wire_say(w, line);
  }
}

void tfp_wire_settings_free(TfpWire *w) {
  Settings *st = state(w);
  if (!st) return;
  tfp_settings_free(&st->store);
  if (w->placement_decode == decode) w->placement_decode = st->previous_decode;
  w->ext[TFP_WIRE_EXT_SETTINGS] = NULL;
  free(st);
}
