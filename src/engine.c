/* The engine bridge (engine.h): lua51.dll's exports, resolved by name; the engine's main state, caught from its
 * lua_pcall; calls into the stingray table through a signature-driven marshaller, every one protected. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include "engine.h"

/* ---- the Lua C API, as lua51.dll exports it (LuaJIT 2.1) ---------------------------------------------------------- */

typedef struct lua_State lua_State;
typedef double lua_Number;
typedef ptrdiff_t lua_Integer;
typedef int (*lua_CFunction)(lua_State* L);
#define LUA_REGISTRYINDEX (-10000)
#define LUA_GLOBALSINDEX (-10002)
#define LUA_MULTRET (-1)
#define LUA_TNIL 0
#define LUA_TBOOLEAN 1
#define LUA_TLIGHTUSERDATA 2
#define LUA_TNUMBER 3
#define LUA_TSTRING 4
#define LUA_TTABLE 5
#define LUA_TFUNCTION 6
#define LUA_TUSERDATA 7
#define LUA_TTHREAD 8
#define LUA_NOREF (-2)

static struct
{
    int (*pcall)(lua_State*, int, int, int);
    int (*error)(lua_State*);
    void (*settop)(lua_State*, int);
    int (*gettop)(lua_State*);
    void (*pushvalue)(lua_State*, int);
    void (*pushnil)(lua_State*);
    void (*pushnumber)(lua_State*, lua_Number);
    void (*pushinteger)(lua_State*, lua_Integer);
    void (*pushboolean)(lua_State*, int);
    void (*pushstring)(lua_State*, const char*);
    void (*pushcclosure)(lua_State*, lua_CFunction, int);
    int (*pushthread)(lua_State*);
    void (*rawget)(lua_State*, int);
    void (*rawgeti)(lua_State*, int, int);
    void (*rawset)(lua_State*, int);
    int (*rawequal)(lua_State*, int, int);
    int (*type)(lua_State*, int);
    lua_Number (*tonumber)(lua_State*, int);
    int (*toboolean)(lua_State*, int);
    const char* (*tolstring)(lua_State*, int, size_t*);
    void (*getfield)(lua_State*, int, const char*);
    size_t (*objlen)(lua_State*, int);
    int (*next)(lua_State*, int);
    void (*insert)(lua_State*, int);
    void (*remove)(lua_State*, int);
    int (*checkstack)(lua_State*, int);
    int (*ref)(lua_State*, int);
    void (*unref)(lua_State*, int, int);
    const void* (*topointer)(lua_State*, int);
    void* (*touserdata)(lua_State*, int);
    lua_CFunction (*tocfunction)(lua_State*, int);
    void (*createtable)(lua_State*, int, int);
    void (*rawseti)(lua_State*, int, int);
} L_;

static const HfApi* g_hf;
static HfMod* g_mod;
static lua_State* volatile g_state;
static const void* volatile g_registryIdentity;
static volatile LONG g_stateGeneration;
static char g_error[512];
static char g_status[256] = "waiting for the engine's Lua state";
static EngRef g_stingray; /* the stingray table */
static uint64_t g_refGeneration;
/* Round 28: protected engine calls (CallV: eng_call and eng_method) and log lines, counted for the Perf line. Game
 * thread only, as every eng_ call; plain counters. */
static uint64_t s_countCalls, s_countLogs;
void eng_counts(uint64_t* calls, uint64_t* logs) { *calls = s_countCalls, *logs = s_countLogs; }

static void Log(const char* format, ...)
{
    char line[1024];
    s_countLogs++;
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (g_hf) g_hf->log(g_mod, "%s", line);
}

static void Fail(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(g_error, sizeof g_error, format, args);
    va_end(args);
}

const char* eng_error(void) { return g_error; }
const char* eng_status(void) { return g_status; }
uint64_t eng_state_generation(void) { return (uint64_t)g_stateGeneration; }

/* ---- references --------------------------------------------------------------------------------------------------- */

/* An EngRef is a registry index (bits 0..21) tagged with the state generation it was made in (bits 22..30, the
 * generation mod 511, plus 1). A reference that outlived its state is refused here: in the new state its index names
 * whatever that registry holds there, and releasing it would free a slot someone else owns. */
#define REF_INDEX_BITS 22
#define REF_INDEX_MASK ((1 << REF_INDEX_BITS) - 1)
static volatile LONG s_staleUses;
static LONG s_staleLogged = -1; /* the generation whose first stale use was logged */

static int RefTag(void) { return (int)((uint32_t)g_stateGeneration % 511u) + 1; }

/* A registry index fresh from luaL_ref as an EngRef: 0 for nil (or a refused index, which is released). */
static EngRef MakeRef(lua_State* L, int index)
{
    if (index <= 0) return 0;
    if (index > REF_INDEX_MASK) { L_.unref(L, LUA_REGISTRYINDEX, index); Fail("registry index %d is beyond the bridge's range", index); return 0; }
    return index | (RefTag() << REF_INDEX_BITS);
}

static bool RefLive(EngRef ref) { return (ref >> REF_INDEX_BITS) == RefTag(); }

/* The registry index behind a live reference, or 0 for none; -1 (counted, logged once per generation, eng_error set)
 * for one made in a state that has since been replaced. */
static int RefIndex(EngRef ref, const char* what)
{
    if (ref <= 0) return 0;
    if (RefLive(ref)) return ref & REF_INDEX_MASK;
    InterlockedIncrement(&s_staleUses);
    Fail("%s: a reference from a replaced Lua state", what);
    if (s_staleLogged != g_stateGeneration) { s_staleLogged = g_stateGeneration; Log("refused a reference from a replaced Lua state (%s)", what); }
    return -1;
}

uint64_t eng_stale_uses(void) { return (uint64_t)s_staleUses; }

/* ---- catching the state ----------------------------------------------------------------------------------------- */

typedef int (*PcallFn)(lua_State*, int, int, int);
typedef void (*CloseFn)(lua_State*);
static PcallFn s_pcall;
static CloseFn s_close;
static bool s_closeInstalled, s_pcallInstalled;
static volatile LONG s_captureEnabled;

/* Only the game's native SDK VM may drive visibility/effect calls. A Lua
 * loader can use the same lua51.dll for a different main state. */
static bool NativeSdkState(lua_State* L)
{
    if (!g_hf || !g_hf->module_base || !L_.checkstack(L, 4)) return false;
    size_t size=0;uintptr_t exe=g_hf->module_base("helldivers2.exe",&size);
    if(!exe)return false;
    int top=L_.gettop(L);bool native=false;
    L_.pushstring(L,"stingray");L_.rawget(L,LUA_GLOBALSINDEX);
    if(L_.type(L,-1)==LUA_TTABLE){
        L_.pushstring(L,"Unit");L_.rawget(L,-2);
        if(L_.type(L,-1)==LUA_TTABLE){
            L_.pushstring(L,"set_mesh_visibility");L_.rawget(L,-2);
            native=L_.type(L,-1)==LUA_TFUNCTION && (uintptr_t)(void*)L_.tocfunction(L,-1)==exe+0x4128e0;
        }
    }
    L_.settop(L,top);return native;
}
static void Capture(lua_State* L)
{
    if (L == NULL || g_state == L) return;
    /* the engine's main thread only: a coroutine's state dies with it */
    if (!L_.checkstack(L, 2)) return; /* No value was pushed: preserve the caller's stack. */
    if (L_.pushthread(L) != 1) { L_.settop(L, L_.gettop(L) - 1); return; }
    L_.settop(L, L_.gettop(L) - 1);
    if(!NativeSdkState(L))return;
    const void* registry=L_.topointer(L,LUA_REGISTRYINDEX);
    if(!registry)return;
    g_registryIdentity=registry;
    g_state = L;
    InterlockedIncrement(&g_stateGeneration);
    g_stingray = 0;
    snprintf(g_status, sizeof g_status, "state %p caught", (void*)L);
    Log("the engine's Lua state is %p (generation %ld)", (void*)L, (long)g_stateGeneration);
}

static void (*volatile s_scriptProbe)(void);
void eng_set_script_probe(void (*probe)(void)) { s_scriptProbe = probe; }
static int PcallHook(lua_State* L, int nargs, int nresults, int errfunc)
{
    if (s_captureEnabled && g_state == NULL) Capture(L);
    void (*probe)(void) = s_scriptProbe;
    if (probe && L == g_state) probe();
    return s_pcall(L, nargs, nresults, errfunc);
}
static void CloseHook(lua_State* L)
{
    /* Lua 5.1/LuaJIT closes the whole VM even when passed a coroutine. The
     * registry identity is shared by all its states; unrelated VMs differ. */
    if (g_state && (g_state == L || (g_registryIdentity && L_.topointer(L,LUA_REGISTRYINDEX)==g_registryIdentity)))
    {
        Log("the engine closed its Lua state %p", (void*)L);
        g_state = NULL;
        g_registryIdentity = NULL;
        g_stingray = 0;
        InterlockedIncrement(&g_stateGeneration);
        snprintf(g_status, sizeof g_status, "the Lua state was closed; waiting for a new one");
    }
    s_close(L);
}

/* Resolves the Lua C API from a loaded lua51.dll into L_; shared by eng_init and the test-only adopt entry. */
static bool ResolveApi(HMODULE lua)
{
#define RESOLVE(field, name) do { FARPROC symbol = GetProcAddress(lua, name); _Static_assert(sizeof symbol == sizeof L_.field, "Windows function pointer size"); memcpy(&L_.field, &symbol, sizeof symbol); if (L_.field == NULL) { snprintf(g_status, sizeof g_status, "lua51.dll has no %s", name); Log("%s", g_status); return false; } } while (0)
    RESOLVE(pcall, "lua_pcall");
    RESOLVE(error, "lua_error");
    RESOLVE(settop, "lua_settop");
    RESOLVE(gettop, "lua_gettop");
    RESOLVE(pushvalue, "lua_pushvalue");
    RESOLVE(pushnil, "lua_pushnil");
    RESOLVE(pushnumber, "lua_pushnumber");
    RESOLVE(pushinteger, "lua_pushinteger");
    RESOLVE(pushboolean, "lua_pushboolean");
    RESOLVE(pushstring, "lua_pushstring");
    RESOLVE(pushcclosure, "lua_pushcclosure");
    RESOLVE(pushthread, "lua_pushthread");
    RESOLVE(rawget, "lua_rawget");
    RESOLVE(rawgeti, "lua_rawgeti");
    RESOLVE(rawset, "lua_rawset");
    RESOLVE(rawequal, "lua_rawequal");
    RESOLVE(type, "lua_type");
    RESOLVE(tonumber, "lua_tonumber");
    RESOLVE(toboolean, "lua_toboolean");
    RESOLVE(tolstring, "lua_tolstring");
    RESOLVE(getfield, "lua_getfield");
    RESOLVE(objlen, "lua_objlen");
    RESOLVE(next, "lua_next");
    RESOLVE(insert, "lua_insert");
    RESOLVE(remove, "lua_remove");
    RESOLVE(checkstack, "lua_checkstack");
    RESOLVE(ref, "luaL_ref");
    RESOLVE(unref, "luaL_unref");
    RESOLVE(topointer, "lua_topointer");
    RESOLVE(touserdata, "lua_touserdata");
    RESOLVE(tocfunction, "lua_tocfunction");
    RESOLVE(createtable, "lua_createtable");
    RESOLVE(rawseti, "lua_rawseti");
#undef RESOLVE
    return true;
}

bool eng_init(const HfApi* hf, HfMod* mod)
{
    g_hf = hf;
    g_mod = mod;
    if (s_closeInstalled && s_pcallInstalled) return true;
    HMODULE lua = GetModuleHandleA("lua51.dll");
    if (lua == NULL) { snprintf(g_status, sizeof g_status, "lua51.dll is not loaded"); Log("%s", g_status); return false; }
    if (!ResolveApi(lua)) return false;
    FARPROC close = GetProcAddress(lua, "lua_close");
    if (close == NULL) { snprintf(g_status, sizeof g_status, "lua51.dll has no lua_close; state lifetime cannot be tracked"); Log("%s", g_status); return false; }
    /* A captured state must never outlive its VM. Install lifetime tracking
     * first; a retained close hook is harmless if pcall admission then fails. */
    if (!s_closeInstalled) {
        if (hf->hook(mod, (uintptr_t)(void*)close, (void*)CloseHook, (void**)&s_close) != 0 || s_close == NULL) {
            snprintf(g_status, sizeof g_status, "lua_close could not be hooked; engine bridge unavailable"); Log("%s", g_status); return false;
        }
        s_closeInstalled = true;
    }
    if (!s_pcallInstalled) {
        if (hf->hook(mod, (uintptr_t)(void*)L_.pcall, (void*)PcallHook, (void**)&s_pcall) != 0 || s_pcall == NULL) {
            snprintf(g_status, sizeof g_status, "lua_pcall could not be hooked; capture remains disabled"); Log("%s", g_status); return false;
        }
        s_pcallInstalled = true;
    }
    InterlockedExchange(&s_captureEnabled, 1);
    Log("lua51.dll hooked: the engine's state is caught at its next script call");
    return true;
}

#ifdef PHOTO_TEST
/* Test only: the harness's own VM (the game's lua51.dll, loaded by the test) becomes the engine's main state, with the
 * API resolved exactly as eng_init does. No hooks are installed; a state that replaces another bumps the generation. */
bool eng_test_adopt(void* lua_state)
{
    lua_State* L = (lua_State*)lua_state;
    if (L == NULL) return false;
    HMODULE lua = GetModuleHandleA("lua51.dll");
    if (lua == NULL) { snprintf(g_status, sizeof g_status, "lua51.dll is not loaded"); return false; }
    if (!ResolveApi(lua)) return false;
    const void* registry = L_.topointer(L, LUA_REGISTRYINDEX);
    if (!registry) return false;
    g_registryIdentity = registry;
    g_state = L;
    InterlockedIncrement(&g_stateGeneration);
    g_stingray = 0;
    snprintf(g_status, sizeof g_status, "state %p adopted", (void*)L);
    return true;
}
#endif

/* ---- the stingray table and cached functions -------------------------------------------------------------------- */

static int GetFieldSafe(lua_State* L) /* a C function pcalled: t[k] through metamethods without a longjmp into us */
{
    L_.getfield(L, 1, L_.tolstring(L, 2, NULL));
    return 1;
}

/* Resolves stingray.<path> onto the stack (true), or pushes nothing (false). Tables are walked with rawget. */
static bool PushPath(lua_State* L, const char* path)
{
    if (!L_.checkstack(L, 8)) return false;
    if (g_stingray == 0 || g_refGeneration != (uint64_t)g_stateGeneration)
    {
        L_.pushstring(L, "stingray");
        L_.rawget(L, LUA_GLOBALSINDEX);
        if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return false; }
        if (g_stingray != 0 && g_refGeneration == (uint64_t)g_stateGeneration) L_.unref(L, LUA_REGISTRYINDEX, g_stingray);
        g_stingray = L_.ref(L, LUA_REGISTRYINDEX);
        g_refGeneration = (uint64_t)g_stateGeneration;
    }
    L_.rawgeti(L, LUA_REGISTRYINDEX, g_stingray);
    const char* at = path;
    while (*at)
    {
        const char* dot = strchr(at, '.');
        size_t n = dot ? (size_t)(dot - at) : strlen(at);
        char key[96];
        if (n == 0 || n >= sizeof key) { L_.settop(L, L_.gettop(L) - 1); return false; }
        memcpy(key, at, n);
        key[n] = 0;
        if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return false; }
        L_.pushstring(L, key);
        L_.rawget(L, -2);
        L_.remove(L, -2);
        at = dot ? dot + 1 : at + n;
    }
    return true;
}

uintptr_t eng_cfunction(const char* path)
{
    lua_State* L = g_state;
    if (L == NULL) return 0;
    int top = L_.gettop(L);
    uintptr_t address = 0;
    if (PushPath(L, path) && L_.type(L, -1) == LUA_TFUNCTION) address = (uintptr_t)(void*)L_.tocfunction(L, -1);
    L_.settop(L, top);
    return address;
}

bool eng_ready(void)
{
    lua_State* L = g_state;
    if (L == NULL) return false;
    int top = L_.gettop(L);
    bool ok = PushPath(L, "Application") && L_.type(L, -1) == LUA_TTABLE;
    L_.settop(L, top);
    if (ok) snprintf(g_status, sizeof g_status, "ready");
    else snprintf(g_status, sizeof g_status, "the stingray table is not available yet");
    return ok;
}

bool eng_has(const char* path)
{
    lua_State* L = g_state;
    if (L == NULL) return false;
    int top = L_.gettop(L);
    bool ok = PushPath(L, path) && (L_.type(L, -1) == LUA_TFUNCTION || L_.type(L, -1) == LUA_TTABLE);
    L_.settop(L, top);
    return ok;
}

/* ---- the marshaller --------------------------------------------------------------------------------------------- */

static bool PushVector3(lua_State* L, const float* v)
{
    if (!PushPath(L, "Vector3")) { Fail("Vector3 unavailable"); return false; }
    L_.pushnumber(L, v[0]);
    L_.pushnumber(L, v[1]);
    L_.pushnumber(L, v[2]);
    if (L_.pcall(L, 3, 1, 0) != 0) { Fail("Vector3: %s", L_.tolstring(L, -1, NULL)); L_.settop(L, L_.gettop(L) - 1); return false; }
    return true;
}
static bool PushQuaternion(lua_State* L, const float* q)
{
    if (!PushPath(L, "Quaternion.from_elements")) { Fail("Quaternion.from_elements unavailable"); return false; }
    for (int i = 0; i < 4; i++) L_.pushnumber(L, q[i]);
    if (L_.pcall(L, 4, 1, 0) != 0) { Fail("Quaternion.from_elements: %s", L_.tolstring(L, -1, NULL)); L_.settop(L, L_.gettop(L) - 1); return false; }
    return true;
}
static bool PushIdString(lua_State* L, uint64_t id)
{
    char hex[24];
    snprintf(hex, sizeof hex, "%016llx", (unsigned long long)id);
    if (!PushPath(L, "IdString64.from_hex")) { Fail("IdString64.from_hex unavailable"); return false; }
    L_.pushstring(L, hex);
    if (L_.pcall(L, 1, 1, 0) != 0) { Fail("IdString64.from_hex: %s", L_.tolstring(L, -1, NULL)); L_.settop(L, L_.gettop(L) - 1); return false; }
    return true;
}
/* A component of a temporary (Vector3.x on a Vector3, Quaternion.to_elements...): the value at `index` stays. */
static bool ReadVector3(lua_State* L, int index, float out[3])
{
    static const char* const names[3] = {"Vector3.x", "Vector3.y", "Vector3.z"};
    if (L_.type(L, index) == LUA_TNIL) { Fail("a Vector3 result is nil"); return false; }
    float values[3];
    for (int i = 0; i < 3; i++)
    {
        if (!PushPath(L, names[i])) { Fail("%s unavailable", names[i]); return false; }
        L_.pushvalue(L, index < 0 ? index - 1 : index);
        if (L_.pcall(L, 1, 1, 0) != 0) { Fail("%s: %s", names[i], L_.tolstring(L, -1, NULL)); L_.settop(L, L_.gettop(L) - 1); return false; }
        double value = L_.tonumber(L, -1);
        bool valid = L_.type(L, -1) == LUA_TNUMBER && isfinite(value) && fabs(value) <= FLT_MAX;
        L_.settop(L, L_.gettop(L) - 1);
        if (!valid) { Fail("%s returned a non-finite or non-number component", names[i]); return false; }
        values[i] = (float)value;
    }
    memcpy(out, values, sizeof values);
    return true;
}
static bool ReadQuaternion(lua_State* L, int index, float out[4])
{
    if (L_.type(L, index) == LUA_TNIL) { Fail("a Quaternion result is nil"); return false; }
    if (!PushPath(L, "Quaternion.to_elements")) { Fail("Quaternion.to_elements unavailable"); return false; }
    L_.pushvalue(L, index < 0 ? index - 1 : index);
    if (L_.pcall(L, 1, 4, 0) != 0) { Fail("Quaternion.to_elements: %s", L_.tolstring(L, -1, NULL)); L_.settop(L, L_.gettop(L) - 1); return false; }
    float values[4];
    for (int i = 0; i < 4; i++) {
        double value = L_.tonumber(L, -4 + i);
        if (L_.type(L, -4 + i) != LUA_TNUMBER || !isfinite(value) || fabs(value) > FLT_MAX) {
            Fail("Quaternion.to_elements returned an invalid component"); L_.settop(L, L_.gettop(L) - 4); return false;
        }
        values[i] = (float)value;
    }
    memcpy(out, values, sizeof values);
    L_.settop(L, L_.gettop(L) - 4);
    return true;
}

/* The shared body: the function is on the stack top; arguments and results per `sig`. */
static bool CallV(lua_State* L, const char* what, const char* sig, int prepushed, va_list args)
{
    int base = L_.gettop(L) - prepushed; /* the function is at base, then `prepushed` arguments already on the stack */
    s_countCalls++;
    const char* s = sig;
    int nargs = prepushed;
    for (; *s && *s != '>'; s++)
    {
        if (!L_.checkstack(L, 4)) { Fail("%s: stack", what); L_.settop(L, base - 1); return false; }
        switch (*s)
        {
        case 'n': L_.pushnumber(L, va_arg(args, double)); break;
        case 'i': L_.pushinteger(L, va_arg(args, int)); break;
        case 'b': L_.pushboolean(L, va_arg(args, int) != 0); break;
        case 's': { const char* text = va_arg(args, const char*); if (text) L_.pushstring(L, text); else L_.pushnil(L); break; }
        case 'r':
        {
            int index = RefIndex(va_arg(args, EngRef), what);
            if (index < 0) { L_.settop(L, base - 1); return false; }
            if (index) L_.rawgeti(L, LUA_REGISTRYINDEX, index); else L_.pushnil(L);
            break;
        }
        case 'v': if (!PushVector3(L, va_arg(args, const float*))) { L_.settop(L, base - 1); return false; } break;
        case 'q': if (!PushQuaternion(L, va_arg(args, const float*))) { L_.settop(L, base - 1); return false; } break;
        case 'u': if (!PushIdString(L, va_arg(args, uint64_t))) { L_.settop(L, base - 1); return false; } break;
        case 'N': L_.pushnil(L); break;
        case 'p': /* a property path: the one-string array {s} RenderDataComponent takes */
        {
            const char* key = va_arg(args, const char*);
            L_.createtable(L, 1, 0);
            if (key) L_.pushstring(L, key); else L_.pushnil(L);
            L_.rawseti(L, -2, 1);
            break;
        }
        default: Fail("%s: bad signature %s", what, sig); L_.settop(L, base - 1); return false;
        }
        nargs++;
    }
    const char* results = *s == '>' ? s + 1 : s;
    int nresults = (int)strlen(results);
    bool rest = nresults > 0 && results[nresults - 1] == 'R'; /* 'R' (last): every remaining result, packed in a table */
    if (rest) nresults--;
    if (nresults > 64 || !L_.checkstack(L, nresults + 8)) {
        Fail("%s: result stack unavailable", what); L_.settop(L, base - 1); return false;
    }
    if (L_.pcall(L, nargs, rest ? LUA_MULTRET : nresults, 0) != 0)
    {
        Fail("%s: %s", what, L_.tolstring(L, -1, NULL));
        L_.settop(L, base - 1);
        return false;
    }
    int first = base; /* results occupy base .. base + nresults - 1 */
    bool ok = true;
    struct { EngRef* out; EngRef value; } created[64];
    int ncreated = 0;
    EngRef packed = 0;
    int packed_index = 0;
    if (rest)
    {
        int total = L_.gettop(L) - base + 1;
        if (!L_.checkstack(L, nresults + 8)) { Fail("%s: result stack unavailable", what); L_.settop(L, base - 1); return false; }
        while (total < nresults) { L_.pushnil(L); total++; }
        int extra = total - nresults;
        L_.createtable(L, extra, 0);
        for (int i = 0; i < extra; i++) { L_.pushvalue(L, base + nresults + i); L_.rawseti(L, -2, i + 1); }
        packed_index = L_.ref(L, LUA_REGISTRYINDEX);
        packed = MakeRef(L, packed_index);
        if (packed == 0) { Fail("%s: the packed results could not be kept", what); L_.settop(L, base - 1); return false; }
    }
    for (int i = 0; i < nresults && ok; i++)
    {
        int at = first + i;
        char kind = results[i];
        switch (kind)
        {
        case 'n': {
            double* out = va_arg(args, double*);
            double value = L_.type(L, at) == LUA_TNUMBER ? L_.tonumber(L, at) : NAN;
            if (!isfinite(value)) { Fail("%s: numeric result %d is missing or non-finite", what, i + 1); ok = false; }
            else *out = value;
            break;
        }
        case 'i': {
            int* out = va_arg(args, int*);
            double value = L_.type(L, at) == LUA_TNUMBER ? L_.tonumber(L, at) : NAN;
            if (!isfinite(value) || value < INT_MIN || value > INT_MAX || trunc(value) != value) {
                Fail("%s: integer result %d is invalid", what, i + 1); ok = false;
            } else *out = (int)value;
            break;
        }
        case 'b': *va_arg(args, int*) = L_.toboolean(L, at); break;
        case 't': *va_arg(args, int*) = L_.type(L, at); break;
        case 'r':
        {
            EngRef* out = va_arg(args, EngRef*);
            if (L_.type(L, at) == LUA_TNIL) *out = 0;
            else
            {
                L_.pushvalue(L, at);
                int index = L_.ref(L, LUA_REGISTRYINDEX);
                *out = MakeRef(L, index);
                if (*out > 0) { created[ncreated].out = out; created[ncreated++].value = index; }
                else if (index > 0) ok = false; /* an index MakeRef refused (and released) */
            }
            break;
        }
        case 'v': ok = ReadVector3(L, at, va_arg(args, float*)); break;
        case 'q': ok = ReadQuaternion(L, at, va_arg(args, float*)); break;
        case 's':
        {
            char* buffer = va_arg(args, char*);
            size_t size = va_arg(args, size_t);
            size_t n = 0;
            const char* text = L_.type(L, at) == LUA_TSTRING ? L_.tolstring(L, at, &n) : NULL;
            if (buffer && size) { if (text) { if (n >= size) n = size - 1; memcpy(buffer, text, n); buffer[n] = 0; } else buffer[0] = 0; }
            break;
        }
        case '_': break;
        default: Fail("%s: bad result signature %s", what, sig); ok = false; break;
        }
    }
    if (rest)
    {
        if (ok) *va_arg(args, EngRef*) = packed; /* every fixed result was read, so this is the 'R' argument */
        else L_.unref(L, LUA_REGISTRYINDEX, packed_index);
    }
    if (!ok) for (int i = 0; i < ncreated; i++) {
        L_.unref(L, LUA_REGISTRYINDEX, created[i].value); *created[i].out = 0;
    }
    L_.settop(L, base - 1);
    return ok;
}

bool eng_callv(const char* path, const char* sig, va_list args)
{
    lua_State* L = g_state;
    if (L == NULL) { Fail("no Lua state"); return false; }
    /* a function, or a constructor table (Vector3, Vector4, Vector2, Color: called through their __call, as the
     * engine's own script calls them; a table that is not callable fails in the protected call). The stack goes back
     * to where it was on a failure: PushPath pushes nothing when the path does not resolve, and the engine's own values
     * below it are never ours to pop (as in eng_has and eng_cfunction). */
    int top = L_.gettop(L);
    if (!PushPath(L, path) || (L_.type(L, -1) != LUA_TFUNCTION && L_.type(L, -1) != LUA_TTABLE))
    {
        L_.settop(L, top);
        Fail("stingray.%s is not a function", path);
        return false;
    }
    return CallV(L, path, sig, 0, args);
}

bool eng_call(const char* path, const char* sig, ...)
{
    va_list args;
    va_start(args, sig);
    bool ok = eng_callv(path, sig, args);
    va_end(args);
    return ok;
}

/* Time policy only: no injected Lua source, and no process/code hook. Each closure
 * retains its own original function and serial, so an outer/retired wrapper remains
 * a valid delegate after detach or a later acquisition. */
static void (*s_policyObserver)(bool);
static uint64_t s_policySerial, s_policyGeneration;
static EngRef s_policyWrapper;
static void PolicyObserve(lua_State* L, bool accepted)
{
    /* Lua may invoke the installed closure on a coroutine. Registry identities
     * are shared, but nested bridge calls must use this callback's own stack. */
    lua_State* previous = g_state;
    g_state = L;
    s_policyObserver(accepted);
    g_state = previous;
}
static int PolicyWrapper(lua_State* L)
{
    int n = L_.gettop(L);
    if (!L_.checkstack(L, 8))
    {
        /* A C callback has LUA_MINSTACK on entry. Reuse it for a nonallocating
         * false receipt; never allocate/call through a failed capacity check. */
        L_.settop(L, 0); L_.pushboolean(L, 0); return 1;
    }
    double serial = L_.tonumber(L, LUA_GLOBALSINDEX - 2);
    bool external = false;
    for (int i = 1; i <= n; i++)
        if (L_.type(L, i) == LUA_TSTRING && strcmp(L_.tolstring(L, i, NULL), "external_multiplier") == 0) external = true;
    bool current = s_policyObserver && s_policyGeneration == eng_state_generation() && serial == (double)s_policySerial;
    if (external && current) PolicyObserve(L, false);
    L_.pushvalue(L, LUA_GLOBALSINDEX - 1); /* original upvalue */
    L_.insert(L, 1);
    if (L_.pcall(L, n, LUA_MULTRET, 0) != 0) return L_.error(L); /* preserve the original error */
    int results = L_.gettop(L);
    bool accepted = results == 0 || L_.type(L, 1) != LUA_TBOOLEAN || L_.toboolean(L, 1);
    if (external && current && accepted && s_policyObserver && serial == (double)s_policySerial) PolicyObserve(L, true);
    return results;
}

bool eng_policy_live(EngRef wrapper)
{
    if (!RefLive(wrapper) || !wrapper || !g_state) return false;
    lua_State* L = g_state;
    int top = L_.gettop(L);
    if (!L_.checkstack(L, 10)) return false;
    bool ok = PushPath(L, "Application.set_time_step_policy");
    if (ok) { L_.rawgeti(L, LUA_REGISTRYINDEX, RefIndex(wrapper, "time-policy binding")); ok = L_.rawequal(L, -1, -2) != 0; }
    L_.settop(L, top);
    return ok;
}

bool eng_policy_attach(void (*observer)(bool), EngRef* original, EngRef* wrapper)
{
    lua_State* L = g_state;
    if (!L || !observer || !L_.checkstack(L, 10)) return false;
    *original = *wrapper = 0;
    int top = L_.gettop(L);
    if (!PushPath(L, "Application") || L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, top); return false; }
    L_.pushstring(L, "set_time_step_policy"); L_.rawget(L, -2);
    if (L_.type(L, -1) != LUA_TFUNCTION) { L_.settop(L, top); return false; }
    L_.pushvalue(L, -1); *original = MakeRef(L, L_.ref(L, LUA_REGISTRYINDEX));
    if (!*original) { L_.settop(L, top); return false; }
    L_.pushnumber(L, (double)(s_policySerial + 1));
    L_.pushcclosure(L, PolicyWrapper, 2);
    L_.pushvalue(L, -1); *wrapper = MakeRef(L, L_.ref(L, LUA_REGISTRYINDEX));
    if (!*wrapper) { eng_unref(original); L_.settop(L, top); return false; }
    L_.pushstring(L, "set_time_step_policy"); L_.insert(L, -2); L_.rawset(L, -3);
    s_policySerial++; s_policyGeneration = eng_state_generation(); s_policyObserver = observer; s_policyWrapper = *wrapper;
    L_.settop(L, top); return true;
}

bool eng_policy_write(EngRef original, double value)
{
    if (!RefLive(original) || !original || !g_state) return false;
    lua_State* L = g_state;
    int top = L_.gettop(L);
    if (!L_.checkstack(L, 4)) { Fail("Time-policy Lua stack is full"); return false; }
    L_.rawgeti(L, LUA_REGISTRYINDEX, RefIndex(original, "time-policy original"));
    L_.pushstring(L, "external_multiplier"); L_.pushnumber(L, value);
    bool ok = L_.pcall(L, 2, 1, 0) == 0;
    if (!ok) Fail("Time restoration failed: %s", L_.tolstring(L, -1, NULL));
    else if (L_.type(L, -1) == LUA_TBOOLEAN && !L_.toboolean(L, -1)) { Fail("Time-policy setter refused"); ok = false; }
    L_.settop(L, top); return ok;
}

void eng_policy_detach(EngRef* original, EngRef* wrapper)
{
    if (eng_policy_live(*wrapper))
    {
        lua_State* L = g_state; int top = L_.gettop(L);
        if (L_.checkstack(L, 10) && PushPath(L, "Application"))
        { L_.pushstring(L, "set_time_step_policy"); L_.rawgeti(L, LUA_REGISTRYINDEX, RefIndex(*original, "time-policy original")); L_.rawset(L, -3); }
        L_.settop(L, top);
    }
    if (*wrapper == s_policyWrapper) { s_policyObserver = NULL; s_policyWrapper = 0; }
    eng_unref(original); eng_unref(wrapper);
}

bool eng_method(EngRef object, const char* method, const char* sig, ...)
{
    lua_State* L = g_state;
    if (L == NULL || object == 0) { Fail("no object for %s", method); return false; }
    int index = RefIndex(object, method);
    if (index < 0) return false;
    if (!L_.checkstack(L, 6)) { Fail("stack"); return false; }
    int top = L_.gettop(L);
    L_.pushcclosure(L, GetFieldSafe, 0);
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    L_.pushstring(L, method);
    if (L_.pcall(L, 2, 1, 0) != 0 || L_.type(L, -1) != LUA_TFUNCTION)
    {
        Fail("%s: no such method (%s)", method, L_.type(L, -1) == LUA_TSTRING ? L_.tolstring(L, -1, NULL) : "");
        L_.settop(L, top);
        return false;
    }
    L_.rawgeti(L, LUA_REGISTRYINDEX, index); /* self, the first argument */
    va_list args;
    va_start(args, sig);
    bool ok = CallV(L, method, sig, 1, args);
    va_end(args);
    return ok;
}

void eng_unref(EngRef* ref)
{
    lua_State* L = g_state;
    if (ref == NULL || *ref == 0) return;
    /* a reference from a replaced state died with it: forget it, never free its index in the new registry */
    if (L != NULL && *ref > 0 && RefLive(*ref)) L_.unref(L, LUA_REGISTRYINDEX, *ref & REF_INDEX_MASK);
    *ref = 0;
}

bool eng_same(EngRef a, EngRef b)
{
    lua_State* L = g_state;
    if (L == NULL || a == 0 || b == 0) return a == b;
    if (a == b) return true;
    int ia = RefIndex(a, "eng_same"), ib = RefIndex(b, "eng_same");
    if (ia <= 0 || ib <= 0) return false;
    if (!L_.checkstack(L, 2)) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, ia);
    L_.rawgeti(L, LUA_REGISTRYINDEX, ib);
    bool same = L_.rawequal(L, -1, -2) != 0;
    L_.settop(L, L_.gettop(L) - 2);
    return same;
}

uintptr_t eng_pointer(EngRef ref)
{
    lua_State* L = g_state;
    if (L == NULL || ref == 0) return 0;
    int index = RefIndex(ref, "eng_pointer");
    if (index <= 0) return 0;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    int type = L_.type(L, -1);
    uintptr_t p = 0;
    if (type == LUA_TLIGHTUSERDATA || type == LUA_TUSERDATA) p = (uintptr_t)L_.touserdata(L, -1);
    else if (type == LUA_TTABLE || type == LUA_TFUNCTION || type == LUA_TTHREAD) p = (uintptr_t)L_.topointer(L, -1);
    L_.settop(L, L_.gettop(L) - 1);
    return p;
}

EngRef eng_copy(EngRef ref)
{
    lua_State* L = g_state;
    if (L == NULL || ref == 0) return 0;
    int index = RefIndex(ref, "eng_copy");
    if (index <= 0) return 0;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    return MakeRef(L, L_.ref(L, LUA_REGISTRYINDEX));
}

int eng_len(EngRef table)
{
    lua_State* L = g_state;
    if (L == NULL || table == 0) return 0;
    int index = RefIndex(table, "eng_len");
    if (index <= 0) return 0;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    int n = L_.type(L, -1) == LUA_TTABLE ? (int)L_.objlen(L, -1) : 0;
    L_.settop(L, L_.gettop(L) - 1);
    return n;
}

int eng_entries(EngRef table, double* keys, EngRef* values, int max)
{
    lua_State* L = g_state;
    if (L == NULL || table == 0 || max < 0) return -1;
    int index = RefIndex(table, "eng_entries");
    if (index <= 0) return -1;
    if (!L_.checkstack(L, 4)) return -1;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return -1; }
    int n = 0;
    L_.pushnil(L);
    while (L_.next(L, -2) != 0)
    {
        if (n == max) { L_.settop(L, L_.gettop(L) - 3); return max + 1; } /* value, key, table */
        keys[n] = L_.type(L, -2) == LUA_TNUMBER ? L_.tonumber(L, -2) : NAN;
        if (L_.type(L, -1) != LUA_TNIL) { L_.pushvalue(L, -1); values[n] = MakeRef(L, L_.ref(L, LUA_REGISTRYINDEX)); }
        else values[n] = 0;
        n++;
        L_.settop(L, L_.gettop(L) - 1);
    }
    L_.settop(L, L_.gettop(L) - 1);
    return n;
}
bool eng_index(EngRef table, int i, EngRef* out)
{
    lua_State* L = g_state;
    *out = 0;
    if (L == NULL || table == 0) return false;
    int index = RefIndex(table, "eng_index");
    if (index <= 0) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return false; }
    L_.rawgeti(L, -1, i);
    if (L_.type(L, -1) != LUA_TNIL) *out = MakeRef(L, L_.ref(L, LUA_REGISTRYINDEX)); else L_.settop(L, L_.gettop(L) - 1);
    L_.settop(L, L_.gettop(L) - 1);
    return true;
}

int eng_userdata_array(EngRef table, uintptr_t* out, int max)
{
    lua_State* L = g_state;
    if (L == NULL || table == 0 || max < 0) return -1;
    int index = RefIndex(table, "eng_userdata_array");
    if (index <= 0) return -1;
    if (!L_.checkstack(L, 4)) return -1;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return -1; }
    int n = (int)L_.objlen(L, -1);
    for (int i = 0; i < n && i < max; i++)
    {
        L_.rawgeti(L, -1, i + 1);
        int type = L_.type(L, -1);
        out[i] = type == LUA_TLIGHTUSERDATA || type == LUA_TUSERDATA ? (uintptr_t)L_.touserdata(L, -1) : 0;
        L_.settop(L, L_.gettop(L) - 1);
    }
    L_.settop(L, L_.gettop(L) - 1);
    return n;
}

bool eng_pairs_count(EngRef table, int* count)
{
    lua_State* L = g_state;
    *count = 0;
    if (L == NULL || table == 0) return false;
    int index = RefIndex(table, "eng_pairs_count");
    if (index <= 0) return false;
    if (!L_.checkstack(L, 4)) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return false; }
    L_.pushnil(L);
    while (L_.next(L, -2) != 0) { (*count)++; L_.settop(L, L_.gettop(L) - 1); }
    L_.settop(L, L_.gettop(L) - 1);
    return true;
}

bool eng_field(EngRef table, const char* key, EngRef* out)
{
    lua_State* L = g_state;
    *out = 0;
    if (L == NULL || table == 0) return false;
    int index = RefIndex(table, "eng_field");
    if (index <= 0) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return false; }
    L_.pushstring(L, key);
    L_.rawget(L, -2);
    if (L_.type(L, -1) != LUA_TNIL) *out = MakeRef(L, L_.ref(L, LUA_REGISTRYINDEX)); else L_.settop(L, L_.gettop(L) - 1);
    L_.settop(L, L_.gettop(L) - 1);
    return true;
}

bool eng_number_field(EngRef table, const char* key, double* out)
{
    lua_State* L = g_state;
    *out = 0;
    if (L == NULL || table == 0) return false;
    int index = RefIndex(table, "eng_number_field");
    if (index <= 0) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    if (L_.type(L, -1) != LUA_TTABLE) { L_.settop(L, L_.gettop(L) - 1); return false; }
    L_.pushstring(L, key);
    L_.rawget(L, -2);
    bool number = L_.type(L, -1) == LUA_TNUMBER;
    if (number) *out = L_.tonumber(L, -1);
    L_.settop(L, L_.gettop(L) - 2);
    return number;
}

/* ---- a reference's own value ------------------------------------------------------------------------------------ */

int eng_type(EngRef ref)
{
    lua_State* L = g_state;
    if (ref == 0) return LUA_TNIL;
    if (L == NULL) return -1;
    int index = RefIndex(ref, "eng_type");
    if (index <= 0) return -1;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    int type = L_.type(L, -1);
    L_.settop(L, L_.gettop(L) - 1);
    return type;
}

bool eng_tonumber(EngRef ref, double* out)
{
    lua_State* L = g_state;
    if (L == NULL || ref == 0) return false;
    int index = RefIndex(ref, "eng_tonumber");
    if (index <= 0) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    bool number = L_.type(L, -1) == LUA_TNUMBER;
    if (number) *out = L_.tonumber(L, -1);
    L_.settop(L, L_.gettop(L) - 1);
    return number;
}

bool eng_toboolean(EngRef ref, bool* out)
{
    lua_State* L = g_state;
    if (L == NULL || ref == 0) return false;
    int index = RefIndex(ref, "eng_toboolean");
    if (index <= 0) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    bool boolean = L_.type(L, -1) == LUA_TBOOLEAN;
    if (boolean) *out = L_.toboolean(L, -1) != 0;
    L_.settop(L, L_.gettop(L) - 1);
    return boolean;
}

bool eng_tostring(EngRef ref, char* out, size_t size)
{
    lua_State* L = g_state;
    if (L == NULL || ref == 0 || size == 0) return false;
    int index = RefIndex(ref, "eng_tostring");
    if (index <= 0) return false;
    L_.rawgeti(L, LUA_REGISTRYINDEX, index);
    size_t n = 0;
    const char* text = L_.type(L, -1) == LUA_TSTRING ? L_.tolstring(L, -1, &n) : NULL;
    if (text) { if (n >= size) n = size - 1; memcpy(out, text, n); out[n] = 0; }
    L_.settop(L, L_.gettop(L) - 1);
    return text != NULL;
}

/* ---- temporaries ------------------------------------------------------------------------------------------------ */

static double s_tempCounts[8];
static int s_tempCount = -1;
static bool s_tempBytes;

void eng_temp_begin(void)
{
    lua_State* L = g_state;
    s_tempCount = -1;
    if (L == NULL) return;
    const char* getter = eng_has("Script.temp_count") ? "Script.temp_count" : eng_has("Script.temp_byte_count") ? "Script.temp_byte_count" : NULL;
    if (getter == NULL) return;
    s_tempBytes = getter[7] == 't' && getter[12] == 'b';
    int top = L_.gettop(L);
    if (!PushPath(L, getter) || L_.pcall(L, 0, LUA_MULTRET, 0) != 0) { L_.settop(L, top); return; }
    int n = L_.gettop(L) - top;
    if (n < 1 || n > 8) { L_.settop(L, top); return; }
    for (int i = 0; i < n; i++)
    {
        if (L_.type(L, top + 1 + i) != LUA_TNUMBER) { L_.settop(L, top); return; }
        s_tempCounts[i] = L_.tonumber(L, top + 1 + i);
    }
    s_tempCount = n;
    L_.settop(L, top);
}

void eng_temp_end(void)
{
    lua_State* L = g_state;
    if (L == NULL || s_tempCount < 0) return;
    const char* setter = s_tempBytes ? "Script.set_temp_byte_count" : "Script.set_temp_count";
    int top = L_.gettop(L);
    if (PushPath(L, setter))
    {
        for (int i = 0; i < s_tempCount; i++) L_.pushnumber(L, s_tempCounts[i]);
        if (L_.pcall(L, s_tempCount, 0, 0) != 0) Log("temporaries could not be released: %s", L_.tolstring(L, -1, NULL));
    }
    L_.settop(L, top);
    s_tempCount = -1;
}

/* ---- matrices --------------------------------------------------------------------------------------------------- */

bool eng_box_matrix(EngRef matrix, EngRef* box) { return eng_call("Matrix4x4Box", "r>r", matrix, box); }
bool eng_unbox_matrix(EngRef box, EngRef* matrix) { return eng_method(box, "unbox", ">r", matrix); }

bool eng_matrix_basis(EngRef matrix, float position[3], float forward[3], float right[3], float up[3])
{
    if (position && !eng_call("Matrix4x4.translation", "r>v", matrix, position)) return false;
    if (forward && !eng_call("Matrix4x4.forward", "r>v", matrix, forward)) return false;
    if (right && !eng_call("Matrix4x4.right", "r>v", matrix, right)) return false;
    if (up && !eng_call("Matrix4x4.up", "r>v", matrix, up)) return false;
    return true;
}

/* Bounded read-only SDK discovery: raw traversal only, protected by the game's
 * Lua C API. This does not execute any function discovered in the SDK. */
typedef struct SdkCensusContext {
    EngSdkVisitor visitor;void *user;unsigned maximum;bool ok;
    EngSdkCensus stats;const void *visited[1024];unsigned visited_count;
} SdkCensusContext;
static SdkCensusContext *sdk_census_context;
static bool SdkCensusTable(lua_State *L,int table,const char *prefix,unsigned depth) {
    SdkCensusContext *c=sdk_census_context;
    if(!L_.checkstack(L,8)){c->ok=false;return false;}
    int top=L_.gettop(L);table=table<0?top+table+1:table;
    L_.pushnil(L);
    while(L_.next(L,table)) {
        if(c->stats.rows==c->maximum){c->stats.limit_reached=true;L_.settop(L,top);return false;}
        char key[128],path[512];size_t length=0;
        if(L_.type(L,-2)!=LUA_TSTRING){c->stats.omitted++;L_.settop(L,L_.gettop(L)-1);continue;}
        const char *raw=L_.tolstring(L,-2,&length);
        bool safe=raw&&length&&length<sizeof key;
        for(size_t i=0;safe&&i<length;i++)if((unsigned char)raw[i]<32||raw[i]==127||raw[i]=='.')safe=false;
        if(!safe){c->stats.omitted++;L_.settop(L,L_.gettop(L)-1);continue;}
        memcpy(key,raw,length);key[length]=0;
        int used=snprintf(path,sizeof path,"%s%s%s",prefix,*prefix?".":"",key);
        if(used<0||(size_t)used>=sizeof path){c->stats.omitted++;L_.settop(L,L_.gettop(L)-1);continue;}
        int type=L_.type(L,-1);bool alias=false;uintptr_t function=0;
        if(type==LUA_TFUNCTION)function=(uintptr_t)(void *)L_.tocfunction(L,-1);
        if(type==LUA_TTABLE){
            const void *identity=L_.topointer(L,-1);
            for(unsigned i=0;i<c->visited_count;i++)if(c->visited[i]==identity){alias=true;break;}
            if(alias)c->stats.aliases++;
            else if(c->visited_count==1024){c->stats.depth_limited++;alias=true;}
            else {c->visited[c->visited_count++]=identity;c->stats.tables++;}
        }
        c->stats.rows++;
        if(!c->visitor(c->user,path,type,function,alias)){c->ok=false;L_.settop(L,top);return false;}
        if(type==LUA_TTABLE&&!alias){
            if(depth>=8)c->stats.depth_limited++;
            else if(!SdkCensusTable(L,L_.gettop(L),path,depth+1)){L_.settop(L,top);return false;}
        }
        L_.settop(L,L_.gettop(L)-1);
    }
    L_.settop(L,top);return true;
}
static int SdkCensusProtected(lua_State *L) {
    SdkCensusContext *c=sdk_census_context;
    L_.pushstring(L,"stingray");L_.rawget(L,LUA_GLOBALSINDEX);
    if(L_.type(L,-1)!=LUA_TTABLE){c->ok=false;return 0;}
    c->visited[c->visited_count++]=L_.topointer(L,-1);c->stats.tables=1;
    SdkCensusTable(L,L_.gettop(L),"",0);return 0;
}
bool eng_sdk_census(EngSdkVisitor visitor,void *user,unsigned maximum,EngSdkCensus *out) {
    if(out)memset(out,0,sizeof *out);
    lua_State *L=g_state;uint64_t generation=eng_state_generation();
    if(!L||!out||!visitor||!maximum||maximum>32768||sdk_census_context||!eng_ready()||!L_.checkstack(L,4))return false;
    SdkCensusContext context={.visitor=visitor,.user=user,.maximum=maximum,.ok=true};
    int top=L_.gettop(L);sdk_census_context=&context;
    L_.pushcclosure(L,SdkCensusProtected,0);
    int status=L_.pcall(L,0,0,0);sdk_census_context=NULL;
    L_.settop(L,top);
    context.stats.complete=status==0&&context.ok&&!context.stats.limit_reached&&
        !context.stats.omitted&&!context.stats.depth_limited;
    *out=context.stats;
    return status==0&&context.ok&&g_state==L&&eng_state_generation()==generation;
}
