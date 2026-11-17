/* The lua51.dll bridge (src/engine.c) as the table src/wire_engine.c takes. */
#ifndef TFP_WIRE_WIN_ENGINE_H
#define TFP_WIRE_WIN_ENGINE_H
#include "wire_engine.h"
#include "hellforge_mod.h"

/* From hellforge_mod_init, on the loader's thread: hooks lua51.dll's lua_pcall and lua_close
 * (eng_init). false: no bridge (the reason is logged and is eng_status()); the modules of
 * wire_engine.c then stay off for the session. */
bool tfp_win_engine_init(const HfApi *hf, HfMod *mod);
/* For tfp_wire_engine_bind. Valid for the life of the process. */
const TfpWireEngineApi *tfp_win_engine_api(void);
#endif
