/* Replays tests/vectors/motion_runtime_vectors.h against src/motion_runtime.c: every operation
 * the retail motion_runtime.lua was run through under LuaJIT, with the same scripted fakes
 * (tools/retail_ref/run_motion_runtime.lua builds its own from the same knobs). Compared per
 * operation, as one text: the call's answer, the ordered trace of every call into a fake with its
 * arguments (log lines among them), and the whole state. Doubles are compared by their bits. */
#include "motion_runtime.h"
#include "vectors/motion_runtime_vectors.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double w[K_COUNT];
static TfpMotionRuntimeLease leases[MR_LEASES + 1];
static char trace[65536], state_text[4096], last_state[4096], line[72000];
static size_t trace_used;
static unsigned clock_calls;
static TfpMotionRuntimeHost host;
static TfpMotionRuntime runtime;
static bool has_runtime;

static void t(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void t(const char *format, ...) {
  va_list args;
  if (trace_used && trace_used < sizeof trace - 1) trace[trace_used++] = ';';
  va_start(args, format);
  int n = vsnprintf(trace + trace_used, sizeof trace - trace_used, format, args);
  va_end(args);
  if (n > 0) trace_used += (size_t)n < sizeof trace - trace_used ? (size_t)n : sizeof trace - trace_used - 1;
}
/* A number as the Lua runner prints it. Four buffers, so one printf can hold several. */
static const char *num(double x) {
  static char ring[16][24];
  static unsigned at;
  char *out = ring[at++ % 16];
  if (x != x) strcpy(out, "nan");
  else if (x == floor(x) && x > -2147483648.0 && x < 2147483648.0 && !(x == 0 && signbit(x))) snprintf(out, 24, "%d", (int)x);
  else {
    uint64_t bits;
    memcpy(&bits, &x, 8);
    snprintf(out, 24, "x%016llx", (unsigned long long)bits);
  }
  return out;
}
static int knob(int k) { return (int)w[k]; }
static const char *S(int k) { return knob(k) ? mr_strings[knob(k)] : NULL; }
static int lease_number(const TfpMotionRuntimeLease *lease) { return (int)(lease - leases); }
static const char *L(const TfpMotionRuntimeLease *lease) {
  static char out[2][16];
  static unsigned at;
  char *text = out[at++ % 2];
  if (!lease) strcpy(text, "L~");
  else snprintf(text, 16, "L%d", lease_number(lease));
  return text;
}
static const char *place_text(const TfpActionPlacement *p) {
  static char out[200];
  if (!p) return "~";
  const double v[6] = {p->x, p->y, p->z, p->yaw, p->pitch, p->roll};
  out[0] = 0;
  for (int i = 0; i < 6; i++) {
    if (i) strcat(out, " ");
    strcat(out, p->present & (1u << i) ? num(v[i]) : "~");
  }
  return out;
}
static void placement(int first, int present, TfpActionPlacement *p) {
  p->x = w[first]; p->y = w[first + 1]; p->z = w[first + 2]; p->yaw = w[first + 3]; p->pitch = w[first + 4]; p->roll = w[first + 5];
  p->present = (unsigned)knob(present);
}
static void options_from(int first, TfpMotionOptions *o) { /* first: the knob of `enabled` */
  o->enabled = (TfpMotionFlag)knob(first);
  o->scale.kind = (TfpMotionKind)knob(first + 1);
  o->scale.value = w[first + 2];
  for (int i = 0; i < 9; i++) {
    o->setting[i].kind = (TfpMotionKind)knob(first + 3 + i);
    o->setting[i].value = w[first + 12 + i];
  }
  o->editor_frozen = (TfpMotionFlag)knob(first + 21);
  o->shoulder_aim_enabled = (TfpMotionFlag)knob(first + 22);
}

/* ------------------------------------------------------------------ the fakes */
static TfpMotionRuntimeGot h_result(void *user, TfpMotionRuntimeControllerResult *out) {
  (void)user;
  t("result");
  if (knob(K_c_result) == 1) return TFP_MOTION_GOT_RAISED;
  if (knob(K_c_result) == 2) return TFP_MOTION_GOT_NIL;
  out->pose_offset = knob(K_c_mode) == 0;
  out->running = knob(K_c_phase) == 0;
  return TFP_MOTION_GOT_VALUE;
}
static TfpMotionRuntimeGot h_enabled(void *user) {
  (void)user;
  t("enabled");
  if (knob(K_c_enabled) == 2) return TFP_MOTION_GOT_RAISED;
  return knob(K_c_enabled) == 1 || knob(K_c_enabled) == 3 ? TFP_MOTION_GOT_VALUE : TFP_MOTION_GOT_NIL;
}
static TfpMotionRuntimeGot h_get(void *user, TfpActionPlacement *out) {
  (void)user;
  t("get");
  if (knob(K_c_get) == 2) return TFP_MOTION_GOT_RAISED;
  if (knob(K_c_get) == 1) return TFP_MOTION_GOT_NIL;
  placement(K_c_x, K_c_present, out);
  return TFP_MOTION_GOT_VALUE;
}
static bool h_set(void *user, const TfpActionPlacement *value, const char **why) {
  (void)user;
  t("set(%s)", place_text(value));
  if (knob(K_c_set) == 3) { *why = S(K_c_set_why) ? S(K_c_set_why) : "set placement raised"; return false; }
  if (knob(K_c_set) == 2) { *why = S(K_c_set_why); return false; }
  if (knob(K_c_set) == 4) return false;
  if (knob(K_c_set) == 0 && value) {
    const double v[6] = {value->x, value->y, value->z, value->yaw, value->pitch, value->roll};
    for (int i = 0; i < 6; i++)
      if (value->present & (1u << i)) w[K_c_x + i] = v[i];
    w[K_c_present] = value->present;
  }
  return true;
}
static bool h_pose(void *user, const TfpMotionRuntimeLease *lease, const TfpMotionRuntimeModifier *value, const char **why) {
  (void)user;
  const double *a = value->axis;
  t("pose(%s %s %s %s %s %s %s %s %s)", L(lease), num(a[0]), num(a[1]), num(a[2]), num(a[3]), num(a[4]), num(a[5]), num(a[6]),
    value->authored_ads ? "A" : "-");
  if (knob(K_b_pose) == 3) { *why = S(K_b_pose_why) ? S(K_b_pose_why) : "pose raised"; return false; }
  if (knob(K_b_pose) == 2) { *why = S(K_b_pose_why); return false; }
  if (knob(K_b_pose) == 4) return false;
  return true;
}
static bool h_ads(void *user, const TfpMotionRuntimeLease *lease, const TfpActionPlacement *ads, const char **why) {
  (void)user;
  t("ads(%s %s)", L(lease), place_text(ads));
  if (knob(K_b_ads_set) == 3) { *why = S(K_b_ads_why) ? S(K_b_ads_why) : "ads placement raised"; return false; }
  if (knob(K_b_ads_set) == 2) { *why = S(K_b_ads_why); return false; }
  if (knob(K_b_ads_set) == 4) return false;
  return true;
}
static bool h_hint(void *user, bool *controller_active, bool *shoulder_aim) {
  (void)user;
  t("hint");
  if (knob(K_b_hint) != 0) return false;
  *controller_active = knob(K_b_hint_active) != 0;
  *shoulder_aim = knob(K_b_hint_shoulder) != 0;
  return true;
}
static TfpMotionRuntimeGot h_action_placement(void *user, const char *key, TfpAct action, TfpActionPlacement *out) {
  (void)user;
  t("ap(%s,%s)", key ? key : "~", tfp_action_name(action));
  int first = K_ap_idle_mode + 8 * (int)action;
  if (knob(first) == 2) return TFP_MOTION_GOT_RAISED;
  if (knob(first) == 1) return TFP_MOTION_GOT_NIL;
  placement(first + 1, first + 7, out);
  return TFP_MOTION_GOT_VALUE;
}
static const TfpActionMapRow map_rows_1[] = {{"4D1C334D294DFA97", "75:71", "sprint"}, {"4D1C334D294DFA97", "5:6", "reload"},
                                             {"4D1C334D294DFA97", "nil:nil", "prone"}, {"4D1C334D294DFA97", "1:2", "unknown"}};
static const TfpActionMapRow map_rows_2[] = {{"0000000000000000", "5:6", "crouch"}, {"4D1C334D294DFA97", "7:nil", "dive"},
                                             {"4D1C334D294DFA97", "nil:9", "slide"}};
static const TfpActionMapping mappings[] = {{map_rows_1, 4}, {map_rows_2, 3}};
static TfpMotionRuntimeGot h_mapping(void *user, const TfpActionMapping **out) {
  (void)user;
  t("mapping");
  if (knob(K_b_mapping_mode) == 2) return TFP_MOTION_GOT_RAISED;
  if (knob(K_b_mapping_mode) == 0) return TFP_MOTION_GOT_NIL;
  *out = &mappings[knob(K_b_mapping_set) - 1];
  return TFP_MOTION_GOT_VALUE;
}
static bool h_transition(void *user, TfpTransitionPacket *out) {
  (void)user;
  t("pt");
  if (knob(K_b_pt) != 1) return false;
  memset(out, 0, sizeof *out);
  out->epoch = (uint64_t)w[K_b_pt_epoch];
  out->progress = w[K_b_pt_progress];
  out->ads = knob(K_b_pt_ads) != 0;
  return true;
}
static void h_action(void *user, TfpAct action, double now) {
  (void)user;
  t("pa(%s,%s)", tfp_action_name(action), num(now));
}
static void h_resume(void *user, uint64_t identity, uint64_t player_identity, double now) {
  (void)user;
  t("pr(%llu,%llu,%s)", (unsigned long long)identity, (unsigned long long)player_identity, num(now));
}
static bool h_active(void *user, bool *active, const char **problem) {
  (void)user;
  t("active");
  if (knob(K_e_active) == 3) { *problem = S(K_e_why) ? S(K_e_why) : "editor raised"; return false; }
  if (knob(K_e_active) == 2) { *problem = S(K_e_why); return false; }
  *active = knob(K_e_active) == 1;
  return true;
}
static TfpMotionRuntimeGot h_preview(void *user, TfpActionPlacement *out) {
  (void)user;
  t("preview");
  if (knob(K_e_preview_mode) == 2) return TFP_MOTION_GOT_RAISED;
  if (knob(K_e_preview_mode) == 1) return TFP_MOTION_GOT_NIL;
  memset(out, 0, sizeof *out);
  out->yaw = w[K_e_yaw]; out->pitch = w[K_e_pitch]; out->roll = w[K_e_roll];
  out->present = ((unsigned)knob(K_e_present) & 7u) << 3;
  return TFP_MOTION_GOT_VALUE;
}
static bool h_sample(void *user, const TfpMotionRuntimeLease *lease, TfpLocomotionSample *out, const char **why) {
  (void)user;
  t("sample(%s)", L(lease));
  if (knob(K_s_mode) == 3) { *why = S(K_s_why) ? S(K_s_why) : "sample raised"; return false; }
  if (knob(K_s_mode) == 1) { *why = S(K_s_why); return false; }
  if (knob(K_s_mode) == 2) return false;
  memset(out, 0, sizeof *out);
  out->player.body = (uint32_t)knob(K_s_owner); /* the owner: equal numbers, equal player identities */
  out->x = w[K_s_x]; out->y = w[K_s_y]; out->z = w[K_s_z]; out->yaw = w[K_s_yaw]; out->pitch = w[K_s_pitch];
  out->ads = knob(K_s_ads) != 0;
  out->shoulder_aim = knob(K_s_shoulder) != 0;
  out->sprint = knob(K_s_sprint) != 0;
  out->reload = knob(K_s_reload) != 0;
  for (int i = 0; i < 24; i++) out->stance_flags[i] = (uint8_t)knob(K_s_f1 + i);
  out->has_action = knob(K_s_has_action) != 0;
  if (out->has_action) {
    snprintf(out->action_resource, sizeof out->action_resource, "%s", S(K_s_resource) ? S(K_s_resource) : "");
    out->action_verified = knob(K_s_verified) != 0;
    out->has_l1 = knob(K_s_has_l) & 1; out->has_l3 = (knob(K_s_has_l) & 2) != 0; out->has_l14 = (knob(K_s_has_l) & 4) != 0;
    out->l1 = knob(K_s_l1); out->l3 = knob(K_s_l3); out->l14 = knob(K_s_l14);
  }
  for (int i = 0; i < 16; i++) { out->camera_basis[i] = w[K_s_cam1 + i]; out->parent_basis[i] = w[K_s_par1 + i]; }
  return true;
}
static bool h_to_parent(void *user, const TfpLocomotionValue *value, const double camera[16], const double parent[16],
                        TfpLocomotionParent *out, const char **error) {
  (void)user;
  const double v[6] = {value->dx, value->dy, value->dz, value->pitch, value->yaw, value->roll};
  char row[200] = "";
  for (int i = 0; i < 6; i++) {
    if (i) strcat(row, " ");
    strcat(row, value->present & (1u << i) ? num(v[i]) : "~");
  }
  t("tp(%s)", row);
  if (knob(K_tp_mode) == 1) { *error = S(K_tp_why) ? S(K_tp_why) : "to_parent raised"; return false; }
  if (knob(K_tp_mode) == 0) return tfp_locomotion_to_parent(value, camera, parent, out, error);
  out->dx = w[K_tp_dx]; out->dy = w[K_tp_dy]; out->dz = w[K_tp_dz];
  out->qx = w[K_tp_qx]; out->qy = w[K_tp_qy]; out->qz = w[K_tp_qz]; out->qw = w[K_tp_qw];
  return true;
}
static bool h_clock(void *user, double *seconds) {
  (void)user;
  t("clock");
  clock_calls++;
  int mode = knob(K_clock_mode1 + (int)(clock_calls < 4 ? clock_calls : 4) - 1);
  double now = w[K_time];
  w[K_time] = w[K_time] + w[K_clock_step];
  if (mode == 1 || mode == 2) return false;
  *seconds = mode == 3 ? NAN : mode == 4 ? INFINITY : mode == 5 ? -INFINITY : now;
  return true;
}
static bool h_options(void *user, TfpMotionOptions *out) {
  (void)user;
  if (knob(K_ctor_options) == 1) { options_from(K_o_enabled, out); return true; } /* a fixed table: nothing is called */
  t("options");
  if (knob(K_o_mode) != 0) return false;
  options_from(K_o_enabled, out);
  return true;
}
static void h_log(void *user, const char *text) {
  (void)user;
  t("log(%s)", text);
}

static void refresh(void) {
  for (int i = 1; i <= MR_LEASES; i++) {
    TfpMotionRuntimeLease *lease = leases + i;
    int first = K_l1_done + 6 * (i - 1);
    lease->done = knob(first) != 0;
    lease->cleanup_only = knob(first + 1) != 0;
    lease->pose_blend_entry = knob(first + 2) != 0;
    lease->has_origin = knob(first + 3) != 0;
    /* 0: nil; n > 0: the n-th text; n < 0: the number -n, which is no text (the key is then to be ignored). */
    lease->has_identity = lease->has_origin && knob(first + 4) > 0;
    lease->has_player_identity = lease->has_origin && knob(first + 5) > 0;
    lease->identity = (uint64_t)abs(knob(first + 4));
    lease->player_identity = (uint64_t)abs(knob(first + 5));
  }
  host.controller_lease = knob(K_c_lease) ? leases + knob(K_c_lease) : NULL;
  host.alignment_lease = knob(K_b_alignment) ? leases + knob(K_b_alignment) : NULL;
  host.placement_weapon_key = S(K_b_key);
  host.action_placement_revision = knob(K_b_revision_nil) == 0 ? w[K_b_revision] : 0;
  host.action_placement = knob(K_b_has_ap) ? h_action_placement : NULL;
  host.action_mapping = knob(K_b_has_mapping) ? h_mapping : NULL;
  host.presentation_transition = knob(K_b_pt) ? h_transition : NULL;
  host.presentation_action = knob(K_b_pa) ? h_action : NULL;
  host.presentation_resume = knob(K_b_pr) ? h_resume : NULL;
  host.editor_preview_rotation = knob(K_e_has_preview) ? h_preview : NULL;
}
static const char *tri(TfpMotionTri value) { return value == TFP_MOTION_TRI_NIL ? "~" : value == TFP_MOTION_TRI_TRUE ? "T" : "F"; }
static const char *maybe(bool has, double value) { return has ? num(value) : "~"; }
static const char *layer(bool has, int value) { return has ? num(value) : "~"; }
static void describe(void) {
  if (!has_runtime) { strcpy(state_text, "-"); return; }
  TfpMotionRuntimeState s;
  tfp_motion_runtime_result(&runtime, &s);
  int n = snprintf(state_text, sizeof state_text, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s", tfp_motion_runtime_phase_name(s.phase),
                   num((double)s.applied), num((double)s.coalesced), num((double)s.softened), num((double)s.read_failures),
                   num((double)s.write_refusals), num((double)s.time_held), num(s.dx), num(s.dy), num(s.dz), num(s.qx), num(s.qy),
                   num(s.qz), num(s.qw), s.has_reason ? s.reason : "~");
  n += snprintf(state_text + n, sizeof state_text - (size_t)n, " %s %s %s %s %s %s %s %s %s %s %s %s",
                maybe(s.has_sprint_weight, s.sprint_weight), maybe(s.has_bob_weight, s.bob_weight), tri(s.sprint_target), tri(s.sprint_held),
                maybe(s.has_shoulder_weight, s.shoulder_weight), maybe(s.has_ads_handoff, s.ads_handoff), tri(s.capture_ready),
                tri(s.capture_native_ads), tri(s.ads), s.action == TFP_ACT_NONE ? "~" : tfp_action_name(s.action), tri(s.shoulder_aim),
                tri(s.sprint_raw));
  snprintf(state_text + n, sizeof state_text - (size_t)n, " %s %s %s %s %s %s %s %s", layer(s.has_l1, s.l1), layer(s.has_l3, s.l3),
           layer(s.has_l14, s.l14), s.has_sprint_evidence ? "E" : "~", maybe(s.has_speed, s.speed), maybe(s.has_gait_phase, s.gait_phase),
           !s.has_reset ? "~" : s.reset == TFP_MOTION_RESET_NONE ? "F" : tfp_motion_reset_name(s.reset),
           !s.has_rebased ? "~" : s.rebased == TFP_MOTION_REBASE_NONE ? "F" : tfp_motion_rebase_name(s.rebased));
}
static void answer_text(char *out, size_t size, TfpMotionRuntimeAnswer answer, const char *reason) {
  if (answer == TFP_MOTION_RUNTIME_RAISED) snprintf(out, size, "R");
  else snprintf(out, size, "%s %s", answer == TFP_MOTION_RUNTIME_TRUE ? "T" : "F", reason ? reason : "~");
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], MR_VECTOR_COMMIT)) {
    fprintf(stderr, "motion_runtime: the vectors come from retail %s, not %s\n", MR_VECTOR_COMMIT, argv[1]);
    return 1;
  }
  if (strcmp(tfp_locomotion_sprint_evidence, MR_SPRINT_EVIDENCE)) {
    fprintf(stderr, "motion_runtime: the sprint evidence text differs from retail's\n");
    return 1;
  }
  size_t count = sizeof mr_ops / sizeof *mr_ops, failures = 0, counts[5] = {0}, raised = 0;
  const char *session = "";
  for (size_t i = 0; i < count; i++) {
    const MrOp *op = mr_ops + i;
    char answer[600];
    if (op->kind == MR_BEGIN) {
      memset(w, 0, sizeof w);
      memset(leases, 0, sizeof leases);
      has_runtime = false;
      last_state[0] = 0;
      session = op->name;
    }
    for (unsigned k = 0; k < op->set_count; k++) w[mr_sets[op->set_first + k].knob] = mr_sets[op->set_first + k].value;
    trace_used = 0;
    trace[0] = 0;
    clock_calls = 0;
    counts[op->kind]++;
    if (op->kind == MR_BEGIN) strcpy(answer, "B");
    else if (op->kind == MR_NEW) {
      memset(&host, 0, sizeof host);
      host.clock = knob(K_ctor_bad) == 3 ? NULL : h_clock;
      host.log = knob(K_ctor_log) ? h_log : NULL;
      host.options = knob(K_ctor_options) ? h_options : NULL;
      host.controller_result = h_result;
      host.controller_is_enabled = h_enabled;
      host.controller_get_placement = h_get;
      host.controller_set_placement = h_set;
      host.set_pose_motion = h_pose;
      host.set_ads_placement = h_ads;
      host.player_snapshot = h_hint;
      host.editor_is_active = knob(K_ctor_editor) ? h_active : NULL;
      host.sample = h_sample;
      host.to_parent = knob(K_ctor_tp) ? h_to_parent : NULL;
      refresh();
      TfpMotionOptions model;
      options_from(K_m_enabled, &model);
      const char *error = NULL;
      bool bad = knob(K_ctor_bad) == 1 || knob(K_ctor_bad) == 2;
      memset(&runtime, 0xA5, sizeof runtime);
      has_runtime = tfp_motion_runtime_init(&runtime, bad ? NULL : &host, knob(K_ctor_model) == 2 ? &model : NULL, &error);
      if (has_runtime) strcpy(answer, "N T");
      else snprintf(answer, sizeof answer, "N F %s", error ? error : "~");
    } else {
      const char *reason = NULL;
      TfpMotionRuntimeAnswer got;
      refresh();
      if (!has_runtime) { fprintf(stderr, "motion_runtime: operation %zu has no runtime\n", i); return 1; }
      if (op->kind == MR_UPDATE) got = tfp_motion_runtime_update(&runtime, &reason);
      else if (op->kind == MR_CAPTURE) got = tfp_motion_runtime_capture_neutral(&runtime, knob(K_cap_native) == 1, &reason);
      else got = tfp_motion_runtime_reset(&runtime) ? TFP_MOTION_RUNTIME_TRUE : TFP_MOTION_RUNTIME_FALSE;
      if (got == TFP_MOTION_RUNTIME_RAISED) raised++;
      answer_text(answer, sizeof answer, got, reason);
    }
    describe();
    snprintf(line, sizeof line, "%s|%s|%s", answer, trace, strcmp(state_text, last_state) ? state_text : "=");
    strcpy(last_state, state_text);
    if (strcmp(line, op->expect)) {
      if (failures++ < 8) {
        size_t at = 0;
        while (line[at] && line[at] == op->expect[at]) at++;
        fprintf(stderr, "motion_runtime: operation %zu (%s) differs at character %zu\n  retail: %s\n  C:      %s\n", i, session, at,
                op->expect, line);
      }
    }
  }
  if (failures) {
    fprintf(stderr, "motion_runtime: %zu of %zu operations differ from retail %s\n", failures, count, MR_VECTOR_COMMIT);
    return 1;
  }
  printf("motion_runtime: %zu operations equal to retail %s (%zu updates, %zu captures, %zu resets, %zu constructions, %zu raised)\n", count,
         MR_VECTOR_COMMIT, counts[MR_UPDATE], counts[MR_CAPTURE], counts[MR_RESET], counts[MR_NEW], raised);
  return 0;
}
