#ifndef TFP_DEPTH_H
#define TFP_DEPTH_H
#include "backend.h"
typedef struct FpDepthProbe {
  uintptr_t buckets;
  uint32_t cap, start, count;
  uint64_t rows[64];
} FpDepthProbe;
typedef struct FpDepthData {
  uintptr_t pool, world, settings, data, resource_table, resource_row,
      parameters, packed;
  uint8_t pool_bytes[0x88], data_bytes[0xdc], parameter_bytes[16],
      resource_bytes[16];
  FpDepthProbe primary, overrides;
  float degrees, zoom[3], coefficient[3], near_term[2];
} FpDepthData;
typedef struct FpDepthObservation {
  uintptr_t stack, request;
  uint64_t token;
  uint32_t thread;
  bool ready;
  uint8_t frame[0x20], state[0x24];
} FpDepthObservation;
typedef struct FpDepth {
  bool checked, code_ok, signature_set, valid, previous_known;
  uint32_t signature[4];
  uint64_t next_check, retry_at;
  float current, previous, current_reference, previous_reference;
  FpDepthData data;
  FpDepthObservation observation, pending;
  float delta;
  bool carrier_ready;
  uint64_t applied, refused;
} FpDepth;
void fp_depth_observe(FpDepth *, const FpMemory *, uintptr_t request,
                      uintptr_t stack, uint64_t token, uint32_t thread);
float fp_depth_delta(FpDepth *, const FpMemory *, const FpSnapshot *,
                     uintptr_t stack, uint64_t token, uint32_t thread,
                     uint64_t now);
bool fp_depth_state_same(const FpMemory *, const FpDepthObservation *);
bool fp_depth_reference(const FpDepthData *, uint32_t selected, float distance,
                        float *reference);
#endif
