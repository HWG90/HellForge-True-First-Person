#include "../src/engine.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef lua_State *(*NewState)(void);
static unsigned sdk_calls, visited;
static bool abort_visit, reenter;
static int native_sdk(lua_State *L) {
  (void)L;
  sdk_calls++;
  return 0;
}
static uintptr_t module(const char *name, size_t *size) {
  (void)name;
  *size = 0x39e8000;
  return (uintptr_t)(void *)native_sdk - 0x4128e0;
}
static void log_message(HfMod *mod, const char *format, ...) {
  (void)mod;
  (void)format;
}
static int hook(HfMod *mod, uintptr_t target, void *detour, void **original) {
  (void)mod;
  (void)detour;
  *original = (void *)target;
  return 0;
}
static bool visit(void *user, const char *path, int type, uintptr_t function,
                  bool alias) {
  (void)user;
  (void)alias;
  assert(path && *path && strlen(path) < 512);
  visited++;
  if (type == LUA_TFUNCTION)
    assert(function == (uintptr_t)(void *)native_sdk);
  if (reenter) {
    reenter = false;
    EngSdkCensus nested;
    assert(!eng_sdk_census(visit, NULL, 32, &nested));
  }
  return !abort_visit;
}
static void prepare(lua_State *L) {
  L_.pushstring(L, "stingray");
  L_.createtable(L, 0, 5);
  L_.pushstring(L, "Unit");
  L_.createtable(L, 0, 3);
  L_.pushstring(L, "set_mesh_visibility");
  L_.pushcclosure(L, native_sdk, 0);
  L_.rawset(L, -3);
  L_.pushstring(L, "self");
  L_.pushvalue(L, -2);
  L_.rawset(L, -3);
  L_.rawset(L, -3);
  L_.pushstring(L, "Application");
  L_.createtable(L, 0, 1);
  L_.pushstring(L, "render_world");
  L_.pushcclosure(L, native_sdk, 0);
  L_.rawset(L, -3);
  L_.rawset(L, -3);
  L_.pushstring(L, "Alias");
  L_.pushstring(L, "Unit");
  L_.rawget(L, -3);
  L_.rawset(L, -3);
  L_.rawset(L, LUA_GLOBALSINDEX);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  HMODULE lua = LoadLibraryA(argv[1]);
  assert(lua);
  HfApi hf = {0};
  hf.hook = hook;
  hf.log = log_message;
  hf.module_base = module;
  assert(eng_init(&hf, NULL));
  FARPROC f = GetProcAddress(lua, "luaL_newstate");
  NewState create;
  memcpy(&create, &f, sizeof f);
  assert(create);
  lua_State *vm = create();
  assert(vm);
  prepare(vm);
  Capture(vm);
  assert(g_state == vm && eng_ready());
  L_.pushnumber(vm, 42);
  int top = L_.gettop(vm);
  EngSdkCensus stats;
  reenter = true;
  assert(eng_sdk_census(visit, NULL, 32768, &stats) && stats.complete &&
         stats.rows == visited && stats.aliases >= 2 && stats.tables == 3);
  assert(!sdk_calls && L_.gettop(vm) == top && L_.tonumber(vm, -1) == 42 &&
         !sdk_census_context);
  assert(eng_sdk_census(visit, NULL, 1, &stats) && stats.limit_reached &&
         !stats.complete && L_.gettop(vm) == top);
  abort_visit = true;
  assert(!eng_sdk_census(visit, NULL, 100, &stats) && L_.gettop(vm) == top &&
         !sdk_census_context);
  abort_visit = false;
  L_.pushstring(vm, "stingray");
  L_.rawget(vm, LUA_GLOBALSINDEX);
  L_.pushinteger(vm, 7);
  L_.pushcclosure(vm, native_sdk, 0);
  L_.rawset(vm, -3);
  L_.settop(vm, top);
  assert(eng_sdk_census(visit, NULL, 32768, &stats) && stats.omitted == 1 &&
         !stats.complete && L_.gettop(vm) == top && !sdk_calls);
  CloseHook(vm);
  assert(!eng_sdk_census(visit, NULL, 100, &stats));
  puts("C SDK census: real Lua VM, raw table aliases/cycles, bounded output, "
       "no SDK calls, reentry/visitor failure, numeric-key omission, "
       "caller-stack preservation and closed-VM refusal passed");
  return 0;
}
