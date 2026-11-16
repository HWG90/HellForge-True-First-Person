#define WIN32_LEAN_AND_MEAN
#include "markers.h"
#include <windows.h>
static __declspec(noinline) void set(uintptr_t target, uintptr_t widget,
                                     bool shown) {
  ((void (*)(uintptr_t, uint8_t))target)(widget, shown ? 1 : 0);
}
bool fp_markers_native(void *memory, uintptr_t widget, bool shown) {
  const FpMemory *m = memory;
  void (*volatile call)(uintptr_t, uintptr_t, bool) = set;
  __try {
    call(m->game + 0x144cfb0, widget, shown);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
