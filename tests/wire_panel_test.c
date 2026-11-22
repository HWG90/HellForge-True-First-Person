/* The mod's own settings panel (src/wire_panel.c) and the locked view's options in the settings store
 * (src/tfp_settings.c, src/wire_settings.c), with a stand-in overlay host: no Windows, no game, no NEST.
 *
 *   1. the nine locked-view options: defaults, the "locked_view" section of settings.json read and written, the named getters;
 *   2. the panel: every option of the MCM registration and the nine new ones is drawn, a changed value reaches the
 *      store and is saved, a missing or old host is refused softly, and DBF-MCM is not called unless its switch is on. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire.h"
#include "wire_off.h"
#include "wire_settings.h"
#include "wire_menu.h"
#include <stddef.h>
#include "wire_panel.h"

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { printf("wire panel: FAILED %s (line %d)\n", #condition, __LINE__); return 1; } } while (0)
#define COUNT(a) (sizeof(a) / sizeof *(a))

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
static bool test_save(void *user, const char *text, size_t length, char *why, size_t why_size) {
  (void)user;
  saves++;
  if (save_fails) { snprintf(why, why_size, "disk full"); return false; }
  memcpy(saved_text, text, length); saved_text[length] = 0; saved_length = length;
  return true;
}
#define STORE_PART
static int store_part(void) {
  TfpSettings s;
  memset(&s, 0, sizeof s);
  CHECK(tfp_settings_defaults(&s));
  /* the nine options and their defaults */
  CHECK(TFP_LOCKED_COUNT == 9);
  CHECK(tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock) && tfp_settings_locked_flag(&s, TFP_LOCKED_fov_lock) &&
        tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 80 && tfp_settings_locked_flag(&s, TFP_LOCKED_fov_zoom_when_aiming) &&
        tfp_settings_locked_number(&s, TFP_LOCKED_kick_strength) == 35 && tfp_settings_locked_number(&s, TFP_LOCKED_kick_max_degrees) == 8 &&
        tfp_settings_locked_number(&s, TFP_LOCKED_thrown_kick_strength) == 70 && tfp_settings_locked_flag(&s, TFP_LOCKED_one_handed_pistols) &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_holster_animation));
  const TfpSettingsLockedInfo *info = tfp_settings_locked_info(TFP_LOCKED_fov_degrees);
  CHECK(info && !strcmp(info->name, "fov_degrees") && !info->is_bool && info->value == 80 && info->min == 50 && info->max == 120 && info->step == 1);
  info = tfp_settings_locked_info(TFP_LOCKED_kick_strength);
  CHECK(info && info->min == 0 && info->max == 100 && info->step == 5 && info->value == 35);
  info = tfp_settings_locked_info(TFP_LOCKED_kick_max_degrees);
  CHECK(info && info->min == 0 && info->max == 20 && info->step == 1);
  info = tfp_settings_locked_info(TFP_LOCKED_thrown_kick_strength);
  CHECK(info && info->min == 0 && info->max == 100 && info->step == 5 && info->value == 70);
  CHECK(tfp_settings_locked_info(TFP_LOCKED_camera_lock)->is_bool && !tfp_settings_locked_info(TFP_LOCKED_COUNT));
  CHECK(tfp_settings_locked("holster_animation") == TFP_LOCKED_holster_animation && tfp_settings_locked("near_clip_cm") < 0 && tfp_settings_locked(NULL) < 0);
  /* writes: counted once per change, the wrong kind and not-a-number refused, a number kept inside its range */
  CHECK(s.locked_writes == 0);
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_degrees, false, false, 95) && s.locked_writes == 1 && tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 95);
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_degrees, false, false, 95) && s.locked_writes == 1);
  CHECK(!tfp_settings_set_locked(&s, TFP_LOCKED_fov_degrees, true, true, 0) && !tfp_settings_set_locked(&s, TFP_LOCKED_camera_lock, false, false, 1));
  CHECK(!tfp_settings_set_locked(&s, TFP_LOCKED_fov_degrees, false, false, NAN) && !tfp_settings_set_locked(&s, TFP_LOCKED_COUNT, true, true, 0));
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_fov_degrees, false, false, 400) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 120);
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_kick_max_degrees, false, false, -3) && tfp_settings_locked_number(&s, TFP_LOCKED_kick_max_degrees) == 0);
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_camera_lock, true, false, 0) && !tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock));
  CHECK(s.menu_writes == 0 && s.writes == 0);
  /* written under "locked_view", and read back */
  static char text[65536], why[200];
  size_t n = tfp_settings_write(&s, text, sizeof text);
  CHECK(n && n < sizeof text && strstr(text, "\"locked_view\": {") && strstr(text, "\"camera_lock\": false") && strstr(text, "\"fov_degrees\": 120") &&
        strstr(text, "\"kick_max_degrees\": 0") && strstr(text, "\"holster_animation\": true") && strstr(text, "\"thrown_kick_strength\": 70"));
  TfpSettings back;
  memset(&back, 0, sizeof back);
  CHECK(tfp_settings_load(&back, text, n, why, sizeof why));
  for (int i = 0; i < TFP_LOCKED_COUNT; i++)
    CHECK(back.locked[i].is_bool == s.locked[i].is_bool && back.locked[i].boolean == s.locked[i].boolean && back.locked[i].number == s.locked[i].number);
  tfp_settings_free(&back);
  tfp_settings_free(&s);
  /* a file from before the section: the defaults */
  static const char old[] = "{\"format\":1,\"first_person\":{}}";
  CHECK(tfp_settings_load(&s, old, sizeof old - 1, why, sizeof why) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 80 &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock));
  tfp_settings_free(&s);
  static const char part[] = "{\"format\":1,\"first_person\":{},\"locked_view\":{\"one_handed_pistols\":false,\"kick_strength\":250}}";
  CHECK(tfp_settings_load(&s, part, sizeof part - 1, why, sizeof why) && !tfp_settings_locked_flag(&s, TFP_LOCKED_one_handed_pistols) &&
        tfp_settings_locked_number(&s, TFP_LOCKED_kick_strength) == 100 && tfp_settings_locked_flag(&s, TFP_LOCKED_holster_animation));
  tfp_settings_free(&s);
  static const struct { const char *json, *why; } refusals[] = {
    {"{\"format\":1,\"first_person\":{},\"locked_view\":[]}", "locked_view: expected an object"},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"volume\":1}}", "an unknown option"},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"camera_lock\":1}}", "expected true or false"},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"fov_degrees\":true}}", "expected a number"},
  };
  for (size_t i = 0; i < COUNT(refusals); i++) {
    CHECK(!tfp_settings_load(&s, refusals[i].json, strlen(refusals[i].json), why, sizeof why) && strstr(why, refusals[i].why));
    CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 80); /* refused: the defaults */
    tfp_settings_free(&s);
  }

  /* through the binder: the named getters, and a change is saved once */
  new_wire();
  CHECK(tfp_locked_view_camera_lock(&wire) && tfp_locked_view_fov_lock(&wire) && tfp_locked_view_fov_degrees(&wire) == 80 &&
        tfp_locked_view_fov_zoom_when_aiming(&wire) && tfp_locked_view_kick_strength(&wire) == 35 && tfp_locked_view_kick_max_degrees(&wire) == 8 &&
        tfp_locked_view_thrown_kick_strength(&wire) == 70 && tfp_locked_view_one_handed_pistols(&wire) && tfp_locked_view_holster_animation(&wire));
  CHECK(!tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90)); /* no store bound: the defaults answer */
  CHECK(tfp_wire_settings_bind(&wire, part, sizeof part - 1, "test") && logged("Ready: settings") && !logged("Refused"));
  tfp_wire_settings_saver(&wire, test_save, NULL);
  CHECK(!tfp_locked_view_one_handed_pistols(&wire) && tfp_locked_view_kick_strength(&wire) == 100 && tfp_locked_view_fov_degrees(&wire) == 80);
  saves = 0;
  tfp_wire_settings_save_frame(&wire);
  CHECK(saves == 0 && !tfp_wire_settings_unsaved(&wire));
  CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 104) && tfp_wire_settings_unsaved(&wire));
  tfp_wire_settings_save_frame(&wire);
  CHECK(saves == 1 && strstr(saved_text, "\"fov_degrees\": 104") && !tfp_wire_settings_unsaved(&wire) && tfp_locked_view_fov_degrees(&wire) == 104);
  tfp_wire_settings_save_frame(&wire);
  CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 104));
  tfp_wire_settings_save_frame(&wire);
  CHECK(saves == 1);
  CHECK(!tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, true, true, 0));
  tfp_wire_settings_free(&wire);
  return 0;
}


/* ---- a stand-in overlay host: what the panel drew, and a user who does one thing per frame ---- */
static struct {
  OvPanelDef def; char id[96], title[64]; bool refuse_panel, open; int registered, opened, closed;
  char drawn[160][420]; int drawn_count;
  const char *tab;            /* the selected tab; NULL: every tab's content is drawn */
  const char *click, *flip, *slide, *release, *pick;
  int slide_to, pick_to, key;
  bool hover, active;
  char last[200];
  int disabled, disabled_depth, tab_bars, tab_items, bad_colours, locks, lock_depth, connects;
  bool no_host;
} ov;
static OvPanel *const HANDLE = (OvPanel *)&ov;
static void drew(const char *kind, const char *text) {
  if (ov.drawn_count < (int)COUNT(ov.drawn)) snprintf(ov.drawn[ov.drawn_count++], sizeof ov.drawn[0], "%s:%s%s", kind, text, ov.disabled ? " (disabled)" : "");
  snprintf(ov.last, sizeof ov.last, "%s", text);
}
static const char *drawn(const char *line) {
  for (int i = 0; i < ov.drawn_count; i++) if (!strcmp(ov.drawn[i], line)) return ov.drawn[i];
  return NULL;
}
static const char *drawn_part(const char *part) {
  for (int i = 0; i < ov.drawn_count; i++) if (strstr(ov.drawn[i], part)) return ov.drawn[i];
  return NULL;
}
static bool is(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static OvPanel *o_register(const OvPanelDef *def) {
  ov.registered++;
  if (ov.refuse_panel) return NULL;
  ov.def = *def; snprintf(ov.id, sizeof ov.id, "%s", def->id); snprintf(ov.title, sizeof ov.title, "%s", def->title);
  return HANDLE;
}
static void o_set_open(OvPanel *p, int open) { if (p == HANDLE) { ov.open = open != 0; if (open) ov.opened++; else ov.closed++; } }
static int o_is_open(OvPanel *p) { return p == HANDLE && ov.open; }
static void o_text(const char *text) { drew("text", text); }
static void o_text_colored(uint32_t colour, const char *text) { if (colour >> 24 != 0xAB) ov.bad_colours++; drew("text", text); }
static void o_heading(const char *text) { drew("heading", text); }
static void o_key_cap(const char *text) { drew("cap", text); }
static void o_tooltip(const char *text) { drew("tip", text); }
static void o_none(void) {}
static int o_hovered(void) { return ov.hover; }
static int o_button(const char *label) {
  drew("button", label);
  if (ov.disabled || !is(ov.click, label)) return 0;
  ov.click = NULL;
  return 1;
}
static int o_primary(const char *label) {
  drew("primary", label);
  if (ov.disabled || !is(ov.click, label)) return 0;
  ov.click = NULL;
  return 1;
}
static int o_checkbox(const char *label, int *value) {
  char line[300];
  snprintf(line, sizeof line, "%s=%s", label, *value ? "on" : "off");
  drew("check", line); snprintf(ov.last, sizeof ov.last, "%s", label);
  if (ov.disabled || !is(ov.flip, label)) return 0;
  ov.flip = NULL; *value = !*value;
  return 1;
}
static int o_slider(const char *label, int *value, int min, int max) {
  char line[300];
  snprintf(line, sizeof line, "%s=%d[%d..%d]", label, *value, min, max);
  drew("slider", line); snprintf(ov.last, sizeof ov.last, "%s", label);
  if (ov.disabled || !is(ov.slide, label)) return 0;
  ov.slide = NULL; *value = ov.slide_to;
  return 1;
}
static int o_active(void) { return ov.active; }
static int o_released(void) { if (!is(ov.release, ov.last)) return 0; ov.release = NULL; return 1; }
static int o_combo(const char *label, int *current, const char *const *items, int count) {
  char line[300];
  snprintf(line, sizeof line, "%s=%s", label, *current >= 0 && *current < count ? items[*current] : "?");
  drew("combo", line);
  if (ov.disabled || !is(ov.pick, label)) return 0;
  ov.pick = NULL; *current = ov.pick_to;
  return 1;
}
static int o_tab_bar(const char *id) { (void)id; ov.tab_bars++; return 1; }
static void o_end_tab_bar(void) { ov.tab_bars--; }
static int o_tab_item(const char *label) { drew("tab", label); if (ov.tab && strcmp(ov.tab, label)) return 0; ov.tab_items++; return 1; }
static void o_end_tab_item(void) { ov.tab_items--; }
static void o_begin_disabled(int disabled) { ov.disabled_depth++; if (disabled) ov.disabled++; }
static void o_end_disabled(void) { ov.disabled_depth--; if (ov.disabled) ov.disabled--; }
static int o_key_pressed(int vk) { return ov.key && vk == ov.key; }
static uint32_t o_theme(int slot) { return 0xAB000000u | (uint32_t)slot; }
static void o_log(const char *format, ...) { (void)format; }
static OvApi api;
static void new_host(uint32_t version) {
  memset(&ov, 0, sizeof ov);
  memset(&api, 0, sizeof api);
  api.version = version, api.size = sizeof api;
  api.register_panel = o_register, api.set_open = o_set_open, api.is_open = o_is_open, api.log = o_log;
  api.text = o_text, api.text_wrapped = o_text, api.text_disabled = o_text, api.text_colored = o_text_colored;
  api.separator = o_none, api.same_line = o_none, api.spacing = o_none;
  api.button = o_button, api.small_button = o_button, api.checkbox = o_checkbox, api.slider_int = o_slider, api.combo = o_combo;
  api.is_item_hovered = o_hovered, api.tooltip = o_tooltip;
  api.begin_tab_bar = o_tab_bar, api.end_tab_bar = o_end_tab_bar, api.begin_tab_item = o_tab_item, api.end_tab_item = o_end_tab_item;
  api.key_pressed = o_key_pressed;
  if (version < 2) return;
  api.is_item_active = o_active, api.is_item_deactivated_after_edit = o_released;
  api.begin_disabled = o_begin_disabled, api.end_disabled = o_end_disabled, api.theme = o_theme;
  if (version < 6) { api.size = offsetof(OvApi, button_primary); return; }
  api.button_primary = o_primary, api.key_cap = o_key_cap, api.heading = o_heading;
}
static void p_lock(void *u) { (void)u; ov.locks++; ov.lock_depth++; }
static void p_unlock(void *u) { (void)u; ov.lock_depth--; }
static const OvApi *p_connect(void *u) { (void)u; ov.connects++; return ov.no_host ? NULL : &api; }
static unsigned mcm_ticks, edits;
static void p_mcm(void *u, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) { (void)u; (void)w; (void)unavailable; (void)edit; mcm_ticks++; }
static const char *test_edit(TfpWire *w) { (void)w; edits++; return NULL; }
static const TfpPanelPlatform platform = {NULL, p_lock, p_unlock, p_connect, p_mcm, false, "goose.true-first-person/settings@test", 33};
static int log_count(const char *part) {
  int n = 0;
  for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) n++;
  return n;
}
/* One frame of the host's render thread, then one game update as src/mod_view.c orders it. */
static double frame_time;
static void draw_frame(void) {
  OvFrame frame = {1920, 1080, 1 / 60.0f, frame_time += 1 / 60.0, 1, 1};
  ov.drawn_count = 0;
  if (ov.def.draw) ov.def.draw(&frame, ov.def.user);
}
static void game_update(void) {
  tfp_wire_panel_frame(&wire);
  tfp_wire_menu_frame(&wire);
  tfp_wire_settings_save_frame(&wire);
}
static const char settings_json[] = "{\"format\":1,\"first_person\":{},\"menu\":{\"motion_strength\":120,\"toggle_key\":122}}";
static bool start_wire(void) {
  new_wire();
  if (!tfp_wire_settings_bind(&wire, settings_json, sizeof settings_json - 1, "test")) return false;
  tfp_wire_settings_saver(&wire, test_save, NULL);
  saves = 0; mcm_ticks = 0; edits = 0;
  return true;
}

static int panel_part(void) {
  TfpSettings *s;
  /* ---- no host: a try a second for 15 s, then one line and no more tries; nothing is registered, MCM is not asked ---- */
  CHECK(start_wire());
  new_host(8); ov.no_host = true;
  for (uint64_t ms = 1000; ms < 16000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 15 && !log_count("Settings panel:") && !tfp_wire_panel_bound(&wire));
  for (uint64_t ms = 16000; ms < 30000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 16 && log_count("Settings panel:") == 1 && logged("is not loaded") && !tfp_wire_panel_bound(&wire));
  CHECK(!ov.registered && !mcm_ticks && !tfp_wire_menu_bound(&wire) && ov.lock_depth == 0);
  tfp_wire_panel_frame(&wire);                                   /* nothing bound: nothing happens */
  CHECK(!tfp_wire_panel_is_open(&wire));
  tfp_wire_panel_close(&wire); tfp_wire_panel_retire(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);

  /* ---- a host older than the panel needs: refused in one line, no panel, the mod carries on ---- */
  CHECK(start_wire());
  new_host(5);
  for (uint64_t ms = 0; ms < 5000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 1 && log_count("Refused: settings panel") == 1 && logged("API 5") && !ov.registered && !tfp_wire_panel_bound(&wire));
  CHECK(!tfp_wire_menu_bound(&wire) && !mcm_ticks);
  tfp_wire_panel_free(&wire);
  new_host(8); api.key_cap = NULL;                               /* the right version with a function missing */
  CHECK(!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit) && log_count("Refused: settings panel") == 2 && !ov.registered);
  new_host(8); ov.refuse_panel = true;                           /* the host does not take the panel */
  CHECK(!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit) && ov.registered == 1 && logged("did not take the panel"));
  CHECK(!tfp_wire_panel_bound(&wire) && !tfp_wire_menu_bound(&wire));
  CHECK(!tfp_wire_panel_bind(&wire, NULL, &platform, 0, test_edit));
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  new_wire();                                                    /* no settings store: refused */
  new_host(8);
  CHECK(!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit) && !ov.registered);
  tfp_wire_panel_free(&wire);

  /* ---- the panel: registered once, with its own key; DBF-MCM is not asked ---- */
  CHECK(start_wire());
  s = tfp_wire_settings(&wire);
  new_host(8);
  for (uint64_t ms = 0; ms < 5000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 1 && ov.registered == 1 && tfp_wire_panel_bound(&wire) && tfp_wire_menu_bound(&wire) && !mcm_ticks);
  CHECK(!strcmp(ov.title, "True First Person") && !strcmp(ov.id, platform.panel_id) && ov.def.kind == OV_PANEL_WINDOW && ov.def.size == sizeof ov.def);
  CHECK(ov.def.hotkey == 0x79 && TFP_PANEL_KEY == 0x79 && ov.def.draw && !(ov.def.window_flags & OV_WINDOW_NO_TITLE));
  CHECK(logged("Ready: settings panel") && logged("F10") && logged("Ready: menu: registered with the mod's own panel") && !logged("DBF-MCM as"));
  CHECK(!ov.open && !tfp_wire_panel_is_open(&wire) && !saves);

  /* ---- every option of the MCM registration and the nine new ones is drawn ---- */
  game_update();
  /* as with MCM: the editor key's retail default (F8, HellForge's own editor) is stored as Page Up once the menu is bound */
  CHECK(saves == 1 && strstr(saved_text, "\"editor_key\": 33") && strstr(saved_text, "\"motion_strength\": 120"));
  saves = 0;
  game_update();
  CHECK(!saves);
  ov.hover = true;
  draw_frame();
  CHECK(drawn("tab:First person") && drawn("tab:Bob and sway") && drawn("tab:Locked view"));
  int count = 0, sliders = 0, checks_drawn = 0, keys = 0, described = 0;
  const TfpMenuControl *controls = tfp_wire_menu_controls(&count);
  CHECK(count == 29);
  for (int i = 0; i < count; i++) {
    const TfpMenuControl *c = &controls[i];
    char line[420];
    TfpMenuValue v = {0, false, 0};
    tfp_wire_menu_default(&wire, c, 33, &v);
    switch (c->kind) {
      case TFP_MK_BUTTON: CHECK(drawn_part(c->label) && (strstr(drawn_part(c->label), "button:") || strstr(drawn_part(c->label), "primary:"))); break;
      case TFP_MK_TOGGLE: case TFP_MK_CHECK:
        if (strcmp(c->id, "show_aim_markers")) { snprintf(line, sizeof line, "check:%s=%s", c->label, v.boolean ? "on" : "off"); CHECK(drawn(line)); }
        else CHECK(drawn_part("check:Show aim markers="));
        checks_drawn++; break;
      case TFP_MK_SLIDER: {
        snprintf(line, sizeof line, "slider:%s=", c->label);
        const char *row = drawn_part(line);
        snprintf(line, sizeof line, "[%g..%g]", c->min, c->max);
        CHECK(row && strstr(row, line));
        sliders++; break;
      }
      case TFP_MK_CHOICE: snprintf(line, sizeof line, "combo:%s=%s", c->label, tfp_wire_menu_presets[(int)v.number - 1]); CHECK(drawn(line)); break;
      case TFP_MK_KEY:
        snprintf(line, sizeof line, "text:%s", c->label); CHECK(drawn(line));
        snprintf(line, sizeof line, "button:Change##%s", c->id); CHECK(drawn(line));
        keys++; break;
      case TFP_MK_STATUS: break;                                   /* the two status rows: below */
    }
    if (c->description) { snprintf(line, sizeof line, "tip:%s", c->description); CHECK(drawn_part(line)); described++; }
  }
  CHECK(sliders == 9 && checks_drawn == 10 && keys == 2 && described == 16);
  CHECK(drawn("primary:Enable True First Person") && drawn("cap:F11") && drawn("cap:PAGE UP"));
  CHECK(drawn("slider:Weapon movement strength=120[0..200]") && drawn("slider:Near clip distance (cm)=15[5..40]") && drawn("slider:Weapon bob speed=100[25..200]"));
  CHECK(drawn("text:Status: First person off") && drawn_part("text:Weapon: enable First Person with a firearm"));
  CHECK(drawn("slider:This weapon: hip clearance (cm)=15[5..40] (disabled)") && drawn("button:Save clipping for this weapon (disabled)"));
  CHECK(drawn("check:Camera lock=on") && drawn("check:Field of view lock=on") && drawn("slider:Field of view (degrees)=80[50..120]") &&
        drawn("check:Zoom when aiming=on") && drawn("slider:View kick strength (%)=35[0..100]") && drawn("slider:View kick limit (degrees)=8[0..20]") &&
        drawn("slider:View kick when thrown (%)=70[0..100]") && drawn("check:One-handed pistols=on") && drawn("check:Holster animation=on"));
  CHECK(!ov.tab_bars && !ov.tab_items && !ov.disabled_depth && !ov.disabled && !ov.lock_depth && ov.locks && !ov.bad_colours);
  ov.hover = false;
  ov.tab = "Bob and sway";                                        /* one tab selected: only its controls */
  draw_frame();
  CHECK(drawn_part("slider:Weapon bob strength=") && !drawn_part("Camera lock") && !drawn_part("Near clip distance") && !drawn_part("tip:"));
  ov.tab = NULL;

  /* ---- a changed value reaches the store and is saved ---- */
  /* a slider: the draft while it is held, the stepped value into the store when it is let go */
  ov.slide = "Weapon movement strength"; ov.slide_to = 137; ov.active = true;
  draw_frame(); game_update();
  CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_strength) == 120 && !saves);
  draw_frame();
  CHECK(drawn("slider:Weapon movement strength=135[0..200]"));     /* step 5 */
  game_update();
  CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_strength) == 120 && !saves);
  ov.release = "Weapon movement strength"; ov.active = false;
  draw_frame(); game_update();
  CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_strength) == 135 && saves == 1 && strstr(saved_text, "\"motion_strength\": 135"));
  CHECK(logged("Saved: settings"));
  draw_frame(); game_update();
  CHECK(drawn("slider:Weapon movement strength=135[0..200]") && saves == 1);
  /* a slider let go without the host saying so (the panel was closed mid-drag): still taken */
  ov.slide = "Near clip distance (cm)"; ov.slide_to = 22; ov.active = true;
  draw_frame(); game_update();
  ov.active = false;
  draw_frame(); game_update();
  CHECK(tfp_settings_menu_number(s, TFP_MENU_near_clip_cm) == 22 && saves == 2);
  /* a checkbox */
  CHECK(!tfp_settings_menu_flag(s, TFP_MENU_show_torso));
  ov.flip = "True First Person: show torso";
  draw_frame(); game_update();
  CHECK(tfp_settings_menu_flag(s, TFP_MENU_show_torso) && saves == 3 && strstr(saved_text, "\"show_torso\": true"));
  draw_frame();
  CHECK(drawn("check:True First Person: show torso=on"));
  /* the preset */
  ov.pick = "Motion preset"; ov.pick_to = 2;
  draw_frame(); game_update();
  CHECK(tfp_settings_menu_number(s, TFP_MENU_motion_preset) == 3 && saves == 4);
  draw_frame();
  CHECK(drawn("combo:Motion preset=Natural"));
  /* a key: Change, then the next key pressed; Esc keeps the old one, Delete clears it, the panel's own key is not taken */
  ov.click = "Change##toggle_key";
  draw_frame(); game_update();
  CHECK(tfp_wire_menu_toggle_key(&wire) == 122 && saves == 4);
  draw_frame();
  CHECK(drawn_part("Press a key"));
  ov.key = 0x79; draw_frame(); game_update();
  CHECK(tfp_wire_menu_toggle_key(&wire) == 122 && drawn_part("Press a key"));
  ov.key = 0x75; draw_frame(); game_update(); ov.key = 0;
  CHECK(tfp_wire_menu_toggle_key(&wire) == 0x75 && saves == 5 && strstr(saved_text, "\"toggle_key\": 117"));
  draw_frame();
  CHECK(drawn("cap:F6") && !drawn_part("Press a key"));
  ov.click = "Change##toggle_key"; draw_frame();
  ov.key = 27; draw_frame(); game_update(); ov.key = 0;
  draw_frame();
  CHECK(tfp_wire_menu_toggle_key(&wire) == 0x75 && !drawn_part("Press a key") && saves == 5);
  ov.click = "Change##toggle_key"; draw_frame();
  ov.key = 46; draw_frame(); game_update(); ov.key = 0;
  draw_frame();
  CHECK(tfp_wire_menu_toggle_key(&wire) == 0 && saves == 6 && drawn("text:not bound"));
  ov.click = "Change##editor_key"; draw_frame();
  for (int i = 0; i < 660; i++) draw_frame();                      /* nothing pressed for ten seconds: the question goes away */
  CHECK(!drawn_part("Press a key") && saves == 6);
  /* the nine new options */
  ov.flip = "Camera lock";
  draw_frame(); game_update();
  CHECK(!tfp_locked_view_camera_lock(&wire) && saves == 7 && strstr(saved_text, "\"camera_lock\": false"));
  ov.slide = "View kick strength (%)"; ov.slide_to = 52; ov.active = true;
  draw_frame(); game_update();
  CHECK(tfp_locked_view_kick_strength(&wire) == 35);
  ov.release = "View kick strength (%)"; ov.active = false;
  draw_frame(); game_update();
  CHECK(tfp_locked_view_kick_strength(&wire) == 50 && saves == 8 && strstr(saved_text, "\"kick_strength\": 50"));
  static const struct { const char *label; bool (*get)(TfpWire *); } flags[] = {
    {"Field of view lock", tfp_locked_view_fov_lock}, {"Zoom when aiming", tfp_locked_view_fov_zoom_when_aiming},
    {"One-handed pistols", tfp_locked_view_one_handed_pistols}, {"Holster animation", tfp_locked_view_holster_animation}};
  for (size_t i = 0; i < COUNT(flags); i++) {
    ov.flip = flags[i].label; draw_frame(); game_update();
    CHECK(!flags[i].get(&wire));
  }
  static const struct { const char *label; int to; double (*get)(TfpWire *); } numbers[] = {
    {"Field of view (degrees)", 103, tfp_locked_view_fov_degrees}, {"View kick limit (degrees)", 12, tfp_locked_view_kick_max_degrees},
    {"View kick when thrown (%)", 85, tfp_locked_view_thrown_kick_strength}};
  for (size_t i = 0; i < COUNT(numbers); i++) {
    ov.slide = ov.release = numbers[i].label; ov.slide_to = numbers[i].to; draw_frame(); game_update();
    CHECK(numbers[i].get(&wire) == numbers[i].to);
  }
  CHECK(saves == 15);
  draw_frame();
  CHECK(drawn("check:Camera lock=off") && drawn("slider:View kick strength (%)=50[0..100]") && drawn("slider:Field of view (degrees)=103[50..120]"));
  /* a button: done at the next update, through the menu's own queue */
  ov.click = "Edit arms and weapon placement";
  draw_frame();
  CHECK(!edits);
  game_update();
  CHECK(edits == 1);
  draw_frame(); game_update();
  CHECK(edits == 1);
  /* a toggle the runtime refuses (the markers module is off in this binder) is put back, as in MCM */
  const char *before = drawn_part("check:Show aim markers=");
  CHECK(before);
  bool was_on = strstr(before, "=on") != NULL;
  ov.flip = "Show aim markers";
  draw_frame(); game_update();
  CHECK(logged("Menu: \"Show aim markers\" was not changed"));
  draw_frame();
  CHECK(drawn(was_on ? "check:Show aim markers=on" : "check:Show aim markers=off"));
  /* open, closed (the placement editor closes it before it takes the cursor), retired */
  ov.open = true;
  CHECK(tfp_wire_panel_is_open(&wire));
  tfp_wire_panel_close(&wire);
  CHECK(!ov.open && !tfp_wire_panel_is_open(&wire));
  tfp_wire_panel_retire(&wire);
  ov.flip = "Camera lock";
  draw_frame(); game_update();
  CHECK(!ov.drawn_count && !tfp_locked_view_camera_lock(&wire) && !ov.lock_depth && !ov.bad_colours);
  ov.flip = NULL;
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);

  /* ---- what the build cannot offer is drawn disabled ---- */
  CHECK(start_wire());
  new_host(11);
  CHECK(tfp_wire_panel_bind(&wire, &api, &platform, TFP_MENU_NO_ARMS | TFP_MENU_NO_EDITOR, NULL));
  game_update(); draw_frame();
  CHECK(drawn("check:Fird Person Arms=on (disabled)") && drawn("button:Edit arms and weapon placement (disabled)") && drawn("button:Change##editor_key (disabled)"));
  CHECK(drawn("button:Change##toggle_key") && drawn("check:True First Person: show torso=off"));
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);

  /* ---- the switch: DBF-MCM instead, and then no panel of the mod's own ---- */
  CHECK(start_wire());
  new_host(8);
  TfpPanelPlatform with_mcm = platform;
  with_mcm.mcm_wanted = true;
  for (uint64_t ms = 0; ms < 3000; ms += 100) tfp_wire_panel_tick(&wire, &with_mcm, ms, 0, test_edit);
  CHECK(mcm_ticks == 30 && !ov.connects && !ov.registered && !tfp_wire_panel_bound(&wire) && !tfp_wire_menu_bound(&wire));
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  return 0;
}

int main(void) {
  if (store_part() || panel_part()) return 1;
  printf("wire panel: %u checks passed\n", checks);
  return 0;
}
