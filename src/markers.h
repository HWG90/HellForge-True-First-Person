#ifndef TFP_MARKERS_H
#define TFP_MARKERS_H
#include "backend.h"
typedef bool (*FpWidgetShow)(void *, uintptr_t, bool);
typedef struct FpMarkerLease {
  uintptr_t root, context, world, widgets[2];
  bool held[2];
} FpMarkerLease;
bool fp_markers_update(FpMarkerLease *, const FpMemory *, bool suppress,
                       FpWidgetShow, void *);
bool fp_markers_native(void *, uintptr_t, bool);
#endif
