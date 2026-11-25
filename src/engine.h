/* The engine bridge: the game's own Lua state (LuaJIT, lua51.dll), driven from C through the Lua C API.
 *
 * Helldivers 2's engine (Stingray) exposes its world, units, cameras, lights and renderer to scripts as the `stingray`
 * table. War Journalist's camera, lens, lights and poses were written against that API; this bridge calls the same
 * functions natively, with no Lua source of its own: the C API pushes a function and its arguments and pcalls it.
 * The state is caught by hooking lua51.dll's lua_pcall (the engine calls it for every script update); every call here
 * must come from the game thread while the engine's own script is not running (HellForge's on_game_thread tick). */
#ifndef PHOTO_ENGINE_H
#define PHOTO_ENGINE_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hellforge_mod.h"

/* A value kept in the Lua registry: 0 is none. It is tied to the state it was made in: once eng_state_generation
 * changes, every use of it is refused (the call fails, eng_error says so, eng_stale_uses counts it), and eng_unref only
 * forgets it. */
typedef int EngRef;

bool eng_init(const HfApi* hf, HfMod* mod); /* from hellforge_mod_init: hooks lua51.dll */
bool eng_ready(void);                        /* the engine's main Lua state is known and `stingray` exists */
/* A function run each time the engine pcalls on its main state, before the call goes on (NULL: none). It runs
 * on the engine's thread with a script call pending: it must not use any eng_ call, and must return quickly. */
void eng_set_script_probe(void (*probe)(void));
const char* eng_status(void);                /* why eng_ready is false, or "ready" */
const char* eng_error(void);                 /* the last failed call's message */
uint64_t eng_state_generation(void);         /* changes when the state is replaced: every EngRef is then dead */
uint64_t eng_stale_uses(void);               /* how many uses of a dead EngRef were refused (eng_unref excepted) */

/* Calls stingray.<path> ("Camera.set_vertical_fov", "Vector3", "World.units"), with `sig` describing the arguments
 * before '>' and the results after it. Arguments: n double, i int, b int (boolean), s const char*, r EngRef (0 for
 * nil), v const float* (three floats: a Vector3), q const float* (four floats x y z w: a Quaternion), N nothing (nil),
 * u uint64_t (an IdString64 from its hex: "IdString64.from_hex"), p const char* (a property path: the one-string array
 * {s} RenderDataComponent's getters and setters take). Results: n double*, i int*, b int* (truthiness),
 * r EngRef* (a new registry reference; 0 when the result is nil), v float* (three floats read from a Vector3),
 * q float* (four floats from a Quaternion), s char* with the next argument size_t (copied), t int* (the Lua type),
 * '_' to drop a result, and, last only, R EngRef* (every remaining result, however many, packed in order into a new
 * table: a tuple-returning function). Missing numeric results, non-finite numbers and invalid integers fail; references
 * may be nil.
 * References created before a later result fails are released. Returns true, or false with eng_error() set. */
bool eng_call(const char* path, const char* sig, ...);
bool eng_callv(const char* path, const char* sig, va_list args); /* eng_call with the arguments as a va_list */
/* A method of a userdata (a Matrix4x4Box's unbox): object:method(...) with the same signatures. */
bool eng_method(EngRef object, const char* method, const char* sig, ...);
bool eng_has(const char* path); /* stingray.<path> is a function (or a table) */
/* Round 28: how many protected calls eng_call / eng_method made (counted when the call starts, failed ones too)
 * and how many lines the bridge logged, since the start. Cumulative; the Perf line logs the difference a second. */
void eng_counts(uint64_t* calls, uint64_t* logs);
uintptr_t eng_cfunction(const char* path); /* the C function behind stingray.<path> (its binding's address), or 0 */
/* Read-only raw SDK table census. No SDK function or metatable is invoked.
 * Bounds and omitted/aliased branches are explicit; caller is game-thread only. */
typedef struct EngSdkCensus {
    unsigned rows,tables,aliases,omitted,depth_limited;
    bool limit_reached,complete;
} EngSdkCensus;
typedef bool (*EngSdkVisitor)(void *,const char *path,int lua_type,uintptr_t cfunction,bool alias);
bool eng_sdk_census(EngSdkVisitor,void *,unsigned max_rows,EngSdkCensus *);

/* Shipped steam_runtime.time_policy's setter wrapper, implemented as a native Lua closure.
 * The saved binding is callable without notifying our observer. A retired closure keeps
 * delegating to its own saved binding. Detach preserves a successor's live binding.
 * observer(false): external write attempted; observer(true): it succeeded (not false).
 * All functions are game-thread only, like eng_call. */
bool eng_policy_attach(void (*observer)(bool), EngRef* original, EngRef* wrapper);
bool eng_policy_live(EngRef wrapper);
bool eng_policy_write(EngRef original, double value);
void eng_policy_detach(EngRef* original, EngRef* wrapper);

void eng_unref(EngRef* ref);           /* releases a reference (sets it to 0) */
bool eng_same(EngRef a, EngRef b);     /* rawequal */
EngRef eng_copy(EngRef ref);           /* another reference to the same value */
uintptr_t eng_pointer(EngRef ref);     /* userdata address; World.units Unit handles encode (registry_ref << 2) | 1 */
int eng_len(EngRef table);             /* #table (0 for anything else) */
bool eng_index(EngRef table, int i, EngRef* out); /* table[i] as a new reference (0 for nil) */
bool eng_pairs_count(EngRef table, int* count);   /* how many keys a table has (next-based) */
/* The userdata addresses (eng_pointer's number; 0 for anything that is not a userdata) of table[1..#table], with no
 * reference made: at most `max` are written. Returns #table, or -1 when it is not a table. For World.units' list. */
int eng_userdata_array(EngRef table, uintptr_t* out, int max);
/* A table's entries in next() order (holes and all, as a native inventory may have them): at most `max` keys (NAN for
 * a key that is not a number) and values (new references, 0 for nil). Returns how many it read, `max` + 1 when the
 * table has more than `max` (the extra one is not kept), or -1 when it is not a table. */
int eng_entries(EngRef table, double* keys, EngRef* values, int max);
bool eng_field(EngRef table, const char* key, EngRef* out); /* rawget(table, key) */
bool eng_number_field(EngRef table, const char* key, double* out);
/* A reference's own value: its Lua type (LUA_T*: 0 nil (ref 0), 1 boolean, 2 light userdata, 3 number, 4 string,
 * 5 table, 7 userdata), -1 for a dead reference; and the number, boolean or string it holds (false when it is not one). */
int eng_type(EngRef ref);
bool eng_tonumber(EngRef ref, double* out);
bool eng_toboolean(EngRef ref, bool* out);
bool eng_tostring(EngRef ref, char* out, size_t size);

/* The engine's temporary values (Vector3, Matrix4x4 lightuserdata) live in a per-frame buffer: everything made between
 * eng_temp_begin and eng_temp_end is released at the end, as the engine's own script does with Script.set_temp_count. */
void eng_temp_begin(void);
void eng_temp_end(void);

/* Convenience: a Matrix4x4 (a temporary) boxed for keeping (a Matrix4x4Box userdata), and back. */
bool eng_box_matrix(EngRef matrix, EngRef* box);
bool eng_unbox_matrix(EngRef box, EngRef* matrix); /* the result is a temporary */
/* The four vectors of a Matrix4x4 temporary: translation, forward, right, up. */
bool eng_matrix_basis(EngRef matrix, float position[3], float forward[3], float right[3], float up[3]);

#endif
