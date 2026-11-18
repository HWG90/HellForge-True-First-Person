/* src/wire_mouse.c: the aim button comes from the engine once its functions are proven, else from Windows. */
#include <stdio.h>
#include <string.h>
#include "wire_mouse.h"

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static struct {
  bool ready, index_fails, button_fails, index_not_number, button_not_number;
  uint64_t base, index_at, id_at, button_at, generation;
  double right_index, level; int windows; unsigned index_calls, button_calls, windows_calls; double asked;
} g;
static bool ready(void *u) { (void)u; return g.ready; }
static uint64_t function_at(void *u, const char *path) {
  (void)u;
  if (!strcmp(path, "Mouse.button_index")) return g.index_at;
  if (!strcmp(path, "Mouse.button_id")) return g.id_at;
  if (!strcmp(path, "Mouse.button")) return g.button_at;
  return 0;
}
static uint64_t base(void *u) { (void)u; return g.base; }
static bool index_of(void *u, const char *path, const char *name, bool *has, double *index) {
  (void)u; (void)path; g.index_calls++;
  if (g.index_fails) return false;
  *has = !g.index_not_number; *index = !strcmp(name, "right") ? g.right_index : g.right_index + 1;
  return true;
}
static bool button(void *u, double index, bool *has, double *level) {
  (void)u; g.button_calls++; g.asked = index;
  if (g.button_fails) return false;
  *has = !g.button_not_number; *level = g.level;
  return true;
}
static uint64_t generation(void *u) { (void)u; return g.generation; }
static int windows(void *u, int b) { (void)u; (void)b; g.windows_calls++; return g.windows; }

int main(void) {
  const TfpWireMouseHost host = {NULL, ready, function_at, base, index_of, button, generation, windows};
  TfpWireMouse m;
  memset(&g, 0, sizeof g);
  g.base = 0x140000000, g.index_at = g.id_at = g.base + TFP_MOUSE_BUTTON_INDEX_RVA, g.button_at = g.base + TFP_MOUSE_BUTTON_RVA;
  g.right_index = 1, g.generation = 7, g.windows = 1, g.level = 0;

  /* the engine is not up yet: Windows answers, and says why */
  tfp_wire_mouse_init(&m, &host);
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_WINDOWS && g.button_calls == 0 && m.note_fresh && strstr(m.note, "not ready"));
  m.note_fresh = false;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && !m.note_fresh); /* the same reason is not said twice */
  CHECK(tfp_wire_mouse_level(&m, 3) == -1 && tfp_wire_mouse_level(&m, 0) == -1); /* only right and middle */

  /* ready and proven: the engine's level, Windows not asked; the index is found once */
  g.ready = true; g.windows_calls = 0;
  CHECK(tfp_wire_mouse_level(&m, 1) == 0 && m.source == TFP_MOUSE_ENGINE && g.windows_calls == 0 && g.asked == 1 && m.note[0] == 0 && m.note_fresh);
  m.note_fresh = false;
  g.level = 1;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && g.index_calls == 1 && !m.note_fresh);
  CHECK(tfp_wire_mouse_level(&m, 2) == 1 && g.asked == 2 && g.index_calls == 2); /* middle has its own index */
  g.level = 0.5; CHECK(tfp_wire_mouse_level(&m, 1) == 1); /* retail: value > 0 */
  g.level = 0;   CHECK(tfp_wire_mouse_level(&m, 1) == 0);
  CHECK(!strcmp(tfp_wire_mouse_source_name(&m), "engine"));

  /* a failed call: Windows for that answer, the engine again afterwards */
  g.button_fails = true; g.windows = 1;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_WINDOWS && m.engine_failures == 1 && m.note_fresh);
  g.button_fails = false; m.note_fresh = false;
  CHECK(tfp_wire_mouse_level(&m, 1) == 0 && m.source == TFP_MOUSE_ENGINE);
  g.button_not_number = true;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_WINDOWS);
  g.button_not_number = false;

  /* a new Lua state: proven again, the indices looked up again */
  unsigned before = g.index_calls;
  g.generation = 8;
  CHECK(tfp_wire_mouse_level(&m, 1) == 0 && m.source == TFP_MOUSE_ENGINE && g.index_calls == before + 1);

  /* another function registered under the name: never called, Windows for the session */
  tfp_wire_mouse_init(&m, &host);
  g.button_at = g.base + 0x482490; g.button_calls = 0; g.windows = 0;
  CHECK(tfp_wire_mouse_level(&m, 1) == 0 && m.source == TFP_MOUSE_WINDOWS && g.button_calls == 0 && m.refused && strstr(m.note, "Mouse.button"));
  g.button_at = g.base + TFP_MOUSE_BUTTON_RVA;
  CHECK(tfp_wire_mouse_level(&m, 1) == 0 && m.source == TFP_MOUSE_WINDOWS && g.button_calls == 0); /* stays refused */
  CHECK(!strcmp(tfp_wire_mouse_source_name(&m), "windows"));

  /* button_index missing: button_id is used, as retail's `mouse.button_index or mouse.button_id` */
  tfp_wire_mouse_init(&m, &host);
  g.index_at = 0; g.index_calls = 0;
  CHECK(tfp_wire_mouse_level(&m, 1) == 0 && m.source == TFP_MOUSE_ENGINE && g.index_calls == 1);
  g.index_at = g.id_at;

  /* the index is not a number: Windows, and it is asked again next state, not every frame */
  tfp_wire_mouse_init(&m, &host);
  g.index_not_number = true; g.index_calls = 0; g.windows = 1;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_WINDOWS);
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && g.index_calls == 1);
  g.index_not_number = false;

  /* no engine functions at all (the view build): Windows, silently usable */
  TfpWireMouseHost bare = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, windows};
  tfp_wire_mouse_init(&m, &bare);
  g.windows = 1;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_WINDOWS);

  /* build 25480438: Windows first (the engine's level arrives after the game has acted); the engine only
   * when Windows gives no answer */
  tfp_wire_mouse_init(&m, &host);
  m.windows_first = true;
  g.ready = true, g.index_not_number = g.button_fails = false, g.level = 0, g.windows = 1, g.button_calls = 0;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_WINDOWS && g.button_calls == 0 && strstr(m.note, "first"));
  g.windows = 0;
  CHECK(tfp_wire_mouse_level(&m, 2) == 0 && m.source == TFP_MOUSE_WINDOWS && g.button_calls == 0);
  g.windows = -1, g.level = 1;
  CHECK(tfp_wire_mouse_level(&m, 1) == 1 && m.source == TFP_MOUSE_ENGINE && g.button_calls == 1);
  g.button_fails = true;
  CHECK(tfp_wire_mouse_level(&m, 1) == -1); /* neither answers */
  g.button_fails = false;

  if (failures) return 1;
  printf("wire mouse: %d checks passed (engine once proven, Windows otherwise, never an unproven function)\n", checks);
  return 0;
}
