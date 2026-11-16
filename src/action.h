#ifndef TFP_ACTION_H
#define TFP_ACTION_H
#include "backend.h"
#include "placement.h"
typedef struct FpActionLayer {
  uintptr_t definition;
  uint32_t count;
  uintptr_t entries[2048];
} FpActionLayer;
typedef struct FpActionReader {
  uintptr_t object, instance, graph, active;
  FpActionLayer layer[3];
} FpActionReader;
typedef struct FpActions {
  bool verified, sprint, on_foot;
  int l1, l3, l14;
  FpAction selected;
} FpActions;
bool fp_action_read(FpActionReader *, const FpMemory *, const FpSnapshot *,
                    FpActions *);
#endif
