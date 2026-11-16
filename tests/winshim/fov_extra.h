// Windows stand-ins the retail field-of-view helper needs beyond tests/winshim/windows.h (which is left
// untouched): the 32-bit interlocked compare-exchange it uses on a request's lens field, with an optional
// observer for the differential test. Include after the standard headers and before the retail source.
#pragma once
#include "windows.h"
typedef int32_t LONG;
inline void (*shim_on_cas32)(uint64_t target, uint32_t exchange, uint32_t comparand, uint32_t observed) = nullptr;
inline LONG InterlockedCompareExchange(volatile LONG *target, LONG exchange, LONG comparand) {
  const LONG observed = __sync_val_compare_and_swap(target, comparand, exchange);
  if (shim_on_cas32) shim_on_cas32(reinterpret_cast<uint64_t>(target), static_cast<uint32_t>(exchange),
                                   static_cast<uint32_t>(comparand), static_cast<uint32_t>(observed));
  return observed;
}
