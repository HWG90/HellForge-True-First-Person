/* The feel build's join (src/wire_feel.c) and the motion binder's adapters (src/wire_motion.c),
 * run with no game behind them: every read fails, the engine's script state never arrives, the pose
 * helper is a scripted table. What this proves: all five module binders can be bound together, the
 * runtime's on_enable and its per-update order run through them without a fault, each module that
 * cannot work says so, and the adapters with logic of their own (snapshot and action-state shapes,
 * presentation:action / :resume, the Feel line, the markers table) answer as written.
 * It does not prove anything about the game: that is docs/wiring/WIRING-FEEL.md, "unproven". */
#include "wire_feel.h"
#include "wire_off.h"
#include "feel_bound.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("wire feel: FAILED %s (line %d)\n", #x, __LINE__); for (int i_ = 0; i_ < line_count; i_++) printf("  log: %s\n", lines[i_]); return 1; } } while (0)
static char lines[256][900];
static int line_count;
static void capture(void *user, const char *text) { (void)user; if (line_count < 256) snprintf(lines[line_count++], sizeof lines[0], "%s", text); }
static bool logged(const char *part) { for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) return true; return false; }

/* ---- a host with no game behind it ---- */
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
static int64_t fake_tick_count;
static int64_t fake_ticks(void) { return fake_tick_count += 3; } /* round 32: the laps' clock */
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

/* ---- a pose helper that is never reached (no game: the backend refuses before prepare) ---- */
static unsigned pose_calls;
static uint32_t p_config(const TfpPoseConfig *c) { (void)c; pose_calls++; return 1; }
static uint32_t p_begin(const TfpPoseConfig *c, uint64_t *g) { (void)c; (void)g; pose_calls++; return 0; }
static uint32_t p_xyz(uint64_t g, float x, float y, float z) { (void)g; (void)x; (void)y; (void)z; pose_calls++; return 0; }
static uint32_t p_transform(uint64_t g, float a, float b, float c, float d, float e, float f, float h) {
  (void)g; (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)h; pose_calls++; return 0;
}
static uint32_t p_u32(uint64_t g, uint32_t v) { (void)g; (void)v; pose_calls++; return 0; }
static uint32_t p_transition(uint64_t g, uint64_t e, float p, uint32_t a) { (void)g; (void)e; (void)p; (void)a; pose_calls++; return 0; }
static uint32_t p_end(uint64_t g) { (void)g; pose_calls++; return 0; }
static uint32_t p_stats(uint64_t g, TfpPoseHelperStats *s) { (void)g; (void)s; pose_calls++; return 0; }
static uint32_t p_depth(uint64_t g, TfpPoseDepthStats *s) { (void)g; (void)s; pose_calls++; return 0; }
static uint32_t p_input(uint64_t g, TfpPoseInputStats *s) { (void)g; (void)s; pose_calls++; return 0; }
static uint32_t p_error(void) { return 7; }
static uint64_t pose_tick(void *u) { (void)u; return 1000; }

/* ---- an engine whose script state never arrives ---- */
static bool e_ready(void *u) { (void)u; return false; }
static uint64_t e_generation(void *u) { (void)u; return 0; }
static const char *e_error(void *u) { (void)u; return "no engine"; }
static bool e_has(void *u, const char *p) { (void)u; (void)p; return false; }
static uintptr_t e_cfunction(void *u, const char *p) { (void)u; (void)p; return 0; }
static bool e_call(void *u, const char *p, const char *s, va_list a) { (void)u; (void)p; (void)s; (void)a; return false; }
static void e_unref(void *u, TfpEngRef *r) { (void)u; *r = 0; }
static uintptr_t e_pointer(void *u, TfpEngRef r) { (void)u; (void)r; return 0; }
static int e_type(void *u, TfpEngRef r) { (void)u; (void)r; return -1; }
static bool e_boolean(void *u, TfpEngRef r, bool *o) { (void)u; (void)r; (void)o; return false; }
static int e_len(void *u, TfpEngRef t) { (void)u; (void)t; return 0; }
static bool e_index(void *u, TfpEngRef t, int i, TfpEngRef *o) { (void)u; (void)t; (void)i; (void)o; return false; }
static int e_array(void *u, TfpEngRef t, uintptr_t *o, int m) { (void)u; (void)t; (void)o; (void)m; return -1; }
static double e_ticks(void *u) { (void)u; return now_seconds; }
static uint64_t e_exe(void *u) { (void)u; return 0; }

/* ---- an effects platform with no engine and no game ---- */
static bool f_ready(void *u, const char **why) { (void)u; *why = "no engine"; return false; }
static uint64_t f_zero(void *u) { (void)u; return 0; }
static uint64_t f_function(void *u, const char *p) { (void)u; (void)p; return 0; }
static bool f_table(void *u, const char *n) { (void)u; (void)n; return false; }
#define NO(e) do { *(e) = "no engine"; return false; } while (0)
static bool f_render_setting(void *u, const char *k, int *v, const char **e) { (void)u; (void)k; (void)v; NO(e); }
static TfpFxSet f_set_render_setting(void *u, const char *k, bool v, const char **e) { (void)u; (void)k; (void)v; *e = "no engine"; return TFP_FX_SET_RAISED; }
static bool f_main_world(void *u, uint64_t *w, const char **e) { (void)u; (void)w; NO(e); }
static bool f_entities(void *u, uint64_t w, uint64_t *l, uint32_t c, TfpVignetteList *k, uint32_t *n, const char **e) { (void)u; (void)w; (void)l; (void)c; (void)k; (void)n; NO(e); }
static bool f_component(void *u, uint64_t w, uint64_t *m, const char **e) { (void)u; (void)w; (void)m; NO(e); }
static bool f_alive(void *u, uint64_t x, bool *a, const char **e) { (void)u; (void)x; (void)a; NO(e); }
static bool f_instances(void *u, uint64_t m, uint64_t x, double *i, uint32_t c, uint32_t *n, const char **e) { (void)u; (void)m; (void)x; (void)i; (void)c; (void)n; NO(e); }
static bool f_get(void *u, uint64_t m, uint64_t x, double i, const char *p, TfpVignetteValue *o, const char **e) { (void)u; (void)m; (void)x; (void)i; (void)p; (void)o; NO(e); }
static bool f_set(void *u, uint64_t m, uint64_t x, double i, const char *p, bool v, bool *r, const char **e) { (void)u; (void)m; (void)x; (void)i; (void)p; (void)v; (void)r; NO(e); }
static bool f_find(void *u, bool i, const char *n, bool *h, double *k, const char **e) { (void)u; (void)i; (void)n; (void)h; (void)k; NO(e); }
static bool f_button(void *u, double k, bool *h, double *v, const char **e) { (void)u; (void)k; (void)h; (void)v; NO(e); }
static bool f_flag(void *u, bool *o, const char **e) { (void)u; (void)o; NO(e); }
static size_t f_query(void *u, uint64_t a, void *r, size_t s) { (void)u; (void)a; (void)r; (void)s; return 0; }
static int f_write(void *u, uint64_t a, const uint8_t *b, size_t s, size_t *w) { (void)u; (void)a; (void)b; (void)s; (void)w; return 0; }
static bool f_widget(void *u, uint64_t f, uint64_t w, uint8_t s, const char **e) { (void)u; (void)f; (void)w; (void)s; NO(e); }

static TfpWire wire;
static TfpWireFeel feel;
static bool text_is(TfpRtValue v, const char *text) { return v.kind == TFP_RT_TEXT && v.length == strlen(text) && !memcmp(v.text, text, v.length); }

/* The placement editor's platform, scripted: what the overlay host and DBF-MCM would answer. */
static struct { bool available, menu_open, busy, acquired, focused; int closes, draws, released, last_count; TfpEditorSample sample; bool down[256]; } ed;
static bool ed_available(void *u) { (void)u; return ed.available; }
static bool ed_menu_close(void *u) { (void)u; ed.closes++; ed.menu_open = false; return true; }
static bool ed_menu_is_open(void *u) { (void)u; return ed.menu_open; }
static bool ed_busy(void *u) { (void)u; return ed.busy; }
static bool ed_acquire(void *u) { (void)u; ed.acquired = true; return true; }
static bool ed_release(void *u) { (void)u; ed.acquired = false; ed.released++; return true; }
static bool ed_focused(void *u) { (void)u; return ed.focused; }
static bool ed_sample(void *u, TfpEditorSample *out) { (void)u; *out = ed.sample; return true; }
static bool ed_down(void *u, int key) { (void)u; return ed.down[key & 255]; }
static bool ed_resolution(void *u, double *w, double *h) { (void)u; *w = 1920; *h = 1080; return true; }
static void ed_draw(void *u, const TfpEdCommand *c, int n) { (void)u; (void)c; ed.draws++; ed.last_count = n; }
static bool ed_window(void *u, bool *focus, bool *cursor) { (void)u; *focus = true; *cursor = false; return true; }
static void ed_weapons(void *u, const TfpEwWeapon *list, int count) { (void)u; (void)list; (void)count; }   /* round 53: only with a drawn frame, which needs a game */
static const TfpEditorPlatform editor_platform = {NULL, ed_available, ed_menu_close, ed_menu_is_open, ed_busy, ed_acquire, ed_release, ed_focused,
                                                  ed_sample, ed_down, ed_resolution, ed_draw, ed_window, ed_weapons};

int main(void) {
  static const TfpCameraHelperApi camera = {h_validate_c, h_begin_c, h_end, h_stats_c, h_error, h_generation, h_handoff, h_u32, h_u32};
  static const TfpFovHelperApi fov = {h_validate_f, h_begin_f, h_bind_f, h_transition_f, h_end, h_stats_f, h_error};
  static const TfpNearHelperApi near_clip = {h_begin_n, h_set_n, h_end, h_stats_n, h_error};
  static const TfpBlurHelperApi blur = {h_validate_b, h_begin_b, h_end, h_stats_b, h_error};
  static const TfpPoseHelperApi pose = {p_config, p_config, p_begin, p_begin, p_begin, p_begin, p_xyz, p_xyz, p_transform, p_transform, p_u32,
                                        p_transition, p_end, p_stats, p_depth, p_input, p_error};
  static const TfpPoseAdapterIo pose_io = {NULL, NULL, pose_tick};
  static const TfpWireEngineApi engine = {NULL, e_ready, e_generation, e_error, e_has, e_cfunction, e_call, e_unref, e_pointer, e_type, e_boolean,
                                          e_len, e_index, e_array, e_ticks, e_exe};
  static const TfpFxPlatform effects = {NULL, f_ready, f_zero, f_function, f_table, f_zero, f_render_setting, f_set_render_setting, f_main_world,
                                        f_entities, f_component, f_alive, f_instances, f_get, f_set, f_find, f_button, f_flag, f_flag, f_query,
                                        f_write, f_widget, NULL};
  TfpOpsHost memory;
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = no_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  TfpWirePlatform platform = {NULL, test_clock, test_mouse, capture};
  TfpWireHelpers helpers = {&camera, &fov, &near_clip, &blur};
  TfpRtValue r[30];

  /* ---- the motion binder's two shape conversions ---- */
  TfpOpsSnapshot ops;
  TfpSnapshot snap;
  memset(&ops, 0, sizeof ops);
  ops.base = 0x1000, ops.camera = 0x2000, ops.player = 0x3000, ops.body = 11, ops.viewmodel = 12, ops.weapon = 13, ops.weapon_unit = 14;
  ops.camera_kind = 6, ops.safe = true, ops.aiming = true, ops.full = true, ops.avatar_index = 2, ops.avatar_owner = 0x6000;
  memcpy(ops.controller_bytes, "\x01\x02\x03\x04\x05\x06", 6);
  tfp_wire_motion_snapshot(&ops, &snap);
  CHECK(snap.base == 0x1000 && snap.camera == 0x2000 && snap.player == 0x3000 && snap.body == 11 && snap.viewmodel == 12 && snap.weapon == 13);
  CHECK(snap.weapon_unit == 14 && snap.camera_kind == 6 && snap.safe && snap.aiming && snap.full && snap.avatar_index == 2 && snap.avatar_owner == 0x6000);
  CHECK(snap.mask == 0x04030201u && !memcmp(snap.controller_bytes, ops.controller_bytes, 6));
  TfpOpsAction action;
  TfpActionState state;
  memset(&action, 0, sizeof action);
  action.body = 11, action.layer_count = 20, action.resource_match = true;
  memcpy(action.resource, "\x11\x22\x33\x44\x55\x66\x77\x88", 8);
  action.sampled = 1u << 1 | 1u << 3 | 1u << 14, action.unknown = 1u << 14;
  action.states[1] = 75, action.states[3] = 71, action.states[14] = 9;
  tfp_wire_motion_action(&action, &snap, &state);
  CHECK(!strcmp(state.resource, "8877665544332211") && state.resource_match && state.layer_count == 20 && state.player.body == 11);
  CHECK(state.has[1] && state.has[3] && state.has[14] && !state.has[2] && state.states[3] == 71 && state.unknown_count == 1 && state.unknown_layers[0] == 14);

  /* ---- everything bound, no game ---- */
  tfp_wire_init(&wire, &memory, &platform, &helpers);
  TfpRtHost off = wire.host;
  static const char locked_json[] = "{\"format\":1,\"locked_view\":{\"kick_max_degrees\":11}}";
  TfpWireFeelParts parts = {NULL, 0, "settings.json", &pose, &pose_io, &engine, &effects, &editor_platform, locked_json, sizeof locked_json - 1, "locked_view.json"};
  tfp_wire_feel_bind(&wire, &feel, &parts);
  CHECK(feel.has_pose && feel.has_motion && feel.has_engine && feel.has_effects && feel.has_editor && logged("Ready: placement editor bound"));
  CHECK(logged("Refused: settings:") && logged("Ready: settings are retail's defaults")); /* no file */
  CHECK(logged("Ready: locked view options read from locked_view.json") && tfp_locked_view_kick_max_degrees(&wire) == 11); /* its own file, read all the same */
  CHECK(logged("Ready: pose") && logged("Ready: motion bound") && logged("Ready: model shadows"));
  TfpRtHost *h = &wire.host;
  CHECK(wire.pose_helper && wire.placement_decode && wire.lease_extra && wire.controller_extra && wire.frame_extra && wire.pose_counts);
  CHECK(h->mod_set && h->config_field != off.config_field && h->actions_resolve != off.actions_resolve && h->motion_update != off.motion_update);
  CHECK(h->transition_action != off.transition_action && h->head_update != off.head_update && h->arm_pieces_new && h->grip_hand_new);
  CHECK(h->dof_update != off.dof_update && h->movement_update != off.movement_update && h->native_aim_update != off.native_aim_update);
  CHECK(h->aim_markers_update != off.aim_markers_update && h->helper_depth_stats != off.helper_depth_stats);
  /* what stays the view build's off answer */
  CHECK(h->editor_request_open != off.editor_request_open && h->editor_update != off.editor_update && h->compat_update == off.compat_update &&
        h->fird_start == off.fird_start);
  CHECK(h->head_positioner_start == off.head_positioner_start && h->camera_free_aim == off.camera_free_aim && h->menu_update == off.menu_update);

  /* tests/feel_bound.h is exactly what the binders replaced: every pointer of wire_off.h's lists is
   * either still wire_off.c's own function or named there (an optional pointer: NULL before, set now). */
#define SAME(name, ...) \
  if (((const void *)h->name != (const void *)off.name) != feel_bound(#name)) { printf("wire feel: %s is %s but feel_bound says otherwise\n", #name, (const void *)h->name != (const void *)off.name ? "bound" : "still off"); mismatches++; }
  int mismatches = 0;
  TFP_OFF_S0(SAME) TFP_OFF_S1(SAME) TFP_OFF_S2(SAME) TFP_OFF_ST(SAME) TFP_OFF_G0(SAME)
  SAME(menu_fird_person_arms) SAME(menu_show_torso) SAME(menu_near_options) SAME(menu_motion_options) SAME(arm_pieces_new) SAME(grip_hand_new)
  SAME(head_sdk_handles) SAME(camera_is_pinned) SAME(camera_look_stats) SAME(mod_set) SAME(actions_resolve) SAME(actions_saved)
  SAME(actions_default_placement) SAME(actions_weapon_info) SAME(actions_observed) SAME(config_field) SAME(config_action_mapped)
  SAME(helper_depth_stats) SAME(helper_input_stats) SAME(backend_prepare_pose) SAME(backend_alignment_context) SAME(backend_pose_status)
  SAME(arm_pieces_update) SAME(grip_hand_update) SAME(aim_markers_toggle) SAME(aim_markers_set_setting) SAME(motion_capture_neutral)
  SAME(editor_cancel) SAME(camera_free_aim) SAME(head_positioner_save) SAME(fird_start) SAME(ref_field)
#undef SAME
  CHECK(mismatches == 0);

  tfp_wire_feel_begin(&wire);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_update, NULL, r) == TFP_RT_OK);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK);
  CHECK(!logged("Idle. F11") && wire.rt.has[TFP_RT_O_CONTROLLER] && wire.rt.has[TFP_RT_O_MOTION]); /* round 36b: retail's Idle line is not written (src/wire_rt.c) */
  CHECK(wire.ops.has_pose_placement && wire.ops.pose_placement.x == .15 && wire.ops.pose_placement.z == -.12);
  for (int i = 0; i < 90; i++) {
    now_seconds += 1 / 60.0;
    tfp_wire_feel_begin(&wire);
    tfp_wire_frame(&wire, NULL);
  }
  CHECK(!wire.controller.enabled && !logged("the frame raised") && !logged("Feel:")); /* off: no Feel line */
  wire.toggle_requested = true; /* the toggle key with no game: retail's own refusal; nothing reaches the pose helper */
  now_seconds += 1 / 60.0;
  tfp_wire_feel_begin(&wire);
  tfp_wire_frame(&wire, NULL);
  CHECK(logged("State: enter requested (toggle key)") && logged("Refused: toggle key enter: Start refused: game.dll unavailable") && !pose_calls);
  CHECK(!wire.controller.enabled && !wire.controller.lease);

  /* ---- round 32: the laps of the Perf frame line (wire.h); from here on every frame runs with them ---- */
  TfpRtValue o1, o2;
  {
    const TfpRtHost before = wire.host;
    CHECK(!wire.laps.clock);
    for (int i = 0; i < TFP_LAPS; i++) CHECK(wire.laps.tick[i] == 0); /* no clock: nothing timed */
    tfp_wire_laps_start(&wire, fake_ticks);
    tfp_wire_laps_start(&wire, fake_ticks); /* a second start does not wrap the wrappers */
    CHECK(h->fov_update != before.fov_update && wire.laps.update[0] == before.fov_update && wire.laps.update[1] == before.blur_update);
    CHECK(wire.laps.update[2] == before.camera_update && wire.laps.update[3] == before.head_update && wire.laps.update[4] == before.movement_update);
    CHECK(wire.laps.update[5] == before.near_update && wire.laps.update[6] == before.dof_update);
    CHECK(h->camera_stop == before.camera_stop && h->fov_result == before.fov_result); /* only the seven updates */
    const int lines_before = line_count;
    now_seconds += 1 / 60.0;
    tfp_wire_feel_begin(&wire);
    tfp_wire_frame(&wire, NULL);
    CHECK(wire.laps.tick[TFP_LAP_UPDATE] > 0 && wire.laps.tick[TFP_LAP_EXTRA] > 0 && wire.laps.tick[TFP_LAP_HOLD] > 0 && wire.laps.tick[TFP_LAP_VIEW_LOG] > 0);
    CHECK(wire.laps.tick[TFP_LAP_POSE] > 0 && wire.laps.tick[TFP_LAP_MOTION] > 0 && wire.laps.tick[TFP_LAP_ENGINE] > 0 && wire.laps.tick[TFP_LAP_EFFECTS] > 0);
    CHECK(line_count == lines_before); /* idle: the laps say nothing */
    { /* round 35: on_update's parts add up to its lap, the runtime marked its parts, the snapshot calls are timed */
      int64_t parts = 0;
      for (int i = 0; i < TFP_UPDS; i++) { CHECK(wire.laps.part[i] >= 0); parts += wire.laps.part[i]; }
      CHECK(parts == wire.laps.tick[TFP_LAP_UPDATE] && !wire.laps.in_update);
      CHECK(wire.laps.part[TFP_UPD_ENTRY] > 0 && wire.laps.part[TFP_UPD_MENU] > 0 && wire.laps.part[TFP_UPD_DIAGNOSTICS] > 0 &&
            wire.laps.part[TFP_UPD_FRAME] > 0);
      CHECK(wire.rt.lap && wire.rt.lap_user == &wire && wire.ops.perf.clock == fake_ticks);
    }
    /* each wrapper answers what the target's own update answers, and times it */
    static const int laps[7] = {TFP_LAP_FOV, TFP_LAP_BLUR, TFP_LAP_CAMERA, TFP_LAP_HEAD, TFP_LAP_MOVEMENT, TFP_LAP_NEAR, TFP_LAP_DOF};
    bool (*const lapped[7])(TfpRtHost *, TfpRtValue *, TfpRtValue *) = {h->fov_update, h->blur_update, h->camera_update, h->head_update,
                                                                       h->movement_update, h->near_update, h->dof_update};
    int64_t parts_before[TFP_UPDS];
    memcpy(parts_before, wire.laps.part, sizeof parts_before);
    for (int i = 0; i < 7; i++) {
      TfpRtValue a1, a2, b1, b2;
      const int64_t spent = wire.laps.tick[laps[i]];
      const bool own = wire.laps.update[i](h, &a1, &a2), timed = lapped[i](h, &b1, &b2);
      CHECK(own == timed && a1.kind == b1.kind && a2.kind == b2.kind && wire.laps.tick[laps[i]] > spent);
    }
    CHECK(!memcmp(parts_before, wire.laps.part, sizeof parts_before)); /* round 35: outside on_update no part is charged */
    { /* round 35: inside on_update a target's call is the targets part; the part it was called from carries on after it */
      wire.laps.in_update = true, wire.laps.current = TFP_UPD_MANAGED, wire.laps.mark = fake_ticks();
      TfpRtValue a1, a2;
      const int64_t targets = wire.laps.part[TFP_UPD_TARGETS], managed = wire.laps.part[TFP_UPD_MANAGED];
      lapped[0](h, &a1, &a2);
      CHECK(wire.laps.current == TFP_UPD_MANAGED && wire.laps.part[TFP_UPD_TARGETS] > targets && wire.laps.part[TFP_UPD_MANAGED] > managed);
      wire.laps.in_update = false;
    }
    int64_t lap[TFP_LAPS];
    memcpy(lap, wire.laps.tick, sizeof lap);
    tfp_perf_laps_close(lap);
    CHECK(lap[TFP_LAP_EXTRA_OTHER] >= 0 && lap[TFP_LAP_UPDATE_OTHER] >= 0);
    memset(wire.laps.tick, 0, sizeof wire.laps.tick);
    memset(wire.laps.part, 0, sizeof wire.laps.part);
  }

  /* ---- presentation:action and :resume through the motion binder (transition.lua:85-104) ---- */
  CHECK(h->transition_action(h, tfp_wire_nil(), now_seconds, &o1, &o2) && o1.kind == TFP_RT_NIL);           /* text(action) fails */
  CHECK(h->transition_action(h, tfp_wire_text(""), now_seconds, &o1, &o2) && o1.kind == TFP_RT_NIL);
  CHECK(h->transition_resume(h, tfp_wire_text("W:zz"), tfp_wire_text("P:0000000000000001"), now_seconds, &o1, &o2) && o1.kind == TFP_RT_NIL);
  CHECK(h->transition_resume(h, tfp_wire_text("W:0000000000000001"), tfp_wire_text("P:0000000000000001"), now_seconds, &o1, &o2) &&
        o1.kind == TFP_RT_NIL); /* no packet has that identity */

  /* ---- the motion module with no player: result() is a table with no action ---- */
  TfpRtTable table;
  CHECK(h->motion_result(h, &table) && table.present && table.f[TFP_RT_MOTION_ACTION].kind == TFP_RT_NIL &&
        table.f[TFP_RT_MOTION_DX].kind == TFP_RT_NUMBER && table.f[TFP_RT_MOTION_DX].number == 0);
  TfpLocomotionSample sample;
  const char *why = NULL;
  CHECK(!tfp_wire_motion_sample(&wire, NULL, &sample, &why) && why && *why); /* no lease: the sampler says why */

  /* ---- the settings store's aim_markers table as the effects binder reads it ---- */
  static const char *const keys[] = {"aaaa000011112222", "bbbb000011112222"};
  TfpAimMarkersSaved saved;
  memset(&saved, 0, sizeof saved);
  TfpRtValue markers = tfp_wire_settings_markers(&wire, keys, 2);
  CHECK(markers.kind == TFP_RT_REF && tfp_wire_feel_markers(&wire, markers, &saved) && saved.count == 2 && !strcmp(saved.keys[0], keys[0]) &&
        !strcmp(saved.keys[1], keys[1]));
  CHECK(!tfp_wire_feel_markers(&wire, tfp_wire_number(1), &saved) && !tfp_wire_feel_markers(&wire, tfp_wire_ref(tfp_rt_default_placement), &saved));
  CHECK(tfp_wire_call(&wire, TFP_RT_E_markers_settings, NULL, r) == TFP_RT_OK && tfp_wire_feel_markers(&wire, r[0], &saved) && saved.count == 0);

  /* ---- the placement editor with no game: retail's own steps and refusals, through the runtime's controller and store ---- */
  {
    const char *why;
    line_count = 0;
    CHECK(!tfp_wire_editor_active(&wire) && !tfp_wire_feel_held(&wire));
    CHECK((why = tfp_wire_editor_toggle(&wire)) && strstr(why, "DBF-MCM captured placement input is unavailable")); /* no overlay platform yet */
    ed.available = ed.focused = true; ed.menu_open = true;
    CHECK(!tfp_wire_editor_toggle(&wire) && tfp_wire_editor_active(&wire) && logged("Placement editor: Opening placement editor"));
    {                                                                                                     /* round 16: motion is handed the editor */
      const TfpMotionRuntime *motion = tfp_wire_motion_runtime(&wire);
      bool open = false;
      TfpActionPlacement preview;
      CHECK(motion && motion->host->editor_is_active && motion->host->editor_preview_rotation);       /* feel.1-feel.50: both NULL */
      CHECK(motion->host->editor_preview_ads);                                                         /* round 20: the ADS draft, live in full aim */
      CHECK(motion->host->editor_is_active(motion->host->user, &open, NULL) && open);                  /* queued counts as active, as retail */
      CHECK(motion->host->editor_preview_rotation(motion->host->user, &preview) == TFP_MOTION_GOT_NIL); /* no draft before it is active */
      bool authored = true;                                                                            /* round 20, the binder's own path: */
      const char *refused = NULL;                                                                      /* queued, no draft: nothing sent */
      CHECK(motion->host->editor_preview_ads(motion->host->user, NULL, &authored, &refused) && !authored && !refused);
    }
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "the placement editor is open"));            /* a live replace waits for it */
    CHECK(wire.rt.store_mode == 1);                                                                     /* store.select_mode('weapon') reached the runtime */
    CHECK((why = tfp_wire_editor_toggle(&wire)) == NULL && !tfp_wire_editor_active(&wire));              /* the key again: cancelled */
    CHECK(logged("Placement editor: Placement editing cancelled; first person stays on"));
    CHECK(!tfp_wire_editor_toggle(&wire));
    now_seconds += 1 / 60.0; tfp_wire_feel_begin(&wire); tfp_wire_frame(&wire, NULL);                    /* queued: the settings menu is closed first */
    CHECK(ed.closes == 1 && tfp_wire_editor_active(&wire) && !ed.acquired);
    now_seconds += 1 / 60.0; tfp_wire_feel_begin(&wire); tfp_wire_frame(&wire, NULL);                    /* handoff: first person cannot start without a game */
    CHECK(logged("Placement editor: First-person entry refused: Start refused: game.dll unavailable"));
    CHECK(!tfp_wire_editor_active(&wire) && !ed.acquired && !ed.draws && !tfp_wire_editor_holds_draft(&wire) && !tfp_wire_feel_held(&wire));
    CHECK(wire.ops.pose_placement.x == .15 && wire.ops.pose_placement.z == -.12);                        /* the placement it prepared was put back */
    ed.busy = true;                                                                                      /* another frontend holds the input */
    CHECK(!tfp_wire_editor_toggle(&wire));
    for (int i = 0; i < 2; i++) { now_seconds += 1 / 60.0; tfp_wire_feel_begin(&wire); tfp_wire_frame(&wire, NULL); }
    CHECK(logged("Placement editor: Close other menus before positioning the weapon") && !tfp_wire_editor_active(&wire));
    ed.busy = false;
    CHECK(!strcmp(tfp_wire_editor_status(&wire), "Close other menus before positioning the weapon"));
    {
      const TfpMotionRuntime *motion = tfp_wire_motion_runtime(&wire);
      bool open = true;
      CHECK(motion->host->editor_is_active(motion->host->user, &open, NULL) && !open);                 /* closed: motion's play path */
    }
    if (getenv("TFP_SHOW_LOG")) for (int i = 0; i < line_count; i++) printf("  log: %s\n", lines[i]);
  }
  /* An idle motion phase retains the previous sampled action during disable or a lost lease. */
  {
    TfpMotionRuntime *motion = (TfpMotionRuntime *)tfp_wire_motion_runtime(&wire);
    CHECK(motion != NULL);
    TfpMotionRuntimeState previous = motion->state;
    line_count = 0;
    motion->state.action = TFP_ACT_IDLE;
    motion->state.phase = TFP_MOTION_PHASE_IDLE;
    for (unsigned frame = 0; frame < 120; ++frame) tfp_wire_motion_frame(&wire);
    CHECK(line_count == 0);
    wire.controller.enabled = true;
    motion->state.phase = TFP_MOTION_PHASE_ACTIVE;
    const TfpAct actions[] = {TFP_ACT_IDLE, TFP_ACT_SPRINT, TFP_ACT_IDLE};
    for (int i = 0; i < 3; ++i) {
      motion->state.action = actions[i];
      for (unsigned frame = 0; frame < 120; ++frame) tfp_wire_motion_frame(&wire);
      CHECK(line_count == i + 1 && strstr(lines[i], tfp_action_name(actions[i])));
    }
    motion->state.phase = TFP_MOTION_PHASE_IDLE;
    tfp_wire_motion_frame(&wire);
    motion->state.phase = TFP_MOTION_PHASE_ACTIVE;
    tfp_wire_motion_frame(&wire);
    CHECK(line_count == 3);
    wire.controller.enabled = false;
    tfp_wire_motion_frame(&wire);
    wire.controller.enabled = true;
    tfp_wire_motion_frame(&wire);
    CHECK(line_count == 4 && strstr(lines[3], "Motion: action=idle"));
    wire.controller.enabled = false;
    motion->state = previous;
  }
  /* ---- the Feel line: once a second while the controller is enabled ---- */
  if (getenv("TFP_SHOW_LOG")) for (int i = 0; i < line_count; i++) printf("  log: %s\n", lines[i]);
  line_count = 0;
  wire.controller.enabled = true; /* the line's gate only; nothing else is called with it here */
  wire.rt.clock_previous = tfp_wire_number(500);
  wire.frame_extra(&wire);
  wire.rt.clock_previous = tfp_wire_number(500.5);
  wire.frame_extra(&wire);
  CHECK(!logged("Feel:"));
  wire.rt.clock_previous = tfp_wire_number(501.01);
  wire.frame_extra(&wire);
  CHECK(logged("Feel: t=501.0 pose=") && logged(" motion=") && logged(" head=") && logged(" dof="));
  if (getenv("TFP_SHOW_LOG")) for (int i = 0; i < line_count; i++) printf("  log: %s\n", lines[i]);
  wire.controller.enabled = false;
  CHECK(text_is(tfp_wire_text("x"), "x"));

  /* ---- live replace (docs/guides/LIVE-REPLACE.md): idle and clean, or the first thing still held ---- */
  {
    TfpWireFeelFacts none, f;
    const char *why;
    TfpFx *fx = tfp_wire_effects_of(&wire);
    CHECK(!tfp_wire_feel_held(&wire));                        /* enabled, never entered: nothing is held */
    tfp_wire_feel_facts(&wire, &none);
    CHECK(!none.view_on && none.phase == TFP_PHASE_IDLE && !none.lease && !none.recovering && !none.pose_lease && !none.look_pending);
    CHECK(!none.head && !none.sleeves && !none.grip && !none.dof && !none.vignette && !none.markers && !none.movement && !none.aim_option);
    /* the gathering reads the modules' own state */
    wire.controller.enabled = true;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "the view is on") && strstr(why, "the toggle key") && !strstr(why, "F11"));
    wire.controller.enabled = false;
    wire.controller.phase = TFP_PHASE_WAITING;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "has not finished switching off"));
    wire.controller.phase = TFP_PHASE_CLEANUP_PENDING;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "cleanup is still pending"));
    wire.controller.phase = TFP_PHASE_IDLE;
    fx->movement.has_owned = true;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "movement"));
    fx->movement.has_owned = false;
    fx->markers.has_lease = true;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "aim markers"));
    fx->markers.has_lease = false;
    fx->aim.has_lease = true;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "native aim option"));
    fx->aim.has_lease = false;
    fx->dof.dof.has_record = true;
    CHECK((why = tfp_wire_feel_held(&wire)) && strstr(why, "depth of field"));
    fx->dof.dof.has_record = false;
    CHECK(!tfp_wire_feel_held(&wire));
    /* each fact has its own answer, and the view comes first */
#define HELD(field, text) f = none; f.field = true; CHECK((why = tfp_wire_feel_held_of(&f)) && strstr(why, text));
    HELD(view_on, "the view is on") HELD(lease, "native-view lease") HELD(recovering, "being recovered") HELD(pose_lease, "pose helper")
    HELD(look_pending, "look-gain") HELD(head, "head visibility") HELD(sleeves, "arm pieces") HELD(grip, "grip hand")
    HELD(dof, "depth of field") HELD(vignette, "vignette") HELD(markers, "aim markers") HELD(movement, "movement")
    HELD(aim_option, "native aim option")
    HELD(editor, "the placement editor is open");
    HELD(unsaved, "settings that could not be saved");
    f = none; f.editor = f.unsaved = true; CHECK(strstr(tfp_wire_feel_held_of(&f), "placement editor")); /* the editor first: closing it may save */
#undef HELD
    f = none; f.phase = TFP_PHASE_RUNNING; CHECK((why = tfp_wire_feel_held_of(&f)) && strstr(why, "has not finished switching off"));
    f = none; f.phase = TFP_PHASE_DISABLED; CHECK(!tfp_wire_feel_held_of(&f));
    f = none; f.head = f.view_on = true; CHECK(strstr(tfp_wire_feel_held_of(&f), "the view is on"));
  }

  /* ---- disable: every module shuts down clean ---- */
  line_count = 0;
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK && !logged("Refused"));
  CHECK(!tfp_wire_feel_held(&wire));                          /* and nothing is held afterwards */

  /* ---- round 35: the snapshot memo's switch (tfp_wire_set_memo): on this backend and every one made after it, until a check
   * turns it off for the session; off, the backend is as round 32 left it ---- */
  CHECK(!wire.memo_on && !wire.ops.perf.memo_on && tfp_wire_memo_state(&wire) == TFP_PERF_MEMO_SHADOW);
  {
    const TfpOpsPerf before = wire.ops.perf;
    tfp_wire_set_memo(&wire, false); /* memo_off.txt: the backend is not touched */
    CHECK(!memcmp(&before, &wire.ops.perf, sizeof before));
  }
  tfp_wire_set_memo(&wire, true);
  CHECK(wire.ops.perf.memo_on && wire.ops.perf.memo_live && tfp_wire_memo_state(&wire) == TFP_PERF_MEMO_LIVE);
  CHECK(h->backend_new(h) && wire.ops.perf.memo_on && wire.ops.perf.memo_live && wire.ops.perf.clock == fake_ticks); /* a new backend */
  wire.ops.perf.off.has = true, wire.ops.perf.off.field = "controller_bytes", wire.ops.perf.off.epoch = 7, wire.ops.perf.memo_live = false;
  CHECK(tfp_wire_memo_state(&wire) == TFP_PERF_MEMO_TURNED_OFF);
  CHECK(h->backend_new(h) && wire.ops.perf.memo_on && !wire.ops.perf.memo_live && !wire.ops.perf.off.has && wire.memo_off &&
        !strcmp(wire.memo_off_cause.field, "controller_bytes") && wire.memo_off_cause.epoch == 7);
  CHECK(tfp_wire_memo_state(&wire) == TFP_PERF_MEMO_TURNED_OFF); /* the session's check holds on the new backend */
  tfp_wire_set_memo(&wire, true);
  CHECK(!wire.ops.perf.memo_live);
  tfp_wire_set_memo(&wire, false);
  CHECK(!wire.ops.perf.memo_on && !wire.ops.perf.memo_live && tfp_wire_memo_state(&wire) == TFP_PERF_MEMO_SHADOW);
  /* ---- round 40: the start's switch files (src/mod_view.c): the memo on by default, memo_off.txt switches it off,
   * memo_on.txt changes nothing; a check that turned it off still holds for the session ---- */
  {
    static TfpWire fresh;
    tfp_wire_init(&fresh, &memory, &platform, &helpers);
    CHECK(!fresh.memo_on && tfp_wire_memo_state(&fresh) == TFP_PERF_MEMO_SHADOW); /* before the start reads the files */
    CHECK(tfp_wire_memo_start(&fresh, false) && fresh.memo_on && fresh.ops.perf.memo_on && fresh.ops.perf.memo_live &&
          tfp_wire_memo_state(&fresh) == TFP_PERF_MEMO_LIVE); /* no file: on */
    CHECK(fresh.host.backend_new(&fresh.host) && fresh.ops.perf.memo_live); /* and on every new backend */
    fresh.ops.perf.off.has = true, fresh.ops.perf.off.moved = true, fresh.ops.perf.off.field = "state", fresh.ops.perf.memo_live = false;
    CHECK(tfp_wire_memo_state(&fresh) == TFP_PERF_MEMO_TURNED_OFF); /* a check failed (backend_ops_test: a mismatch, a move) */
    CHECK(fresh.host.backend_new(&fresh.host) && fresh.ops.perf.memo_on && !fresh.ops.perf.memo_live && fresh.memo_off &&
          fresh.memo_off_cause.moved && tfp_wire_memo_state(&fresh) == TFP_PERF_MEMO_TURNED_OFF); /* off for the session */
    tfp_wire_init(&fresh, &memory, &platform, &helpers);
    const TfpOpsPerf before = fresh.ops.perf;
    CHECK(!tfp_wire_memo_start(&fresh, true) && !fresh.memo_on && tfp_wire_memo_state(&fresh) == TFP_PERF_MEMO_SHADOW &&
          !memcmp(&before, &fresh.ops.perf, sizeof before)); /* memo_off.txt: the backend untouched, round 32's shadow */
    CHECK(fresh.host.backend_new(&fresh.host) && !fresh.ops.perf.memo_on);
  }
  CHECK(wire.memo_off && tfp_wire_memo_start(&wire, false) && !wire.ops.perf.memo_live &&
        tfp_wire_memo_state(&wire) == TFP_PERF_MEMO_TURNED_OFF); /* the session's failed check is not undone */
  /* ---- round 53: the placement editor's list of saved placements (src/editor_weapons.h), from the settings store and the
   * catalogue: every key that holds an action, the older per-weapon rows, the held weapon; in its category, by name ---- */
  {
    static TfpEwWeapon list[TFP_EW_MAX];
    TfpSettings *store = tfp_wire_settings(&wire);
    const char *why = NULL;
    CHECK(store != NULL);
    CHECK(tfp_wire_editor_weapon_list(&wire, NULL, list, TFP_EW_MAX) == 0);                              /* nothing saved, nothing held */
    CHECK(tfp_wire_editor_weapon_list(&wire, "64ce3d03c0118296", list, TFP_EW_MAX) == 1 && list[0].held && !list[0].saved &&
          !strcmp(list[0].name, "AR-23 Liberator") && list[0].group == TFP_EW_PRIMARY);                    /* the held weapon alone */
    TfpActionProfile rows[6];
    memset(rows, 0, sizeof rows);
    static const char *const ids[6] = {"5689b3ab3b7dc211" /* MG-43 Machine Gun */, "0294f1b2a3523d8d" /* P-4 Senator */, "28131d96503b1846" /* SG-225 Breaker */,
                                       "__all", "ffff000011112222" /* not in the catalogue */, "a2446edbc2e5e405" /* P-2 Peacemaker: a row without an action */};
    for (int i = 0; i < 6; i++) {
      rows[i].id = ids[i];
      if (i == 5) continue;
      rows[i].has = 1u << TFP_ACT_IDLE;
      rows[i].action[TFP_ACT_IDLE] = (TfpActionPlacement){.1, .2, .3, 0, 0, 0, TFP_PLACE_XYZ};
    }
    const TfpActionLegacy legacy[2] = {{"64ce3d03c0118296" /* AR-23 Liberator */, {.1, .2, .3, 0, 0, 0, TFP_PLACE_XYZ}},
                                       {"28131d96503b1846" /* also in action_placements: once */, {.1, .2, .3, 0, 0, 0, TFP_PLACE_XYZ}}};
    CHECK(tfp_settings_set(store, TFP_SET_action_placements, tfp_settings_new_profiles(store, rows, 6), &why));
    CHECK(tfp_settings_set(store, TFP_SET_weapon_placements, tfp_settings_new_placements(store, legacy, 2), &why));
    int n = tfp_wire_editor_weapon_list(&wire, "0294f1b2a3523d8d", list, TFP_EW_MAX);
    static const struct { const char *name; TfpEwGroup group; } want[6] = {
      {"P-4 Senator", TFP_EW_SECONDARY}, {"AR-23 Liberator", TFP_EW_PRIMARY}, {"SG-225 Breaker", TFP_EW_PRIMARY}, {"MG-43 Machine Gun", TFP_EW_SUPPORT},
      {"All weapons (shared)", TFP_EW_OTHER}, {"ffff000011112222", TFP_EW_OTHER}};
    CHECK(n == 6);
    for (int i = 0; i < 6; i++) CHECK(!strcmp(list[i].name, want[i].name) && list[i].group == want[i].group && list[i].saved && list[i].held == (i == 0));
    n = tfp_wire_editor_weapon_list(&wire, "a2446edbc2e5e405", list, TFP_EW_MAX);                          /* held, its row holds no action: listed as not saved */
    CHECK(n == 7 && list[0].held && !list[0].saved && !strcmp(list[0].name, "P-2 Peacemaker") && !strcmp(list[1].name, "AR-23 Liberator") &&
          !strcmp(list[3].name, "P-4 Senator") && list[3].group == TFP_EW_SECONDARY && !list[3].held);
    CHECK(tfp_wire_editor_weapon_list(&wire, NULL, list, 3) == 3);                                         /* a short list is filled, not overrun */
    static TfpWire unbound;
    CHECK(tfp_wire_editor_weapon_list(&unbound, "64ce3d03c0118296", list, TFP_EW_MAX) == 0);              /* no settings bound */
  }
  tfp_wire_set_memo(&wire, false);
  printf("wire feel: %u checks passed (all five module binders joined with no game: enable, 90 frames, F11 refused, the placement editor opened and refused, disable, what a live replace finds held; the motion "
         "binder's shapes and presentation calls; the markers table; the Feel line; the laps and parts; the memo switch, on by default; the editor's list of saved placements)\n", checks);
  return 0;
}
