#ifndef TFP_EFFECTS_H
#define TFP_EFFECTS_H
#include "backend.h"
#include "engine.h"
typedef struct FpRenderSetting {
  bool held, pending, restoring, observed;
  uint64_t next_poll;
  unsigned writes, restores;
} FpRenderSetting;
typedef struct FpVignetteRow {
  EngRef entity, id;
  bool held;
} FpVignetteRow;
typedef struct FpEffects {
  FpRenderSetting setting[2];
  EngRef world, manager, entities;
  uint64_t generation, next_scan, next_poll;
  int scan, count;
  FpVignetteRow rows[32];
  unsigned writes, restores;
  char status[160];
} FpEffects;
void fp_effects_init(FpEffects *);
bool fp_effects_update(FpEffects *, const FpMemory *, bool wanted,
                       uint64_t now);
#endif
