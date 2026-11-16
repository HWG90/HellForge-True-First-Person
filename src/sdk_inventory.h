#ifndef TFP_SDK_INVENTORY_H
#define TFP_SDK_INVENTORY_H
#include "backend.h"
#include "hellforge_mod.h"
#include <stdbool.h>
/* Optional development census; names/types/C binding addresses only. */
void fp_sdk_inventory(const HfApi *, HfMod *, const FpMemory *);
bool fp_sdk_inventory_pending(void);
void fp_sdk_observer(const HfApi *, HfMod *, const FpMemory *, bool enabled,
                     uint64_t now);
#endif
