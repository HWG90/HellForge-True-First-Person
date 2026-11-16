#include "action.h"
#include "profile.h"
#include <string.h>
static bool offset(uint32_t n) { return n >= 12 && n <= 0x1000000; }
bool fp_action_read(FpActionReader *reader, const FpMemory *m,
                    const FpSnapshot *s, FpActions *out) {
  if (!reader || !m || !s || !out)
    return false;
  memset(out, 0, sizeof *out);
  out->l1 = out->l3 = out->l14 = -1;
  uintptr_t object = fp_unit_object(m, s->body), vt = 0, getter = 0,
            instance = 0, graph = 0, active = 0;
  uint64_t resource = 0;
  uint32_t header[3];
  if (!object || !fp_pointer(m, object, &vt) ||
      !fp_pointer(m, vt + 0x1b0, &getter) || getter != m->exe + 0x2bd9c0 ||
      !fp_bytes(m, getter, "488b8178010000c3") ||
      !fp_read(m, object + 0x358, &resource, 8) ||
      resource != UINT64_C(0x4d1c334d294dfa97) ||
      !fp_pointer(m, object + 0x178, &instance) ||
      !fp_pointer(m, instance + 0x28, &graph) ||
      !fp_read(m, graph, header, 12) || header[1] != 31 || !offset(header[2]) ||
      header[2] > 0x10000 || !fp_pointer(m, instance + 0x50, &active))
    return false;
  if (reader->object != object || reader->instance != instance ||
      reader->graph != graph || reader->active != active) {
    memset(reader, 0, sizeof *reader);
    reader->object = object;
    reader->instance = instance;
    reader->graph = graph;
    reader->active = active;
  }
  static const unsigned layers[] = {1, 3, 14};
  int states[3];
  uintptr_t observed[3];
  for (unsigned n = 0; n < 3; n++) {
    uint32_t relative = 0, count = 0;
    if (!fp_u32(m, graph + header[2] + 4 + 4 * (layers[n] - 1), &relative) ||
        !offset(relative))
      return false;
    uintptr_t definition = graph + header[2] + relative;
    if (!fp_u32(m, definition + 8, &count) || !count || count > 2048)
      return false;
    FpActionLayer *l = reader->layer + n;
    if (l->definition != definition || l->count != count) {
      uint32_t rows[2048];
      if (!fp_read(m, definition + 12, rows, count * 4))
        return false;
      l->definition = 0;
      for (unsigned i = 0; i < count; i++) {
        if (!offset(rows[i]))
          return false;
        l->entries[i] = definition + rows[i];
      }
      l->count = count;
      l->definition = definition;
    }
    uintptr_t state = 0;
    if (!fp_read(m, active + (layers[n] - 1) * 8, &state, 8))
      return false;
    observed[n] = state;
    states[n] = -1;
    for (unsigned i = 0; state && i < count; i++)
      if (l->entries[i] == state) {
        states[n] = (int)i;
        break;
      }
    if (state && states[n] < 0)
      return false;
  }
  uintptr_t after = 0;
  uint64_t after_resource = 0;
  if (fp_unit_object(m, s->body) != object ||
      !fp_pointer(m, object + 0x178, &after) || after != instance ||
      !fp_read(m, object + 0x358, &after_resource, 8) ||
      after_resource != resource)
    return false;
  uint32_t final_header[3];
  if (!fp_pointer(m, instance + 0x28, &after) || after != graph ||
      !fp_pointer(m, instance + 0x50, &after) || after != active ||
      !fp_read(m, graph, final_header, 12) || memcmp(final_header, header, 12))
    return false;
  for (unsigned n = 0; n < 3; n++) {
    uint32_t relative = 0, count = 0;
    if (!fp_read(m, active + (layers[n] - 1) * 8, &after, 8) ||
        after != observed[n] ||
        !fp_u32(m, graph + header[2] + 4 + 4 * (layers[n] - 1), &relative) ||
        graph + header[2] + relative != reader->layer[n].definition ||
        !fp_u32(m, reader->layer[n].definition + 8, &count) ||
        count != reader->layer[n].count)
      return false;
  }
  out->verified = true;
  out->l1 = states[0];
  out->l3 = states[1];
  out->l14 = states[2];
  out->sprint = out->l1 == 78 || out->l1 == 79 || out->l3 == 71;
  out->on_foot = out->l14 == 36;
  if (out->l14 == 3) {
    uint32_t flags = 0;
    memcpy(&flags, s->flags, 4);
    uintptr_t settings = 0;
    uint32_t scene = 0;
    out->on_foot =
        (flags & 6) == 2 && fp_pointer(m, m->game + 0x3326340, &settings) &&
        fp_u32(m, settings + 0xac21c, &scene) && (scene == 3 || scene == 4);
  }
  FpActionSample sample = {.verified_resource = true,
                           .ads = s->full_ads,
                           .reload = s->reloading,
                           .shoulder_aim = s->shoulder_aim,
                           .sprint = out->sprint};
  memcpy(sample.flags, s->flags, 24);
  out->selected = fp_action_select(&sample);
  return true;
}
