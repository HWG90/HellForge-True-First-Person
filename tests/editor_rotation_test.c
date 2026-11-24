/* The placement editor's rotation, from the mouse to what is drawn, with no game (round 16, 2026-11-24).
 *
 * The user, on 0.6.0-feel.50: moving the weapon in the placement editor works, rotating it does not
 * (right-drag yaw/pitch, middle-drag roll). The editor (src/placement_editor.c) changed its draft's yaw,
 * pitch and roll and handed them to the controller, but the controller and the pose helper take x, y, z
 * only: in retail the rotation is drawn by the motion runtime, which asks the editor for
 * editor:preview_rotation() while editor:is_active() (motion_runtime.lua:281-296) and, in play, adds the
 * placement's yaw, pitch and roll to its own (:384-388). The feel build gave the motion runtime no
 * editor (src/wire_motion.c: editor_is_active and editor_preview_rotation NULL), so a drag reached
 * nothing that is drawn.
 *
 * Here the real editor (placement_editor.c) is driven through its own update with scripted input; the
 * real motion runtime (motion_runtime.c) is handed the editor through src/editor_motion.h, the same two
 * functions src/wire_motion.c gives it; what is "drawn" is the modifier the runtime hands the pose
 * helper (set_pose_motion: the rotation written on the rig node that carries the arms and the gun,
 * src/arm_graft.h tfp_graft_view_node). Checked: each axis's drag changes the drawn rotation while
 * dragging; the save carries yaw, pitch and roll; the saved rotation is drawn in play with the editor
 * closed. What it cannot show: that the game draws the helper's rotation (docs/guides/POSER-AND-MENU.md). */
#include "editor_motion.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("editor rotation: FAILED %s (line %d)\n", #x, __LINE__); return 1; } } while (0)

/* ---- the editor's world: controller, store, overlay input ---- */
static TfpEdPlace live, saved;             /* the controller's placement; the store's */
static bool has_saved, saved_all;
static unsigned saves;
static struct { double x, y; bool click, right, middle, enter; } input;
static double clock_now = 10;
static bool cursor_used;

static TfpEdPlace place(double x, double y, double z, double yaw, double pitch, double roll) {
  TfpEdPlace p = {{true, true, true, true, true, true}, {x, y, z, yaw, pitch, roll}};
  return p;
}
static bool ed_call(void *user, TfpEdCall which, const TfpEdArgs *a, TfpEdReply *r) {
  (void)user;
  switch (which) {
    case TFP_ED_CLOCK: r->a.kind = TFP_ED_NUMBER; r->a.n = clock_now; return true;
    case TFP_ED_C_IS_CLEAN: case TFP_ED_C_IS_ENABLED: case TFP_ED_S_SELECT_MODE: case TFP_ED_S_CAN_START: case TFP_ED_S_ACTION_SUPPORTED:
    case TFP_ED_A_CLOSE: case TFP_ED_I_SET_FOCUS: case TFP_ED_I_RELEASE: case TFP_ED_I_FOCUSED: case TFP_ED_C_START:
      r->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_C_RESULT: r->a.kind = TFP_ED_TABLE; snprintf(r->a.s, sizeof r->a.s, "running"); return true;
    case TFP_ED_C_SET_PLACEMENT: live = a->place; r->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_C_GET_PLACEMENT: case TFP_ED_S_GET: r->a.kind = TFP_ED_TABLE; r->place = live; return true;
    case TFP_ED_S_TARGET: r->a.kind = r->b.kind = TFP_ED_TEXT; snprintf(r->a.s, sizeof r->a.s, "TALON"); snprintf(r->b.s, sizeof r->b.s, "me"); return true;
    case TFP_ED_S_GET_ACTION: case TFP_ED_S_BASELINE:
      if (has_saved) { r->a.kind = TFP_ED_TABLE; r->place = saved; }
      return true;
    case TFP_ED_S_SAVE: saved = a->place; has_saved = true; saved_all = a->flag; saves++; r->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_A_IS_OPEN: r->a.kind = TFP_ED_FALSE; return true;
    case TFP_ED_A_INPUT_STATUS: r->a.kind = TFP_ED_TABLE; return true;
    case TFP_ED_A_RESOLUTION: r->a.kind = r->b.kind = TFP_ED_NUMBER; r->a.n = 1920; r->b.n = 1080; return true;
    case TFP_ED_A_CREATE_VIEW: r->a.kind = TFP_ED_TABLE; r->view.has_draw = r->view.has_release = true; return true;
    case TFP_ED_I_ACQUIRE: cursor_used = true; r->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_I_SAMPLE:
      r->a.kind = TFP_ED_TABLE;
      r->sample.click = input.click; r->sample.x = input.x; r->sample.y = input.y; r->sample.wheel = 0; r->sample.dt = 1.0 / 60;
      return true;
    case TFP_ED_I_DOWN: {
      int key = (int)a->number;
      bool down = key == 2 ? input.right : key == 4 ? input.middle : key == 13 ? input.enter : false;
      r->a.kind = down ? TFP_ED_TRUE : TFP_ED_FALSE;
      return true;
    }
    case TFP_ED_V_DRAW: case TFP_ED_V_RELEASE: return true;
    default: return true;
  }
}
static void ed_look(void *user, TfpEdLook *l) {
  (void)user;
  /* As src/wire_editor.c host_look answers for the weapon mode: rotation is not false, one action. */
  l->has_select_mode = l->has_can_start = l->has_target = l->has_action_supported = l->reset_all = true;
  l->nactions = 1; l->action_id[0] = "idle"; l->action_label[0] = "Idle";
  l->provider = l->provider_input = 1; l->provider_supported = true;
}

/* ---- the motion runtime's world ---- */
static TfpMotionRuntimeLease lease = {false, false, false, false, false, false, 0, 0};
static const TfpEditor *editor;
static TfpMotionRuntimeModifier drawn = {{0, 0, 0, 0, 0, 0, 1}, false};
static unsigned draws;
static double motion_clock = 50;

static bool m_clock(void *u, double *s) { (void)u; *s = motion_clock; return true; }
static TfpMotionRuntimeGot m_result(void *u, TfpMotionRuntimeControllerResult *out) { (void)u; out->pose_offset = out->running = true; return TFP_MOTION_GOT_VALUE; }
static TfpMotionRuntimeGot m_enabled(void *u) { (void)u; return TFP_MOTION_GOT_VALUE; }
static void action_of(const TfpEdPlace *p, TfpActionPlacement *out) {
  *out = (TfpActionPlacement){p->v[0], p->v[1], p->v[2], p->v[3], p->v[4], p->v[5], 0};
  for (int i = 0; i < 6; i++) if (p->has[i]) out->present |= 1u << i;
}
static TfpMotionRuntimeGot m_get(void *u, TfpActionPlacement *out) { (void)u; action_of(&live, out); return TFP_MOTION_GOT_VALUE; }
static bool m_set(void *u, const TfpActionPlacement *v, const char **why) {
  (void)u; (void)why;
  if (v) live = place(v->x, v->y, v->z, v->yaw, v->pitch, v->roll);
  return true;
}
static bool m_pose(void *u, const TfpMotionRuntimeLease *l, const TfpMotionRuntimeModifier *v, const char **why) {
  (void)u; (void)l; (void)why;
  drawn = *v; draws++;
  return true;
}
static bool m_ads(void *u, const TfpMotionRuntimeLease *l, const TfpActionPlacement *ads, const char **why) { (void)u; (void)l; (void)ads; (void)why; return true; }
static bool m_snapshot(void *u, bool *active, bool *shoulder) { (void)u; *active = *shoulder = false; return true; }
static TfpMotionRuntimeGot m_action(void *u, const char *key, TfpAct action, TfpActionPlacement *out) {
  (void)u; (void)key; (void)action;
  TfpEdPlace standard = place(.15, 0, -.12, 0, 0, 0);   /* the store answers the default before a save */
  action_of(has_saved ? &saved : &standard, out);
  return TFP_MOTION_GOT_VALUE;
}
static bool m_sample(void *u, const TfpMotionRuntimeLease *l, TfpLocomotionSample *out, const char **why) {
  (void)u; (void)l; (void)why;
  memset(out, 0, sizeof *out);
  out->player.body = 1;
  for (int i = 0; i < 16; i++) out->camera_basis[i] = out->parent_basis[i] = i % 5 == 0;   /* the identity: parent = camera */
  return true;
}
/* What src/wire_motion.c hands the runtime as the editor (editor_motion.h). */
static bool m_editor_active(void *u, bool *active, const char **problem) { (void)u; (void)problem; return tfp_editor_motion_is_active(editor, active); }
static TfpMotionRuntimeGot m_editor_preview(void *u, TfpActionPlacement *out) { (void)u; return tfp_editor_motion_preview(editor, out); }

static TfpMotionRuntimeHost host;
static TfpMotionRuntime runtime;
static TfpEditor *ed;

/* One frame in retail's order (runtime.lua:477-481): the editor, then motion. */
static int frame(void) {
  clock_now += 1.0 / 60; motion_clock += 1.0 / 60;
  TfpEdResult r = tfp_editor_update(ed);
  if (r.raised) { printf("editor rotation: the editor raised %s\n", r.r1.s); return 1; }
  const char *reason = NULL;
  if (tfp_motion_runtime_update(&runtime, &reason) == TFP_MOTION_RUNTIME_RAISED) { printf("editor rotation: motion raised\n"); return 1; }
  return 0;
}
static double turn_between(const TfpMotionRuntimeModifier *a, const TfpMotionRuntimeModifier *b) {
  double d = 0;
  for (int i = TFP_MOTION_QX; i <= TFP_MOTION_QW; i++) d += a->axis[i] * b->axis[i];
  d = fmin(1, fabs(d));
  return 2 * acos(d) * 57.29577951308232;
}
/* A drag: press at (x0, y0), move to (x1, y1) over a few frames, release. */
static int drag(bool right, bool middle, double x0, double y0, double x1, double y1) {
  input.x = x0; input.y = y0; input.right = right; input.middle = middle;
  if (frame()) return 1;
  for (int i = 1; i <= 4; i++) { input.x = x0 + (x1 - x0) * i / 4; input.y = y0 + (y1 - y0) * i / 4; if (frame()) return 1; }
  input.right = input.middle = false;
  return frame();
}

int main(void) {
  live = place(.15, 0, -.12, 0, 0, 0);
  host = (TfpMotionRuntimeHost){
    .user = NULL, .clock = m_clock, .controller_lease = &lease, .alignment_lease = &lease, .placement_weapon_key = "TALON",
    .controller_result = m_result, .controller_is_enabled = m_enabled, .controller_get_placement = m_get, .controller_set_placement = m_set,
    .set_pose_motion = m_pose, .set_ads_placement = m_ads, .player_snapshot = m_snapshot, .action_placement = m_action,
    .editor_is_active = m_editor_active, .editor_preview_rotation = m_editor_preview, .sample = m_sample};
  const char *error = NULL;
  CHECK(tfp_motion_runtime_init(&runtime, &host, NULL, &error));
  TfpEdHost eh = {NULL, ed_call, ed_look, NULL};
  ed = tfp_editor_new(&eh);
  editor = ed;
  CHECK(ed != NULL);

  /* Closed: nothing to preview, and the runtime is told so. */
  bool active = true;
  TfpActionPlacement preview;
  CHECK(tfp_editor_motion_is_active(NULL, &active) && !active);                    /* no editor: an idle one */
  CHECK(tfp_editor_motion_preview(NULL, &preview) == TFP_MOTION_GOT_NIL);
  CHECK(tfp_editor_motion_is_active(ed, &active) && !active && tfp_editor_motion_preview(ed, &preview) == TFP_MOTION_GOT_NIL);

  /* Open it: queued, handoff, active; then a frame with nothing held makes it ready. */
  CHECK(tfp_editor_request_open(ed, "weapon").r1.kind == TFP_ED_TRUE);
  for (int i = 0; i < 4 && strcmp(tfp_editor_phase(ed), "active"); i++) CHECK(frame() == 0);
  CHECK(!strcmp(tfp_editor_phase(ed), "active") && cursor_used);
  CHECK(tfp_editor_motion_is_active(ed, &active) && active);
  CHECK(frame() == 0);
  TfpMotionRuntimeModifier before = drawn;

  /* Moving works as it did: a left drag moves the live placement. */
  input.x = 900; input.y = 700; input.click = true; CHECK(frame() == 0);
  input.x = 1000; CHECK(frame() == 0);
  input.click = false; CHECK(frame() == 0);
  CHECK(live.v[0] > .15 + .04);

  /* Yaw: right-drag sideways. The draft, the preview and the drawn rotation all change while dragging. */
  input.x = 800; input.y = 600; input.right = true; CHECK(frame() == 0);
  input.x = 900; CHECK(frame() == 0);
  CHECK(tfp_editor_motion_preview(ed, &preview) == TFP_MOTION_GOT_VALUE && preview.yaw < -5 && preview.pitch == 0 && preview.roll == 0);
  CHECK(live.v[3] == preview.yaw);                                                /* handed to the controller too, as retail */
  CHECK(turn_between(&drawn, &before) > 5);                                       /* drawn mid-drag, not on release */
  input.right = false; CHECK(frame() == 0);
  TfpMotionRuntimeModifier after_yaw = drawn;

  /* Pitch: right-drag up and down. */
  CHECK(drag(true, false, 800, 500, 800, 650) == 0);
  CHECK(tfp_editor_motion_preview(ed, &preview) == TFP_MOTION_GOT_VALUE && fabs(preview.pitch) > 5);
  CHECK(turn_between(&drawn, &after_yaw) > 5);
  TfpMotionRuntimeModifier after_pitch = drawn;

  /* Roll: middle-drag sideways. */
  CHECK(drag(false, true, 800, 600, 950, 600) == 0);
  CHECK(tfp_editor_motion_preview(ed, &preview) == TFP_MOTION_GOT_VALUE && fabs(preview.roll) > 5);
  CHECK(turn_between(&drawn, &after_pitch) > 5);
  double yaw = preview.yaw, pitch = preview.pitch, roll = preview.roll;
  TfpMotionRuntimeModifier edited = drawn;

  /* Saved with the placement: Enter saves this weapon's, yaw, pitch and roll included. */
  input.enter = true; CHECK(frame() == 0);
  input.enter = false;
  CHECK(saves == 1 && !saved_all && saved.has[3] && saved.has[4] && saved.has[5]);
  CHECK(saved.v[3] == yaw && saved.v[4] == pitch && saved.v[5] == roll);
  CHECK(tfp_editor_motion_is_active(ed, &active) && !active);

  /* In play, the editor closed: the saved rotation is still what is drawn (the motion's play path adds the
   * placement's yaw, pitch and roll to its own; standing still it has none of its own). */
  for (int i = 0; i < 30; i++) CHECK(frame() == 0);
  CHECK(turn_between(&drawn, &before) > 5);
  CHECK(turn_between(&drawn, &edited) < .5);

  /* Opened again: the saved rotation is the draft, and a fresh drag turns it from there. */
  CHECK(tfp_editor_request_open(ed, "weapon").r1.kind == TFP_ED_TRUE);
  for (int i = 0; i < 4 && strcmp(tfp_editor_phase(ed), "active"); i++) CHECK(frame() == 0);
  CHECK(frame() == 0);
  CHECK(tfp_editor_motion_preview(ed, &preview) == TFP_MOTION_GOT_VALUE && preview.yaw == yaw && preview.roll == roll);
  CHECK(turn_between(&drawn, &edited) < .5);
  CHECK(drag(true, false, 800, 600, 700, 600) == 0);
  CHECK(tfp_editor_motion_preview(ed, &preview) == TFP_MOTION_GOT_VALUE && preview.yaw > yaw + 5);
  CHECK(turn_between(&drawn, &edited) > 5);
  CHECK(tfp_editor_cancel(ed, NULL).r1.kind == TFP_ED_TRUE);
  CHECK(live.v[3] == yaw);                                                        /* cancel puts the saved placement back */

  tfp_editor_free(ed);
  printf("editor rotation: %u checks passed (yaw, pitch and roll drags drawn while dragging, saved with the placement, drawn in play)\n", checks);
  return 0;
}
