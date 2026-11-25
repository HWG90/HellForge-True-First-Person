/* The few Windows calls src/mod_menu.c reaches through goose_mcm.h's mcm_connect() and goose_overlay.h's ov_connect(),
 * for tests/mod_menu_test.c on Linux: the test defines them and plays the loaded goose_mcm.dll. */
#ifndef TFP_FAKE_WINDOWS_H
#define TFP_FAKE_WINDOWS_H
typedef void *HMODULE;
typedef void (*FARPROC)(void);
HMODULE GetModuleHandleA(const char *name);
FARPROC GetProcAddress(HMODULE module, const char *name);
#endif
