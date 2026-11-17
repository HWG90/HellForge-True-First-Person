/* The Windows / HellForge half of the view build: what retail's helper DLLs and backend took from
 * Windows, given to the translated units through their host structs. See wire_win.c. */
#ifndef TFP_WIRE_WIN_H
#define TFP_WIRE_WIN_H
#include "hellforge_mod.h"
#include "wire.h"

enum { TFP_WIN_CAMERA, TFP_WIN_FOV, TFP_WIN_NEAR, TFP_WIN_BLUR, TFP_WIN_HOOKS };
typedef struct TfpWinHook {   /* written by the hook's own thread, read by the tick */
  uint32_t calls;             /* total */
  uint32_t first_thread;      /* 0: never fired */
  uint64_t first_return;      /* the caller's return address of the first call */
  uint64_t slot, original;    /* where it is installed now (0: nowhere) and what was there */
  uint32_t refused;           /* 1: an install was refused by the pin check; the tick logs and clears it */
} TfpWinHook;

/* Module bases, the code ranges and the wine flag. false: a module is not loaded (yet). */
bool tfp_win_setup(const HfApi *api, HfMod *mod);
/* The memory host of the backend, the platform of the binder, the linked helpers. Sets the four
 * helper hosts (they must be set before the first adapter call). */
void tfp_win_hosts(TfpOpsHost *memory, TfpWirePlatform *platform, TfpWireHelpers *helpers);
/* All bytes, read as they were before any HellForge hook when `at` is code of game.dll or the exe. */
bool tfp_win_read(uint64_t at, void *out, size_t size);
uint64_t tfp_win_game(void);
uint64_t tfp_win_exe(void);
const TfpWinHook *tfp_win_hook(int which);
const char *tfp_win_hook_name(int which);
void tfp_win_hook_refusal_seen(int which);
/* Session end: puts back every dispatch slot that still holds one of this mod's hooks, with one
 * compare-and-swap each and nothing else (no lock, no game call). Returns how many were put back. */
unsigned tfp_win_restore_slots(void);
/* F11 and focus. */
bool tfp_win_focused(void);
bool tfp_win_key_down(int virtual_key);
#endif
