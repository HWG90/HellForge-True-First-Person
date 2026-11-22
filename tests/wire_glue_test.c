/* The glue of the view build that has logic of its own (src/wire_rt.c, wire_recovery.c, wire_log.c):
 * the value adapters between the runtime host and the typed units, the placement rule of
 * controller.lua:112-124, the session log, and the whole binder run without a game (every read
 * fails): enable, idle frames, F11, disable. No Windows, no helper is ever reached. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wire.h"
#include "wire_off.h"
#include "wire_recovery.h"

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { printf("wire glue: FAILED %s (line %d)\n", #condition, __LINE__); return 1; } } while (0)

static char lines[64][700];
static int line_count;
static void capture(void *user, const char *text) {
  (void)user;
  if (line_count < 64) snprintf(lines[line_count++], sizeof lines[0], "%s", text);
}
static bool logged(const char *part) {
  for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) return true;
  return false;
}

/* ---- a host with no game behind it ---- */
static double now_seconds = 100;
static int mouse_level;
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
/* A controller (6 bytes) and a camera request's sight distance, for the aim keeper between ticks. */
enum { AIM_CONTROLLER = 0x500000000, AIM_RECORD = 0x300000200 };
static uint8_t aim_controller[6], aim_sight[4];
static unsigned aim_writes;
static uint8_t *aim_at(uint64_t at, size_t size) {
  if (at >= AIM_CONTROLLER && at + size <= AIM_CONTROLLER + 6) return aim_controller + (at - AIM_CONTROLLER);
  if (at >= AIM_RECORD + 0x98 && at + size <= AIM_RECORD + 0x98 + 4) return aim_sight + (at - AIM_RECORD - 0x98);
  return NULL;
}
static bool aim_read(void *u, uint64_t at, void *out, size_t size) { (void)u; uint8_t *p = aim_at(at, size); if (p) memcpy(out, p, size); return p != NULL; }
static bool aim_write(void *u, uint64_t at, const void *bytes, size_t size) {
  (void)u; uint8_t *p = aim_at(at, size); if (p) memcpy(p, bytes, size), aim_writes++; return p != NULL;
}
static int test_mouse(void *u, int button) { (void)u; return button == 1 ? mouse_level : 0; }
static unsigned helper_calls;
static uint32_t h_validate_c(const TfpCameraConfig *c) { (void)c; helper_calls++; return 0; }
static uint32_t h_begin_c(const TfpCameraConfig *c, uint64_t *g) { (void)c; (void)g; helper_calls++; return 0; }
static uint32_t h_end(uint64_t g) { (void)g; helper_calls++; return 1; }
static uint32_t h_stats_c(uint64_t g, TfpCameraStats *s) { (void)g; (void)s; helper_calls++; return 0; }
static uint32_t h_error(void) { helper_calls++; return 0; }
static uint32_t h_generation(uint64_t g) { (void)g; helper_calls++; return 0; }
static uint32_t h_handoff(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; helper_calls++; return 0; }
static uint32_t h_u32(uint64_t g, uint32_t v) { (void)g; (void)v; helper_calls++; return 0; }
static uint32_t h_validate_f(const TfpFovConfig *c) { (void)c; helper_calls++; return 0; }
static uint32_t h_begin_f(const TfpFovConfig *c, uint64_t *g) { (void)c; (void)g; helper_calls++; return 0; }
static uint32_t h_bind_f(uint64_t g, uint64_t r, uint32_t w) { (void)g; (void)r; (void)w; helper_calls++; return 0; }
static uint32_t h_transition_f(uint64_t g, uint64_t e, float p, uint32_t a) { (void)g; (void)e; (void)p; (void)a; helper_calls++; return 0; }
static uint32_t h_stats_f(uint64_t g, TfpFovStats *s) { (void)g; (void)s; helper_calls++; return 0; }
static uint32_t h_begin_n(const TfpNearConfig *c, uint64_t *g) { (void)c; (void)g; helper_calls++; return 0; }
static uint32_t h_set_n(uint64_t g, float a, float b) { (void)g; (void)a; (void)b; helper_calls++; return 0; }
static uint32_t h_stats_n(uint64_t g, TfpNearStats *s) { (void)g; (void)s; helper_calls++; return 0; }
static uint32_t h_validate_b(const TfpBlurConfig *c) { (void)c; helper_calls++; return 0; }
static uint32_t h_begin_b(const TfpBlurConfig *c, uint64_t *g) { (void)c; (void)g; helper_calls++; return 0; }
static uint32_t h_stats_b(uint64_t g, TfpBlurStats *s) { (void)g; (void)s; helper_calls++; return 0; }

static TfpWire wire;
/* The action hold's two hooks, as the feel build's would be. */
static unsigned hold_applies, hold_probes;
static bool hold_armed, hold_holding;
static void hold_apply(TfpWire *w, bool armed, bool holding) { (void)w; hold_applies++, hold_armed = armed, hold_holding = holding; }
static void hold_probe(TfpWire *w, const TfpOpsHold *view, uint32_t weapon_unit, TfpWireHoldProbe *out) {
  (void)w; (void)view; (void)weapon_unit; (void)out;
  hold_probes++;
}
/* A saved per-weapon placement as another binder would decode it: the ref points at six numbers. */
static bool saved_decode(TfpWire *w, TfpRtValue value, TfpOpsPlacement *out) {
  const double *v = value.ref;
  (void)w;
  *out = (TfpOpsPlacement){v[0], v[1], v[2], v[3], v[4], v[5], TFP_OPS_X | TFP_OPS_Y | TFP_OPS_Z | TFP_OPS_YAW | TFP_OPS_PITCH | TFP_OPS_ROLL};
  return true;
}
static bool text_is(TfpRtValue v, const char *text) {
  return v.kind == TFP_RT_TEXT && v.length == strlen(text) && !memcmp(v.text, text, v.length);
}
static TfpOpsSnapshot sample(bool full) {
  TfpOpsSnapshot s;
  memset(&s, 0, sizeof s);
  s.base = 0x7ff600000000, s.context = 0x1000, s.camera = 0x2000, s.record = 0x1200, s.world = 0x3000, s.controller = 0x4000;
  s.player = 0x5000, s.avatar_owner = 0x6000, s.camera_kind = 6, s.avatar = 7, s.avatar_index = 2, s.body = 11, s.viewmodel = 77;
  s.weapon = 912, s.weapon_unit = 78, s.controller_bytes[4] = 1, s.controller_bytes[5] = 9;
  s.controller_active = true, s.aiming = true, s.safe = true, s.full = full, s.weapon_ready = full;
  return s;
}

int main(void) {
  /* ---- the placement rule (controller.lua:112-124) ---- */
  const double zero[3] = {0, 0, 0}, shipped[3] = {.15, 0, -.12}, far_x[3] = {.51, 0, 0}, edge[3] = {.5, -.5, .5}, not_a_number[3] = {0, NAN, 0};
  const double tilted[3] = {90, -90, 0}, over[3] = {0, 90.5, 0};
  CHECK(!tfp_wire_placement_check(shipped, zero) && !tfp_wire_placement_check(edge, tilted));
  CHECK(!strcmp(tfp_wire_placement_check(far_x, zero), "Placement must be finite XYZ within 0.5 metres"));
  CHECK(!strcmp(tfp_wire_placement_check(not_a_number, zero), "Placement must be finite XYZ within 0.5 metres"));
  CHECK(!strcmp(tfp_wire_placement_check(shipped, over), "Angles must be within90degrees"));
  CHECK(!strcmp(tfp_wire_placement_check(shipped, not_a_number), "Angles must be within90degrees"));

  /* ---- a snapshot as the controller's view ---- */
  TfpOpsSnapshot full = sample(true), player = sample(false);
  TfpView view;
  tfp_wire_view(&full, "kept", &view);
  CHECK(view.available && view.identity == tfp_ops_identity_key(&full) && view.player_identity == tfp_ops_player_key(&full));
  CHECK(view.aiming && view.controller_active && view.safe && view.camera_kind == 6 && view.record == 0x1200 && view.guard == 9);
  CHECK(view.reason == NULL);
  full.has_reason = true;
  tfp_wire_view(&full, "kept", &view);
  CHECK(view.reason && !strcmp(view.reason, "kept"));
  full.has_reason = false;
  tfp_wire_view(&player, "", &view);
  CHECK(view.identity == view.player_identity && view.identity == tfp_ops_player_key(&player));

  /* ---- a snapshot as the runtime's table ---- */
  memset(&wire, 0, sizeof wire);
  TfpRtTable t, again;
  tfp_wire_snapshot_table(&wire, &full, &t);
  CHECK(t.present && t.ref == &wire.snaps[0]);
  CHECK(t.f[TFP_RT_SNAP_CAMERA_KIND].kind == TFP_RT_NUMBER && t.f[TFP_RT_SNAP_CAMERA_KIND].number == 6);
  CHECK(t.f[TFP_RT_SNAP_SAFE].kind == TFP_RT_TRUE && t.f[TFP_RT_SNAP_AIMING].kind == TFP_RT_TRUE && t.f[TFP_RT_SNAP_SEATED].kind == TFP_RT_FALSE);
  CHECK(t.f[TFP_RT_SNAP_IDENTITY].kind == TFP_RT_TEXT && t.f[TFP_RT_SNAP_IDENTITY].text[0] == 'W');
  CHECK(t.f[TFP_RT_SNAP_PLAYER_IDENTITY].text[0] == 'P' && t.f[TFP_RT_SNAP_PLAYER_IDENTITY].length == 18);
  CHECK(t.f[TFP_RT_SNAP_CONTROLLER_BYTES].kind == TFP_RT_TEXT && t.f[TFP_RT_SNAP_CONTROLLER_BYTES].length == 6);
  CHECK(t.f[TFP_RT_SNAP_CONTROLLER_BYTES].text[0] == 0 && t.f[TFP_RT_SNAP_CONTROLLER_BYTES].text[4] == 1 && t.f[TFP_RT_SNAP_CONTROLLER_BYTES].text[5] == 9);
  CHECK(t.f[TFP_RT_SNAP_WEAPON_UNIT].number == 78 && t.f[TFP_RT_SNAP_WEAPON_READY].kind == TFP_RT_TRUE);
  CHECK(t.f[TFP_RT_SNAP_BODY].number == 11 && t.f[TFP_RT_SNAP_WORLD].number == 0x3000 && t.f[TFP_RT_SNAP_CAMERA].number == 0x2000);
  CHECK(t.f[TFP_RT_SNAP_VIEWMODEL].number == 77 && t.f[TFP_RT_SNAP_AVATAR_OWNER].number == 0x6000 && t.f[TFP_RT_SNAP_AVATAR_INDEX].number == 2);
  CHECK(t.f[TFP_RT_SNAP_RECORD].number == 0x1200 && t.f[TFP_RT_SNAP_REASON].kind == TFP_RT_NIL && t.f[TFP_RT_SNAP_ZOOM_IDENTITY].kind == TFP_RT_NIL);
  CHECK(t.f[TFP_RT_SNAP_CONTROLLER_ACTIVE].kind == TFP_RT_TRUE);
  tfp_wire_snapshot_table(&wire, &player, &again);
  CHECK(again.ref == &wire.snaps[1] && again.f[TFP_RT_SNAP_WEAPON_UNIT].kind == TFP_RT_NIL && again.f[TFP_RT_SNAP_WEAPON_READY].kind == TFP_RT_NIL);
  /* a player-level identity is the player identity and never equals a weapon-level one */
  CHECK(!strcmp(again.f[TFP_RT_SNAP_IDENTITY].text, again.f[TFP_RT_SNAP_PLAYER_IDENTITY].text));
  CHECK(!strcmp(again.f[TFP_RT_SNAP_PLAYER_IDENTITY].text, t.f[TFP_RT_SNAP_PLAYER_IDENTITY].text));
  CHECK(strcmp(again.f[TFP_RT_SNAP_IDENTITY].text, t.f[TFP_RT_SNAP_IDENTITY].text));
  TfpOpsSnapshot other = full;
  other.weapon_unit = 79;
  tfp_wire_snapshot_table(&wire, &other, &again);
  CHECK(strcmp(again.f[TFP_RT_SNAP_IDENTITY].text, t.f[TFP_RT_SNAP_IDENTITY].text)); /* another weapon: another identity */
  full.has_reason = true;
  snprintf(full.reason, sizeof full.reason, "Character is not in the verified on-foot lifecycle");
  tfp_wire_snapshot_table(&wire, &full, &again);
  CHECK(again.f[TFP_RT_SNAP_REASON].kind == TFP_RT_TEXT && !strcmp(again.f[TFP_RT_SNAP_REASON].text, full.reason));
  full.has_reason = false;

  /* ---- a lease as the runtime's flat table ---- */
  static TfpWireLease lease;
  char identity[24], who[24];
  lease.ops.origin = full, lease.ops.request = 0x1200, lease.ops.has_alignment = true;
  lease.ops.alignment.kind = TFP_OPS_KIND_POSE, lease.ops.alignment.handle = &wire.null_handle;
  tfp_wire_lease_table(&lease, &t, identity, who);
  CHECK(t.present && t.ref == &lease && t.f[TFP_RT_LEASE_ORIGIN].kind == TFP_RT_TRUE && t.f[TFP_RT_LEASE_ALIGNMENT].kind == TFP_RT_TRUE);
  CHECK(!strcmp(t.f[TFP_RT_LEASE_KIND].text, "pose") && t.f[TFP_RT_LEASE_ENDED].kind == TFP_RT_FALSE && t.f[TFP_RT_LEASE_HELPER].kind == TFP_RT_TRUE);
  CHECK(t.f[TFP_RT_LEASE_DEPTH_STATS].kind == TFP_RT_NIL && t.f[TFP_RT_LEASE_INPUT_STATS].kind == TFP_RT_NIL); /* no pose helper */
  CHECK(t.f[TFP_RT_LEASE_ORIGIN_IDENTITY].text[0] == 'W' && t.f[TFP_RT_LEASE_ORIGIN_PLAYER_IDENTITY].text[0] == 'P');
  CHECK(t.f[TFP_RT_LEASE_ORIGIN_BODY].number == 11 && t.f[TFP_RT_LEASE_ORIGIN_WORLD].number == 0x3000 && t.f[TFP_RT_LEASE_HANDLE].kind == TFP_RT_REF);
  tfp_wire_lease_table(NULL, &t, identity, who);
  CHECK(!t.present);

  /* ---- recovery: names and phases as retail's texts ---- */
  CHECK(tfp_wire_recovery_policy("fov") == 0 && tfp_wire_recovery_policy("movement") == 4 && tfp_wire_recovery_policy("near") < 0);
  TfpRtHost rh;
  memset(&rh, 0, sizeof rh);
  tfp_wire_recovery_bind(&rh, NULL);
  TfpRtValue v, v2, v3, proof[TFP_RT_PROOF_N];
  TfpRtPolicies policies;
  memset(proof, 0, sizeof proof);
  proof[TFP_RT_PROOF_FRESH] = proof[TFP_RT_PROOF_SAFE] = proof[TFP_RT_PROOF_SAME_PLAYER] = tfp_wire_bool(true);
  proof[TFP_RT_PROOF_FOREIGN] = tfp_wire_bool(false);
  CHECK(rh.recovery_new(&rh) && rh.recovery_has_policies(&rh, &v) && v.kind == TFP_RT_FALSE);
  CHECK(rh.recovery_action(&rh, "camera", 1, &v) && v.kind == TFP_RT_TEXT && !strcmp(v.text, "update"));
  CHECK(rh.recovery_failure(&rh, "camera", NULL, false, proof, 1, tfp_wire_nil(), &t) && t.present);
  CHECK(t.f[TFP_RT_ROW_CORE].kind == TFP_RT_TRUE && !strcmp(t.f[TFP_RT_ROW_PHASE].text, "cleanup"));
  CHECK(rh.recovery_action(&rh, "camera", 1, &v) && !strcmp(v.text, "cleanup"));
  CHECK(rh.recovery_snapshot(&rh, &policies) && policies.count == 1 && !strcmp(policies.name[0], "camera"));
  CHECK(rh.recovery_blocks_view(&rh, 1, &v, &v2, &v3) && v.kind == TFP_RT_TRUE && !strcmp(v2.text, "camera"));
  CHECK(rh.recovery_failure(&rh, "blur", NULL, true, proof, 1, tfp_wire_nil(), &t) && t.f[TFP_RT_ROW_CORE].kind == TFP_RT_FALSE);
  CHECK(!strcmp(t.f[TFP_RT_ROW_PHASE].text, "unavailable"));
  CHECK(!rh.recovery_action(&rh, "near", 1, &v) && rh.raised); /* not a managed policy: a raise, never a silent answer */
  CHECK(rh.recovery_reset(&rh) && rh.recovery_has_policies(&rh, &v) && v.kind == TFP_RT_FALSE);

  /* ---- the session log ---- */
  TfpViewLog log;
  TfpViewSample s;
  tfp_view_log_init(&log, capture, NULL);
  memset(&s, 0, sizeof s);
  s.phase = "idle", s.label = "Idle; manual start required";
  tfp_view_log_frame(&log, &s);
  CHECK(line_count == 0); /* off and unchanged: silent */
  s.time = 10, s.enabled = true, s.has_lease = true, s.request = 0x1200, s.camera_kind = 6, s.phase = "running", s.safe = true;
  s.label = "First person enabled until manually stopped", s.has_identity = true, s.identity = 5, s.has_eye = true, s.has_fov = true;
  s.fov_applied = 1.1, s.fov_native = 1.0;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Status: First person enabled until manually stopped") && logged("State: entered the native view request=0x1200 kind=6"));
  for (int i = 1; i <= 30; i++) { /* walking: 2 cm a frame */
    s.time = 10 + i / 60.0, s.eye[0] = .02 * i;
    tfp_view_log_frame(&log, &s);
  }
  s.time += 1 / 60.0, s.eye[0] += .02, s.eye[2] += .30, s.fov_applied = 1.4; /* one frame: a 30 cm cut and a lens jump */
  tfp_view_log_frame(&log, &s);
  for (int i = 0; i < 40; i++) {
    s.time += 1 / 60.0, s.eye[0] += .02;
    tfp_view_log_frame(&log, &s);
  }
  CHECK(logged("View: t=") && logged("eye_step=0.3007 eye_jump=0.3000") && logged("fov_jump=0.3000") && logged("frames=6"));
  s.controller_active = true;
  tfp_view_log_frame(&log, &s);
  s.aim_keeps = 1, s.controller_active = false, s.identity = 6;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("State: aim in") && logged("State: aim out") && logged("State: aim kept (1;") && logged("State: swap"));
  s.fault[2] = "Camera helper refused: code 5";
  tfp_view_log_frame(&log, &s);
  s.fault[2] = NULL, s.has_lease = false, s.wait = "Gameplay action or camera owns the view", s.phase = "waiting";
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Refused: camera: Camera helper refused: code 5") && logged("Cleared: camera"));
  CHECK(logged("State: left the native view: Gameplay action or camera owns the view"));
  s.enabled = false, s.phase = "idle";
  tfp_view_log_frame(&log, &s);
  CHECK(logged("State: exit complete"));

  /* ---- what the log says at each drop out of the view, and how large the cut was ---- */
  tfp_view_log_init(&log, capture, NULL);
  memset(&s, 0, sizeof s);
  line_count = 0;
  s.phase = "idle", s.label = "Idle";
  tfp_view_log_frame(&log, &s);
  s.time = 100, s.enabled = true, s.has_lease = true, s.request = 0x1200, s.camera_kind = 6, s.phase = "running", s.safe = true;
  s.label = "First person enabled until manually stopped", s.has_eye = true;
  s.gate = "kind=6 safe=1", s.action = "layers=3:12", s.camera = "cam=active replaced=0", s.aim_source = "engine";
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Return: kind=6 safe=1 | layers=3:12 | cam=active replaced=0"));
  for (int i = 0; i < 30; i++) { s.time += 1 / 100.0, s.eye[0] += .01; tfp_view_log_frame(&log, &s); }
  CHECK(logged("Cut: largest camera jump 0.000 m in the 0.30 s after entering the native view")); /* said even when smooth */
  for (int i = 0; i < 30; i++) { s.time += 1 / 100.0, s.eye[0] += .01; tfp_view_log_frame(&log, &s); }
  /* the game takes the camera: the drop line names the gate, the action layers and the camera helper's state */
  s.has_lease = false, s.phase = "waiting", s.wait = "Gameplay action or camera owns the view", s.camera_kind = 4, s.safe = false;
  s.gate = "kind=4 safe=0 mask=0x20", s.action = "layers=3:44", s.camera = "cam=active replaced=7";
  s.time += 1 / 100.0, s.eye[0] += .01;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Drop: kind=4 safe=0 mask=0x20 | layers=3:44 | cam=active replaced=7"));
  s.time += 1 / 100.0, s.eye[0] += .01, s.eye[2] += .40; /* the cut, one frame later */
  tfp_view_log_frame(&log, &s);
  int lines_before = line_count;
  for (int i = 0; i < 10; i++) { s.time += 1 / 100.0, s.eye[0] += .01; s.action = i & 1 ? "layers=3:45" : "layers=3:46"; tfp_view_log_frame(&log, &s); }
  CHECK(line_count == lines_before); /* the same gate: silent, whatever the animation layers do */
  s.gate = "kind=4 safe=0 mask=0x02";
  s.time += 1 / 100.0, s.eye[0] += .01;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Wait: kind=4 safe=0 mask=0x02 | layers=3:45 | cam=active replaced=7"));
  for (int i = 0; i < 30; i++) { s.time += 1 / 100.0, s.eye[0] += .01; tfp_view_log_frame(&log, &s); }
  CHECK(logged("Cut: largest camera jump 0.400 m in the 0.30 s after leaving the native view"));
  /* a gate that keeps changing is said seven times in one stretch (one above already), then once that more are left out */
  static char gates[12][24];
  lines_before = line_count;
  for (int i = 0; i < 12; i++) {
    snprintf(gates[i], sizeof gates[i], "kind=4 mask=0x%02x", 0x40 + i);
    s.gate = gates[i], s.time += 1 / 100.0;
    tfp_view_log_frame(&log, &s);
  }
  CHECK(line_count == lines_before + 7 && logged("Wait: more gate changes are not logged until the view returns"));
  /* the view returns before the cut window of the leave closed: that window is closed first */
  s.has_lease = true, s.phase = "running", s.camera_kind = 6, s.safe = true, s.gate = "kind=6 safe=1";
  s.time += 1 / 100.0, s.eye[2] -= .25;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Return: kind=6 safe=1 | "));
  s.has_lease = false, s.gate = "kind=4 safe=1";
  s.time += 1 / 100.0;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Cut: largest camera jump 0.250 m in the 0.01 s after entering the native view"));
  /* aim release: kept (the keeper acted on the frame before) says nothing more; not kept says where the button came from */
  s.has_lease = true, s.gate = "kind=6 safe=1 active=1", s.controller_active = true, s.time += 1;
  tfp_view_log_frame(&log, &s);
  s.aim_keeps = 1, s.time += 1 / 100.0;
  tfp_view_log_frame(&log, &s);
  s.controller_active = false, s.gate = "kind=6 safe=1 active=0", s.time += 1 / 100.0;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("State: aim out") && !logged("Aim: release not kept"));
  s.controller_active = true, s.time += 1;
  tfp_view_log_frame(&log, &s);
  s.controller_active = false, s.has_lease = false, s.gate = "kind=4 safe=1 active=0", s.time += 1 / 100.0;
  tfp_view_log_frame(&log, &s);
  CHECK(logged("Aim: release not kept (aim button from engine): kind=4 safe=1 active=0"));
  /* a build that fills none of the new fields logs as before */
  tfp_view_log_init(&log, capture, NULL);
  memset(&s, 0, sizeof s);
  line_count = 0;
  s.phase = "idle", s.label = "Idle";
  tfp_view_log_frame(&log, &s);
  s.time = 5, s.enabled = s.has_lease = true, s.phase = "running", s.label = "On";
  tfp_view_log_frame(&log, &s);
  s.has_lease = false, s.time += .5;
  tfp_view_log_frame(&log, &s);
  CHECK(!logged("Drop:") && !logged("Return:") && !logged("Wait:"));

  /* ---- the whole binder with no game behind it ---- */
  static const TfpCameraHelperApi camera = {h_validate_c, h_begin_c, h_end, h_stats_c, h_error, h_generation, h_handoff, h_u32, h_u32};
  static const TfpFovHelperApi fov = {h_validate_f, h_begin_f, h_bind_f, h_transition_f, h_end, h_stats_f, h_error};
  static const TfpNearHelperApi near_clip = {h_begin_n, h_set_n, h_end, h_stats_n, h_error};
  static const TfpBlurHelperApi blur = {h_validate_b, h_begin_b, h_end, h_stats_b, h_error};
  TfpOpsHost memory;
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = no_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  TfpWirePlatform platform = {NULL, test_clock, test_mouse, capture};
  TfpWireHelpers helpers = {&camera, &fov, &near_clip, &blur};
  TfpRtValue r[30];
  line_count = 0;
  tfp_wire_init(&wire, &memory, &platform, &helpers);
  /* every pointer of the host is bound, except the ones retail's runtime tests for */
  size_t unbound = 0;
  void *const *slots = (void *const *)&wire.host.backend_new;
  size_t slot_count = (sizeof wire.host - offsetof(TfpRtHost, backend_new)) / sizeof(void *);
  for (size_t i = 0; i < slot_count; i++) unbound += slots[i] == NULL;
  CHECK(unbound == 10); /* 4 menu options, 2 arm modules, head_sdk_handles, camera_is_pinned, camera_look_stats, mod_set */
  CHECK(!wire.host.menu_near_options && !wire.host.arm_pieces_new && !wire.host.grip_hand_new && !wire.host.mod_set &&
        !wire.host.camera_is_pinned && !wire.host.camera_look_stats && !wire.host.head_sdk_handles && !wire.host.menu_show_torso &&
        !wire.host.menu_fird_person_arms && !wire.host.menu_motion_options);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_update, NULL, r) == TFP_RT_OK); /* before enable: nothing */
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK);
  CHECK(logged("Idle. F11 toggles True First Person"));
  CHECK(wire.rt.has[TFP_RT_O_CONTROLLER] && !wire.rt.has[TFP_RT_O_ARM_PIECES] && !wire.rt.has[TFP_RT_O_GRIP_HAND]);
  CHECK(wire.ops.has_pose_placement && wire.ops.pose_placement.x == .15 && wire.ops.pose_placement.z == -.12); /* on_enable's set_placement */
  for (int i = 0; i < 5; i++) {
    now_seconds += 1 / 60.0;
    tfp_wire_frame(&wire, NULL);
  }
  CHECK(!wire.controller.enabled && wire.rt.idle_clean.kind == TFP_RT_TRUE && !logged("Refused"));
  wire.toggle_requested = true; /* F11 with no game: the controller refuses with the backend's own text */
  now_seconds += 1 / 60.0;
  tfp_wire_frame(&wire, NULL);
  CHECK(!wire.toggle_requested && logged("State: enter requested (F11)") && logged("Refused: F11 enter: Start refused: game.dll unavailable"));
  CHECK(!wire.controller.enabled && !wire.controller.lease);
  now_seconds -= 5; /* a clock that goes back: the frame raises as retail's does, and says so once */
  tfp_wire_frame(&wire, NULL);
  tfp_wire_frame(&wire, NULL);
  int raised_lines = 0;
  for (int i = 0; i < line_count; i++) raised_lines += strstr(lines[i], "Engine FPV clock moved backwards") != NULL;
  CHECK(raised_lines == 2); /* retail's own "Presentation diagnostics unavailable: ..." and the binder's "the frame raised: ..." */
  now_seconds += 10;
  tfp_wire_frame(&wire, NULL);
  /* the debug entries that end in a stub answer with a text, never a crash */
  CHECK(tfp_wire_call(&wire, TFP_RT_E_start_fird, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_FALSE);
  CHECK(r[1].kind == TFP_RT_TEXT && !strncmp(r[1].text, "Fird Person render placement is unavailable", r[1].length));
  CHECK(tfp_wire_call(&wire, TFP_RT_E_status, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_TEXT);
  CHECK(tfp_wire_call(&wire, TFP_RT_E_placement, NULL, r) == TFP_RT_OK);

  /* ---- the controller's placement and status through the host pointers (controller.lua:102-147) ---- */
  {
    TfpRtHost *h = &wire.host;
    TfpRtTable t;
    TfpRtValue a, b, none = {.kind = TFP_RT_NIL}, number = {.kind = TFP_RT_NUMBER, .number = 5};
    TfpRtValue shipped_ref = {.kind = TFP_RT_REF, .ref = tfp_rt_default_placement}, empty = {.kind = TFP_RT_REF, .ref = &tfp_rt_empty_table};
    static const double saved_ok[6] = {.2, -.1, .05, 12, -90, 3.5}, saved_tilted[6] = {.2, -.1, .05, 12, 91, 0}, saved_far[6] = {.2, .6, 0, 0, 0, 0};
    static const char unknown = 0;
    TfpRtValue unknown_ref = {.kind = TFP_RT_REF, .ref = &unknown};
    /* on_enable's set_placement went through the controller: six numbers reached the backend's defaults */
    CHECK(wire.ops.pose_placement.present == 63 && wire.ops.pose_placement.yaw == 0 && wire.controller.placement.kind[TFP_AXIS_ROLL] == TFP_AXIS_NUMBER);
    CHECK(h->controller_get_placement(h, &t) && t.present && t.f[TFP_RT_PLACE_X].number == .15 && t.f[TFP_RT_PLACE_Z].number == -.12);
    CHECK(t.f[TFP_RT_PLACE_YAW].kind == TFP_RT_NUMBER && t.f[TFP_RT_PLACE_YAW].number == 0 && t.f[TFP_RT_PLACE_ROLL].number == 0);
    CHECK(h->controller_set_placement(h, number, none, &a, &b) && a.kind == TFP_RT_FALSE && text_is(b, "Placement requires XYZ"));
    CHECK(h->controller_set_placement(h, none, none, &a, &b) && a.kind == TFP_RT_FALSE && text_is(b, "Placement requires XYZ"));
    CHECK(h->controller_set_placement(h, empty, none, &a, &b) && a.kind == TFP_RT_FALSE &&
          text_is(b, "Placement must be finite XYZ within 0.5 metres"));
    CHECK(h->controller_set_placement(h, unknown_ref, none, &a, &b) && a.kind == TFP_RT_FALSE &&
          text_is(b, "Placement table is not one the binder can read")); /* no decoder: the binder's own refusal */
    CHECK(h->controller_set_placement(h, shipped_ref, none, &a, &b) && a.kind == TFP_RT_TRUE && b.kind == TFP_RT_NIL);
    wire.placement_decode = saved_decode;
    TfpRtValue saved = {.kind = TFP_RT_REF, .ref = saved_tilted};
    CHECK(h->controller_set_placement(h, saved, none, &a, &b) && a.kind == TFP_RT_FALSE && text_is(b, "Angles must be within90degrees"));
    saved.ref = saved_far;
    CHECK(h->controller_set_placement(h, saved, none, &a, &b) && a.kind == TFP_RT_FALSE &&
          text_is(b, "Placement must be finite XYZ within 0.5 metres"));
    CHECK(wire.ops.pose_placement.x == .15 && wire.controller.placement.v[TFP_AXIS_X] == .15); /* a refusal changes nothing */
    saved.ref = saved_ok;
    CHECK(h->controller_set_placement(h, saved, none, &a, &b) && a.kind == TFP_RT_TRUE && b.kind == TFP_RT_NIL);
    CHECK(wire.ops.pose_placement.x == .2 && wire.ops.pose_placement.yaw == 12 && wire.ops.pose_placement.pitch == -90 &&
          wire.ops.pose_placement.roll == 3.5 && wire.controller.placement.v[TFP_AXIS_PITCH] == -90);
    CHECK(h->controller_get_placement(h, &t) && t.f[TFP_RT_PLACE_Y].number == -.1 && t.f[TFP_RT_PLACE_YAW].number == 12 &&
          t.f[TFP_RT_PLACE_ROLL].number == 3.5);
    CHECK(h->controller_placement_target(h, &a, &b) && a.kind == TFP_RT_NIL && b.kind == TFP_RT_NIL); /* no lease */
    /* a session that never began: the label and the observation count, no arms / weapon counts */
    CHECK(h->controller_status(h, &a) && text_is(a, "Start refused: game.dll unavailable; camera6/aimfalse observations=0"));
    /* With a running lease the lease call is the one asked, and the target names the weapon and the lease's origin.
     * No game stands behind this lease, so the backend refuses it in its own words. */
    TfpWireLease *lease = calloc(1, sizeof *lease);
    CHECK(lease != NULL);
    lease->ops.origin = sample(true);
    wire.controller.lease = &lease->view, wire.controller.phase = TFP_PHASE_RUNNING;
    CHECK(h->controller_set_placement(h, shipped_ref, none, &a, &b) && a.kind == TFP_RT_FALSE &&
          text_is(b, "Pose placement lease is not active or owned"));
    CHECK(wire.ops.pose_placement.x == .2 && wire.controller.placement.v[TFP_AXIS_X] == .2);
    CHECK(h->controller_placement_target(h, &a, &b) && a.kind == TFP_RT_NIL); /* the backend names no weapon yet */
    wire.ops.has_placement_weapon_key = true;
    snprintf(wire.ops.placement_weapon_key, sizeof wire.ops.placement_weapon_key, "%s", "00000000000003a0");
    char expected[24];
    tfp_wire_identity_text(expected, 'W', tfp_ops_identity_key(&lease->ops.origin));
    CHECK(h->controller_placement_target(h, &a, &b) && text_is(a, "00000000000003a0") && text_is(b, expected));
    wire.controller.phase = TFP_PHASE_CLEANUP_PENDING; /* held but not running: no target, the defaults call */
    CHECK(h->controller_placement_target(h, &a, &b) && a.kind == TFP_RT_NIL && b.kind == TFP_RT_NIL);
    CHECK(h->controller_set_placement(h, shipped_ref, none, &a, &b) && a.kind == TFP_RT_TRUE && wire.ops.pose_placement.x == .15);
    wire.controller.lease = NULL, wire.controller.phase = TFP_PHASE_IDLE;
    wire.ops.has_placement_weapon_key = false;
    wire.placement_decode = NULL;
    free(lease);
  }
  { /* the aim button between ticks (src/wire_action.h): the keeper from a probe, on the tick's snapshot */
    static TfpWire between;
    static const uint8_t active[6] = {0, 0, 0, 0, 1, 0};
    TfpOpsHost real = memory;
    TfpOpsSnapshot view;
    real.read = aim_read, real.write = aim_write;
    tfp_wire_init(&between, &real, &platform, &helpers);
    CHECK(tfp_wire_call(&between, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK); /* the backend exists from here */
    memset(&view, 0, sizeof view);
    view.safe = view.controller_active = view.request_local = true;
    view.camera_kind = 6, view.controller = AIM_CONTROLLER, view.record = AIM_RECORD;
    memcpy(view.controller_bytes, active, 6);
    memcpy(aim_controller, active, 6), aim_sight[0] = 0x3f;
    CHECK(!tfp_wire_aim_due(&between)); /* nothing armed */
    now_seconds += 1;
    tfp_aim_watch_frame(&between.aim, now_seconds, true, true);
    between.aim_snapshot = view, between.aim_has_snapshot = true;
    tfp_aim_watch_note(&between.aim, 1, 1), tfp_aim_watch_note(&between.aim, 2, 0);
    /* full aim has only just begun: a change is the press arriving late, nothing is written */
    now_seconds += .010;
    CHECK(tfp_wire_aim_due(&between) && !tfp_wire_aim_probe(&between, 0, 0) && aim_writes == 0 && between.aim.guarded == 1);
    /* the next tick; held; then the release, seen between ticks */
    now_seconds += .5;
    tfp_aim_watch_frame(&between.aim, now_seconds, true, true);
    between.aim_snapshot = view, between.aim_has_snapshot = true;
    tfp_aim_watch_note(&between.aim, 1, 1);
    now_seconds += .002;
    CHECK(tfp_wire_aim_due(&between) && !tfp_wire_aim_probe(&between, 1, 0) && aim_writes == 0);
    now_seconds += .002;
    CHECK(tfp_wire_aim_due(&between) && tfp_wire_aim_probe(&between, 0, 0));
    CHECK(aim_controller[4] == 0 && aim_sight[0] == 0 && aim_writes == 2 && between.ops.aim_view_keeps == 1 && between.aim.probe_keeps == 1);
    /* one change, one try: nothing more this frame */
    aim_controller[4] = 1;
    now_seconds += .002;
    CHECK(!tfp_wire_aim_due(&between) && !tfp_wire_aim_probe(&between, 1, 0) && aim_writes == 2);
    /* the controller changed since the tick's snapshot (the game acted first): no write */
    now_seconds += .5;
    tfp_aim_watch_frame(&between.aim, now_seconds, true, true);
    between.aim_snapshot = view, between.aim_has_snapshot = true;
    tfp_aim_watch_note(&between.aim, 1, 1);
    aim_controller[0] = 2, aim_sight[0] = 0x3f;
    now_seconds += .002;
    CHECK(tfp_wire_aim_due(&between) && !tfp_wire_aim_probe(&between, 0, 0) && aim_writes == 2 && aim_sight[0] == 0x3f && between.aim.probe_keeps == 1);
    /* the runtime's own call obeys the same guard: young full aim is not kept */
    aim_controller[0] = 0, aim_controller[4] = 1;
    tfp_aim_watch_frame(&between.aim, now_seconds + 1, false, true);
    tfp_aim_watch_frame(&between.aim, now_seconds + 1.01, true, true);
    now_seconds += 1.02;
    TfpRtTable table;
    tfp_wire_snapshot_table(&between, &view, &table);
    CHECK(between.host.backend_keep_aim_view(&between.host, &table) && aim_writes == 2);
    now_seconds += 1;
    CHECK(between.host.backend_keep_aim_view(&between.host, &table) && aim_writes == 4 && between.ops.aim_view_keeps == 2);
    CHECK(tfp_wire_call(&between, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK);
  }
  { /* the action hold probe (hold_lease.txt): the switch, the hooks, the lines */
    static TfpWire off, on;
    static TfpWireLease held_lease;
    char text[640];
    /* without the switch: no hook call, no line, the controller's hold off, whatever the controller's state says */
    tfp_wire_init(&off, &memory, &platform, &helpers);
    off.hold.apply = hold_apply, off.hold.probe = hold_probe;
    CHECK(tfp_wire_call(&off, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK && !off.controller.hold_enabled && !tfp_wire_hold_on(&off));
    line_count = 0, hold_applies = hold_probes = 0;
    off.controller.enabled = true, off.controller.holding = true;
    tfp_wire_frame(&off, NULL);
    tfp_wire_hold_frame(&off);
    CHECK(hold_applies == 0 && hold_probes == 0 && !logged("Hold:") && !logged("State: hold"));
    off.controller.enabled = off.controller.holding = false;
    CHECK(tfp_wire_call(&off, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK);
    /* with it: the controller made by on_enable has its hold on, the hooks get the controller's state each tick */
    tfp_wire_init(&on, &memory, &platform, &helpers);
    tfp_wire_hold_set(&on, true);
    on.hold.apply = hold_apply, on.hold.probe = hold_probe;
    CHECK(tfp_wire_call(&on, TFP_RT_E_on_enable, NULL, r) == TFP_RT_OK && on.controller.hold_enabled && tfp_wire_hold_on(&on));
    line_count = 0;
    tfp_wire_frame(&on, NULL);
    CHECK(hold_applies == 1 && !hold_armed && !hold_holding && !logged("Hold:")); /* the view is off: the hooks hear "no", nothing is written */
    on.controller.enabled = true;
    on.controller.lease = &held_lease.view, on.controller.phase = TFP_PHASE_RUNNING;
    held_lease.view.request = held_lease.ops.request = 0x1200, held_lease.ops.origin = sample(true);
    tfp_wire_hold_frame(&on);
    CHECK(hold_applies == 2 && hold_armed && !hold_holding);
    CHECK(logged("Hold: player not proven (") && logged("holding=0 why=-")); /* this test has no game memory behind it */
    on.controller.holding = true, on.controller.holds = 1;
    snprintf(on.controller.hold_why, sizeof on.controller.hold_why, "Gameplay action or camera owns the view");
    line_count = 0;
    tfp_wire_hold_frame(&on);
    CHECK(hold_armed && hold_holding && logged("State: holding the native view through an action") && logged("holding=1 why=Gameplay action"));
    int said = line_count;
    tfp_wire_hold_frame(&on); /* one line per tick while holding, the State line only on the edge */
    CHECK(line_count == said + 1);
    on.controller.holding = false, on.controller.hold_exits = 1;
    snprintf(on.controller.hold_why, sizeof on.controller.hold_why, "the cap was reached (4000 ms)");
    line_count = 0;
    tfp_wire_hold_frame(&on);
    CHECK(logged("State: hold ended: the cap was reached (4000 ms)") && !hold_holding);
    /* ten more ticks after the edge, then rate-limited while nothing happens: one line a second */
    line_count = 0;
    for (int i = 0; i < 12; i++) { now_seconds += 1 / 60.0; tfp_wire_hold_frame(&on); }
    CHECK(line_count == 10);
    line_count = 0;
    for (int i = 0; i < 30; i++) { now_seconds += 1 / 60.0; tfp_wire_hold_frame(&on); }
    CHECK(line_count <= 1);
    now_seconds += 1;
    tfp_wire_hold_frame(&on);
    CHECK(line_count >= 1 && line_count <= 2);
    /* the line itself, from a view and a probe */
    TfpOpsHold view;
    TfpWireHoldProbe extra;
    memset(&view, 0, sizeof view);
    memset(&extra, 0, sizeof extra);
    view.player = sample(false), view.player.controller_bytes[0] = 2, view.player.controller_bytes[4] = 0, view.player.controller_bytes[5] = 0;
    view.player.request_weapon = 912, view.wielded = 913;
    extra.gun_parent = "body", extra.has_camera = true, extra.camera_parent_is_rig_eye = true, extra.has_pose = true, extra.pose_armed = true;
    extra.pose_error = 0, extra.has_eye = extra.has_rig_eye = true;
    extra.eye[0] = 1, extra.eye[1] = 2, extra.eye[2] = 3.5, extra.rig_eye[0] = 1, extra.rig_eye[1] = 2, extra.rig_eye[2] = 3.25;
    on.controller.holding = true;
    snprintf(on.controller.hold_why, sizeof on.controller.hold_why, "Gameplay action or camera owns the view");
    tfp_wire_hold_line(&on, &view, NULL, &extra, 3, text, sizeof text);
    CHECK(!strcmp(text, "Hold: mask=00000002 active=0 byte5=0 kind=6 record_ours=1 request_weapon=912 wielded=913 gun_parent=body "
                        "camera_parent_is_rig_eye=1 pose_armed=1 pose_error=0 node5_calls=+3 eye=(1.000 2.000 3.500) rig_eye=(1.000 2.000 3.250) "
                        "holding=1 why=Gameplay action or camera owns the view"));
    view.player.record = 0x1300, extra.has_camera = extra.has_pose = extra.has_eye = extra.has_rig_eye = false, extra.gun_parent = NULL;
    on.controller.holding = false;
    tfp_wire_hold_line(&on, &view, NULL, &extra, 0, text, sizeof text);
    CHECK(strstr(text, "record_ours=0") && strstr(text, "gun_parent=- camera_parent_is_rig_eye=- pose_armed=- pose_error=- node5_calls=+0 eye=- rig_eye=- holding=0"));
    on.controller.enabled = false, on.controller.lease = NULL, on.controller.phase = TFP_PHASE_IDLE;
    CHECK(tfp_wire_call(&on, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK);
  }
  CHECK(tfp_wire_call(&wire, TFP_RT_E_on_disable, NULL, r) == TFP_RT_OK && r[0].kind == TFP_RT_TRUE);
  CHECK(!wire.rt.has[TFP_RT_O_CONTROLLER]);
  CHECK(helper_calls == 0); /* no helper was reached: nothing was armed, nothing to disarm */
  printf("wire glue: %u checks passed (value adapters, placement rule, recovery texts, session log, the binder without a game)\n", checks);
  return 0;
}
