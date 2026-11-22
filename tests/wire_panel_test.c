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

int main(void) {
  if (store_part()) return 1;
  printf("wire panel: %u checks passed\n", checks);
  return 0;
}
