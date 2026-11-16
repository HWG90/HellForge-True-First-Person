#ifndef TFP_LIFECYCLE_H
#define TFP_LIFECYCLE_H
#include "backend.h"
typedef enum FpNativeOperation {
  FP_NATIVE_ALLOWED,
  FP_NATIVE_ENTER,
  FP_NATIVE_EXIT
} FpNativeOperation;
typedef bool (*FpNativeInvoke)(void *, FpNativeOperation, const FpMemory *,
                               const FpSnapshot *, bool *);
typedef struct FpNativeLease {
  FpSnapshot origin;
  uintptr_t request;
  bool held, called, aim_changed, control_changed;
} FpNativeLease;
bool fp_native_entry_ready(const FpMemory *);
bool fp_native_begin(const FpMemory *, const FpSnapshot *, FpNativeLease *,
                     FpNativeInvoke, void *, char *, size_t);
bool fp_native_release(const FpMemory *, FpNativeLease *, FpNativeInvoke,
                       void *, char *, size_t);
bool fp_native_platform(void *, FpNativeOperation, const FpMemory *,
                        const FpSnapshot *, bool *);
#endif
