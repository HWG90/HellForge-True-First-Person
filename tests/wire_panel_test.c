/* The mod's own settings panel (src/wire_panel.c) and the locked view's options in the settings store
 * (src/tfp_settings.c, src/wire_settings.c), with a stand-in overlay host: no Windows, no game, no NEST.
 *
 *   1. the eleven locked-view options: defaults, the "locked_view" section of settings.json read and written, the named
 *      getters, and the old "holster_animation" read as put_away;
 *   2. the panel: every option of the one list (src/wire_menu.c) is drawn, a changed value reaches the store and is
 *      saved, a missing or old host is refused softly;
 *   2b. which menu a session gets: DBF-MCM when it is ready within its 15 s (no panel then, F10 opens MCM), the panel
 *      when MCM is absent, late past its window or refuses, the panel at once with panel_on.txt, and one line when
 *      neither is there;
 *   3. the three live features (src/wire_options.c): a changed option reaches its setter at the next update, a test
 *      file beats the option both ways, and a change waits while a lease is held through an action. */
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
#include "wire_options.h"

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
static TfpSettingsFile saved_file;
static bool test_save(void *user, TfpSettingsFile file, const char *text, size_t length, char *why, size_t why_size) {
  (void)user;
  saves++, saved_file = file;
  if (save_fails) { snprintf(why, why_size, "disk full"); return false; }
  memcpy(saved_text, text, length); saved_text[length] = 0; saved_length = length;
  return true;
}
static unsigned copies;        /* copies made (the file is there: copy_there) */
static bool copy_there;
static char copy_suffix[32];
static TfpSettingsCopy test_copy(void *user, TfpSettingsFile file, const char *suffix, char *why, size_t why_size) { /* an empty folder, unless copy_there */
  (void)user; (void)file; (void)why; (void)why_size;
  if (!copy_there) return TFP_COPY_NONE;
  copies++, snprintf(copy_suffix, sizeof copy_suffix, "%s", suffix);
  return TFP_COPY_DONE;
}
static const TfpSettingsDisk test_disk = {NULL, test_save, test_copy, NULL};
#define STORE_PART
static int store_part(void) {
  TfpSettings s;
  memset(&s, 0, sizeof s);
  CHECK(tfp_settings_defaults(&s));
  /* the sixteen options and their defaults (round 20: Aim field of view, its chosen degrees, the sight distance in aim) */
  CHECK(TFP_LOCKED_COUNT == 16);
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim) == 1 && tfp_settings_locked_number(&s, TFP_LOCKED_fov_aim_degrees) == 50 &&
        tfp_settings_locked_number(&s, TFP_LOCKED_ads_sight_cm) == 0);
  CHECK(tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock) && tfp_settings_locked_flag(&s, TFP_LOCKED_fov_lock) &&
        tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 80 && tfp_settings_locked_flag(&s, TFP_LOCKED_fov_zoom_when_aiming) &&
        tfp_settings_locked_number(&s, TFP_LOCKED_kick_strength) == 35 && tfp_settings_locked_number(&s, TFP_LOCKED_kick_max_degrees) == 8 &&
        tfp_settings_locked_number(&s, TFP_LOCKED_thrown_kick_strength) == 70 && tfp_settings_locked_number(&s, TFP_LOCKED_camera_damping) == 60 &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_one_handed_pistols) &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_animated_arms) && tfp_settings_locked_flag(&s, TFP_LOCKED_put_away) &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_smooth_eye));
  /* the camera style: Stable unless Bodycam is switched on; first among the kick options */
  CHECK(!tfp_settings_locked_flag(&s, TFP_LOCKED_bodycam) && s.locked[TFP_LOCKED_bodycam].is_bool && TFP_LOCKED_bodycam + 1 == TFP_LOCKED_kick_strength &&
        TFP_LOCKED_bodycam == TFP_LOCKED_fov_zoom_when_aiming + 1 && tfp_settings_locked("bodycam") == TFP_LOCKED_bodycam);
  const TfpSettingsLockedInfo *info = tfp_settings_locked_info(TFP_LOCKED_bodycam);
  CHECK(info && !strcmp(info->name, "bodycam") && info->is_bool && info->value == 0 && info->min == 0 && info->max == 1);
  info = tfp_settings_locked_info(TFP_LOCKED_fov_degrees);
  CHECK(info && !strcmp(info->name, "fov_degrees") && !info->is_bool && info->value == 80 && info->min == 50 && info->max == 120 && info->step == 1);
  info = tfp_settings_locked_info(TFP_LOCKED_kick_strength);
  CHECK(info && info->min == 0 && info->max == 100 && info->step == 5 && info->value == 35);
  info = tfp_settings_locked_info(TFP_LOCKED_kick_max_degrees);
  CHECK(info && info->min == 0 && info->max == 20 && info->step == 1);
  info = tfp_settings_locked_info(TFP_LOCKED_thrown_kick_strength);
  CHECK(info && info->min == 0 && info->max == 100 && info->step == 5 && info->value == 70);
  info = tfp_settings_locked_info(TFP_LOCKED_camera_damping);
  CHECK(info && !strcmp(info->name, "camera_damping") && !info->is_bool && info->min == 0 && info->max == 100 && info->step == 5 && info->value == 60);
  CHECK(TFP_LOCKED_camera_damping == TFP_LOCKED_thrown_kick_strength + 1 && tfp_settings_locked("camera_damping") == TFP_LOCKED_camera_damping);
  CHECK(tfp_settings_locked_info(TFP_LOCKED_camera_lock)->is_bool && !tfp_settings_locked_info(TFP_LOCKED_COUNT));
  CHECK(tfp_settings_locked("put_away") == TFP_LOCKED_put_away && tfp_settings_locked("near_clip_cm") < 0 && tfp_settings_locked(NULL) < 0);
  CHECK(tfp_settings_locked("animated_arms") == TFP_LOCKED_animated_arms && tfp_settings_locked("smooth_eye") == TFP_LOCKED_smooth_eye);
  CHECK(tfp_settings_locked("holster_animation") < 0);   /* no longer an option of its own (read as put_away: below) */
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
  /* written as locked_view.json, never into settings.json (0.6.0-feel.19 and older refuse a member they do not know), and read back */
  static char text[65536], why[200];
  size_t n = tfp_settings_write(&s, text, sizeof text);
  CHECK(n && n < sizeof text && !strstr(text, "locked_view") && !strstr(text, "fov_degrees"));
  n = tfp_settings_write_locked(&s, text, sizeof text);
  CHECK(n && n < sizeof text && tfp_settings_write_locked(&s, NULL, 0) == n && strstr(text, "{\n \"format\": 1,\n \"locked_view\": {") == text &&
        strstr(text, "\"camera_lock\": false") && strstr(text, "\"fov_degrees\": 120") &&
        strstr(text, "\"kick_max_degrees\": 0") && strstr(text, "\"put_away\": true") && strstr(text, "\"thrown_kick_strength\": 70") &&
        strstr(text, " \"locked_view\": {\n  \"ads_sight_cm\": 0,\n  \"animated_arms\": true,\n  \"bodycam\": false,\n  \"camera_damping\": 60,\n  \"camera_lock\": false,") &&
        strstr(text, "\"animated_arms\": true") && strstr(text, "\"smooth_eye\": true") && !strstr(text, "holster_animation"));
  TfpSettings back;
  memset(&back, 0, sizeof back);
  CHECK(tfp_settings_defaults(&back) && tfp_settings_load_locked(&back, text, n, why, sizeof why) && !back.locked_skipped[0] && back.locked_writes == 0);
  for (int i = 0; i < TFP_LOCKED_COUNT; i++)
    CHECK(back.locked[i].is_bool == s.locked[i].is_bool && back.locked[i].boolean == s.locked[i].boolean && back.locked[i].number == s.locked[i].number);
  tfp_settings_free(&back);
  tfp_settings_free(&s);
  /* a locked_view.json from before camera_damping was an option (0.6.0-feel.25 and older): its default, and nothing
   * in the file counts as skipped; a value outside the range is stored as the nearest end, the wrong type refuses */
  static const char before[] = "{\"format\":1,\"locked_view\":{\"animated_arms\":true,\"camera_lock\":true,\"fov_degrees\":95,\"fov_lock\":true,"
    "\"fov_zoom_when_aiming\":true,\"kick_max_degrees\":8,\"kick_strength\":35,\"one_handed_pistols\":true,\"put_away\":true,\"smooth_eye\":true,"
    "\"thrown_kick_strength\":70}}";
  static const char calm[] = "{\"format\":1,\"locked_view\":{\"camera_damping\":250}}", wrong[] = "{\"format\":1,\"locked_view\":{\"camera_damping\":true}}";
  memset(&s, 0, sizeof s);
  CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, before, sizeof before - 1, why, sizeof why) && !s.locked_skipped[0] &&
        tfp_settings_locked_number(&s, TFP_LOCKED_camera_damping) == 60 && tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 95 && s.locked_writes == 0);
  CHECK(tfp_settings_load_locked(&s, calm, sizeof calm - 1, why, sizeof why) && tfp_settings_locked_number(&s, TFP_LOCKED_camera_damping) == 100);
  CHECK(!tfp_settings_load_locked(&s, wrong, sizeof wrong - 1, why, sizeof why) && tfp_settings_locked_number(&s, TFP_LOCKED_camera_damping) == 100);
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_camera_damping, false, false, 0) && tfp_settings_locked_number(&s, TFP_LOCKED_camera_damping) == 0);
  tfp_settings_free(&s);
  /* the same for a locked_view.json from before bodycam was an option (0.6.0-feel.39 and older): off, nothing skipped;
   * a file that has it is read, written back, and the wrong type refuses */
  static const char body[] = "{\"format\":1,\"locked_view\":{\"bodycam\":true}}", body_wrong[] = "{\"format\":1,\"locked_view\":{\"bodycam\":1}}";
  memset(&s, 0, sizeof s);
  CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, before, sizeof before - 1, why, sizeof why) && !s.locked_skipped[0] &&
        !tfp_settings_locked_flag(&s, TFP_LOCKED_bodycam) && s.locked_writes == 0);
  CHECK(tfp_settings_load_locked(&s, body, sizeof body - 1, why, sizeof why) && !s.locked_skipped[0] && tfp_settings_locked_flag(&s, TFP_LOCKED_bodycam));
  CHECK(tfp_settings_write_locked(&s, text, sizeof text) < sizeof text && strstr(text, "\"bodycam\": true"));
  CHECK(!tfp_settings_load_locked(&s, body_wrong, sizeof body_wrong - 1, why, sizeof why) && strstr(why, "locked_view.bodycam: expected true or false") &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_bodycam));
  CHECK(tfp_settings_set_locked(&s, TFP_LOCKED_bodycam, true, false, 0) && !tfp_settings_locked_flag(&s, TFP_LOCKED_bodycam) && s.locked_writes == 1 &&
        !tfp_settings_set_locked(&s, TFP_LOCKED_bodycam, false, false, 1));
  tfp_settings_free(&s);
  /* a settings.json without the member (0.6.0-feel.19 and older, and this build): the defaults */
  static const char old[] = "{\"format\":1,\"first_person\":{}}";
  CHECK(tfp_settings_load(&s, old, sizeof old - 1, why, sizeof why) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 80 &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock) && !s.locked_in_settings);
  tfp_settings_free(&s);
  /* a settings.json of 0.6.0-feel.20 to feel.24: the member is still read */
  static const char part[] = "{\"format\":1,\"first_person\":{},\"locked_view\":{\"one_handed_pistols\":false,\"kick_strength\":250}}";
  CHECK(tfp_settings_load(&s, part, sizeof part - 1, why, sizeof why) && !tfp_settings_locked_flag(&s, TFP_LOCKED_one_handed_pistols) &&
        tfp_settings_locked_number(&s, TFP_LOCKED_kick_strength) == 100 && tfp_settings_locked_flag(&s, TFP_LOCKED_put_away) && s.locked_in_settings &&
        s.locked_writes == 0);
  tfp_settings_free(&s);
  /* "holster_animation" is put_away's value, unless the text has a put_away of its own (wherever it stands); it is
   * not written back, and the load does not count as a change. The same in either file. */
  static const struct { const char *options; bool put_away; } old_keys[] = {
    {"{\"holster_animation\":false}", false},
    {"{\"holster_animation\":true}", true},
    {"{\"holster_animation\":false,\"put_away\":true}", true},
    {"{\"put_away\":false,\"holster_animation\":true}", false},
    {"{\"put_away\":false}", false},
  };
  for (size_t i = 0; i < COUNT(old_keys) * 2; i++) {
    char json[200];
    const bool own_file = i >= COUNT(old_keys);
    const bool put_away = old_keys[i % COUNT(old_keys)].put_away;
    snprintf(json, sizeof json, own_file ? "{\"format\":1,\"locked_view\":%s}" : "{\"format\":1,\"first_person\":{},\"locked_view\":%s}", old_keys[i % COUNT(old_keys)].options);
    if (own_file) CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, json, strlen(json), why, sizeof why));
    else CHECK(tfp_settings_load(&s, json, strlen(json), why, sizeof why));
    CHECK(tfp_settings_locked_flag(&s, TFP_LOCKED_put_away) == put_away && s.locked_writes == 0 && !s.locked_skipped[0]);
    CHECK(tfp_settings_locked_flag(&s, TFP_LOCKED_animated_arms) && tfp_settings_locked_flag(&s, TFP_LOCKED_smooth_eye));
    n = tfp_settings_write_locked(&s, text, sizeof text);
    CHECK(n && n < sizeof text && !strstr(text, "holster_animation") && strstr(text, put_away ? "\"put_away\": true" : "\"put_away\": false"));
    tfp_settings_free(&s);
  }
  /* Nothing under "locked_view" refuses settings.json any more: what cannot be read is skipped and named. */
  static const struct { const char *json, *skipped; double fov; } lenient[] = {
    {"{\"format\":1,\"first_person\":{},\"locked_view\":[]}", "locked_view (not an object)", 80},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"volume\":1,\"fov_degrees\":95}}", "volume", 95},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"fov_degrees\":95,\"camera_lock\":1}}", "camera_lock", 95},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"fov_degrees\":true}}", "fov_degrees", 80},
    {"{\"format\":1,\"first_person\":{},\"locked_view\":{\"holster_animation\":1,\"later\":{\"a\":[1]}}}", "holster_animation, later", 80},
  };
  for (size_t i = 0; i < COUNT(lenient); i++) {
    CHECK(tfp_settings_load(&s, lenient[i].json, strlen(lenient[i].json), why, sizeof why) && s.locked_in_settings);
    CHECK(!strcmp(s.locked_skipped, lenient[i].skipped) && tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == lenient[i].fov);
    CHECK(tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock) && tfp_settings_locked_flag(&s, TFP_LOCKED_put_away));
    tfp_settings_free(&s);
  }
  /* locked_view.json: a name this build does not know is skipped; a known one of the wrong type refuses that file,
   * and then no option of it is taken */
  static const char later[] = "{\"format\":2,\"locked_view\":{\"fov_degrees\":95,\"volume\":1,\"camera_lock\":false,\"deeper\":{\"x\":1}},\"other\":[1,2]}";
  CHECK(tfp_settings_defaults(&s) && tfp_settings_load_locked(&s, later, sizeof later - 1, why, sizeof why) && !strcmp(s.locked_skipped, "volume, deeper, other"));
  CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 95 && !tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock) &&
        tfp_settings_locked_flag(&s, TFP_LOCKED_fov_lock) && s.locked_writes == 0);
  tfp_settings_free(&s);
  static const struct { const char *json, *why; } refusals[] = {
    {"{\"format\":1,\"locked_view\":[]}", "locked_view: expected an object"},
    {"{\"format\":1}", "locked_view: expected an object"},
    {"[]", "the document must be an object"},
    {"{\"locked_view\":{", "not JSON"},
    {"{\"locked_view\":{\"fov_degrees\":95,\"camera_lock\":1}}", "locked_view.camera_lock: expected true or false"},
    {"{\"locked_view\":{\"camera_lock\":false,\"fov_degrees\":true}}", "locked_view.fov_degrees: expected a number"},
    {"{\"locked_view\":{\"camera_lock\":false,\"put_away\":true,\"holster_animation\":1}}", "expected true or false"},
  };
  for (size_t i = 0; i < COUNT(refusals); i++) {
    CHECK(tfp_settings_defaults(&s));
    CHECK(!tfp_settings_load_locked(&s, refusals[i].json, strlen(refusals[i].json), why, sizeof why) && strstr(why, refusals[i].why));
    CHECK(tfp_settings_locked_number(&s, TFP_LOCKED_fov_degrees) == 80 && tfp_settings_locked_flag(&s, TFP_LOCKED_camera_lock)); /* refused: nothing taken */
    tfp_settings_free(&s);
  }
  CHECK(tfp_settings_defaults(&s) && !tfp_settings_load_locked(&s, NULL, 0, why, sizeof why) && strstr(why, "no file"));
  tfp_settings_free(&s);

  /* through the binder: the named getters, and a change is saved once */
  new_wire();
  CHECK(tfp_locked_view_camera_lock(&wire) && tfp_locked_view_fov_lock(&wire) && tfp_locked_view_fov_degrees(&wire) == 80 &&
        tfp_locked_view_fov_zoom_when_aiming(&wire) && tfp_locked_view_kick_strength(&wire) == 35 && tfp_locked_view_kick_max_degrees(&wire) == 8 &&
        tfp_locked_view_thrown_kick_strength(&wire) == 70 && tfp_locked_view_camera_damping(&wire) == 60 && tfp_locked_view_one_handed_pistols(&wire) &&
        tfp_locked_view_animated_arms(&wire) && !tfp_locked_view_bodycam(&wire) &&
        tfp_locked_view_put_away(&wire) && tfp_locked_view_smooth_eye(&wire));
  CHECK(!tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 90)); /* no store bound: the defaults answer */
  CHECK(tfp_wire_settings_bind(&wire, part, sizeof part - 1, "test") && logged("Ready: settings") && !logged("Refused"));
  tfp_wire_settings_disk(&wire, &test_disk, false);
  CHECK(!tfp_locked_view_one_handed_pistols(&wire) && tfp_locked_view_kick_strength(&wire) == 100 && tfp_locked_view_fov_degrees(&wire) == 80);
  saves = 0;
  tfp_wire_settings_save_frame(&wire);
  CHECK(saves == 0 && !tfp_wire_settings_unsaved(&wire));
  CHECK(tfp_wire_settings_locked_set(&wire, TFP_LOCKED_fov_degrees, false, false, 104) && tfp_wire_settings_unsaved(&wire));
  tfp_wire_settings_save_frame(&wire);
  CHECK(saves == 1 && saved_file == TFP_LOCKED_FILE && strstr(saved_text, "\"fov_degrees\": 104") && !tfp_wire_settings_unsaved(&wire) &&
        tfp_locked_view_fov_degrees(&wire) == 104 && logged("Saved: locked view options (locked_view.json"));
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
  bool hover, active;         /* hover: the mouse is over every item (what the real host makes of that: o_hovered) */
  bool last_disabled;         /* the last item was drawn greyed (inside begin_disabled(1)) */
  int items, covers, bad_covers; float cursor[2]; bool cursor_set;   /* o_item_rect, o_dummy */
  char last[200];
  int disabled, disabled_depth, tab_bars, tab_items, bad_colours, locks, lock_depth, connects;
  const char *taken;          /* an id the host already holds (a replaced build's panel): refused */
  uint32_t asked;             /* the API version the panel last asked the host's export for */
  bool no_host;
} ov;
static OvPanel *const HANDLE = (OvPanel *)&ov;
static void drew(const char *kind, const char *text) {
  if (ov.drawn_count < (int)COUNT(ov.drawn)) snprintf(ov.drawn[ov.drawn_count++], sizeof ov.drawn[0], "%s:%s%s", kind, text, ov.disabled ? " (disabled)" : "");
  snprintf(ov.last, sizeof ov.last, "%s", text);
  if (strcmp(kind, "tip")) ov.last_disabled = ov.disabled != 0, ov.items++, ov.cursor_set = false;   /* a tooltip is no item */
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
  if (ov.refuse_panel || is(ov.taken, def->id)) return NULL;
  ov.def = *def; snprintf(ov.id, sizeof ov.id, "%s", def->id); snprintf(ov.title, sizeof ov.title, "%s", def->title);
  return HANDLE;
}
static void o_set_open(OvPanel *p, int open) { if (p == HANDLE) { ov.open = open != 0; if (open) ov.opened++; else ov.closed++; } }
static int o_is_open(OvPanel *p) { return p == HANDLE && ov.open; }
static void o_text(const char *text) { drew("text", text); }
static void o_text_colored(uint32_t colour, const char *text) { if (colour >> 24 != 0xAB) ov.bad_colours++; drew("text", text); }
static void o_heading(const char *text) { drew("heading", text); }
static void o_key_cap(const char *text) { drew("cap", text); }
/* As the real host (overlay-host src/api.cpp): is_item_hovered is Dear ImGui's IsItemHovered() without flags, which
 * answers false for an item drawn disabled, and tooltip asks the same again before it draws anything. */
static int o_hovered(void) { return ov.hover && !ov.last_disabled; }
static void o_tooltip(const char *text) { if (o_hovered()) drew("tip", text); }
static void o_none(void) {}
/* The last item's rectangle: one row of 300 by 18 for each item, 20 apart. */
static void o_item_rect(float *x0, float *y0, float *x1, float *y1) { *x0 = 10, *y0 = 20.0f * (float)ov.items, *x1 = 310, *y1 = *y0 + 18; }
static void o_set_cursor(float x, float y) { ov.cursor[0] = x, ov.cursor[1] = y, ov.cursor_set = true; }
/* An item that draws nothing. The panel lays one over a greyed item to carry its tooltip: counted when it is exactly
 * the last item's rectangle (so the layout goes on as after that item), and it is the last item from here on. */
static void o_dummy(float width, float height) {
  float x0, y0, x1, y1;
  o_item_rect(&x0, &y0, &x1, &y1);
  if (ov.cursor_set && ov.cursor[0] == x0 && ov.cursor[1] == y0 && width == x1 - x0 && height == y1 - y0) ov.covers++; else ov.bad_covers++;
  ov.cursor_set = false, ov.last_disabled = ov.disabled != 0;
}
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
  api.item_rect = o_item_rect, api.set_cursor_screen_pos = o_set_cursor, api.dummy = o_dummy;
  api.begin_disabled = o_begin_disabled, api.end_disabled = o_end_disabled, api.theme = o_theme;
  if (version < 6) { api.size = offsetof(OvApi, button_primary); return; }
  api.button_primary = o_primary, api.key_cap = o_key_cap, api.heading = o_heading;
  /* a host's table ends where its own API version does */
  if (version == 6) api.size = offsetof(OvApi, passive_wheel_total);
  if (version == 7) api.size = offsetof(OvApi, input_captured);
}
static void p_lock(void *u) { (void)u; ov.locks++; ov.lock_depth++; }
static void p_unlock(void *u) { (void)u; ov.lock_depth--; }
/* As the host's export (overlay-host src/host.cpp ov_api): its table, or NULL when asked for a newer API than its own. */
static const OvApi *p_connect(void *u, uint32_t version) {
  (void)u;
  ov.connects++, ov.asked = version;
  return ov.no_host || version > api.version ? NULL : &api;
}
/* DBF-MCM, one try each call: what it answers (mcm_ready_after: READY from that many tries on, -1 never), and F10. */
static unsigned mcm_ticks, edits, mcm_shows;
static TfpMcmState mcm_answer = TFP_MCM_WAITING;
static int mcm_ready_after = -1;
static bool f10_down;
static TfpMcmState p_mcm(void *u, TfpWire *w, unsigned unavailable, TfpMenuEditFn edit) {
  (void)u; (void)w; (void)unavailable; (void)edit;
  mcm_ticks++;
  return mcm_ready_after >= 0 && (int)mcm_ticks > mcm_ready_after ? TFP_MCM_READY : mcm_answer;
}
static bool p_key(void *u, int vk) { (void)u; return vk == TFP_PANEL_KEY && f10_down; }
static void p_show(void *u) { (void)u; mcm_shows++; }
static const char *test_edit(TfpWire *w) { (void)w; edits++; return NULL; }
/* panel_on.txt: the panel at once, DBF-MCM is not tried (the parts below about the panel itself) */
static const TfpPanelPlatform platform = {NULL, p_lock, p_unlock, p_connect, p_mcm, true, "goose.true-first-person/settings", "goose.true-first-person/settings@test", 33,
                                          p_key, p_show};
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
/* The three features' stand-in setters: how often each was called, and with what last. */
static struct { unsigned calls[TFP_FEATURE_COUNT]; bool value[TFP_FEATURE_COUNT]; } features;
static void set_arms(void *u, bool on) { (void)u; features.calls[TFP_FEATURE_ARMS]++, features.value[TFP_FEATURE_ARMS] = on; }
static void set_hold(void *u, bool on) { (void)u; features.calls[TFP_FEATURE_HOLD]++, features.value[TFP_FEATURE_HOLD] = on; }
static void set_smooth(void *u, bool on) { (void)u; features.calls[TFP_FEATURE_SMOOTH]++, features.value[TFP_FEATURE_SMOOTH] = on; }
static TfpWireOptions options;
static void new_options(void) {
  memset(&options, 0, sizeof options);
  memset(&features, 0, sizeof features);
  options.set[TFP_FEATURE_ARMS] = set_arms, options.set[TFP_FEATURE_HOLD] = set_hold, options.set[TFP_FEATURE_SMOOTH] = set_smooth;
}
static bool feature(TfpFeature f, unsigned calls, bool value) { return features.calls[f] == calls && features.value[f] == value; }
static void game_update(void) {
  tfp_wire_panel_frame(&wire);
  tfp_wire_menu_frame(&wire);
  tfp_wire_options_tick(&wire, &options);
  tfp_wire_settings_save_frame(&wire);
}
static const char settings_json[] = "{\"format\":1,\"first_person\":{},\"menu\":{\"motion_strength\":120,\"toggle_key\":122}}";
static bool start_wire(void) {
  new_wire();
  if (!tfp_wire_settings_bind(&wire, settings_json, sizeof settings_json - 1, "test")) return false;
  tfp_wire_settings_disk(&wire, &test_disk, false);
  saves = 0; mcm_ticks = 0; edits = 0; mcm_shows = 0; mcm_answer = TFP_MCM_WAITING; mcm_ready_after = -1; f10_down = false;
  new_options();
  return true;
}

static int panel_part(void) {
  TfpSettings *s;
  /* ---- no host: a try a second for 15 s, then one line and no more tries; nothing is registered, MCM is not asked ---- */
  CHECK(start_wire());
  new_host(8); ov.no_host = true;
  for (uint64_t ms = 1000; ms < 16000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 15 && !log_count("Menu:") && !tfp_wire_panel_bound(&wire));
  for (uint64_t ms = 16000; ms < 30000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 16 && log_count("Menu: no settings menu") == 1 && logged("is not loaded") && logged("older than API 6") && !tfp_wire_panel_bound(&wire));
  CHECK(logged("panel_on.txt") && logged("edited by hand") && logged("settings.json and locked_view.json") && tfp_wire_panel_choice(&wire) == TFP_CHOICE_NONE);
  CHECK(ov.asked == TFP_PANEL_API && TFP_PANEL_API == 6);        /* the API the panel draws with, not the header's newest */
  CHECK(!ov.registered && !mcm_ticks && !tfp_wire_menu_bound(&wire) && ov.lock_depth == 0);
  tfp_wire_panel_frame(&wire);                                   /* nothing bound: nothing happens */
  CHECK(!tfp_wire_panel_is_open(&wire));
  tfp_wire_panel_close(&wire); tfp_wire_panel_retire(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);

  /* ---- a host older than the panel needs: refused in one line, no panel, the mod carries on ---- */
  CHECK(start_wire());
  new_host(5);                                                   /* the host's export answers NULL to a request for API 6: as no host */
  for (uint64_t ms = 0; ms < 20000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 16 && ov.asked == 6 && log_count("Menu: no settings menu") == 1 && logged("older than API 6") && !ov.registered && !tfp_wire_panel_bound(&wire));
  CHECK(!tfp_wire_menu_bound(&wire) && !mcm_ticks);
  tfp_wire_panel_free(&wire);
  CHECK(!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit));   /* handed such a host all the same: refused by name */
  CHECK(log_count("Refused: settings panel") == 1 && logged("API 5") && logged("it needs API 6 or newer") && !ov.registered && !tfp_wire_panel_bound(&wire));
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* ---- hosts 0.4.0 (API 6) and 0.5.0 (API 7), older than the header (API 8): the panel is there, and it is drawn
   * with nothing past the end of such a host's table ---- */
  for (uint32_t version = 6; version <= 7; version++) {
    CHECK(start_wire());
    new_host(version);
    CHECK(api.size < sizeof api && OV_API_VERSION > version);
    for (uint64_t ms = 0; ms < 3000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
    CHECK(ov.connects == 1 && ov.asked == 6 && ov.registered == 1 && tfp_wire_panel_bound(&wire) && !logged("Refused") && !log_count("Menu: no settings"));
    ov.hover = true;
    draw_frame(); game_update();
    CHECK(drawn("tab:Locked view") && drawn("primary:Enable True First Person") && drawn_part("cap:") && ov.lock_depth == 0);
    tfp_wire_menu_unbind(&wire);
    tfp_wire_panel_free(&wire);
    tfp_wire_settings_free(&wire);
  }
  CHECK(start_wire());
  new_host(8); api.key_cap = NULL;                               /* the right version with a function missing */
  CHECK(!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit) && log_count("Refused: settings panel") == 1 && !ov.registered);
  new_host(8); ov.refuse_panel = true;                           /* the host does not take the panel */
  CHECK(!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit) && ov.registered == 2 && logged("did not take the panel"));
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
  /* the same id at every game start: the host keeps the window's place and size by it, and its config can name it */
  CHECK(!strcmp(ov.id, "goose.true-first-person/settings") && !logged("replaced build"));
  CHECK(!strcmp(ov.title, "True First Person") && !strcmp(ov.id, platform.panel_id) && ov.def.kind == OV_PANEL_WINDOW && ov.def.size == sizeof ov.def);
  CHECK(ov.def.hotkey == 0x79 && TFP_PANEL_KEY == 0x79 && ov.def.draw && !(ov.def.window_flags & OV_WINDOW_NO_TITLE));
  CHECK(logged("Ready: settings panel") && logged("F10") && logged("Ready: menu: registered with the mod's own panel") && !logged("DBF-MCM as"));
  CHECK(!ov.open && !tfp_wire_panel_is_open(&wire) && !saves);

  /* ---- every option of the one list is drawn, on the pages MCM gets ---- */
  game_update();
  /* the editor key's retail default (F8, HellForge's own editor) is Page Up in the store once the menu is bound; that is
   * the bind's own doing and no change to save: no file is written until the user changes something */
  CHECK(!saves && !tfp_wire_settings_unsaved(&wire) && tfp_settings_menu_number(s, TFP_MENU_editor_key) == 33);
  game_update();
  CHECK(!saves);
  ov.hover = true;
  draw_frame();
  CHECK(drawn("tab:First person") && drawn("tab:Bob and sway") && drawn("tab:Locked view"));
  int count = 0, sliders = 0, checks_drawn = 0, keys = 0, described = 0, choices = 0;
  const TfpMenuControl *controls = tfp_wire_menu_controls(&count);
  CHECK(count == 44);
  for (int i = 0; i < count; i++) {
    const TfpMenuControl *c = &controls[i];
    char line[420];
    TfpMenuValue v = {0, false, 0};
    tfp_wire_menu_default(&wire, c, 33, &v);
    switch (c->kind) {
      case TFP_MK_BUTTON: CHECK(drawn_part(c->label) && (strstr(drawn_part(c->label), "button:") || strstr(drawn_part(c->label), "primary:"))); break;
      case TFP_MK_TOGGLE: case TFP_MK_CHECK:
        if (strcmp(c->id, "show_aim_markers")) {
          snprintf(line, sizeof line, "check:%s=%s", c->label, v.boolean ? "on" : "off");
          if (!drawn(line)) snprintf(line, sizeof line, "check:%s=%s (disabled)", c->label, v.boolean ? "on" : "off");
          CHECK(drawn(line));
        }
        else CHECK(drawn_part("check:Show aim markers="));
        checks_drawn++; break;
      case TFP_MK_SLIDER: {
        snprintf(line, sizeof line, "slider:%s=", c->label);
        const char *row = drawn_part(line);
        snprintf(line, sizeof line, "[%g..%g]", c->min, c->max);
        CHECK(row && strstr(row, line));
        sliders++; break;
      }
      case TFP_MK_CHOICE: snprintf(line, sizeof line, "combo:%s=%s", c->label, c->choices[(int)v.number - 1]); CHECK(drawn(line)); choices++; break;
      case TFP_MK_KEY:
        snprintf(line, sizeof line, "text:%s", c->label); CHECK(drawn(line));
        snprintf(line, sizeof line, "button:Change##%s", c->id); CHECK(drawn(line));
        keys++; break;
      case TFP_MK_STATUS: break;                                   /* the two status rows: below */
    }
    if (c->description) { snprintf(line, sizeof line, "tip:%s", c->description); CHECK(drawn_part(line)); described++; }
  }
  CHECK(sliders == 16 && checks_drawn == 16 && keys == 2 && choices == 3 && described == 31);
  CHECK(drawn("text:First person through every action, one field of view, and how much your body's movement moves the view."));
  CHECK(drawn("primary:Enable True First Person") && drawn("cap:F11") && drawn("cap:PAGE UP"));
  CHECK(drawn("slider:Weapon movement strength=120[0..200]") && drawn("slider:Near clip distance (cm)=15[5..40]") && drawn("slider:Weapon bob speed=100[25..200]"));
  CHECK(drawn("text:Status: First person off") && drawn_part("text:Weapon: enable First Person with a firearm"));
  CHECK(drawn("slider:This weapon: hip clearance (cm)=15[5..40] (disabled)") && drawn("button:Save clipping for this weapon (disabled)"));
  CHECK(drawn("check:Camera lock=on") && drawn("check:Field of view lock=on") && drawn("slider:Field of view (degrees)=80[50..120]") &&
        drawn("combo:Aim field of view=The game's zoom") && drawn("slider:Aim field of view (degrees)=50[20..90] (disabled)") &&
        drawn("text:Used when Aim field of view is A chosen value.") && drawn("slider:Sight distance in aim (cm)=0[0..30]") &&
        !drawn_part("Zoom when aiming") && drawn("slider:View kick strength (%)=35[0..100] (disabled)") && drawn("slider:View kick limit (degrees)=8[0..20] (disabled)") &&
        drawn("slider:View kick when thrown (%)=70[0..100]") && drawn("slider:Camera damping (%)=60[0..100] (disabled)") && drawn("check:Animated arms=on") && drawn("check:Weapon put-away=on") && drawn("check:Smooth eye=on"));
  /* The camera style. Bodycam is off (the Stable camera): its row is directly above the kick sliders, with what the
   * two styles are; the three sliders only Bodycam uses are greyed through the host, with one line that says so, and
   * the thrown slider, which both styles use, is not. */
  {
    const char *row = drawn("combo:Camera style=Stable"), *tip_line = drawn("tip:Stable: a steady, traditional first-person camera. Bodycam: the view moves with "
      "your head, like a body camera: it takes kicks from your body's movement and follows the head through actions.");
    const char *note = drawn("text:View kick strength, View kick limit and Camera damping apply to Bodycam.");
    const char *first = drawn("slider:View kick strength (%)=35[0..100] (disabled)"), *above = drawn("slider:Sight distance in aim (cm)=0[0..30]");
    CHECK(row && tip_line && note && first && above && above < row && row < tip_line && tip_line < note && note < first && first - note == (ptrdiff_t)sizeof ov.drawn[0]);
    CHECK(!drawn("slider:View kick strength (%)=35[0..100]") && !drawn("slider:View kick limit (degrees)=8[0..20]") && !drawn("slider:Camera damping (%)=60[0..100]") &&
          !drawn("slider:View kick when thrown (%)=70[0..100] (disabled)"));
    CHECK(drawn("tip:How much of your body's movement reaches the view while on foot. Default 35.") &&
          drawn("tip:How much of your body's movement reaches the view while you are thrown or ragdolled. The Stable camera uses half of it, as a slow sway. Default 70."));
  }
  CHECK(drawn("tip:The first-person arms follow your character's own animation.") &&
        drawn("tip:Keep first person while the weapon is put away for a swap, grenade or stim.") &&
        drawn("tip:No camera pops when the game changes its camera."));
  /* One-handed pistols sets nothing: greyed through the host, with a note that says where the behaviour comes from */
  CHECK(drawn("check:One-handed pistols=on (disabled)") && !drawn("check:One-handed pistols=on") && !drawn_part("Holster animation"));
  CHECK(drawn("text:Comes from your character's own animation while Animated arms is on.") &&
        drawn("tip:One-handed weapons are held as your character holds them. This comes from the character's own animation while Animated arms is on; there is nothing to set here."));
  CHECK(!ov.tab_bars && !ov.tab_items && !ov.disabled_depth && !ov.disabled && !ov.lock_depth && ov.locks && !ov.bad_colours);
  /* A greyed item answers "not hovered" in the host, so each greyed row's description is carried by an unseen item
   * laid over it, of its own rectangle and not greyed: the three Bodycam sliders, One-handed pistols, and the two
   * rows of the First person tab that need a weapon; and (round 20) the chosen aim field of view's slider while the
   * game's zoom is the answer. */
  CHECK(drawn("tip:The most the view is turned by a kick while on foot. Default 8.") &&
        drawn("tip:How much the view is steadied against your body's movement. 0 is the raw movement; higher is calmer and slower. Default 60."));
  CHECK(ov.covers == 7 && !ov.bad_covers);
  {
    const char *slider_row = drawn("slider:View kick strength (%)=35[0..100] (disabled)"), *tip_row = drawn("tip:How much of your body's movement reaches the view while on foot. Default 35.");
    CHECK(slider_row && tip_row && tip_row - slider_row == (ptrdiff_t)sizeof ov.drawn[0]);   /* directly after its slider */
  }
  ov.hover = false;
  draw_frame();
  CHECK(ov.covers == 14 && !ov.bad_covers && !drawn_part("tip:"));   /* laid over them in every frame; no tooltip without the mouse */
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
  CHECK(strstr(saved_text, "\"editor_key\": 33"));               /* the user's first change takes the Page Up along */
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
  /* the Locked view tab's options */
  ov.flip = "Camera lock";
  draw_frame(); game_update();
  CHECK(!tfp_locked_view_camera_lock(&wire) && saves == 7 && strstr(saved_text, "\"camera_lock\": false"));
  /* greyed while Bodycam is off: a drag on one of its three sliders does nothing */
  static const char *const greyed[] = {"View kick strength (%)", "View kick limit (degrees)", "Camera damping (%)"};
  for (size_t i = 0; i < COUNT(greyed); i++) {
    ov.slide = ov.release = greyed[i]; ov.slide_to = 15; draw_frame(); game_update();
    CHECK(ov.slide && saves == 7);
  }
  ov.slide = ov.release = NULL;
  CHECK(tfp_locked_view_kick_strength(&wire) == 35 && tfp_locked_view_kick_max_degrees(&wire) == 8 && tfp_locked_view_camera_damping(&wire) == 60);
  /* Bodycam on: saved, and from the next frame the three are ordinary sliders and the line is gone */
  ov.pick = "Camera style"; ov.pick_to = 1; draw_frame(); game_update();
  CHECK(tfp_locked_view_bodycam(&wire) && saves == 8 && strstr(saved_text, "\"bodycam\": true"));
  draw_frame();
  CHECK(drawn("combo:Camera style=Bodycam") && drawn("slider:View kick strength (%)=35[0..100]") && drawn("slider:View kick limit (degrees)=8[0..20]") &&
        drawn("slider:Camera damping (%)=60[0..100]") && drawn("slider:View kick when thrown (%)=70[0..100]") && !drawn_part("apply to Bodycam") &&
        !drawn_part("View kick strength (%)=35[0..100] (disabled)"));
  ov.slide = "View kick strength (%)"; ov.slide_to = 52; ov.active = true;
  draw_frame(); game_update();
  CHECK(tfp_locked_view_kick_strength(&wire) == 35);
  ov.release = "View kick strength (%)"; ov.active = false;
  draw_frame(); game_update();
  CHECK(tfp_locked_view_kick_strength(&wire) == 50 && saves == 9 && strstr(saved_text, "\"kick_strength\": 50"));
  static const struct { const char *label; bool (*get)(TfpWire *); } flags[] = {
    {"Field of view lock", tfp_locked_view_fov_lock},
    {"Animated arms", tfp_locked_view_animated_arms}, {"Weapon put-away", tfp_locked_view_put_away}, {"Smooth eye", tfp_locked_view_smooth_eye}};
  for (size_t i = 0; i < COUNT(flags); i++) {
    ov.flip = flags[i].label; draw_frame(); game_update();
    CHECK(!flags[i].get(&wire));
  }
  CHECK(strstr(saved_text, "\"animated_arms\": false") && strstr(saved_text, "\"put_away\": false") && strstr(saved_text, "\"smooth_eye\": false"));
  /* round 20: Aim field of view, a choice of three; A chosen value enables its slider, the old toggle follows in the file */
  ov.pick = "Aim field of view"; ov.pick_to = 2; draw_frame(); game_update();
  CHECK(tfp_locked_view_fov_aim(&wire) == 3 && !tfp_locked_view_fov_zoom_when_aiming(&wire) && strstr(saved_text, "\"fov_aim\": 3") &&
        strstr(saved_text, "\"fov_zoom_when_aiming\": false"));
  draw_frame();
  CHECK(drawn("combo:Aim field of view=A chosen value") && drawn("slider:Aim field of view (degrees)=50[20..90]") && !drawn("text:Used when Aim field of view is A chosen value."));
  CHECK(fabs(tfp_locked_view_aim_field(&wire) - 50 * 3.14159265358979 / 180) < 1e-6);
  ov.flip = "One-handed pistols"; draw_frame(); game_update();     /* greyed: a click does nothing */
  CHECK(tfp_locked_view_one_handed_pistols(&wire) && ov.flip);
  ov.flip = NULL;
  static const struct { const char *label; int to; double (*get)(TfpWire *); } numbers[] = {
    {"Field of view (degrees)", 103, tfp_locked_view_fov_degrees}, {"View kick limit (degrees)", 12, tfp_locked_view_kick_max_degrees},
    {"View kick when thrown (%)", 85, tfp_locked_view_thrown_kick_strength}, {"Camera damping (%)", 25, tfp_locked_view_camera_damping}};
  for (size_t i = 0; i < COUNT(numbers); i++) {
    ov.slide = ov.release = numbers[i].label; ov.slide_to = numbers[i].to; draw_frame(); game_update();
    CHECK(numbers[i].get(&wire) == numbers[i].to);
  }
  CHECK(saves == 18);
  draw_frame();
  CHECK(drawn("check:Camera lock=off") && drawn("slider:View kick strength (%)=50[0..100]") && drawn("slider:Field of view (degrees)=103[50..120]") &&
        drawn("slider:Camera damping (%)=25[0..100]") && strstr(saved_text, "\"camera_damping\": 25"));
  /* Bodycam off again while a slider of its three is being dragged: the drag is let go, nothing is stored from it, and
   * the sliders are greyed with the values they had */
  ov.slide = "Camera damping (%)"; ov.slide_to = 90; ov.active = true;
  draw_frame(); game_update();
  ov.pick = "Camera style"; ov.pick_to = 0; draw_frame(); game_update();
  CHECK(!tfp_locked_view_bodycam(&wire) && saves == 19 && strstr(saved_text, "\"bodycam\": false") && strstr(saved_text, "\"camera_damping\": 25"));
  ov.active = false; ov.release = "Camera damping (%)";
  draw_frame(); game_update();
  CHECK(tfp_locked_view_camera_damping(&wire) == 25 && saves == 19 && drawn("combo:Camera style=Stable") && drawn("slider:Camera damping (%)=25[0..100] (disabled)") &&
        drawn("slider:View kick strength (%)=50[0..100] (disabled)") && drawn("slider:View kick limit (degrees)=12[0..20] (disabled)") &&
        drawn("slider:View kick when thrown (%)=85[0..100]") && drawn_part("apply to Bodycam") && !ov.disabled_depth && !ov.disabled);
  ov.release = NULL;
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
  CHECK(drawn("check:First Person Arms=on (disabled)") && drawn("text:Sleeves and hand grips are refused on this build.") && !drawn_part("Fird") && drawn("button:Edit arms and weapon placement (disabled)") && drawn("button:Change##editor_key (disabled)"));
  CHECK(drawn("button:Change##toggle_key") && drawn("check:True First Person: show torso=off"));
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);

  return 0;
}

/* ---- which menu a session gets (tfp_wire_panel_tick) ---- */
static int choice_part(void) {
  TfpPanelPlatform mcm = platform;
  mcm.panel_forced = false;
  /* MCM ready at its first try: the menu is MCM's, no panel, the host is not even asked; MCM is asked again once a
   * second (a released registration is made again); F10 opens MCM, once per press */
  CHECK(start_wire());
  new_host(8); mcm_answer = TFP_MCM_READY;
  for (uint64_t ms = 0; ms < 3000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_MCM && mcm_ticks == 3 && !ov.connects && !ov.registered && !tfp_wire_panel_bound(&wire));
  CHECK(!mcm_shows && !logged("Menu: no settings menu"));
  f10_down = true;
  for (uint64_t ms = 3000; ms < 3500; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(mcm_shows == 1);                                          /* held: once */
  f10_down = false; tfp_wire_panel_tick(&wire, &mcm, 3600, 0, test_edit);
  f10_down = true; tfp_wire_panel_tick(&wire, &mcm, 3700, 0, test_edit);
  CHECK(mcm_shows == 2 && !tfp_wire_panel_is_open(&wire));
  /* a registration lost later and refused when made again (after a refused live replace): the panel takes over */
  mcm_answer = TFP_MCM_REFUSED;
  for (uint64_t ms = 4000; ms < 6000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_PANEL && ov.registered == 1 && tfp_wire_panel_bound(&wire));
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* F10 held down from the start counts only once it has been seen up */
  CHECK(start_wire());
  new_host(8); mcm_answer = TFP_MCM_READY; f10_down = true;
  for (uint64_t ms = 0; ms < 2000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(!mcm_shows);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* MCM late, but inside its 15 s (its first update after this mod's start): still MCM, no panel */
  CHECK(start_wire());
  new_host(8); mcm_ready_after = 6;
  for (uint64_t ms = 0; ms < 20000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_MCM && !ov.connects && !ov.registered && !logged("Menu:"));
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* MCM absent (or later than its 15 s): a try a second, then one line and the panel, with every page */
  CHECK(start_wire());
  new_host(8);
  for (uint64_t ms = 0; ms < 14900; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_DECIDING && mcm_ticks == 15 && !ov.connects);
  for (uint64_t ms = 14900; ms < 20000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_PANEL && mcm_ticks == 16 && ov.connects == 1 && ov.registered == 1 && tfp_wire_panel_bound(&wire));
  CHECK(log_count("DBF-MCM (goose.mcm) did not answer") == 1 && logged("Ready: settings panel") && logged("Ready: menu: registered with the mod's own panel"));
  mcm_ready_after = 0;                                           /* MCM coming after that changes nothing: the panel stays */
  for (uint64_t ms = 20000; ms < 25000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(mcm_ticks == 16 && tfp_wire_panel_choice(&wire) == TFP_CHOICE_PANEL);
  ov.hover = true; draw_frame(); game_update();
  CHECK(drawn("tab:First person") && drawn("tab:Bob and sway") && drawn("tab:Locked view") && drawn_part("slider:Field of view (degrees)="));
  f10_down = true; tfp_wire_panel_tick(&wire, &mcm, 26000, 0, test_edit);
  CHECK(!mcm_shows);                                             /* F10 is the panel's own key here (the host's) */
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* MCM refuses the registration: the panel at once */
  CHECK(start_wire());
  new_host(8); mcm_answer = TFP_MCM_REFUSED;
  for (uint64_t ms = 0; ms < 3000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_PANEL && mcm_ticks == 1 && ov.registered == 1 && tfp_wire_panel_bound(&wire));
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* panel_on.txt: MCM is not tried, even when it is ready */
  CHECK(start_wire());
  new_host(8); mcm_answer = TFP_MCM_READY;
  tfp_wire_panel_tick(&wire, &platform, 0, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_PANEL && !mcm_ticks && ov.registered == 1 && tfp_wire_panel_bound(&wire) && logged("panel_on.txt"));
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* no MCM in this build of the glue: the panel */
  CHECK(start_wire());
  new_host(8);
  TfpPanelPlatform none = mcm;
  none.mcm_try = NULL;
  tfp_wire_panel_tick(&wire, &none, 0, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_PANEL && ov.registered == 1);
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  /* neither MCM nor NEST: 15 s for each, then one line that says there is no menu and where the files are; nothing more,
   * nothing bound, no crash */
  CHECK(start_wire());
  new_host(8); ov.no_host = true;
  for (uint64_t ms = 0; ms < 60000; ms += 100) tfp_wire_panel_tick(&wire, &mcm, ms, 0, test_edit);
  CHECK(tfp_wire_panel_choice(&wire) == TFP_CHOICE_NONE && mcm_ticks == 16 && ov.connects == 16 && !tfp_wire_menu_bound(&wire));
  CHECK(log_count("Menu: no settings menu") == 1 && logged("DBF-MCM (goose.mcm) is not loaded") && logged("NEST (Goose Overlay Host) is not loaded") &&
        logged("edited by hand") && logged("%LOCALAPPDATA%\\Goose\\HellForge\\TrueFirstPerson"));
  game_update();
  CHECK(!tfp_wire_panel_is_open(&wire));
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
  return 0;
}

/* ---- the three live features: the option, the files, and when a change may be made ---- */
static const char *const F_ARMS = "Animated arms", *const F_HOLD = "Weapon put-away", *const F_SMOOTH = "Smooth eye";
static bool bind_panel(void) {
  if (!start_wire()) return false;
  new_host(8);
  if (!tfp_wire_panel_bind(&wire, &api, &platform, 0, test_edit)) return false;
  tfp_wire_panel_frame(&wire); tfp_wire_menu_frame(&wire); tfp_wire_settings_save_frame(&wire);
  return true;
}
static void end_panel(void) {
  tfp_wire_menu_unbind(&wire);
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);
}
static void flip(const char *label) { ov.flip = label; draw_frame(); game_update(); }
static int options_part(void) {
  /* ---- the start: every setter once, with the option's value (defaults: on), and a line each that names who decided ---- */
  CHECK(bind_panel());
  line_count = 0;
  CHECK(tfp_wire_options_wanted(&wire, &options, TFP_FEATURE_ARMS, NULL));
  tfp_wire_options_tick(&wire, &options);                          /* before the start: nothing */
  CHECK(feature(TFP_FEATURE_ARMS, 0, false) && feature(TFP_FEATURE_HOLD, 0, false) && feature(TFP_FEATURE_SMOOTH, 0, false));
  tfp_wire_options_start(&wire, &options);
  CHECK(feature(TFP_FEATURE_ARMS, 1, true) && feature(TFP_FEATURE_HOLD, 1, true) && feature(TFP_FEATURE_SMOOTH, 1, true));
  CHECK(logged("Option: Animated arms: on (the option on the Locked view tab)") && logged("Option: Weapon put-away: on (the option on the Locked view tab)") &&
        logged("Option: Smooth eye: on (the option on the Locked view tab)") && log_count("Option:") == 3);
  /* nothing changed: no setter is called again, however many updates pass */
  for (int i = 0; i < 5; i++) { draw_frame(); game_update(); }
  CHECK(feature(TFP_FEATURE_ARMS, 1, true) && feature(TFP_FEATURE_HOLD, 1, true) && feature(TFP_FEATURE_SMOOTH, 1, true) && log_count("Option:") == 3);
  /* ---- a change in the panel reaches the store, and the feature's setter at the next update: only that one ---- */
  ov.flip = F_ARMS; draw_frame();
  CHECK(tfp_locked_view_animated_arms(&wire) && feature(TFP_FEATURE_ARMS, 1, true));   /* drawn, not yet a game update */
  game_update();
  CHECK(!tfp_locked_view_animated_arms(&wire) && feature(TFP_FEATURE_ARMS, 2, false) && strstr(saved_text, "\"animated_arms\": false"));
  CHECK(feature(TFP_FEATURE_HOLD, 1, true) && feature(TFP_FEATURE_SMOOTH, 1, true));
  CHECK(logged("Option: Animated arms: switched off (the option on the Locked view tab)"));
  flip(F_ARMS);
  CHECK(feature(TFP_FEATURE_ARMS, 3, true) && logged("Option: Animated arms: switched on (the option on the Locked view tab)"));
  flip(F_HOLD);
  CHECK(!tfp_locked_view_put_away(&wire) && feature(TFP_FEATURE_HOLD, 2, false) && feature(TFP_FEATURE_ARMS, 3, true) && feature(TFP_FEATURE_SMOOTH, 1, true));
  flip(F_HOLD);
  CHECK(feature(TFP_FEATURE_HOLD, 3, true));
  flip(F_SMOOTH);
  CHECK(!tfp_locked_view_smooth_eye(&wire) && feature(TFP_FEATURE_SMOOTH, 2, false) && feature(TFP_FEATURE_ARMS, 3, true) && feature(TFP_FEATURE_HOLD, 3, true));
  flip(F_SMOOTH);
  CHECK(feature(TFP_FEATURE_SMOOTH, 3, true));
  /* ---- a lease held through an action: every change waits, one line says so, and it is made once the hold is over ---- */
  wire.controller.holding = true;
  CHECK(tfp_wire_options_busy(&wire));
  line_count = 0;
  flip(F_HOLD); flip(F_ARMS); flip(F_SMOOTH);
  for (int i = 0; i < 4; i++) game_update();
  CHECK(!tfp_locked_view_put_away(&wire) && !tfp_locked_view_animated_arms(&wire) && !tfp_locked_view_smooth_eye(&wire));   /* the store has them */
  CHECK(feature(TFP_FEATURE_HOLD, 3, true) && feature(TFP_FEATURE_ARMS, 3, true) && feature(TFP_FEATURE_SMOOTH, 3, true));   /* the features not yet */
  CHECK(log_count("waits until the action in progress is over") == 3 && logged("Option: Weapon put-away: switching off waits"));
  flip(F_ARMS);                                                    /* changed back while waiting: nothing is left to do for it */
  wire.controller.holding = false, wire.hold.was_holding = true;   /* the controller let go; the hold's own tick has not yet said so */
  CHECK(tfp_wire_options_busy(&wire));
  game_update();
  CHECK(feature(TFP_FEATURE_HOLD, 3, true));
  wire.hold.was_holding = false, wire.controller.phase = TFP_PHASE_CLEANUP_PENDING;   /* the lease waits for its native exit */
  CHECK(tfp_wire_options_busy(&wire));
  game_update();
  CHECK(feature(TFP_FEATURE_HOLD, 3, true) && feature(TFP_FEATURE_SMOOTH, 3, true));
  wire.controller.phase = TFP_PHASE_WAITING;
  CHECK(!tfp_wire_options_busy(&wire));
  game_update();
  CHECK(feature(TFP_FEATURE_HOLD, 4, false) && feature(TFP_FEATURE_SMOOTH, 4, false) && feature(TFP_FEATURE_ARMS, 3, true));
  CHECK(logged("Option: Weapon put-away: switched off") && !logged("Option: Animated arms: switched"));
  game_update();
  CHECK(feature(TFP_FEATURE_HOLD, 4, false) && feature(TFP_FEATURE_SMOOTH, 4, false));
  wire.controller.phase = TFP_PHASE_RUNNING;                       /* a lease that runs with no action: a change is made at once */
  flip(F_HOLD);
  CHECK(feature(TFP_FEATURE_HOLD, 5, true));
  end_panel();

  /* ---- a file forces the feature on, whatever the option says ---- */
  CHECK(bind_panel());
  flip(F_ARMS); flip(F_HOLD);                                      /* both options off, in the panel */
  CHECK(!tfp_locked_view_animated_arms(&wire) && !tfp_locked_view_put_away(&wire) && !features.calls[TFP_FEATURE_ARMS]);
  options.force_on[TFP_FEATURE_ARMS] = "arms_live.txt", options.force_on[TFP_FEATURE_HOLD] = "hold_lease.txt";
  const char *file = NULL;
  CHECK(tfp_wire_options_wanted(&wire, &options, TFP_FEATURE_ARMS, &file) && is(file, "arms_live.txt"));
  CHECK(tfp_wire_options_wanted(&wire, &options, TFP_FEATURE_SMOOTH, &file) && !file);
  line_count = 0;
  tfp_wire_options_start(&wire, &options);
  CHECK(feature(TFP_FEATURE_ARMS, 1, true) && feature(TFP_FEATURE_HOLD, 1, true) && feature(TFP_FEATURE_SMOOTH, 1, true));
  CHECK(logged("Option: Animated arms: on (test file arms_live.txt beside the DLL; the option on the Locked view tab is off and is not used)"));
  CHECK(logged("Option: Weapon put-away: on (test file hold_lease.txt beside the DLL; the option on the Locked view tab is off and is not used)"));
  CHECK(logged("Option: Smooth eye: on (the option on the Locked view tab)"));
  flip(F_ARMS); flip(F_HOLD);                                      /* the option on, off again: the file decides, no setter is called */
  CHECK(tfp_locked_view_animated_arms(&wire) && tfp_locked_view_put_away(&wire) && feature(TFP_FEATURE_ARMS, 1, true) && feature(TFP_FEATURE_HOLD, 1, true));
  flip(F_ARMS); flip(F_HOLD);
  CHECK(!tfp_locked_view_animated_arms(&wire) && !tfp_locked_view_put_away(&wire) && feature(TFP_FEATURE_ARMS, 1, true) && feature(TFP_FEATURE_HOLD, 1, true));
  end_panel();

  /* ---- a file forces the feature off, whatever the option says; with both files, off wins ---- */
  CHECK(bind_panel());
  options.force_off[TFP_FEATURE_ARMS] = "arms_off.txt", options.force_off[TFP_FEATURE_HOLD] = "hold_off.txt", options.force_off[TFP_FEATURE_SMOOTH] = "smooth_off.txt";
  options.force_on[TFP_FEATURE_HOLD] = "hold_lease.txt";
  CHECK(tfp_locked_view_animated_arms(&wire) && tfp_locked_view_put_away(&wire) && tfp_locked_view_smooth_eye(&wire));
  CHECK(!tfp_wire_options_wanted(&wire, &options, TFP_FEATURE_HOLD, &file) && is(file, "hold_off.txt"));
  line_count = 0;
  tfp_wire_options_start(&wire, &options);
  CHECK(feature(TFP_FEATURE_ARMS, 1, false) && feature(TFP_FEATURE_HOLD, 1, false) && feature(TFP_FEATURE_SMOOTH, 1, false));
  CHECK(logged("Option: Animated arms: off (test file arms_off.txt beside the DLL; the option on the Locked view tab is on and is not used)"));
  CHECK(logged("Option: Weapon put-away: off (test file hold_off.txt beside the DLL; the option on the Locked view tab is on and is not used)"));
  CHECK(logged("Option: Smooth eye: off (test file smooth_off.txt beside the DLL; the option on the Locked view tab is on and is not used)"));
  flip(F_ARMS); flip(F_HOLD); flip(F_SMOOTH);
  CHECK(!tfp_locked_view_animated_arms(&wire) && !tfp_locked_view_put_away(&wire) && !tfp_locked_view_smooth_eye(&wire));
  CHECK(feature(TFP_FEATURE_ARMS, 1, false) && feature(TFP_FEATURE_HOLD, 1, false) && feature(TFP_FEATURE_SMOOTH, 1, false));
  flip(F_ARMS); flip(F_HOLD); flip(F_SMOOTH);
  CHECK(tfp_locked_view_animated_arms(&wire) && tfp_locked_view_put_away(&wire) && tfp_locked_view_smooth_eye(&wire));
  CHECK(feature(TFP_FEATURE_ARMS, 1, false) && feature(TFP_FEATURE_HOLD, 1, false) && feature(TFP_FEATURE_SMOOTH, 1, false) && log_count("Option:") == 3);
  end_panel();

  /* ---- no settings store (it could not be made): the defaults, on; a missing setter is skipped ---- */
  new_wire();
  new_options();
  options.set[TFP_FEATURE_SMOOTH] = NULL;
  tfp_wire_options_start(&wire, &options);
  tfp_wire_options_tick(&wire, &options);
  CHECK(feature(TFP_FEATURE_ARMS, 1, true) && feature(TFP_FEATURE_HOLD, 1, true) && feature(TFP_FEATURE_SMOOTH, 0, false) && log_count("Option:") == 3);
  return 0;
}

/* A settings.json that was refused at load (or one that could not be opened) is not replaced by anything the user did
 * not do: binding the panel, whose rows hold the store's values normalised (119 -> Page Up, a slider inside its range),
 * writes nothing and copies nothing. The user's first change does both. */
static int untouched_part(void) {
  static const char broken[] = "{\"format\":1,\"first_person\":{},\"menu\":{\"motion_strength\":120,";
  static const char clamped[] = "{\"format\":1,\"first_person\":{},\"menu\":{\"motion_strength\":900,\"motion_preset\":7}}";
  for (int round = 0; round < 3; round++) { /* 0: refused; 1: the user's file did not open; 2: read, with values outside the controls' ranges */
    new_wire();
    CHECK(tfp_wire_settings_bind(&wire, round == 2 ? clamped : broken, (round == 2 ? sizeof clamped : sizeof broken) - 1, "test") && (logged("Refused: settings") != 0) == (round != 2));
    tfp_wire_settings_disk(&wire, &test_disk, round != 1);
    saves = copies = 0, copy_there = true, mcm_ticks = 0, edits = 0;
    new_options();
    new_host(8);
    for (uint64_t ms = 0; ms < 3000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
    CHECK(tfp_wire_panel_bound(&wire) && tfp_wire_menu_bound(&wire));
    for (int i = 0; i < 5; i++) { draw_frame(); game_update(); }
    CHECK(!saves && !copies && !tfp_wire_settings_unsaved(&wire) && !logged("Saved") && !logged("Kept"));
    CHECK(tfp_wire_menu_editor_key(&wire, 33) == 33);
    /* the user's own change: now the copy, then the write */
    ov.hover = true;
    ov.slide = "Weapon movement strength"; ov.slide_to = 60; ov.active = true;
    draw_frame(); game_update();
    ov.release = "Weapon movement strength"; ov.active = false;
    draw_frame(); game_update();
    CHECK(saves == 1 && saved_file == TFP_SETTINGS_FILE && strstr(saved_text, "\"motion_strength\": 60") && strstr(saved_text, "\"editor_key\": 33"));
    CHECK(copies == (round == 2 ? 0u : 1u) && (round == 2 || !strcmp(copy_suffix, round ? ".unread" : ".refused")));
    end_panel();
  }
  copy_there = false;
  return 0;
}

/* A key bound in the panel is still down when the game thread first polls it (src/mod_view.c): it must not act
 * until it has been let go once. */
static int rebind_part(void) {
  TfpKeyEdge toggle = {0}, editor = {0};
  CHECK(bind_panel());
  /* the keys as they were at the start: an ordinary press acts once, and not again while held */
  CHECK(tfp_wire_menu_toggle_key(&wire) == 122 && tfp_wire_menu_editor_key(&wire, 33) == 33);
  CHECK(!tfp_wire_menu_key_edge(&toggle, 122, false) && tfp_wire_menu_key_edge(&toggle, 122, true) && !tfp_wire_menu_key_edge(&toggle, 122, true));
  CHECK(!tfp_wire_menu_key_edge(&toggle, 122, false) && tfp_wire_menu_key_edge(&toggle, 122, true) && !tfp_wire_menu_key_edge(&toggle, 122, false));
  CHECK(!tfp_wire_menu_key_edge(&editor, 33, false));
  /* the toggle key is bound to T: the panel takes the key on its way down, the game thread stores it ... */
  ov.click = "Change##toggle_key"; draw_frame(); game_update();
  ov.key = 'T'; draw_frame(); game_update(); ov.key = 0;
  CHECK(tfp_wire_menu_toggle_key(&wire) == 'T');
  /* ... and T is still down at the next polls: no toggle. Let go, then pressed: one toggle. */
  CHECK(!tfp_wire_menu_key_edge(&toggle, tfp_wire_menu_toggle_key(&wire), true));
  CHECK(!tfp_wire_menu_key_edge(&toggle, 'T', true) && !tfp_wire_menu_key_edge(&toggle, 'T', false));
  CHECK(tfp_wire_menu_key_edge(&toggle, 'T', true) && !tfp_wire_menu_key_edge(&toggle, 'T', true));
  /* the editor's key likewise */
  ov.click = "Change##editor_key"; draw_frame(); game_update();
  ov.key = 'P'; draw_frame(); game_update(); ov.key = 0;
  CHECK(tfp_wire_menu_editor_key(&wire, 33) == 'P');
  CHECK(!tfp_wire_menu_key_edge(&editor, 'P', true) && !tfp_wire_menu_key_edge(&editor, 'P', false) && tfp_wire_menu_key_edge(&editor, 'P', true));
  /* cleared (0) and bound again while the new key is held: still nothing until it is let go */
  CHECK(!tfp_wire_menu_key_edge(&editor, 0, false) && !tfp_wire_menu_key_edge(&editor, 'K', true) && !tfp_wire_menu_key_edge(&editor, 'K', false) &&
        tfp_wire_menu_key_edge(&editor, 'K', true));
  end_panel();

  /* ---- after a live replace the old build's panel still holds the id (the host cannot take a panel out and refuses
   * a second one with the same id): this load registers under its own id, and says so ---- */
  CHECK(start_wire());
  new_host(8); ov.taken = platform.panel_id;
  for (uint64_t ms = 0; ms < 3000; ms += 100) tfp_wire_panel_tick(&wire, &platform, ms, 0, test_edit);
  CHECK(ov.connects == 1 && ov.registered == 2 && tfp_wire_panel_bound(&wire) && tfp_wire_menu_bound(&wire) && !logged("Refused"));
  CHECK(!strcmp(ov.id, "goose.true-first-person/settings@test") && !strcmp(ov.id, platform.panel_id_reload) && ov.def.hotkey == TFP_PANEL_KEY);
  CHECK(log_count("replaced build") == 1 && logged("Ready: settings panel"));
  ov.hover = true;
  draw_frame(); game_update();
  CHECK(drawn("tab:Locked view"));
  end_panel();
  /* no second id to offer: refused as before, and the menu is free again */
  CHECK(start_wire());
  new_host(8); ov.taken = platform.panel_id;
  TfpPanelPlatform one_id = platform;
  one_id.panel_id_reload = NULL;
  CHECK(!tfp_wire_panel_bind(&wire, &api, &one_id, 0, test_edit) && ov.registered == 1 && logged("did not take the panel"));
  CHECK(!tfp_wire_panel_bound(&wire) && !tfp_wire_menu_bound(&wire));
  tfp_wire_panel_free(&wire);
  tfp_wire_settings_free(&wire);

  /* ---- F8 for the placement editor: a stored 119 means "the default" (Page Up here), so the row would show F8 while
   * Page Up stayed the key. The panel does not take it, says why in the row, and goes on asking. ---- */
  CHECK(bind_panel());
  ov.click = "Change##editor_key"; draw_frame(); game_update();
  draw_frame();
  CHECK(drawn_part("Press a key") && !drawn_part("F8 is HellForge's own editor key"));
  ov.key = 119; draw_frame(); game_update(); ov.key = 0;
  CHECK(tfp_settings_menu_number(tfp_wire_settings(&wire), TFP_MENU_editor_key) == 33 && tfp_wire_menu_editor_key(&wire, 33) == 33 && !saves);
  draw_frame();
  CHECK(drawn("cap:PAGE UP") && !drawn("cap:F8") && drawn_part("F8 is HellForge's own editor key") && drawn_part("Press a key") && !ov.bad_colours);
  ov.key = 0x76; draw_frame(); game_update(); ov.key = 0;         /* F7: taken */
  draw_frame();
  CHECK(tfp_wire_menu_editor_key(&wire, 33) == 0x76 && drawn("cap:F7") && !drawn_part("HellForge's own editor key") && !drawn_part("Press a") && saves == 1);
  ov.click = "Change##editor_key"; draw_frame();                   /* asked again: the plain question, not the refusal */
  draw_frame();
  CHECK(drawn_part("Press a key") && !drawn_part("HellForge's own editor key"));
  ov.key = 27; draw_frame(); ov.key = 0;
  /* the toggle key may be F8 */
  ov.click = "Change##toggle_key"; draw_frame();
  ov.key = 119; draw_frame(); game_update(); ov.key = 0;
  draw_frame();
  CHECK(tfp_wire_menu_toggle_key(&wire) == 119 && drawn("cap:F8") && !drawn_part("Press a"));
  end_panel();
  /* where F8 is the editor's own key (the platform says a stored 119 means 119), the row takes it */
  CHECK(start_wire());
  new_host(8);
  TfpPanelPlatform retail = platform;
  retail.editor_key = 119;
  CHECK(tfp_wire_panel_bind(&wire, &api, &retail, 0, test_edit));
  game_update();
  ov.click = "Change##editor_key"; draw_frame();
  ov.key = 0x76; draw_frame(); game_update(); ov.key = 0;
  ov.click = "Change##editor_key"; draw_frame();
  ov.key = 119; draw_frame(); game_update(); ov.key = 0;
  draw_frame();
  CHECK(tfp_wire_menu_editor_key(&wire, 119) == 119 && drawn("cap:F8") && !drawn_part("Press a"));
  end_panel();
  return 0;
}

int main(void) {
  if (store_part() || panel_part() || choice_part() || options_part() || untouched_part() || rebind_part()) return 1;
  printf("wire panel: %u checks passed\n", checks);
  return 0;
}
