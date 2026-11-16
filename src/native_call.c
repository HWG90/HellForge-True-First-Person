#define WIN32_LEAN_AND_MEAN
#include "lifecycle.h"
#include "profile.h"
#include <windows.h>
static bool executable(uintptr_t address, uintptr_t image) {
  MEMORY_BASIC_INFORMATION region;
  if (VirtualQuery((void *)address, &region, sizeof region) != sizeof region ||
      region.State != MEM_COMMIT || (uintptr_t)region.AllocationBase != image ||
      (region.Type != MEM_IMAGE && region.Type != MEM_MAPPED))
    return false;
  return region.Protect == PAGE_EXECUTE_READ ||
         region.Protect == PAGE_EXECUTE_READWRITE ||
         region.Protect == PAGE_EXECUTE_WRITECOPY;
}
/* Keep the entire indirect call, including the result store, inside the
 * protected callee. This is the same C SEH boundary as the slot adapter. */
static __declspec(noinline) void invoke(FpNativeOperation op, uintptr_t address,
                                        const FpSnapshot *s, bool *allowed) {
  if (op == FP_NATIVE_ALLOWED)
    *allowed = ((uint8_t (*)(void *, uint32_t))address)((void *)s->controller,
                                                        s->weapon) != 0;
  else if (op == FP_NATIVE_ENTER)
    ((void (*)(void *, uint32_t, uint32_t))address)((void *)s->context,
                                                    s->avatar, s->weapon);
  else
    ((void (*)(void *))address)((void *)s->context);
}
bool fp_native_platform(void *unused, FpNativeOperation op, const FpMemory *m,
                        const FpSnapshot *s, bool *allowed) {
  (void)unused;
  if (op > FP_NATIVE_EXIT || !m || !s || !allowed)
    return false;
  uintptr_t at = m->game + (op == FP_NATIVE_ALLOWED ? FP_ALLOWED
                            : op == FP_NATIVE_ENTER ? FP_ENTRY
                                                    : FP_EXIT);
  if (!executable(at, m->game))
    return false;
  void (*volatile protected_call)(FpNativeOperation, uintptr_t,
                                  const FpSnapshot *, bool *) = invoke;
  __try {
    protected_call(op, at, s, allowed);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
