/* Drives the verified bridge (engine.c) against the game's real lua51.dll: argv[1] is its path.
 * Lua 5.1/LuaJIT closes the whole VM when lua_close is given a coroutine, so the bridge must retire its captured state
 * for a close through any of that VM's coroutines, and leave it alone for an unrelated VM. */
#include <assert.h>
#include <stdio.h>
#include "../src/engine.c"
typedef lua_State* (*NewState)(void);
typedef lua_State* (*NewThread)(lua_State*);
static int native_sdk(lua_State *L){(void)L;return 0;}
static uintptr_t module(const char *name,size_t *size){(void)name;*size=0x39e8000;return (uintptr_t)(void*)native_sdk-0x4128e0;}
static void prepare(lua_State *L){
 L_.pushstring(L,"stingray");L_.createtable(L,0,1);
 L_.pushstring(L,"Unit");L_.createtable(L,0,1);
 L_.pushstring(L,"set_mesh_visibility");L_.pushcclosure(L,native_sdk,0);L_.rawset(L,-3);L_.rawset(L,-3);L_.rawset(L,LUA_GLOBALSINDEX);
}
static void log_message(HfMod* mod,const char* format,...){(void)mod;(void)format;}
static int hook(HfMod* mod,uintptr_t target,void* detour,void** original){(void)mod;(void)detour;*original=(void*)target;return 0;}
int main(int argc,char** argv){
 assert(argc==2);HMODULE lua=LoadLibraryA(argv[1]);assert(lua);
 HfApi hf={0};hf.hook=hook;hf.log=log_message;hf.module_base=module;assert(eng_init(&hf,NULL)); /* s_close/s_pcall are the real exports */
 FARPROC f=GetProcAddress(lua,"luaL_newstate");NewState newstate;memcpy(&newstate,&f,sizeof f);assert(newstate);
 f=GetProcAddress(lua,"lua_newthread");NewThread newthread;memcpy(&newthread,&f,sizeof f);assert(newthread);
 lua_State* vm=newstate();assert(vm);lua_State* other=newstate();assert(other&&other!=vm);
 lua_State* co=newthread(vm);assert(co&&co!=vm);L_.settop(vm,0);
 prepare(vm);Capture(vm);assert(g_state==vm&&g_stateGeneration==1);
 Capture(co);assert(g_state==vm&&g_stateGeneration==1); /* a coroutine is never the engine's state */
 CloseHook(other);assert(g_state==vm&&g_stateGeneration==1); /* unrelated VM: capture preserved (and that VM is really closed) */
 CloseHook(co);assert(!g_state&&!g_registryIdentity&&g_stateGeneration==2); /* closing the main VM through its coroutine retires the capture */
 puts("C engine: real lua51 VM: coroutine close retires the captured state; unrelated VM close preserves it");return 0;
}
