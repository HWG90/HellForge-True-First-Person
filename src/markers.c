#include "markers.h"
#include "feature_contract.h"
#include <string.h>
static bool probe(const FpMemory *m, FpMarkerLease *out) {
  memset(out, 0, sizeof *out);
  uint8_t active = 0;
  if (!fp_feature_contract(m, FP_FEATURE_MARKERS) ||
      !fp_pointer(m, m->game + 0x346d518, &out->root) ||
      !fp_read(m, out->root + 0x24e334, &active, 1) || active != 1)
    return false;
  uintptr_t crosshair = out->root + 0x24e340 + 0x19aaf8, parent = 0;
  if (!fp_pointer(m, crosshair + 0xf0, &parent) ||
      parent != out->root + 0x820 ||
      !fp_pointer(m, crosshair + 0xf8, &out->context) ||
      !fp_read(m, out->context, &active, 1) || active != 1 ||
      !fp_pointer(m, out->context + 16, &out->world))
    return false;
  out->widgets[0] = crosshair + 0x110;
  out->widgets[1] = crosshair + 0x220;
  for (unsigned i = 0; i < 2; i++) {
    uintptr_t p = 0, c = 0;
    uint32_t bits = 0;
    if (!fp_pointer(m, out->widgets[i] + 0xf0, &p) || p != crosshair ||
        !fp_pointer(m, out->widgets[i] + 0xf8, &c) || c != out->context ||
        !fp_u32(m, out->widgets[i], &bits) || !bits)
      return false;
  }
  return true;
}
static bool same(const FpMarkerLease *a, const FpMarkerLease *b) {
  return a->root == b->root && a->context == b->context &&
         a->world == b->world && a->widgets[0] == b->widgets[0] &&
         a->widgets[1] == b->widgets[1];
}
bool fp_markers_update(FpMarkerLease *lease, const FpMemory *m, bool suppress,
                       FpWidgetShow show, void *user) {
  FpMarkerLease live;
  if (!probe(m, &live))
    return !lease->held[0] && !lease->held[1];
  if (lease->root && !same(lease, &live))
    memset(lease, 0, sizeof *lease); /* positively rebuilt HUD */
  if (!lease->root)
    *lease = live;
  for (unsigned i = 0; i < 2; i++) {
    uint32_t bits = 0;
    if (!fp_u32(m, live.widgets[i], &bits))
      return false;
    bool visible = (bits & 16) != 0;
    if (!suppress && lease->held[i]) {
      if (!visible) {
        FpMarkerLease current;
        if (!probe(m, &current) || !same(&live, &current) || !show ||
            !show(user, live.widgets[i], true) ||
            !fp_u32(m, live.widgets[i], &bits) || !(bits & 16))
          return false;
      }
      lease->held[i] = false;
    } else if (suppress && visible) {
      FpMarkerLease current;
      if (!probe(m, &current) || !same(&live, &current))
        return false;
      lease->held[i] = true; /* reserve before native setter */
      if (!show || !show(user, live.widgets[i], false) ||
          !fp_u32(m, live.widgets[i], &bits) || (bits & 16))
        return false;
    }
  }
  if (!suppress && !lease->held[0] && !lease->held[1])
    memset(lease, 0, sizeof *lease);
  return true;
}
