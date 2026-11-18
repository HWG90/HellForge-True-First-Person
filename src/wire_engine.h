/* The feel build's engine half: head visibility with its model shadow source, the sleeves
 * (arm pieces) and the hand grips, bound to the game's own script functions.
 *
 *   tfp_runtime.c -> TfpRtHost -> wire_engine.c -> head_visibility.c, model_shadow_source.c,
 *                                                  arm_pieces.c, grip_hand.c
 *                                               -> TfpWireEngineApi: the lua51.dll bridge (src/engine.h)
 *                                               -> TfpWireEngineMemory: the backend's readers
 *
 * Nothing here is Windows: the bridge comes in as a table of functions (src/wire_win_engine.c fills
 * it from src/engine.c; tests/wire_engine_test.c from a scripted engine). Every engine function a
 * module uses is resolved and compared with this build's own C function before the module may run
 * (tfp_wire_engine_functions), and a set of read-only checks runs once in the game before a module
 * is allowed to write (work/feel-doc/engine.md, arms.md). Until then, and after a refusal, the
 * module's host pointers give the "off" answers of wire_off.c.
 *
 * Calls come from the game-thread tick only, inside the tick's eng_temp_begin / eng_temp_end. */
#ifndef TFP_WIRE_ENGINE_H
#define TFP_WIRE_ENGINE_H
#include "wire.h"
#include "engine_iface.h"
#include "arm_engine_iface.h"
#include <stdarg.h>

typedef int TfpEngRef; /* src/engine.h's EngRef: 0 is none */

/* src/engine.h, as a table. `call` takes eng_call's signatures. */
typedef struct TfpWireEngineApi {
  void *user;
  bool (*ready)(void *user);                              /* eng_ready */
  uint64_t (*generation)(void *user);                     /* eng_state_generation */
  const char *(*error)(void *user);                       /* eng_error */
  bool (*has)(void *user, const char *path);              /* eng_has */
  uintptr_t (*cfunction)(void *user, const char *path);   /* eng_cfunction: 0 when it is not a C function */
  bool (*call)(void *user, const char *path, const char *sig, va_list args); /* eng_callv */
  void (*unref)(void *user, TfpEngRef *ref);
  uintptr_t (*pointer)(void *user, TfpEngRef ref);        /* eng_pointer */
  int (*type)(void *user, TfpEngRef ref);                 /* eng_type: 1 boolean, 5 table, ... */
  bool (*boolean)(void *user, TfpEngRef ref, bool *out);  /* eng_toboolean */
  int (*len)(void *user, TfpEngRef table);                /* eng_len */
  bool (*index)(void *user, TfpEngRef table, int i, TfpEngRef *out);            /* eng_index */
  int (*userdata_array)(void *user, TfpEngRef table, uintptr_t *out, int max);  /* eng_userdata_array */
  double (*ticks)(void *user);                            /* GetTickCount64() / 1000: head visibility's clock */
  uint64_t (*exe)(void *user);                            /* base of helldivers2.exe; 0: not known yet */
} TfpWireEngineApi;

/* Game memory for the in-game checks and the modules' readers. NULL in tfp_wire_engine_bind: the
 * binder's backend (tfp_ops_read, tfp_ops_alignment_context). The tests give a scripted one. */
typedef struct TfpWireEngineMemory {
  void *user;
  bool (*read)(void *user, uint64_t at, void *out, size_t size);
  /* backend:_alignment_context{viewmodel=unit, avatar_owner=, avatar_index=}.object */
  bool (*object)(void *user, uint32_t unit, bool has_owner, uint64_t owner, bool has_index, uint32_t index, uint64_t *object,
                 char *why, size_t why_size);
} TfpWireEngineMemory;

typedef enum TfpWireEngineModule {
  TFP_WE_HEAD,     /* "head visibility" */
  TFP_WE_SHADOWS,  /* "model shadows" */
  TFP_WE_SLEEVES,  /* "arm pieces" */
  TFP_WE_GRIP,     /* "grip hand" */
  TFP_WE_MODULES
} TfpWireEngineModule;
typedef enum TfpWireEngineState {
  TFP_WE_WAITING,   /* the engine's Lua state is not caught yet: off answers */
  TFP_WE_RESOLVED,  /* every function is this build's; the in-game checks have not passed yet: off answers */
  TFP_WE_READY,     /* bound */
  TFP_WE_REFUSED    /* off for the session */
} TfpWireEngineState;
const char *tfp_wire_engine_module_name(TfpWireEngineModule module);

/* One engine function a module needs: its path under `stingray`, where this build's C body is
 * (exe + rva; 0: a constructor table, only its presence is asked) and the line of
 * docs/research-0.5.34/live-sdk-census-g1-9472b065.tsv that says so. */
typedef struct TfpWireEngineFunction { const char *path; uint32_t rva; uint16_t census; uint8_t modules; /* bit per module */ } TfpWireEngineFunction;
const TfpWireEngineFunction *tfp_wire_engine_functions(size_t *count);

/* After tfp_wire_init, before the runtime's on_enable. false: no memory for the modules (nothing
 * was bound; the off answers stay). `memory` may be NULL. */
bool tfp_wire_engine_bind(TfpWire *w, const TfpWireEngineApi *api, const TfpWireEngineMemory *memory);
/* First thing in every tick (inside the temp frame): notices the engine's Lua state arriving,
 * leaving or being replaced, resolves the functions and logs the Ready / Refused lines. */
void tfp_wire_engine_begin(TfpWire *w);
/* Once per tick after on_update: this frame's numbers for the session log. */
void tfp_wire_engine_frame(TfpWire *w);
/* Once a second while the mode is on: "head=active:3 sleeves=linked:8 grip=on corr=12
 * max=0.004m/1.2deg"; the accumulators start again. Returns the characters written. */
size_t tfp_wire_engine_second(TfpWire *w, char *out, size_t size);
/* For a live replace: bit 0 head visibility owns rows, bit 1 arm pieces are hung, bit 2 the grip hand
 * has written bones or hidden a hand. Reads only. */
unsigned tfp_wire_engine_held(const TfpWire *w);
TfpWireEngineState tfp_wire_engine_state(const TfpWire *w, TfpWireEngineModule module);

/* The 27 Vector3 / Quaternion / Matrix4x4 value functions, computed here on 32-bit floats with the
 * engine's conventions (Quaternion.multiply(second, first); Matrix4x4.multiply(a, b) is a, then b).
 * A Matrix4x4 is a rigid pose: the rotation in m[0..3], the position in m[4..6]. Only those 27
 * pointers of *engine are written. tests/run-wire-engine.sh replays the arm modules' vectors over
 * them (same value-maths checksums as retail's reference run). */
void tfp_wire_engine_maths(TfpArmEngine *engine);

/* ---- for tests/wire_engine_test.c: the pieces with logic of their own ---- */
/* The tables the modules get (filled once by bind; the pointers never change afterwards). */
const TfpEngineIface *tfp_wire_engine_head_iface(const TfpWire *w);
const TfpArmEngine *tfp_wire_engine_arm_iface(const TfpWire *w, TfpWireEngineModule module);
/* userdata_address of a handle: the number the retail host gives for the engine value. */
double tfp_wire_engine_address(const TfpWire *w, const void *handle);
/* The in-game checks, each logging its Check / Refused line through tfp_wire_say. */
/* Whose units: the fields of a snapshot the checks ask the backend with. */
typedef struct TfpWireEngineBody { uint32_t body, viewmodel, avatar_index; uint64_t avatar_owner; bool has_owner; } TfpWireEngineBody;
bool tfp_wire_engine_check_head(TfpWire *w, const TfpWireEngineBody *s);
/* 1 passed, 0 refused, -1 cannot be judged yet (nothing hangs on the rig). `sleeves` / `grip`: each module's verdict. */
int tfp_wire_engine_check_arms(TfpWire *w, const TfpWireEngineBody *s, const TfpArmHandles *handles, bool *sleeves, bool *grip);
bool tfp_wire_engine_check_maths(TfpWire *w);
/* Tests only: a module's state without its checks (which need a live player). */
void tfp_wire_engine_force(TfpWire *w, TfpWireEngineModule module, TfpWireEngineState state);
#endif
