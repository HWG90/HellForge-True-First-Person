/* The placement editor's keys, with no game (round 17, 2026-11-24).
 *
 * The user: "remove PG UP AND PG DOWN from repositioning for depth". Retail's editor
 * (placement_editor.lua) moved the draft forward and back while Page Up / Page Down were held; in
 * HellForge Page Up is the editor's own open/close key (TFP_EDITOR_KEY, src/mod_menu.h) and Page Down
 * opens the overlay host's panel list, so both clashed. Depth is the wheel's alone now; the arrows still
 * move; Shift still makes either fine.
 *
 * Here the real editor (src/placement_editor.c) is driven through its own update with scripted input
 * while an action other than idle is edited. Checked: Page Up or Page Down held for many frames leaves
 * the depth (and everything else) where it was, and the editor never even asks for them; the wheel
 * still changes depth, Shift still makes it fine; the arrows still move; the help line reads
 * "Wheel: depth   Arrows: move". What it cannot show: which keys the overlay host polls in the game
 * (src/mod_editor.c KEYS). */
#include "placement_editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { printf("editor keys: FAILED %s (line %d)\n", #x, __LINE__); return 1; } } while (0)

static TfpEdPlace live;
static bool held[256];
static double wheel;
static unsigned asked[256];
static char help[512];
static double clock_now = 10;

static TfpEdPlace place(double x, double y, double z) {
  TfpEdPlace p = {{true, true, true, true, true, true}, {x, y, z, 0, 0, 0}};
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
    case TFP_ED_S_GET_ACTION: case TFP_ED_S_BASELINE: return true;
    case TFP_ED_A_IS_OPEN: r->a.kind = TFP_ED_FALSE; return true;
    case TFP_ED_A_INPUT_STATUS: r->a.kind = TFP_ED_TABLE; return true;
    case TFP_ED_A_RESOLUTION: r->a.kind = r->b.kind = TFP_ED_NUMBER; r->a.n = 1920; r->b.n = 1080; return true;
    case TFP_ED_A_CREATE_VIEW: r->a.kind = TFP_ED_TABLE; r->view.has_draw = r->view.has_release = true; return true;
    case TFP_ED_I_ACQUIRE: r->a.kind = TFP_ED_TRUE; return true;
    case TFP_ED_I_SAMPLE:
      r->a.kind = TFP_ED_TABLE;
      r->sample.click = false; r->sample.x = 900; r->sample.y = 600; r->sample.wheel = wheel; r->sample.dt = 1.0 / 60;
      wheel = 0;
      return true;
    case TFP_ED_I_DOWN: {
      int key = (int)a->number;
      if (key > 0 && key < 256) asked[key]++;
      r->a.kind = key > 0 && key < 256 && held[key] ? TFP_ED_TRUE : TFP_ED_FALSE;
      return true;
    }
    case TFP_ED_V_DRAW:
      for (int i = 0; i < a->ncommands; i++)
        if (a->commands[i].is_text && strstr(a->commands[i].text, "Arrows")) snprintf(help, sizeof help, "%s", a->commands[i].text);
      return true;
    case TFP_ED_V_RELEASE: return true;
    default: return true;
  }
}
static void ed_look(void *user, TfpEdLook *l) {
  (void)user;
  /* As src/wire_editor.c host_look answers for the weapon mode, with one action that is not idle. */
  l->has_select_mode = l->has_can_start = l->has_target = l->has_action_supported = l->reset_all = true;
  l->nactions = 1; l->action_id[0] = "sprint"; l->action_label[0] = "Sprint";
  l->provider = l->provider_input = 1; l->provider_supported = true;
}

static TfpEditor *ed;
static int frame(void) {
  clock_now += 1.0 / 60;
  TfpEdResult r = tfp_editor_update(ed);
  if (r.raised) { printf("editor keys: the editor raised %s\n", r.r1.s); return 1; }
  return 0;
}
static bool same(const TfpEdPlace *a, const TfpEdPlace *b) {
  for (int i = 0; i < 6; i++) if (a->has[i] != b->has[i] || a->v[i] != b->v[i]) return false;
  return true;
}

int main(void) {
  live = place(.15, .05, -.12);
  TfpEdHost eh = {NULL, ed_call, ed_look, NULL};
  ed = tfp_editor_new(&eh);
  CHECK(ed != NULL);
  CHECK(tfp_editor_request_open(ed, "weapon").r1.kind == TFP_ED_TRUE);
  for (int i = 0; i < 4 && strcmp(tfp_editor_phase(ed), "active"); i++) CHECK(frame() == 0);
  CHECK(!strcmp(tfp_editor_phase(ed), "active"));
  CHECK(frame() == 0);
  CHECK(!strcmp(tfp_editor_preview_action(ed), "sprint"));
  memset(asked, 0, sizeof asked);
  CHECK(frame() == 0);
  CHECK(asked[37] == 1 && asked[39] == 1 && asked[38] == 1 && asked[40] == 1 && asked[16] == 1);   /* the edit loop ran */

  /* Page Up, Page Down, both, each with and without Shift: the placement stays where it was. */
  const TfpEdPlace start = live;
  static const int PAGE[3][2] = {{33, 0}, {34, 0}, {33, 34}};
  for (int fine = 0; fine < 2; fine++)
    for (int p = 0; p < 3; p++) {
      held[16] = fine; held[PAGE[p][0]] = true; if (PAGE[p][1]) held[PAGE[p][1]] = true;
      for (int i = 0; i < 60; i++) CHECK(frame() == 0);
      held[16] = held[33] = held[34] = false;
      CHECK(same(&live, &start));
    }
  CHECK(asked[33] == 0 && asked[34] == 0);                                         /* not even asked */
  CHECK(!strcmp(tfp_editor_phase(ed), "active"));

  /* The wheel is still depth: one notch forward is 1 cm, with Shift 1 mm. */
  wheel = 120; CHECK(frame() == 0);
  CHECK(fabs(live.v[1] - (start.v[1] + .01)) < 1e-12);
  CHECK(live.v[0] == start.v[0] && live.v[2] == start.v[2]);
  held[16] = true; wheel = -120; CHECK(frame() == 0); held[16] = false;
  CHECK(fabs(live.v[1] - (start.v[1] + .009)) < 1e-12);
  const double depth = live.v[1];

  /* The arrows still move: Left one second is 15 cm left; Up moves up; depth untouched. */
  held[37] = true; for (int i = 0; i < 60; i++) CHECK(frame() == 0); held[37] = false;
  CHECK(fabs(live.v[0] - (start.v[0] - .15)) < 1e-9 && live.v[1] == depth);
  held[38] = true; for (int i = 0; i < 10; i++) CHECK(frame() == 0); held[38] = false;
  CHECK(live.v[2] > start.v[2] + .02 && live.v[1] == depth);

  /* The help line says what the keys do. */
  CHECK(!strcmp(help, "Wheel: depth   Arrows: move"));
  CHECK(strstr(help, "PgUp") == NULL && strstr(help, "PgDn") == NULL);

  CHECK(tfp_editor_cancel(ed, NULL).r1.kind == TFP_ED_TRUE);
  tfp_editor_free(ed);
  printf("editor keys: %u checks passed (Page Up / Page Down leave depth alone; the wheel is depth; the arrows move)\n", checks);
  return 0;
}
