/* See wire_menu.h. Retail: src/first_person/menu.lua (0.5.41-smooth.66); line numbers are that file's. */
#include "wire_menu.h"
#include "wire_off.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TFP_MK_STATUS: retail's two 'text' rows whose label changes. DBF-MCM for HellForge can relabel only a control it can
 * find by id, and a text block is not one, so there they are buttons that are always disabled. */
typedef TfpMenuControl Control;
const char *const tfp_wire_menu_presets[3] = {"Custom (keep existing sliders)", "Comfort", "Natural"};
static const char *const STYLES[2] = {"Stable", "Bodycam"};
const char *const tfp_wire_menu_aim_fields[3] = {"The game's zoom", "Your locked view", "A chosen value"};
/* Every entry names its store, so one without a store says -1 and never 0 (a real id). */
#define ROW(id_, kind_, label_, page_, ...) {.id = id_, .kind = kind_, .label = label_, .page = page_, __VA_ARGS__}
#define NONE .menu = -1, .locked = -1, .config = -1
#define M(name) .menu = TFP_MENU_##name, .locked = -1, .config = -1
#define L(name) .menu = -1, .locked = TFP_LOCKED_##name, .config = -1
#define CFG(name) .menu = -1, .locked = -1, .config = TFP_SET_##name
#define RANGE(lo, hi, st) .min = lo, .max = hi, .step = st
#define TIP(text) .description = text
/* Pages 0 and 1: retail's menu.lua:343-418 in retail's order. Not here: 'start_free_aim' and 'start_native_test'
 * (30-second test tools of modes this build does not wire) and the commented-out ads_mode control (tfp_wire_menu_not_offered).
 * Page 2: the locked view's options (TFP_SETTINGS_LOCKED), with the ranges of the store. */
static const Control CONTROLS[] = {
  ROW("start_true_first_person", TFP_MK_BUTTON, "Enable True First Person", 0, NONE),
  ROW("stop_test", TFP_MK_BUTTON, "Turn first-person view off", 0, NONE),
  ROW("show_aim_markers", TFP_MK_CHECK, "Show aim markers", 0, CFG(show_aim_markers),
      TIP("Show the crosshair and aiming circle while aiming in first person. Both remain visible when not aiming. Saved weapon preferences apply "
          "until you change this option. Hit markers and scope markings keep their normal behavior.")),
  ROW("edit_placement", TFP_MK_BUTTON, "Edit arms and weapon placement", 0, NONE),
  ROW("editor_key", TFP_MK_KEY, "Open / close placement editor", 0, M(editor_key),
      TIP("Default Page Up (F8 is HellForge's own editor). Closing the editor restores unsaved placement and leaves first person on.")),
  ROW("toggle_key", TFP_MK_KEY, "Toggle True First Person", 0, M(toggle_key), TIP("Default F11. Toggle True First Person; it stays enabled when changing weapons.")),
  ROW("true_native_ads", TFP_MK_CHECK, "True First Person: native ADS", 0, M(true_native_ads),
      TIP("Use the game's native sights and zoom while aiming in True First Person.")),
  ROW("show_torso", TFP_MK_CHECK, "True First Person: show torso", 0, M(show_torso),
      TIP("Keep your torso armour visible in the moments without the first-person arms: holding a stratagem, turning a valve, swapping weapons. "
          "While the first-person arms are up the torso is always hidden. The head and shoulders stay hidden.")),
  /* retail's label is "Fird Person Arms", a typo; the stored key keeps retail's name */
  ROW("fird_person_arms", TFP_MK_CHECK, "First Person Arms", 0, M(fird_person_arms),
      TIP("Dress the first-person arms like your body: sleeves and shoulders follow your torso, and the left hand holds the weapon grip the way it "
          "does in third person. Off leaves the game's own first-person arms and hand animation untouched."),
      .note = "Sleeves and hand grips are refused on this build."),
  ROW("family_placement_defaults", TFP_MK_CHECK, "Use weapon-family placement defaults", 0, CFG(family_placement_defaults),
      TIP("Opt in to conservative defaults for catalogue-identified firearms. Saved weapon/action placements and your customised global placement "
          "take precedence. Unknown weapons retain the existing baseline.")),
  ROW("motion_preset", TFP_MK_CHOICE, "Motion preset", 0, M(motion_preset), .choices = tfp_wire_menu_presets, .choice_count = 3,
      TIP("Comfort adds restrained weapon movement. Natural adds more weapon motion. Custom restores your existing sliders; selecting a preset never "
          "overwrites them. The game controls camera movement.")),
  ROW("motion_enabled", TFP_MK_TOGGLE, "Weapon movement", 0, M(motion_enabled), TIP("Walking bob, turn inertia and a lowered sprint position. ADS remains steady.")),
  ROW("motion_strength", TFP_MK_SLIDER, "Weapon movement strength", 0, M(motion_strength), RANGE(0, 200, 5)),
  ROW("motion_bob_enabled", TFP_MK_CHECK, "Weapon movement: walking bob", 0, M(motion_bob_enabled), TIP("The weapon bobbing with your steps.")),
  ROW("motion_sway_enabled", TFP_MK_CHECK, "Weapon movement: sway", 0, M(motion_sway_enabled), TIP("The weapon lagging behind when you turn or strafe.")),
  ROW("motion_sprint_enabled", TFP_MK_CHECK, "Weapon movement: sprint pose", 0, M(motion_sprint_enabled), TIP("The weapon lowering and turning while you sprint.")),
  ROW("near_clip_cm", TFP_MK_SLIDER, "Near clip distance (cm)", 0, M(near_clip_cm), RANGE(5, 40, 1),
      TIP("Hides anything closer to the first-person camera than this, such as armor edges. 5 is the game's own distance (off). Full ADS always "
          "uses the game's distance.")),
  ROW("near_clip_aim", TFP_MK_TOGGLE, "Near clip while aiming", 0, M(near_clip_aim),
      TIP("Keep the near clip during normal aim. Off returns to the game's distance while aiming so sights are not cut.")),
  ROW("weapon_composition_status", TFP_MK_STATUS, "Weapon: no active firearm", 0, NONE),
  ROW("weapon_near_hip_cm", TFP_MK_SLIDER, "This weapon: hip clearance (cm)", 0, M(weapon_near_hip_cm), RANGE(5, 40, 1),
      TIP("Draft for this firearm only. Save below to apply. 5 keeps the game distance.")),
  ROW("weapon_near_aim_cm", TFP_MK_SLIDER, "This weapon: normal-aim clearance (cm)", 0, M(weapon_near_aim_cm), RANGE(5, 40, 1),
      TIP("Independent normal-aim clearance. Full native ADS always keeps the game distance.")),
  ROW("save_weapon_near", TFP_MK_BUTTON, "Save clipping for this weapon", 0, NONE),
  ROW("reset_weapon_near", TFP_MK_BUTTON, "Use global clipping for this weapon", 0, NONE),
  ROW("runtime_status", TFP_MK_STATUS, "Status: First person off", 0, NONE),
  /* :21-27, :334 (step 5) */
  ROW("weapon_bob_strength", TFP_MK_SLIDER, "Weapon bob strength", 1, M(weapon_bob_strength), RANGE(0, 300, 5)),
  ROW("weapon_bob_speed", TFP_MK_SLIDER, "Weapon bob speed", 1, M(weapon_bob_speed), RANGE(25, 200, 5)),
  ROW("weapon_sway_strength", TFP_MK_SLIDER, "Weapon sway strength", 1, M(weapon_sway_strength), RANGE(0, 300, 5)),
  ROW("weapon_sway_speed", TFP_MK_SLIDER, "Weapon sway response speed", 1, M(weapon_sway_speed), RANGE(25, 200, 5)),
  ROW("sprint_motion_strength", TFP_MK_SLIDER, "Sprint weapon movement", 1, M(sprint_motion_strength), RANGE(0, 200, 5)),
  /* the locked view (docs/design/specs/2026-11-22-locked-view-design.md), in the store's order */
  ROW("camera_lock", TFP_MK_TOGGLE, "Camera lock", 2, L(camera_lock), TIP("Keep the view first person through every action and camera of your own character.")),
  ROW("fov_lock", TFP_MK_TOGGLE, "Field of view lock", 2, L(fov_lock), TIP("Hold one field of view whatever the weapon or the camera asks for.")),
  ROW("fov_degrees", TFP_MK_SLIDER, "Field of view (degrees)", 2, L(fov_degrees), RANGE(50, 120, 1), TIP("The field of view the lock holds. Default 80.")),
  /* round 20: "Aim field of view" took the place of the toggle "Zoom when aiming" (still stored: NOT_OFFERED) */
  ROW("fov_aim", TFP_MK_CHOICE, "Aim field of view", 2, L(fov_aim), .choices = tfp_wire_menu_aim_fields, .choice_count = 3,
      TIP("What you see through the sights in full aim. The game's zoom: each weapon's own zoom and zoom levels, as the game shows them. "
          "Your locked view: the field of view above, so a scope looks as small as it does at the hip. A chosen value: the field of view "
          "below, for every weapon and zoom level, with or without the lock.")),
  ROW("fov_aim_degrees", TFP_MK_SLIDER, "Aim field of view (degrees)", 2, L(fov_aim_degrees), RANGE(20, 90, 1), .aim_chosen_only = true,
      TIP("The field of view in full aim when Aim field of view is A chosen value. Smaller is more zoom. Default 50."),
      .note = "Used when Aim field of view is A chosen value."),
  ROW("ads_sight_cm", TFP_MK_SLIDER, "Sight distance in aim (cm)", 2, L(ads_sight_cm), RANGE(0, 30, 1),
      TIP("How close full aim brings the sight to your eye. The weapon is moved toward you until its sight is this far away; a sight that "
          "is already closer is left where it is. 0 keeps the game's own distance (the default). Fine-tune one weapon with the placement "
          "editor's ADS action (aim, then Page Up).")),
  /* the camera style (src/camera_damp.h): Boolean `bodycam` in the file, a choice of two here */
  ROW("camera_style", TFP_MK_CHOICE, "Camera style", 2, L(bodycam), .choices = STYLES, .choice_count = 2,
      TIP("Stable: a steady, traditional first-person camera. Bodycam: the view moves with your head, like a body camera: it takes kicks from your "
          "body's movement and follows the head through actions."),
      .note = "View kick strength, View kick limit and Camera damping apply to Bodycam.", .note_when = 1),
  ROW("kick_strength", TFP_MK_SLIDER, "View kick strength (%)", 2, L(kick_strength), RANGE(0, 100, 5), .bodycam_only = true,
      TIP("How much of your body's movement reaches the view while on foot. Default 35.")),
  ROW("kick_max_degrees", TFP_MK_SLIDER, "View kick limit (degrees)", 2, L(kick_max_degrees), RANGE(0, 20, 1), .bodycam_only = true,
      TIP("The most the view is turned by a kick while on foot. Default 8.")),
  ROW("thrown_kick_strength", TFP_MK_SLIDER, "View kick when thrown (%)", 2, L(thrown_kick_strength), RANGE(0, 100, 5),
      TIP("How much of your body's movement reaches the view while you are thrown or ragdolled. The Stable camera uses half of it, as a slow sway. "
          "Default 70.")),
  ROW("camera_damping", TFP_MK_SLIDER, "Camera damping (%)", 2, L(camera_damping), RANGE(0, 100, 5), .bodycam_only = true,
      TIP("How much the view is steadied against your body's movement. 0 is the raw movement; higher is calmer and slower. Default 60.")),
  ROW("animated_arms", TFP_MK_TOGGLE, "Animated arms", 2, L(animated_arms), TIP("The first-person arms follow your character's own animation.")),
  /* stored, and nothing acts on it (src/wire_settings.h): always disabled, with the note */
  ROW("one_handed_pistols", TFP_MK_TOGGLE, "One-handed pistols", 2, L(one_handed_pistols),
      TIP("One-handed weapons are held as your character holds them. This comes from the character's own animation while Animated arms is on; "
          "there is nothing to set here."),
      .note = "Comes from your character's own animation while Animated arms is on.", .inert = true),
  ROW("put_away", TFP_MK_TOGGLE, "Weapon put-away", 2, L(put_away), TIP("Keep first person while the weapon is put away for a swap, grenade or stim.")),
  ROW("smooth_eye", TFP_MK_TOGGLE, "Smooth eye", 2, L(smooth_eye), TIP("No camera pops when the game changes its camera.")),
};
#undef ROW
#undef NONE
#undef M
#undef L
#undef CFG
#undef RANGE
#undef TIP
enum { CONTROL_COUNT = sizeof CONTROLS / sizeof *CONTROLS };
const TfpMenuControl *tfp_wire_menu_controls(int *count) { if (count) *count = CONTROL_COUNT; return CONTROLS; }
const char *const tfp_wire_menu_pages[TFP_MENU_PAGES] = {"First person", "Bob and sway", "Locked view"};
const char *const tfp_wire_menu_page_ids[TFP_MENU_PAGES] = {"test", "movement", "locked_view"};   /* retail's two, then this build's */
const char *const tfp_wire_menu_page_intro[TFP_MENU_PAGES] = {
  NULL, NULL, "First person through every action, one field of view, and how much your body's movement moves the view."};

/* The keys of both files that are no control (docs/guides/POSER-AND-MENU.md "Every option"). */
static const TfpMenuNotOffered NOT_OFFERED[] = {
  {"settings.json first_person", "placement", "the global weapon placement; written by the placement editor"},
  {"settings.json first_person", "weapon_placements", "older per-weapon placements; read as a fallback, written by no control (the editor writes action_placements)"},
  {"settings.json first_person", "action_placements", "per-weapon, per-action placements; written by the placement editor"},
  {"settings.json first_person", "action_state_mapping", "which game state is which action (idle, aim, sprint, ...); data the runtime reads, not a choice"},
  {"settings.json first_person", "fird_head_camera", "the eye-camera placement; its editor (retail's Shift+F8) is not in this build"},
  {"settings.json first_person", "motion_enabled", "retail's runtime default for Weapon movement; the control's own value always wins, so it changes nothing"},
  {"settings.json first_person", "motion_scale", "retail's fallback for Weapon movement strength, used only while the stored strength is outside 0 to 200"},
  {"settings.json first_person", "camera_kick_enabled", "retail's own camera kick switch; no unit of this build reads it (the locked view's kick sliders replace it)"},
  {"settings.json first_person", "aim_markers", "per-weapon aim-marker choices, saved by the runtime; Show aim markers is the control"},
  {"settings.json first_person", "near_clip_profiles", "per-weapon near clip, written by Save clipping for this weapon / Use global clipping for this weapon"},
  {"settings.json first_person", "grab_offset", "kept from older files; no unit of retail or of this build reads it"},
  {"settings.json first_person", "grab_offsets", "kept from older files; no unit of retail or of this build reads it"},
  {"locked_view.json", "fov_zoom_when_aiming", "the toggle Aim field of view replaced (round 20); kept in step with it so an older build reads the nearest answer"},
  {"settings.json menu", "ads_mode", "retail's control is commented out (Fird Person ADS behaviour); only its free-aim test tool, not in this build, reads it"},
};
const TfpMenuNotOffered *tfp_wire_menu_not_offered(int *count) { if (count) *count = (int)(sizeof NOT_OFFERED / sizeof *NOT_OFFERED); return NOT_OFFERED; }

typedef struct Menu {
  TfpMenuHost host;
  TfpMenuEditFn edit;
  unsigned unavailable;
  bool syncing;                         /* a set of ours is in flight: its `changed` is not a user's change */
  char status[300], weapon[200];        /* the two labels as last sent */
  char clip_key[TFP_SETTINGS_ID]; char clip_identity[24]; bool has_clip_target; /* :204-229 clip_target */
  bool drawn_weapon; char drawn_key[TFP_SETTINGS_ID], drawn_identity[24]; bool drawn_profile; double drawn_hip, drawn_aim;
  char message[200];                    /* a button's answer */
  int bodycam_shown, aim_shown;                    /* the camera style the Bodycam-only sliders were last enabled for; -1: not yet */
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
  if (c->locked >= 0) return tfp_settings_locked_flag(s, (TfpSettingsLockedId)c->locked);
  if (!strcmp(c->id, "family_placement_defaults")) return tfp_settings_flag(s, TFP_SET_family_placement_defaults) == 1;
  if (!strcmp(c->id, "show_aim_markers")) { /* :48-50 marker_setting() */
    TfpRtValue r[30]; char why[8];
    return action(w, TFP_RT_E_aim_markers_setting, NULL, r, why, sizeof why) && r[0].kind == TFP_RT_TRUE;
  }
  return false;
}
bool tfp_wire_menu_default(TfpWire *w, const TfpMenuControl *c, int editor_key, TfpMenuValue *out) {
  TfpSettings *s = tfp_wire_settings(w);
  if (!s || !c || !out) return false;
  double value = c->menu >= 0 ? tfp_settings_menu_number(s, (TfpSettingsMenuId)c->menu) : 0;
  if (c->locked >= 0) {
    const TfpSettingsLockedInfo *info = tfp_settings_locked_info((TfpSettingsLockedId)c->locked);
    value = info->is_bool ? 1 + tfp_settings_locked_flag(s, (TfpSettingsLockedId)c->locked) : tfp_settings_locked_number(s, (TfpSettingsLockedId)c->locked);
  }
  switch (c->kind) {
    case TFP_MK_TOGGLE: case TFP_MK_CHECK: *out = (TfpMenuValue){TFP_MENU_VALUE_BOOL, current_bool(w, c), 0}; return true;
    case TFP_MK_SLIDER:
      if (!(value >= c->min && value <= c->max)) value = value < c->min ? c->min : c->max; /* MCM refuses a default outside the range */
      break;
    case TFP_MK_CHOICE: value = value >= 1 && value <= c->choice_count ? (int)value : 1; break;
    case TFP_MK_KEY:
      value = value >= 0 && value <= 257 ? (int)value : 0;
      if (c->menu == TFP_MENU_editor_key && value == 119) value = editor_key;
      break;
    default: return false;
  }
  *out = (TfpMenuValue){TFP_MENU_VALUE_NUMBER, false, value};
  return true;
}
size_t tfp_wire_menu_definition(TfpWire *w, int editor_key, char *out, size_t size) {
  TfpSettings *s = tfp_wire_settings(w);
  char number[96];
  size_t at = 0;
  if (!s) return 0;
  /* "storage": this mod keeps the values (wire_menu.h); MCM's load answers none, its save is the store's */
  at = put(out, size, at, "{\"id\":\"" TFP_MENU_MOD_ID "\",\"name\":\"True First Person\",\"storage\":true,\"description\":");
  at = put_text(out, size, at, "Use the game's first-person arms and weapon view. F11 toggles True First Person; F10 opens this page.");
  at = put(out, size, at, ",\"pages\":[");
  for (int page = 0; page < TFP_MENU_PAGES; page++) {
    at = put(out, size, at, page ? ",{\"id\":\"" : "{\"id\":\""), at = put(out, size, at, tfp_wire_menu_page_ids[page]);
    at = put(out, size, at, "\",\"name\":"), at = put_text(out, size, at, tfp_wire_menu_pages[page]);
    at = put(out, size, at, ",\"require_confirmation\":false,\"controls\":[");
    bool first = true;
    if (tfp_wire_menu_page_intro[page])
      at = put(out, size, at, "{\"type\":\"text\",\"label\":"), at = put_text(out, size, at, tfp_wire_menu_page_intro[page]), at = put(out, size, at, "}"), first = false;
    for (int i = 0; i < CONTROL_COUNT; i++) {
      const Control *c = &CONTROLS[i];
      static const char *const types[] = {"button", "toggle", "toggle", "slider", "choice", "keybind", "button"};
      TfpMenuValue value = {TFP_MENU_VALUE_NONE, false, 0};
      if (c->page != page) continue;
      tfp_wire_menu_default(w, c, editor_key, &value);
      at = put(out, size, at, first ? "{\"id\":\"" : ",{\"id\":\""), first = false;
      at = put(out, size, at, c->id), at = put(out, size, at, "\",\"type\":\""), at = put(out, size, at, types[c->kind]);
      at = put(out, size, at, "\",\"label\":"), at = put_text(out, size, at, c->label);
      if (c->kind == TFP_MK_CHECK) at = put(out, size, at, ",\"presentation\":\"checkbox\"");
      if (c->kind == TFP_MK_BUTTON || c->kind == TFP_MK_STATUS || !strcmp(c->id, "show_aim_markers")) at = put(out, size, at, ",\"require_confirmation\":false");
      if (c->kind == TFP_MK_TOGGLE || c->kind == TFP_MK_CHECK) at = put(out, size, at, value.boolean ? ",\"default\":true" : ",\"default\":false");
      if (c->kind == TFP_MK_SLIDER) {
        snprintf(number, sizeof number, ",\"min\":%g,\"max\":%g,\"step\":%g", c->min, c->max, c->step), at = put(out, size, at, number);
        snprintf(number, sizeof number, ",\"default\":%.17g", value.number), at = put(out, size, at, number);
      }
      if (c->kind == TFP_MK_CHOICE) {   /* MCM's one-click < choice >, not its two-click drop-down; Left/Right step it */
        at = put(out, size, at, ",\"presentation\":\"selector\",\"choices\":[");
        for (int choice = 0; choice < c->choice_count; choice++) at = put(out, size, at, choice ? "," : ""), at = put_text(out, size, at, c->choices[choice]);
        snprintf(number, sizeof number, "],\"default\":%d", (int)value.number), at = put(out, size, at, number);
      }
      if (c->kind == TFP_MK_KEY) snprintf(number, sizeof number, ",\"default\":%d", (int)value.number), at = put(out, size, at, number);
      const char *note = c->inert || c->note_when ? c->note : NULL;  /* First Person Arms' note holds only when it is refused: not here */
      if (c->description || note) { /* MCM draws no line under a row: the note ends the description */
        char text[640];
        snprintf(text, sizeof text, "%s%s%s", c->description ? c->description : "", c->description && note ? " " : "", note ? note : "");
        at = put(out, size, at, ",\"description\":"), at = put_text(out, size, at, text);
      }
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
  bool is_bool = c->kind == TFP_MK_TOGGLE || c->kind == TFP_MK_CHECK;
  if (v->kind != (is_bool ? TFP_MENU_VALUE_BOOL : TFP_MENU_VALUE_NUMBER)) return;
  if (c->menu >= 0) tfp_wire_settings_menu_set(w, (TfpSettingsMenuId)c->menu, is_bool, v->boolean, v->number);
  else if (c->locked >= 0 && c->kind == TFP_MK_CHOICE && tfp_settings_locked_info((TfpSettingsLockedId)c->locked)->is_bool)
    tfp_wire_settings_locked_set(w, (TfpSettingsLockedId)c->locked, true, v->number == 2, 0); /* a choice of two over a Boolean option: 1 off, 2 on */
  else if (c->locked >= 0 && c->kind == TFP_MK_CHOICE) /* a choice over a number: the choice's own number, 1 .. choice_count */
    tfp_wire_settings_locked_set(w, (TfpSettingsLockedId)c->locked, false, false, v->number);
  else if (c->locked >= 0) tfp_wire_settings_locked_set(w, (TfpSettingsLockedId)c->locked, is_bool, v->boolean, v->number);
}
static bool stored(const Control *c) { return c->menu >= 0 || c->locked >= 0; }
/* The chosen aim field of view's slider: enabled while Aim field of view is A chosen value; sent on a change only. */
static void sync_aim(TfpWire *w, Menu *m) {
  const int chosen = tfp_locked_view_fov_aim(w) == 3;
  if (chosen == m->aim_shown) return;
  m->aim_shown = chosen;
  for (int i = 0; i < CONTROL_COUNT; i++) if (CONTROLS[i].aim_chosen_only) m->host.set_disabled(m->host.user, CONTROLS[i].id, !chosen);
}
/* The Bodycam-only sliders: enabled for the Bodycam camera, disabled for the Stable one; sent on a change only. */
static void sync_bodycam(TfpWire *w, Menu *m) {
  const int bodycam = tfp_locked_view_bodycam(w);
  if (bodycam == m->bodycam_shown) return;
  m->bodycam_shown = bodycam;
  for (int i = 0; i < CONTROL_COUNT; i++) if (CONTROLS[i].bodycam_only) m->host.set_disabled(m->host.user, CONTROLS[i].id, !bodycam);
}
bool tfp_wire_menu_bind(TfpWire *w, const TfpMenuHost *host, unsigned unavailable, TfpMenuEditFn edit) {
  if (!tfp_wire_settings(w) || !host || !host->get || !host->set || !host->set_disabled || !host->set_label) return false;
  Menu *m = state(w);
  if (!m && !(m = calloc(1, sizeof *m))) return false;
  memset(m, 0, sizeof *m);
  m->host = *host, m->edit = edit, m->unavailable = unavailable;
  w->ext[TFP_WIRE_EXT_MENU] = m;
  /* The host's values into the store. A value that is the store's own, as this host was given it (the registration's
   * default: a slider inside its range, the editor key's retail 119 as the host's key), is the bind's doing and not a
   * change to save: on its own it must never write a file, least of all one that was refused or not opened at the
   * start (src/wire_settings.h). Those go first, between the mirror marks; a value the host holds for itself (MCM's
   * own file, set by the user there) is a change like any other and comes after. */
  bool own[CONTROL_COUNT];
  for (int i = 0; i < CONTROL_COUNT; i++) {
    TfpMenuValue v, registered;
    own[i] = stored(&CONTROLS[i]) && host->get(host->user, CONTROLS[i].id, &v) &&
             tfp_wire_menu_default(w, &CONTROLS[i], host->editor_key ? host->editor_key : 119, &registered) && registered.kind == v.kind &&
             (v.kind == TFP_MENU_VALUE_BOOL ? registered.boolean == v.boolean : registered.number == v.number);
  }
  const uint64_t mark = tfp_wire_settings_mirror_begin(w);
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < CONTROL_COUNT; i++) {
      TfpMenuValue v;
      if (own[i] == !pass && stored(&CONTROLS[i]) && host->get(host->user, CONTROLS[i].id, &v)) into_store(w, &CONTROLS[i], &v);
    }
    if (!pass) tfp_wire_settings_mirror_end(w, mark);
  }
  host->set_disabled(host->user, "weapon_composition_status", true), host->set_disabled(host->user, "runtime_status", true);
  for (int i = 0; i < CONTROL_COUNT; i++) if (CONTROLS[i].inert) host->set_disabled(host->user, CONTROLS[i].id, true);
  m->bodycam_shown = m->aim_shown = -1;
  sync_bodycam(w, m);
  sync_aim(w, m);
  if (unavailable & TFP_MENU_NO_ARMS) {
    host->set_disabled(host->user, "fird_person_arms", true);
    tfp_wire_say(w, "Menu: \"First Person Arms\" is shown disabled: sleeves and hand grips refused on this build (see their Refused lines)");
  }
  if ((unavailable & TFP_MENU_NO_EDITOR) || !edit) {
    host->set_disabled(host->user, "edit_placement", true), host->set_disabled(host->user, "editor_key", true);
    tfp_wire_say(w, "Menu: the placement editor's button and key are shown disabled: the editor is not available in this session");
  }
  say(w, "Ready: menu: registered with %s as \"True First Person\" (three pages, every option: First person, Bob and sway, Locked view; "
         "not offered: free aim and the native ADS comparison, which are retail's 30-second test tools)%s", host->name ? host->name : "DBF-MCM", "");
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
bool tfp_wire_menu_key_edge(TfpKeyEdge *edge, int key, bool down) {
  if (key != edge->key) edge->key = key, edge->held = true;   /* just bound: counted as held until it is seen up */
  bool pressed = down && !edge->held;
  edge->held = down;
  return pressed;
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
  if (stored(c)) { into_store(w, c, value); return; } /* the store only: safe at once */
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
  if (!m || !c || c->kind != TFP_MK_BUTTON) return "Unknown button";
  enqueue(w, m, id, NULL, NULL);
  return NULL;
}
static void run_queue(TfpWire *w, Menu *m) {
  unsigned count = m->queue_count;
  m->queue_count = 0;
  for (unsigned i = 0; i < count; i++) {
    const Control *c = control(m->queued[i].id);
    if (c->kind != TFP_MK_BUTTON) { run_toggle(w, m, c->id, &m->queued[i].value, &m->queued[i].previous); continue; }
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
  sync_bodycam(w, m);
  sync_aim(w, m);
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
  const uint64_t mark = tfp_wire_settings_mirror_begin(w); /* the drafts following the weapon are not a change to save */
  if (m->host.get(m->host.user, "weapon_near_hip_cm", &now) && now.number != hip) m->host.set(m->host.user, "weapon_near_hip_cm", &want);
  want.number = aim;
  if (m->host.get(m->host.user, "weapon_near_aim_cm", &now) && now.number != aim) m->host.set(m->host.user, "weapon_near_aim_cm", &want);
  tfp_wire_settings_mirror_end(w, mark);
}
