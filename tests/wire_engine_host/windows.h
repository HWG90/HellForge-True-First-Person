/* The few Windows names src/engine.c and src/wire_win_engine.c use, for tests/wire_engine_test.c on the
 * host: the test fills engine.c's table of Lua C API functions itself, so nothing here loads a DLL. */
#ifndef TFP_WIRE_ENGINE_HOST_WINDOWS_H
#define TFP_WIRE_ENGINE_HOST_WINDOWS_H
#include <stdint.h>
typedef void *HMODULE;
typedef void (*FARPROC)(void);
typedef int32_t LONG;
static inline HMODULE GetModuleHandleA(const char *name) { (void)name; return (HMODULE)0; }
static inline FARPROC GetProcAddress(HMODULE module, const char *name) { (void)module; (void)name; return (FARPROC)0; }
static inline LONG InterlockedIncrement(volatile LONG *value) { return ++*value; }
static inline LONG InterlockedExchange(volatile LONG *value, LONG next) { LONG was = *value; *value = next; return was; }
extern uint64_t tfp_test_ticks;
static inline uint64_t GetTickCount64(void) { return tfp_test_ticks; }
#endif
