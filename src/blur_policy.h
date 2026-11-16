#ifndef TFP_BLUR_POLICY_H
#define TFP_BLUR_POLICY_H
#include "camera_policy.h"
typedef struct FpBlurSource {
  uint32_t index, entity, row, count, hashes[512];
  uintptr_t blob, fields[3];
  float native[3];
  bool owned[3];
} FpBlurSource;
typedef struct FpBlurPolicy {
  SRWLOCK lock;
  FpMemory memory, backing;
  bool read_failed;
  FpSnapshot origin;
  FpSlotPlatform platform;
  FpDispatchLease lease;
  uint64_t token;
  uintptr_t provider, system, table, param_pool, param_descriptor, param_row,
      ids, classes, chains, blobs;
  uint8_t param_bytes[24];
  uint32_t indices[2], count;
  FpBlurSource sources[2];
  uint64_t changes;
} FpBlurPolicy;
void fp_blur_policy_init(FpBlurPolicy *);
/* No source means this weapon has no native scoped blur to suppress. */
bool fp_blur_policy_open(FpBlurPolicy *, const FpMemory *, const FpSnapshot *,
                         bool *no_source, char *, size_t);
bool fp_blur_policy_close(FpBlurPolicy *);
#endif
