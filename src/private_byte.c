#define WIN32_LEAN_AND_MEAN
#include "preferences.h"
#include <windows.h>
static __declspec(noinline) bool exchange(uintptr_t at, uint8_t expected,
                                          uint8_t value) {
  return __atomic_compare_exchange_n((uint8_t *)at, &expected, value, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
bool fp_private_byte(void *unused, uintptr_t at, uint8_t expected,
                     uint8_t value) {
  (void)unused;
  MEMORY_BASIC_INFORMATION region;
  if (at < 65536 ||
      VirtualQuery((void *)at, &region, sizeof region) != sizeof region ||
      region.State != MEM_COMMIT || region.Type != MEM_PRIVATE ||
      (region.Protect != PAGE_READWRITE && region.Protect != PAGE_WRITECOPY))
    return false;
  bool (*volatile call)(uintptr_t, uint8_t, uint8_t) = exchange;
  __try {
    return call(at, expected, value);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
