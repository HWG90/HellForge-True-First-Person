/* Include the bridge to exercise its private capture boundary with a fake Lua stack.
 * No game, real Lua VM, hooks, or engine calls are used. */
#include <assert.h>
#include <stdio.h>
#include "../src/engine.c"
static void log_message(HfMod *mod,const char *format,...){(void)mod;(void)format;}
static int top,can_grow,is_main,pushes,pops,closed;static bool native_sdk=true;
static uintptr_t module(const char *name,size_t *size){(void)name;*size=0x39e8000;return 0x10000000;}
static void push_string(lua_State *L,const char *text){(void)L;(void)text;top++;}
static void raw_get(lua_State *L,int at){(void)L;(void)at;}
static int type_of(lua_State *L,int at){(void)L;(void)at;return top==10?LUA_TFUNCTION:LUA_TTABLE;}
static lua_CFunction function_of(lua_State *L,int at){(void)L;(void)at;return (lua_CFunction)(uintptr_t)(native_sdk?0x104128e0:0x70000000);}

static int check(lua_State* state,int n){(void)state;(void)n;return can_grow;}
static int push(lua_State* state){(void)state;top++;pushes++;return is_main;}
static int gettop(lua_State* state){(void)state;return top;}
static void settop(lua_State* state,int n){(void)state;top=n;pops++;}
static void close_state(lua_State* state){(void)state;closed++;}
/* The verified bridge identifies a VM by its registry: main and its coroutine share one, an unrelated VM has another. */
static const void* topointer(lua_State* state,int index){(void)index;return (uintptr_t)state==3?(const void*)0x200:(const void*)0x100;}
int main(void){
 HfApi api={0};api.module_base=module;api.log=log_message;g_hf=&api;L_.pushstring=push_string;L_.rawget=raw_get;L_.type=type_of;L_.tocfunction=function_of;
 L_.checkstack=check;L_.pushthread=push;L_.gettop=gettop;L_.settop=settop;s_close=close_state;L_.topointer=topointer;
 lua_State* main=(lua_State*)1;lua_State* coroutine=(lua_State*)2;lua_State* unrelated=(lua_State*)3;
 top=7;can_grow=0;Capture(main);assert(top==7&&!pushes&&!pops&&!g_state); /* formerly popped argument 7 */
 can_grow=1;is_main=0;Capture(coroutine);assert(top==7&&pushes==1&&pops==1&&!g_state);
 is_main=1;native_sdk=false;Capture(main);assert(top==7&&!g_state);native_sdk=true;Capture(main);assert(top==7&&g_state==main&&g_stateGeneration==1);
 int prior=pushes;Capture(main);assert(top==7&&pushes==prior);
 CloseHook(unrelated);assert(closed==1&&g_state==main&&g_stateGeneration==1); /* an unrelated VM's close leaves the capture alone */
 CloseHook(coroutine);assert(closed==2&&!g_state&&g_stateGeneration==2); /* a coroutine close takes the whole VM (it shares the registry) */
 is_main=1;Capture(main);assert(g_state==main&&g_stateGeneration==3);
 CloseHook(main);assert(closed==3&&!g_state&&g_stateGeneration==4);
 Capture(NULL);assert(top==7);
 puts("C engine: engine capture preserves caller stack on allocation failure, rejects coroutines and retires closed states");return 0;
}
