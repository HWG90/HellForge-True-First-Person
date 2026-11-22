/* The primary world render hook of the arm drive, and the drive's Windows host. See render_slot.c.
 * Call tfp_win_setup (wire_win.h) first. */
#ifndef TFP_RENDER_SLOT_H
#define TFP_RENDER_SLOT_H
#include "wire_win.h"

/* Once: the HellForge API (for reads of code as it is now, beside the reads from before any hook), and the
 * arm drive's host (tfp_arm_drive_set_host). */
void tfp_render_slot_setup(const HfApi *api);
/* The contract of build 25480438, reads only: the two images, the primary draw's call bytes, the world
 * renderer's entry, the engine's root and application table, and what the slot holds. NULL: it holds the
 * renderer or this mod's hook, and the hook may go in. Otherwise one line saying what differs. */
const char *tfp_render_slot_contract(void);
/* Game thread. Checks the contract, then swaps the slot from the renderer to this mod's hook with one
 * compare-and-swap. NULL: the hook is in (or was already). Otherwise why not; nothing was written. */
const char *tfp_render_slot_install(void);
/* Puts the renderer back if the slot still holds this mod's hook; a value another mod put there since is
 * left alone. true: this mod's hook is not in the slot any more. */
bool tfp_render_slot_remove(void);
bool tfp_render_slot_installed(void);
/* Session end and live replace, as tfp_win_restore_slots: one compare-and-swap, nothing else. 1: put back. */
unsigned tfp_render_slot_restore(void);
/* Every tick of the mod: which thread the game thread is, and when it last ran. The hook acts only on that
 * thread, within half a second of a tick. */
void tfp_render_slot_tick(void);
/* No call is inside the hook now (a call stays inside for a whole world render). */
bool tfp_render_slot_idle(void);
/* calls: every call through the slot; first_thread / first_return: of the first primary draw seen. */
const TfpWinHook *tfp_render_slot_hook(void);
#endif
