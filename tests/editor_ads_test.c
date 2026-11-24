/* The placement editor opened while aiming (round 20, 2026-11-24), from the mouse to what is drawn, with no game.
 *
 * The report, on 0.6.0-feel.50: "In ADS the scope is too far away"; the player opened the placement editor (Page Up)
 * while aiming to pull it closer, "but the weapon would not move". Why, in the code:
 *   - the editor always opened on its first action, idle (placement_editor.c action_index = 1);
 *   - whatever action it shows, its live preview goes to the controller's placement (C_SET_PLACEMENT), which the pose
 *     helper adds to node 5 times the hip weight, and that weight is 0 in full aim (src/pose_helper.c, "full ADS is
 *     never offset");
 *   - the ADS placement, the one full aim does draw, reached the pose helper only from the motion runtime at a new
 *     lease or a saved revision (set_ads_placement), and the motion runtime is frozen while the editor is open.
 * So a drag in full aim moved nothing that is drawn; a saved ADS placement showed only at the next lease.
 *
 * The fix: opened in full aim, the editor begins on its "ads" action (tfp_editor_select_action, which the binder asks
 * for when the player is in full aim: tfp_editor_motion_open_action); and while the ADS action is edited, the motion
 * runtime hands the draft to the pose helper's ADS placement every frame (editor_preview_ads), with the rotation drawn
 * as authored for full aim. On close the motion runtime sends the saved ADS placement again, as it does at any lease.
 *
 * Here the real editor and the real motion runtime; the pose helper is a model of what full aim draws (node 5: the ADS
 * placement, plus the hip placement times the hip weight, which is 0 in full aim). What it cannot show: that the game
 * keeps the aim while the editor's panel has the cursor (the "Aim:" line says, per aim, whether the editor was open). */
#include "editor_motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("editor ads: FAILED %s (line %d)\n", #x, __LINE__); return 1; } } while (0)

/* ---- the editor's world ---- */
static TfpEditor *ed;
static TfpEdPlace live;                                   /* the controller's placement (the hip's) */
static TfpEdPlace stored[3]; static bool has_stored[3];   /* the store, by action: idle, aim, ads */
static char saved_action[16]; static unsigned saves;
static struct { double x, y, wheel; bool click, enter, right; } input;
static double clock_now = 10;
static double revision;

static TfpEdPlace place(double x, double y, double z, double yaw, double pitch, double roll) {
  TfpEdPlace p = {{true, true, true, true, true, true}, {x, y, z, yaw, pitch, roll}};
  return p;
}
static int action_slot(const char *a) { return !strcmp(a, "ads") ? 2 : !strcmp(a, "aim") ? 1 : 0; }
static bool ed_call(void *user, TfpEdCall which, const TfpEdArgs *a, TfpEdReply *r) {
  (void)user;
  switch (which) {
    case TFP_ED_CLOCK: r->a.kind = TFP_ED_NUMBER; r->a.n = clock_now; return true;
    case TFP_ED_C_IS_CLEAN: case TFP_ED_C_IS_ENABLED: case TFP_ED_S_SELECT_MODE: case TFP_ED_S_CAN_START: case TFP_ED_S_ACTION_SUPPORTED:
    case TFP_ED_A_CLOSE: case TFP_ED_I_SET_FOCUS: case TFP_ED_I_RELEASE: case TFP_ED_I_FOCUSED: case TFP_ED_C_START: case TFP_ED_I_ACQUIRE:
      r->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_C_RESULT: r->a.kind = TFP_ED_TABLE; snprintf(r->a.s, sizeof r->a.s, "running"); return true;
    case TFP_ED_C_SET_PLACEMENT: live = tfp_editor_motion_controller_value(ed, &a->place); r->a.kind = TFP_ED_TRUE; return true; /* as src/wire_editor.c */
    case TFP_ED_C_GET_PLACEMENT: case TFP_ED_S_GET: r->a.kind = TFP_ED_TABLE; r->place = live; return true;
    case TFP_ED_S_TARGET: r->a.kind = r->b.kind = TFP_ED_TEXT; snprintf(r->a.s, sizeof r->a.s, "HYENA"); snprintf(r->b.s, sizeof r->b.s, "me"); return true;
    case TFP_ED_S_GET_ACTION: case TFP_ED_S_BASELINE: {
      const int slot = action_slot(a->text);
      if (has_stored[slot]) { r->a.kind = TFP_ED_TABLE; r->place = stored[slot]; }
      else if (slot == 2) { r->a.kind = TFP_ED_TABLE; r->place = place(0, 0, 0, 0, 0, 0); }   /* the runtime's ADS answer: all zeros */
      return true;
    }
    case TFP_ED_S_SAVE: {
      const int slot = action_slot(a->text);
      stored[slot] = a->place; has_stored[slot] = true; saves++; revision++;
      snprintf(saved_action, sizeof saved_action, "%s", a->text);
      r->a.kind = TFP_ED_TRUE; return true;
    }
    case TFP_ED_A_IS_OPEN: r->a.kind = TFP_ED_FALSE; return true;
    case TFP_ED_A_INPUT_STATUS: r->a.kind = TFP_ED_TABLE; return true;
    case TFP_ED_A_RESOLUTION: r->a.kind = r->b.kind = TFP_ED_NUMBER; r->a.n = 1920; r->b.n = 1080; return true;
    case TFP_ED_A_CREATE_VIEW: r->a.kind = TFP_ED_TABLE; r->view.has_draw = r->view.has_release = true; return true;
    case TFP_ED_I_SAMPLE:
      r->a.kind = TFP_ED_TABLE;
      r->sample.click = input.click; r->sample.x = input.x; r->sample.y = input.y; r->sample.wheel = input.wheel; r->sample.dt = 1.0 / 60;
      input.wheel = 0;
      return true;
    case TFP_ED_I_DOWN: r->a.kind = ((int)a->number == 13 && input.enter) || ((int)a->number == 2 && input.right) ? TFP_ED_TRUE : TFP_ED_FALSE; return true;
    default: return true;
  }
}
static void ed_look(void *user, TfpEdLook *l) {
  (void)user;
  l->has_select_mode = l->has_can_start = l->has_target = l->has_action_supported = l->reset_all = true;
  l->nactions = 3; l->action_id[0] = "idle"; l->action_label[0] = "Idle"; l->action_id[1] = "aim"; l->action_label[1] = "Normal aim";
  l->action_id[2] = "ads"; l->action_label[2] = "ADS";
  l->provider = l->provider_input = 1; l->provider_supported = true;
}

/* ---- the motion runtime's world, and a model of what the pose helper draws in full aim ---- */
static TfpMotionRuntimeLease lease = {false, false, false, false, false, false, 0, 0};
static bool full_aim = true;
static TfpActionPlacement ads_sent; static bool has_ads_sent; static unsigned ads_sends;
static TfpMotionRuntimeModifier drawn = {{0, 0, 0, 0, 0, 0, 1}, false};
static double motion_clock = 50;
/* node 5 in full aim, as src/pose_helper.c position_locked: the hip placement times the hip weight (0), plus the ADS
 * placement. The returned forward is what the player sees move. */
static double full_aim_forward(void) { const double hip_weight = full_aim ? 0 : 1; return live.v[1] * hip_weight + (has_ads_sent && full_aim ? ads_sent.y : 0); }
static double full_aim_right(void) { const double hip_weight = full_aim ? 0 : 1; return live.v[0] * hip_weight + (has_ads_sent && full_aim ? ads_sent.x : 0); }

static bool m_clock(void *u, double *s) { (void)u; *s = motion_clock; return true; }
static TfpMotionRuntimeGot m_result(void *u, TfpMotionRuntimeControllerResult *out) { (void)u; out->pose_offset = out->running = true; return TFP_MOTION_GOT_VALUE; }
static TfpMotionRuntimeGot m_enabled(void *u) { (void)u; return TFP_MOTION_GOT_VALUE; }
static void action_of(const TfpEdPlace *p, TfpActionPlacement *out) {
  *out = (TfpActionPlacement){p->v[0], p->v[1], p->v[2], p->v[3], p->v[4], p->v[5], 0};
  for (int i = 0; i < 6; i++) if (p->has[i]) out->present |= 1u << i;
}
static TfpMotionRuntimeGot m_get(void *u, TfpActionPlacement *out) { (void)u; action_of(&live, out); return TFP_MOTION_GOT_VALUE; }
static bool m_set(void *u, const TfpActionPlacement *v, const char **why) { (void)u; (void)why; if (v) live = place(v->x, v->y, v->z, v->yaw, v->pitch, v->roll); return true; }
static bool m_pose(void *u, const TfpMotionRuntimeLease *l, const TfpMotionRuntimeModifier *v, const char **why) { (void)u; (void)l; (void)why; drawn = *v; return true; }
static bool m_ads(void *u, const TfpMotionRuntimeLease *l, const TfpActionPlacement *ads, const char **why) {
  (void)u; (void)l; (void)why;
  has_ads_sent = ads != NULL; if (ads) ads_sent = *ads; ads_sends++;
  return true;
}
static bool m_snapshot(void *u, bool *active, bool *shoulder) { (void)u; *active = full_aim; *shoulder = false; return true; }
static TfpMotionRuntimeGot m_action(void *u, const char *key, TfpAct action, TfpActionPlacement *out) {
  (void)u; (void)key;
  const int slot = action == TFP_ACT_ADS ? 2 : action == TFP_ACT_AIM ? 1 : 0;
  TfpEdPlace standard = slot == 2 ? place(0, 0, 0, 0, 0, 0) : place(.15, 0, -.12, 0, 0, 0);
  action_of(has_stored[slot] ? &stored[slot] : &standard, out);
  return TFP_MOTION_GOT_VALUE;
}
static bool m_sample(void *u, const TfpMotionRuntimeLease *l, TfpLocomotionSample *out, const char **why) {
  (void)u; (void)l; (void)why;
  memset(out, 0, sizeof *out);
  out->player.body = 1; out->ads = full_aim;
  for (int i = 0; i < 16; i++) out->camera_basis[i] = out->parent_basis[i] = i % 5 == 0;
  return true;
}
static bool m_editor_active(void *u, bool *active, const char **problem) { (void)u; (void)problem; return tfp_editor_motion_is_active(ed, active); }
static TfpMotionRuntimeGot m_editor_preview(void *u, TfpActionPlacement *out) { (void)u; return tfp_editor_motion_preview(ed, out); }
/* What src/wire_motion.c hands the runtime: the same editor_motion.h function, with this test's ADS placement. */
static bool m_editor_ads(void *u, const TfpMotionRuntimeLease *l, bool *authored, const char **why) {
  return tfp_editor_motion_preview_ads(ed, m_ads, u, l, authored, why);
}

static TfpMotionRuntimeHost host;
static TfpMotionRuntime runtime;

static int frame(void) {
  clock_now += 1.0 / 60; motion_clock += 1.0 / 60;
  host.action_placement_revision = revision;
  TfpEdResult r = tfp_editor_update(ed);
  if (r.raised) { printf("editor ads: the editor raised %s\n", r.r1.s); return 1; }
  const char *reason = NULL;
  if (tfp_motion_runtime_update(&runtime, &reason) == TFP_MOTION_RUNTIME_RAISED) { printf("editor ads: motion raised\n"); return 1; }
  return 0;
}
static int open_editor(bool controller_active) {
  bool on_ads = true;
  /* as src/wire_editor.c: full aim is the player snapshot's controller (review 28, I2) */
  TfpOpsSnapshot snapshot; memset(&snapshot, 0, sizeof snapshot); snapshot.controller_active = controller_active;
  const bool aiming = tfp_editor_motion_full_aim(TFP_OPS_TRUE, &snapshot);
  if (aiming != controller_active) return 1;
  if (tfp_editor_motion_open(ed, "weapon", aiming, &on_ads).r1.kind != TFP_ED_TRUE || on_ads != aiming) return 1;
  for (int i = 0; i < 4 && strcmp(tfp_editor_phase(ed), "active"); i++) if (frame()) return 1;
  return strcmp(tfp_editor_phase(ed), "active") != 0 || frame();
}
static int drag(double x0, double y0, double x1, double y1) {
  input.x = x0; input.y = y0; input.click = true; if (frame()) return 1;
  for (int i = 1; i <= 4; i++) { input.x = x0 + (x1 - x0) * i / 4; input.y = y0 + (y1 - y0) * i / 4; if (frame()) return 1; }
  input.click = false;
  return frame();
}

int main(void) {
  live = place(.15, 0, -.12, 0, 0, 0);
  host = (TfpMotionRuntimeHost){
    .user = NULL, .clock = m_clock, .controller_lease = &lease, .alignment_lease = &lease, .placement_weapon_key = "HYENA",
    .controller_result = m_result, .controller_is_enabled = m_enabled, .controller_get_placement = m_get, .controller_set_placement = m_set,
    .set_pose_motion = m_pose, .set_ads_placement = m_ads, .player_snapshot = m_snapshot, .action_placement = m_action,
    .editor_is_active = m_editor_active, .editor_preview_rotation = m_editor_preview, .editor_preview_ads = m_editor_ads, .sample = m_sample};
  const char *error = NULL;
  CHECK(tfp_motion_runtime_init(&runtime, &host, NULL, &error));
  TfpEdHost eh = {NULL, ed_call, ed_look, NULL};
  ed = tfp_editor_new(&eh);
  CHECK(ed != NULL);
  for (int i = 0; i < 5; i++) CHECK(frame() == 0);
  CHECK(has_ads_sent && ads_sent.x == 0 && ads_sent.y == 0 && full_aim_forward() == 0);   /* in full aim, the game's own seat */

  /* Which action the binder opens on: ADS in full aim, the editor's own first (idle) otherwise. */
  CHECK(tfp_editor_motion_open_action(true) && !strcmp(tfp_editor_motion_open_action(true), "ads") && tfp_editor_motion_open_action(false) == NULL);
  /* Whether the player is in full aim (src/wire_editor.c open_editor): the snapshot taken and its controller active */
  {
    TfpOpsSnapshot snapshot; memset(&snapshot, 0, sizeof snapshot); snapshot.controller_active = true;
    CHECK(tfp_editor_motion_full_aim(TFP_OPS_TRUE, &snapshot) && !tfp_editor_motion_full_aim(TFP_OPS_FALSE, &snapshot));
    CHECK(!tfp_editor_motion_full_aim(TFP_OPS_TRUE, NULL));
    snapshot.controller_active = false;
    CHECK(!tfp_editor_motion_full_aim(TFP_OPS_TRUE, &snapshot));
  }
  CHECK(!tfp_editor_motion_editing_ads(NULL) && !tfp_editor_motion_editing_ads(ed));   /* round 20c: the near clip's follow */

  /* ---- the report: opened on idle while aiming, a drag moves the hip placement, and full aim draws none of it ---- */
  CHECK(open_editor(false) == 0 && !strcmp(tfp_editor_preview_action(ed), "idle") && !tfp_editor_motion_editing_ads(ed));
  CHECK(drag(900, 700, 1100, 700) == 0);
  CHECK(live.v[0] > .15 + .05);                       /* the editor did move something: the hip placement */
  CHECK(full_aim_right() == 0 && full_aim_forward() == 0);   /* and in full aim nothing moved: "the weapon would not move" */
  CHECK(tfp_editor_cancel(ed, NULL).r1.kind == TFP_ED_TRUE);
  for (int i = 0; i < 3; i++) CHECK(frame() == 0);

  /* ---- opened in full aim: on the ADS action, and the drag and the wheel move what full aim draws, live ---- */
  CHECK(open_editor(true) == 0 && !strcmp(tfp_editor_preview_action(ed), "ads") && tfp_editor_motion_editing_ads(ed));
  const unsigned sends_before = ads_sends;
  input.x = 900; input.y = 700; input.click = true; CHECK(frame() == 0);
  input.x = 1000; CHECK(frame() == 0);
  CHECK(full_aim_right() > .04);                      /* mid-drag, not on release */
  input.x = 1100; CHECK(frame() == 0);
  input.click = false; CHECK(frame() == 0);
  const double right = full_aim_right();
  CHECK(right > .09 && ads_sends > sends_before);
  /* the wheel: depth. Pulled toward the eye by 3 cm (three notches back; forward is away from the eye) */
  for (int i = 0; i < 3; i++) { input.wheel = -120; CHECK(frame() == 0); }
  CHECK(fabs(full_aim_forward() - (-.03)) < 1e-9);
  /* the rotation preview (a right-drag: yaw) is drawn as authored for full aim (the pose helper draws an unauthored one
   * only at the hip) */
  input.x = 900; input.y = 700; input.right = true; CHECK(frame() == 0);
  input.x = 1000; CHECK(frame() == 0);
  CHECK(drawn.authored_ads && drawn.axis[TFP_MOTION_QW] < 1);
  input.right = false; CHECK(frame() == 0);
  CHECK(drawn.authored_ads);
  /* saved for this weapon's ADS action; closed, the motion runtime keeps sending the saved one */
  input.enter = true; CHECK(frame() == 0); input.enter = false;
  CHECK(saves == 1 && !strcmp(saved_action, "ads") && has_stored[2] && fabs(stored[2].v[1] - (-.03)) < 1e-9 && fabs(stored[2].v[0] - right) < 1e-9);
  for (int i = 0; i < 5; i++) CHECK(frame() == 0);
  CHECK(fabs(full_aim_forward() - (-.03)) < 1e-9 && fabs(full_aim_right() - right) < 1e-9 && !tfp_editor_motion_editing_ads(ed));
  CHECK(fabs(live.v[0] - .15) < 1e-9 && fabs(live.v[1]) < 1e-9);   /* the hip placement was never touched by it */

  /* ---- cancel puts the saved ADS placement back, at once ---- */
  CHECK(open_editor(true) == 0 && !strcmp(tfp_editor_preview_action(ed), "ads"));
  for (int i = 0; i < 5; i++) { input.wheel = -120; CHECK(frame() == 0); }
  CHECK(fabs(full_aim_forward() - (-.08)) < 1e-9);
  CHECK(tfp_editor_cancel(ed, NULL).r1.kind == TFP_ED_TRUE);
  for (int i = 0; i < 3; i++) CHECK(frame() == 0);
  CHECK(fabs(full_aim_forward() - (-.03)) < 1e-9 && saves == 1);

  /* ---- opened at the hip, the ADS action chosen with ">": the draft still goes to the ADS placement (drawn the
   * next time the player aims), and the hip view keeps the editor's retail preview ---- */
  full_aim = false;
  for (int i = 0; i < 3; i++) CHECK(frame() == 0);
  CHECK(open_editor(false) == 0 && !strcmp(tfp_editor_preview_action(ed), "idle"));
  CHECK(tfp_editor_select_action(ed, "ads") == false);   /* only before it is active: the ">" button is the way once open */
  CHECK(tfp_editor_cancel(ed, NULL).r1.kind == TFP_ED_TRUE);
  for (int i = 0; i < 3; i++) CHECK(frame() == 0);
  CHECK(tfp_editor_motion_ads_draft(ed, &ads_sent) == TFP_MOTION_GOT_NIL);   /* closed: no draft */

  tfp_editor_free(ed);
  printf("editor ads: %u checks passed (opened in full aim on the ADS action; drag, wheel and save move what full aim draws, live; cancel puts it back)\n", checks);
  return 0;
}
