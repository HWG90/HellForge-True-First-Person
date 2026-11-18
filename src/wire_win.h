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
/* The aim button (src/wire_mouse.h). The feel build installs its engine caller; without one the
 * level comes from Windows. tfp_win_mouse_note: true once per change of why the engine is or is
 * not used ("" = the engine answers), for one log line. */
struct TfpWireMouseHost;
void tfp_win_mouse_engine(const struct TfpWireMouseHost *engine);
/* Windows' own level of button 1 (right) or 2 (middle), 0 while the game is not the foreground window. No
 * engine call: usable between ticks (src/wire_action.h). */
int tfp_win_button(int button);
void tfp_win_buttons(int *right, int *middle); /* both, with one look at the foreground window */
const char *tfp_win_mouse_source(void);
bool tfp_win_mouse_note(char *out, size_t size);
/* For src/wire_win_pose.c: the page and write rules the four helper hosts use (native_memory.h,
 * native_ads_camera.cpp:228-232, :432-445, native_ads_fov.cpp:355-361), and a code pin compared
 * through hf->read_code (false too when read_code is not there). */
bool tfp_win_code_is(uint64_t at, const char *hex);
bool tfp_win_writable_slot(uint64_t at, uint64_t exe);
bool tfp_win_writable32(uint64_t at);
bool tfp_win_executable(uint64_t at);
bool tfp_win_write(void *out, const void *data, size_t length);
bool tfp_win_pin(uint64_t address);
#endif
