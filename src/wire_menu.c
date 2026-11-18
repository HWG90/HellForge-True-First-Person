/* See wire_menu.h. Retail: src/first_person/menu.lua (0.5.41-smooth.66); line numbers are that file's. */
#include "wire_menu.h"
#include "wire_off.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* K_STATUS: retail's two 'text' rows whose label changes. DBF-MCM for HellForge can relabel only a control it can
 * find by id, and a text block is not one, so here they are buttons that are always disabled. */
typedef enum Kind { K_BUTTON, K_TOGGLE, K_CHECK, K_SLIDER, K_CHOICE, K_KEY, K_STATUS } Kind;
typedef struct Control {
  const char *id; Kind kind; const char *label;
  double min, max, step;
  int menu;                 /* TfpSettingsMenuId the value is stored under, or -1 */
  const char *description;  /* NULL: none */
  int page;                 /* 0 'test' (First person), 1 'movement' (Bob and sway) */
} Control;
#define M(name) TFP_MENU_##name
/* :343-418, in retail's order. Not here: 'start_free_aim' and 'start_native_test' (30-second test tools of
 * modes this build does not wire) and the commented-out Fird Person controls. */
static const Control CONTROLS[] = {
  {"start_true_first_person", K_BUTTON, "Enable True First Person", 0, 0, 0, -1, NULL, 0},
  {"stop_test", K_BUTTON, "Turn first-person view off", 0, 0, 0, -1, NULL, 0},
  {"show_aim_markers", K_CHECK, "Show aim markers", 0, 0, 0, -1,
   "Show the crosshair and aiming circle while aiming in first person. Both remain visible when not aiming. Saved weapon preferences apply "
   "until you change this option. Hit markers and scope markings keep their normal behavior.", 0},
  {"edit_placement", K_BUTTON, "Edit arms and weapon placement", 0, 0, 0, -1, NULL, 0},
  {"editor_key", K_KEY, "Open / close placement editor", 0, 0, 0, M(editor_key),
   "Default Page Up (F8 is HellForge's own editor). Closing the editor restores unsaved placement and leaves first person on.", 0},
  {"toggle_key", K_KEY, "Toggle True First Person", 0, 0, 0, M(toggle_key),
   "Default F11. Toggle True First Person; it stays enabled when changing weapons.", 0},
  {"true_native_ads", K_CHECK, "True First Person: native ADS", 0, 0, 0, M(true_native_ads),
   "Use the game's native sights and zoom while aiming in True First Person.", 0},
  {"show_torso", K_CHECK, "True First Person: show torso", 0, 0, 0, M(show_torso),
   "Keep your torso armour visible in the moments without the first-person arms: holding a stratagem, turning a valve, swapping weapons. "
   "While the first-person arms are up the torso is always hidden. The head and shoulders stay hidden.", 0},
  {"fird_person_arms", K_CHECK, "Fird Person Arms", 0, 0, 0, M(fird_person_arms),
   "Dress the first-person arms like your body: sleeves and shoulders follow your torso, and the left hand holds the weapon grip the way it "
   "does in third person. Off leaves the game's own first-person arms and hand animation untouched.", 0},
  {"family_placement_defaults", K_CHECK, "Use weapon-family placement defaults", 0, 0, 0, -1,
   "Opt in to conservative defaults for catalogue-identified firearms. Saved weapon/action placements and your customised global placement "
   "take precedence. Unknown weapons retain the existing baseline.", 0},
  {"motion_preset", K_CHOICE, "Motion preset", 0, 0, 0, M(motion_preset),
   "Comfort adds restrained weapon movement. Natural adds more weapon motion. Custom restores your existing sliders; selecting a preset never "
   "overwrites them. The game controls camera movement.", 0},
  {"motion_enabled", K_TOGGLE, "Weapon movement", 0, 0, 0, M(motion_enabled),
   "Walking bob, turn inertia and a lowered sprint position. ADS remains steady.", 0},
  {"motion_strength", K_SLIDER, "Weapon movement strength", 0, 200, 5, M(motion_strength), NULL, 0},
  {"motion_bob_enabled", K_CHECK, "Weapon movement: walking bob", 0, 0, 0, M(motion_bob_enabled), "The weapon bobbing with your steps.", 0},
  {"motion_sway_enabled", K_CHECK, "Weapon movement: sway", 0, 0, 0, M(motion_sway_enabled),
   "The weapon lagging behind when you turn or strafe.", 0},
  {"motion_sprint_enabled", K_CHECK, "Weapon movement: sprint pose", 0, 0, 0, M(motion_sprint_enabled),
   "The weapon lowering and turning while you sprint.", 0},
  {"near_clip_cm", K_SLIDER, "Near clip distance (cm)", 5, 40, 1, M(near_clip_cm),
   "Hides anything closer to the first-person camera than this, such as armor edges. 5 is the game's own distance (off). Full ADS always "
   "uses the game's distance.", 0},
  {"near_clip_aim", K_TOGGLE, "Near clip while aiming", 0, 0, 0, M(near_clip_aim),
   "Keep the near clip during normal aim. Off returns to the game's distance while aiming so sights are not cut.", 0},
  {"weapon_composition_status", K_STATUS, "Weapon: no active firearm", 0, 0, 0, -1, NULL, 0},
  {"weapon_near_hip_cm", K_SLIDER, "This weapon: hip clearance (cm)", 5, 40, 1, M(weapon_near_hip_cm),
   "Draft for this firearm only. Save below to apply. 5 keeps the game distance.", 0},
  {"weapon_near_aim_cm", K_SLIDER, "This weapon: normal-aim clearance (cm)", 5, 40, 1, M(weapon_near_aim_cm),
   "Independent normal-aim clearance. Full native ADS always keeps the game distance.", 0},
  {"save_weapon_near", K_BUTTON, "Save clipping for this weapon", 0, 0, 0, -1, NULL, 0},
  {"reset_weapon_near", K_BUTTON, "Use global clipping for this weapon", 0, 0, 0, -1, NULL, 0},
  {"runtime_status", K_STATUS, "Status: First person off", 0, 0, 0, -1, NULL, 0},
  /* :21-27, :334 (step 5) */
  {"weapon_bob_strength", K_SLIDER, "Weapon bob strength", 0, 300, 5, M(weapon_bob_strength), NULL, 1},
  {"weapon_bob_speed", K_SLIDER, "Weapon bob speed", 25, 200, 5, M(weapon_bob_speed), NULL, 1},
  {"weapon_sway_strength", K_SLIDER, "Weapon sway strength", 0, 300, 5, M(weapon_sway_strength), NULL, 1},
  {"weapon_sway_speed", K_SLIDER, "Weapon sway response speed", 25, 200, 5, M(weapon_sway_speed), NULL, 1},
  {"sprint_motion_strength", K_SLIDER, "Sprint weapon movement", 0, 200, 5, M(sprint_motion_strength), NULL, 1},
};
#undef M
enum { CONTROL_COUNT = sizeof CONTROLS / sizeof *CONTROLS };

typedef struct Menu {
  TfpMenuHost host;
  TfpMenuEditFn edit;
  unsigned unavailable;
  bool syncing;                         /* a set of ours is in flight: its `changed` is not a user's change */
  char status[300], weapon[200];        /* the two labels as last sent */
  char clip_key[TFP_SETTINGS_ID]; char clip_identity[24]; bool has_clip_target; /* :204-229 clip_target */
  bool drawn_weapon; char drawn_key[TFP_SETTINGS_ID], drawn_identity[24]; bool drawn_profile; double drawn_hip, drawn_aim;
  char message[200];                    /* a button's answer */
  /* What the user did in MCM, kept for the next tick: MCM calls back from its own update, outside this
   * mod's tick (no engine frame, the runtime not in its update), so nothing that reaches the runtime runs there. */
  struct { char id[40]; TfpMenuValue value, previous; } queued[8];
  unsigned queue_count;
} Menu;
static Menu *state(TfpWire *w) { return w->ext[TFP_WIRE_EXT_MENU]; }
static const Control *control(const char *id) {
  for (int i = 0; id && i < CONTROL_COUNT; i++) if (!strcmp(CONTROLS[i].id, id)) return &CONTROLS[i];
  return NULL;
}
static void say(TfpWire *w, const char *format, const char *a, const char *b) {
  char line[420];
  snprintf(line, sizeof line, format, a, b);
  tfp_wire_say(w, line);
}
static void text_of(TfpRtValue v, char *out, size_t size) {
  size_t n = v.kind == TFP_RT_TEXT && v.text ? (v.length < size - 1 ? v.length : size - 1) : 0;
  memcpy(out, n ? v.text : "", n), out[n] = 0;
}
/* One runtime entry, as retail's menu calls `actions.<name>(...)` through pcall. */
static bool action(TfpWire *w, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue r[30], char *why, size_t size) {
  memset(r, 0, 30 * sizeof *r);
  TfpRtStatus status = tfp_wire_call(w, entry, args, r);
  if (status == TFP_RT_OK) return true;
  if (status == TFP_RT_RAISED) text_of(w->rt.error, why, size); else snprintf(why, size, "First person is not started yet");
  return false;
}

/* ---- the definition ---- */
static size_t put(char *out, size_t size, size_t at, const char *text) {
  size_t n = strlen(text);
  if (out && at < size) memcpy(out + at, text, n < size - at ? n : size - at);
  return at + n;
}
static size_t put_text(char *out, size_t size, size_t at, const char *text) { /* a JSON string */
  char one[8];
  at = put(out, size, at, "\"");
  for (const unsigned char *c = (const unsigned char *)text; *c; c++) {
    if (*c == '"' || *c == '\\') snprintf(one, sizeof one, "\\%c", *c);
    else if (*c < 0x20) snprintf(one, sizeof one, "\\u%04x", *c);
    else snprintf(one, sizeof one, "%c", *c);
    at = put(out, size, at, one);
  }
  return put(out, size, at, "\"");
}
static bool current_bool(TfpWire *w, const Control *c) {
  TfpSettings *s = tfp_wire_settings(w);
  if (c->menu >= 0) return tfp_settings_menu_flag(s, (TfpSettingsMenuId)c->menu);
  if (!strcmp(c->id, "family_placement_defaults")) return tfp_settings_flag(s, TFP_SET_family_placement_defaults) == 1;
  if (!strcmp(c->id, "show_aim_markers")) { /* :48-50 marker_setting() */
    TfpRtValue r[30]; char why[8];
    return action(w, TFP_RT_E_aim_markers_setting, NULL, r, why, sizeof why) && r[0].kind == TFP_RT_TRUE;
  }
  return false;
}
size_t tfp_wire_menu_definition(TfpWire *w, int editor_key, char *out, size_t size) {
  TfpSettings *s = tfp_wire_settings(w);
  char number[96];
  size_t at = 0;
  if (!s) return 0;
  at = put(out, size, at, "{\"id\":\"" TFP_MENU_MOD_ID "\",\"name\":\"True First Person\",\"description\":");
  at = put_text(out, size, at, "Use the game's first-person arms and weapon view. F11 toggles True First Person.");
  at = put(out, size, at, ",\"pages\":[");
  for (int page = 0; page < 2; page++) {
    at = put(out, size, at, page ? ",{\"id\":\"movement\",\"name\":\"Bob and sway\"" : "{\"id\":\"test\",\"name\":\"First person\"");
    at = put(out, size, at, ",\"require_confirmation\":false,\"controls\":[");
    bool first = true;
    for (int i = 0; i < CONTROL_COUNT; i++) {
      const Control *c = &CONTROLS[i];
      static const char *const types[] = {"button", "toggle", "toggle", "slider", "choice", "keybind", "button"};
      if (c->page != page) continue;
      at = put(out, size, at, first ? "{\"id\":\"" : ",{\"id\":\""), first = false;
      at = put(out, size, at, c->id), at = put(out, size, at, "\",\"type\":\""), at = put(out, size, at, types[c->kind]);
      at = put(out, size, at, "\",\"label\":"), at = put_text(out, size, at, c->label);
      if (c->kind == K_CHECK) at = put(out, size, at, ",\"presentation\":\"checkbox\"");
      if (c->kind == K_BUTTON || c->kind == K_STATUS || !strcmp(c->id, "show_aim_markers")) at = put(out, size, at, ",\"require_confirmation\":false");
      if (c->kind == K_TOGGLE || c->kind == K_CHECK) at = put(out, size, at, current_bool(w, c) ? ",\"default\":true" : ",\"default\":false");
      if (c->kind == K_SLIDER) {
        snprintf(number, sizeof number, ",\"min\":%g,\"max\":%g,\"step\":%g", c->min, c->max, c->step), at = put(out, size, at, number);
        double value = tfp_settings_menu_number(s, (TfpSettingsMenuId)c->menu);
        if (!(value >= c->min && value <= c->max)) value = value < c->min ? c->min : c->max; /* MCM refuses a default outside the range */
        snprintf(number, sizeof number, ",\"default\":%.17g", value), at = put(out, size, at, number);
      }
      if (c->kind == K_CHOICE) {
        double value = tfp_settings_menu_number(s, (TfpSettingsMenuId)c->menu);
        at = put(out, size, at, ",\"presentation\":\"dropdown\",\"choices\":[\"Custom (keep existing sliders)\",\"Comfort\",\"Natural\"]");
        snprintf(number, sizeof number, ",\"default\":%d", value >= 1 && value <= 3 ? (int)value : 1), at = put(out, size, at, number);
      }
      if (c->kind == K_KEY) {
        double value = tfp_settings_menu_number(s, (TfpSettingsMenuId)c->menu);
        int key = value >= 0 && value <= 257 ? (int)value : 0;
        if (c->menu == TFP_MENU_editor_key && key == 119) key = editor_key;
        snprintf(number, sizeof number, ",\"default\":%d", key), at = put(out, size, at, number);
      }
      if (c->description) at = put(out, size, at, ",\"description\":"), at = put_text(out, size, at, c->description);
      at = put(out, size, at, "}");
    }
    at = put(out, size, at, "]}");
  }
  at = put(out, size, at, "]}");
  if (out && size) out[at < size ? at : size - 1] = 0;
  return at;
}

/* ---- registration ---- */
static void into_store(TfpWire *w, const Control *c, const TfpMenuValue *v) {
  bool is_bool = c->kind == K_TOGGLE || c->kind == K_CHECK;
  if (c->menu < 0 || v->kind != (is_bool ? TFP_MENU_VALUE_BOOL : TFP_MENU_VALUE_NUMBER)) return;
  tfp_wire_settings_menu_set(w, (TfpSettingsMenuId)c->menu, is_bool, v->boolean, v->number);
}
bool tfp_wire_menu_bind(TfpWire *w, const TfpMenuHost *host, unsigned unavailable, TfpMenuEditFn edit) {
  if (!tfp_wire_settings(w) || !host || !host->get || !host->set || !host->set_disabled || !host->set_label) return false;
  Menu *m = state(w);
  if (!m && !(m = calloc(1, sizeof *m))) return false;
  memset(m, 0, sizeof *m);
  m->host = *host, m->edit = edit, m->unavailable = unavailable;
  w->ext[TFP_WIRE_EXT_MENU] = m;
  for (int i = 0; i < CONTROL_COUNT; i++) {
    TfpMenuValue v;
    if (CONTROLS[i].menu >= 0 && host->get(host->user, CONTROLS[i].id, &v)) into_store(w, &CONTROLS[i], &v);
  }
  host->set_disabled(host->user, "weapon_composition_status", true), host->set_disabled(host->user, "runtime_status", true);
  if (unavailable & TFP_MENU_NO_ARMS) {
    host->set_disabled(host->user, "fird_person_arms", true);
    tfp_wire_say(w, "Menu: \"Fird Person Arms\" is shown disabled: sleeves and hand grips refused on this build (see their Refused lines)");
  }
  if ((unavailable & TFP_MENU_NO_EDITOR) || !edit) {
    host->set_disabled(host->user, "edit_placement", true), host->set_disabled(host->user, "editor_key", true);
    tfp_wire_say(w, "Menu: the placement editor's button and key are shown disabled: the editor is not available in this session");
  }
  tfp_wire_say(w, "Ready: menu: registered with DBF-MCM as \"True First Person\" (two pages; not offered: free aim and the native ADS comparison, "
                  "which are retail's 30-second test tools)");
  return true;
}
void tfp_wire_menu_unbind(TfpWire *w) { free(state(w)); w->ext[TFP_WIRE_EXT_MENU] = NULL; }
bool tfp_wire_menu_bound(TfpWire *w) { return state(w) != NULL; }
int tfp_wire_menu_toggle_key(TfpWire *w) {
  TfpSettings *s = tfp_wire_settings(w);
  double key = s ? tfp_settings_menu_number(s, TFP_MENU_toggle_key) : 122;
  return key >= 1 && key <= 255 ? (int)key : 0; /* 0: unbound */
}
int tfp_wire_menu_editor_key(TfpWire *w, int fallback) {
  TfpSettings *s = tfp_wire_settings(w);
  double key = s ? tfp_settings_menu_number(s, TFP_MENU_editor_key) : fallback;
  if (key == 119) return fallback;
  return key >= 1 && key <= 255 ? (int)key : 0;
}

/* ---- callbacks ---- */
static void put_back(TfpWire *w, Menu *m, const char *id, const TfpMenuValue *previous) {
  if (!previous || previous->kind == TFP_MENU_VALUE_NONE) return;
  m->syncing = true, m->host.set(m->host.user, id, previous), m->syncing = false;
  (void)w;
}
static void enqueue(TfpWire *w, Menu *m, const char *id, const TfpMenuValue *value, const TfpMenuValue *previous) {
  if (m->queue_count == sizeof m->queued / sizeof *m->queued) { say(w, "Menu: \"%s\" was dropped: %s", id, "too many actions in one update"); return; }
  snprintf(m->queued[m->queue_count].id, sizeof m->queued[0].id, "%s", id);
  m->queued[m->queue_count].value = value ? *value : (TfpMenuValue){0, false, 0};
  m->queued[m->queue_count++].previous = previous ? *previous : (TfpMenuValue){0, false, 0};
}
void tfp_wire_menu_changed(TfpWire *w, const char *id, const TfpMenuValue *value, const TfpMenuValue *previous) {
  Menu *m = state(w);
  const Control *c = control(id);
  if (!m || !c || !value || m->syncing) return;
  if (c->menu >= 0) { into_store(w, c, value); return; } /* the store only: safe at once */
  if (value->kind == TFP_MENU_VALUE_BOOL) enqueue(w, m, id, value, previous);
}
static void run_toggle(TfpWire *w, Menu *m, const char *id, const TfpMenuValue *value, const TfpMenuValue *previous) {
  const Control *c = control(id);
  TfpRtValue r[30];
  TfpRtArgs args;
  char why[200] = "";
  memset(&args, 0, sizeof args);
  bool done = false;
  if (!strcmp(id, "show_aim_markers")) { /* :349-361 */
    args.v[0] = tfp_wire_bool(value->boolean);
    done = action(w, TFP_RT_E_set_aim_markers_setting, &args, r, why, sizeof why) && r[0].kind == TFP_RT_TRUE;
    if (!done && !why[0]) text_of(r[1], why, sizeof why);
    if (!done && !why[0]) snprintf(why, sizeof why, "Aim-marker setting is unavailable");
  } else if (!strcmp(id, "family_placement_defaults")) { /* :381-388 */
    args.v[0] = tfp_wire_text("family_placement_defaults"), args.v[1] = tfp_wire_bool(value->boolean);
    done = action(w, TFP_RT_E_save_presentation, &args, r, why, sizeof why) && r[0].kind == TFP_RT_TRUE;
    if (!done && !why[0]) text_of(r[1], why, sizeof why);
    if (!done && !why[0]) snprintf(why, sizeof why, "Placement preference could not be saved");
  } else return;
  if (!done) { say(w, "Menu: \"%s\" was not changed: %s", c->label, why); put_back(w, m, id, previous); }
}
/* :11 clip_cm, :12 resource_key */
static bool clip_cm(double v) { return v == v && v >= 5 && v <= 40 && v == (double)(long long)v; }
static bool resource_key(const char *k) {
  if (strlen(k) != 16) return false;
  for (int i = 0; i < 16; i++) if (!((k[i] >= '0' && k[i] <= '9') || (k[i] >= 'a' && k[i] <= 'f') || (k[i] >= 'A' && k[i] <= 'F'))) return false;
  return true;
}
/* :133-137 current_weapon(): key (16 hex) and the identity's text. */
static bool current_weapon(TfpWire *w, char key[TFP_SETTINGS_ID], char identity[24], char *label, size_t label_size) {
  TfpRtValue r[30]; char why[8], family[48];
  if (!action(w, TFP_RT_E_current_weapon, NULL, r, why, sizeof why) || r[0].kind != TFP_RT_TRUE) return false;
  text_of(r[1], key, TFP_SETTINGS_ID), text_of(r[2], identity, 24);
  if (!resource_key(key) || r[2].kind == TFP_RT_NIL) return false;
  if (label) { /* :232 */
    char name[96];
    text_of(r[3], name, sizeof name), text_of(r[4], family, sizeof family);
    snprintf(label, label_size, "Weapon: %s%s%s%s", name[0] ? name : key, family[0] ? " (" : "", family, family[0] ? ")" : "");
  }
  return true;
}
/* :183-203 save_clip(remove) */
static const char *save_clip(TfpWire *w, Menu *m, bool remove) {
  TfpSettings *s = tfp_wire_settings(w);
  char key[TFP_SETTINGS_ID], identity[24];
  TfpMenuValue hip, aim;
  if (!current_weapon(w, key, identity, NULL, 0) || !m->has_clip_target || strcmp(key, m->clip_key) || strcmp(identity, m->clip_identity))
    return "Weapon changed; reopen its clipping controls";
  bool got = m->host.get(m->host.user, "weapon_near_hip_cm", &hip) && m->host.get(m->host.user, "weapon_near_aim_cm", &aim) &&
             hip.kind == TFP_MENU_VALUE_NUMBER && aim.kind == TFP_MENU_VALUE_NUMBER;
  if (!remove && (!got || !clip_cm(hip.number) || !clip_cm(aim.number))) return "Clip distances must be 5 to 40 cm";
  const TfpSettingsValue *old = s->field[TFP_SET_near_clip_profiles];
  size_t most = (old ? old->count : 0) + 1, count = 0;
  const char **keys = calloc(most, sizeof *keys);
  TfpSettingsNear *rows = calloc(most, sizeof *rows);
  if (!keys || !rows) { free(keys), free(rows); return "Clipping settings could not be saved"; }
  for (size_t i = 0; old && i < old->count; i++) {
    const TfpSettingsNear *row = &old->near_clip[i];
    if (strcmp(old->ids[i], key) && resource_key(old->ids[i]) && row->has_hip && row->has_aim && clip_cm(row->hip_cm) && clip_cm(row->aim_cm))
      keys[count] = old->ids[i], rows[count++] = *row;
  }
  if (!remove) keys[count] = key, rows[count++] = (TfpSettingsNear){true, true, hip.number, aim.number};
  const TfpSettingsValue *made = tfp_settings_new_near(s, keys, rows, count);
  free(keys), free(rows);
  if (!made) return "Clipping settings could not be saved";
  TfpRtArgs args; TfpRtValue r[30];
  memset(&args, 0, sizeof args);
  args.v[0] = tfp_wire_text("near_clip_profiles"), args.v[1] = (TfpRtValue){TFP_RT_REF, 0, 0, 0, made};
  args.v[2] = tfp_wire_text(tfp_wire_keep(w, key)), args.v[3] = tfp_wire_text(tfp_wire_keep(w, identity));
  m->message[0] = 0;
  if (action(w, TFP_RT_E_save_presentation, &args, r, m->message, sizeof m->message) && r[0].kind == TFP_RT_TRUE) {
    m->drawn_weapon = false; /* :201 clip_source=nil: the drafts are read again */
    return NULL;
  }
  if (!m->message[0]) text_of(r[1], m->message, sizeof m->message);
  return m->message[0] ? m->message : "Clipping settings could not be saved";
}
static const char *run_button(TfpWire *w, Menu *m, const char *id) {
  TfpRtValue r[30];
  TfpRtArgs args;
  memset(&args, 0, sizeof args);
  m->message[0] = 0;
  if (!strcmp(id, "start_true_first_person") || !strcmp(id, "stop_test")) { /* :87-94, :95-104 */
    bool start = id[2] == 'a';
    if (!start) args.v[0] = tfp_wire_text("manual stop");
    bool done = action(w, start ? TFP_RT_E_start_true_first_person : TFP_RT_E_stop_pose, &args, r, m->message, sizeof m->message) &&
                r[0].kind == TFP_RT_TRUE;
    if (done) return NULL;
    if (!m->message[0]) text_of(r[1], m->message, sizeof m->message);
    if (!m->message[0]) snprintf(m->message, sizeof m->message, start ? "First person could not start" : "First person could not stop");
    return m->message;
  }
  if (!strcmp(id, "edit_placement")) return m->edit && !(m->unavailable & TFP_MENU_NO_EDITOR) ? m->edit(w) : "Placement editor is unavailable";
  if (!strcmp(id, "save_weapon_near")) return save_clip(w, m, false);
  if (!strcmp(id, "reset_weapon_near")) return save_clip(w, m, true);
  return "Unknown button";
}
/* A button: taken now, done in the next tick; what it answered is in the log and, when it failed, in the status line. */
const char *tfp_wire_menu_activate(TfpWire *w, const char *id) {
  Menu *m = state(w);
  const Control *c = control(id);
  if (!m || !c || c->kind != K_BUTTON) return "Unknown button";
  enqueue(w, m, id, NULL, NULL);
  return NULL;
}
static void run_queue(TfpWire *w, Menu *m) {
  unsigned count = m->queue_count;
  m->queue_count = 0;
  for (unsigned i = 0; i < count; i++) {
    const Control *c = control(m->queued[i].id);
    if (c->kind != K_BUTTON) { run_toggle(w, m, c->id, &m->queued[i].value, &m->queued[i].previous); continue; }
    const char *problem = run_button(w, m, c->id);
    if (problem) say(w, "Menu: \"%s\": %s", c->label, problem);
  }
}

/* :204-234 refresh_weapon_controls and :305 the status label. Compared with what was last sent, so MCM is
 * called only on a change; the reads behind it are the runtime's own state (no game call). What the user did
 * in MCM since the last tick is done first. */
void tfp_wire_menu_frame(TfpWire *w) {
  Menu *m = state(w);
  TfpSettings *s = tfp_wire_settings(w);
  char text[300], key[TFP_SETTINGS_ID] = "", identity[24] = "";
  if (!m || !s) return;
  if (m->queue_count) run_queue(w, m);
  snprintf(text, sizeof text, "Status: %s", w->controller.label[0] ? w->controller.label : "First person off");
  if (strcmp(text, m->status)) { snprintf(m->status, sizeof m->status, "%s", text); m->host.set_label(m->host.user, "runtime_status", text); }
  bool weapon = current_weapon(w, key, identity, text, sizeof text);
  if (!weapon) snprintf(text, sizeof text, "Weapon: enable First Person with a firearm to edit its clipping");
  if (strcmp(text, m->weapon)) {
    snprintf(m->weapon, sizeof m->weapon, "%.199s", text);
    m->host.set_label(m->host.user, "weapon_composition_status", text);
    static const char *const ids[] = {"weapon_near_hip_cm", "weapon_near_aim_cm", "save_weapon_near", "reset_weapon_near"};
    for (int i = 0; i < 4; i++) m->host.set_disabled(m->host.user, ids[i], !weapon);
  }
  /* The drafts follow the weapon and its saved profile (:219-229). */
  const TfpSettingsValue *all = s->field[TFP_SET_near_clip_profiles];
  int row = weapon && all ? tfp_settings_row(all, key) : -1;
  bool profile = row >= 0 && all->near_clip[row].has_hip && all->near_clip[row].has_aim && clip_cm(all->near_clip[row].hip_cm) &&
                 clip_cm(all->near_clip[row].aim_cm);
  double cm = tfp_settings_menu_number(s, TFP_MENU_near_clip_cm);
  if (!clip_cm(cm)) cm = 15;
  double hip = profile ? all->near_clip[row].hip_cm : cm;
  double aim = profile ? all->near_clip[row].aim_cm : tfp_settings_menu_flag(s, TFP_MENU_near_clip_aim) ? cm : 5;
  bool same = m->drawn_weapon && weapon == m->has_clip_target && !strcmp(key, m->drawn_key) && !strcmp(identity, m->drawn_identity) &&
              profile == m->drawn_profile && (!profile || (hip == m->drawn_hip && aim == m->drawn_aim));
  if (same) return;
  m->drawn_weapon = true, m->has_clip_target = weapon, m->drawn_profile = profile, m->drawn_hip = hip, m->drawn_aim = aim;
  snprintf(m->drawn_key, sizeof m->drawn_key, "%s", key), snprintf(m->drawn_identity, sizeof m->drawn_identity, "%s", identity);
  snprintf(m->clip_key, sizeof m->clip_key, "%s", key), snprintf(m->clip_identity, sizeof m->clip_identity, "%s", identity);
  TfpMenuValue now, want = {TFP_MENU_VALUE_NUMBER, false, hip};
  if (m->host.get(m->host.user, "weapon_near_hip_cm", &now) && now.number != hip) m->host.set(m->host.user, "weapon_near_hip_cm", &want);
  want.number = aim;
  if (m->host.get(m->host.user, "weapon_near_aim_cm", &now) && now.number != aim) m->host.set(m->host.user, "weapon_near_aim_cm", &want);
}
