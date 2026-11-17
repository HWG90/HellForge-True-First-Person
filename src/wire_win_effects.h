/* The engine and Windows half of the effect modules: a TfpFxPlatform over the engine bridge
 * (src/engine.h: eng_call and friends, as the 0.5.34 port called these functions in src/effects.c)
 * and over VirtualQuery, WriteProcessMemory and a guarded call of the game's widget setter.
 * See wire_win_effects.c. */
#ifndef TFP_WIRE_WIN_EFFECTS_H
#define TFP_WIRE_WIN_EFFECTS_H
#include "wire_effects.h"

/* Fills `out`. exe: the base of helldivers2.exe (tfp_win_exe()). Every engine function of the
 * platform must be called on the game-thread tick, inside the frame's eng_temp_begin / eng_temp_end
 * if the mod uses them; nothing here opens or closes a temporary frame. */
void tfp_win_effects_platform(TfpFxPlatform *out, uint64_t exe);
/* Forgets the engine handles (world, manager, entity list, kept entities). Safe at any time on the
 * game thread; the next vignette call starts again from Application.main_world. */
void tfp_win_effects_forget(void);
/* The last World.entities list the vignette read: its entries, the microseconds reading it took and
 * the worst so far (for the session log). false: none read yet. */
bool tfp_win_effects_last_scan(uint32_t *entries, uint32_t *microseconds, uint32_t *worst);
#endif
