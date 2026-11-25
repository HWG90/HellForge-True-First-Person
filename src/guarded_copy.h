/* Round 28 (docs/design/specs/2026-11-25-performance.md): the copy behind host_write in src/wire_win.c.
 *
 * Up to 0.6.0-feel.53 host_write was WriteProcessMemory(GetCurrentProcess()), as retail's helper DLLs did: under Wine
 * every call is a wineserver round trip (about 5 us idle, more under load: the path the mouse-lag note blames). Every
 * place that calls it hands the address of the mod's own memory (the audit in the spec above), so the copy is a plain
 * memcpy, guarded as the mod's other guarded paths are (host_cas32 in wire_win.c, src/slot_atomic.c): the copy runs in
 * a function called through a volatile pointer inside __try, so the compiler keeps it inside the guarded scope and an
 * address that faults answers false instead of crashing the game, as WriteProcessMemory answered FALSE.
 *
 * Game memory is not written here: ops_write (hf->write) and the warp glow's mode word keep their own paths. Windows
 * only: the DLL and tests/host_write_test.c. */
#ifndef TFP_GUARDED_COPY_H
#define TFP_GUARDED_COPY_H
#include <windows.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static __declspec(noinline) void tfp_guarded_copy_raw(void *out, const void *data, size_t length) { memcpy(out, data, length); }
static void (*volatile tfp_guarded_copy_dispatch)(void *, const void *, size_t) = tfp_guarded_copy_raw;
/* `length` bytes from `data` to `out`. false: reading or writing faulted (some bytes may have been written before). */
static bool tfp_guarded_copy(void *out, const void *data, size_t length) {
  bool done = false;
  __try { tfp_guarded_copy_dispatch(out, data, length); done = true; }
  __except (EXCEPTION_EXECUTE_HANDLER) { done = false; }
  return done;
}
#endif
