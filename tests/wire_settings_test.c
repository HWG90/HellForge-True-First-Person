/* The settings store and its binder (src/tfp_settings.c, src/wire_settings.c).
 *
 *   wire_settings_test                  the tests: the JSON reader's refusals, the store, and retail's own answers
 *                                       (tests/vectors/settings_vectors.h, from the unmodified retail Lua) replayed
 *                                       through the runtime host's pointers on the converted settings
 *   wire_settings_test --dump <file>    the store loaded from that settings.json, one `path=value` line per leaf
 *   wire_settings_test --write <file>   the store loaded from it and written back as JSON
 *
 * No Windows, no game: every memory read fails. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire.h"
#include "wire_off.h"
#include "wire_settings.h"
#include "wire_menu.h"
#ifdef TFP_HAVE_MCM_SCHEMA
#include "schema.h"
#endif
#include "vectors/settings_vectors.h"

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { printf("wire settings: FAILED %s (line %d)\n", #condition, __LINE__); return 1; } } while (0)
#define COUNT(a) (sizeof(a) / sizeof *(a))
typedef TfpRtValue V;

static char lines[64][700];
static int line_count;
static void capture(void *user, const char *text) {
  (void)user;
  if (line_count < 64) snprintf(lines[line_count++], sizeof lines[0], "%s", text);
}
static const char *logged(const char *part) {
  for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) return lines[i];
  return NULL;
}

/* ---- a host with no game behind it (as tests/wire_glue_test.c) ---- */
static double now_seconds = 100;
static uint64_t no_module(void *u, TfpOpsModule which) { (void)u; (void)which; return 0; }
static bool no_read(void *u, uint64_t at, void *out, size_t size) { (void)u; (void)at; (void)out; (void)size; return false; }
static bool no_write(void *u, uint64_t at, const void *bytes, size_t size) { (void)u; (void)at; (void)bytes; (void)size; return false; }
static void no_query(void *u, uint64_t at, TfpOpsRegion *out) { (void)u; (void)at; memset(out, 0, sizeof *out); }
static bool no_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  (void)u; (void)which; (void)address; (void)args; (void)result;
  raised->present = true;
  snprintf(raised->text, sizeof raised->text, "no game");
  return false;
}
static double test_clock(void *u) { (void)u; return now_seconds; }
static int test_mouse(void *u, int button) { (void)u; (void)button; return 0; }
static uint32_t h_validate_c(const TfpCameraConfig *c) { (void)c; return 0; }
static uint32_t h_begin_c(const TfpCameraConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_end(uint64_t g) { (void)g; return 1; }
static uint32_t h_stats_c(uint64_t g, TfpCameraStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_error(void) { return 0; }
static uint32_t h_generation(uint64_t g) { (void)g; return 0; }
static uint32_t h_handoff(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; return 0; }
static uint32_t h_u32(uint64_t g, uint32_t v) { (void)g; (void)v; return 0; }
static uint32_t h_validate_f(const TfpFovConfig *c) { (void)c; return 0; }
static uint32_t h_begin_f(const TfpFovConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_bind_f(uint64_t g, uint64_t r, uint32_t w) { (void)g; (void)r; (void)w; return 0; }
static uint32_t h_transition_f(uint64_t g, uint64_t e, float p, uint32_t a) { (void)g; (void)e; (void)p; (void)a; return 0; }
static uint32_t h_stats_f(uint64_t g, TfpFovStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_begin_n(const TfpNearConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_set_n(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; return 0; }
static uint32_t h_stats_n(uint64_t g, TfpNearStats *s) { (void)g; (void)s; return 0; }
static uint32_t h_validate_b(const TfpBlurConfig *c) { (void)c; return 0; }
static uint32_t h_begin_b(const TfpBlurConfig *c, uint64_t *g) { (void)c; (void)g; return 0; }
static uint32_t h_stats_b(uint64_t g, TfpBlurStats *s) { (void)g; (void)s; return 0; }
static TfpWire wire;
static void new_wire(void) {
  static const TfpCameraHelperApi camera = {h_validate_c, h_begin_c, h_end, h_stats_c, h_error, h_generation, h_handoff, h_u32, h_u32};
  static const TfpFovHelperApi fov = {h_validate_f, h_begin_f, h_bind_f, h_transition_f, h_end, h_stats_f, h_error};
  static const TfpNearHelperApi near_clip = {h_begin_n, h_set_n, h_end, h_stats_n, h_error};
  static const TfpBlurHelperApi blur = {h_validate_b, h_begin_b, h_end, h_stats_b, h_error};
  TfpOpsHost memory;
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = no_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  TfpWirePlatform platform = {NULL, test_clock, test_mouse, capture};
  TfpWireHelpers helpers = {&camera, &fov, &near_clip, &blur};
  line_count = 0;
  tfp_wire_init(&wire, &memory, &platform, &helpers);
}

static unsigned saves;
static bool save_fails;
static char saved_text[TFP_SETTINGS_MAX];
static size_t saved_length;
static TfpSettingsFile saved_file;
static bool test_save(void *user, TfpSettingsFile file, const char *text, size_t length, char *why, size_t why_size) {
  (void)user;
  saves++, saved_file = file;
  if (save_fails) { snprintf(why, why_size, "disk full"); return false; }
  memcpy(saved_text, text, length); saved_text[length] = 0; saved_length = length;
  return true;
}
static TfpSettingsCopy test_copy(void *user, TfpSettingsFile file, const char *suffix, char *why, size_t why_size) { /* an empty folder */
  (void)user; (void)file; (void)suffix; (void)why; (void)why_size;
  return TFP_COPY_NONE;
}
static const TfpSettingsDisk test_disk = {NULL, test_save, test_copy, NULL};
/* ---- a stand-in for MCM: the saved values, and what the binder asked of it ---- */
static struct { const char *id; TfpMenuValue v; } mcm_values[64];
static unsigned mcm_count, mcm_sets, mcm_labels, mcm_disables;
static char mcm_last_label[2][300], mcm_disabled[1200];
static TfpMenuValue *mcm_slot(const char *id) {
  for (unsigned i = 0; i < mcm_count; i++) if (!strcmp(mcm_values[i].id, id)) return &mcm_values[i].v;
  return NULL;
}
static void mcm_put(const char *id, int kind, bool b, double n) {
  TfpMenuValue *v = mcm_slot(id);
  if (!v) { mcm_values[mcm_count].id = id; v = &mcm_values[mcm_count++].v; }
  *v = (TfpMenuValue){kind, b, n};
}
static bool mcm_get(void *u, const char *id, TfpMenuValue *out) { (void)u; TfpMenuValue *v = mcm_slot(id); if (v) *out = *v; return v != NULL; }
static bool mcm_set(void *u, const char *id, const TfpMenuValue *value) {
  (void)u; mcm_sets++;
  TfpMenuValue before = {0, false, 0}, *v = mcm_slot(id);
  if (v) before = *v;
  mcm_put(id, value->kind, value->boolean, value->number);
  tfp_wire_menu_changed(&wire, id, value, &before); /* MCM calls `changed` back after a set */
  return true;
}
static struct { char id[40]; bool disabled; } mcm_state[64]; /* each control's disabled state as last set */
static unsigned mcm_state_count;
static bool mcm_is_disabled(const char *id) {
  for (unsigned i = 0; i < mcm_state_count; i++) if (!strcmp(mcm_state[i].id, id)) return mcm_state[i].disabled;
  return false;
}
static bool mcm_set_disabled(void *u, const char *id, bool disabled) {
  (void)u; mcm_disables++;
  if (disabled && strlen(mcm_disabled) + strlen(id) + 2 < sizeof mcm_disabled) { strcat(mcm_disabled, id); strcat(mcm_disabled, " "); }
  unsigned i = 0;
  while (i < mcm_state_count && strcmp(mcm_state[i].id, id)) i++;
  if (i == mcm_state_count && mcm_state_count < COUNT(mcm_state)) snprintf(mcm_state[mcm_state_count++].id, sizeof mcm_state[0].id, "%s", id);
  if (i < COUNT(mcm_state)) mcm_state[i].disabled = disabled;
  return true;
}
static bool mcm_set_label(void *u, const char *id, const char *label) {
  (void)u; mcm_labels++;
  snprintf(mcm_last_label[!strcmp(id, "runtime_status") ? 0 : 1], 300, "%s", label);
  return true;
}
static const char *test_edit(TfpWire *w) { (void)w; return "editor asked"; }
static V text(const char *t) { return tfp_wire_text(t); }
static bool text_is(V v, const char *t) { return v.kind == TFP_RT_TEXT && t && v.length == strlen(t) && !memcmp(v.text, t, v.length); }
static bool same_placement(const TfpActionPlacement *p, unsigned mask, const double v[6]) {
  const double got[6] = {p->x, p->y, p->z, p->yaw, p->pitch, p->roll};
  if (p->present != mask) return false;
  for (int i = 0; i < 6; i++) if ((mask >> i & 1) && got[i] != v[i]) return false; /* an absent field's storage is not part of the answer */
  return true;
}

/* ---- the store as flat lines (the Python side compares them with the import, key by key) ---- */
static void dump_placement(const char *path, const TfpActionPlacement *p) {
  static const char *const names[6] = {"x", "y", "z", "yaw", "pitch", "roll"};
  const double v[6] = {p->x, p->y, p->z, p->yaw, p->pitch, p->roll};
  if (!p->present) printf("%s={}\n", path);
  for (int i = 0; i < 6; i++) if (p->present >> i & 1) printf("%s.%s=%.17g\n", path, names[i], v[i]);
}
static void dump(const TfpSettings *s) {
  char path[200];
  for (int f = 0; f < TFP_SET_COUNT; f++) {
    const TfpSettingsValue *v = s->field[f];
    const char *name = tfp_settings_field_name((TfpSettingsField)f);
    if (!v) continue;
    snprintf(path, sizeof path, "first_person.%s", name);
    if (v->kind == TFP_SETTINGS_BOOL) printf("%s=%s\n", path, v->boolean ? "true" : "false");
    else if (v->kind == TFP_SETTINGS_NUMBER) printf("%s=%.17g\n", path, v->number);
    else if (v->kind == TFP_SETTINGS_PLACEMENT) dump_placement(path, &v->placement);
    else if (!v->count) printf("%s={}\n", path);
    for (size_t i = 0; i < v->count; i++) {
      if (v->kind == TFP_SETTINGS_MAPPING) { printf("%s.%s.%s=%s\n", path, v->rows[i].resource, v->rows[i].states, v->rows[i].action); continue; }
      snprintf(path, sizeof path, "first_person.%s.%s", name, v->ids[i]);
      if (v->kind == TFP_SETTINGS_MARKERS) printf("%s=true\n", path);
      else if (v->kind == TFP_SETTINGS_PLACEMENTS) dump_placement(path, &v->placements[i].placement);
      else if (v->kind == TFP_SETTINGS_NEAR) {
        if (!v->near_clip[i].has_hip && !v->near_clip[i].has_aim) printf("%s={}\n", path);
        if (v->near_clip[i].has_hip) printf("%s.hip_cm=%.17g\n", path, v->near_clip[i].hip_cm);
        if (v->near_clip[i].has_aim) printf("%s.aim_cm=%.17g\n", path, v->near_clip[i].aim_cm);
      } else {
        if (!v->profiles[i].has) printf("%s={}\n", path);
        for (int action = 0; action < TFP_ACT_COUNT; action++) {
          if (!(v->profiles[i].has >> action & 1)) continue;
          snprintf(path, sizeof path, "first_person.%s.%s.%s", name, v->ids[i], tfp_action_name((TfpAct)action));
          dump_placement(path, &v->profiles[i].action[action]);
        }
      }
    }
  }
  for (int id = 0; id < TFP_MENU_COUNT; id++) {
    if (s->menu[id].is_bool) printf("menu.%s=%s\n", tfp_settings_menu_name((TfpSettingsMenuId)id), s->menu[id].boolean ? "true" : "false");
    else printf("menu.%s=%.17g\n", tfp_settings_menu_name((TfpSettingsMenuId)id), s->menu[id].number);
  }
}
static int file_mode(const char *mode, const char *path) {
  static char bytes[TFP_SETTINGS_MAX + 1], out[TFP_SETTINGS_MAX];
  static TfpSettings s;
  char why[200];
  FILE *file = fopen(path, "rb");
  if (!file) { fprintf(stderr, "cannot open %s\n", path); return 2; }
  size_t n = fread(bytes, 1, sizeof bytes, file);
  fclose(file);
  if (!tfp_settings_load(&s, bytes, n, why, sizeof why)) { fprintf(stderr, "refused: %s\n", why); return 1; }
  if (!strcmp(mode, "--dump")) dump(&s);
  else { tfp_settings_write(&s, out, sizeof out); fputs(out, stdout); }
  tfp_settings_free(&s);
  return 0;
}

/* ---- retail's answers replayed through the host pointers ---- */
static int replay(const char *label, const SettingsOp *ops, size_t count) {
  TfpRtHost *h = &wire.host;
  TfpSettings *store = tfp_wire_settings(&wire);
  for (size_t i = 0; i < count; i++) {
    const SettingsOp *op = &ops[i];
    V key = text(op->key), name = text(op->name), o1 = tfp_wire_nil(), o2 = tfp_wire_nil();
    TfpActionPlacement p, given = {op->in[0], op->in[1], op->in[2], op->in[3], op->in[4], op->in[5], op->in_mask};
    TfpRtTable table;
    bool ok = false, returned;
    switch (op->op) {
      case 'R': case 'D':
        returned = op->op == 'R' ? h->actions_resolve(h, key, name, &o1, &o2) : h->actions_default_placement(h, key, name, &o1, &o2);
        if (op->answer == 'E') ok = !returned && h->raised && !strcmp(h->raised, op->text);
        else ok = returned && tfp_wire_settings_placement_of(&wire, o1, &p) && same_placement(&p, op->mask, op->v) &&
                  (op->op == 'D' ? o2.kind == TFP_RT_NIL : o2.kind == (op->extra ? TFP_RT_TRUE : TFP_RT_FALSE));
        if (ok && op->op == 'R' && op->answer == 'P' && (!op->name || tfp_action_from_name(op->name) != TFP_ACT_NONE)) {
          /* the log's wording agrees with retail's `explicit` */
          char source[48];
          tfp_wire_settings_source(&wire, op->key && strlen(op->key) < 64 ? op->key : NULL, op->name ? tfp_action_from_name(op->name) : TFP_ACT_IDLE,
                                   source, sizeof source);
          bool named = !strncmp(source, "imported(own)", 13) || !strncmp(source, "imported(__all)", 15);
          ok = named == (op->extra == 1);
        }
        break;
      case 'I':
        ok = h->actions_weapon_info(h, key, &table) && table.present == (op->text != NULL) &&
             (!op->text || (text_is(table.f[TFP_RT_WINFO_NAME], op->text) && text_is(table.f[TFP_RT_WINFO_FAMILY], op->text2)));
        break;
      case 'N': {
        double hip, aim;
        bool profile;
        tfp_settings_near_options(store, op->key, &hip, &aim, &profile);
        ok = hip == op->v[0] && aim == op->v[1] && profile == (op->extra == 1);
        break;
      }
      case 'O': { /* runtime.lua:816-821: the defaults the runtime hands the menu */
        V defaults[TFP_RT_DEFAULTS_N], scale = h->config_field(h, "motion_scale");
        TfpMotionOptions options;
        defaults[TFP_RT_DEFAULTS_ENABLED] = tfp_wire_bool(h->config_field(h, "motion_enabled").kind != TFP_RT_FALSE);
        defaults[TFP_RT_DEFAULTS_SCALE] = scale.kind == TFP_RT_NUMBER ? scale : tfp_wire_number(1);
        defaults[TFP_RT_DEFAULTS_CAMERA_BOB_STRENGTH] = defaults[TFP_RT_DEFAULTS_CAMERA_SWAY_STRENGTH] = tfp_wire_number(0);
        ok = h->menu_motion_options(h, defaults, &o1) && tfp_wire_settings_options_of(&wire, o1, &options) &&
             options.enabled == (op->extra ? TFP_MOTION_TRUE : TFP_MOTION_FALSE) && options.scale.kind == TFP_MOTION_NUMBER &&
             options.scale.value == op->v[0] && options.editor_frozen == TFP_MOTION_ABSENT && options.shoulder_aim_enabled == TFP_MOTION_ABSENT &&
             options.setting[TFP_MOTION_CAMERA_BOB_SPEED].kind == TFP_MOTION_NIL && options.setting[TFP_MOTION_CAMERA_SWAY_SPEED].kind == TFP_MOTION_NIL;
        static const int order[7] = {TFP_MOTION_BOB_STRENGTH, TFP_MOTION_BOB_SPEED, TFP_MOTION_SWAY_STRENGTH, TFP_MOTION_SWAY_SPEED,
                                     TFP_MOTION_SPRINT_STRENGTH, TFP_MOTION_CAMERA_BOB_STRENGTH, TFP_MOTION_CAMERA_SWAY_STRENGTH};
        for (int k = 0; ok && k < 7; k++) ok = options.setting[order[k]].kind == TFP_MOTION_NUMBER && options.setting[order[k]].value == op->v[1 + k];
        break;
      }
      case 'F': {
        V arms, torso, native, mode;
        ok = h->menu_fird_person_arms(h, &arms) && h->menu_show_torso(h, &torso) && h->menu_native_ads(h, &native) && h->menu_ads_mode(h, &mode) &&
             (arms.kind == TFP_RT_TRUE) == (op->extra & 1) && (torso.kind == TFP_RT_TRUE) == (op->extra >> 1 & 1) &&
             (native.kind == TFP_RT_TRUE) == (op->extra >> 2 & 1) && text_is(mode, op->text);
        break;
      }
      case 'V': {
        V value = h->config_field(h, op->name);
        const TfpSettingsValue *saved = tfp_wire_settings_value_of(&wire, value);
        if (op->answer == '0') ok = value.kind == TFP_RT_NIL;
        else if (op->answer == 'b') ok = value.kind == (op->extra ? TFP_RT_TRUE : TFP_RT_FALSE);
        else if (op->answer == 'n') ok = value.kind == TFP_RT_NUMBER && value.number == op->v[0];
        else if (op->answer == 't') ok = saved && saved->kind != TFP_SETTINGS_PLACEMENT && saved->count == (size_t)op->extra;
        else ok = saved && saved->kind == TFP_SETTINGS_PLACEMENT && same_placement(&saved->placement, op->mask, op->v) &&
                  tfp_wire_settings_placement_of(&wire, value, &p) && same_placement(&p, op->mask, op->v);
        break;
      }
      case 'M':
        ok = h->config_action_mapped(h, name).kind == (op->extra ? TFP_RT_TRUE : TFP_RT_FALSE);
        break;
      case 'S': { /* runtime.lua:782-783 */
        V profiles = tfp_wire_nil();
        returned = h->actions_saved(h, key, name, tfp_wire_settings_placement(&wire, &given), tfp_wire_bool(op->flag), &profiles);
        if (op->answer == 'E') ok = !returned && h->raised && !strcmp(h->raised, op->text);
        else ok = returned && profiles.kind == TFP_RT_REF && h->mod_set(h, "action_placements", profiles, &o1, &o2) && o1.kind == TFP_RT_TRUE &&
                  o2.kind == TFP_RT_NIL;
        break;
      }
      case 'X': { /* runtime.lua:806-811, as tfp_runtime.c runs it */
        static const char *const keys[3] = {"weapon_placements", "action_placements", "placement"};
        V before[3], values[3] = {tfp_wire_ref(&tfp_rt_empty_table), tfp_wire_ref(&tfp_rt_empty_table), tfp_wire_ref(tfp_rt_default_placement)};
        TfpActionPlacement old = {0, 0, 0, 0, 0, 0, 0}, kept = {0, 0, 0, 0, 0, 0, 0};
        for (int k = 0; k < 3; k++) before[k] = h->config_field(h, keys[k]);
        bool had = tfp_wire_settings_placement_of(&wire, before[2], &old);
        size_t rows = before[1].kind == TFP_RT_REF ? tfp_wire_settings_value_of(&wire, before[1])->count : 0;
        ok = true;
        for (int k = 0; ok && k < 3; k++) ok = h->mod_set(h, keys[k], values[k], &o1, &o2) && o1.kind == TFP_RT_TRUE;
        /* the tables read before the writes still hold what they held (retail puts them back on a failure) */
        if (ok && had) ok = tfp_wire_settings_placement_of(&wire, before[2], &kept) && !memcmp(&old, &kept, sizeof old);
        if (ok && before[1].kind == TFP_RT_REF) ok = tfp_wire_settings_value_of(&wire, before[1])->count == rows;
        break;
      }
      case 'W': {
        V value = op->flag == 0 ? tfp_wire_nil() : op->flag == 1 ? tfp_wire_bool(false) : op->flag == 2 ? tfp_wire_bool(true)
                  : op->flag == 3 ? tfp_wire_number(op->in[0]) : tfp_wire_settings_placement(&wire, &given);
        ok = h->mod_set(h, op->name, value, &o1, &o2) && o1.kind == (op->extra ? TFP_RT_TRUE : TFP_RT_FALSE);
        break;
      }
      case 'U': {
        int id = tfp_settings_menu(op->name);
        ok = id >= 0 && tfp_settings_set_menu(store, (TfpSettingsMenuId)id, op->flag != 3, op->flag == 2, op->in[0]);
        break;
      }
      default: break;
    }
    checks++;
    if (!ok) {
      printf("wire settings: FAILED %s operation %zu ('%c' key %s name %s): retail answered '%c'\n", label, i, op->op, op->key ? op->key : "nil",
             op->name ? op->name : "nil", op->answer);
      return 1;
    }
  }
  return 0;
}

static bool foreign_decode(TfpWire *w, V value, TfpOpsPlacement *out) {
  static const char marker = 0;
  (void)w;
  if (value.ref != &marker && value.ref != (const void *)foreign_decode) return false;
  *out = (TfpOpsPlacement){.25, 0, 0, 0, 0, 0, TFP_OPS_X};
  return true;
}

/* ---- the two files in the user's folder, in memory: what src/mod_view.c does with Windows calls ---- */
static struct DiskFile { char name[64]; char *text; size_t length; } disk_files[24];
static unsigned disk_writes[2], disk_copies;
static int disk_fail_write = -1, disk_fail_copy; /* the file whose write fails; copies fail */
static const char *const disk_names[2] = {"settings.json", "locked_view.json"};
static bool disk_unreadable[2];
static const char *disk_beside;
static struct DiskFile *disk_find(const char *name) {
  for (size_t i = 0; i < COUNT(disk_files); i++) if (disk_files[i].text && !strcmp(disk_files[i].name, name)) return &disk_files[i];
  return NULL;
}
static void disk_put(const char *name, const char *text, size_t length) {
  struct DiskFile *f = disk_find(name);
  for (size_t i = 0; !f && i < COUNT(disk_files); i++) if (!disk_files[i].text) f = &disk_files[i];
  if (!f) { printf("wire settings: the test disk is full\n"); exit(1); }
  free(f->text);
  snprintf(f->name, sizeof f->name, "%s", name);
  f->text = malloc(length + 1), memcpy(f->text, text, length), f->text[length] = 0, f->length = length;
}
static void disk_clear(void) {
  for (size_t i = 0; i < COUNT(disk_files); i++) free(disk_files[i].text), disk_files[i].text = NULL;
  disk_writes[0] = disk_writes[1] = disk_copies = 0, disk_fail_write = -1, disk_fail_copy = 0;
  disk_unreadable[0] = disk_unreadable[1] = false, disk_beside = NULL;
}
static const char *disk_text(const char *name) { struct DiskFile *f = disk_find(name); return f ? f->text : NULL; }
static unsigned disk_count(void) { unsigned n = 0; for (size_t i = 0; i < COUNT(disk_files); i++) n += disk_files[i].text != NULL; return n; }
static bool disk_write(void *user, TfpSettingsFile file, const char *text, size_t length, char *why, size_t why_size) {
  (void)user;
  if (disk_fail_write == (int)file) { snprintf(why, why_size, "disk full"); return false; }
  disk_writes[file]++;
  disk_put(disk_names[file], text, length);
  return true;
}
static TfpSettingsCopy disk_copy(void *user, TfpSettingsFile file, const char *suffix, char *why, size_t why_size) {
  char name[64];
  (void)user;
  struct DiskFile *from = disk_find(disk_names[file]);
  if (!from) return TFP_COPY_NONE;
  snprintf(name, sizeof name, "%s%s", disk_names[file], suffix);
  if (disk_find(name)) return TFP_COPY_EXISTS;
  if (disk_fail_copy) { snprintf(why, why_size, "access denied"); return TFP_COPY_FAILED; }
  disk_copies++;
  disk_put(name, from->text, from->length);
  return TFP_COPY_DONE;
}
static bool disk_same(void *user, TfpSettingsFile file, const char *suffix) {
  char name[64];
  (void)user;
  snprintf(name, sizeof name, "%s%s", disk_names[file], suffix);
  const struct DiskFile *from = disk_find(disk_names[file]), *copy = disk_find(name);
  return from && copy && from->length == copy->length && !memcmp(from->text, copy->text, from->length);
}
static const TfpSettingsDisk disk = {NULL, disk_write, disk_copy, disk_same};
/* A start of the mod on the test disk: settings.json and locked_view.json are read as src/mod_view.c reads them.
 * disk_unreadable: the file is there and gives no bytes at the start (it cannot be opened); for settings.json the
 * file beside the DLL (disk_beside) is then the one bound, as read_settings falls through to it. */
static bool start_on_disk(void) {
  struct DiskFile *settings = disk_unreadable[0] ? NULL : disk_find("settings.json"), *locked = disk_unreadable[1] ? NULL : disk_find("locked_view.json");
  const char *text = settings ? settings->text : disk_beside;
  new_wire();
  if (!tfp_wire_settings_bind(&wire, text, settings ? settings->length : text ? strlen(text) : 0, "settings.json")) return false;
  tfp_wire_settings_bind_locked(&wire, locked ? locked->text : NULL, locked ? locked->length : 0, "locked_view.json");
  tfp_wire_settings_disk(&wire, &disk, settings != NULL);
  return true;
}
/* The top-level member names of a text tfp_settings_write made (one space, then the name), joined by commas. */
static void top_level_names(const char *text, char *out, size_t size) {
  out[0] = 0;
  for (const char *line = text; line && *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : NULL) {
    if (line[0] != ' ' || line[1] != '"') continue;
    const char *end = strchr(line + 2, '"');
    size_t used = strlen(out);
    snprintf(out + used, size - used, "%s%.*s", used ? "," : "", (int)(end - line - 2), line + 2);
  }
}
/* settings.json as 0.6.0-feel.24 wrote it: this build's text with the "locked_view" member between "format" and "menu". */
#define FEEL24_LOCKED " \"locked_view\": {\n  \"animated_arms\": false,\n  \"camera_lock\": true,\n  \"fov_degrees\": 104,\n  \"fov_lock\": true,\n" \
  "  \"fov_zoom_when_aiming\": false,\n  \"kick_max_degrees\": 8,\n  \"kick_strength\": 55,\n  \"one_handed_pistols\": true,\n  \"put_away\": true,\n" \
  "  \"smooth_eye\": true,\n  \"thrown_kick_strength\": 70\n },\n"
static char feel19_text[TFP_SETTINGS_MAX], feel24_text[TFP_SETTINGS_MAX];
static bool locked_as_feel24(void) {
  return !tfp_locked_view_animated_arms(&wire) && tfp_locked_view_camera_lock(&wire) && tfp_locked_view_fov_degrees(&wire) == 104 &&
         !tfp_locked_view_fov_zoom_when_aiming(&wire) && tfp_locked_view_kick_strength(&wire) == 55 && tfp_locked_view_put_away(&wire);
}
/* ---- round 20: "Aim field of view" and the sight distance in aim (locked_view.json) ---- */
static int aim_part(void) {
  static TfpSettings s;
  char why[200], text[1 << 14];
  /* defaults: the game's zoom in aim (as "Zoom when aiming" on), a chosen value of 50 degrees, the sight brought to 10 cm */
  CHECK(tfp_settings_defaults(&s));
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 1 && tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim_degrees) == 50);
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_ads_sight_cm) == 10 && tfp_settings_locked_flag(&s, TFP_LOCKED_fov_zoom_when_aiming));
  /* a file of feel.51 and before: the toggle off is "your locked view", on is "the game's zoom" */
  static const char off[] = "{\"format\":1,\"locked_view\":{\"fov_zoom_when_aiming\":false}}", on[] = "{\"format\":1,\"locked_view\":{\"fov_zoom_when_aiming\":true}}";
  CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, off, strlen(off), why, sizeof why));
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 2 && !s.locked_skipped[0]);
  CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, on, strlen(on), why, sizeof why) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 1);
  /* a file of this build: its own fov_aim wins over the toggle beside it */
  static const char both[] = "{\"format\":1,\"locked_view\":{\"fov_aim\":3,\"fov_aim_degrees\":42,\"fov_zoom_when_aiming\":false,\"ads_sight_cm\":7}}";
  CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, both, strlen(both), why, sizeof why));
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 3 && tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim_degrees) == 42);
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_ads_sight_cm) == 7);
  /* setting the choice keeps the old toggle in step, so a build before this one reads the nearest answer from the file */
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_aim, false, false, 1) && tfp_settings_locked_flag(&s, TFP_LOCKED_fov_zoom_when_aiming));
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_aim, false, false, 2) && !tfp_settings_locked_flag(&s, TFP_LOCKED_fov_zoom_when_aiming));
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_aim, false, false, 3));
  CHECK(!tfp_settings_locked_flag(&s, TFP_LOCKED_fov_zoom_when_aiming));
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_aim, false, false, 9) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 3); /* kept in range */
  /* and the other way: the toggle set by an old path moves the choice between its two answers, a chosen value stays */
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_zoom_when_aiming, true, true, 0) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 3);
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_aim, false, false, 2) && tfp_settings_set_locked(&s, TFP_LOCKED_fov_zoom_when_aiming, true, true, 0));
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 1);
  /* written and read again: all three, and the toggle */
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_aim, false, false, 3) && tfp_settings_set_locked(&s, TFP_LOCKED_ads_sight_cm, false, false, 14));
  const size_t n = tfp_settings_write_locked(&s, text, sizeof text);
  CHECK(n > 0 && strstr(text, "\"fov_aim\": 3") && strstr(text, "\"ads_sight_cm\": 14") && strstr(text, "\"fov_zoom_when_aiming\": false"));
  static TfpSettings again;
  CHECK(tfp_settings_defaults(&again) && tfp_settings_load_locked(&again, text, n, why, sizeof why));
  CHECK(tfp_settings_locked_number(&again, TFP_LOCKED_fov_aim) == 3 && tfp_settings_locked_number(&again, TFP_LOCKED_ads_sight_cm) == 14);
  /* what the field-of-view helper is handed (src/fov_lock.h): the game's zoom, the locked view, or the chosen radians */
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, "{\"format\":1,\"first_person\":{}}", 30, "settings.json"));
  tfp_wire_settings_bind_locked(&wire, both, strlen(both), "locked_view.json");
  CHECK(fabs(tfp_locked_view_aim_field(&wire) - 42 * 3.14159265358979 / 180) < 1e-6);
  CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_aim, false, false, 1) && tfp_locked_view_aim_field(&wire) == TFP_FOV_AIM_ZOOM);
  CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_aim, false, false, 2) && tfp_locked_view_aim_field(&wire) == TFP_FOV_AIM_LOCKED);
  CHECK(!tfp_locked_view_fov_zoom_when_aiming(&wire));
  CHECK(tfp_locked_view_ads_sight_cm(&wire) == 7);
  return 0;
}
/* ---- the one list (src/wire_menu.c): every option of both files, each with a setter, once ---- */
static bool has_fird(const char *text) {
  for (const char *t = text; t && *t; t++) if ((t[0] == 'F' || t[0] == 'f') && !strncmp(t + 1, "ird", 3)) return true;
  return false;
}
static int list_part(void) {
  int count = 0, skipped = 0;
  const TfpMenuControl *controls = tfp_wire_menu_controls(&count);
  const TfpMenuNotOffered *out = tfp_wire_menu_not_offered(&skipped);
  CHECK(controls && count >= 42 && out && skipped > 0);
  /* no duplicate id or label; a page each, every page used; no "Fird" in what the user reads (the stored key
   * fird_person_arms keeps its name: settings.json files have it) */
  int per_page[TFP_MENU_PAGES] = {0};
  for (int i = 0; i < count; i++) {
    const TfpMenuControl *c = &controls[i];
    CHECK(c->id && c->label && c->page >= 0 && c->page < TFP_MENU_PAGES);
    per_page[c->page]++;
    CHECK(!has_fird(c->label) && !has_fird(c->description) && !has_fird(c->note));
    for (int j = 0; j < i; j++) CHECK(strcmp(controls[j].id, c->id) && strcmp(controls[j].label, c->label));
  }
  for (int page = 0; page < TFP_MENU_PAGES; page++) CHECK(per_page[page] > 0 && tfp_wire_menu_pages[page] && !has_fird(tfp_wire_menu_pages[page]));
  CHECK(!strcmp(tfp_wire_menu_pages[2], "Locked view") && !strcmp(tfp_wire_menu_page_ids[2], "locked_view"));
  /* where each control is stored: one place, never two; a button and a status row store nothing */
  for (int i = 0; i < count; i++) {
    const TfpMenuControl *c = &controls[i];
    const int places = (c->menu >= 0) + (c->locked >= 0) + (c->config >= 0);
    CHECK(places == (c->kind == TFP_MK_BUTTON || c->kind == TFP_MK_STATUS ? 0 : 1));
    if (c->kind == TFP_MK_CHOICE) CHECK(c->choices && c->choice_count >= 2);
    if (c->locked >= 0) {
      /* the third page holds the locked view's options, with the store's own ranges */
      const TfpSettingsLockedInfo *info = tfp_settings_locked_info((TfpSettingsLockedId)c->locked);
      CHECK(info && c->page == 2);
      if (c->kind == TFP_MK_SLIDER) CHECK(!info->is_bool && c->min == info->min && c->max == info->max && c->step == info->step);
      else if (c->kind == TFP_MK_CHOICE && !info->is_bool) CHECK(info->min == 1 && info->max == c->choice_count && info->step == 1); /* a choice over a number */
      else CHECK(info->is_bool && (c->kind == TFP_MK_TOGGLE || c->kind == TFP_MK_CHECK || (c->kind == TFP_MK_CHOICE && c->choice_count == 2)));
    }
  }
  /* every key of both files: a control's or a reason's, exactly once */
  int named = 0;
  for (int id = 0; id < TFP_MENU_COUNT; id++, named++) {
    int controls_of = 0, reasons = 0;
    for (int i = 0; i < count; i++) controls_of += controls[i].menu == id;
    for (int i = 0; i < skipped; i++) reasons += !strcmp(out[i].file, "settings.json menu") && !strcmp(out[i].key, tfp_settings_menu_name((TfpSettingsMenuId)id));
    if (controls_of + reasons != 1) printf("wire settings: menu.%s: %d controls, %d reasons\n", tfp_settings_menu_name((TfpSettingsMenuId)id), controls_of, reasons);
    CHECK(controls_of + reasons == 1);
  }
  for (int id = 0; id < TFP_LOCKED_COUNT; id++, named++) {
    int controls_of = 0, reasons = 0;
    for (int i = 0; i < count; i++) controls_of += controls[i].locked == id;
    for (int i = 0; i < skipped; i++) reasons += !strcmp(out[i].file, "locked_view.json") && !strcmp(out[i].key, tfp_settings_locked_info((TfpSettingsLockedId)id)->name);
    if (controls_of + reasons != 1) printf("wire settings: locked_view.%s: %d controls, %d reasons\n", tfp_settings_locked_info((TfpSettingsLockedId)id)->name, controls_of, reasons);
    CHECK(controls_of + reasons == 1);
  }
  for (int id = 0; id < TFP_SET_COUNT; id++, named++) {
    int controls_of = 0, reasons = 0;
    for (int i = 0; i < count; i++) controls_of += controls[i].config == id;
    for (int i = 0; i < skipped; i++) reasons += !strcmp(out[i].file, "settings.json first_person") && !strcmp(out[i].key, tfp_settings_field_name((TfpSettingsField)id));
    if (controls_of + reasons != 1) printf("wire settings: first_person.%s: %d controls, %d reasons\n", tfp_settings_field_name((TfpSettingsField)id), controls_of, reasons);
    CHECK(controls_of + reasons == 1);
  }
  int stored = 0;
  for (int i = 0; i < count; i++) stored += controls[i].kind != TFP_MK_BUTTON && controls[i].kind != TFP_MK_STATUS;
  CHECK(named == stored + skipped);   /* and no reason names a key that is not there */
  for (int i = 0; i < skipped; i++) CHECK(out[i].why && strlen(out[i].why) > 20 && out[i].key);
  /* the options the locked view brought are all in: field of view and its lock and zoom, the camera style, the kicks,
   * damping, the three live features */
  static const char *const wanted[] = {"camera_lock", "fov_lock", "fov_degrees", "fov_aim", "fov_aim_degrees", "ads_sight_cm", "camera_style", "kick_strength",
                                        "kick_max_degrees", "thrown_kick_strength", "camera_damping", "animated_arms", "one_handed_pistols",
                                        "put_away", "smooth_eye", "edit_placement", "editor_key", "toggle_key", "fird_person_arms"};
  for (size_t k = 0; k < COUNT(wanted); k++) {
    const TfpMenuControl *c = NULL;
    for (int i = 0; i < count; i++) if (!strcmp(controls[i].id, wanted[k])) c = &controls[i];
    if (!c) printf("wire settings: no control %s\n", wanted[k]);
    CHECK(c);
  }
  for (int i = 0; i < count; i++)
    if (!strcmp(controls[i].id, "fird_person_arms")) CHECK(!strcmp(controls[i].label, "First Person Arms"));
    else if (!strcmp(controls[i].id, "camera_style"))
      CHECK(controls[i].kind == TFP_MK_CHOICE && controls[i].locked == TFP_LOCKED_bodycam && !strcmp(controls[i].choices[0], "Stable") && !strcmp(controls[i].choices[1], "Bodycam"));
    else if (!strcmp(controls[i].id, "one_handed_pistols")) CHECK(controls[i].note && controls[i].note_when == 0 && controls[i].inert);
    else CHECK(!controls[i].inert);
  /* every stored control reaches its store through the menu (the setter), at once, and shows the new value */
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, settings_user_json, sizeof settings_user_json - 1, "settings.json"));
  TfpSettings *s = tfp_wire_settings(&wire);
  mcm_count = mcm_sets = mcm_labels = mcm_disables = mcm_state_count = 0, mcm_disabled[0] = 0;
  TfpMenuHost host = {NULL, mcm_get, mcm_set, mcm_set_disabled, mcm_set_label, NULL, 33};
  CHECK(tfp_wire_menu_bind(&wire, &host, 0, test_edit));
  for (int i = 0; i < count; i++) {
    const TfpMenuControl *c = &controls[i];
    TfpMenuValue now, want;
    if (c->menu < 0 && c->locked < 0) continue;   /* the two `config` toggles go through the runtime: below in the MCM part */
    CHECK(tfp_wire_menu_default(&wire, c, 33, &now));
    want = now;
    switch (c->kind) {
      case TFP_MK_TOGGLE: case TFP_MK_CHECK: want.boolean = !now.boolean; break;
      case TFP_MK_SLIDER: want.number = now.number == c->max ? c->min : c->max; break;
      case TFP_MK_CHOICE: want.number = now.number == 1 ? 2 : 1; break;
      case TFP_MK_KEY: want.number = now.number == 0x41 ? 0x42 : 0x41; break;
      default: CHECK(false);
    }
    const uint64_t before = s->menu_writes + s->locked_writes;
    tfp_wire_menu_changed(&wire, c->id, &want, &now);
    TfpMenuValue after;
    CHECK(tfp_wire_menu_default(&wire, c, 33, &after) && after.kind == want.kind && after.boolean == want.boolean && after.number == want.number);
    if (s->menu_writes + s->locked_writes != before + 1) printf("wire settings: %s reached no store\n", c->id);
    CHECK(s->menu_writes + s->locked_writes == before + 1);
  }
  CHECK(tfp_settings_locked_flag(s, TFP_LOCKED_bodycam) != tfp_settings_locked_info(TFP_LOCKED_bodycam)->value);   /* the style choice moved bodycam */
  /* every button is one the menu knows */
  for (int i = 0; i < count; i++) if (controls[i].kind == TFP_MK_BUTTON) CHECK(!tfp_wire_menu_activate(&wire, controls[i].id));
  line_count = 0;
  tfp_wire_menu_frame(&wire);
  CHECK(!logged("Unknown button"));
  tfp_wire_menu_unbind(&wire);
  tfp_wire_settings_free(&wire);
  printf("wire settings: the one list: %d controls on %d pages, %d keys of the files that are no control, each with its reason\n", count,
         TFP_MENU_PAGES, skipped);
  return 0;
}

static int compat_part(void) {
  char problem[200], names[200];
  { /* ---- what this build writes as settings.json is what 0.6.0-feel.19 (e9b3c10) reads: its loader refuses the whole
     * document on a top-level member other than format, source, first_person, menu, unread ---- */
    TfpSettings s;
    memset(&s, 0, sizeof s);
    CHECK(tfp_settings_load(&s, settings_user_json, sizeof settings_user_json - 1, problem, sizeof problem));
    CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_degrees, false, false, 104) && tfp_settings_set_locked(&s, TFP_LOCKED_camera_lock, true, false, 0));
    size_t n = tfp_settings_write(&s, feel19_text, sizeof feel19_text);
    CHECK(n && n < sizeof feel19_text);
    top_level_names(feel19_text, names, sizeof names);
    CHECK(!strcmp(names, "first_person,format,menu"));
    CHECK(!strstr(feel19_text, "locked_view") && !strstr(feel19_text, "fov_degrees") && !strstr(feel19_text, "camera_lock"));
    const char *out = getenv("TFP_SETTINGS_OUT"); /* tests/run-wire-settings.sh hands the text to the loader of e9b3c10 itself */
    if (out) {
      FILE *file = fopen(out, "wb");
      CHECK(file && fwrite(feel19_text, 1, n, file) == n && !fclose(file));
    }
    tfp_settings_free(&s);
    /* the same file as 0.6.0-feel.24 wrote it */
    const char *menu = strstr(feel19_text, " \"menu\": {");
    CHECK(menu && menu > feel19_text + 14 && !memcmp(menu - 14, " \"format\": 1,\n", 14));
    snprintf(feel24_text, sizeof feel24_text, "%.*s%s%s", (int)(menu - feel19_text), feel19_text, FEEL24_LOCKED, menu);
    top_level_names(feel24_text, names, sizeof names);
    CHECK(!strcmp(names, "first_person,format,locked_view,menu"));
  }
  { /* ---- a file as 0.6.0-feel.19 wrote it loads and nothing is written or copied at the start ---- */
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    CHECK(start_on_disk() && !logged("Refused") && !logged("Converted") && !logged("Note:"));
    CHECK(tfp_wire_settings(&wire)->field[TFP_SET_action_placements]->count == 19 && tfp_locked_view_fov_degrees(&wire) == 80);
    for (int i = 0; i < 3; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == 1 && !strcmp(disk_text("settings.json"), feel19_text));
    CHECK(!tfp_wire_settings_unsaved(&wire));
    /* a locked-view option changes: locked_view.json only; a placement changes: settings.json only, still without the member */
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 96) && tfp_wire_settings_unsaved(&wire));
    for (int i = 0; i < 3; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && disk_writes[1] == 1 && !disk_copies && disk_count() == 2 && !strcmp(disk_text("settings.json"), feel19_text));
    CHECK(strstr(disk_text("locked_view.json"), "\"fov_degrees\": 96") && !tfp_wire_settings_unsaved(&wire));
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    for (int i = 0; i < 3; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 1 && !disk_copies && disk_count() == 2 && !strstr(disk_text("settings.json"), "locked_view"));
    top_level_names(disk_text("settings.json"), names, sizeof names);
    CHECK(!strcmp(names, "first_person,format,menu"));
    tfp_wire_settings_free(&wire);
    /* the next start reads both */
    CHECK(start_on_disk() && tfp_locked_view_fov_degrees(&wire) == 96 && logged("Ready: locked view options read from locked_view.json") && !logged("Refused"));
    CHECK(tfp_settings_menu_number(tfp_wire_settings(&wire), TFP_MENU_motion_strength) == 135 && disk_writes[0] == 1 && disk_writes[1] == 1 && !disk_copies);
    tfp_wire_settings_free(&wire);
  }
  { /* ---- a file of 0.6.0-feel.20 to feel.24 is converted at the start: the member is gone from settings.json, the
     * file as it was is kept, the options are in locked_view.json, and everything else is as it was ---- */
    disk_clear();
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    CHECK(start_on_disk() && !logged("Refused") && logged("Converted: settings.json was written by 0.6.0-feel.20 to feel.24"));
    CHECK(locked_as_feel24() && !tfp_wire_settings_unsaved(&wire));
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 2 && disk_copies == 1 && disk_count() == 3); /* locked_view.json twice: with the member it was taken from until settings.json is written, then without */
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), feel24_text));   /* the backup: the file as it was */
    CHECK(!strcmp(disk_text("settings.json"), feel19_text));                             /* byte for byte the old file without the member */
    CHECK(strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104") && strstr(disk_text("locked_view.json"), "\"animated_arms\": false") &&
          strstr(disk_text("locked_view.json"), "\"kick_strength\": 55"));
    for (int i = 0; i < 3; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 2 && disk_copies == 1);               /* once, and not again by the save frame */
    /* the placements: every saved table of the converted file is the one of the file as it was */
    TfpSettings before, after;
    memset(&before, 0, sizeof before), memset(&after, 0, sizeof after);
    CHECK(tfp_settings_load(&before, feel24_text, strlen(feel24_text), problem, sizeof problem));
    CHECK(tfp_settings_load(&after, disk_text("settings.json"), strlen(disk_text("settings.json")), problem, sizeof problem) && !after.locked_in_settings);
    const TfpSettingsValue *a = before.field[TFP_SET_action_placements], *b = after.field[TFP_SET_action_placements];
    CHECK(a && b && a->count == b->count && a->count == 19);
    for (size_t i = 0; i < a->count; i++) {
      CHECK(!strcmp(a->ids[i], b->ids[i]) && a->profiles[i].has == b->profiles[i].has);
      for (int action = 0; action < TFP_ACT_COUNT; action++)
        CHECK(!memcmp(&a->profiles[i].action[action], &b->profiles[i].action[action], sizeof a->profiles[i].action[action]));
    }
    a = before.field[TFP_SET_weapon_placements], b = after.field[TFP_SET_weapon_placements];
    CHECK(a && b && a->count == b->count);
    for (size_t i = 0; i < a->count; i++) CHECK(!strcmp(a->ids[i], b->ids[i]) && !memcmp(&a->placements[i].placement, &b->placements[i].placement, sizeof a->placements[i].placement));
    for (int i = 0; i < TFP_MENU_COUNT; i++)
      CHECK(before.menu[i].is_bool == after.menu[i].is_bool && before.menu[i].boolean == after.menu[i].boolean && before.menu[i].number == after.menu[i].number);
    tfp_settings_free(&before), tfp_settings_free(&after);
    tfp_wire_settings_free(&wire);
    /* the next start: nothing to convert, the options come from locked_view.json */
    CHECK(start_on_disk() && !logged("Converted") && !logged("Refused") && locked_as_feel24());
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 2 && disk_copies == 1 && disk_count() == 3);
    tfp_wire_settings_free(&wire);
    /* 0.6.0-feel.24 is run once more and writes its member back (with other values); then this build again: the
     * member is the newer one and wins for what it names, locked_view.json takes those values (the file as it was is
     * kept as locked_view.json.replaced), the first backup is kept as it is and the settings.json as found gets the
     * next free numbered name; settings.json loses the member again */
    static const char older_locked[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":77}}";
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "the first backup", 16);
    disk_put("locked_view.json", older_locked, sizeof older_locked - 1);
    disk_writes[0] = disk_writes[1] = disk_copies = 0;
    CHECK(start_on_disk() && logged("Converted") && !logged("Refused"));
    CHECK(locked_as_feel24());                                                            /* 104 and the rest as the older build just wrote them */
    CHECK(logged("kept as settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2") && logged("locked_view.json.replaced"));
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 2 && disk_copies == 2 && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), "the first backup"));
    CHECK(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2") && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2"), feel24_text));
    CHECK(disk_text("locked_view.json.replaced") && !strcmp(disk_text("locked_view.json.replaced"), older_locked));
    CHECK(!strcmp(disk_text("settings.json"), feel19_text) && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104") && !tfp_wire_settings_unsaved(&wire));
    for (int i = 0; i < 3; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 2 && disk_copies == 2);
    tfp_wire_settings_free(&wire);
    /* the next start: nothing to convert, and the values the older build wrote are the ones in use */
    CHECK(start_on_disk() && !logged("Converted") && !logged("Refused") && locked_as_feel24() && disk_copies == 2);
    tfp_wire_settings_free(&wire);
  }
  { /* ---- the second conversion, case by case ---- */
    static const char partial[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"locked_view\":{\"fov_degrees\":104}}";
    static const char locked_two[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":77,\"kick_strength\":33}}";
    static const char locked_same[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":104,\"kick_strength\":33}}";
    char name[64];
    /* the member names one option: that one is the member's, every other stays locked_view.json's */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "the first backup", 16);
    disk_put("locked_view.json", locked_two, sizeof locked_two - 1);
    CHECK(start_on_disk() && logged("Converted") && !logged("Refused"));
    CHECK(tfp_locked_view_fov_degrees(&wire) == 104 && tfp_locked_view_kick_strength(&wire) == 33);
    CHECK(!strcmp(disk_text("locked_view.json.replaced"), locked_two) && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104") &&
          strstr(disk_text("locked_view.json"), "\"kick_strength\": 33"));
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2"), partial) && !strstr(disk_text("settings.json"), "locked_view"));
    tfp_wire_settings_free(&wire);
    /* the member says what locked_view.json says: that file is not touched and has no copy; settings.json as found is still kept */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "the first backup", 16);
    disk_put("locked_view.json", locked_same, sizeof locked_same - 1);
    CHECK(start_on_disk() && logged("Converted") && !logged("Refused") && !logged("replaced"));
    CHECK(disk_writes[0] == 1 && !disk_writes[1] && disk_copies == 1 && !disk_text("locked_view.json.replaced") && !strcmp(disk_text("locked_view.json"), locked_same));
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2"), partial) && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), "the first backup"));
    tfp_wire_settings_free(&wire);
    /* the member and locked_view.json differ in one boolean option and in no number: the member is still the newer
     * one, the file as it was is kept and written again, and the next start reads the member's value */
    static const char member_flag[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"locked_view\":{\"animated_arms\":true,\"fov_degrees\":77,\"kick_strength\":33}}";
    static const char locked_flag[] = "{\"format\":1,\"locked_view\":{\"animated_arms\":false,\"fov_degrees\":77,\"kick_strength\":33}}";
    disk_clear();
    disk_put("settings.json", member_flag, sizeof member_flag - 1);
    disk_put("locked_view.json", locked_flag, sizeof locked_flag - 1);
    CHECK(start_on_disk() && logged("Converted") && !logged("Refused") && logged("newer") && tfp_locked_view_animated_arms(&wire));
    CHECK(disk_text("locked_view.json.replaced") && !strcmp(disk_text("locked_view.json.replaced"), locked_flag));
    CHECK(strstr(disk_text("locked_view.json"), "\"animated_arms\": true") && !strstr(disk_text("settings.json"), "locked_view"));
    tfp_wire_settings_free(&wire);
    CHECK(start_on_disk() && !logged("Converted") && !logged("Refused") && tfp_locked_view_animated_arms(&wire) && tfp_locked_view_fov_degrees(&wire) == 77);
    tfp_wire_settings_free(&wire);
    /* .before-feel25 and -2 are taken: -3. A taken name is never written over */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "first", 5);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2", "second", 6);
    CHECK(start_on_disk() && logged("Converted") && logged("kept as settings.json" TFP_SETTINGS_BEFORE_SPLIT "-3"));
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), "first") && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2"), "second") &&
          !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-3"), partial));
    tfp_wire_settings_free(&wire);
    /* all nine names are taken: refused, nothing is written and no name is written over */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "1", 1);
    for (int n = 2; n <= 9; n++) { snprintf(name, sizeof name, "settings.json" TFP_SETTINGS_BEFORE_SPLIT "-%d", n); disk_put(name, "n", 1); }
    disk_put("locked_view.json", locked_two, sizeof locked_two - 1);
    CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds the locked view's options") &&
          logged("settings.json.before-feel25 to .before-feel25-9 are all taken; move them away"));
    CHECK(!disk_writes[0] && !strcmp(disk_text("settings.json"), partial) && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-9"), "n"));
    CHECK(tfp_locked_view_fov_degrees(&wire) == 104);                                    /* the session runs on the newer values */
    tfp_wire_settings_free(&wire);
    /* the copy is made, then settings.json cannot be written: a later save of the session does not make another copy */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "the first backup", 16);
    disk_fail_write = TFP_SETTINGS_FILE;
    CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds") && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2"), partial));
    disk_fail_write = -1;
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && !disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-3") && !strstr(disk_text("settings.json"), "locked_view"));
    tfp_wire_settings_free(&wire);
    /* the conversion fails (no copy can be made) and a locked option is changed in the session: locked_view.json, which
     * still holds the older values, is not replaced without its copy */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("locked_view.json", locked_two, sizeof locked_two - 1);
    disk_fail_copy = 1;
    CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds") && !strcmp(disk_text("locked_view.json"), locked_two));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_kick_strength, false, false, 60));
    tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[1] && !strcmp(disk_text("locked_view.json"), locked_two) && logged("Refused: locked view options were not saved:"));
    disk_fail_copy = 0;
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_kick_strength, false, false, 61));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && !strcmp(disk_text("locked_view.json.replaced"), locked_two) && strstr(disk_text("locked_view.json"), "\"kick_strength\": 61") &&
          strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104"));
    tfp_wire_settings_free(&wire);
    /* locked_view.json.replaced is taken: -2 */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("locked_view.json", locked_two, sizeof locked_two - 1);
    disk_put("locked_view.json.replaced", "an earlier one", 14);
    CHECK(start_on_disk() && logged("Converted") && logged("locked_view.json.replaced-2"));
    CHECK(!strcmp(disk_text("locked_view.json.replaced"), "an earlier one") && !strcmp(disk_text("locked_view.json.replaced-2"), locked_two));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- a conversion that did not finish, a locked option changed in that session, the next start: the member in
     * settings.json is then the one this build already took, not a newer one, and the change stays ---- */
    static const char partial[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"locked_view\":{\"fov_degrees\":104}}";
    static const char locked_two[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":77,\"kick_strength\":33}}";
    char name[64], pending[2000];
    /* settings.json cannot be written at the conversion */
    disk_clear();
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    disk_fail_write = TFP_SETTINGS_FILE;
    CHECK(start_on_disk() && logged("Refused: settings.json still holds") && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104"));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90.5));
    tfp_wire_settings_save_frame(&wire);
    CHECK(logged("Saved: locked view options") && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90.5"));
    /* the file says which member it was taken from, beside "locked_view" where 0.6.0-feel.25 to feel.30 leave it alone */
    CHECK(strstr(disk_text("locked_view.json"), "\n \"taken_from_settings\": {") && strstr(strstr(disk_text("locked_view.json"), "taken_from_settings"), "\"fov_degrees\": 104"));
    snprintf(pending, sizeof pending, "%s", disk_text("locked_view.json"));
    tfp_wire_settings_free(&wire);
    disk_fail_write = -1;
    disk_writes[0] = disk_writes[1] = 0;
    CHECK(start_on_disk() && logged("Converted") && !logged("Refused") && !logged("Note") && !logged("newer"));
    CHECK(tfp_locked_view_fov_degrees(&wire) == 90.5 && tfp_locked_view_kick_strength(&wire) == 55);
    CHECK(!disk_text("locked_view.json.replaced") && !strcmp(disk_text("settings.json"), feel19_text) && disk_writes[0] == 1);
    CHECK(strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90.5") && !strstr(disk_text("locked_view.json"), "taken_from_settings"));
    tfp_wire_settings_free(&wire);
    CHECK(start_on_disk() && !logged("Converted") && tfp_locked_view_fov_degrees(&wire) == 90.5 && disk_writes[0] == 1);
    tfp_wire_settings_free(&wire);
    /* the same unfinished state, and an older build runs before the next start and writes the member with another
     * value: that member is not the one taken, it is the newer one and wins, and the file it replaces is kept */
    {
      char later[TFP_SETTINGS_MAX];
      const char *at = strstr(feel24_text, "\"fov_degrees\": 104");
      CHECK(at != NULL);
      snprintf(later, sizeof later, "%.*s\"fov_degrees\": 99%s", (int)(at - feel24_text), feel24_text, at + strlen("\"fov_degrees\": 104"));
      disk_clear();
      disk_put("settings.json", later, strlen(later));
      disk_put("locked_view.json", pending, strlen(pending));
      CHECK(start_on_disk() && logged("Converted") && logged("newer") && tfp_locked_view_fov_degrees(&wire) == 99);
      CHECK(!strcmp(disk_text("locked_view.json.replaced"), pending) && !strstr(disk_text("locked_view.json"), "taken_from_settings"));
      tfp_wire_settings_free(&wire);
      /* the older build changed one boolean option and no number: not the member taken either, and it wins */
      const char *flag = strstr(feel24_text, "\"animated_arms\": false");
      CHECK(flag != NULL);
      snprintf(later, sizeof later, "%.*s\"animated_arms\": true%s", (int)(flag - feel24_text), feel24_text, flag + strlen("\"animated_arms\": false"));
      disk_clear();
      disk_put("settings.json", later, strlen(later));
      disk_put("locked_view.json", pending, strlen(pending));
      CHECK(start_on_disk() && logged("Converted") && logged("newer") && tfp_locked_view_animated_arms(&wire) && tfp_locked_view_fov_degrees(&wire) == 104);
      CHECK(disk_text("locked_view.json.replaced") && !strcmp(disk_text("locked_view.json.replaced"), pending) &&
            strstr(disk_text("locked_view.json"), "\"animated_arms\": true") && !strstr(disk_text("locked_view.json"), "taken_from_settings"));
      tfp_wire_settings_free(&wire);
      CHECK(start_on_disk() && !logged("Converted") && tfp_locked_view_animated_arms(&wire));
      tfp_wire_settings_free(&wire);
      /* and when that older build wrote the member as it was (nothing changed there): still the one taken */
      disk_clear();
      disk_put("settings.json", feel24_text, strlen(feel24_text));
      disk_put("locked_view.json", pending, strlen(pending));
      CHECK(start_on_disk() && logged("Converted") && !logged("newer") && tfp_locked_view_fov_degrees(&wire) == 90.5 && !disk_text("locked_view.json.replaced"));
      tfp_wire_settings_free(&wire);
    }
    /* the conversion finishes and the record cannot be taken out of locked_view.json: one note, and the next save of
     * the locked options takes it out */
    disk_clear();
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    disk_put("locked_view.json", pending, strlen(pending));
    disk_fail_write = TFP_LOCKED_FILE;
    CHECK(start_on_disk() && logged("Converted") && logged("Note: locked_view.json still names the settings.json member it was taken from (disk full)"));
    CHECK(!strcmp(disk_text("locked_view.json"), pending) && !strcmp(disk_text("settings.json"), feel19_text) && tfp_locked_view_fov_degrees(&wire) == 90.5);
    disk_fail_write = -1;
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[1]);
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 92));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 92") && !strstr(disk_text("locked_view.json"), "taken_from_settings"));
    tfp_wire_settings_free(&wire);
    /* all nine copy names taken, the member differs from locked_view.json: refused with nothing written, start after start */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "1", 1);
    for (int n = 2; n <= 9; n++) { snprintf(name, sizeof name, "settings.json" TFP_SETTINGS_BEFORE_SPLIT "-%d", n); disk_put(name, "n", 1); }
    disk_put("locked_view.json", locked_two, sizeof locked_two - 1);
    const unsigned files = disk_count();
    CHECK(start_on_disk() && logged("are all taken; move them away") && tfp_locked_view_fov_degrees(&wire) == 104);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == files && !strcmp(disk_text("locked_view.json"), locked_two));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && !strcmp(disk_text("locked_view.json.replaced"), locked_two) && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90"));
    tfp_wire_settings_free(&wire);
    for (int start = 0; start < 3; start++) { /* the change stays, no further copy is made, and each session's change is kept */
      disk_writes[0] = disk_writes[1] = disk_copies = 0;
      CHECK(start_on_disk() && logged("are all taken; move them away") && tfp_locked_view_fov_degrees(&wire) == 90 + start);
      CHECK(tfp_locked_view_kick_strength(&wire) == 33 && !disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == files + 1);
      CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 91 + start));
      tfp_wire_settings_save_frame(&wire);
      CHECK(disk_writes[1] == 1 && !disk_copies && disk_count() == files + 1 && !strcmp(disk_text("settings.json"), partial));
      tfp_wire_settings_free(&wire);
    }
    /* the names are moved away: converted, the last change in use, the record gone */
    for (int n = 2; n <= 9; n++) { snprintf(name, sizeof name, "settings.json" TFP_SETTINGS_BEFORE_SPLIT "-%d", n); struct DiskFile *f = disk_find(name); free(f->text), f->text = NULL; }
    CHECK(start_on_disk() && logged("Converted") && !logged("newer") && tfp_locked_view_fov_degrees(&wire) == 93);
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2"), partial) && !strstr(disk_text("settings.json"), "locked_view") &&
          !strstr(disk_text("locked_view.json"), "taken_from_settings") && !disk_text("locked_view.json.replaced-2"));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- a step of the conversion fails: one line, the session runs on the loaded values, settings.json is not touched ---- */
    static const struct { int fail_write; int fail_copy; const char *why; bool locked_written, backup; } failures[] = {
      {TFP_LOCKED_FILE, 0, "locked_view.json could not be written: disk full", false, true},
      {-1, 1, "settings.json could not be copied to settings.json.before-feel25: access denied", false, false}, /* the copy comes first: nothing written */
      {TFP_SETTINGS_FILE, 0, "which 0.6.0-feel.19 and older cannot read: disk full", true, true},
    };
    for (size_t i = 0; i < COUNT(failures); i++) {
      disk_clear();
      disk_put("settings.json", feel24_text, strlen(feel24_text));
      disk_fail_write = failures[i].fail_write, disk_fail_copy = failures[i].fail_copy;
      CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds the locked view's options") && logged(failures[i].why));
      int refused_lines = 0;
      for (int k = 0; k < line_count; k++) refused_lines += !strncmp(lines[k], "Refused", 7);
      CHECK(refused_lines == 1 && locked_as_feel24() && tfp_wire_settings(&wire)->field[TFP_SET_action_placements]->count == 19);
      CHECK(!strcmp(disk_text("settings.json"), feel24_text) && !disk_writes[0]);
      CHECK((disk_text("locked_view.json") != NULL) == failures[i].locked_written && (disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT) != NULL) == failures[i].backup);
      for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);   /* not tried again every frame */
      CHECK(!strcmp(disk_text("settings.json"), feel24_text) && !disk_writes[0]);
      /* a change while the disk still fails: the save does not skip the step that failed, so settings.json keeps its member */
      CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
      for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
      CHECK(!strcmp(disk_text("settings.json"), feel24_text) && !disk_writes[0] && tfp_wire_settings_unsaved(&wire) && logged("Refused: settings were not saved:"));
      if (i == 1) { /* the disk is well again within the session: the next change converts and saves in one */
        disk_fail_copy = 0;
        CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 140));
        tfp_wire_settings_save_frame(&wire);
        CHECK(disk_writes[0] == 1 && disk_writes[1] == 2 && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), feel24_text));
        CHECK(!strstr(disk_text("settings.json"), "locked_view") && strstr(disk_text("settings.json"), "\"motion_strength\": 140") &&
              strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104") && !tfp_wire_settings_unsaved(&wire));
        tfp_wire_settings_free(&wire);
        continue;
      }
      tfp_wire_settings_free(&wire);
      /* the next start, the disk well again: converted */
      disk_fail_write = -1, disk_fail_copy = 0;
      CHECK(start_on_disk() && logged("Converted") && !logged("Refused") && locked_as_feel24() && !strcmp(disk_text("settings.json"), feel19_text));
      CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), feel24_text));
      CHECK(disk_copies == 1 && !disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2")); /* the copy of the failed start is this file's: not made twice */
      tfp_wire_settings_free(&wire);
    }
    /* the file with the member is the one shipped beside the DLL, not the user's: left alone */
    disk_clear();
    new_wire();
    CHECK(tfp_wire_settings_bind(&wire, feel24_text, strlen(feel24_text), "settings.json"));
    tfp_wire_settings_bind_locked(&wire, NULL, 0, NULL);
    tfp_wire_settings_disk(&wire, &disk, false);
    CHECK(locked_as_feel24() && !disk_count() && !logged("Converted") && !logged("Refused"));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- a copy that already holds the file's bytes is the one kept: a write that keeps failing does not use up the
     * nine names with the same file, start after start ---- */
    static const char partial[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"locked_view\":{\"fov_degrees\":104}}";
    static const char locked_two[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":77,\"kick_strength\":33}}";
    static const char refused[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"menu\":{},\"a_later_member\":{\"x\":1}}";
    char name[64];
    /* settings.json cannot be written: one copy, and the line gives the write's own reason every time */
    disk_clear();
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    disk_fail_write = TFP_SETTINGS_FILE;
    for (int start = 0; start < 12; start++) {
      CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds") && logged("disk full") && !logged("all taken"));
      CHECK(disk_copies == 1 && disk_count() == 3 && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), feel24_text));
      tfp_wire_settings_free(&wire);
    }
    disk_fail_write = -1;
    CHECK(start_on_disk() && logged("Converted") && logged("kept as settings.json" TFP_SETTINGS_BEFORE_SPLIT) && !logged(TFP_SETTINGS_BEFORE_SPLIT "-2"));
    CHECK(disk_copies == 1 && disk_count() == 3 && !strcmp(disk_text("settings.json"), feel19_text));
    tfp_wire_settings_free(&wire);
    /* all nine names are taken and the ninth holds this file: not refused for the names; the write is tried */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "1", 1);
    for (int n = 2; n <= 8; n++) { snprintf(name, sizeof name, "settings.json" TFP_SETTINGS_BEFORE_SPLIT "-%d", n); disk_put(name, "n", 1); }
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-9", partial, sizeof partial - 1);
    disk_fail_write = TFP_SETTINGS_FILE;
    CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds") && logged("disk full") && !logged("all taken") && !disk_copies);
    tfp_wire_settings_free(&wire);
    disk_fail_write = -1;
    CHECK(start_on_disk() && logged("Converted") && logged("kept as settings.json" TFP_SETTINGS_BEFORE_SPLIT "-9") && !disk_copies);
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-9"), partial) && !strstr(disk_text("settings.json"), "locked_view"));
    tfp_wire_settings_free(&wire);
    /* a free name comes before the copy that holds this file: the copy is still the one kept */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT, "first", 5);
    disk_put("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-3", partial, sizeof partial - 1);
    CHECK(start_on_disk() && logged("Converted") && logged("kept as settings.json" TFP_SETTINGS_BEFORE_SPLIT "-3") && !disk_copies);
    CHECK(!disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT "-2") && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), "first"));
    tfp_wire_settings_free(&wire);
    /* locked_view.json cannot be written while the member's values replace its own: one .replaced, one .before-feel25 */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("locked_view.json", locked_two, sizeof locked_two - 1);
    disk_fail_write = TFP_LOCKED_FILE;
    for (int start = 0; start < 3; start++) {
      CHECK(start_on_disk() && !logged("Converted") && logged("locked_view.json could not be written: disk full"));
      CHECK(disk_copies == 2 && disk_count() == 4 && !strcmp(disk_text("locked_view.json.replaced"), locked_two) &&
            !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), partial) && !strcmp(disk_text("locked_view.json"), locked_two));
      tfp_wire_settings_free(&wire);
    }
    /* a refused settings.json whose replacement cannot be written: one .refused */
    disk_clear();
    disk_put("settings.json", refused, sizeof refused - 1);
    disk_fail_write = TFP_SETTINGS_FILE;
    for (int start = 0; start < 3; start++) {
      CHECK(start_on_disk() && logged("Refused: settings: settings.json: settings.a_later_member: an unknown member"));
      CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
      tfp_wire_settings_save_frame(&wire);
      CHECK(logged("Refused: settings were not saved: disk full") && logged("is now settings.json.refused;"));
      CHECK(disk_copies == 1 && disk_count() == 2 && !strcmp(disk_text("settings.json.refused"), refused) && !strcmp(disk_text("settings.json"), refused));
      tfp_wire_settings_free(&wire);
    }
  }
  { /* ---- locked_view.json from before camera_damping was an option: its default, and the file is not taken for a
     * later build's (no note, no copy at the first write of it) ---- */
    static const char before[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":95,\"kick_strength\":40,\"thrown_kick_strength\":70,\"smooth_eye\":true}}";
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_put("locked_view.json", before, sizeof before - 1);
    CHECK(start_on_disk() && !logged("Refused") && !logged("Note") && !logged("skipped") && tfp_locked_view_camera_damping(&wire) == 60 &&
          tfp_locked_view_fov_degrees(&wire) == 95 && tfp_locked_view_kick_strength(&wire) == 40);
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == 2);       /* no change: nothing touched */
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_camera_damping, false, false, 80));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && !disk_copies && disk_count() == 2 && strstr(disk_text("locked_view.json"), "\"camera_damping\": 80") &&
          strstr(disk_text("locked_view.json"), "\"fov_degrees\": 95"));
    tfp_wire_settings_free(&wire);
    CHECK(start_on_disk() && !logged("Note") && tfp_locked_view_camera_damping(&wire) == 80);
    tfp_wire_settings_free(&wire);
  }
  { /* ---- the same for bodycam (0.6.0-feel.39 and older have no such member): off, which is the Stable camera, with
     * no note and no ".newer" copy; switched on, it is saved and read back ---- */
    static const char before[] = "{\"format\":1,\"locked_view\":{\"camera_damping\":40,\"fov_degrees\":95,\"kick_strength\":40,\"thrown_kick_strength\":70,\"smooth_eye\":true}}";
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_put("locked_view.json", before, sizeof before - 1);
    CHECK(start_on_disk() && !logged("Refused") && !logged("Note") && !logged("skipped") && !tfp_locked_view_bodycam(&wire) &&
          tfp_locked_view_camera_damping(&wire) == 40 && tfp_locked_view_kick_strength(&wire) == 40);
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == 2);       /* no change: nothing touched */
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_bodycam, true, true, 0) && tfp_locked_view_bodycam(&wire));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && !disk_copies && disk_count() == 2 && strstr(disk_text("locked_view.json"), "\"bodycam\": true") &&
          strstr(disk_text("locked_view.json"), "\"camera_damping\": 40") && !logged("Note"));
    tfp_wire_settings_free(&wire);
    CHECK(start_on_disk() && !logged("Note") && !logged("skipped") && tfp_locked_view_bodycam(&wire));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_bodycam, true, false, 0));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 2 && !disk_copies && strstr(disk_text("locked_view.json"), "\"bodycam\": false"));
    tfp_wire_settings_free(&wire);
    CHECK(start_on_disk() && !tfp_locked_view_bodycam(&wire));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- locked_view.json of a later build (names or members this build skips): the first write of it by this build
     * would drop them, so the file as it is is copied to locked_view.json.newer first, once ---- */
    static const char unknown[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":95,\"future_option\":3,\"camera_lock\":false,\"another\":\"x\"}}";
    static const char member[] = "{\"format\":2,\"locked_view\":{\"fov_degrees\":95},\"later_member\":{\"x\":1}}";
    static const char partial[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"locked_view\":{\"fov_degrees\":104}}";
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_put("locked_view.json", unknown, sizeof unknown - 1);
    CHECK(start_on_disk() && !logged("Refused") && logged("skipped: future_option, another"));
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == 2);       /* no change: nothing touched */
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && disk_copies == 1 && disk_text("locked_view.json" TFP_LOCKED_NEWER) && !strcmp(disk_text("locked_view.json" TFP_LOCKED_NEWER), unknown));
    CHECK(strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90") && !strstr(disk_text("locked_view.json"), "future_option"));
    CHECK(logged("Kept: the locked_view.json that names what this build does not know is now locked_view.json" TFP_LOCKED_NEWER "; nothing of it was changed"));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 91));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 2 && disk_copies == 1 && disk_count() == 3);                  /* once */
    tfp_wire_settings_free(&wire);
    CHECK(start_on_disk() && !logged("Note") && tfp_locked_view_fov_degrees(&wire) == 91);
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 92));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 3 && disk_copies == 1 && disk_count() == 3 && !strcmp(disk_text("locked_view.json" TFP_LOCKED_NEWER), unknown));
    tfp_wire_settings_free(&wire);
    /* a member beside "locked_view" that this build does not know: named in the note, and kept the same way; .newer
     * is taken by another file, so -2 */
    disk_put("locked_view.json", member, sizeof member - 1);
    disk_copies = 0;
    CHECK(start_on_disk() && !logged("Refused") && logged("Note: locked view options: locked_view.json names what this build does not know; skipped: later_member"));
    CHECK(tfp_locked_view_fov_degrees(&wire) == 95 && tfp_wire_settings_locked_set(&wire, TFP_LOCKED_kick_strength, false, false, 60));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_copies == 1 && disk_text("locked_view.json" TFP_LOCKED_NEWER "-2") && !strcmp(disk_text("locked_view.json" TFP_LOCKED_NEWER "-2"), member));
    CHECK(!strcmp(disk_text("locked_view.json" TFP_LOCKED_NEWER), unknown) && !strstr(disk_text("locked_view.json"), "later_member") && logged("is now locked_view.json" TFP_LOCKED_NEWER "-2;"));
    tfp_wire_settings_free(&wire);
    /* the copy cannot be made: the save is refused and the file stays; the write cannot be made: the copy is not made
     * again at the next start */
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_put("locked_view.json", unknown, sizeof unknown - 1);
    disk_fail_copy = 1;
    CHECK(start_on_disk() && tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(logged("Refused: locked view options were not saved:") && logged("access denied") && !disk_writes[1] && !strcmp(disk_text("locked_view.json"), unknown));
    disk_fail_copy = 0, disk_fail_write = TFP_LOCKED_FILE;
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 91));
    tfp_wire_settings_save_frame(&wire);
    CHECK(logged("disk full") && disk_copies == 1 && !strcmp(disk_text("locked_view.json" TFP_LOCKED_NEWER), unknown) && !strcmp(disk_text("locked_view.json"), unknown));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 92));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_copies == 1 && disk_count() == 3);
    tfp_wire_settings_free(&wire);
    for (int start = 0; start < 3; start++) {
      CHECK(start_on_disk() && tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
      tfp_wire_settings_save_frame(&wire);
      CHECK(logged("disk full") && disk_copies == 1 && disk_count() == 3 && !strcmp(disk_text("locked_view.json"), unknown));
      tfp_wire_settings_free(&wire);
    }
    disk_fail_write = -1;
    CHECK(start_on_disk() && tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && disk_copies == 1 && disk_count() == 3 && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90"));
    tfp_wire_settings_free(&wire);
    /* such a file whose values settings.json's newer member replaces: its one copy is locked_view.json.replaced */
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("locked_view.json", unknown, sizeof unknown - 1);
    CHECK(start_on_disk() && logged("Converted") && logged("newer") && tfp_locked_view_fov_degrees(&wire) == 104);
    CHECK(disk_copies == 2 && !strcmp(disk_text("locked_view.json.replaced"), unknown) && !disk_text("locked_view.json" TFP_LOCKED_NEWER));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_copies == 2 && !disk_text("locked_view.json" TFP_LOCKED_NEWER));
    tfp_wire_settings_free(&wire);
    /* and when the member says what such a file says, the conversion does not write it: its first write later copies it */
    static const char unknown_same[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":104,\"future_option\":3}}";
    disk_clear();
    disk_put("settings.json", partial, sizeof partial - 1);
    disk_put("locked_view.json", unknown_same, sizeof unknown_same - 1);
    CHECK(start_on_disk() && logged("Converted") && !logged("newer") && disk_copies == 1 && !strcmp(disk_text("locked_view.json"), unknown_same));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_copies == 2 && disk_text("locked_view.json" TFP_LOCKED_NEWER) && !strcmp(disk_text("locked_view.json" TFP_LOCKED_NEWER), unknown_same));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- locked_view.json: an unknown name is skipped with one line; a wrong type refuses that file only, and the
     * refused file is copied aside before the first save replaces it ---- */
    static const char unknown[] = "{\"format\":1,\"locked_view\":{\"fov_degrees\":95,\"future_option\":3,\"camera_lock\":false,\"another\":\"x\"}}";
    static const char wrong[] = "{\"format\":1,\"locked_view\":{\"camera_lock\":false,\"fov_degrees\":true}}";
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_put("locked_view.json", unknown, sizeof unknown - 1);
    CHECK(start_on_disk() && !logged("Refused") && logged("Note: locked view options: locked_view.json names what this build does not know; skipped: future_option, another"));
    int notes = 0;
    for (int k = 0; k < line_count; k++) notes += !strncmp(lines[k], "Note", 4);
    CHECK(notes == 1 && tfp_locked_view_fov_degrees(&wire) == 95 && !tfp_locked_view_camera_lock(&wire) && tfp_locked_view_fov_lock(&wire));
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies);
    tfp_wire_settings_free(&wire);
    disk_put("locked_view.json", wrong, sizeof wrong - 1);
    CHECK(start_on_disk() && logged("Refused: locked view options: locked_view.json: locked_view.fov_degrees: expected a number; they stay at their defaults"));
    CHECK(!logged("Refused: settings:") && logged("Ready: settings imported from settings.json: 18 weapon placement profiles"));
    CHECK(tfp_locked_view_camera_lock(&wire) && tfp_locked_view_fov_degrees(&wire) == 80);   /* nothing of the refused file is taken */
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && !strcmp(disk_text("locked_view.json"), wrong));
    CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && disk_copies == 1 && !strcmp(disk_text("locked_view.json.refused"), wrong) && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90"));
    CHECK(!disk_writes[0] && !strcmp(disk_text("settings.json"), feel19_text) && logged("Kept: the locked_view.json that could not be read is now locked_view.json.refused"));
    tfp_wire_settings_free(&wire);
    /* "locked_view" inside settings.json with things this build cannot read: settings.json loads all the same */
    static const char odd[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"locked_view\":{\"volume\":1,\"camera_lock\":1,\"fov_degrees\":95}}";
    disk_clear();
    disk_put("settings.json", odd, sizeof odd - 1);
    CHECK(start_on_disk() && !logged("Refused") && logged("Note: settings: the \"locked_view\" member held what this build does not read; skipped: volume, camera_lock"));
    CHECK(tfp_locked_view_fov_degrees(&wire) == 95 && tfp_locked_view_camera_lock(&wire) && tfp_wire_settings(&wire)->field[TFP_SET_motion_scale]->number == .5);
    CHECK(logged("Converted") && !strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), odd) && !strstr(disk_text("settings.json"), "locked_view"));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- a settings.json that was refused is copied to settings.json.refused before the first save of the session ---- */
    static const char later[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"menu\":{},\"a_later_member\":{\"x\":1}}";
    disk_clear();
    disk_put("settings.json", later, sizeof later - 1);
    CHECK(start_on_disk() && logged("Refused: settings: settings.json: settings.a_later_member: an unknown member; retail's defaults are used"));
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == 1 && !strcmp(disk_text("settings.json"), later)); /* no change: nothing touched */
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_copies == 1 && !strcmp(disk_text("settings.json.refused"), later) && strstr(disk_text("settings.json"), "\"motion_strength\": 135"));
    CHECK(logged("Kept: the settings.json that could not be read is now settings.json.refused") && logged("Saved: settings"));
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 140));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 2 && disk_copies == 1 && !strcmp(disk_text("settings.json.refused"), later));   /* once a session */
    tfp_wire_settings_free(&wire);
    /* a copy of that name is already there: it is never written over; the next free name takes this one */
    disk_put("settings.json", "{\"format\":7}", 12);
    disk_writes[0] = disk_copies = 0;
    CHECK(start_on_disk() && logged("Refused: settings: settings.json: format: expected 1"));
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 150));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_copies == 1 && !strcmp(disk_text("settings.json.refused"), later) && !strcmp(disk_text("settings.json.refused-2"), "{\"format\":7}"));
    CHECK(logged("is now settings.json.refused-2"));
    tfp_wire_settings_free(&wire);
    /* the copy cannot be made: the save fails, the refused file stays, and the next change tries again */
    disk_clear();
    disk_put("settings.json", later, sizeof later - 1);
    disk_fail_copy = 1;
    CHECK(start_on_disk());
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !strcmp(disk_text("settings.json"), later) && tfp_wire_settings_unsaved(&wire) && disk_count() == 1);
    CHECK(logged("Refused: settings were not saved: the settings.json that could not be read could not be copied to settings.json.refused first (access denied)"));
    disk_fail_copy = 0;
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 140));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && !strcmp(disk_text("settings.json.refused"), later) && !tfp_wire_settings_unsaved(&wire));
    tfp_wire_settings_free(&wire);
    /* all ten names taken: the save fails and none of them is written over */
    disk_clear();
    disk_put("settings.json", later, sizeof later - 1);
    disk_put("settings.json.refused", "0", 1);
    for (int k = 2; k <= 9; k++) { char name[40]; snprintf(name, sizeof name, "settings.json.refused-%d", k); disk_put(name, "0", 1); }
    CHECK(start_on_disk() && tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_copies && !strcmp(disk_text("settings.json"), later) && logged("settings.json.refused to .refused-9 are all taken"));
    CHECK(!strcmp(disk_text("settings.json.refused"), "0") && !strcmp(disk_text("settings.json.refused-9"), "0"));
    tfp_wire_settings_free(&wire);
    /* no file at all (the first run): the defaults, and the first save needs no copy */
    disk_clear();
    CHECK(start_on_disk() && logged("no settings file") && tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && !disk_copies && disk_count() == 1 && !logged("Kept:"));
    tfp_wire_settings_free(&wire);
  }
  { /* ---- a file that is there but gave no bytes at the start (it could not be opened) was never read: the first
     * write of it first copies it to <name>.unread, so it is not replaced by what was loaded from elsewhere ---- */
    static const char beside[] = "{\"format\":1,\"first_person\":{\"motion_scale\":0.5},\"menu\":{}}";
    static const char locked_file[] = "{\"fov_degrees\": 77}";
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_unreadable[0] = true, disk_beside = beside;
    CHECK(start_on_disk() && !logged("Refused") && !logged("Converted"));
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_writes[1] && !disk_copies && disk_count() == 1);                         /* no change: nothing touched */
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_copies == 1 && disk_text("settings.json.unread") && !strcmp(disk_text("settings.json.unread"), feel19_text));
    CHECK(strstr(disk_text("settings.json"), "\"motion_strength\": 135") && logged("Saved: settings"));
    CHECK(logged("Kept: the settings.json that could not be opened at the start is now settings.json.unread"));
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 140));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 2 && disk_copies == 1 && !strcmp(disk_text("settings.json.unread"), feel19_text)); /* once a session */
    tfp_wire_settings_free(&wire);
    /* the name is taken: never written over, the next free one is used */
    disk_put("settings.json", "second", 6);
    disk_writes[0] = disk_copies = 0;
    CHECK(start_on_disk() && tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 150));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && !strcmp(disk_text("settings.json.unread"), feel19_text) && !strcmp(disk_text("settings.json.unread-2"), "second"));
    tfp_wire_settings_free(&wire);
    /* the copy cannot be made: the save fails and the file stays; the next change tries again */
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_unreadable[0] = true, disk_beside = beside, disk_fail_copy = 1;
    CHECK(start_on_disk() && tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    for (int k = 0; k < 3; k++) tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && disk_count() == 1 && !strcmp(disk_text("settings.json"), feel19_text) && tfp_wire_settings_unsaved(&wire));
    CHECK(logged("Refused: settings were not saved: the settings.json that could not be opened at the start could not be copied to settings.json.unread first (access denied)"));
    disk_fail_copy = 0;
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 140));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && !strcmp(disk_text("settings.json.unread"), feel19_text) && !tfp_wire_settings_unsaved(&wire));
    tfp_wire_settings_free(&wire);
    /* an unopened settings.json and no file beside the DLL either: the defaults, and still .unread before the save */
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_unreadable[0] = true;
    CHECK(start_on_disk() && tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_copies == 1 && !strcmp(disk_text("settings.json.unread"), feel19_text) && !disk_text("settings.json.refused"));
    tfp_wire_settings_free(&wire);
    /* locked_view.json is there and could not be opened: a changed option does not replace it without a copy */
    disk_clear();
    disk_put("settings.json", feel19_text, strlen(feel19_text));
    disk_put("locked_view.json", locked_file, sizeof locked_file - 1);
    disk_unreadable[1] = true;
    CHECK(start_on_disk() && !logged("Refused") && tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[1] == 1 && !disk_writes[0] && disk_copies == 1 && !strcmp(disk_text("locked_view.json.unread"), locked_file));
    CHECK(strstr(disk_text("locked_view.json"), "\"fov_degrees\": 90") && logged("Kept: the locked_view.json that could not be opened at the start is now locked_view.json.unread"));
    tfp_wire_settings_free(&wire);
    /* the same while settings.json is converted at the start: the conversion's own write of locked_view.json copies it first */
    disk_clear();
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    disk_put("locked_view.json", locked_file, sizeof locked_file - 1);
    disk_unreadable[1] = true;
    CHECK(start_on_disk() && logged("Converted") && locked_as_feel24());
    CHECK(!strcmp(disk_text("locked_view.json.unread"), locked_file) && strstr(disk_text("locked_view.json"), "\"fov_degrees\": 104"));
    CHECK(!strcmp(disk_text("settings.json" TFP_SETTINGS_BEFORE_SPLIT), feel24_text) && !strstr(disk_text("settings.json"), "locked_view"));
    tfp_wire_settings_free(&wire);
    /* and when that copy cannot be made the conversion is refused with every file as it was */
    disk_clear();
    disk_put("settings.json", feel24_text, strlen(feel24_text));
    disk_put("locked_view.json", locked_file, sizeof locked_file - 1);
    disk_unreadable[1] = true, disk_fail_copy = 1;
    CHECK(start_on_disk() && !logged("Converted") && logged("Refused: settings.json still holds the locked view's options"));
    CHECK(disk_count() == 2 && !strcmp(disk_text("locked_view.json"), locked_file) && !strcmp(disk_text("settings.json"), feel24_text));
    tfp_wire_settings_free(&wire);
    /* no file there at all: nothing to copy, no line */
    disk_clear();
    disk_beside = beside;
    CHECK(start_on_disk() && tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135) &&
          tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_writes[1] == 1 && !disk_copies && disk_count() == 2 && !logged("Kept:"));
    tfp_wire_settings_free(&wire);
  }
  disk_clear();
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && (!strcmp(argv[1], "--dump") || !strcmp(argv[1], "--write"))) return file_mode(argv[1], argv[2]);
  static TfpSettings s, defaults;
  static char written[1 << 16], expected[1 << 16];
  char why[200];

  /* ---- the JSON reader and the loader: what is refused, and that a refusal leaves retail's defaults ---- */
  CHECK(tfp_settings_defaults(&defaults));
  size_t defaults_length = tfp_settings_write(&defaults, expected, sizeof expected);
  CHECK(defaults_length > 400 && defaults_length < sizeof expected);
  static const struct { const char *json, *why; } refused[] = {
    {"", "no settings file"}, {"   ", "a value is missing"}, {"{", "a member needs a name"}, {"[1,]", "an unknown word"},
    {"{\"a\":1,}", "a member needs a name"}, {"{\"a\":1 \"b\":2}", "a ',' or '}' is missing"}, {"{\"a\":1,\"a\":2}", "twice in one object"},
    {"{\"a\" 1}", "a ':' is missing"}, {"{a:1}", "a member needs a name"}, {"[01]", "must not start with 0"}, {"[1.]", "digit after the point"},
    {"[1e]", "digit in its exponent"}, {"[-]", "a number needs a digit"}, {"[.5]", "an unknown word"}, {"[+1]", "an unknown word"},
    {"[tru]", "an unknown word"}, {"[True]", "an unknown word"}, {"[NaN]", "an unknown word"}, {"[1e999]", "out of range"},
    {"[\"abc]", "a string is not closed"}, {"[\"a\\x\"]", "an unknown escape"}, {"[\"\\ud800\"]", "a lone high surrogate"},
    {"[\"\\udc00\"]", "a lone low surrogate"}, {"[\"\\u12g4\"]", "four hex digits"}, {"[\"\\u0000\"]", "a NUL character"},
    {"[\"a\tb\"]", "a control character"}, {"[\"\xC3\xA9\"]", "outside ASCII"}, {"{} x", "text after the end"}, {"[1] [2]", "text after the end"},
    {"\xEF\xBB\xBF{}", "byte order mark"}, {"[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]", "nested deeper"}, {"[1", "a ',' or ']' is missing"},
    {"'a'", "an unknown word"}, {"[1 // note\n]", "a ',' or ']' is missing"},
    /* a JSON document of the wrong shape */
    {"[]", "the document must be an object"}, {"{}", "format: expected 1"}, {"{\"format\":2,\"first_person\":{}}", "format: expected 1"},
    {"{\"format\":\"1\",\"first_person\":{}}", "format: expected 1"}, {"{\"format\":1}", "first_person: expected an object"},
    {"{\"format\":1,\"first_person\":[]}", "first_person: expected an object"}, {"{\"format\":1,\"first_person\":{},\"extra\":1}", "an unknown member"},
    {"{\"format\":1,\"first_person\":{\"speed\":1}}", "an unknown setting"},
    {"{\"format\":1,\"first_person\":{\"motion_enabled\":1}}", "expected true or false"},
    {"{\"format\":1,\"first_person\":{\"motion_scale\":true}}", "expected a number"},
    {"{\"format\":1,\"first_person\":{\"placement\":3}}", "expected an object"},
    {"{\"format\":1,\"first_person\":{\"placement\":{\"x\":\"0.1\"}}}", "must be a number"},
    {"{\"format\":1,\"first_person\":{\"placement\":{\"w\":1}}}", "x, y, z, yaw, pitch, roll only"},
    {"{\"format\":1,\"first_person\":{\"weapon_placements\":{\"k\":{\"x\":null}}}}", "must be a number"},
    {"{\"format\":1,\"first_person\":{\"weapon_placements\":{\"0123456789abcdef0123456789abcdef\":{}}}}", "1 to 31 bytes"},
    {"{\"format\":1,\"first_person\":{\"weapon_placements\":{\"\":{}}}}", "1 to 31 bytes"},
    {"{\"format\":1,\"first_person\":{\"action_placements\":{\"k\":{\"fly\":{}}}}}", "an action retail does not know"},
    {"{\"format\":1,\"first_person\":{\"action_placements\":{\"k\":7}}}", "expected an object of actions"},
    {"{\"format\":1,\"first_person\":{\"action_state_mapping\":{\"r\":{\"1:2\":5}}}}", "expected an action name"},
    {"{\"format\":1,\"first_person\":{\"action_state_mapping\":{\"r\":3}}}", "expected an object of states"},
    {"{\"format\":1,\"first_person\":{\"aim_markers\":{\"k\":false}}}", "expected true"},
    {"{\"format\":1,\"first_person\":{\"near_clip_profiles\":{\"k\":{\"hip\":10}}}}", "hip_cm and aim_cm only"},
    {"{\"format\":1,\"first_person\":{\"near_clip_profiles\":{\"k\":{\"hip_cm\":\"10\"}}}}", "must be a number"},
    {"{\"format\":1,\"first_person\":{},\"menu\":{\"volume\":1}}", "an unknown control"},
    {"{\"format\":1,\"first_person\":{},\"menu\":{\"show_torso\":1}}", "expected true or false"},
    {"{\"format\":1,\"first_person\":{},\"menu\":{\"near_clip_cm\":true}}", "expected a number"},
    {"{\"format\":1,\"first_person\":{},\"menu\":[]}", "menu: expected an object"},
  };
  for (size_t i = 0; i < COUNT(refused); i++) {
    bool loaded = tfp_settings_load(&s, refused[i].json, strlen(refused[i].json), why, sizeof why);
    size_t n = tfp_settings_write(&s, written, sizeof written);
    checks++;
    if (loaded || !strstr(why, refused[i].why) || s.from_file || n != defaults_length || strcmp(written, expected)) {
      printf("wire settings: FAILED refusal %zu: %s -> %s \"%s\" (expected \"%s\")\n", i, refused[i].json, loaded ? "loaded" : "refused", why, refused[i].why);
      return 1;
    }
    tfp_settings_free(&s);
  }
  CHECK(!tfp_settings_load(&s, NULL, 0, why, sizeof why) && !strcmp(why, "no settings file") && s.field[TFP_SET_placement]->placement.x == .15);
  tfp_settings_free(&s);
  /* what is accepted: every escape, both surrogates, the number forms, white space, an empty first_person (every field nil) */
  static const char accepted[] =
      " {\r\n\t\"format\" : 1 , \"source\" : {\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\\u0041\\u00e9\\u20ac\\ud83d\\ude00\": [1, -0, 0.5, -1.25e-3, 2E+2, 1e0, null, true, false, {}, []]},"
      " \"first_person\": {\"motion_scale\": 1.5e0, \"placement\": {\"x\": -0.0, \"roll\": 9e1}}, \"unread\": {\"anything\": [{\"deep\": [[[[[[[1]]]]]]]}]} } \n";
  CHECK(tfp_settings_load(&s, accepted, sizeof accepted - 1, why, sizeof why) && !*why && s.from_file);
  CHECK(s.field[TFP_SET_motion_scale]->number == 1.5 && s.field[TFP_SET_placement]->placement.present == (TFP_PLACE_X | TFP_PLACE_ROLL));
  CHECK(s.field[TFP_SET_placement]->placement.roll == 90 && !s.field[TFP_SET_action_placements] && !s.field[TFP_SET_aim_markers] &&
        tfp_settings_menu_number(&s, TFP_MENU_near_clip_cm) == 15 && tfp_settings_menu_flag(&s, TFP_MENU_fird_person_arms));
  tfp_settings_free(&s);
  CHECK(!tfp_settings_load(&s, accepted, sizeof accepted - 4, why, sizeof why)); /* cut short: text is never read past n */
  tfp_settings_free(&s);

  /* ---- the store ---- */
  CHECK(tfp_settings_field("placement") == TFP_SET_placement && tfp_settings_field("grab_offsets") == TFP_SET_grab_offsets && tfp_settings_field("x") < 0);
  CHECK(tfp_settings_menu("toggle_key") == TFP_MENU_toggle_key && tfp_settings_menu("first_person_toggle_key") < 0);
  CHECK(defaults.field[TFP_SET_placement]->placement.x == .15 && defaults.field[TFP_SET_placement]->placement.z == -.12 &&
        defaults.field[TFP_SET_placement]->placement.present == TFP_PLACE_XYZ && defaults.field[TFP_SET_fird_head_camera]->placement.z == .04);
  CHECK(tfp_settings_flag(&defaults, TFP_SET_motion_enabled) == 1 && defaults.field[TFP_SET_motion_scale]->number == 1 &&
        tfp_settings_flag(&defaults, TFP_SET_camera_kick_enabled) == 1 && tfp_settings_flag(&defaults, TFP_SET_show_aim_markers) == -1 &&
        tfp_settings_flag(&defaults, TFP_SET_family_placement_defaults) == -1 && !defaults.field[TFP_SET_near_clip_profiles]);
  CHECK(defaults.field[TFP_SET_weapon_placements]->count == 0 && defaults.field[TFP_SET_action_placements]->count == 0 &&
        defaults.field[TFP_SET_action_state_mapping]->count == 0 && defaults.field[TFP_SET_aim_markers]->count == 0 && !defaults.from_file);
  const char *problem = NULL;
  const TfpSettingsValue *old_placement = defaults.field[TFP_SET_placement];
  TfpActionPlacement moved = {.1, .2, .3, 4, 5, 6, TFP_PLACE_ALL};
  const TfpSettingsValue *made = tfp_settings_new_placement(&defaults, &moved), *flag = tfp_settings_new_bool(&defaults, true);
  CHECK(made && flag && tfp_settings_owned(&defaults, made) == made && !tfp_settings_owned(&defaults, &moved));
  CHECK(!tfp_settings_set(&defaults, TFP_SET_placement, flag, &problem) && problem && strstr(problem, "cannot store"));
  CHECK(!tfp_settings_set(&defaults, TFP_SET_motion_enabled, made, &problem) && defaults.writes == 0);
  TfpSettingsValue stranger = *made;
  CHECK(!tfp_settings_set(&defaults, TFP_SET_placement, &stranger, &problem) && strstr(problem, "not made by"));
  CHECK(tfp_settings_set(&defaults, TFP_SET_placement, made, &problem) && !problem && defaults.writes == 1);
  CHECK(defaults.field[TFP_SET_placement]->placement.yaw == 4 && old_placement->placement.x == .15); /* the old table is unchanged */
  CHECK(tfp_settings_set(&defaults, TFP_SET_placement, made, &problem) && defaults.writes == 1);   /* the same value: no write */
  CHECK(tfp_settings_set(&defaults, TFP_SET_placement, NULL, &problem) && !defaults.field[TFP_SET_placement] && defaults.writes == 2);
  const char *unsorted[3] = {"ffff000000000000", "0000000000000001", "8000000000000000"}, *twice[2] = {"a", "a"};
  const TfpSettingsValue *markers = tfp_settings_new_markers(&defaults, unsorted, 3);
  CHECK(markers && markers->count == 3 && !strcmp(markers->ids[0], "0000000000000001") && !strcmp(markers->ids[2], "ffff000000000000"));
  CHECK(tfp_settings_row(markers, "8000000000000000") == 1 && tfp_settings_row(markers, "8") < 0 && tfp_settings_row(NULL, "a") < 0);
  CHECK(!tfp_settings_new_markers(&defaults, twice, 2));
  const char *long_key[1] = {"0123456789abcdef0123456789abcdef"};
  CHECK(!tfp_settings_new_markers(&defaults, long_key, 1) && !tfp_settings_new_empty(&defaults, TFP_SETTINGS_BOOL));
  CHECK(!tfp_settings_set_menu(&defaults, TFP_MENU_show_torso, false, false, 1) && tfp_settings_set_menu(&defaults, TFP_MENU_show_torso, true, true, 0));
  tfp_settings_free(&defaults);

  /* ---- written and read back: the same store ---- */
  CHECK(tfp_settings_load(&s, settings_user_json, sizeof settings_user_json - 1, why, sizeof why) && s.from_file);
  size_t user_length = tfp_settings_write(&s, expected, sizeof expected);
  CHECK(user_length < sizeof expected && tfp_settings_write(&s, NULL, 0) == user_length);
  char small[32];
  CHECK(tfp_settings_write(&s, small, sizeof small) == user_length && strlen(small) == sizeof small - 1 && !strncmp(small, expected, 31));
  tfp_settings_free(&s);
  CHECK(tfp_settings_load(&s, expected, user_length, why, sizeof why));
  CHECK(tfp_settings_write(&s, written, sizeof written) == user_length && !strcmp(written, expected));
  CHECK(s.field[TFP_SET_action_placements]->count == 19 && s.field[TFP_SET_near_clip_profiles]->count == 1 && s.field[TFP_SET_grab_offsets]->count == 2);
  tfp_settings_free(&s);

  /* ---- retail's answers, through the binder ---- */
  new_wire();
  CHECK(!tfp_wire_settings(&wire) && !wire.host.mod_set && !wire.host.menu_near_options);
  wire.placement_decode = foreign_decode; /* a decoder that was there first stays in the chain */
  CHECK(tfp_wire_settings_bind(&wire, settings_defaults_json, sizeof settings_defaults_json - 1, "settings.json"));
  CHECK(logged("Ready: settings imported from settings.json: 0 weapon placement profiles") && !logged("Refused"));
  CHECK(tfp_wire_settings(&wire) && tfp_wire_settings(&wire)->from_file && tfp_wire_settings_catalogue(&wire)->count == 105);
  CHECK(wire.host.mod_set && wire.host.menu_near_options && wire.host.menu_motion_options && wire.host.menu_show_torso && wire.host.menu_fird_person_arms);
  if (replay("defaults", settings_defaults_ops, COUNT(settings_defaults_ops))) return 1;
  tfp_wire_settings_free(&wire);
  CHECK(!tfp_wire_settings(&wire) && wire.placement_decode == foreign_decode);

  /* no file at all, and a broken one: retail's defaults, and the log says why */
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, NULL, 0, "settings.json"));
  CHECK(logged("Refused: settings: settings.json: no settings file; retail's defaults are used") && logged("Ready: settings are retail's defaults: 0 weapon"));
  CHECK(!tfp_wire_settings(&wire)->from_file);
  if (replay("no file", settings_defaults_ops, COUNT(settings_defaults_ops))) return 1;
  tfp_wire_settings_free(&wire);
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, "{\"format\":1,", 12, NULL) && logged("Refused: settings: not JSON: line 1:") && logged("Ready: settings are retail's defaults"));
  if (replay("broken file", settings_defaults_ops, COUNT(settings_defaults_ops))) return 1;
  tfp_wire_settings_free(&wire);

  /* the user's file */
  new_wire();
  wire.placement_decode = foreign_decode;
  CHECK(tfp_wire_settings_bind(&wire, settings_user_json, sizeof settings_user_json - 1, "settings.json"));
  CHECK(logged("Ready: settings imported from settings.json: 18 weapon placement profiles (") && logged("family defaults on") &&
        logged("1 weapon clipping profiles") && logged("near clip hip 0.00 m aim 0.00 m") && logged("sprint 1.00") && !logged("Refused"));
  {
    TfpRtHost *h = &wire.host;
    TfpSettings *store = tfp_wire_settings(&wire);
    TfpActionPlacement p, q;
    V a, b, none = tfp_wire_nil();
    /* placement refs: each of the last 64 reads back; older ones are overwritten; foreign refs go down the chain */
    V refs[70];
    for (int i = 0; i < 70; i++) {
      TfpActionPlacement value = {i * .001, -i * .002, i * .003, i, -i, i * .5, i % 2 ? TFP_PLACE_ALL : TFP_PLACE_XYZ};
      refs[i] = tfp_wire_settings_placement(&wire, &value);
      CHECK(refs[i].kind == TFP_RT_REF);
    }
    for (int i = 6; i < 70; i++) {
      TfpOpsPlacement ops;
      CHECK(tfp_wire_placement_of(&wire, refs[i], &ops) && ops.x == i * .001 && ops.y == -i * .002 && ops.z == i * .003 && ops.yaw == i &&
            ops.pitch == -i && ops.roll == i * .5 && ops.present == (i % 2 ? 63u : 7u));
    }
    CHECK(refs[0].ref == refs[64].ref && tfp_wire_settings_placement_of(&wire, refs[0], &p) && p.yaw == 64);
    V shipped = tfp_wire_ref(tfp_rt_default_placement), baseline = tfp_wire_ref(tfp_rt_camera_baseline), empty = tfp_wire_ref(&tfp_rt_empty_table);
    V chained = tfp_wire_ref((const void *)foreign_decode), unknown = tfp_wire_ref(&checks), inside = tfp_wire_ref((const char *)refs[10].ref + 1);
    CHECK(tfp_wire_settings_placement_of(&wire, shipped, &p) && p.x == .15 && p.z == -.12 && p.present == TFP_PLACE_XYZ);
    CHECK(tfp_wire_settings_placement_of(&wire, baseline, &p) && p.x == 0 && p.z == .04 && p.present == TFP_PLACE_XYZ);
    CHECK(tfp_wire_settings_placement_of(&wire, chained, &p) && p.x == .25 && p.present == TFP_PLACE_X);
    CHECK(!tfp_wire_settings_placement_of(&wire, unknown, &p) && !tfp_wire_settings_placement_of(&wire, empty, &p) &&
          !tfp_wire_settings_placement_of(&wire, inside, &p) && !tfp_wire_settings_placement_of(&wire, none, &p) &&
          !tfp_wire_settings_placement_of(&wire, tfp_wire_number(1), &p));
    CHECK(tfp_wire_settings_placement(&wire, NULL).kind == TFP_RT_NIL);
    /* a saved placement table is a placement too, and stays what it was after the field is written */
    V saved = h->config_field(h, "placement");
    CHECK(tfp_wire_settings_placement_of(&wire, saved, &p) && p.x == .15 && p.present == TFP_PLACE_XYZ);
    CHECK(h->mod_set(h, "placement", refs[69], &a, &b) && a.kind == TFP_RT_TRUE && b.kind == TFP_RT_NIL);
    CHECK(tfp_wire_settings_placement_of(&wire, saved, &q) && !memcmp(&p, &q, sizeof p));
    CHECK(tfp_wire_settings_placement_of(&wire, h->config_field(h, "placement"), &q) && q.x == .069 && q.roll == 34.5);
    CHECK(h->mod_set(h, "placement", saved, &a, &b) && a.kind == TFP_RT_TRUE && h->config_field(h, "placement").ref == saved.ref);
    /* mod.set: what this store cannot hold is refused with a text, and nothing changes */
    uint64_t writes = store->writes;
    CHECK(h->mod_set(h, "speed", tfp_wire_number(1), &a, &b) && a.kind == TFP_RT_FALSE && text_is(b, "Unknown first-person setting"));
    CHECK(h->mod_set(h, "placement", tfp_wire_bool(true), &a, &b) && a.kind == TFP_RT_FALSE && b.kind == TFP_RT_TEXT);
    CHECK(h->mod_set(h, "motion_enabled", tfp_wire_number(1), &a, &b) && a.kind == TFP_RT_FALSE);
    CHECK(h->mod_set(h, "motion_scale", empty, &a, &b) && a.kind == TFP_RT_FALSE);
    CHECK(h->mod_set(h, "action_placements", unknown, &a, &b) && a.kind == TFP_RT_FALSE);
    CHECK(h->mod_set(h, "action_placements", h->config_field(h, "weapon_placements"), &a, &b) && a.kind == TFP_RT_FALSE);
    CHECK(h->mod_set(h, "aim_markers", text("x"), &a, &b) && a.kind == TFP_RT_FALSE && store->writes == writes);
    CHECK(h->mod_set(h, "fird_head_camera", baseline, &a, &b) && a.kind == TFP_RT_TRUE &&
          tfp_wire_settings_placement_of(&wire, h->config_field(h, "fird_head_camera"), &p) && p.z == .04);
    CHECK(h->mod_set(h, "near_clip_profiles", none, &a, &b) && a.kind == TFP_RT_TRUE && h->config_field(h, "near_clip_profiles").kind == TFP_RT_NIL);
    /* the aim-marker binder's table */
    const char *weapons[2] = {"5663fbf6d81eee27", "12a8953da33a06ce"};
    V table = tfp_wire_settings_markers(&wire, weapons, 2);
    CHECK(table.kind == TFP_RT_REF && h->mod_set(h, "aim_markers", table, &a, &b) && a.kind == TFP_RT_TRUE);
    const TfpSettingsValue *now = tfp_wire_settings_value_of(&wire, h->config_field(h, "aim_markers"));
    CHECK(now && now->kind == TFP_SETTINGS_MARKERS && now->count == 2 && !strcmp(now->ids[0], "12a8953da33a06ce"));
    CHECK(h->mod_set(h, "show_aim_markers", tfp_wire_bool(false), &a, &b) && h->config_field(h, "show_aim_markers").kind == TFP_RT_FALSE);
    /* fields and tables retail hands on by identity */
    CHECK(h->config_table(h).kind == TFP_RT_REF && h->config_field(h, "nothing").kind == TFP_RT_NIL && h->config_field(h, "camera_kick_enabled").kind == TFP_RT_TRUE);
    CHECK(h->actions_actions(h).ref == tfp_wire_settings_actions && !strcmp(tfp_wire_settings_actions[8][0], "dive") &&
          !strcmp(tfp_wire_settings_actions[1][1], "Normal aim"));
    for (int action = 0; action < TFP_ACT_COUNT; action++)
      CHECK(!strcmp(tfp_wire_settings_actions[action][0], tfp_action_name((TfpAct)action)) &&
            !strcmp(tfp_wire_settings_actions[action][1], tfp_action_label((TfpAct)action)));
    CHECK(h->actions_observed(h, text("crouch")).kind == TFP_RT_TRUE && h->actions_observed(h, text("idle")).kind == TFP_RT_NIL &&
          h->actions_observed(h, none).kind == TFP_RT_NIL && h->actions_observed(h, tfp_wire_number(3)).kind == TFP_RT_NIL);
    CHECK(tfp_wire_settings_mapping_of(&wire, h->config_field(h, "action_state_mapping")) && !tfp_wire_settings_mapping_of(&wire, saved));
    /* a key that is not text is no key (retail: a table lookup that finds nothing) */
    CHECK(h->actions_resolve(h, tfp_wire_number(5), none, &a, &b) && tfp_wire_settings_placement_of(&wire, a, &p) && b.kind == TFP_RT_FALSE);
    CHECK(!h->actions_resolve(h, none, tfp_wire_number(5), &a, &b) && !strcmp(h->raised, "Unknown placement action"));
    CHECK(!h->actions_saved(h, tfp_wire_number(5), text("idle"), shipped, none, &a) && !strcmp(h->raised, "Invalid weapon identity"));
    CHECK(!h->actions_saved(h, text("5663fbf6d81eee27"), text("idle"), unknown, none, &a) && !strcmp(h->raised, "Invalid action placement"));
  }
  tfp_wire_settings_free(&wire);
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, settings_user_json, sizeof settings_user_json - 1, "settings.json"));
  if (replay("user", settings_user_ops, COUNT(settings_user_ops))) return 1;
  tfp_wire_settings_free(&wire);
  /* a made-up file: older placements, a mapping, markers, clipping profiles retail ignores, placements out of bounds */
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, settings_synthetic_json, sizeof settings_synthetic_json - 1, "settings.json") && !logged("Refused"));
  CHECK(tfp_wire_settings(&wire)->field[TFP_SET_action_state_mapping]->count == 3 && tfp_wire_settings(&wire)->field[TFP_SET_weapon_placements]->count == 4);
  if (replay("synthetic", settings_synthetic_ops, COUNT(settings_synthetic_ops))) return 1;
  tfp_wire_settings_free(&wire);

  /* ---- inside the runtime: enable, the entry placement, the near-clip reader, the weapon line ---- */
  new_wire();
  CHECK(tfp_wire_settings_bind(&wire, settings_user_json, sizeof settings_user_json - 1, "settings.json"));
  {
    V r[30];
    TfpNearOptions options;
    CHECK(tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK && logged("Idle. F11 toggles True First Person"));
    /* rt:742 assert(controller:set_placement(saved_placement())): no weapon yet, so the global placement */
    CHECK(wire.ops.has_pose_placement && wire.ops.pose_placement.x == .15 && wire.ops.pose_placement.y == 0 && wire.ops.pose_placement.z == -.12);
    for (int i = 0; i < 3; i++) { now_seconds += 1 / 60.0; tfp_wire_frame(&wire, NULL); tfp_wire_settings_frame(&wire); }
    CHECK(!logged("Refused") && !logged("Weapon:"));
    wire.near_io.options(wire.near_io.user, &options); /* no weapon: the user's global 5 cm is the game's own distance */
    CHECK(options.is_table && options.hip_is_number && options.aim_is_number && options.hip == 0 && options.aim == 0);
    /* a running lease on the weapon that has a saved clipping profile (hip 10 cm, aim 5 cm) */
    TfpWireLease *lease = calloc(1, sizeof *lease);
    CHECK(lease != NULL);
    lease->ops.origin.full = true, lease->ops.origin.weapon_unit = 78, lease->ops.origin.body = 11;
    wire.controller.lease = &lease->view, wire.controller.phase = TFP_PHASE_RUNNING;
    wire.ops.has_placement_weapon_key = true;
    snprintf(wire.ops.placement_weapon_key, sizeof wire.ops.placement_weapon_key, "%s", "5663fbf6d81eee27");
    CHECK(tfp_wire_call(&wire, TFP_RT_E_current_weapon, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_TRUE && text_is(r[1], "5663fbf6d81eee27"));
    wire.near_io.options(wire.near_io.user, &options);
    CHECK(options.is_table && options.hip == .1 && options.aim == 0);
    CHECK(tfp_settings_set_menu(tfp_wire_settings(&wire), TFP_MENU_near_clip_cm, false, false, 20));
    snprintf(wire.ops.placement_weapon_key, sizeof wire.ops.placement_weapon_key, "%s", "12a8953da33a06ce");
    wire.near_io.options(wire.near_io.user, &options); /* a weapon without a profile: the global distance */
    CHECK(options.is_table && options.hip == .2 && options.aim == 0);
    /* the placement the backend asks for on entry (backend.placement_resolver, rt:739) is the weapon's saved idle one */
    TfpOpsPlacement resolved;
    TfpOpsText raised;
    CHECK(wire.ops_host.placement_resolver(wire.ops_host.user, "12a8953da33a06ce", &resolved, &raised) == TFP_OPS_TRUE);
    CHECK(resolved.x == 0.080729166666667 && resolved.y == .15 && resolved.z == -0.00027777777777775 && resolved.present == 63);
    tfp_wire_settings_frame(&wire);
    const char *line = logged("Weapon: key=12a8953da33a06ce");
    CHECK(line && strstr(line, "placement=imported(own) x=0.0807 y=0.1500 z=-0.0003 yaw=0.00 pitch=0.00 roll=0.00") && strstr(line, "near=global hip=0.20m"));
    int before = line_count;
    tfp_wire_settings_frame(&wire);
    CHECK(line_count == before); /* the same weapon: no second line */
    snprintf(wire.ops.placement_weapon_key, sizeof wire.ops.placement_weapon_key, "%s", "5663fbf6d81eee27");
    tfp_wire_settings_frame(&wire);
    line = logged("Weapon: key=5663fbf6d81eee27");
    CHECK(line && strstr(line, "placement=imported(") && strstr(line, "near=imported(own) hip=0.10m aim=0.00m"));
    snprintf(wire.ops.placement_weapon_key, sizeof wire.ops.placement_weapon_key, "%s", "b2b69248b51ea9a8"); /* AR-11: in the catalogue, nothing saved */
    tfp_wire_settings_frame(&wire);
    line = logged("Weapon: key=b2b69248b51ea9a8");
    CHECK(line && strstr(line, "name=\"AR-11 Arbitrator\" family=rifle"));
    CHECK(tfp_wire_settings_weapon_name(&wire) && !strcmp(tfp_wire_settings_weapon_name(&wire), "AR-11 Arbitrator")); /* round 20: the Aim: line's name */
    char source[48];
    CHECK(!strcmp(tfp_wire_settings_source(&wire, "0123456789abcdef", TFP_ACT_ADS, source, sizeof source), "ads default") ||
          !strncmp(source, "imported(__all)", 15));
    wire.controller.lease = NULL, wire.controller.phase = TFP_PHASE_IDLE, wire.ops.has_placement_weapon_key = false;
    free(lease);
    CHECK(tfp_wire_call(&wire, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_TRUE);
  }
  tfp_wire_settings_free(&wire);
  { /* ---- saving: one write per change, on the edge; the text loads back to the same store ---- */
    new_wire();
    CHECK(tfp_wire_settings_bind(&wire, settings_user_json, sizeof settings_user_json - 1, "settings.json"));
    TfpSettings *s = tfp_wire_settings(&wire);
    CHECK(!tfp_wire_settings_unsaved(&wire));
    tfp_wire_settings_save_frame(&wire);                       /* no saver yet: nothing happens */
    saves = 0, save_fails = false;
    tfp_wire_settings_disk(&wire, &test_disk, false);
    for (int i = 0; i < 5; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(saves == 0 && !tfp_wire_settings_unsaved(&wire));    /* nothing changed: never a write */
    TfpActionPlacement moved = {.present = 63, .x = .11, .y = .02, .z = -.03, .yaw = 1, .pitch = 2, .roll = 3};
    const char *why = NULL;
    CHECK(tfp_settings_set(s, TFP_SET_placement, tfp_settings_new_placement(s, &moved), &why));
    CHECK(tfp_wire_settings_unsaved(&wire));
    for (int i = 0; i < 5; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(saves == 1 && !tfp_wire_settings_unsaved(&wire) && logged("Saved: settings"));
    TfpSettings again;
    char problem[200];
    memset(&again, 0, sizeof again);
    CHECK(tfp_settings_load(&again, saved_text, saved_length, problem, sizeof problem));
    CHECK(again.field[TFP_SET_placement] && again.field[TFP_SET_placement]->placement.x == .11 && again.field[TFP_SET_placement]->placement.roll == 3);
    CHECK(again.field[TFP_SET_action_placements] && again.field[TFP_SET_action_placements]->count == s->field[TFP_SET_action_placements]->count);
    tfp_settings_free(&again);
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    CHECK(tfp_wire_settings_unsaved(&wire));
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135) && saves == 1); /* the same value again: still one change */
    tfp_wire_settings_save_frame(&wire);
    CHECK(saves == 2 && strstr(saved_text, "135"));
    tfp_wire_settings_save_frame(&wire);
    CHECK(saves == 2);
    CHECK(!tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, true, true, 0)); /* the wrong kind */
    save_fails = true;
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_show_torso, true, true, 0));
    for (int i = 0; i < 5; i++) tfp_wire_settings_save_frame(&wire);
    CHECK(saves == 3 && tfp_wire_settings_unsaved(&wire) && logged("Refused: settings were not saved: disk full")); /* one try per change, not one a frame */
    save_fails = false;
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_show_torso, true, false, 0));
    tfp_wire_settings_save_frame(&wire);
    CHECK(saves == 4 && !tfp_wire_settings_unsaved(&wire));
    { /* The menu's per-weapon drafts follow the weapon in the hands: that is the menu mirroring a profile, not a
       * change of the user's (seen in the game 2026-11-23: settings.json was rewritten on every weapon swap). */
      const double hip_before = tfp_settings_menu_number(tfp_wire_settings(&wire), TFP_MENU_weapon_near_hip_cm);
      uint64_t mark = tfp_wire_settings_mirror_begin(&wire);
      CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_weapon_near_hip_cm, false, false, hip_before == 9 ? 8 : 9));
      tfp_wire_settings_mirror_end(&wire, mark);
      CHECK(!tfp_wire_settings_unsaved(&wire));
      mark = tfp_wire_settings_mirror_begin(&wire);
      CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_weapon_near_hip_cm, false, false, hip_before));
      tfp_wire_settings_mirror_end(&wire, mark);
      CHECK(!tfp_wire_settings_unsaved(&wire) && saves == 4);
    }
    tfp_wire_settings_free(&wire);
  }
  { /* ---- the menu on MCM (src/wire_menu.c) ---- */
    static char definition[65536];
    TfpRtValue r[30];
    new_wire();
    CHECK(tfp_wire_settings_bind(&wire, settings_user_json, sizeof settings_user_json - 1, "settings.json"));
    TfpSettings *s = tfp_wire_settings(&wire);
    CHECK(tfp_wire_settings_menu_set(&wire, TFP_MENU_motion_strength, false, false, 135));
    size_t n = tfp_wire_menu_definition(&wire, 33, definition, sizeof definition);
    CHECK(n > 2000 && n < sizeof definition && strlen(definition) == n);
    CHECK(tfp_wire_menu_definition(&wire, 33, NULL, 0) == n);                  /* measuring writes nothing */
    CHECK(strstr(definition, "\"id\":\"motion_strength\",\"type\":\"slider\",\"label\":\"Weapon movement strength\",\"min\":0,\"max\":200,\"step\":5,\"default\":135"));
    CHECK(strstr(definition, "\"id\":\"editor_key\",\"type\":\"keybind\",\"label\":\"Open / close placement editor\",\"default\":33")); /* retail's F8 is HellForge's editor */
    CHECK(strstr(definition, "\"id\":\"toggle_key\",\"type\":\"keybind\",\"label\":\"Toggle True First Person\",\"default\":122"));
    CHECK(strstr(definition, "\"id\":\"weapon_sway_speed\",\"type\":\"slider\",\"label\":\"Weapon sway response speed\",\"min\":25,\"max\":200,\"step\":5"));
    CHECK(!strstr(definition, "start_free_aim") && !strstr(definition, "start_native_test"));
    /* the whole menu: an id old values cannot be read into, MCM keeps no file of its own, three pages, every control */
    CHECK(strstr(definition, "{\"id\":\"" TFP_MENU_MOD_ID "\",") == definition && strstr(definition, "\"storage\":true"));
    CHECK(strcmp(TFP_MENU_MOD_ID, "goose_true_first_person") && strcmp(TFP_MENU_MOD_ID, "true_first_person") && strcmp(TFP_MENU_MOD_ID, "first_person"));
    CHECK(!strstr(definition, "Fird") && !strstr(definition, "fird_person_arms\",\"type\":\"toggle\",\"label\":\"Fird"));
    CHECK(strstr(definition, "\"id\":\"fird_person_arms\",\"type\":\"toggle\",\"label\":\"First Person Arms\""));
    CHECK(strstr(definition, "{\"id\":\"locked_view\",\"name\":\"Locked view\""));
    CHECK(strstr(definition, "\"id\":\"fov_degrees\",\"type\":\"slider\",\"label\":\"Field of view (degrees)\",\"min\":50,\"max\":120,\"step\":1,\"default\":80"));
    CHECK(strstr(definition, "\"id\":\"camera_style\",\"type\":\"choice\",\"label\":\"Camera style\",\"presentation\":\"dropdown\",\"choices\":[\"Stable\",\"Bodycam\"],\"default\":1"));
    CHECK(strstr(definition, "\"id\":\"camera_damping\",\"type\":\"slider\""));
    {
      int count = 0;
      const TfpMenuControl *controls = tfp_wire_menu_controls(&count);
      for (int i = 0; i < count; i++) {
        char id[64];
        snprintf(id, sizeof id, "\"id\":\"%s\",", controls[i].id);
        CHECK(strstr(definition, id));
        if (controls[i].kind == TFP_MK_CHOICE)
          for (int k = 0; k < controls[i].choice_count; k++) CHECK(strstr(definition, controls[i].choices[k]));
      }
    }
#ifdef TFP_HAVE_MCM_SCHEMA
    {
      char error[256] = "";
      McmModDef *def = schema_parse(definition, n, error, sizeof error);
      if (!def) printf("wire settings: DBF-MCM refuses the definition: %s\n", error);
      CHECK(def && !strcmp(def->id, TFP_MENU_MOD_ID) && def->page_count == 3 && !def->pages[0].require_confirmation && def->storage);
      CHECK(!strcmp(def->pages[2].id, "locked_view") && !def->pages[2].require_confirmation);
      int count = 0;
      const TfpMenuControl *controls = tfp_wire_menu_controls(&count);
      for (int i = 0; i < count; i++) {
        const int at = schema_find(def, controls[i].id);
        if (at < 0) printf("wire settings: MCM has no control %s\n", controls[i].id);
        CHECK(at >= 0 && def->controls[at].page == (uint32_t)controls[i].page);
        if (controls[i].kind == TFP_MK_SLIDER) CHECK(def->controls[at].type == MCM_SLIDER && def->controls[at].min == controls[i].min && def->controls[at].max == controls[i].max);
        if (controls[i].kind == TFP_MK_CHOICE) CHECK(def->controls[at].type == MCM_CHOICE && def->controls[at].choice_count == (uint32_t)controls[i].choice_count);
      }
      const int style = schema_find(def, "camera_style"), fov = schema_find(def, "fov_degrees");
      CHECK(style >= 0 && def->controls[style].def.number == 1 && fov >= 0 && def->controls[fov].def.number == 80);
      int strength = schema_find(def, "motion_strength"), preset = schema_find(def, "motion_preset"), key = schema_find(def, "editor_key");
      CHECK(strength >= 0 && def->controls[strength].type == MCM_SLIDER && def->controls[strength].def.number == 135 && def->controls[strength].max == 200);
      CHECK(preset >= 0 && def->controls[preset].type == MCM_CHOICE && key >= 0 && def->controls[key].type == MCM_KEYBIND && def->controls[key].def.number == 33);
      CHECK(schema_find(def, "save_weapon_near") >= 0 && schema_find(def, "runtime_status") >= 0 && schema_find(def, "sprint_motion_strength") >= 0);
      schema_free(def);
    }
#endif
    /* MCM's file holds other values than settings.json: they win */
    mcm_count = mcm_sets = mcm_labels = mcm_disables = mcm_state_count = 0, mcm_disabled[0] = 0;
    mcm_put("motion_strength", TFP_MENU_VALUE_NUMBER, false, 150), mcm_put("show_torso", TFP_MENU_VALUE_BOOL, true, 0);
    mcm_put("toggle_key", TFP_MENU_VALUE_NUMBER, false, 121), mcm_put("editor_key", TFP_MENU_VALUE_NUMBER, false, 119);
    mcm_put("weapon_near_hip_cm", TFP_MENU_VALUE_NUMBER, false, 9), mcm_put("weapon_near_aim_cm", TFP_MENU_VALUE_NUMBER, false, 9);
    mcm_put("show_aim_markers", TFP_MENU_VALUE_BOOL, true, 0), mcm_put("family_placement_defaults", TFP_MENU_VALUE_BOOL, false, 0);
    TfpMenuHost host = {NULL, mcm_get, mcm_set, mcm_set_disabled, mcm_set_label, NULL, 0};
    CHECK(!tfp_wire_menu_bound(&wire) && tfp_wire_menu_toggle_key(&wire) == 122);
    CHECK(tfp_wire_menu_bind(&wire, &host, TFP_MENU_NO_ARMS, NULL) && tfp_wire_menu_bound(&wire));
    CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_strength) == 150 && tfp_settings_menu_flag(s, TFP_MENU_show_torso));
    CHECK(tfp_wire_menu_toggle_key(&wire) == 121 && tfp_wire_menu_editor_key(&wire, 33) == 33);
    CHECK(strstr(mcm_disabled, "fird_person_arms") && strstr(mcm_disabled, "edit_placement") && logged("Ready: menu: registered with DBF-MCM as"));
    CHECK(logged("\"First Person Arms\" is shown disabled") && !logged("Fird"));
    CHECK(logged("three pages") && mcm_is_disabled("one_handed_pistols"));
    /* the locked view's options: MCM's values reach the store at once; the Bodycam-only sliders follow the style */
    CHECK(mcm_is_disabled("kick_strength") && mcm_is_disabled("kick_max_degrees") && mcm_is_disabled("camera_damping") && !mcm_is_disabled("thrown_kick_strength"));
    {
      TfpMenuValue style = {TFP_MENU_VALUE_NUMBER, false, 2}, stable = {TFP_MENU_VALUE_NUMBER, false, 1}, fov = {TFP_MENU_VALUE_NUMBER, false, 95};
      TfpMenuValue fov_before = {TFP_MENU_VALUE_NUMBER, false, 80};
      tfp_wire_menu_changed(&wire, "fov_degrees", &fov, &fov_before);
      CHECK(tfp_settings_locked_number(s, TFP_LOCKED_fov_degrees) == 95);
      tfp_wire_menu_changed(&wire, "camera_style", &style, &stable);
      CHECK(tfp_settings_locked_flag(s, TFP_LOCKED_bodycam));
      tfp_wire_menu_frame(&wire);
      CHECK(!mcm_is_disabled("kick_strength") && !mcm_is_disabled("camera_damping"));
      tfp_wire_menu_changed(&wire, "camera_style", &stable, &style);
      tfp_wire_menu_frame(&wire);
      CHECK(!tfp_settings_locked_flag(s, TFP_LOCKED_bodycam) && mcm_is_disabled("kick_strength") && mcm_is_disabled("camera_damping"));
    }
    /* a slider moved in MCM reaches the store at once and is saved by the next save frame */
    TfpMenuValue v = {TFP_MENU_VALUE_NUMBER, false, 80}, before = {TFP_MENU_VALUE_NUMBER, false, 150};
    tfp_wire_menu_changed(&wire, "motion_strength", &v, &before);
    CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_strength) == 80);
    v = (TfpMenuValue){TFP_MENU_VALUE_BOOL, true, 0};
    tfp_wire_menu_changed(&wire, "motion_strength", &v, &before);              /* the wrong kind changes nothing */
    CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_strength) == 80);
    tfp_wire_menu_changed(&wire, "no_such_control", &v, &before);
    /* buttons and the two config toggles wait for the tick */
    CHECK(tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK);
    unsigned sets = mcm_sets;
    TfpMenuValue on = {TFP_MENU_VALUE_BOOL, true, 0}, off = {TFP_MENU_VALUE_BOOL, false, 0};
    int family = tfp_settings_flag(s, TFP_SET_family_placement_defaults);  /* the user's file has it on */
    tfp_wire_menu_changed(&wire, "family_placement_defaults", family == 1 ? &off : &on, family == 1 ? &on : &off);
    CHECK(tfp_settings_flag(s, TFP_SET_family_placement_defaults) == family);  /* not yet: MCM's callback is outside the tick */
    CHECK(!tfp_wire_menu_activate(&wire, "save_weapon_near") && !tfp_wire_menu_activate(&wire, "edit_placement"));
    CHECK(tfp_wire_menu_activate(&wire, "motion_strength") && tfp_wire_menu_activate(&wire, "nothing"));   /* not buttons */
    tfp_wire_menu_changed(&wire, "show_aim_markers", &off, &on);               /* the markers module is off in this binder: refused, put back */
    tfp_wire_menu_frame(&wire);
    CHECK(tfp_settings_flag(s, TFP_SET_family_placement_defaults) == (family == 1 ? 0 : 1) && tfp_wire_settings_unsaved(&wire) == false);
    CHECK(logged("Menu: \"Save clipping for this weapon\": Weapon changed; reopen its clipping controls"));
    CHECK(logged("Menu: \"Edit arms and weapon placement\": Placement editor is unavailable"));
    CHECK(logged("Menu: \"Show aim markers\" was not changed:") && mcm_slot("show_aim_markers")->boolean && mcm_sets >= sets + 1);
    /* labels: sent when they change, not every frame */
    CHECK(!strncmp(mcm_last_label[0], "Status: ", 8) && !strcmp(mcm_last_label[1], "Weapon: enable First Person with a firearm to edit its clipping"));
    CHECK(strstr(mcm_disabled, "weapon_near_hip_cm") && strstr(mcm_disabled, "reset_weapon_near"));
    unsigned labels = mcm_labels, disables = mcm_disables;
    sets = mcm_sets;
    for (int i = 0; i < 10; i++) tfp_wire_menu_frame(&wire);
    CHECK(mcm_labels == labels && mcm_disables == disables && mcm_sets == sets);
    /* with no weapon the per-weapon drafts show the global clipping (near_clip_cm 5 here, aim off: 5 / 5) */
    CHECK(mcm_slot("weapon_near_hip_cm")->number == tfp_settings_menu_number(s, TFP_MENU_near_clip_cm) && mcm_slot("weapon_near_aim_cm")->number == 5);
    /* an editor is offered: the button reaches it */
    CHECK(tfp_wire_menu_bind(&wire, &host, 0, test_edit));
    CHECK(!tfp_wire_menu_activate(&wire, "edit_placement"));
    tfp_wire_menu_frame(&wire);
    CHECK(logged("Menu: \"Edit arms and weapon placement\": editor asked"));
    CHECK(tfp_wire_call(&wire, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK);
    tfp_wire_menu_unbind(&wire);
    CHECK(!tfp_wire_menu_bound(&wire) && tfp_wire_menu_activate(&wire, "stop_test"));
    tfp_wire_menu_frame(&wire);
    tfp_wire_settings_free(&wire);
  }
  { /* ---- binding a menu is no change to save: a refused settings.json is not written over by it ---- */
    static const char broken[] = "{\"format\":1,\"first_person\":{},\"menu\":{\"motion_strength\":120,";
    /* MCM holds the registration's defaults (the store's values, the editor key's retail 119 as Page Up 33) */
    disk_clear();
    disk_put("settings.json", broken, sizeof broken - 1);
    CHECK(start_on_disk() && logged("Refused: settings"));
    mcm_count = mcm_sets = mcm_labels = mcm_disables = 0, mcm_disabled[0] = 0;
    mcm_put("editor_key", TFP_MENU_VALUE_NUMBER, false, 33), mcm_put("motion_strength", TFP_MENU_VALUE_NUMBER, false, 100);
    mcm_put("show_torso", TFP_MENU_VALUE_BOOL, false, 0);
    TfpMenuHost host = {NULL, mcm_get, mcm_set, mcm_set_disabled, mcm_set_label, NULL, 33};
    CHECK(tfp_settings_menu_number(tfp_wire_settings(&wire), TFP_MENU_motion_strength) == 100 && tfp_settings_menu_number(tfp_wire_settings(&wire), TFP_MENU_editor_key) == 119);
    CHECK(tfp_wire_menu_bind(&wire, &host, 0, NULL) && tfp_settings_menu_number(tfp_wire_settings(&wire), TFP_MENU_editor_key) == 33);
    CHECK(!tfp_wire_settings_unsaved(&wire));
    for (int i = 0; i < 3; i++) tfp_wire_menu_frame(&wire), tfp_wire_settings_save_frame(&wire);
    CHECK(!disk_writes[0] && !disk_copies && !strcmp(disk_text("settings.json"), broken) && disk_count() == 1);
    tfp_wire_menu_unbind(&wire);
    tfp_wire_settings_free(&wire);
    /* MCM's own file holds a value the user set there: that one is a change, saved with the copy first */
    CHECK(start_on_disk());
    mcm_slot("motion_strength")->number = 150;
    CHECK(tfp_wire_menu_bind(&wire, &host, 0, NULL) && tfp_wire_settings_unsaved(&wire));
    tfp_wire_settings_save_frame(&wire);
    CHECK(disk_writes[0] == 1 && disk_copies == 1 && !strcmp(disk_text("settings.json.refused"), broken));
    CHECK(strstr(disk_text("settings.json"), "\"motion_strength\": 150") && strstr(disk_text("settings.json"), "\"editor_key\": 33"));
    tfp_wire_menu_unbind(&wire);
    tfp_wire_settings_free(&wire);
    /* a host that does not say its editor key: another key than the store's is a value of its own */
    disk_clear();
    disk_put("settings.json", broken, sizeof broken - 1);
    CHECK(start_on_disk());
    mcm_slot("motion_strength")->number = 100;
    host.editor_key = 0;
    CHECK(tfp_wire_menu_bind(&wire, &host, 0, NULL) && tfp_wire_settings_unsaved(&wire));
    tfp_wire_menu_unbind(&wire);
    tfp_wire_settings_free(&wire);
  }
  if (list_part()) return 1;
  if (aim_part()) return 1;
  if (compat_part()) return 1;
  printf("wire settings: %u checks passed (%zu refusals of the reader and loader; retail's answers on %zu + %zu + %zu operations, the defaults "
         "also with no file and with a broken one; placement refs; mod.set; the runtime's entry placement, near clip and weapon line)\n",
         checks, COUNT(refused), COUNT(settings_defaults_ops), COUNT(settings_user_ops), COUNT(settings_synthetic_ops));
  return 0;
}
