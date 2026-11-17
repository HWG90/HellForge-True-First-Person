/* The feel build's join (src/wire_feel.c) and the motion binder's adapters (src/wire_motion.c),
 * run with no game behind them: every read fails, the engine's script state never arrives, the pose
 * helper is a scripted table. What this proves: all five module binders can be bound together, the
 * runtime's on_enable and its per-update order run through them without a fault, each module that
 * cannot work says so, and the adapters with logic of their own (snapshot and action-state shapes,
 * presentation:action / :resume, the Feel line, the markers table) answer as written.
 * It does not prove anything about the game: that is docs/WIRING-FEEL.md, "unproven". */
#include "wire_feel.h"
#include "wire_off.h"
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
                                        f_write, f_widget};
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
  TfpWireFeelParts parts = {NULL, 0, "settings.json", &pose, &pose_io, &engine, &effects};
  tfp_wire_feel_bind(&wire, &feel, &parts);
  CHECK(feel.has_pose && feel.has_motion && feel.has_engine && feel.has_effects);
  CHECK(logged("Refused: settings:") && logged("Ready: settings are retail's defaults")); /* no file */
  CHECK(logged("Ready: pose") && logged("Ready: motion bound") && logged("Ready: model shadows"));
  TfpRtHost *h = &wire.host;
  CHECK(wire.pose_helper && wire.placement_decode && wire.lease_extra && wire.controller_extra && wire.frame_extra && wire.pose_counts);
  CHECK(h->mod_set && h->config_field != off.config_field && h->actions_resolve != off.actions_resolve && h->motion_update != off.motion_update);
  CHECK(h->transition_action != off.transition_action && h->head_update != off.head_update && h->arm_pieces_new && h->grip_hand_new);
  CHECK(h->dof_update != off.dof_update && h->movement_update != off.movement_update && h->native_aim_update != off.native_aim_update);
  CHECK(h->aim_markers_update != off.aim_markers_update && h->helper_depth_stats != off.helper_depth_stats);
  /* what stays the view build's off answer */
  CHECK(h->editor_request_open == off.editor_request_open && h->compat_update == off.compat_update && h->fird_start == off.fird_start);
  CHECK(h->head_positioner_start == off.head_positioner_start && h->camera_free_aim == off.camera_free_aim && h->menu_update == off.menu_update);

  tfp_wire_feel_begin(&wire);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_update, NULL, r) == TFP_RT_OK);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK);
  CHECK(logged("Idle. F11 toggles True First Person") && wire.rt.has[TFP_RT_O_CONTROLLER] && wire.rt.has[TFP_RT_O_MOTION]);
  CHECK(wire.ops.has_pose_placement && wire.ops.pose_placement.x == .15 && wire.ops.pose_placement.z == -.12);
  for (int i = 0; i < 90; i++) {
    now_seconds += 1 / 60.0;
    tfp_wire_feel_begin(&wire);
    tfp_wire_frame(&wire, NULL);
  }
  CHECK(!wire.controller.enabled && !logged("the frame raised") && !logged("Feel:")); /* off: no Feel line */
  wire.toggle_requested = true; /* F11 with no game: retail's own refusal; nothing reaches the pose helper */
  now_seconds += 1 / 60.0;
  tfp_wire_feel_begin(&wire);
  tfp_wire_frame(&wire, NULL);
  CHECK(logged("State: enter requested (F11)") && logged("Refused: F11 enter: Start refused: game.dll unavailable") && !pose_calls);
  CHECK(!wire.controller.enabled && !wire.controller.lease);

  /* ---- presentation:action and :resume through the motion binder (transition.lua:85-104) ---- */
  TfpRtValue o1, o2;
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

  /* ---- disable: every module shuts down clean ---- */
  line_count = 0;
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK && !logged("Refused"));
  printf("wire feel: %u checks passed (all five module binders joined with no game: enable, 90 frames, F11 refused, disable; the motion "
         "binder's shapes and presentation calls; the markers table; the Feel line)\n", checks);
  return 0;
}
