#ifndef TFP_PREFERENCES_H
#define TFP_PREFERENCES_H
#include "backend.h"
typedef bool (*FpByteWrite)(void *, uintptr_t, uint8_t, uint8_t);
typedef struct FpByteOwner {
  FpSnapshot origin;
  uintptr_t at, pool, descriptor, rows;
  uint8_t bytes[20];
  uint32_t weapon, entity, mode, index;
  bool held, draining;
} FpByteOwner;
/* Native aim-mode byte: the game still owns button input and ADS transitions.
 */
bool fp_aim_preference(FpByteOwner *, const FpMemory *, bool, FpByteWrite,
                       void *);
bool fp_movement_preference(FpByteOwner *, const FpMemory *, bool, FpByteWrite,
                            void *);
bool fp_private_byte(void *, uintptr_t, uint8_t, uint8_t);
#endif
