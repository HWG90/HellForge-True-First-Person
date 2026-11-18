/* See wire_mouse.h. */
#include "wire_mouse.h"
#include <stdio.h>
#include <string.h>

void tfp_wire_mouse_init(TfpWireMouse *m, const TfpWireMouseHost *host) {
  memset(m, 0, sizeof *m);
  m->host = *host;
}
static void note(TfpWireMouse *m, const char *text) {
  if (!strncmp(m->note, text, sizeof m->note - 1)) return;
  snprintf(m->note, sizeof m->note, "%s", text);
  m->note_fresh = true;
}
static int from_windows(TfpWireMouse *m, int button, const char *why) {
  note(m, why);
  m->source = TFP_MOUSE_WINDOWS;
  return m->host.windows_button ? m->host.windows_button(m->host.user, button) : -1;
}
/* The functions registered under the two names are this build's own. */
static bool prove(TfpWireMouse *m, const char **index_path) {
  const TfpWireMouseHost *h = &m->host;
  const uint64_t base = h->exe_base(h->user);
  const uint64_t by_index = h->engine_function(h->user, "Mouse.button_index"), by_id = h->engine_function(h->user, "Mouse.button_id");
  const uint64_t button = h->engine_function(h->user, "Mouse.button");
  char text[160];
  if (!base) { note(m, "aim button from Windows: the game's executable is not found"); return false; }
  if (button != base + TFP_MOUSE_BUTTON_RVA) {
    snprintf(text, sizeof text, "aim button from Windows: Mouse.button is registered at exe+0x%llx, not at exe+0x%x",
             (unsigned long long)(button ? button - base : 0), TFP_MOUSE_BUTTON_RVA);
    m->refused = true; note(m, text); return false;
  }
  if (by_index == base + TFP_MOUSE_BUTTON_INDEX_RVA) *index_path = "Mouse.button_index";
  else if (!by_index && by_id == base + TFP_MOUSE_BUTTON_INDEX_RVA) *index_path = "Mouse.button_id";
  else {
    snprintf(text, sizeof text, "aim button from Windows: Mouse.button_index is registered at exe+0x%llx, not at exe+0x%x",
             (unsigned long long)(by_index ? by_index - base : by_id ? by_id - base : 0), TFP_MOUSE_BUTTON_INDEX_RVA);
    m->refused = true; note(m, text); return false;
  }
  return true;
}
int tfp_wire_mouse_level(TfpWireMouse *m, int button) {
  const TfpWireMouseHost *h = &m->host;
  if (button != 1 && button != 2) return -1;
  if (!h->engine_ready || !h->engine_function || !h->exe_base || !h->engine_index || !h->engine_button || !h->engine_generation)
    return from_windows(m, button, "aim button from Windows: this build has no engine caller");
  if (m->refused) { m->source = TFP_MOUSE_WINDOWS; return h->windows_button ? h->windows_button(h->user, button) : -1; }
  if (!h->engine_ready(h->user)) return from_windows(m, button, "aim button from Windows: the engine's script state is not ready yet");
  const uint64_t generation = h->engine_generation(h->user);
  if (!m->proven || generation != m->generation) {
    m->proven = false, m->has_index[1] = m->has_index[2] = m->tried[1] = m->tried[2] = false, m->index_path = NULL;
    if (!prove(m, &m->index_path)) { m->source = TFP_MOUSE_WINDOWS; return h->windows_button ? h->windows_button(h->user, button) : -1; }
    m->proven = true, m->generation = generation;
  }
  if (!m->tried[button]) {
    /* Once per button and Lua state: a lookup that fails or answers no number is not repeated every frame. */
    bool has = false; double index = 0;
    m->tried[button] = true;
    if (h->engine_index(h->user, m->index_path, button == 1 ? "right" : "middle", &has, &index) && has) m->has_index[button] = true, m->index[button] = index;
  }
  if (!m->has_index[button]) return from_windows(m, button, "aim button from Windows: the engine has no index for the button");
  bool has = false; double level = 0;
  if (!h->engine_button(h->user, m->index[button], &has, &level) || !has) {
    m->engine_failures++;
    return from_windows(m, button, "aim button from Windows: Mouse.button failed or did not answer a number");
  }
  note(m, "");
  m->source = TFP_MOUSE_ENGINE;
  return level > 0 ? 1 : 0;
}
const char *tfp_wire_mouse_source_name(const TfpWireMouse *m) {
  return m->source == TFP_MOUSE_ENGINE ? "engine" : m->source == TFP_MOUSE_WINDOWS ? "windows" : "unknown";
}
